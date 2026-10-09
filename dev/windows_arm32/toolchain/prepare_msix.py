#!/usr/bin/env python3
"""Build the full host MSIX packager in an isolated pinned checkout."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

from package_compiler import audit_native, sha

REVISION = '25a65f5c1690930813bcc10cdf1d59fa865f2bb1'
REPOSITORY = 'https://github.com/microsoft/msix-packaging.git'


def prepare(output, repository=REPOSITORY, jobs=2):
    host = platform.system()
    if host not in ('Darwin', 'Linux'):
        raise ValueError('The MSIX distribution requires a macOS or Linux host')
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    temporary = output / 'tmp'
    temporary.mkdir(exist_ok=True)
    env = dict(os.environ, TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary))
    source = output / 'source'
    if not source.exists():
        subprocess.run(['git', 'init', str(source)], env=env, check=True)
        subprocess.run(['git', '-C', str(source), 'remote', 'add', 'origin', repository],
                       env=env, check=True)
    revision = subprocess.run(['git', '-C', str(source), 'rev-parse', 'HEAD'],
                              env=env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                              text=True)
    if revision.returncode:
        subprocess.run(['git', '-C', str(source), 'fetch', '--depth', '1', 'origin', REVISION],
                       env=env, check=True)
        subprocess.run(['git', '-C', str(source), 'checkout', '--detach', 'FETCH_HEAD'],
                       env=env, check=True)
    elif revision.stdout.strip() != REVISION:
        raise ValueError('Unexpected MSIX source revision')
    changes = subprocess.check_output(['git', '-C', str(source), 'diff', '--name-status'],
                                      text=True).strip()
    # Upstream zlib CMake removes this pre-generated header in favor of its
    # build-local configured header. This occurs only in our private checkout.
    if changes not in ('', 'D\tlib/zlib/zconf.h'):
        raise ValueError('Refusing to build modified MSIX sources')
    build = output / 'build'
    settings = ['-D' + ('LINUX' if host == 'Linux' else 'MACOS') + '=ON',
                '-DMSIX_PACK=ON', '-DSKIP_BUNDLES=OFF',
                '-DXML_PARSER=xerces', '-DUSE_VALIDATION_PARSER=ON',
                '-DUSE_MSIX_SDK_ZLIB=ON', '-DUSE_SHARED_ZLIB=OFF',
                '-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON',
                '-DCMAKE_INSTALL_RPATH=' + ('$ORIGIN/../lib;$ORIGIN' if host == 'Linux'
                                           else '@loader_path/../lib;@loader_path')]
    subprocess.run(['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja',
                    '-DCMAKE_BUILD_TYPE=Release', *settings], env=env, check=True)
    subprocess.run(['cmake', '--build', str(build), '--target', 'makemsix',
                    '--parallel', str(jobs)], env=env, check=True)
    changes = subprocess.check_output(['git', '-C', str(source), 'diff', '--name-status'],
                                      text=True).strip()
    if changes not in ('', 'D\tlib/zlib/zconf.h'):
        raise ValueError('MSIX sources changed during compilation')
    distribution = output / 'distribution'
    if distribution.exists():
        raise ValueError('MSIX distribution already exists')
    (distribution / 'bin').mkdir(parents=True)
    (distribution / 'lib').mkdir()
    library = 'libmsix.so' if host == 'Linux' else 'libmsix.dylib'
    for name in ('bin/makemsix', 'lib/' + library):
        shutil.copy2(build / name, distribution / name)
    architecture = platform.machine()
    native = {
        'bin/makemsix': audit_native(distribution / 'bin/makemsix', {library}, architecture, root=distribution),
        'lib/' + library: audit_native(distribution / 'lib' / library,
                                      {library}, architecture, dylib=True, root=distribution),
    }
    for name in ('LICENSE', 'THIRD PARTY CODE NOTICE'):
        shutil.copy2(source / name, distribution / name)
    report = {'sourceRevision': REVISION, 'sourceBuilt': True,
              'cmakeFeatureSettings': settings, 'nativeDependencies': native,
              'upstreamBuildGeneratedSourceChanges': changes,
              'systemLibrariesCopied': False,
              'files': {str(p.relative_to(distribution)): sha(p)
                        for p in distribution.rglob('*') if p.is_file()}}
    (distribution / 'msix-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    return distribution


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-repository', default=REPOSITORY,
                        help='Pinned upstream repository or an existing local upstream clone')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    print(prepare(args.output, args.source_repository, args.jobs))
