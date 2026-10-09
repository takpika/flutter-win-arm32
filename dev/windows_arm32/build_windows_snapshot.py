#!/usr/bin/env python3
"""Cross-build a Windows x64 ARM32 AOT generator from the pinned Dart sources."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys

from sdk_hosts import select_host, verify_native_host
from prepare_sources import prepare_sources


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(args):
    root = Path(__file__).resolve().parents[2]
    source = root / 'engine/src'
    output = args.output.resolve()
    configuration = args.toolchain_config.resolve()
    if args.jobs < 1 or not configuration.is_file():
        raise ValueError('Supply an isolated toolchain configuration and positive job count')
    prepare_sources(root)
    output.mkdir(parents=True, exist_ok=True)
    scratch = output / 'tmp'
    scratch.mkdir(exist_ok=True)
    environment = dict(os.environ, TMPDIR=str(scratch), TMP=str(scratch), TEMP=str(scratch))
    driver = root / 'dev/windows_arm32/toolchain/windows_snapshot_driver.py'
    # Include the configuration contents in Ninja's command identity.
    identity = sha(configuration)
    configured_driver = output / ('snapshot-driver-' + identity + '.py')
    configured_driver.write_text(
        'import os, runpy, sys\n'
        f'os.environ["FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG"] = {str(configuration)!r}\n'
        f'sys.path.insert(0, {str(driver.parent)!r})\n'
        f'# configuration SHA256: {identity}\n'
        f'runpy.run_path({str(driver)!r}, run_name="__main__")\n')
    gn = runpy.run_path(str(source / 'flutter/tools/gn'))
    options = gn['parse_args']([
        'gn', '--target-os=win', '--windows-cpu=arm', '--runtime-mode=release',
        '--no-enable-unittests', '--no-rbe', '--ide='])
    settings = gn['to_gn_args'](options)
    settings.update(current_cpu='x64', flutter_windows_snapshot_tool=str(configured_driver),
                    python_path=sys.executable)
    if sys.platform.startswith('linux'):
        settings['use_default_linux_sysroot'] = False
    if args.host_macos_sdk:
        settings['mac_sdk_path'] = str(args.host_macos_sdk.resolve())
    # This build has only the upstream generator as its root. It neither builds
    # nor changes the RT/Phone engine, and keeps Dart's target architecture ARM.
    dotfile = output / 'snapshot.gn'
    dotfile.write_text(
        'buildconfig = "//build/config/BUILDCONFIG.gn"\n'
        'secondary_source = "//flutter/build/secondary/"\n'
        f'script_executable = {json.dumps(sys.executable)}\n'
        'root = "//flutter/third_party/dart/runtime/bin:gen_snapshot"\n')
    build_dir = output / 'build'
    manifests = root / 'dev/windows_arm32/patches'
    inputs = {path.name: sha(path) for path in sorted(manifests.glob('*-source-manifest.json'))}
    host_paths = [Path(__file__).resolve(), *(driver.parent / name for name in (
        'windows_snapshot_driver.py', 'gnu_driver.py', 'msvc_options.py', 'windows_header_vfs.py'))]
    host_inputs = {str(path.relative_to(root)): sha(path) for path in host_paths}
    subprocess.run([
        str(source / 'flutter/third_party/gn/gn'), 'gen', str(build_dir),
        '--root=' + str(source), '--dotfile=' + str(dotfile),
        '--args=' + ' '.join(gn['to_command_line'](settings)),
    ], cwd=source, env=environment, check=True)
    if args.configure_only:
        return {'configured': True, 'sourceBuilt': False, 'buildDirectory': str(build_dir)}
    subprocess.run([args.ninja, '-C', str(build_dir), '-j', str(args.jobs), 'gen_snapshot.exe'],
                   env=environment, check=True)
    generator = build_dir / 'gen_snapshot.exe'
    verify_native_host(generator, select_host('windows-x64'))
    if (sha(configuration) != identity or host_inputs != {
            str(path.relative_to(root)): sha(path) for path in host_paths} or inputs != {
            path.name: sha(path) for path in sorted(manifests.glob('*-source-manifest.json'))}):
        raise ValueError('Build inputs changed during snapshot generator compilation')
    report = {'host': 'windows-x64', 'dartTargetArchitecture': 'arm',
              'sourceBuilt': True, 'runtimeExecutionVerified': False,
              'sourceManifests': inputs, 'toolchainConfigurationSha256': identity,
              'hostBuildInputs': host_inputs,
              'generatorSha256': sha(generator), 'gnArgumentsSha256': sha(build_dir / 'args.gn')}
    (output / 'snapshot-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--toolchain-config', type=Path, required=True)
    parser.add_argument('--host-macos-sdk', type=Path)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--configure-only', action='store_true')
    print(json.dumps(build(parser.parse_args()), indent=2))
