#!/usr/bin/env python3
"""Cross-build private Windows x64 compiler dependencies on Ubuntu amd64."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tarfile

from package_compiler import sha


def prepare(args):
    output, gnu = args.output.resolve(), args.gnu.resolve()
    output.mkdir(parents=True, exist_ok=True)
    prefix = output / 'prefix'
    temporary = output / 'tmp'
    temporary.mkdir(exist_ok=True)
    environment = dict(os.environ, TMPDIR=str(temporary), TMP=str(temporary),
                       TEMP=str(temporary), PATH=str(gnu / 'bin') + os.pathsep + os.environ['PATH'])
    toolchain = output / 'windows-x64.cmake'
    toolchain.write_text('set(CMAKE_SYSTEM_NAME Windows)\nset(CMAKE_SYSTEM_PROCESSOR x86_64)\n' +
        ''.join('set(CMAKE_' + key + ' "' + str(gnu / 'bin' / value) + '")\n' for key, value in (
            ('C_COMPILER', 'x86_64-w64-mingw32-clang'),
            ('CXX_COMPILER', 'x86_64-w64-mingw32-clang++'),
            ('RC_COMPILER', 'x86_64-w64-mingw32-windres'))) +
        'set(CMAKE_FIND_ROOT_PATH "' + str(prefix) + ';' + str(gnu / 'x86_64-w64-mingw32') + '")\n'
        'set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)\n'
        'set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)\n'
        'set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)\n'
        'set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)\n')
    records = json.loads(Path(__file__).with_name('host-dependencies.json').read_text())
    def run(command, **kwargs):
        subprocess.run([str(v) for v in command], env=environment, check=True, **kwargs)
    for name, record in records.items():
        source = output / ('source-' + name)
        if source.exists():
            raise ValueError('Dependency source already exists: ' + str(source))
        if 'repository' in record:
            run(['git', 'init', source])
            run(['git', '-C', source, 'fetch', '--depth', '1', record['repository'], record['revision']])
            run(['git', '-C', source, 'checkout', '--detach', 'FETCH_HEAD'])
            revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
            if revision != record['revision']:
                raise ValueError('Dependency revision differs: ' + name)
        else:
            archive = output / Path(record['url']).name
            run(['curl', '--fail', '--location', '--retry', '3', record['url'], '--output', archive])
            if sha(archive) != record['sha256']:
                raise ValueError('Dependency source checksum differs: ' + name)
            with tarfile.open(archive) as package:
                package.extractall(output / ('extract-' + name), filter='data')
            roots = list((output / ('extract-' + name)).iterdir())
            if len(roots) != 1 or not roots[0].is_dir():
                raise ValueError('Unexpected dependency source layout')
            roots[0].rename(source)
        build = output / ('build-' + name)
        if name == 'iconv':
            build.mkdir()
            run([source / 'configure', '--host=x86_64-w64-mingw32', '--prefix=' + str(prefix),
                 'CC=x86_64-w64-mingw32-clang', 'CXX=x86_64-w64-mingw32-clang++'], cwd=build)
            run(['make', '-j' + str(args.jobs)], cwd=build)
            run(['make', 'install'], cwd=build)
        else:
            flags = []
            if name == 'xz':
                flags += ['-DBUILD_SHARED_LIBS=ON']
            if name == 'libxml2':
                target_python = args.windows_python.resolve()
                flags += ['-DPython_EXECUTABLE=' + str(args.native_python.resolve()),
                          '-DPython_INCLUDE_DIR=' + str(target_python / 'include'),
                          '-DPython_LIBRARY=' + str(target_python / 'libs/python314.lib'),
                          '-DLIBXML2_WITH_LEGACY=ON', '-DLIBXML2_WITH_HTTP=ON',
                          '-DLIBXML2_WITH_LZMA=ON', '-DLIBXML2_WITH_ZLIB=ON']
            run(['cmake', '-G', 'Ninja', '-S', source / record['cmake'], '-B', build,
                 '-DCMAKE_TOOLCHAIN_FILE=' + str(toolchain), '-DCMAKE_BUILD_TYPE=Release',
                 '-DCMAKE_INSTALL_PREFIX=' + str(prefix), '-DCMAKE_PREFIX_PATH=' + str(prefix), *flags])
            run(['cmake', '--build', build, '--parallel', args.jobs])
            run(['cmake', '--install', build])
        notices = prefix / 'share/licenses' / name
        notices.mkdir(parents=True, exist_ok=True)
        import shutil
        for path in source.iterdir():
            if path.is_file() and path.name.upper().startswith(('LICENSE', 'COPYING', 'COPYRIGHT')):
                shutil.copy2(path, notices / path.name)
        if not list(notices.iterdir()):
            raise ValueError('Missing dependency license: ' + name)
    required = ['include/zlib.h', 'include/zstd.h', 'include/lzma.h',
                'include/iconv.h', 'include/libxml2/libxml/parser.h']
    for name in required:
        if not (prefix / name).is_file():
            raise ValueError('Missing full dependency installation: ' + name)
    report = {'host': 'windows-x64', 'sources': records, 'systemLibrariesCopied': False,
              'featureDisableFlags': [], 'files': {str(p.relative_to(prefix)): sha(p)
                  for p in prefix.rglob('*') if p.is_file()}}
    (prefix / 'host-dependency-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    return toolchain


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('output', 'gnu', 'native-python', 'windows-python'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    print(prepare(parser.parse_args()))
