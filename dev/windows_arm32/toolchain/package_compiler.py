#!/usr/bin/env python3
"""Package the complete source-built host compiler without system libraries."""
import argparse
import gzip
import hashlib
import json
import os
import platform
import re
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

TOOLS = ('clang', 'clang++', 'lld', 'ld.lld', 'ld64.lld', 'lld-link', 'wasm-ld',
         'llvm-ar', 'llvm-ranlib', 'llvm-mc', 'llvm-objcopy', 'llvm-objdump',
         'llvm-readobj', 'llvm-readelf', 'llvm-nm', 'llvm-symbolizer', 'llvm-dlltool')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dependencies(path, architecture):
    output = subprocess.check_output(['otool', '-arch', architecture, '-L', str(path)], text=True)
    return [line.strip().split(' (compatibility version', 1)[0]
            for line in output.splitlines()[1:] if line.strip()]


def audit_native(path, libraries, architecture, dylib=False, root=None):
    with path.open('rb') as stream:
        elf = stream.read(4) == b'\x7fELF'
    if elf:
        header = subprocess.check_output(['readelf', '-h', str(path)], text=True)
        if architecture != 'x86_64' or 'Advanced Micro Devices X86-64' not in header:
            raise ValueError('Unexpected ELF host architecture: ' + str(path))
        dynamic = subprocess.check_output(['readelf', '-d', str(path)], text=True)
        imports = re.findall(r'\(NEEDED\).*\[([^\]]+)\]', dynamic)
        system = {'libc.so.6', 'libm.so.6', 'libdl.so.2', 'libpthread.so.0',
                  'librt.so.1', 'libgcc_s.so.1', 'libstdc++.so.6', 'libz.so.1',
                  'libxml2.so.2', 'ld-linux-x86-64.so.2', 'libutil.so.1',
                  'libresolv.so.2', 'libtinfo.so.6', 'libicuuc.so.74',
                  'libicudata.so.74', 'libicui18n.so.74'}
        for name in imports:
            if name.startswith(('$ORIGIN/', '${ORIGIN}/')):
                suffix=name.split('/',1)[1]
                dependency=(path.parent / suffix).resolve()
                boundary=(root or path.parent).resolve()
                if dependency.is_relative_to(boundary) and dependency.is_file() and dependency.name in libraries:
                    continue
                raise ValueError('External or missing origin-relative dependency: ' + name)
            if name not in libraries and name not in system:
                raise ValueError('Unpackaged native dependency: ' + name)
        for paths in re.findall(r'\((?:RPATH|RUNPATH)\).*\[([^\]]+)\]', dynamic):
            if any(not part.startswith(('$ORIGIN', '${ORIGIN}')) for part in paths.split(':')):
                raise ValueError('Non-relocatable ELF runtime search path: ' + paths)
        return imports
    arches = subprocess.check_output(['lipo', '-archs', str(path)], text=True).split()
    if architecture not in arches:
        raise ValueError('Missing host architecture in ' + str(path))
    imports = dependencies(path, architecture)
    if dylib:
        imports = imports[1:]  # otool includes the dylib's own install name.
    for name in imports:
        if name.startswith(('/usr/lib/', '/System/Library/')):
            continue
        if name.startswith(('@rpath/', '@loader_path/', '@executable_path/')):
            if Path(name).name in libraries:
                continue
        raise ValueError('Unpackaged compiler dependency: ' + name)
    return imports


def package(source, destination, zstd, architecture):
    source, destination, zstd = source.resolve(), destination.resolve(), zstd.resolve()
    if not destination.name.endswith('.tar.gz'):
        raise ValueError('Output must end in .tar.gz')
    required = [source / 'bin' / name for name in TOOLS]
    linux = platform.system() == 'Linux'
    lto = 'libLTO.so' if linux else 'libLTO.dylib'
    notices = (('share/doc/libzstd-dev/copyright', 'Zstd-LICENSE.txt'),) if linux else (
        ('LICENSE', 'Zstd-LICENSE.txt'), ('COPYING', 'Zstd-COPYING.txt'))
    required += [source / ('lib/' + lto), source / 'source-provenance.json']
    required += [zstd / name for name, _ in notices]
    for path in required:
        if not path.is_file():
            raise ValueError('Missing source-built compiler payload: ' + str(path))
    resource = source / 'lib/clang'
    if not list(resource.glob('*/include/stddef.h')):
        raise ValueError('Missing Clang resource headers')
    provenance = json.loads((source / 'source-provenance.json').read_text())
    if not provenance.get('sourceBuilt') or provenance['compilerSha256'] != sha(source / 'bin/clang'):
        raise ValueError('Compiler does not match its source-build provenance')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='compiler-package-', dir=destination.parent) as scratch:
        stage = Path(scratch) / 'distribution'
        (stage / 'bin').mkdir(parents=True)
        (stage / 'lib').mkdir()
        copied = {}
        for name in TOOLS:
            actual = (source / 'bin' / name).resolve()
            target = stage / 'bin' / name
            if actual in copied:
                target.symlink_to(copied[actual])
            else:
                shutil.copy2(actual, target)
                copied[actual] = name
        shutil.copytree(resource, stage / 'lib/clang')
        library = stage / 'lib' / lto
        shutil.copy2(source / 'lib' / lto, library)
        # Change only this staged copy, never the shared build output.
        if linux:
            for path in {p.resolve() for p in (stage / 'bin').iterdir()} | {library}:
                subprocess.run(['patchelf', '--set-rpath', '$ORIGIN/../lib:$ORIGIN', str(path)], check=True)
        else:
            subprocess.run(['install_name_tool', '-id', '@rpath/' + lto, str(library)], check=True)
            subprocess.run(['codesign', '--force', '--sign', '-', str(library)], check=True)
            subprocess.run(['codesign', '--verify', '--strict', str(library)], check=True)
        native = {}
        for name in TOOLS:
            path = stage / 'bin' / name
            native['bin/' + name] = audit_native(path, {lto}, architecture, root=stage)
        native['lib/' + lto] = audit_native(library, {lto}, architecture, True, root=stage)
        # RPATH is changed only in the distribution. Preserve the build hash
        # alongside the staged compiler hash so provenance remains verifiable.
        staged_provenance = dict(provenance)
        staged_provenance['buildCompilerSha256'] = provenance['compilerSha256']
        staged_provenance['compilerSha256'] = sha(stage / 'bin/clang')
        (stage / 'source-provenance.json').write_text(json.dumps(staged_provenance, indent=2) + '\n')
        shutil.copy2(Path(__file__).resolve().parent / 'LLVM-LICENSE.txt', stage)
        for name, target in notices:
            shutil.copy2(zstd / name, stage / target)
        files = {}
        for path in sorted(stage.rglob('*')):
            if path.is_symlink():
                files[str(path.relative_to(stage))] = {'symlink': os.readlink(path)}
            elif path.is_file():
                files[str(path.relative_to(stage))] = {'sha256': sha(path), 'bytes': path.stat().st_size}
        (stage / 'distribution-manifest.json').write_text(json.dumps({
            'hostArchitecture': architecture, 'files': files,
            'nativeDependencies': native, 'systemLibrariesCopied': False,
        }, indent=2) + '\n')
        temporary = Path(scratch) / 'compiler.tar.gz'
        with temporary.open('wb') as stream, gzip.GzipFile(fileobj=stream, mode='wb', mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w') as archive:
                for path in sorted(stage.rglob('*')):
                    info = archive.gettarinfo(str(path), arcname=str(path.relative_to(stage)))
                    info.uid = info.gid = info.mtime = 0
                    info.uname = info.gname = ''
                    if info.isfile():
                        with path.open('rb') as payload:
                            archive.addfile(info, payload)
                    else:
                        archive.addfile(info)
        shutil.move(temporary, destination)
    destination.with_name(destination.name + '.sha256').write_text(sha(destination) + '  ' + destination.name + '\n')
    return destination


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--zstd-prefix', type=Path, required=True)
    parser.add_argument('--architecture', choices=('arm64', 'x86_64'), required=True)
    args = parser.parse_args()
    print(package(args.source, args.output, args.zstd_prefix, args.architecture))
