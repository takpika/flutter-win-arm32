#!/usr/bin/env python3
"""Stage the full Windows host compiler and its private runtime DLL closure."""
import argparse
import json
from pathlib import Path
import shutil
import sys
import tarfile

from package_compiler import TOOLS, sha
from windows_native_dependencies import audit, system_library
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sdk_hosts import select_host, verify_native_host


def package(source, dependencies, gnu, output, llvm_readobj):
    source, dependencies, gnu, output = (p.resolve() for p in (source, dependencies, gnu, output))
    host = select_host('windows-x64')
    provenance = json.loads((source / 'source-provenance.json').read_text())
    if (not provenance.get('sourceBuilt') or provenance.get('host') != host['id'] or
            provenance.get('targets') != 'all' or
            set(provenance.get('requiredFeatures', [])) != {'zlib', 'zstd', 'libxml2'} or
            sha(source / 'bin/clang.exe') != provenance['compilerSha256']):
        raise ValueError('Windows compiler source-build provenance differs')
    manifest = json.loads(Path(__file__).with_name('compiler-source-manifest.json').read_text())
    expected_patches = [{'patch': item['patch'], 'sha256': item['patchSha256']}
                        for item in manifest['patches']]
    if (provenance['revision'] != manifest['upstreamRevision'] or
            provenance['patches'] != expected_patches or
            provenance['nativeBuilderSha256'] != sha(Path(__file__).with_name('build_compiler.py'))):
        raise ValueError('Compiler does not match the current shared source and native builder')
    dependency_record = json.loads((dependencies / 'host-dependency-provenance.json').read_text())
    for name, expected in dependency_record['files'].items():
        if sha(dependencies / name) != expected:
            raise ValueError('Private dependency installation changed: ' + name)
    if output.exists():
        raise ValueError('Compiler staging output already exists')
    (output / 'bin').mkdir(parents=True)
    def stage(path):
        verify_native_host(path, host)
        if system_library(path.name):
            raise ValueError('Refusing to stage Windows system DLL: ' + path.name)
        destination = output / 'bin' / path.name
        shutil.copy2(path, destination)
        if sha(destination) != sha(path):
            raise ValueError('Compiler runtime changed while copying')
        return destination
    for name in TOOLS:
        # Materialize executable aliases for normal Windows ZIP extraction.
        stage(source / 'bin' / (name + '.exe'))
    lto = [p for directory in ('bin', 'lib') for p in (source / directory).glob('*LTO*.dll')]
    if not lto or len({sha(p) for p in lto}) != 1:
        raise ValueError('Expected one unambiguous native Windows LTO library')
    stage(lto[0])
    resource = source / 'lib/clang'
    if not list(resource.glob('*/include/stddef.h')):
        raise ValueError('Compiler resource headers missing')
    shutil.copytree(resource, output / 'lib/clang')
    # Runtime DLLs are selected only from the source build, its verified private
    # dependencies, and the pinned GNU distribution, never the host OS.
    roots = [source / 'bin', source / 'lib', dependencies / 'bin', dependencies / 'lib',
             gnu / 'x86_64-w64-mingw32/bin']
    candidates = {}
    for root in roots:
        if root.is_dir():
            for path in root.iterdir():
                if path.is_file() and path.suffix.lower() == '.dll':
                    candidates.setdefault(path.name.lower(), []).append(path)
    inspected = set()
    native = {}
    while True:
        pending = [p for p in (output / 'bin').iterdir() if p.name not in inspected]
        if not pending:
            break
        for path in pending:
            verify_native_host(path, host)
            # Allow known candidates during discovery; the final pass permits
            # only DLLs actually staged in the payload.
            imports = audit(path, set(candidates), llvm_readobj)
            inspected.add(path.name)
            native[str(path.relative_to(output))] = imports
            for name in imports:
                if system_library(name):
                    continue
                if any(p.name.lower() == name.lower() for p in (output / 'bin').iterdir()):
                    continue
                choices = candidates.get(name.lower(), [])
                if not choices or len({sha(p) for p in choices}) != 1:
                    raise ValueError('Missing or ambiguous private Windows DLL: ' + name)
                stage(choices[0])
    libraries = {p.name for p in (output / 'bin').iterdir() if p.suffix.lower() == '.dll'}
    for path in (output / 'bin').iterdir():
        audit(path, libraries, llvm_readobj)
    shutil.copy2(Path(__file__).with_name('LLVM-LICENSE.txt'), output)
    shutil.copytree(dependencies / 'share/licenses', output / 'licenses/dependencies')
    shutil.copy2(gnu / 'LICENSE.TXT', output / 'licenses/GNU-runtime-LICENSE.txt')
    (output / 'source-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    (output / 'host-dependency-provenance.json').write_text(json.dumps(dependency_record, indent=2) + '\n')
    files = {str(p.relative_to(output)): {'sha256': sha(p), 'bytes': p.stat().st_size}
             for p in output.rglob('*') if p.is_file()}
    (output / 'distribution-manifest.json').write_text(json.dumps({
        'host': host['id'], 'hostArchitecture': 'x86_64', 'files': files,
        'nativeDependencies': native, 'systemLibrariesCopied': False,
        'runtimeExecutionVerified': False,
    }, indent=2) + '\n')
    archive = output.with_name(output.name + '.tar.gz')
    with tarfile.open(archive, 'w:gz') as package:
        package.add(output, arcname='.')
    archive.with_name(archive.name + '.sha256').write_text(sha(archive) + '  ' + archive.name + '\n')
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'dependencies', 'gnu', 'output', 'llvm-readobj'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    print(package(args.source, args.dependencies, args.gnu, args.output, args.llvm_readobj))
