#!/usr/bin/env python3
"""Prepare an SDK-owned std source tree without modifying installed Rust."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(rustc, output, copy_host_stdlib=False):
    rustc, output = rustc.resolve(), output.resolve()
    info = subprocess.check_output([str(rustc), '-vV'], text=True)
    release = next(line[9:] for line in info.splitlines() if line.startswith('release: '))
    host = next(line[6:] for line in info.splitlines() if line.startswith('host: '))
    installed = rustc.parent.parent / 'lib/rustlib'
    source = installed / 'src/rust/library'
    if not (source / 'Cargo.toml').is_file():
        raise ValueError('The selected Rust compiler needs its matching rust-src component')
    destination = output / 'lib/rustlib/src/rust/library'
    if not destination.exists():
        shutil.copytree(source, destination)
    directory = Path(__file__).resolve().parent
    manifest = json.loads((directory / 'rust-source-manifest.json').read_text())
    entry = manifest['versions'].get(release)
    permitted = {item['path']: item for item in entry['files']} if entry else {}
    originals = {}
    for path in source.rglob('*'):
        if not path.is_file():
            continue
        relative = path.relative_to(source).as_posix()
        expected = sha(path)
        originals[relative] = expected
        copied = destination / relative
        allowed = {expected}
        if relative in permitted:
            item = permitted[relative]
            if expected != item['originalSha256']:
                raise ValueError('Installed std source differs from the pinned patch base')
            allowed.add(item['patchedSha256'])
        if not copied.is_file() or sha(copied) not in allowed:
            raise ValueError('Refusing to replace modified SDK std source: ' + relative)
    if entry:
        patch = directory / entry['patch']
        if sha(patch) != entry['patchSha256']:
            raise ValueError('Rust source patch checksum mismatch')
        if not all(sha(destination / item['path']) == item['patchedSha256'] for item in entry['files']):
            # The copied std tree can be inside a Flutter Git checkout. Apply
            # against this tree, without interpreting paths in the parent repo.
            environment = dict(os.environ, GIT_CEILING_DIRECTORIES=str(destination.parent))
            subprocess.run(['git', 'apply', '--check', str(patch)], cwd=destination,
                           env=environment, check=True)
            subprocess.run(['git', 'apply', str(patch)], cwd=destination,
                           env=environment, check=True)
        if not all(sha(destination / item['path']) == item['patchedSha256'] for item in entry['files']):
            raise ValueError('Patched Rust source checksum mismatch')
    native = output / 'lib/rustlib' / host
    native_source = installed / host
    if copy_host_stdlib:
        # Distribution copies must not pull in files outside this Rust package.
        source_paths = list(native_source.rglob('*'))
        source_files = {path.relative_to(native_source): path
                        for path in source_paths if path.is_file()}
        if (not source_files or
                any(not path.resolve().is_relative_to(rustc.parent.parent) or
                    (path.is_symlink() and path.is_dir()) for path in source_paths)):
            raise ValueError('Missing or external Rust host library dependency')
        if native.is_symlink() and native.resolve() != native_source.resolve():
            raise ValueError('Refusing to replace the SDK host standard library')
        if native.exists() and not native.is_symlink():
            copied_files = {path.relative_to(native): path
                            for path in native.rglob('*') if path.is_file()}
            if (any(path.is_symlink() for path in native.rglob('*')) or
                    copied_files.keys() != source_files.keys() or
                    any(path.is_symlink() or sha(path) != sha(source_files[name])
                        for name, path in copied_files.items())):
                raise ValueError('Refusing to replace modified SDK host libraries')
        else:
            # Preserve an existing development symlink until copying succeeds.
            with tempfile.TemporaryDirectory(prefix='rust-host-', dir=native.parent) as scratch:
                staged = Path(scratch) / host
                shutil.copytree(native_source, staged)
                if native.is_symlink():
                    if native.resolve() != native_source.resolve():
                        raise ValueError('SDK host library link changed during preparation')
                    native.unlink()
                elif native.exists():
                    raise ValueError('SDK host library destination changed during preparation')
                staged.rename(native)
    elif native.is_symlink() and native.resolve() == native_source.resolve():
        pass
    elif native.exists() or native.is_symlink():
        raise ValueError('Refusing to replace the SDK host standard library')
    else:
        native.symlink_to(os.path.relpath(installed / host, native.parent), target_is_directory=True)
    (output / 'source-provenance.json').write_text(json.dumps({
        'rustVersion': release, 'host': host, 'installedCompiler': os.path.relpath(rustc, output),
        'installedCompilerPathRelativeToOutput': True,
        'originalSourceHashes': originals, 'sdkSourcePatch': entry,
        'installedSourceModified': False,
        'hostStandardLibraryCopied': copy_host_stdlib,
    }, indent=2) + '\n')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rustc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--copy-host-stdlib', action='store_true',
                        help='Copy host libraries into the SDK for relocation instead of linking installed Rust')
    args = parser.parse_args()
    print(prepare(args.rustc, args.output, args.copy_host_stdlib))
