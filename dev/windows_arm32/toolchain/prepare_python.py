#!/usr/bin/env python3
"""Prepare a fixed relocatable Python runtime and the Phone image dependency."""
import argparse
import copy
import json
import os
import platform
import re
from pathlib import Path
import shutil
import subprocess
import tarfile

from package_compiler import audit_native, sha

PYTHON_VERSION = '3.14.6'
ARCHIVE_SHA256 = '58ba7c2f7a5bad3031065abaad75f701a3e8b7f83679917c396a850876a48205'
ARCHIVE_URL = ('https://github.com/astral-sh/python-build-standalone/releases/download/20260623/'
               'cpython-3.14.6%2B20260623-aarch64-apple-darwin-pgo%2Blto-full.tar.zst')
WHEEL_SHA256 = 'e158cb00350dc278f3b91551101aa7d12415a66ebf2c91d8d5ac14e56ddd3ad0'
WHEEL_URL = ('https://files.pythonhosted.org/packages/c7/da/'
             '32c752228ae345f489e3a42499d817b6c3996da7e8a3bc7a04fc806b243b/'
             'pillow-12.3.0-cp314-cp314-macosx_11_0_arm64.whl')
PYTHON_HOST = 'aarch64-apple-darwin'
if platform.system() == 'Linux':
    PYTHON_HOST = 'x86_64-unknown-linux-gnu'
    ARCHIVE_SHA256 = '59a0e2d202f1d36cd8233cf1c426b469048025cf4eb26b972aea334c302cd999'
    ARCHIVE_URL = ('https://github.com/astral-sh/python-build-standalone/releases/download/20260623/'
                   'cpython-3.14.6%2B20260623-x86_64-unknown-linux-gnu-pgo%2Blto-full.tar.zst')
    WHEEL_SHA256 = '251bf95b67017e27b13d82f5b326234ca62d70f9cf4c2b9032de2358a3b12c7b'
    WHEEL_URL = ('https://files.pythonhosted.org/packages/5c/44/'
                 'c85361f65dbe00eea8576ee467c768d25129989efb76e94f205e9ca9bb46/'
                 'pillow-12.3.0-cp314-cp314-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl')


def prepare(archive, wheel, output):
    archive, wheel, output = (p.resolve() for p in (archive, wheel, output))
    if sha(archive) != ARCHIVE_SHA256 or sha(wheel) != WHEEL_SHA256:
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
    if metadata['python_version'] != PYTHON_VERSION or metadata['target_triple'] != PYTHON_HOST:
        raise ValueError('Unexpected Python distribution identity')
    if not (output / metadata['license_path']).is_file():
        raise ValueError('Missing upstream Python license')
    python = output / 'bin/python3.14'
    subprocess.run([str(python), '-I', '-B', '-m', 'pip', '--isolated', '--no-cache-dir',
                    '--disable-pip-version-check', 'install', '--no-index', '--no-deps',
                    str(wheel)], env=env, check=True)
    # Local installer paths are build inputs, not portable package origins.
    for origin in (output / 'lib/python3.14/site-packages').glob('*.dist-info/direct_url.json'):
        record = json.loads(origin.read_text())
        if record.get('url', '').startswith('file:'):
            if origin.parent.name.startswith('pillow-'):
                record['url'] = WHEEL_URL
                origin.write_text(json.dumps(record) + '\n')
            else:
                origin.unlink()
    for path in output.rglob('*'):
        if path.is_symlink() and not path.resolve().is_relative_to(output):
            raise ValueError('External Python runtime link: ' + str(path))
    magic = {b'\x7fELF'} | {bytes.fromhex(value) for value in ('cffaedfe', 'feedfacf', 'cafebabe', 'bebafeca')}
    native = []
    for path in output.rglob('*'):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as stream:
                if stream.read(4) in magic:
                    native.append(path)
    libraries = {p.name for p in native}
    relocated = []
    if platform.system() == 'Linux':
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
    dependencies = {str(p.relative_to(output)): audit_native(
        p, libraries, 'x86_64' if PYTHON_HOST == 'x86_64-unknown-linux-gnu' else 'arm64',
        dylib=p.suffix == '.dylib', root=output) for p in native}
    shutil.rmtree(scratch)
    report = {'pythonVersion': PYTHON_VERSION, 'archiveSha256': ARCHIVE_SHA256,
              'pillowWheelSha256': WHEEL_SHA256, 'nativeDependencies': dependencies,
              'systemLibrariesCopied': False,
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
    args = parser.parse_args()
    print(prepare(args.archive, args.pillow_wheel, args.output))
