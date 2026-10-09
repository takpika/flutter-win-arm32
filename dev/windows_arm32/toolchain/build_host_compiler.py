#!/usr/bin/env python3
"""Cross-build the shared patched LLVM compiler for an official SDK host.

The existing native builder owns source preparation and compiler capabilities.
The supplied CMake toolchain and dependency prefixes select the destination
host without modifying the installed build compiler or the Linux CI builder.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

from build_compiler import sha, prepare
from package_compiler import TOOLS
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from sdk_hosts import select_host, verify_native_host


def build(args):
    host = select_host(args.host)
    output, source, toolchain = args.output.resolve(), args.source.resolve(), args.toolchain.resolve()
    if args.jobs < 1 or not toolchain.is_file():
        raise ValueError('Supply a CMake cross toolchain and a positive job count')
    manifest = prepare(source)
    original = sha(toolchain)
    temporary = output / 'tmp'
    temporary.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, CMAKE_TOOLCHAIN_FILE=str(toolchain),
                       TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary))
    native_builder = Path(__file__).with_name('build_compiler.py')
    configure = [sys.executable, str(native_builder), str(source), '--output', str(output),
                 '--configure-only', '--jobs', str(args.jobs)]
    for prefix in args.dependency_prefix:
        configure += ['--dependency-prefix', str(prefix.resolve())]
    subprocess.run(configure, env=environment, check=True)
    cache = {}
    for line in (output / 'CMakeCache.txt').read_text().splitlines():
        if ':' in line and '=' in line and not line.startswith(('#', '//')):
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    # A host transition cannot silently lose the native compiler's features.
    required = {'LLVM_ENABLE_ASSERTIONS': 'ON', 'LLVM_TARGETS_TO_BUILD': 'all',
                'LLVM_ENABLE_PROJECTS': 'clang;lld',
                'LLVM_ENABLE_ZLIB': 'FORCE_ON', 'LLVM_ENABLE_ZSTD': 'FORCE_ON',
                'LLVM_ENABLE_LIBXML2': 'FORCE_ON', 'LLVM_USE_STATIC_ZSTD': 'ON'}
    if any(cache.get(key) != value for key, value in required.items()):
        raise ValueError('Cross compiler capability settings differ from the shared native compiler')
    if Path(cache['CMAKE_TOOLCHAIN_FILE']).resolve() != toolchain:
        raise ValueError('Cross build directory belongs to a different CMake toolchain')
    selected_tools = getattr(args, 'tools_only', None)
    targets = selected_tools or ['clang', 'clang-resource-headers', 'lld', 'LTO',
                                *(name for name in TOOLS if name.startswith('llvm-'))]
    subprocess.run(['cmake', '--build', str(output), '--parallel', str(args.jobs),
                    '--target', *targets], env=environment, check=True)
    suffix = '.exe' if host['os'] == 'windows' else ''
    compiler = output / 'bin' / ('clang' + suffix)
    for name in selected_tools or ['clang']:
        verify_native_host(output / 'bin' / (name + suffix), host)
    if sha(toolchain) != original:
        raise ValueError('Host toolchain changed during compilation')
    report = {'revision': manifest['upstreamRevision'],
              'patches': [{'patch': entry['patch'], 'sha256': entry['patchSha256']}
                          for entry in manifest['patches']],
              'compilerSha256': sha(compiler) if compiler.is_file() else None,
              'builtTools': selected_tools or list(TOOLS), 'sourceBuilt': True, 'targets': 'all',
              'requiredFeatures': ['zlib', 'zstd', 'libxml2'], 'host': host['id'],
              'cmakeToolchainSha256': original, 'nativeBuilderSha256': sha(native_builder),
              'runtimeExecutionVerified': False, 'systemLibrariesCopied': False}
    (output / 'source-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    for name in ('source', 'output', 'toolchain'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--dependency-prefix', type=Path, action='append', default=[])
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--tools-only', nargs='+', choices=('llvm-rc', 'llvm-cvtres'),
                        help='Build resource utilities separately with unchanged shared compiler capabilities')
    args = parser.parse_args()
    print(json.dumps(build(args)))
