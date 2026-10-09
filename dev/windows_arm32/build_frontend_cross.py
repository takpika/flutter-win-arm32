#!/usr/bin/env python3
"""Generate a destination SDK frontend with its source-built cross generator.

This runs on Linux amd64. The result is an ELF AOT snapshot for the selected
SDK's dartaotruntime, including on Windows and macOS; it is not an executable.
Destination runtime execution is a separate, required distribution check.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from urllib.parse import urljoin, urlparse, unquote

from prepare_dependency import prepare_dependency
from sdk_hosts import select_host, verify_native_host


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(args):
    root = Path(__file__).resolve().parents[2]
    source = root / 'engine/src/flutter/third_party/dart'
    # gclient sync restores the pinned upstream checkout. Prepare its checked
    # Dart patch before comparing it with the cross-generator's source record.
    prepare_dependency(source, 'dart')
    host = select_host(args.target_host)
    dart = args.dart.resolve()
    generator = args.generator.resolve()
    verify_native_host(dart, select_host('linux-x64'))
    verify_native_host(generator, select_host('linux-x64'))
    record = json.loads(args.generator_provenance.read_text())
    manifest = root / 'dev/windows_arm32/patches/dart-source-manifest.json'
    manifest_hash = sha(manifest)
    if (not record['sourceBuilt'] or record['aotDestinationHost'] != host['id']
            or record['buildHost'] != 'linux-x64'
            or record['generatorSha256'] != sha(generator)
            or record.get('frontendCompilerSourceManifestSha256') != manifest_hash
            or record.get('hostVmSourceUnmodified') is not True
            or record.get('hostVmSnapshotVersion') != '1ce86630892e2dca9a8543fdb8ed8e22'
            or record.get('hostVmUpstreamRevision') != '7c8a3e077e8a3c6209c865301aada26e9fde764a'):
        raise ValueError('Frontend generator does not match the destination and Dart source')
    revision = dart.parent.parent.joinpath('revision').read_text().strip()
    if revision != '7c8a3e077e8a3c6209c865301aada26e9fde764a':
        raise ValueError('Bootstrap Dart revision does not match Flutter 3.38.10')
    packages = source / '.dart_tool/package_config.json'
    entries = {p['name']: p for p in json.loads(packages.read_text())['packages']}
    for name in ('frontend_server', 'front_end', 'kernel', 'vm', 'compiler'):
        uri = urlparse(urljoin(packages.as_uri(), entries[name]['rootUri']))
        if uri.scheme != 'file' or Path(unquote(uri.path)).resolve() != source / 'pkg' / name:
            raise ValueError('Frontend package is outside the matching Dart checkout: ' + name)
    output = args.output.resolve()
    scratch = output / 'tmp'
    scratch.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, CI='true', TMPDIR=str(scratch), TMP=str(scratch),
                       TEMP=str(scratch), XDG_CONFIG_HOME=str(output / 'config'))
    platform = output / 'vm_platform.dill'
    outline = output / 'vm_outline.dill'
    kernel = output / 'frontend.dill'
    snapshot = output / 'frontend_server_aot.dart.snapshot'
    sdk_hash = revision[:10]
    common = [str(dart), '-Dsdk_hash=' + sdk_hash, '--packages=' + str(packages)]
    commands = [
        [*common, str(source / 'pkg/front_end/tool/compile_platform.dart'),
         'dart:core', '-Ddart.vm.product=true', '-Ddart.isVM=true',
         '--single-root-scheme=org-dartlang-sdk', '--single-root-base=' + str(source),
         'org-dartlang-sdk:///sdk/lib/libraries.json', str(outline), str(platform), str(outline)],
        [*common, str(source / 'pkg/vm/bin/gen_kernel.dart'),
         '--packages=' + str(packages), '--platform=' + str(platform), '--aot', '--tfa',
         '--target-os=' + host['os'], '-Dsdk_hash=' + sdk_hash, '-Ddart.vm.product=true',
         '--output=' + str(kernel), str(source / 'pkg/frontend_server/bin/frontend_server_starter.dart')],
        [str(generator), '--deterministic', '--snapshot_kind=app-aot-elf',
         '--elf=' + str(snapshot), str(kernel)],
    ]
    for name, command in zip(('platform', 'kernel', 'snapshot'), commands):
        with (output / (name + '-build.log')).open('w') as log:
            subprocess.run(command, cwd=source, env=environment,
                           stdout=log, stderr=log, check=True)
    with snapshot.open('rb') as file:
        header = file.read(20)
    machine = {'x64': 62, 'arm64': 183}[host['cpu']]
    if header[:6] != b'\x7fELF\x02\x01' or int.from_bytes(header[18:20], 'little') != machine:
        raise ValueError('Frontend AOT does not match the destination architecture')
    prepare_dependency(source, 'dart', verify_only=True)
    if sha(manifest) != manifest_hash:
        raise ValueError('Dart source manifest changed during frontend generation')
    report = {'dartRevision': revision, 'sourceManifestSha256': manifest_hash,
              'snapshotSha256': sha(snapshot), 'hostBootstrapDartSha256': sha(dart),
              'compiledFromCurrentSource': True, 'targetRuntimeVmCopied': False,
              'sdkHost': host['id'], 'generatorSha256': sha(generator),
              'generatorProvenanceSha256': sha(args.generator_provenance),
              'platformSha256': sha(platform), 'inputKernelSha256': sha(kernel),
              'destinationFrontendRuntimeVerified': False}
    (output / 'frontend-source-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target-host', required=True)
    for name in ('dart', 'generator', 'generator-provenance', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    print(json.dumps(build(parser.parse_args()), indent=2))
