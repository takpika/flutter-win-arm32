#!/usr/bin/env python3
"""Package source-built Mach-O tools on Linux without modifying signed bytes."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile

from package_compiler import TOOLS, sha
from macho_native_dependencies import audit
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sdk_hosts import select_host, verify_native_host


def inspect(path, host, objdump, libraries):
    imports = audit(path, libraries, host, objdump)
    architecture = 'x86_64' if host['cpu'] == 'x64' else 'arm64'
    headers = subprocess.check_output([str(objdump), '--macho', '--private-headers',
                                       '--arch=' + architecture, str(path)], text=True)
    platforms = re.findall(r'cmd LC_BUILD_VERSION\s+cmdsize \d+\s+platform (\S+)', headers)
    if platforms != ['macos'] and not (
            not platforms and 'cmd LC_VERSION_MIN_MACOSX' in headers):
        raise ValueError('Mach-O payload is not built for macOS: ' + str(path))
    rpaths = re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', headers)
    allowed = {'@loader_path/../lib', '@loader_path', '@executable_path/../lib'}
    if any(value not in allowed for value in rpaths):
        raise ValueError('Non-relocatable Mach-O runtime search path: ' + str(path))
    for name in imports:
        if name.startswith('@'):
            if name != '@rpath/' + Path(name).name or not rpaths:
                raise ValueError('Unresolved private Mach-O dependency: ' + name)
            if path.parent.name == 'bin' and not (
                    {'@loader_path/../lib', '@executable_path/../lib'} & set(rpaths)):
                raise ValueError('Private Mach-O dependency is outside the binary search path: ' + name)
    signed = 'cmd LC_CODE_SIGNATURE' in headers
    if host['cpu'] == 'arm64' and not signed:
        raise ValueError('ARM64 Mach-O requires a linker-generated code signature')
    return {'imports': imports, 'rpaths': rpaths, 'platform': 'macos',
            'codeSignaturePresent': signed}


def package(source, dependencies, output, host_name, objdump):
    source, dependencies, output, objdump = (
        path.resolve() for path in (source, dependencies, output, objdump))
    host = select_host(host_name)
    if host['os'] != 'macos' or output.exists():
        raise ValueError('Select a macOS host and a new output directory')
    provenance = json.loads((source / 'source-provenance.json').read_text())
    manifest = json.loads(Path(__file__).with_name('compiler-source-manifest.json').read_text())
    patches = [{'patch': entry['patch'], 'sha256': entry['patchSha256']}
               for entry in manifest['patches']]
    if (not provenance.get('sourceBuilt') or provenance.get('host') != host['id'] or
            provenance.get('targets') != 'all' or
            set(provenance.get('requiredFeatures', [])) != {'zlib', 'zstd', 'libxml2'} or
            provenance.get('revision') != manifest['upstreamRevision'] or
            provenance.get('patches') != patches or
            provenance.get('nativeBuilderSha256') != sha(Path(__file__).with_name('build_compiler.py')) or
            provenance.get('compilerSha256') != sha(source / 'bin/clang')):
        raise ValueError('Compiler differs from the current shared source-build provenance')
    dependency_record = json.loads((dependencies / 'host-dependency-provenance.json').read_text())
    if dependency_record.get('host') != host['id']:
        raise ValueError('Private dependencies belong to another host')
    for name, expected in dependency_record['files'].items():
        if sha(dependencies / name) != expected:
            raise ValueError('Private dependency installation changed: ' + name)
    resource = source / 'lib/clang'
    if not list(resource.glob('*/include/stddef.h')):
        raise ValueError('Compiler resource headers missing')
    library = source / 'lib/libLTO.dylib'
    verify_native_host(library, host)
    architecture = 'x86_64' if host['cpu'] == 'x64' else 'arm64'
    ids = subprocess.check_output([str(objdump), '--macho', '--dylib-id',
                                    '--arch=' + architecture, str(library)], text=True)
    names = [line.strip() for line in ids.splitlines()[1:] if line.strip()]
    if len(names) != 1 or not re.fullmatch(r'@rpath/libLTO(?:\.[A-Za-z0-9_.-]+)?\.dylib', names[0]):
        raise ValueError('LTO library must use its upstream relocatable install name')
    library_name = Path(names[0]).name
    libraries = {library_name, 'libLTO.dylib'}
    native = {}
    # Audit every source binary before allocating the distribution copy.
    for name in TOOLS:
        native['bin/' + name] = inspect(source / 'bin' / name, host, objdump, libraries)
    native['lib/' + library_name] = inspect(library, host, objdump, libraries)
    (output / 'bin').mkdir(parents=True)
    (output / 'lib').mkdir()
    copied = {}
    for name in TOOLS:
        actual = (source / 'bin' / name).resolve()
        target = output / 'bin' / name
        if actual in copied:
            target.symlink_to(copied[actual])
        else:
            shutil.copy2(actual, target)
            copied[actual] = name
        if sha(target) != sha(actual):
            raise ValueError('Compiler changed during staging')
    shutil.copy2(library, output / 'lib' / library_name)
    if sha(output / 'lib' / library_name) != sha(library):
        raise ValueError('LTO library changed during staging')
    if library_name != 'libLTO.dylib':
        (output / 'lib/libLTO.dylib').symlink_to(library_name)
    shutil.copytree(resource, output / 'lib/clang')
    shutil.copy2(Path(__file__).with_name('LLVM-LICENSE.txt'), output)
    shutil.copytree(dependencies / 'share/licenses', output / 'licenses/dependencies')
    for name, record in [('source-provenance.json', provenance),
                         ('host-dependency-provenance.json', dependency_record)]:
        (output / name).write_text(json.dumps(record, indent=2) + '\n')
    files = {}
    for path in sorted(output.rglob('*')):
        name = str(path.relative_to(output))
        if path.is_symlink():
            files[name] = {'symlink': str(path.readlink())}
        elif path.is_file():
            files[name] = {'sha256': sha(path), 'bytes': path.stat().st_size}
    (output / 'distribution-manifest.json').write_text(json.dumps({
        'host': host['id'], 'files': files, 'nativeDependencies': native,
        'systemLibrariesCopied': False, 'binaryBytesUnmodified': True,
        'codeSignatureIntegrityVerified': False, 'runtimeExecutionVerified': False,
    }, indent=2) + '\n')
    archive = output.with_name(output.name + '.tar.gz')
    with tarfile.open(archive, 'w:gz') as package_file:
        package_file.add(output, arcname='.')
    archive.with_name(archive.name + '.sha256').write_text(sha(archive) + '  ' + archive.name + '\n')
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'dependencies', 'output', 'llvm-objdump'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--host', choices=('macos-x64', 'macos-arm64'), required=True)
    args = parser.parse_args()
    print(package(args.source, args.dependencies, args.output, args.host, args.llvm_objdump))
