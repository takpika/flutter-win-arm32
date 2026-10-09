#!/usr/bin/env python3
"""Build and stage an SDK-owned UWP runner with Flutter's normal asset output."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--binary-name', required=True)
    parser.add_argument('--package-metadata', type=Path)
    parser.add_argument('--icon', type=Path)
    parser.add_argument('--plugin', action='append', default=[])
    parser.add_argument('--ffi-plugin', action='append', default=[])
    parser.add_argument('--application-root', type=Path)
    parser.add_argument('--package', action='append', default=[])
    parser.add_argument('--config-only', action='store_true')
    args = parser.parse_args()
    if not args.binary_name or Path(args.binary_name).name != args.binary_name:
        parser.error('The executable name must be a filename')
    configuration = args.configuration.resolve()
    config = json.loads(configuration.read_text())
    def configured(value):
        path = Path(value)
        return path if path.is_absolute() else (configuration.parent / path).resolve()
    runtime = config['runtimeFiles']
    expected = {'flutter_engine.dll', 'libEGL.dll', 'libGLESv2.dll', 'flutter_winrt_compat.dll', 'icudtl.dat'}
    if set(runtime) != expected:
        parser.error('The SDK must supply precisely its four runtime DLLs and ICU data')
    for name, source in runtime.items():
        if not configured(source).is_file():
            parser.error('Missing SDK runtime artifact: ' + name)
    output = args.output.resolve()
    native = output / 'native'
    command = [sys.executable, str(Path(__file__).with_name('build_uwp_host.py')),
               '--toolchain-config', str(configured(config['toolchainConfig'])),
               '--output', str(native)]
    if 'cppWinrtInclude' in config:
        command += ['--cppwinrt-include', str(configured(config['cppWinrtInclude']))]
    for plugin in args.plugin:
        command += ['--plugin', plugin]
    for package in args.package:
        command += ['--package', package]
    if args.config_only:
        command += ['--config-only']
    subprocess.run(command, check=True)
    ffi_libraries = []
    if args.ffi_plugin:
        if args.application_root is None or 'ffiToolchainDirectory' not in config:
            parser.error('FFI builds require the original application root and SDK native toolchain')
        from build_uwp_ffi import build as build_ffi
        ffi_libraries = build_ffi(args.application_root, output / 'native/ffi',
                                  configured(config['ffiToolchainDirectory']),
                                  dict(value.split('=', 1) for value in args.package),
                                  args.ffi_plugin, args.config_only)
    if args.config_only:
        return
    assets = args.assets.resolve()
    for path in (assets / 'windows/app.so', assets / 'flutter_assets'):
        if not path.exists():
            parser.error('Missing Flutter asset output: ' + str(path))
    bundle = output / 'bundle'
    if bundle.exists():
        shutil.rmtree(bundle)
    data = bundle / 'data'
    data.mkdir(parents=True, exist_ok=True)
    shutil.copy2(native / 'flutter_uwp_runner.exe', bundle / (args.binary_name + '.exe'))
    for name, source in runtime.items():
        shutil.copy2(configured(source), (data if name == 'icudtl.dat' else bundle) / name)
    for library in ffi_libraries:
        destination = bundle / library.name
        if destination.exists():
            parser.error('FFI bundled library collides with a runner/runtime artifact: ' + library.name)
        shutil.copy2(library, destination)
    shutil.copy2(assets / 'windows/app.so', data / 'app.so')
    target = data / 'flutter_assets'
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(assets / 'flutter_assets', target)
    if args.package_metadata is not None:
        if args.icon is None:
            parser.error('Supply the original application icon for Appx packaging')
        from package_uwp import create_assets_and_manifest
        metadata = json.loads(args.package_metadata.read_text())
        if 'gal' in args.plugin:
            # The original gallery backend writes both media types to Pictures.
            metadata['capabilities'] = sorted(set(metadata.get('capabilities',
                ['internetClientServer', 'privateNetworkClientServer'])) | {'picturesLibrary'})
        create_assets_and_manifest(bundle, args.binary_name,
                                  metadata, args.icon)
        packager = configured(config['packager'])
        destination = output / (args.binary_name + '.unsigned.appx')
        destination.unlink(missing_ok=True)
        kind = config.get('packagerKind', 'makemsix')
        if kind == 'makemsix':
            subprocess.run([str(packager), 'pack', '-d', str(bundle), '-p', str(destination)], check=True)
        elif kind == 'makeappx':
            subprocess.run([str(packager), 'pack', '/d', str(bundle), '/p', str(destination), '/o'], check=True)
        else:
            parser.error('Unknown SDK packagerKind: ' + kind)
        signing = config.get('signing')
        if signing:
            signed = output / (args.binary_name + '.appx')
            signed.unlink(missing_ok=True)
            tool = configured(signing['tool'])
            if signing['kind'] == 'osslsigncode':
                certificate = configured(signing['certificate'])
                key = configured(signing['privateKey'])
                subprocess.run([str(tool), 'sign', '-certs', str(certificate), '-key', str(key),
                                '-h', 'sha256', '-in', str(destination), '-out', str(signed)], check=True)
                subprocess.run([str(tool), 'verify', '-CAfile', str(certificate), '-in', str(signed)], check=True)
            elif signing['kind'] == 'signtool':
                shutil.copy2(destination, signed)
                subprocess.run([str(tool), 'sign', '/sha1', signing['certificateThumbprint'],
                                '/fd', 'SHA256', str(signed)], check=True)
                subprocess.run([str(tool), 'verify', '/pa', str(signed)], check=True)
            else:
                parser.error('Unknown SDK signing kind: ' + signing['kind'])
            print(signed)
        else:
            print(destination)
    else:
        print(bundle)


if __name__ == '__main__':
    main()
