#!/usr/bin/env python3
"""Match a built SDK archive to physical RT/Phone validation before publishing."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import tarfile
import posixpath

RUNTIME = {
    'rt': ('flutter_windows.dll', 'libEGL.dll', 'libGLESv2.dll'),
    'phone': ('flutter_engine.dll', 'libEGL.dll', 'libGLESv2.dll', 'flutter_winrt_compat.dll'),
}
CHECKS = {
    'rt': {'startup', 'send', 'receive'},
    'phone': {'startup', 'touch_alignment', 'folder_picker', 'url_launcher', 'send', 'receive'},
}
SYSTEM_DLLS = {'kernel32.dll', 'kernelbase.dll', 'ntdll.dll', 'user32.dll', 'gdi32.dll',
               'ucrtbase.dll', 'msvcrt.dll', 'd3d11.dll', 'dxgi.dll', 'combase.dll',
               'ole32.dll', 'oleaut32.dll', 'shell32.dll', 'runtimeobject.dll',
               'advapi32.dll', 'ws2_32.dll', 'bcrypt.dll', 'ncrypt.dll', 'crypt32.dll',
               'shlwapi.dll', 'propsys.dll', 'winhttp.dll', 'wininet.dll'}


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def verify(archive, validation, run_id, revision):
    proof = json.loads(validation.read_text())
    if proof.get('schemaVersion') != 1 or not re.fullmatch(r'[0-9a-f]{40}', revision):
        raise ValueError('Unsupported physical validation schema or source revision')
    actual_digest = digest(archive)
    if (str(proof.get('buildRunId')) != str(run_id) or proof.get('sourceRevision') != revision or
            proof.get('sdkArchiveSha256') != actual_digest):
        raise ValueError('Physical validation refers to a different SDK build')
    required = {}
    prefix = 'flutter/bin/cache/artifacts/engine/windows-arm-release/'
    for family, names in RUNTIME.items():
        device = proof['devices'][family]
        if (device.get('hardwareAccelerated') is not True or device.get('softwareAdapter') is not False or
                not isinstance(device.get('adapterName'), str) or not device['adapterName'].strip()):
            raise ValueError('Missing physical hardware GPU evidence: ' + family)
        if not CHECKS[family].issubset(set(device.get('verifiedChecks', []))):
            raise ValueError('Incomplete physical application checks: ' + family)
        for name in names:
            expected = device['runtimeSha256'][name]
            if not re.fullmatch(r'[0-9a-f]{64}', expected):
                raise ValueError('Invalid physical runtime digest: ' + name)
            required[prefix + ('phone/' if family == 'phone' else '') + name] = expected
    metadata_path = prefix + 'sdk-assembly.json'
    seen = set()
    matched = {}
    metadata = None
    with tarfile.open(archive, 'r|gz') as source:
        for member in source:
            name = PurePosixPath(member.name)
            if name.is_absolute() or '..' in name.parts or member.name in seen:
                raise ValueError('Unsafe or duplicate SDK archive member')
            seen.add(member.name)
            if member.issym() or member.islnk():
                target = member.linkname if member.islnk() else posixpath.join(str(name.parent), member.linkname)
                target = posixpath.normpath(target)
                if target != 'flutter' and not target.startswith('flutter/'):
                    raise ValueError('SDK archive link escapes its installation directory')
            lower = name.name.lower()
            if lower in SYSTEM_DLLS or (lower.endswith('.dll') and lower.startswith(('api-ms-', 'ext-ms-', 'd3dcompiler_'))):
                raise ValueError('SDK archive contains an OS library: ' + member.name)
            if member.name not in required and member.name != metadata_path:
                continue
            if not member.isfile() or member.size > 128 * 1024 * 1024:
                raise ValueError('Invalid SDK runtime or validation metadata member')
            content = source.extractfile(member).read()
            if member.name == metadata_path:
                metadata = json.loads(content)
            else:
                if content[:2] != b'MZ' or len(content) < 64:
                    raise ValueError('Invalid physical runtime PE image')
                pe = int.from_bytes(content[60:64], 'little')
                if content[pe:pe + 6] != b'PE\0\0\xc4\x01':
                    raise ValueError('SDK runtime is not Windows ARM32')
                matched[member.name] = hashlib.sha256(content).hexdigest()
    if matched != required:
        raise ValueError('SDK runtime bytes differ from physical validation')
    if not metadata or not all(metadata.get(key) is True for key in (
            'bothRuntimeFamiliesInstalled', 'sdkDependencyPathsPreflightPassed',
            'applicationBuildVerified', 'relocatedSdkBuildVerified')):
        raise ValueError('Missing complete SDK build and relocation validation')
    return {'schemaVersion': 1, 'buildRunId': str(run_id), 'sourceRevision': revision,
            'sdkArchiveSha256': actual_digest, 'physicalValidationRecordSha256': digest(validation),
            'physicalRuntimeDigests': matched, 'hardwareGpuValidatedForBothFamilies': True,
            'systemLibrariesCopied': False, 'releaseReady': True}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--validation-record', type=Path, required=True)
    parser.add_argument('--build-run-id', required=True)
    parser.add_argument('--source-revision', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = verify(args.archive, args.validation_record, args.build_run_id, args.source_revision)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
