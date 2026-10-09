#!/usr/bin/env python3
"""Prepare a fixed relocatable Python runtime and the Phone image dependency."""
import argparse
import copy
from email.parser import Parser
import json
import os
import sys
import re
from pathlib import Path
import shutil
import subprocess
import tarfile

from package_compiler import audit_native, sha

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sdk_hosts import current_host, select_host, verify_native_host


def inputs(host=None):
    identity = (host or current_host())['id']
    return json.loads(Path(__file__).with_name('python-hosts.json').read_text())[identity]


def verify_cross_python(root, host, pillow_version):
    """Validate pinned destination files without executing foreign extensions."""
    root = root.resolve()

    def contained(name):
        path = (root / name).resolve()
        if not path.is_relative_to(root) or not path.is_file():
            raise ValueError("Missing or external Python distribution file: " + name)
        return path

    pinned = inputs(host)
    record = json.loads((root / 'python-provenance.json').read_text())
    if (record.get('host', host['id']) != host['id'] or
            record.get('pythonVersion') != pinned['pythonVersion'] or
            record.get('archiveSha256') != pinned['archiveSha256'] or
            record.get('pillowWheelSha256') != pinned['wheelSha256']):
        raise ValueError('Cross-packaged Python differs from pinned host inputs')
    for name, expected in record['files'].items():
        if sha(contained(name)) != expected:
            raise ValueError('Cross-packaged Python file changed: ' + name)
    version = '.'.join(pinned['pythonVersion'].split('.')[:2])
    verify_native_host(contained('bin/python' + version), host)
    metadata = list((root / ('lib/python' + version + '/site-packages')).glob('pillow-*.dist-info/METADATA'))
    if len(metadata) != 1 or str(metadata[0].relative_to(root)) not in record['files']:
        raise ValueError('Missing audited Pillow distribution metadata')
    package = Parser().parsestr(metadata[0].read_text())
    names = package.get_all('Name', [])
    if (len(names) != 1 or names[0].lower() != 'pillow' or
            package.get_all('Version') != [pillow_version]):
        raise ValueError('Cross-packaged Pillow version differs from Phone requirements')


# Preserve the native CI download contract.
_native_inputs = inputs()
PYTHON_VERSION = _native_inputs['pythonVersion']
PYTHON_HOST = _native_inputs['pythonHost']
ARCHIVE_SHA256 = _native_inputs['archiveSha256']
ARCHIVE_URL = _native_inputs['archiveUrl']
WHEEL_SHA256 = _native_inputs['wheelSha256']
WHEEL_URL = _native_inputs['wheelUrl']


def prepare(archive, wheel, output, host=None, installer_python=None, llvm_readobj=None, llvm_objdump=None):
    host = select_host(host) if host else current_host()
    record = inputs(host)
    archive, wheel, output = (p.resolve() for p in (archive, wheel, output))
    if sha(archive) != record['archiveSha256'] or sha(wheel) != record['wheelSha256']:
        raise ValueError('Python or Pillow input checksum mismatch')
    if output.exists():
        raise ValueError('Python distribution output already exists')
    output.mkdir(parents=True)
    scratch = output / 'preparation-tmp'
    scratch.mkdir()
    env = dict(os.environ, TMPDIR=str(scratch), TMP=str(scratch), TEMP=str(scratch),
               PYTHONDONTWRITEBYTECODE='1')
    process = subprocess.Popen(['zstd', '-d', '-c', str(archive)], stdout=subprocess.PIPE)
    try:
        with tarfile.open(fileobj=process.stdout, mode='r|') as source:
            for member in source:
                if member.name.startswith('python/install/'):
                    selected = copy.copy(member)
                    selected.name = member.name.removeprefix('python/install/')
                    if selected.islnk():
                        selected.linkname = selected.linkname.removeprefix('python/install/')
                elif member.name.startswith('python/licenses/'):
                    selected = copy.copy(member)
                    selected.name = member.name.removeprefix('python/')
                elif member.name == 'python/PYTHON.json':
                    selected = copy.copy(member)
                    selected.name = 'upstream-python.json'
                else:
                    continue
                source.extract(selected, output, filter='data')
        if process.wait() != 0:
            raise ValueError('Python archive decompression failed')
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait()
        process.stdout.close()
    metadata = json.loads((output / 'upstream-python.json').read_text())
    if metadata['python_version'] != record['pythonVersion'] or metadata['target_triple'] != record['pythonHost']:
        raise ValueError('Unexpected Python distribution identity')
    if not (output / metadata['license_path']).is_file():
        raise ValueError('Missing upstream Python license')
    windows = host['os'] == 'windows'
    python = output / ('python.exe' if windows else 'bin/python3.14')
    verify_native_host(python, host)
    site = output / ('Lib/site-packages' if windows else 'lib/python3.14/site-packages')
    pip = [str(installer_python or (python if host == current_host() else sys.executable)),
           '-I', '-B', '-m', 'pip', '--isolated', '--no-cache-dir',
           '--disable-pip-version-check', 'install', '--no-index', '--no-deps', '--no-compile']
    if installer_python or host != current_host():
        # pip's supported cross-platform wheel installer never executes target
        # Python or its extension modules on the packaging machine.
        pip += ['--target', str(site), '--platform', record['wheelPlatform'],
                '--python-version', '3.14', '--implementation', 'cp',
                '--abi', 'cp314', '--only-binary', ':all:']
    subprocess.run([*pip, str(wheel)], env=env, check=True)
    # Local installer paths are build inputs, not portable package origins.
    for origin in site.glob('*.dist-info/direct_url.json'):
        origin_record = json.loads(origin.read_text())
        if origin_record.get('url', '').startswith('file:'):
            if origin.parent.name.startswith('pillow-'):
                origin_record['url'] = record['wheelUrl']
                origin.write_text(json.dumps(origin_record) + '\n')
            else:
                origin.unlink()
    for path in output.rglob('*'):
        if path.is_symlink() and not path.resolve().is_relative_to(output):
            raise ValueError('External Python runtime link: ' + str(path))
    magic = {b'\x7fELF'} | {bytes.fromhex(value) for value in ('cffaedfe', 'feedfacf', 'cafebabe', 'bebafeca')}
    native = []
    installer_templates = {}
    for path in output.rglob('*'):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as stream:
                header = stream.read(4)
                if header in magic or header[:2] == b'MZ':
                    # pip ships PE launcher templates for generating Windows
                    # wheel entry points, even in Unix distributions. Preserve
                    # these data files, separately from native host programs.
                    if (path.parent == site / 'pip/_vendor/distlib' and
                            path.name in ('t32.exe', 't64.exe', 't64-arm.exe',
                                          'w32.exe', 'w64.exe', 'w64-arm.exe')):
                        installer_templates[str(path.relative_to(output))] = sha(path)
                    else:
                        native.append(path)
    libraries = {p.name for p in native}
    relocated = []
    if host['os'] == 'linux':
        for path in native:
            dynamic = subprocess.check_output(['readelf', '-d', str(path)], text=True)
            entries = re.findall(r'\((?:RPATH|RUNPATH)\).*\[([^\]]+)\]', dynamic)
            parts = [part for entry in entries for part in entry.split(':')]
            if not any(part.startswith('/') for part in parts):
                continue
            # Standalone Python can retain build-only /tools/deps/lib entries.
            # Replace those only in our staged copy; preserve wheel-relative
            # paths and resolve every private imported library within payload.
            relative = [part for part in parts if not part.startswith('/')]
            needed = re.findall(r'\(NEEDED\).*\[([^\]]+)\]', dynamic)
            for library in native:
                if library.name in needed:
                    relative.append('$ORIGIN/' + os.path.relpath(library.parent, path.parent))
            relative.append('$ORIGIN/' + os.path.relpath(output / 'lib', path.parent))
            runpath = ':'.join(dict.fromkeys(relative))
            subprocess.run(['patchelf', '--set-rpath', runpath, str(path)], env=env, check=True)
            relocated.append(str(path.relative_to(output)))
    dependencies = {}
    for path in native:
        verify_native_host(path, host)
        if windows:
            from windows_native_dependencies import audit
            imports = audit(path, libraries, llvm_readobj)
        elif host['os'] == 'macos' and llvm_objdump:
            from macho_native_dependencies import audit
            imports = audit(path, libraries, host, llvm_objdump)
        else:
            imports = audit_native(path, libraries, 'x86_64' if host['cpu'] == 'x64' else 'arm64',
                                   dylib=path.suffix == '.dylib', root=output)
        dependencies[str(path.relative_to(output))] = imports
    shutil.rmtree(scratch)
    report = {'host': host['id'], 'pythonVersion': record['pythonVersion'],
              'archiveSha256': record['archiveSha256'],
              'pillowWheelSha256': record['wheelSha256'], 'nativeDependencies': dependencies,
              'systemLibrariesCopied': False,
              'preservedWheelInstallerTemplates': installer_templates,
              'relocatedNativeSearchPaths': relocated,
              'files': {str(p.relative_to(output)): sha(p)
                        for p in output.rglob('*') if p.is_file() and not p.is_symlink()}}
    (output / 'python-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--pillow-wheel', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--host', help='Explicit host for cross packaging')
    parser.add_argument('--installer-python', type=Path, help='Native Python with pip for cross packaging')
    parser.add_argument('--llvm-readobj', type=Path, help='Native LLVM reader for Windows import auditing')
    parser.add_argument('--llvm-objdump', type=Path, help='Native LLVM reader for cross-packaged Mach-O imports')
    args = parser.parse_args()
    print(prepare(args.archive, args.pillow_wheel, args.output, args.host,
                  args.installer_python, args.llvm_readobj, args.llvm_objdump))
