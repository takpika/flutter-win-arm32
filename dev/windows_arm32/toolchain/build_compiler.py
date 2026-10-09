#!/usr/bin/env python3
"""Build the pinned LLVM tools with the MSVC language compatibility patch."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(source):
    directory = Path(__file__).resolve().parent
    manifest = json.loads((directory / 'compiler-source-manifest.json').read_text())
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip()
    if revision != manifest['upstreamRevision']:
        raise ValueError('LLVM checkout must be at ' + manifest['upstreamRevision'])
    for entry in manifest.get('patches', [manifest]):
        patch = directory / entry['patch']
        if sha(patch) != entry['patchSha256']:
            raise ValueError('Compiler patch differs from its pinned source manifest')
        files = entry['files']
        if not all(sha(source / item['path']) == item['patchedSha256'] for item in files):
            if not all(sha(source / item['path']) == item['originalSha256'] for item in files):
                raise ValueError('Refusing to replace modified compiler sources')
            subprocess.run(['git', 'apply', '--check', str(patch)], cwd=source, check=True)
            subprocess.run(['git', 'apply', str(patch)], cwd=source, check=True)
        if not all(sha(source / item['path']) == item['patchedSha256'] for item in files):
            raise ValueError('Patched compiler source hash mismatch')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--configure-only', action='store_true')
    parser.add_argument('--dependency-prefix', type=Path, action='append', default=[],
                        help='Prefix for existing compression/XML dependencies')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    source, output = args.source.resolve(), args.output.resolve()
    manifest = prepare(source)
    scratch = output / 'tmp'
    scratch.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TMPDIR=str(scratch), TMP=str(scratch), TEMP=str(scratch))
    dependency_flags = []
    if args.dependency_prefix:
        dependency_flags = ['-DCMAKE_PREFIX_PATH=' + ';'.join(
            str(path.resolve()) for path in args.dependency_prefix)]
    subprocess.run(['cmake', '-G', 'Ninja', '-S', str(source / 'llvm'), '-B', str(output),
                    '-DCMAKE_BUILD_TYPE=Release', '-DLLVM_ENABLE_PROJECTS=clang;lld',
                    '-DLLVM_TARGETS_TO_BUILD=all',
                    '-DLLVM_ENABLE_ASSERTIONS=ON', '-DLLVM_INCLUDE_TESTS=OFF',
                    '-DLLVM_INCLUDE_BENCHMARKS=OFF', '-DLLVM_ENABLE_ZLIB=FORCE_ON',
                    '-DLLVM_ENABLE_ZSTD=FORCE_ON', '-DLLVM_USE_STATIC_ZSTD=ON',
                    '-DLLVM_ENABLE_LIBXML2=FORCE_ON',
                    '-DLLVM_PARALLEL_LINK_JOBS=1', *dependency_flags], env=env, check=True)
    if args.configure_only:
        return
    subprocess.run(['cmake', '--build', str(output), '--parallel', str(args.jobs),
                    '--target', 'clang', 'clang-resource-headers', 'lld', 'LTO', 'llvm-ar',
                    'llvm-ranlib', 'llvm-mc', 'llvm-objcopy', 'llvm-objdump',
                    'llvm-readobj', 'llvm-readelf', 'llvm-nm', 'llvm-symbolizer', 'llvm-dlltool'], env=env, check=True)
    (output / 'source-provenance.json').write_text(json.dumps({
        'revision': manifest['upstreamRevision'],
        'patches': [{'patch': entry['patch'], 'sha256': entry['patchSha256']}
                    for entry in manifest.get('patches', [manifest])],
        'compilerSha256': sha(output / 'bin/clang'),
        'sourceBuilt': True,
        'targets': 'all',
        'requiredFeatures': ['zlib', 'zstd', 'libxml2'],
    }, indent=2) + '\n')


if __name__ == '__main__':
    main()
