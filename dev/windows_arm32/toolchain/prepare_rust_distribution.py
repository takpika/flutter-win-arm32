#!/usr/bin/env python3
"""Copy complete Rust build components from a private or read-only installation."""
import argparse
import json
import platform
from pathlib import Path
import shutil
import subprocess

from package_compiler import audit_native, sha


def prepare(rustc, output, version, verify_existing=False):
    rustc, output = rustc.resolve(), output.resolve()
    source = rustc.parent.parent
    info = subprocess.check_output([str(rustc), '-vV'], text=True)
    fields = dict(line.split(': ', 1) for line in info.splitlines() if ': ' in line)
    expected_host = ('x86_64-unknown-linux-gnu' if platform.system() == 'Linux'
                     else 'aarch64-apple-darwin')
    if fields['release'] != version or fields['host'] != expected_host:
        raise ValueError('Unexpected Rust compiler version or host')
    if output.exists() and not verify_existing:
        raise ValueError('Rust distribution output already exists')
    host = fields['host']
    components = ('rustc-' + host, 'cargo-' + host, 'rust-std-' + host, 'rust-src')
    files = set()
    manifests = {}
    for component in components:
        manifest = source / 'lib/rustlib' / ('manifest-' + component)
        manifests[component] = sha(manifest)
        for line in manifest.read_text().splitlines():
            kind, name = line.split(':', 1)
            relative = Path(name)
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('Unsafe Rust component path: ' + name)
            selected = source / relative
            if not selected.resolve().is_relative_to(source):
                raise ValueError('External Rust component dependency: ' + name)
            if kind == 'file' and selected.is_file():
                files.add(relative)
            elif kind == 'dir' and selected.is_dir():
                for path in selected.rglob('*'):
                    if path.is_file():
                        if not path.resolve().is_relative_to(source):
                            raise ValueError('External Rust component dependency: ' + str(path))
                        files.add(path.relative_to(source))
            else:
                raise ValueError('Missing or unsupported Rust component entry: ' + line)
    for name in ('bin/cargo', 'bin/rustc', 'bin/rustdoc',
                 'lib/rustlib/src/rust/library/Cargo.toml'):
        if Path(name) not in files:
            raise ValueError('Missing required Rust component file: ' + name)
    original = {str(path): sha(source / path) for path in sorted(files)}
    if verify_existing:
        provenance = output / 'rust-distribution-provenance.json'
        expected = set(original)
        if provenance.is_file():
            previous = json.loads(provenance.read_text())
            if (previous.get('rustVersion') != version or previous.get('host') != host or
                    previous.get('componentManifests') != manifests or previous.get('files') != original):
                raise ValueError('Existing Rust distribution provenance differs')
            expected.add(provenance.name)
        actual = {str(path.relative_to(output)) for path in output.rglob('*') if path.is_file()}
        if (not output.is_dir() or output.is_symlink() or
                any(path.is_symlink() for path in output.rglob('*')) or
                actual != expected or
                any(sha(output / name) != digest for name, digest in original.items())):
            raise ValueError('Existing Rust payload differs from complete upstream components')
    else:
        output.mkdir(parents=True)
        for relative in sorted(files):
            destination = output / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            # Materialize component links; do not retain links into shared installs.
            shutil.copy2(source / relative, destination)
            if sha(destination) != original[str(relative)]:
                raise ValueError('Rust component changed while copying: ' + str(relative))
    magic = {b'\x7fELF'} | {bytes.fromhex(value) for value in ('cffaedfe', 'feedfacf', 'cafebabe', 'bebafeca')}
    native = []
    for relative in files:
        with (output / relative).open('rb') as stream:
            if stream.read(4) in magic:
                native.append(relative)
    libraries = {path.name for path in native}
    architecture = 'x86_64' if expected_host == 'x86_64-unknown-linux-gnu' else 'arm64'
    dependencies = {str(path): audit_native(output / path, libraries, architecture,
                                            dylib=path.suffix == '.dylib', root=output) for path in native}
    if any(sha(source / Path(name)) != digest for name, digest in original.items()):
        raise ValueError('Installed Rust component changed during preparation')
    copied_info = subprocess.check_output([str(output / 'bin/rustc'), '-vV'], text=True)
    if copied_info != info:
        raise ValueError('Copied Rust compiler identity differs')
    report = {'rustVersion': version, 'host': host, 'compilerIdentity': info,
              'componentManifests': manifests, 'files': original,
              'nativeDependencies': dependencies, 'systemLibrariesCopied': False,
              'installedSourceModified': False}
    (output / 'rust-distribution-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rustc', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--verify-existing', action='store_true',
                        help='Audit an existing complete copy without replacing payload files')
    args = parser.parse_args()
    print(prepare(args.rustc, args.output, args.version, args.verify_existing))
