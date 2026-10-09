#!/usr/bin/env python3
"""Select the common native LLVM and an explicit macOS build-only sysroot."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from sdk_hosts import current_host, select_host, verify_native_host


def prepare(args):
    host = select_host(args.host)
    if host['os'] != 'macos':
        raise ValueError('Select a macOS SDK host')
    compiler, sdk, output = args.compiler_root.resolve(), args.sdk.resolve(), args.output.resolve()
    for name in ('clang', 'clang++', 'ld64.lld', 'llvm-ar', 'llvm-ranlib'):
        path = compiler / 'bin' / name
        if not path.is_file():
            raise ValueError('Missing common compiler tool: ' + name)
        verify_native_host(path, current_host())
    for name in ('usr/include/stdio.h', 'usr/lib/libSystem.tbd', 'SDKSettings.json'):
        if not (sdk / name).is_file():
            raise ValueError('Missing macOS build declaration: ' + name)
    cpu = {'x64': 'x86_64', 'arm64': 'arm64'}[host['cpu']]
    minimum = '10.15' if host['cpu'] == 'x64' else '11.0'
    target = cpu + '-apple-macos' + minimum
    rules = output.with_name(output.stem + '-rules.cmake')
    settings = {
        'CMAKE_SYSTEM_NAME': 'Darwin', 'CMAKE_SYSTEM_PROCESSOR': cpu,
        'CMAKE_OSX_SYSROOT': str(sdk), 'CMAKE_OSX_ARCHITECTURES': cpu,
        'CMAKE_OSX_DEPLOYMENT_TARGET': minimum,
        'CMAKE_USER_MAKE_RULES_OVERRIDE': str(rules),
        'CMAKE_C_COMPILER': str(compiler / 'bin/clang'),
        'CMAKE_CXX_COMPILER': str(compiler / 'bin/clang++'),
        'CMAKE_ASM_COMPILER': str(compiler / 'bin/clang'),
        'CMAKE_C_COMPILER_TARGET': target, 'CMAKE_CXX_COMPILER_TARGET': target,
        'CMAKE_ASM_COMPILER_TARGET': target,
        'CMAKE_AR': str(compiler / 'bin/llvm-ar'),
        'CMAKE_RANLIB': str(compiler / 'bin/llvm-ranlib'),
        'CMAKE_LINKER': str(compiler / 'bin/ld64.lld'),
        'CMAKE_FIND_ROOT_PATH': str(sdk),
        'CMAKE_FIND_ROOT_PATH_MODE_PROGRAM': 'NEVER',
        'CMAKE_FIND_ROOT_PATH_MODE_LIBRARY': 'ONLY',
        'CMAKE_FIND_ROOT_PATH_MODE_INCLUDE': 'ONLY',
        'CMAKE_FIND_ROOT_PATH_MODE_PACKAGE': 'ONLY',
    }
    for kind in ('EXE', 'SHARED', 'MODULE'):
        # --ld-path selects the executable; the flavor tells Clang to emit
        # LLD's required Darwin platform/version arguments on Linux too.
        settings['CMAKE_' + kind + '_LINKER_FLAGS_INIT'] = '-fuse-ld=lld --ld-path=' + str(compiler / 'bin/ld64.lld')
    if any(']=]' in value for value in settings.values()):
        raise ValueError('Unsupported CMake bracket delimiter in build input')
    output.parent.mkdir(parents=True, exist_ok=True)
    # GenericSystem clears runtime flags after loading the toolchain; apply
    # this through CMake's supported post-platform rules hook. Darwin's own
    # sw_vers probe cannot identify deployment support on a Linux build host.
    rules.write_text('if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND '
                     'CMAKE_OSX_DEPLOYMENT_TARGET VERSION_GREATER_EQUAL "10.5")\n'
                     '  foreach(language C CXX OBJC OBJCXX)\n'
                     '    set(CMAKE_SHARED_LIBRARY_RUNTIME_${language}_FLAG "-Wl,-rpath,")\n'
                     '  endforeach()\nendif()\n')
    # Darwin-Initialize creates cache entries before project(); normal OSX
    # variables can be removed by that initialization under older policies.
    cache_types = {'CMAKE_OSX_SYSROOT': 'PATH', 'CMAKE_OSX_ARCHITECTURES': 'STRING',
                   'CMAKE_OSX_DEPLOYMENT_TARGET': 'STRING'}
    output.write_text(''.join('set(' + key + ' [=[' + value + ']=]' +
                             (' CACHE ' + cache_types[key] + ' ""' if key in cache_types else '') + ')\n'
                             for key, value in settings.items()))
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    for name in ('compiler-root', 'sdk', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    print(prepare(parser.parse_args()))
