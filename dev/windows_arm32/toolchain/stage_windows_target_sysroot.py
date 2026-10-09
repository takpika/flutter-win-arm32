#!/usr/bin/env python3
"""Stage host-independent MinGW target data beside the common host compiler.

Retain every target header, import/static library and target runtime from the
pinned distribution. Host compiler executables remain in their own component.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil


def sha(path):
    result = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1048576), b''):
            result.update(block)
    return result.hexdigest()


def stage(source, destination):
    source, destination = source.resolve(), destination.resolve()
    if destination.exists():
        raise ValueError('Target sysroot destination already exists')
    targets = sorted(p for p in source.iterdir()
                     if p.is_dir() and p.name.endswith('-w64-mingw32'))
    if not {'generic-w64-mingw32', 'armv7-w64-mingw32'}.issubset(p.name for p in targets):
        raise ValueError('Missing complete ARM32 and generic target headers/libraries')
    directories = [*targets, source / 'lib/clang']
    directories += [source / name for name in ('include', 'share') if (source / name).is_dir()]
    if not list((source / 'lib/clang').glob('*/lib/windows/libclang_rt.builtins-arm.a')):
        raise ValueError('Missing the ARM32 compiler runtime')
    # Audit target data links before copying; materialize them for Windows ZIP
    # distribution and retain the layout expected by Clang's sysroot search.
    original = {}
    for directory in directories:
        for folder, children, files in os.walk(directory, followlinks=True):
            parent = Path(folder)
            if not parent.resolve().is_relative_to(source):
                raise ValueError('Target data link escapes the pinned distribution')
            ancestors = {p.resolve() for p in (parent, *parent.parents) if p.is_relative_to(source)}
            for name in children:
                path = parent / name
                if not path.resolve().is_relative_to(source) or path.resolve() in ancestors:
                    raise ValueError('External or cyclic target data directory link')
            for name in files:
                path = parent / name
                if not path.resolve().is_relative_to(source) or not path.is_file():
                    raise ValueError('External or non-file target data entry')
                original[path.relative_to(source).as_posix()] = sha(path)
    licenses = [p for p in source.iterdir() if p.is_file()
                and p.name.lower().startswith(('license', 'copying', 'notice', 'readme'))]
    for path in licenses:
        original[path.name] = sha(path)
    destination.mkdir(parents=True)
    for directory in directories:
        shutil.copytree(directory, destination / directory.relative_to(source))
    for path in licenses:
        shutil.copy2(path, destination / path.name)
    actual = {p.relative_to(destination).as_posix(): sha(p)
              for p in destination.rglob('*') if p.is_file()}
    if actual != original:
        raise ValueError('Staged target headers/libraries differ from the original data')
    report = {'targetDirectories': [p.name for p in targets], 'files': actual,
              'allSelectedTargetBytesPreserved': True, 'rootHostCompilerDirectoryCopied': False,
              'targetApplicationBuildVerified': False}
    (destination / 'target-sysroot-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    stage(args.source, args.output)
