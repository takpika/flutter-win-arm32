#!/usr/bin/env python3
"""Cross-build private Zstd and select macOS system zlib/XML declarations.

No system library bodies are copied. Compiler features remain the same as the
shared Linux/Windows builds; the macOS OS supplies its zlib and libxml2 ABI.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

from package_compiler import sha


def prepare(args):
    output, sdk, base = args.output.resolve(), args.sdk.resolve(), args.toolchain.resolve()
    if args.jobs < 1 or not base.is_file():
        raise ValueError('Supply a CMake cross toolchain and positive job count')
    if output.exists():
        raise ValueError('Use a new private dependency output directory')
    required = ['usr/include/zlib.h', 'usr/lib/libz.tbd', 'usr/lib/libxml2.2.tbd']
    for name in required:
        if not (sdk / name).is_file():
            raise ValueError('Missing macOS system declaration: ' + name)
    xml_include = next((directory for directory in
                        (sdk / 'usr/include/libxml2', sdk / 'usr/include')
                        if (directory / 'libxml/parser.h').is_file()), None)
    if xml_include is None:
        raise ValueError('Missing macOS libxml2 development declarations')
    prefix, temporary = output / 'prefix', output / 'tmp'
    temporary.mkdir(parents=True)
    environment = dict(os.environ, TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary))
    toolchain = output / 'macos.cmake'
    settings = {
        'CMAKE_FIND_ROOT_PATH': str(prefix) + ';' + str(sdk),
        'ZLIB_INCLUDE_DIR': str(sdk / 'usr/include'),
        'ZLIB_LIBRARY': str(sdk / 'usr/lib/libz.tbd'),
        'LIBXML2_INCLUDE_DIR': str(xml_include),
        'LIBXML2_LIBRARY': str(sdk / 'usr/lib/libxml2.2.tbd'),
    }
    if any(']=]' in str(value) for value in [base, *settings.values()]):
        raise ValueError('Unsupported CMake bracket delimiter')
    toolchain.write_text('include([=[' + str(base) + ']=])\n' + ''.join(
        'set(' + key + ' [=[' + value + ']=])\n'
        for key, value in settings.items()))
    if args.config_only:
        return toolchain
    manifest = json.loads(Path(__file__).with_name('host-dependencies.json').read_text())
    record = manifest['zstd']
    source = output / 'source-zstd'
    def run(command):
        subprocess.run([str(value) for value in command], env=environment, check=True)
    run(['git', 'init', source])
    run(['git', '-C', source, 'fetch', '--depth', '1', record['repository'], record['revision']])
    run(['git', '-C', source, 'checkout', '--detach', 'FETCH_HEAD'])
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != record['revision']:
        raise ValueError('Zstd source revision differs from the shared dependency pin')
    build = output / 'build-zstd'
    run(['cmake', '-G', 'Ninja', '-S', source / record['cmake'], '-B', build,
         '-DCMAKE_TOOLCHAIN_FILE=' + str(toolchain), '-DCMAKE_BUILD_TYPE=Release',
         '-DCMAKE_INSTALL_PREFIX=' + str(prefix)])
    run(['cmake', '--build', build, '--parallel', args.jobs])
    run(['cmake', '--install', build])
    for name in ('include/zstd.h', 'lib/libzstd.a'):
        if not (prefix / name).is_file():
            raise ValueError('Missing private static Zstd installation: ' + name)
    notices = prefix / 'share/licenses/zstd'
    notices.mkdir(parents=True, exist_ok=True)
    for name in ('LICENSE', 'COPYING'):
        shutil.copy2(source / name, notices / name)
    report = {'host': args.host, 'sources': {'zstd': record},
              'systemDeclarations': {name: sha(sdk / name) for name in required},
              'libxml2Include': str(xml_include.relative_to(sdk)),
              'cmakeBaseToolchainSha256': sha(base), 'featureDisableFlags': [],
              'systemLibrariesCopied': False,
              'files': {str(path.relative_to(prefix)): sha(path)
                        for path in prefix.rglob('*') if path.is_file()}}
    (prefix / 'host-dependency-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    return toolchain


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', choices=('macos-x64', 'macos-arm64'), required=True)
    for name in ('output', 'sdk', 'toolchain'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--config-only', action='store_true')
    print(prepare(parser.parse_args()))
