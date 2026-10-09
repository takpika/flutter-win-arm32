#!/usr/bin/env python3
"""SDK CMake entry point for the package's unchanged Cargokit Rust builder."""
import argparse
import json
import os
from pathlib import Path
import subprocess

from prepare_cargokit_bridge import prepare


def run(configuration, cargokit, platform):
    configuration = configuration.resolve()
    config = json.loads(configuration.read_text())
    def path(value):
        value = Path(value)
        return value if value.is_absolute() else (configuration.parent / value).resolve()
    dart = path(config['dart'])
    pub_cache = path(config['pubCache'])
    rustup_home = path(config['rustupHome'])
    cargo_config = path(config['cargoConfiguration'])
    provenance = json.loads((rustup_home / 'sdk-provenance.json').read_text())
    prepared_configuration = Path(provenance['configuration'])
    if not prepared_configuration.is_absolute():
        prepared_configuration = rustup_home / prepared_configuration
    if prepared_configuration.resolve() != cargo_config:
        raise ValueError('Prepared SDK Rustup home uses a different Cargo configuration')
    target = 'thumbv7a-' + platform + '-windows-msvc'
    if target not in json.loads(cargo_config.read_text())['targets']:
        raise ValueError('Rust target is not installed in the SDK: ' + target)
    if os.environ.get('CARGOKIT_TARGET_PLATFORM') != 'windows-arm':
        raise ValueError('SDK Cargo backend only supports windows-arm')
    cargokit = cargokit.resolve()
    original = cargokit / 'build_tool'
    if not dart.is_file() or not (original / 'pubspec.yaml').is_file():
        raise ValueError('Missing SDK Dart or original Cargokit package')
    temporary = Path(os.environ['CARGOKIT_TOOL_TEMP_DIR']).resolve()
    temporary.mkdir(parents=True, exist_ok=True)
    bridge = prepare(cargokit, temporary / 'bin/sdk_cargokit_bridge.dart')
    # The runner, pub lock and package configuration are build output. The
    # original build-tool dependency declarations and libraries remain intact.
    (temporary / 'pubspec.yaml').write_text(
        'name: sdk_cargokit_runner\npublish_to: none\n'
        'environment:\n  sdk: ">=3.0.0 <4.0.0"\n'
        'dependencies:\n  build_tool:\n    path: ' + json.dumps(str(original)) + '\n')
    # Match Windows path normalization before traversing Flutter's generated
    # plugin symlinks. POSIX otherwise resolves the symlink before its '..'.
    manifest = os.path.abspath(os.environ['CARGOKIT_MANIFEST_DIR'])
    if not (Path(manifest) / 'Cargo.toml').is_file():
        raise ValueError('Missing original Cargo manifest after path normalization: ' + manifest)
    environment = dict(os.environ, CARGOKIT_MANIFEST_DIR=manifest,
                       CI='true', PUB_CACHE=str(pub_cache),
                       RUSTUP_HOME=str(rustup_home), TMPDIR=str(temporary),
                       TMP=str(temporary), TEMP=str(temporary))
    # Original Cargokit resolves the real Rustup executable. Its run command
    # selects the private SDK launchers; no global home or compiler is changed.
    pub_arguments = ['pub', 'get', '--no-precompile']
    if config.get('pubGetOffline', False):
        pub_arguments.append('--offline')
    subprocess.run([str(dart), *pub_arguments], cwd=temporary,
                   env=environment, check=True)
    subprocess.run([str(dart), str(bridge), target, '--copy-artifacts'],
                   cwd=temporary, env=environment, check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', type=Path, required=True)
    parser.add_argument('--cargokit-directory', type=Path, required=True)
    parser.add_argument('--platform', choices=('rt', 'phone'), default='rt')
    parser.add_argument('command', choices=('build-cmake',))
    args = parser.parse_args()
    run(args.configuration, args.cargokit_directory, args.platform)
