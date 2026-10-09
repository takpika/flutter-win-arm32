#!/usr/bin/env python3
"""Stage installed macOS SDK declarations for a Linux cross-build input.

This archive is a build input, not a Flutter SDK release asset. It contains
headers and text-based linker declarations, never macOS library binaries.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tarfile


def stage(sdk, output):
    sdk, output = sdk.resolve(), output.resolve()
    if not (sdk / 'usr/include/stdio.h').is_file() or not (sdk / 'usr/lib/libSystem.tbd').is_file():
        raise ValueError('Supply an installed macOS SDK with headers and linker declarations')
    selected = set((sdk / 'usr/include').rglob('*'))
    selected.update((sdk / 'usr/lib').rglob('*.tbd'))
    for framework in (sdk / 'System/Library/Frameworks').glob('*.framework'):
        for path in framework.rglob('*'):
            if path.is_symlink() or path.suffix == '.tbd' or any(
                    part in ('Headers', 'Modules') for part in path.relative_to(framework).parts):
                selected.add(path)
    for name in ('SDKSettings.json', 'SDKSettings.plist'):
        if (sdk / name).is_file():
            selected.add(sdk / name)
    records = {}
    for path in sorted(selected):
        relative = str(path.relative_to(sdk))
        if path.is_symlink():
            if not path.resolve().is_relative_to(sdk):
                raise ValueError('SDK declaration link escapes its source SDK: ' + relative)
            records[relative] = {'link': os.readlink(path)}
        elif path.is_file():
            with path.open('rb') as source:
                header = source.read(4)
            if header[:2] == b'MZ' or header == b'\x7fELF' or header in (
                    bytes.fromhex('cffaedfe'), bytes.fromhex('feedfacf'),
                    bytes.fromhex('cefaedfe'), bytes.fromhex('feedface'),
                    bytes.fromhex('cafebabe'), bytes.fromhex('cafebabf'),
                    bytes.fromhex('bebafeca'), bytes.fromhex('bfbafeca')):
                raise ValueError('SDK build input contains a native binary: ' + relative)
            records[relative] = {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                                 'bytes': path.stat().st_size}
    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, 'w:gz', dereference=False) as archive:
        for name in records:
            archive.add(sdk / name, arcname='sysroot/' + name, recursive=False)
    settings = json.loads((sdk / 'SDKSettings.json').read_text())
    report = {'sdkName': sdk.name, 'sdkVersion': settings['Version'], 'declarationsOnly': True,
              'osLibraryBinariesIncluded': False, 'flutterReleasePayload': False,
              'archiveSha256': hashlib.sha256(output.read_bytes()).hexdigest(), 'files': records}
    output.with_name(output.name + '.json').write_text(json.dumps(report, indent=2) + '\n')
    output.with_name(output.name + '.sha256').write_text(report['archiveSha256'] + '  ' + output.name + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = stage(args.sdk, args.output)
    print(json.dumps({key: value for key, value in report.items() if key != 'files'}, indent=2))
