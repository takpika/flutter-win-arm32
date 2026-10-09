#!/usr/bin/env python3
"""Configure the SDK-owned Phone build from installed build dependencies."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


RUNTIME_FILES = (
    'flutter_engine.dll', 'libEGL.dll', 'libGLESv2.dll',
    'flutter_winrt_compat.dll', 'icudtl.dat',
)


def prepare(output, configuration, runtime_directory, python, packager,
            packager_kind, package_defaults, ffi_toolchain_directory=None, cppwinrt_include=None):
    output = output.resolve()
    configuration = configuration.resolve()
    runtime_directory = runtime_directory.resolve()
    # Keep interpreter symlinks: a venv executable must run in its own environment.
    python = Path(os.path.abspath(python))
    packager = Path(os.path.abspath(packager))
    required = [configuration, python, packager,
                *(runtime_directory / name for name in RUNTIME_FILES)]
    for path in required:
        if not path.is_file():
            raise ValueError('Missing Phone build dependency: ' + str(path))
    if not os.access(python, os.X_OK) or not os.access(packager, os.X_OK):
        raise ValueError('The Python interpreter and packager must be executable')
    toolchain = json.loads(configuration.read_text())
    path_keys = ('compiler', 'llvmBin', 'sysroot', 'resourceDir')
    if 'runtimeResourceDir' in toolchain:
        path_keys += ('runtimeResourceDir',)
    for key in path_keys:
        path = Path(toolchain[key])
        path = path if path.is_absolute() else configuration.parent / path
        if not path.exists():
            raise ValueError('Missing toolchain dependency: ' + str(path))
    for key in ('includeDirs', 'systemIncludeDirs', 'libraryDirs', 'phoneLibraryDirs'):
        for value in toolchain.get(key, []):
            path = Path(value)
            path = path if path.is_absolute() else configuration.parent / path
            if not path.is_dir():
                raise ValueError('Missing toolchain directory: ' + str(path))
    if not isinstance(package_defaults, dict):
        raise ValueError('Package defaults must be a JSON object')
    # Packaging dependencies belong to the selected host environment, never Appx.
    requirements = Path(__file__).resolve().parents[1] / 'requirements-phone-packaging.txt'
    requirement = requirements.read_text().strip()
    package, version = requirement.split('==')
    if package.lower() != 'pillow':
        raise ValueError('Unsupported Phone packaging dependency: ' + requirement)
    subprocess.run([str(python), '-c',
                    'import PIL,sys; sys.exit(0 if PIL.__version__ == sys.argv[1] else 1)',
                    version], check=True)
    relative = lambda path: os.path.relpath(path, output)
    config = {
        'python': relative(python),
        'toolchainConfig': relative(configuration),
        'runtimeFiles': {name: relative(runtime_directory / name) for name in RUNTIME_FILES},
        'packager': relative(packager),
        'packagerKind': packager_kind,
        'packageDefaults': package_defaults,
    }
    if ffi_toolchain_directory is not None:
        ffi_toolchain_directory = ffi_toolchain_directory.resolve()
        if not (ffi_toolchain_directory / 'rt-cross.cmake').is_file():
            raise ValueError('Missing SDK native FFI toolchain')
        config['ffiToolchainDirectory'] = relative(ffi_toolchain_directory)
    if cppwinrt_include is not None:
        cppwinrt_include = cppwinrt_include.resolve()
        if not (cppwinrt_include / 'winrt/base.h').is_file():
            raise ValueError('Missing SDK C++/WinRT projection')
        config['cppWinrtInclude'] = relative(cppwinrt_include)
    output.mkdir(parents=True, exist_ok=True)
    destination = output / 'phone-build.json'
    temporary = destination.with_suffix('.json.new')
    temporary.write_text(json.dumps(config, indent=2) + '\n')
    temporary.replace(destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True,
                        help='SDK windows-arm-release/toolchain directory')
    parser.add_argument('--toolchain-config', type=Path, required=True)
    parser.add_argument('--runtime-directory', type=Path, required=True)
    parser.add_argument('--python', type=Path, default=Path(sys.executable))
    parser.add_argument('--packager', type=Path, required=True)
    parser.add_argument('--packager-kind', choices=('makeappx', 'makemsix'), required=True)
    parser.add_argument('--ffi-toolchain-directory', type=Path)
    parser.add_argument('--cppwinrt-include', type=Path)
    parser.add_argument('--package-defaults', type=Path,
                        help='Optional JSON publisher, branding and permission defaults')
    args = parser.parse_args()
    defaults = json.loads(args.package_defaults.read_text()) if args.package_defaults else {}
    print(prepare(args.output, args.toolchain_config, args.runtime_directory,
                  args.python, args.packager, args.packager_kind, defaults))


if __name__ == '__main__':
    main()
