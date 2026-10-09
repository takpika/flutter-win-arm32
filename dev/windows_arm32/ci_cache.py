#!/usr/bin/env python3
"""Key source-built CI archives by their actual native build inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tarfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inputs(kind):
    port = 'dev/windows_arm32/'
    compiler = [port + 'toolchain/build_compiler.py', port + 'toolchain/package_compiler.py',
                port + 'toolchain/compiler-source-manifest.json',
                port + 'toolchain/LLVM-LICENSE.txt']
    manifest = json.loads(Path(compiler[2]).read_text())
    compiler += [port + 'toolchain/' + entry['patch'] for entry in manifest['patches']]
    compiler.append(port + 'ci_cache.py')
    paths = compiler
    if kind != 'compiler':
        paths += ['DEPS', 'engine/src', port + 'patches',
                  *[port + name + '.py' for name in ('build_engine', 'configure_engine',
                    'build_frontend', 'prepare_sources', 'prepare_dependency', 'prepare_engine_checkout')],
                  *[port + 'toolchain/' + name for name in ('gnu_driver.py', 'windows_header_vfs.py',
                    'msvc_options.py', 'rt_driver.py', 'phone_driver.py', 'windows_guids.c',
                    'phone_job_api.def', 'phone_process_api.def', 'prepare_windows_sdk.py',
                    'generate_xaml_headers.py', 'generate_xps_header.py', 'generate_uia_headers.py',
                    'prepare_atl.py', 'msvc_codecvt_compat.h', 'sdk', 'compat_include')]]
    files = subprocess.check_output(['git', '-c', 'safe.directory=' + str(Path.cwd().resolve()),
                                     'ls-files', '-z', '--', *paths]).split(b'\0')
    records = {name.decode(): digest(Path(name.decode()).read_bytes()) for name in files if name}
    # Scope workflow settings to the native jobs. SDK assembly edits do not
    # invalidate compiler or engine archives, while native command edits do.
    jobs = [('compiler', '.github/workflows/windows-arm32-compiler.yml')]
    if kind != 'compiler':
        jobs.append(('engine', '.github/workflows/windows-arm32-engine.yml'))
    for job, workflow in jobs:
        text = Path(workflow).read_text()
        block = re.search(r'^  ' + job + r':\n.*?(?=^  [A-Za-z_]+:\n|\Z)', text, re.M | re.S)
        if block is None:
            raise ValueError('Missing native workflow job: ' + job)
        records[workflow + '#' + job] = digest(block.group().encode())
    environment = subprocess.check_output(['dpkg-query', '-W', '-f=${Package}=${Version}\n'], text=True)
    return {'schemaVersion': 1, 'kind': kind, 'host': 'ubuntu-24.04-x86_64',
            'hostPackagesSha256': digest('\n'.join(sorted(environment.splitlines())).encode()),
            'sourceInputs': records}


def verify(expected, stored, archive):
    if json.loads(expected.read_text()) != json.loads(stored.read_text()):
        raise ValueError('Cached archive native inputs differ from this checkout')
    wanted = archive.with_name(archive.name + '.sha256').read_text().split()[0]
    if digest(archive.read_bytes()) != wanted:
        raise ValueError('Cached archive checksum mismatch')
    kind = json.loads(expected.read_text())['kind']
    with tarfile.open(archive) as source:
        members = {Path(member.name).as_posix().removeprefix('./'): member for member in source}
        if any(Path(name).is_absolute() or '..' in Path(name).parts for name in members):
            raise ValueError('Unsafe cached archive member')
        def read(name):
            with source.extractfile(members[name]) as stream:
                return stream.read()
        if kind == 'compiler':
            report = json.loads(read('source-provenance.json'))
            if not report.get('sourceBuilt') or report['compilerSha256'] != digest(read('bin/clang')):
                raise ValueError('Cached compiler does not match source-build provenance')
            manifest = json.loads(Path('dev/windows_arm32/toolchain/compiler-source-manifest.json').read_text())
            if report['revision'] != manifest['upstreamRevision'] or report['patches'] != [
                    {'patch': entry['patch'], 'sha256': entry['patchSha256']} for entry in manifest['patches']]:
                raise ValueError('Cached compiler source or patches differ')
            distribution = json.loads(read('distribution-manifest.json'))
            for name, record in distribution['files'].items():
                if 'sha256' in record:
                    data = read(name)
                    if len(data) != record['bytes'] or digest(data) != record['sha256']:
                        raise ValueError('Cached compiler distribution file changed: ' + name)
                elif members[name].linkname != record['symlink']:
                    raise ValueError('Cached compiler alias changed: ' + name)
        else:
            report = json.loads(read('engine-build-provenance.json'))
            if not report['freshBuildInvocation']:
                raise ValueError('Cached engine lacks source-build provenance')
            if report['nativeBuildInputs'] != json.loads(expected.read_text()):
                raise ValueError('Cached engine provenance has different native inputs')
            manifests = {path.name: digest(path.read_bytes()) for path in
                         Path('dev/windows_arm32/patches').glob('*-source-manifest.json')}
            if report['sourceManifests'] != manifests:
                raise ValueError('Cached engine source manifests differ')
            for name, record in report['families'][kind]['artifacts'].items():
                data = read(name)
                if len(data) != record['bytes'] or digest(data) != record['sha256']:
                    raise ValueError('Cached engine artifact changed: ' + name)
            frontend = json.loads(read('frontend-source-provenance.json'))
            if (not frontend['compiledFromCurrentSource'] or
                    frontend['sourceManifestSha256'] != manifests['dart-source-manifest.json'] or
                    frontend['snapshotSha256'] != digest(read('frontend_server_aot.dart.snapshot'))):
                raise ValueError('Cached frontend does not match source-build provenance')
    print('Verified identical native inputs and source-built archive:', kind)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('inputs', 'verify'))
    parser.add_argument('--kind', choices=('compiler', 'rt', 'phone'))
    parser.add_argument('--expected', type=Path, required=True)
    parser.add_argument('--stored', type=Path)
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    if args.command == 'inputs':
        record = inputs(args.kind)
        args.expected.parent.mkdir(parents=True, exist_ok=True)
        args.expected.write_text(json.dumps(record, sort_keys=True, indent=2) + '\n')
        print(digest(json.dumps(record, sort_keys=True).encode()))
    else:
        verify(args.expected, args.stored, args.archive)
