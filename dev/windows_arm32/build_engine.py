#!/usr/bin/env python3
"""Build both ARM32 engine families from one source tree and toolchain."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import struct
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_armnt(path):
    with path.open('rb') as stream:
        if stream.read(2) != b'MZ':
            raise ValueError('Not a PE image: ' + str(path))
        stream.seek(0x3c)
        offset = struct.unpack('<I', stream.read(4))[0]
        stream.seek(offset)
        if stream.read(4) != b'PE\0\0' or stream.read(2) != b'\xc4\x01':
            raise ValueError('Not an ARMNT PE image: ' + str(path))


def artifacts(directory, family, host_cpu, compiler_platform=False):
    engine = 'flutter_windows' if family == 'rt' else 'flutter_engine'
    names = [engine + '.dll', engine + '.dll.lib', 'libEGL.dll',
             'libGLESv2.dll', 'icudtl.dat', 'clang_' + host_cpu + '/gen_snapshot']
    if family == 'phone':
        names += ['flutter_winrt_compat.dll']
    if compiler_platform:
        names += ['flutter_patched_sdk/platform_strong.dill',
                  'flutter_patched_sdk/vm_outline_strong.dill']
    result = {}
    for name in names:
        path = directory / name
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError('Missing build artifact: ' + str(path))
        if path.suffix == '.dll':
            verify_armnt(path)
        result[name] = {'bytes': path.stat().st_size, 'sha256': digest(path)}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--toolchain-config', type=Path, required=True)
    parser.add_argument('--host-macos-sdk', type=Path,
                        help='Installed macOS SDK used only for this build')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--platform', choices=('rt', 'phone', 'both'), default='both',
                        help='Build both families locally, or one family in a CI matrix')
    parser.add_argument('--compiler-platform', action='store_true',
                        help='Also generate and verify the matching Flutter platform dill files')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--host-cpu', choices=('arm64', 'x64'),
                        default='arm64' if platform.machine().lower() in ('arm64', 'aarch64') else 'x64')
    parser.add_argument('--verify-existing', action='store_true',
                        help='Audit existing artifacts only; does not prove a fresh source build')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    configuration = args.toolchain_config.resolve()
    if not configuration.is_file():
        parser.error('Missing toolchain configuration')
    scratch = output / '.windows-arm32-cache/tmp'
    scratch.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG=str(configuration),
                       TMPDIR=str(scratch), TMP=str(scratch), TEMP=str(scratch))
    report = {'sourceRoot': str(root), 'toolchainConfigSha256': digest(configuration),
              'frameworkRevision': subprocess.check_output(
                  ['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
              'sourceManifests': {
                  path.name: digest(path) for path in sorted(
                      (root / 'dev/windows_arm32/patches').glob('*-source-manifest.json'))},
              'freshBuildInvocation': not args.verify_existing, 'families': {}}
    families = ('rt', 'phone') if args.platform == 'both' else (args.platform,)
    for family in families:
        directory = output / 'out' / ('win_release_arm_' + family)
        if not args.verify_existing:
            configure = [sys.executable, str(root / 'dev/windows_arm32/configure_engine.py'),
                            '--platform', family, '--output', str(output),
                            '--toolchain-config', str(configuration)]
            if args.host_macos_sdk is not None:
                configure += ['--host-macos-sdk', str(args.host_macos_sdk.resolve())]
            subprocess.run(configure, env=environment, check=True)
            targets = ['flutter_windows' if family == 'rt' else 'flutter_engine_library',
                       'libEGL', 'libGLESv2', 'icudtl.dat', 'clang_' + args.host_cpu + '/gen_snapshot']
            if family == 'phone':
                targets += ['flutter_winrt_compat']
            if args.compiler_platform:
                targets += ['flutter/lib/snapshot:strong_platform']
            subprocess.run([args.ninja, '-C', str(directory), '-j', str(args.jobs),
                            *targets], env=environment, check=True)
        report['families'][family] = {
            'gnArgumentsSha256': digest(directory / 'args.gn'),
            'artifacts': artifacts(directory, family, args.host_cpu, args.compiler_platform)}
    current_manifests = {
        path.name: digest(path) for path in sorted(
            (root / 'dev/windows_arm32/patches').glob('*-source-manifest.json'))}
    if (current_manifests != report['sourceManifests'] or
            digest(configuration) != report['toolchainConfigSha256']):
        raise ValueError('Build inputs changed during compilation; rerun before recording provenance')
    destination = output / ('engine-artifact-audit.json' if args.verify_existing else 'engine-build-provenance.json')
    destination.write_text(json.dumps(report, indent=2) + '\n')
    print(destination)


if __name__ == '__main__':
    main()
