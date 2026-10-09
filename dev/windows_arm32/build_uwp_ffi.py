#!/usr/bin/env python3
"""Build original Windows FFI package CMake contracts for the Phone family."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess


def quote(value):
    value = str(value)
    if ']===]' in value:
        raise ValueError('Unsupported CMake bracket delimiter in path')
    return '[===[' + value + ']===]'


def build(application, output, toolchain, packages, names, config_only=False):
    application, output, toolchain = application.resolve(), output.resolve(), toolchain.resolve()
    for required in ('rt-cross.cmake', 'cargo-backend', 'cmake-cxx'):
        if not (toolchain / required).is_file():
            raise ValueError('Missing SDK FFI build dependency: ' + required)
    source = output / 'cmake-source'
    source.mkdir(parents=True, exist_ok=True)
    links = application / 'windows/flutter/ephemeral/.plugin_symlinks'
    lines = ['cmake_minimum_required(VERSION 3.20)',
             'project(flutter_phone_ffi LANGUAGES CXX)',
             'set(FLUTTER_TARGET_PLATFORM windows-arm)',
             'set(FLUTTER_ARM32_CARGOKIT_PROJECT_ROOT ' + quote(application / 'windows') + ')']
    for name in sorted(names):
        if not re.fullmatch('[a-z][a-z0-9_]*', name) or name not in packages:
            raise ValueError('Supply a valid original FFI package directory: ' + name)
        package = Path(packages[name]).resolve()
        if not (package / 'windows/CMakeLists.txt').is_file():
            raise ValueError('Missing original Windows FFI CMake contract: ' + name)
        link = links / name
        links.mkdir(parents=True, exist_ok=True)
        if link.exists() or link.is_symlink():
            if link.resolve() != package:
                raise ValueError('Generated plugin link points to another package: ' + name)
        else:
            link.symlink_to(package, target_is_directory=True)
        lines += ['add_subdirectory(' + quote(link / 'windows') + ' ' + quote('plugins/' + name) + ')',
                  'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/' + name + '-libraries.txt"',
                  '  CONTENT "$<JOIN:${' + name + '_bundled_libraries},\\n>\\n")']
    (source / 'CMakeLists.txt').write_text('\n'.join(lines) + '\n')
    binary = output / 'cmake'
    environment = dict(os.environ, FLUTTER_WINDOWS_ARM32_PLATFORM='phone')
    scratch = output / 'tmp'
    scratch.mkdir(exist_ok=True)
    environment.update(TMPDIR=str(scratch), TMP=str(scratch), TEMP=str(scratch))
    arguments = ['cmake', '-G', 'Ninja', '-S', str(source), '-B', str(binary),
                 '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_TOOLCHAIN_FILE=' + str(toolchain / 'rt-cross.cmake')]
    with (output / 'build.log').open('w') as log:
        subprocess.run(arguments, env=environment, stdout=log, stderr=log, check=True)
        if config_only:
            return []
        subprocess.run(['cmake', '--build', str(binary), '--parallel',
                        os.environ.get('CMAKE_BUILD_PARALLEL_LEVEL', '1')],
                       env=environment, stdout=log, stderr=log, check=True)
    libraries = []
    for name in sorted(names):
        paths = (binary / (name + '-libraries.txt')).read_text().splitlines()
        if not paths:
            raise ValueError('Original FFI package did not declare its bundled libraries: ' + name)
        for value in paths:
            path = Path(value)
            if not path.is_file() or not path.resolve().is_relative_to(binary):
                raise ValueError('Expected a source-built FFI artifact in this build: ' + value)
            libraries.append(path)
    (output / 'ffi-libraries.json').write_text(json.dumps([str(p) for p in libraries], indent=2) + '\n')
    return libraries


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--toolchain-directory', type=Path, required=True)
    parser.add_argument('--ffi-plugin', action='append', default=[])
    parser.add_argument('--package', action='append', default=[])
    parser.add_argument('--config-only', action='store_true')
    args = parser.parse_args()
    packages = dict(item.split('=', 1) for item in args.package)
    print(json.dumps([str(p) for p in build(args.application_root, args.output,
          args.toolchain_directory, packages, args.ffi_plugin, args.config_only)]))
