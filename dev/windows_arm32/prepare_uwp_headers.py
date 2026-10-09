#!/usr/bin/env python3
"""Install pinned ANGLE/RapidJSON development headers into a standalone SDK."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from prepare_dependency import prepare_dependency


def prepare(sdk, work):
    sdk, work = sdk.resolve(), work.resolve()
    deps = (sdk / 'DEPS').read_text()
    host = re.search(r"'flutter_git':\s*'([^']+)'", deps).group(1)
    records = {}
    for name, license_name in (('angle', 'LICENSE'), ('rapidjson', 'license.txt')):
        entry = re.search(r"'engine/src/flutter/third_party/" + name +
                          r"':\s*Var\('flutter_git'\)\s*\+\s*'([^']+)'\s*\+\s*'@'\s*\+\s*'([a-f0-9]{40})'", deps)
        if not entry:
            raise ValueError('Missing pinned header dependency: ' + name)
        repository, revision = host + entry.group(1), entry.group(2)
        destination = sdk / 'engine/src/flutter/third_party' / name
        if destination.exists():
            raise ValueError('Existing SDK dependency is preserved: ' + str(destination))
        checkout = work / name
        if checkout.exists():
            raise ValueError('Use a new header source directory: ' + str(checkout))
        checkout.mkdir(parents=True)
        paths = ['/include/', '/' + license_name]
        if name == 'angle':
            manifest = json.loads(Path(__file__).with_name('patches').joinpath('angle-source-manifest.json').read_text())
            if revision != manifest['upstreamRevision']:
                raise ValueError('ANGLE headers must match the runtime source revision')
            paths += ['/' + item['path'] for item in manifest['files']]
            paths += ['/' + item['path'] for item in manifest.get('restoredFiles', [])]
        subprocess.run(['git', 'init', str(checkout)], check=True, stdout=sys.stderr)
        subprocess.run(['git', '-C', str(checkout), 'remote', 'add', 'origin', repository], check=True, stdout=sys.stderr)
        subprocess.run(['git', '-C', str(checkout), 'sparse-checkout', 'set', '--no-cone', '--stdin'],
                       input='\n'.join(paths) + '\n', text=True, check=True, stdout=sys.stderr)
        fetch = ['git', '-C', str(checkout), 'fetch', '--depth', '1', '--filter=blob:none',
                 'origin', revision]
        fetched_repository = repository
        try:
            subprocess.run(fetch, check=True, stdout=sys.stderr)
        except subprocess.CalledProcessError:
            if name != 'rapidjson':
                raise
            fetched_repository = 'https://fuchsia.googlesource.com/third_party/rapidjson'
            subprocess.run(['git', '-C', str(checkout), 'remote', 'set-url', 'origin', fetched_repository],
                           check=True, stdout=sys.stderr)
            subprocess.run(fetch, check=True, stdout=sys.stderr)
        subprocess.run(['git', '-C', str(checkout), 'checkout', '--detach', 'FETCH_HEAD'], check=True, stdout=sys.stderr)
        actual = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != revision:
            raise ValueError('Header source revision differs from the pinned dependency')
        if name == 'angle':
            prepare_dependency(checkout, 'angle')
        destination.mkdir(parents=True)
        shutil.copytree(checkout / 'include', destination / 'include')
        shutil.copy2(checkout / license_name, destination)
        if name == 'angle':
            header = Path('src/libANGLE/renderer/d3d/d3d11/winrt/PhoneDisplayInformation.h')
            (destination / header).parent.mkdir(parents=True)
            shutil.copy2(checkout / header, destination / header)
        files = {str(path.relative_to(destination)): hashlib.sha256(path.read_bytes()).hexdigest()
                 for path in destination.rglob('*') if path.is_file()}
        record = {'repository': repository, 'fetchedRepository': fetched_repository,
                  'revision': revision, 'files': files,
                  'developmentHeadersOnly': True, 'systemLibrariesCopied': False}
        (destination / 'sdk-header-provenance.json').write_text(json.dumps(record, indent=2) + '\n')
        records[name] = record
    return records


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.sdk, args.work), indent=2))
