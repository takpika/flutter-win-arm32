#!/usr/bin/env python3
"""Stage verified host components and the common Windows ARM32 target data."""
import hashlib
import json
from pathlib import Path
import shutil
import tarfile

from sys import path
path.insert(0, str(Path(__file__).resolve().parents[1] / 'toolchain'))
path.insert(0, str(Path(__file__).resolve().parents[1]))
from stage_windows_target_sysroot import stage
from sdk_hosts import select_host, verify_native_host
from toolchain.windows_native_dependencies import system_library


def install_resource_tools(resources, compiler):
    """Supplement the same LLVM build without replacing any compiler file."""
    report = json.loads((resources / 'resource-tools-provenance.json').read_text())
    common = json.loads((compiler / 'source-provenance.json').read_text())
    source = report['source']
    if (report['host'] != 'windows-x64' or report['systemLibrariesCopied'] is not False
            or source['sourceBuilt'] is not True
            or source['builtTools'] != ['llvm-rc', 'llvm-cvtres']
            or any(source[key] != common[key] for key in ('revision', 'patches', 'requiredFeatures'))):
        raise ValueError('Resource utilities do not match the shared native compiler')
    for name, expected in report['files'].items():
        relative = Path(name)
        if relative.is_absolute() or '..' in relative.parts or digest(resources / relative) != expected:
            raise ValueError('Resource utility payload differs from its source build')
    staged = []
    for file in (resources / 'bin').iterdir():
        relative = file.relative_to(resources).as_posix()
        if relative not in report['files'] or system_library(file.name):
            raise ValueError('Unverified or system resource-tool binary')
        verify_native_host(file, select_host('windows-x64'))
        destination = compiler / 'bin' / file.name
        if destination.exists():
            if digest(destination) != digest(file):
                raise ValueError('Resource utility would replace a different shared compiler dependency')
        else:
            shutil.copy2(file, destination)
            staged.append(file.name)
    for name in ('llvm-rc.exe', 'llvm-cvtres.exe'):
        if not (compiler / 'bin' / name).is_file():
            raise ValueError('Required common resource utility missing')
    return {'addedCompilerFiles': staged, 'existingCompilerFilesChanged': False,
            'sharedSourceRevision': source['revision'], 'systemLibrariesCopied': False}


def digest(file):
    result = hashlib.sha256()
    with file.open('rb') as source:
        for block in iter(lambda: source.read(1048576), b''):
            result.update(block)
    return result.hexdigest()


def prepare(work, sdk, host_name='windows-x64'):
    host = select_host(host_name)
    if host['os'] not in ('windows', 'macos'):
        raise ValueError('Select a Windows or macOS SDK host')
    dependencies = sdk / 'bin/cache/windows-arm32-dependencies'
    dependencies.mkdir(parents=True, exist_ok=False)
    inputs = work / 'inputs'
    records = {}
    components = {
        'compiler': ('windows-arm32-compiler-' + host_name + '.tar.gz', dependencies / 'compiler'),
        'python': ('python-' + host_name + '.tar.gz', dependencies / 'python'),
        'rust-1.95.0': ('windows-arm32-rust-1.95.0-' + host_name + '.tar.gz', dependencies / 'rust-1.95.0'),
        'rust-1.93.1': ('windows-arm32-rust-1.93.1-' + host_name + '.tar.gz', dependencies / 'rust-1.93.1'),
        'msix': ('windows-arm32-msix-' + host_name + '.tar.gz', dependencies / 'msix'),
        'engines': ('windows-arm32-engines-linux-x64.tar.gz', work / 'engines'),
    }
    if host['os'] == 'windows':
        components['resources'] = ('windows-arm32-resource-tools-windows-x64.tar.gz', dependencies / 'resource-tools')
    else:
        components['generator'] = ('windows-arm32-snapshot-' + host_name + '.tar.gz', work / 'generator')
    for name, (filename, destination) in components.items():
        archive = inputs / name / filename
        actual = digest(archive)
        checksum = archive.with_name(filename + '.sha256')
        if host['os'] == 'macos' and not checksum.is_file():
            raise ValueError('Missing component checksum: ' + name)
        if checksum.is_file() and actual != checksum.read_text().split()[0]:
            raise ValueError('Component checksum mismatch: ' + name)
        destination.mkdir(parents=True)
        with tarfile.open(archive) as source:
            source.extractall(destination, filter='data')
        records[name] = {'archiveSha256': actual}
        archive.unlink()
    if host['os'] == 'windows':
        records['resources'].update(install_resource_tools(dependencies / 'resource-tools',
                                                          dependencies / 'compiler'))
    # Reuse only target headers/libraries and source data from the verified
    # Linux SDK. Its host executables and application VM tools are not staged.
    archive = inputs / 'linux-sdk/flutter-windows-arm32-linux-x64.tar.gz'
    actual = digest(archive)
    if actual != archive.with_name(archive.name + '.sha256').read_text().split()[0]:
        raise ValueError('Target-data source SDK checksum mismatch')
    prefix = 'flutter/bin/cache/windows-arm32-dependencies/'
    selected = {'cppwinrt', 'windows-sdk', 'atl', 'rust-dependencies',
                'llvm-mingw-20251104-ucrt-ubuntu-22.04-x86_64'}
    temporary = work / 'target-data-source'
    temporary.mkdir()
    with tarfile.open(archive) as source:
        for member in source:
            if member.name.startswith(prefix) and member.name[len(prefix):].split('/')[0] in selected:
                source.extract(member, temporary, filter='data')
    original = temporary / prefix
    for name in selected - {'llvm-mingw-20251104-ucrt-ubuntu-22.04-x86_64'}:
        shutil.copytree(original / name, dependencies / name)
    # Target imports/projections remain original. Refresh this SDK-owned shim
    # from the current port source, including any materialized case aliases.
    windows = dependencies / 'windows-sdk'
    shim = Path(__file__).resolve().parents[1] / 'toolchain/sdk/windows.h'
    provenance = json.loads((windows / 'sdk-provenance.json').read_text())
    refreshed = []
    for destination in (windows / 'include').iterdir():
        if destination.is_file() and destination.name.lower() == 'windows.h':
            shutil.copy2(shim, destination)
            name = destination.relative_to(windows).as_posix()
            provenance['extractedSourceHashes'][name] = digest(shim)
            refreshed.append(name)
    if not refreshed:
        raise ValueError('Expected the SDK-owned windows.h shim')
    (windows / 'sdk-provenance.json').write_text(json.dumps(provenance, indent=2)+'\n')
    records['currentHeaderShims'] = {'files': refreshed, 'sourceSha256': digest(shim),
                                     'microsoftHeadersChanged': False}
    stage(original / 'llvm-mingw-20251104-ucrt-ubuntu-22.04-x86_64', dependencies / 'target-sysroot')
    records['targetData'] = {'sourceSdkArchiveSha256': actual,
                            'linuxHostCompilerStaged': False}
    shutil.rmtree(temporary)
    archive.unlink()
    if host['os'] == 'windows':
        launcher = dependencies / 'launcher'
        launcher.mkdir()
        for file in ('sdk-python-launcher.exe', 'sdk-python-launcher.provenance.json'):
            shutil.copy2(inputs / 'launcher/launcher' / file, launcher / file)
    (work / (host['os'] + '-sdk-inputs.json')).write_text(json.dumps(records, indent=2) + '\n')


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--host', default='windows-x64', choices=('windows-x64', 'macos-x64', 'macos-arm64'))
    args = parser.parse_args()
    prepare(args.work.resolve(), args.sdk.resolve(), args.host)
