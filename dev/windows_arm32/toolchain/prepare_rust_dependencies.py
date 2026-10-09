#!/usr/bin/env python3
"""Prepare version-pinned ARM32 native SDK dependencies without application edits."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

from package_compiler import sha

CRATES = {
    'windows-sys-0.52.0': '282be5f36a8ce781fad8c8ae18fa3f9beff57ec1b52cb3de0789201425d9a33d',
    'windows-sys-0.59.0': '1e38bc4d79ed67fd075bcc251a1c39b32a1776bbe92e5bef1f0bf1f8c531853b',
    'windows-sys-0.61.2': 'ae137229bcbd6cdf0f7b80a31df61766145077ddf49416a728b02cb3921ff3fc',
    'ring-0.17.14': 'a4689e6c2294d81e88dc6261c768b63bc4fcdb852be6d1352498b114f61383b7',
}
PATCHES = {
    'rust-windows-api-types.patch': 'cb24a00d3143defbba66d329a920eeb14b6b3378b293263d9597f799fb925db8',
    'ring-windows-thumb2.patch': '0440c58137f7d6f4899a0d54b2835aef8e73880a620d119027b9a37d89daf34e',
}
GENERATORS = (
    'crypto/fipsmodule/aes/asm/bsaes-armv7.pl', 'crypto/fipsmodule/aes/asm/ghash-armv4.pl',
    'crypto/fipsmodule/aes/asm/vpaes-armv7.pl', 'crypto/fipsmodule/bn/asm/armv4-mont.pl',
    'crypto/chacha/asm/chacha-armv4.pl', 'crypto/fipsmodule/sha/asm/sha256-armv4.pl',
    'crypto/fipsmodule/sha/asm/sha512-armv4.pl',
)


def prepare(cache, output, names=None):
    cache, output = cache.resolve(), output.resolve()
    names = list(CRATES) if names is None else list(names)
    if not names or len(set(names)) != len(names) or any(name not in CRATES for name in names):
        raise ValueError('Select distinct supported native SDK crates')
    directory = Path(__file__).resolve().parent
    for name, digest in PATCHES.items():
        if sha(directory / name) != digest:
            raise ValueError('Native SDK source patch checksum mismatch: ' + name)
    for name in names:
        if sha(cache / (name + '.crate')) != CRATES[name]:
            raise ValueError('Native SDK crate checksum mismatch: ' + name)
    if output.exists():
        raise ValueError('Native SDK dependency output already exists')
    output.mkdir(parents=True)
    scratch = output / 'tmp'
    scratch.mkdir()
    env = dict(os.environ, TMPDIR=str(scratch), TMP=str(scratch), TEMP=str(scratch))
    records = {}
    for name in names:
        source = output / name
        with tarfile.open(cache / (name + '.crate')) as archive:
            for member in archive.getmembers():
                path = Path(member.name)
                if path.parts[0] != name or path.is_absolute() or '..' in path.parts:
                    raise ValueError('Unexpected native SDK crate member: ' + member.name)
            archive.extractall(output, filter='data')
        original = {str(p.relative_to(source)): sha(p) for p in source.rglob('*') if p.is_file()}
        if name.startswith('windows-sys-'):
            patch = directory / 'rust-windows-api-types.patch'
            arguments = ['--include=' + name + '/*']
            cwd = output
        else:
            patch = directory / 'ring-windows-thumb2.patch'
            arguments = []
            cwd = source
        subprocess.run(['git', 'apply', '--check', *arguments, str(patch)], cwd=cwd, env=env, check=True)
        subprocess.run(['git', 'apply', *arguments, str(patch)], cwd=cwd, env=env, check=True)
        if name.startswith('ring-'):
            for relative in GENERATORS:
                subprocess.run(['perl', str(source / relative), 'win32',
                                str(source / 'pregenerated' / (Path(relative).stem + '-win32.S'))],
                               env=env, check=True)
            if any(sha(source / path) != digest for path, digest in original.items()
                   if path.startswith('src/')):
                raise ValueError('Native SDK patch changed Rust crypto algorithm code')
        final = {str(p.relative_to(source)): sha(p) for p in source.rglob('*') if p.is_file()}
        if any(not p.resolve().is_relative_to(output) for p in source.rglob('*')):
            raise ValueError('External native SDK dependency link')
        records[name] = {'crateSha256': CRATES[name], 'patchSha256': sha(patch),
                         'files': final, 'changedFiles': sorted(
                             path for path in final if original.get(path) != final[path])}
    shutil.rmtree(scratch)
    config = output / '.cargo/sdk-patches.toml'
    config.parent.mkdir()
    lines = ['[patch.crates-io]']
    for name in names:
        package, version = name.rsplit('-', 1)
        alias = package + '-' + version.replace('.', '')
        # Cargo resolves file-relative paths from the config's grandparent.
        lines.append(json.dumps(alias) + ' = { package = ' + json.dumps(package) +
                     ', path = ' + json.dumps(name) + ' }')
    config.write_text('\n'.join(lines) + '\n')
    (output / 'dependency-provenance.json').write_text(json.dumps({
        'crates': records, 'applicationSourceChanges': 0,
        'originalRegistrySourcesModified': False, 'sharedByRtAndPhone': True,
        'systemLibrariesCopied': False, 'configuration': str(config.relative_to(output)),
    }, indent=2) + '\n')
    return config


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--crate-cache', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--crate', action='append', choices=tuple(CRATES))
    args = parser.parse_args()
    print(prepare(args.crate_cache, args.output, args.crate))
