#!/usr/bin/env python3
"""Copy complete Rust build components from a private or read-only installation."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

from package_compiler import audit_native, sha
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from sdk_hosts import current_host, select_host, verify_native_host


def prepare(rustc, output, version, verify_existing=False, rustup=None,
            *, host=None, runner=(), llvm_readobj=None, llvm_objdump=None,
            reference_rustc=None):
    rustc, output = rustc.resolve(), output.resolve()
    source = rustc.parent.parent
    selected_host = select_host(host) if host else current_host()
    verify_native_host(rustc, selected_host)
    executable_suffix = '.exe' if selected_host['os'] == 'windows' else ''
    expected_host = selected_host['rustHost']
    if reference_rustc is not None:
        if runner or llvm_objdump is None:
            raise ValueError('Cross preparation needs a native LLVM Mach-O reader and no destination runner')
        from rust_cross_identity import inspect, CHANNEL_PATH
        identity = inspect(rustc, version, selected_host, reference_rustc)
    else:
        info = subprocess.check_output([*runner, str(rustc), '-vV'], text=True)
        fields = dict(line.split(': ', 1) for line in info.splitlines() if ': ' in line)
        if fields['release'] != version or fields['host'] != expected_host:
            raise ValueError('Unexpected Rust compiler version or host')
        identity = {'compilerIdentity': info, 'compilerExecutionVerified': True}
    if output.exists() and not verify_existing:
        raise ValueError('Rust distribution output already exists')
    host = expected_host
    components = ('rustc-' + host, 'cargo-' + host, 'rust-std-' + host, 'rust-src')
    if host.endswith('windows-gnu'):
        mingw = [name for name in ('rust-mingw-' + host, 'rust-mingw')
                 if (source / 'lib/rustlib' / ('manifest-' + name)).is_file()]
        if len(mingw) != 1:
            raise ValueError('Expected one complete official Rust MinGW component')
        components += tuple(mingw)
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
    for name in (*(f'bin/{tool}{executable_suffix}' for tool in ('cargo', 'rustc', 'rustdoc')),
                 'lib/rustlib/src/rust/library/Cargo.toml'):
        if Path(name) not in files:
            raise ValueError('Missing required Rust component file: ' + name)
    inputs = {path: source / path for path in files}
    if reference_rustc is not None:
        inputs[CHANNEL_PATH] = source / CHANNEL_PATH
        files = set(inputs)
    if rustup is not None:
        rustup = rustup.resolve()
        matrix = Path(__file__).with_name('rustup-hosts.json')
        rustup_record = json.loads(matrix.read_text())[selected_host['id']]
        if sha(rustup) != rustup_record['sha256']:
            raise ValueError('Unexpected official Rustup 1.28.2 executable')
        verify_native_host(rustup, selected_host)
        inputs[Path('bin/rustup' + executable_suffix)] = rustup
        for name in ('LICENSE-MIT', 'LICENSE-APACHE', 'SOURCE.txt'):
            source_name = ('SOURCE-common.txt' if name == 'SOURCE.txt' and
                           selected_host['id'] != 'linux-x64' else name)
            inputs[Path('share/doc/rustup') / name] = Path(__file__).parent / 'licenses/rustup' / source_name
        if selected_host['id'] != 'linux-x64':
            inputs[Path('share/doc/rustup/host-inputs.json')] = matrix
        files = set(inputs)
    original = {str(path): sha(inputs[path]) for path in sorted(files)}
    if (reference_rustc is not None and
            original[str(CHANNEL_PATH)] != identity['channelManifestSha256']):
        raise ValueError('Rust channel changed after reference inspection')
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
            shutil.copy2(inputs[relative], destination)
            if sha(destination) != original[str(relative)]:
                raise ValueError('Rust component changed while copying: ' + str(relative))
    magic = {b'\x7fELF'} | {bytes.fromhex(value) for value in ('cffaedfe', 'feedfacf', 'cafebabe', 'bebafeca')}
    native = []
    for relative in files:
        with (output / relative).open('rb') as stream:
            header = stream.read(4)
            if header in magic or header[:2] == b'MZ':
                native.append(relative)
    libraries = {path.name for path in native}
    architecture = 'x86_64' if selected_host['cpu'] == 'x64' else 'arm64'
    dependencies = {}
    for path in native:
        verify_native_host(output / path, selected_host)
        if selected_host['os'] == 'windows':
            from windows_native_dependencies import audit
            imports = audit(output / path, libraries, llvm_readobj)
        elif selected_host['os'] == 'macos' and llvm_objdump is not None:
            from macho_native_dependencies import audit
            imports = audit(output / path, libraries, selected_host, llvm_objdump)
        else:
            imports = audit_native(output / path, libraries, architecture,
                                   dylib=path.suffix == '.dylib', root=output)
        dependencies[str(path)] = imports
    if any(sha(inputs[Path(name)]) != digest for name, digest in original.items()):
        raise ValueError('Installed Rust component changed during preparation')
    if reference_rustc is None:
        copied_info = subprocess.check_output([*runner, str(output / ('bin/rustc' + executable_suffix)), '-vV'], text=True)
        if copied_info != info:
            raise ValueError('Copied Rust compiler identity differs')
    report = {'rustVersion': version, 'host': host, **identity,
              'componentManifests': manifests, 'files': original,
              'nativeDependencies': dependencies, 'systemLibrariesCopied': False,
              'installedSourceModified': False,
              'executionRunner': list(runner)}
    (output / 'rust-distribution-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rustc', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--verify-existing', action='store_true',
                        help='Audit an existing complete copy without replacing payload files')
    parser.add_argument('--rustup', type=Path, help='Include the pinned official Rustup executable for the selected SDK host')
    parser.add_argument('--host', help='Explicit SDK host for execution under a runner')
    parser.add_argument('--runner', action='append', default=[], help='Execution runner argument, e.g. wine')
    parser.add_argument('--llvm-readobj', type=Path, help='Native LLVM reader for Windows imports')
    parser.add_argument('--llvm-objdump', type=Path, help='Native LLVM reader for cross-packaged Mach-O imports')
    parser.add_argument('--reference-rustc', type=Path,
                        help='Runnable compiler from the identical Rustup channel; destination execution is deferred')
    args = parser.parse_args()
    print(prepare(args.rustc, args.output, args.version, args.verify_existing, args.rustup,
                  host=args.host, runner=args.runner, llvm_readobj=args.llvm_readobj,
                  llvm_objdump=args.llvm_objdump, reference_rustc=args.reference_rustc))
