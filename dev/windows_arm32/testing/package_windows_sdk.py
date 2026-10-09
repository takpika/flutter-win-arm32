#!/usr/bin/env python3
"""Package a build-verified native Windows SDK without mutable caches or OS DLLs."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_release import SYSTEM_DLLS
from toolchain.windows_native_dependencies import system_library


def package(sdk, archive):
    sdk, archive = sdk.resolve(), archive.resolve()
    if archive.is_relative_to(sdk) or archive.exists():
        raise ValueError('Use a new output archive outside the SDK')
    metadata = sdk / 'bin/cache/artifacts/engine/windows-arm-release/sdk-assembly.json'
    proof = json.loads(metadata.read_text())
    if not all(proof.get(key) is True for key in (
            'bothRuntimeFamiliesInstalled', 'sdkDependencyPathsPreflightPassed',
            'applicationBuildVerified', 'relocatedSdkBuildVerified')):
        raise ValueError('SDK has not passed both normal application builds after relocation')
    bootstrap = json.loads((sdk / 'bin/cache/windows-arm32-host-bootstrap.json').read_text())
    if bootstrap['sdkHost'] != 'windows-x64':
        raise ValueError('Expected a native Windows x64 SDK')
    files = []
    for path in sorted(sdk.rglob('*')):
        relative = path.relative_to(sdk)
        if '.windows-arm32-cache' in relative.parts:
            continue
        if path.is_symlink():
            raise ValueError('Windows SDK must contain materialized files: ' + str(relative))
        if not path.is_file():
            continue
        name = path.name.lower()
        if (name in SYSTEM_DLLS or system_library(name) or
                (name.endswith('.dll') and name.startswith(('api-ms-', 'ext-ms-', 'd3dcompiler_')))):
            raise ValueError('SDK contains an OS library: ' + str(relative))
        files.append(path)
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED,
                         compresslevel=6, strict_timestamps=False) as output:
        for path in files:
            output.write(path, 'flutter/' + path.relative_to(sdk).as_posix())
    digest = hashlib.sha256()
    with archive.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    archive.with_name(archive.name + '.sha256').write_text(digest.hexdigest() + '  ' + archive.name + '\n')
    archive.with_name(archive.name + '.json').write_text(json.dumps({
        'sdkHost': bootstrap['sdkHost'], 'sourceRevision': bootstrap['sourceRevision'],
        'archiveSha256': digest.hexdigest(), 'fileCount': len(files),
        'systemDllPayloads': [], 'applicationBuildVerified': True,
        'relocatedSdkBuildVerified': True, 'releaseReady': False,
        'physicalValidationOfCiArtifactsPending': True}, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    args = parser.parse_args()
    package(args.sdk, args.archive)
