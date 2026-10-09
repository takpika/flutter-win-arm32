#!/usr/bin/env python3
"""Build the pinned upstream C++/WinRT generator and its SDK projection."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import zipfile

from prepare_windows_sdk import PREFIX, SDK_SHA256, SDK_VERSION

REVISION = 'd2a66776bb16e1da9dac60770c977e847485e24b'
VERSION = '2.0.240405.15'
SOURCE_SHA256 = 'efe2adf8207d2ae56d3ddd75b8a15fc13fbae9afa200db3a4c34316e70cadb4d'
SOURCE_URL = 'https://codeload.github.com/microsoft/cppwinrt/tar.gz/' + REVISION
WINMD_REVISION = '0f1eae3bfa63fa2ba3c2912cbfe72a01db94cc5a'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(source_archive, sdk_package, output, jobs=2):
    source_archive, sdk_package, output = (
        path.resolve() for path in (source_archive, sdk_package, output))
    if sha(source_archive) != SOURCE_SHA256 or sha(sdk_package) != SDK_SHA256:
        raise ValueError('C++/WinRT source or Windows SDK checksum mismatch')
    output.mkdir(parents=True, exist_ok=True)
    scratch = output / 'tmp'
    scratch.mkdir(exist_ok=True)
    env = dict(os.environ, TMPDIR=str(scratch), TMP=str(scratch), TEMP=str(scratch))
    source_root = output / 'source'
    source = source_root / ('cppwinrt-' + REVISION)
    with tarfile.open(source_archive) as archive:
        originals = {}
        for member in archive.getmembers():
            if member.isfile():
                relative = Path(member.name).relative_to('cppwinrt-' + REVISION)
                originals[str(relative)] = hashlib.sha256(archive.extractfile(member).read()).hexdigest()
        if not source.exists():
            source_root.mkdir(exist_ok=True)
            archive.extractall(source_root, filter='data')

    def verify_source():
        actual = {str(path.relative_to(source)): sha(path)
                  for path in source.rglob('*') if path.is_file() and path.name != '.DS_Store'}
        if actual != originals:
            raise ValueError('Refusing to build modified C++/WinRT sources')

    verify_source()
    metadata = output / 'metadata'
    metadata.mkdir(exist_ok=True)
    inputs = {}
    with zipfile.ZipFile(sdk_package) as archive:
        selected = {Path(name).name: name for name in archive.namelist()
                    if name.startswith(PREFIX.replace('/Include/', '/References/'))
                    and name.endswith('.winmd')}
        selected['Windows.winmd'] = 'c/UnionMetadata/10.0.22621.0/Facade/windows.winmd'
        selected['Microsoft.UI.Xaml.Markup.winmd'] = (
            'c/bin/10.0.22621.0/XamlCompiler/Microsoft.UI.Xaml.Markup.winmd')
        for name, member in selected.items():
            data = archive.read(member)
            expected = hashlib.sha256(data).hexdigest()
            target = metadata / name
            if target.exists() and sha(target) != expected:
                raise ValueError('Refusing to replace modified Windows metadata: ' + name)
            if not target.exists():
                target.write_bytes(data)
            inputs[name] = expected
    if set(path.name for path in metadata.iterdir()) != set(inputs):
        raise ValueError('Unexpected Windows metadata inputs')
    build = output / 'build'
    subprocess.run(['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja',
                    '-DCMAKE_BUILD_TYPE=Release', '-DCPPWINRT_BUILD_VERSION=' + VERSION,
                    '-DEXTERNAL_WINMD_INCLUDE_DIR='],
                   env=env, check=True)
    subprocess.run(['cmake', '--build', str(build), '--target', 'cppwinrt',
                    '--parallel', str(jobs)], env=env, check=True)
    generator = build / 'cppwinrt'
    winmd = build / 'winmd-prefix/src/winmd'
    dependency_revision = subprocess.check_output(
        ['git', '-C', str(winmd), 'rev-parse', 'HEAD'], text=True).strip()
    dependency_changes = subprocess.check_output(
        ['git', '-C', str(winmd), 'status', '--porcelain'], text=True).strip()
    if dependency_revision != WINMD_REVISION or dependency_changes:
        raise ValueError('Unexpected or modified winmd generator dependency')
    projection = output / 'projection'
    # Generate into a fresh directory; never overwrite a modified SDK projection.
    if projection.exists():
        raise ValueError('Projection output already exists')
    subprocess.run([str(generator), '-input', str(metadata), '-output', str(projection), '-base'],
                   env=env, check=True)
    verify_source()
    if any(sha(metadata / name) != expected for name, expected in inputs.items()):
        raise ValueError('Windows metadata changed during generation')
    base = projection / 'winrt/base.h'
    if '#define CPPWINRT_VERSION "' + VERSION + '"' not in base.read_text():
        raise ValueError('Unexpected C++/WinRT generator version')
    shutil.copy2(source / 'LICENSE', projection / 'CppWinRT-LICENSE.txt')
    shutil.copy2(winmd / 'LICENSE', projection / 'Winmd-LICENSE.txt')
    files = {str(path.relative_to(projection)): sha(path)
             for path in sorted(projection.rglob('*')) if path.is_file()}
    report = {'sourceRevision': REVISION, 'generatorVersion': VERSION,
              'sourceArchiveSha256': SOURCE_SHA256, 'sdkPackageVersion': SDK_VERSION,
              'winmdSourceRevision': dependency_revision,
              'sdkPackageSha256': SDK_SHA256, 'metadataHashes': inputs,
              'generatorSha256': sha(generator), 'generatedFiles': files,
              'sourceBuiltGenerator': True, 'systemLibrariesCopied': False}
    (projection / 'projection-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    return projection


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-archive', type=Path, required=True)
    parser.add_argument('--sdk-package', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    print(prepare(args.source_archive, args.sdk_package, args.output, args.jobs))
