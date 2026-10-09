#!/usr/bin/env python3
"""Match a built SDK archive to physical RT/Phone validation before publishing."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import tarfile
import posixpath
import stat
import zipfile

from toolchain.windows_native_dependencies import system_library

RUNTIME = {
    'rt': ('flutter_windows.dll', 'libEGL.dll', 'libGLESv2.dll'),
    'phone': ('flutter_engine.dll', 'libEGL.dll', 'libGLESv2.dll', 'flutter_winrt_compat.dll'),
}
CHECKS = {
    'rt': {'startup', 'send', 'receive'},
    'phone': {
        'startup', 'touch_alignment', 'safe_area', 'system_bars',
        'folder_picker', 'url_launcher', 'send', 'receive', 'theme', 'accent_color',
        'continuum_capture', 'continuum_hover', 'continuum_scroll',
        'continuum_secondary_button', 'continuum_resize', 'suspend_resume',
        'fcm_foreground', 'fcm_background', 'fcm_cold_start',
        'fcm_initial_message', 'fcm_notification_tap', 'fcm_token_lifecycle',
        'fcm_topics', 'fcm_notification_settings', 'fcm_migration',
    },
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


def verified_macos_builds(proof, bootstrap, cross, archive_sha, revision):
    """Accept later native tests of the exact, unchanged cross-built archive."""
    if not isinstance(bootstrap, dict) or not isinstance(cross, dict):
        return False
    host = (bootstrap or {}).get('sdkHost')
    if not isinstance(host, str):
        return False
    architecture = {'macos-x64': 'x86_64', 'macos-arm64': 'arm64'}.get(host)
    native = proof.get('nativeSdkValidation', {})
    if not isinstance(native, dict):
        return False
    if (not architecture or not cross or cross.get('host') != host
            or cross.get('nativeDestinationExecutionVerified') is not False
            or cross.get('relocatedApplicationBuildVerified') is not False
            or bootstrap.get('sourceRevision') != revision
            or native.get('schemaVersion') != 1 or native.get('sdkHost') != host
            or native.get('sourceRevision') != revision
            or native.get('sdkArchiveSha256') != archive_sha
            or native.get('executionSystem') != 'Darwin'
            or native.get('executionArchitecture') != architecture
            or native.get('applicationCommit') != '5ccc6dea192d1c697c2602bf456b2eb2ad8e9674'
            or not all(native.get(key) is True for key in (
                'applicationSourcesUnmodified', 'sdkSourcesUnmodified',
                'nativeDependencyPathsPreflightPassed',
                'originalSdkPathUnavailableAfterRelocation'))):
        return False
    builds = native.get('builds', [])
    required = {(phase, family) for phase in ('initial', 'relocated') for family in ('rt', 'phone')}
    if not isinstance(builds, list) or len(builds) != len(required):
        return False
    observed = set()
    for build in builds:
        if not isinstance(build, dict):
            return False
        identity = (build.get('phase'), build.get('family'))
        if not all(isinstance(item, str) for item in identity):
            return False
        command = ['flutter', 'build', 'windows', '--release', '--target-platform', 'windows-arm']
        if identity[1] == 'phone':
            command += ['--windows-family', 'phone']
        if (identity not in required or identity in observed
                or type(build.get('exitCode')) is not int or build['exitCode'] != 0
                or build.get('command') not in (command, command + ['--no-pub'])
                or not isinstance(build.get('logSha256'), str)
                or not re.fullmatch(r'[0-9a-f]{64}', build['logSha256'])):
            return False
        observed.add(identity)
    return observed == required


def archive_members(archive):
    """Read SDK tar or zip members without extracting into the host filesystem."""
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as source:
            for member in source.infolist():
                mode = member.external_attr >> 16
                link = stat.S_ISLNK(mode)
                target = None
                if link:
                    if member.file_size > 4096:
                        raise ValueError('Invalid SDK archive symlink')
                    target = source.read(member).decode('utf-8')
                regular = not member.is_dir() and not link and stat.S_IFMT(mode) in (0, stat.S_IFREG)
                yield (member.filename, target, regular, member.file_size,
                       lambda item=member: source.read(item))
    else:
        with tarfile.open(archive, 'r|*') as source:
            for member in source:
                target = None
                if member.issym():
                    target = member.linkname
                elif member.islnk():
                    # Hard-link targets in tar are archive-root relative.
                    target = posixpath.relpath(member.linkname, str(PurePosixPath(member.name).parent))
                yield (member.name, target, member.isfile(), member.size,
                       lambda item=member: source.extractfile(item).read())


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
    bootstrap_path = 'flutter/bin/cache/windows-arm32-host-bootstrap.json'
    cross_path = 'flutter/bin/cache/windows-arm32-cross-preparation.json'
    seen = set()
    matched = {}
    metadata = None
    auxiliary = {}
    for member_name, link, regular, size, read in archive_members(archive):
        name = PurePosixPath(member_name)
        if name.is_absolute() or '..' in name.parts or '\\' in member_name or member_name in seen:
            raise ValueError('Unsafe or duplicate SDK archive member')
        seen.add(member_name)
        if link is not None:
            if '\\' in link:
                raise ValueError('Invalid SDK archive link')
            target = posixpath.normpath(posixpath.join(str(name.parent), link))
            if target != 'flutter' and not target.startswith('flutter/'):
                raise ValueError('SDK archive link escapes its installation directory')
        lower = name.name.lower()
        if (lower in SYSTEM_DLLS or system_library(lower) or
                (lower.endswith('.dll') and lower.startswith(('api-ms-', 'ext-ms-', 'd3dcompiler_')))):
            raise ValueError('SDK archive contains an OS library: ' + member_name)
        if member_name not in required and member_name not in (metadata_path, bootstrap_path, cross_path):
            continue
        if not regular or size > 128 * 1024 * 1024:
            raise ValueError('Invalid SDK runtime or validation metadata member')
        content = read()
        if member_name == metadata_path:
            metadata = json.loads(content)
        elif member_name in (bootstrap_path, cross_path):
            auxiliary[member_name] = json.loads(content)
        else:
            if content[:2] != b'MZ' or len(content) < 64:
                raise ValueError('Invalid physical runtime PE image')
            pe = int.from_bytes(content[60:64], 'little')
            if content[pe:pe + 6] != b'PE\0\0\xc4\x01':
                raise ValueError('SDK runtime is not Windows ARM32')
            matched[member_name] = hashlib.sha256(content).hexdigest()
    if matched != required:
        raise ValueError('SDK runtime bytes differ from physical validation')
    if not metadata or not all(metadata.get(key) is True for key in (
            'bothRuntimeFamiliesInstalled', 'sdkDependencyPathsPreflightPassed')):
        raise ValueError('Missing complete SDK build and relocation validation')
    ci_builds = all(metadata.get(key) is True for key in (
        'applicationBuildVerified', 'relocatedSdkBuildVerified'))
    native_builds = verified_macos_builds(proof, auxiliary.get(bootstrap_path),
        auxiliary.get(cross_path), actual_digest, revision)
    macos_host = (auxiliary.get(bootstrap_path) or {}).get('sdkHost', '').startswith('macos-')
    if (macos_host and not native_builds) or (not ci_builds and not native_builds):
        raise ValueError('Missing complete SDK build and relocation validation')
    return {'schemaVersion': 1, 'buildRunId': str(run_id), 'sourceRevision': revision,
            'sdkArchiveSha256': actual_digest, 'physicalValidationRecordSha256': digest(validation),
            'physicalRuntimeDigests': matched, 'hardwareGpuValidatedForBothFamilies': True,
            'systemLibrariesCopied': False, 'nativeMacosSdkValidationAccepted': native_builds,
            'releaseReady': True}


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
