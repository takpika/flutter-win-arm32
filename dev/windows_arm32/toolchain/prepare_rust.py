#!/usr/bin/env python3
"""Prepare RT and Phone Rust targets from the compiler's upstream ARM32 target."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def prepare(output, rustc, configuration):
    output = output.resolve()
    rustc = rustc.resolve()
    configuration = configuration.resolve()
    if not rustc.is_file() or not configuration.is_file():
        raise ValueError('Supply the Rust compiler and native toolchain configuration')
    # Scope unstable target introspection to this process, never the shared compiler.
    environment = dict(os.environ, RUSTC_BOOTSTRAP='1')
    output.mkdir(parents=True, exist_ok=True)
    temporary = output / 'tmp'
    temporary.mkdir(exist_ok=True)
    environment.update(TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary))
    version = subprocess.check_output([str(rustc), '-vV'], env=environment, text=True)
    upstream = json.loads(subprocess.check_output([
        str(rustc), '-Zunstable-options', '--print', 'target-spec-json',
        '--target', 'thumbv7a-pc-windows-msvc'], env=environment, text=True))
    expected = {'arch': 'arm', 'binary-format': 'coff', 'target-pointer-width': 32,
                'env': 'msvc', 'llvm-target': 'thumbv7a-pc-windows-msvc'}
    for key, value in expected.items():
        if upstream.get(key) != value and not (key == 'target-pointer-width' and upstream.get(key) == str(value)):
            raise ValueError('Unsupported upstream Rust ARM32 target field: ' + key)
    config = json.loads(configuration.read_text())
    for key in ('compiler', 'llvmBin', 'sysroot', 'resourceDir'):
        path = Path(config[key])
        path = (path if path.is_absolute() else configuration.parent / path).resolve()
        if not path.exists():
            raise ValueError('Missing native toolchain dependency: ' + str(path))
        config[key] = os.path.relpath(path, output)
    for key in ('includeDirs', 'systemIncludeDirs', 'libraryDirs', 'rtLibraryDirs', 'phoneLibraryDirs'):
        if key in config:
            config[key] = [os.path.relpath(
                (Path(value) if Path(value).is_absolute() else configuration.parent / value).resolve(), output)
                           for value in config[key]]
    directory = Path(__file__).resolve().parent
    shutil.copytree(Path(__file__).resolve().parent / 'compat_include', output / 'compat_include', dirs_exist_ok=True)
    for name in ('rust_link.py', 'rust_cc.py', 'gnu_driver.py', 'windows_header_vfs.py', 'windows_guids.c', 'msvc_options.py', 'msvc_codecvt_compat.h',
                 'rt_driver.py', 'phone_driver.py', 'phone_job_api.def', 'phone_process_api.def'):
        shutil.copy2(directory / name, output / name)
    (output / 'toolchain.json').write_text(json.dumps(config, indent=2) + '\n')
    targets = {}
    for platform, vendor, contracts in (
            ('rt', 'w64', ['-lucrt']), ('phone', 'uwp', ['-lwindowsapp', '-lucrtapp'])):
        launcher = output / ('rust-link-' + platform)
        launcher.write_text('#!/bin/sh\n'
            'driver_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
            'export FLUTTER_WINDOWS_ARM32_PLATFORM=' + platform + '\n'
            'export FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG="$driver_dir/toolchain.json"\n'
            'exec python3 "$driver_dir/rust_link.py" "$@"\n')
        launcher.chmod(0o755)
        cc = output / ('clang-cc-' + platform)
        cc.write_text('#!/bin/sh\n'
            'driver_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
            'export FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG="$driver_dir/toolchain.json"\n'
            'exec python3 "$driver_dir/rust_cc.py" --sdk-platform ' + platform + ' "$@"\n')
        cc.chmod(0o755)
        target = dict(upstream)
        # Retain upstream ABI, TLS, static-CRT and debug-information capabilities.
        # The native SDK driver uses the GNU linker command line with the MSVC ABI.
        target.update({'linker-flavor': 'gnu-cc', 'linker-is-gnu': True,
                       'lld-flavor': 'gnu', 'is-like-msvc': False,
                       # Windows COFF uses .pdata/.xdata, not an ELF EH header.
                       'eh-frame-header': False,
                       'crt-objects-fallback': 'mingw', 'vendor': vendor,
                       # Cargo's SDK dispatcher places this target's directory
                       # on PATH, so moving an installed SDK retains its linker.
                       'linker': launcher.name,
                       'pre-link-args': {'gnu-cc': ['-fno-use-linker-plugin',
                           '-Wl,--dynamicbase', '-Wl,--nxcompat'] +
                           (['-Wl,--appcontainer'] if platform == 'phone' else [])},
                       'late-link-args': {'gnu-cc': contracts}})
        destination = output / ('thumbv7a-' + platform + '-windows-msvc.json')
        destination.write_text(json.dumps(target, indent=2) + '\n')
        subprocess.run([str(rustc), '-Zunstable-options', '--print', 'cfg',
                        '--target', str(destination)], env=environment,
                       stdout=subprocess.DEVNULL, check=True)
        targets[platform] = destination.name
    (output / 'rust-target-provenance.json').write_text(json.dumps({
        'rustCompilerVersion': version, 'upstreamTarget': upstream,
        'targets': targets, 'generatedLinkerPathsRequirePreparationAfterRelocation': False,
        'fullStandardLibraryBuildVerified': False,
    }, indent=2) + '\n')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rustc', type=Path, required=True)
    parser.add_argument('--toolchain-config', type=Path, required=True)
    args = parser.parse_args()
    print(prepare(args.output, args.rustc, args.toolchain_config))
