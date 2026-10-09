#!/usr/bin/env python3
"""Prepare the SDK-owned CMake compiler adapters without changing a project."""
import argparse
import json
import os
from pathlib import Path
import shutil
import shlex


def prepare(output, configuration, cppwinrt, cargo_configuration=None, sdk_root=None, python=None):
    output = output.resolve()
    configuration = configuration.resolve()
    config = json.loads(configuration.read_text())
    path_keys = ('compiler', 'llvmBin', 'sysroot', 'resourceDir')
    if 'runtimeResourceDir' in config:
        path_keys += ('runtimeResourceDir',)
    if python is not None:
        # Preserve the interpreter alias so a venv keeps its environment.
        python = Path(os.path.abspath(python))
        if not python.is_file() or not os.access(python, os.X_OK):
            raise ValueError('Missing executable SDK Python interpreter')
    if sdk_root is not None:
        sdk_root = sdk_root.resolve()
        dependencies = [output, cppwinrt.resolve()]
        if python is not None:
            if not python.is_relative_to(sdk_root) or not python.parent.resolve().is_relative_to(sdk_root):
                raise ValueError('Python interpreter alias is outside the SDK')
            dependencies.append(python)
        for key in path_keys:
            dependencies.append(Path(config[key]) if Path(config[key]).is_absolute()
                                else configuration.parent / config[key])
        for key in ('includeDirs', 'systemIncludeDirs', 'libraryDirs', 'rtLibraryDirs', 'phoneLibraryDirs'):
            dependencies += [Path(value) if Path(value).is_absolute()
                             else configuration.parent / value for value in config.get(key, [])]
        if cargo_configuration is not None:
            cargo_configuration = cargo_configuration.resolve()
            dependencies.append(cargo_configuration)
            cargo = json.loads(cargo_configuration.read_text())
            dependencies += [Path(cargo[key]) if Path(cargo[key]).is_absolute()
                             else cargo_configuration.parent / cargo[key]
                             for key in ('dart', 'pubCache', 'rustupHome', 'cargoConfiguration')]
        for dependency in dependencies:
            if not dependency.resolve().is_relative_to(sdk_root):
                raise ValueError('Distribution dependency is outside the SDK: ' + str(dependency))
    output.mkdir(parents=True, exist_ok=True)
    for key in path_keys:
        path = Path(config[key])
        if not path.is_absolute():
            path = configuration.parent / path
        path = path.resolve()
        if not path.exists():
            raise ValueError('Missing toolchain dependency: ' + str(path))
        config[key] = os.path.relpath(path, output)
    for key in ('includeDirs', 'systemIncludeDirs', 'libraryDirs', 'rtLibraryDirs', 'phoneLibraryDirs'):
        if key in config:
            config[key] = [os.path.relpath(
                (Path(value) if Path(value).is_absolute() else configuration.parent / value).resolve(), output)
                           for value in config[key]]
    cppwinrt = cppwinrt.resolve()
    if not (cppwinrt / 'winrt/base.h').is_file():
        raise ValueError('A complete C++/WinRT projection is required')
    directory = Path(__file__).resolve().parent
    header = '#!/bin/sh\n'
    interpreter = 'python3'
    if python is not None:
        header += 'toolchain_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
        header += 'export PATH="$toolchain_dir"/' + shlex.quote(os.path.relpath(python.parent, output)) + ':"$PATH"\n'
        interpreter = '"$toolchain_dir"/' + shlex.quote(os.path.relpath(python, output))
    shutil.copytree(directory / 'compat_include', output / 'compat_include', dirs_exist_ok=True)
    for name in ('cmake_driver.py', 'resource_driver.py', 'gnu_driver.py', 'windows_header_vfs.py', 'windows_guids.c', 'msvc_options.py',
                 'rt_driver.py', 'phone_driver.py', 'rust_link.py', 'phone_job_api.def', 'phone_process_api.def', 'rt-cross.cmake', 'rt-project.cmake',
                 'native-package-compat.cmake', 'msvc_codecvt_compat.h'):
        shutil.copy2(directory / name, output / name)
    for name, driver in (('cmake-cxx', 'cmake_driver.py'), ('armv7-w64-mingw32-windres', 'resource_driver.py')):
        path = output / name
        path.write_text(header + 'exec ' + interpreter + ' "$(dirname "$0")/' + driver + '" "$@"\n')
        path.chmod(0o755)
    (output / 'toolchain.json').write_text(json.dumps(config, indent=2) + '\n')
    (output / 'cross-build.json').write_text(json.dumps({
        'cmake': 'cmake', 'toolchainRoot': config['sysroot'],
        'cppWinrtInclude': os.path.relpath(cppwinrt, output),
    }, indent=2) + '\n')
    if cargo_configuration is not None:
        cargo_configuration = cargo_configuration.resolve()
        cargo = json.loads(cargo_configuration.read_text())
        for key in ('dart', 'pubCache', 'rustupHome', 'cargoConfiguration'):
            dependency = Path(cargo[key])
            if not dependency.is_absolute():
                dependency = cargo_configuration.parent / dependency
            dependency = dependency.resolve()
            if not dependency.exists():
                raise ValueError('Missing SDK Cargo dependency: ' + str(dependency))
            cargo[key] = os.path.relpath(dependency, output)
        for name in ('cargo_backend.py', 'prepare_cargokit_bridge.py'):
            shutil.copy2(directory / name, output / name)
        (output / 'cargo-build.json').write_text(json.dumps(cargo, indent=2) + '\n')
        launcher = output / 'cargo-backend'
        launcher.write_text(header + 'exec ' + interpreter + ' "$(dirname "$0")/cargo_backend.py" '
                            '--configuration "$(dirname "$0")/cargo-build.json" "$@"\n')
        launcher.chmod(0o755)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--toolchain-config', type=Path, required=True)
    parser.add_argument('--cppwinrt-include', type=Path, required=True)
    parser.add_argument('--cargo-build-configuration', type=Path)
    parser.add_argument('--sdk-root', type=Path,
                        help='For distributions, reject dependencies outside this SDK root')
    parser.add_argument('--python', type=Path, help='SDK-owned interpreter for generated launchers')
    args = parser.parse_args()
    print(prepare(args.output, args.toolchain_config, args.cppwinrt_include,
                  args.cargo_build_configuration, args.sdk_root, args.python))
