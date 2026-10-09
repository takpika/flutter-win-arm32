#!/usr/bin/env python3
"""Stage port sources with the exact official SDK cache for a destination host.

This prepares build inputs, not a completed ARM32 SDK distribution.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import tarfile
import tempfile
import zipfile

from sdk_hosts import select_host, verify_native_host


def sha(path):
    value = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def clone_sources(repository, destination, revision, framework, version):
    tag = 'refs/tags/' + version
    tagged = subprocess.check_output(
        ['git', '-C', str(repository), 'rev-parse', tag + '^{commit}'], text=True).strip()
    if tagged != framework:
        raise ValueError('Official version tag differs from the pinned framework revision')
    # Flutter's version fallback selects the newest tag in the repository,
    # including unrelated future releases. Stage only this SDK's official tag.
    subprocess.run(['git', 'clone', '--no-tags', '--no-hardlinks', '--no-checkout',
                    str(repository), str(destination)], check=True)
    subprocess.run(['git', '-C', str(destination), 'checkout', '--detach', revision], check=True)
    subprocess.run(['git', '-C', str(destination), 'fetch', '--no-tags',
                    str(repository), tag + ':' + tag], check=True)
    subprocess.run(['git', '-C', str(destination), 'remote', 'set-url', 'origin',
                    'https://github.com/takpika/flutter-win-arm32.git'], check=True)


def bootstrap(repository, archive, destination, identity):
    repository, archive, destination = repository.resolve(), archive.resolve(), destination.resolve()
    host = select_host(identity)
    if destination.exists():
        raise ValueError('SDK staging destination already exists')
    if sha(archive) != host['officialSha256']:
        raise ValueError('Official SDK archive checksum differs for ' + identity)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repository, text=True).strip()
    manifest = json.loads(Path(__file__).with_name('sdk-hosts.json').read_text())
    framework = manifest['frameworkRevision']
    subprocess.run(['git', 'merge-base', '--is-ancestor', framework, revision], cwd=repository, check=True)
    count = subprocess.check_output(['git', 'rev-list', '--count', framework + '..' + revision],
                                    cwd=repository, text=True).strip()
    if count != '1':
        raise ValueError('Stage the single port commit above the pinned official Flutter source')
    destination.parent.mkdir(parents=True, exist_ok=True)
    # The released SDK must keep its own objects after relocation. A shared
    # Git clone would leave alternates pointing into the CI source checkout.
    clone_sources(repository, destination, revision, framework, manifest['flutterVersion'])

    def selected(name):
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name:
            raise ValueError('Unsafe official SDK archive member')
        return name.startswith(('flutter/bin/cache/', 'flutter/bin/mingit/'))

    # Keep temporary extraction on the same filesystem as the staged SDK.
    with tempfile.TemporaryDirectory(prefix='official-sdk-', dir=destination.parent) as temporary:
        scratch = Path(temporary)
        if zipfile.is_zipfile(archive):
            with zipfile.ZipFile(archive) as source:
                for member in source.infolist():
                    if not selected(member.filename):
                        continue
                    path = scratch / member.filename
                    mode = member.external_attr >> 16
                    if member.is_dir():
                        path.mkdir(parents=True, exist_ok=True)
                        continue
                    path.parent.mkdir(parents=True, exist_ok=True)
                    if stat.S_ISLNK(mode):
                        target = source.read(member).decode('utf-8')
                        if PurePosixPath(target).is_absolute() or '\\' in target or not (path.parent / target).resolve().is_relative_to(scratch / 'flutter'):
                            raise ValueError('Official SDK link escapes its root')
                        path.symlink_to(target)
                    else:
                        with source.open(member) as value, path.open('wb') as output:
                            shutil.copyfileobj(value, output)
                        if mode & 0o777:
                            path.chmod(mode & 0o777)
        else:
            with tarfile.open(archive) as source:
                source.extractall(scratch, members=(member for member in source
                                  if selected(member.name)), filter='data')
        for name in ('cache', 'mingit'):
            source = scratch / 'flutter/bin' / name
            if source.is_dir():
                shutil.copytree(source, destination / 'bin' / name, symlinks=True)
    for directory in ('cache', 'mingit'):
        for path in (destination / 'bin' / directory).rglob('*'):
            if path.is_symlink() and not path.resolve().is_relative_to(destination):
                raise ValueError('Official SDK cache has an external link')
    extension = '.exe' if host['os'] == 'windows' else ''
    dart = destination / ('bin/cache/dart-sdk/bin/dart' + extension)
    runtime = dart.with_name('dartaotruntime' + extension)
    verify_native_host(dart, host)
    verify_native_host(runtime, host)
    # Rebuild Flutter tools from this port's source through normal bootstrap.
    # The official cached tool snapshot was compiled from the upstream source.
    for name in ('flutter_tools.snapshot', 'flutter_tools.stamp'):
        (destination / 'bin/cache' / name).unlink(missing_ok=True)
    if (destination / '.git/objects/info/alternates').exists():
        raise ValueError('Staged SDK has an external Git object dependency')
    if subprocess.check_output(['git', '-C', str(destination), 'status', '--porcelain'], text=True).strip():
        raise ValueError('Host bootstrap changed the tracked Flutter source')
    record = {'sdkHost': identity, 'sourceRevision': revision,
              'officialFrameworkRevision': framework,
              'officialArchiveSha256': host['officialSha256'],
              'officialDartSha256': sha(dart), 'officialAotRuntimeSha256': sha(runtime),
              'officialHostCacheStaged': True, 'nativeDartBinaryTypesVerified': True,
              'flutterBootstrapExecuted': False, 'trackedSourcesUnmodified': True,
              'sdkDependencyPathsPreflightPassed': False,
              'applicationBuildVerified': False, 'releaseReady': False}
    (destination / 'bin/cache/windows-arm32-host-bootstrap.json').write_text(
        json.dumps(record, indent=2) + '\n')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--host', required=True)
    args = parser.parse_args()
    print(json.dumps(bootstrap(args.repository, args.archive, args.sdk, args.host)))
