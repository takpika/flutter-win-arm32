#!/usr/bin/env python3
"""Compile the matching Dart source frontend for the Windows ARM32 SDK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from urllib.parse import urljoin, urlparse, unquote

from prepare_dependency import prepare_dependency


def build(root, dart, output, package_config):
    root, dart, output, package_config = (p.resolve() for p in
                                         (root, dart, output, package_config))
    source = root / 'engine/src/flutter/third_party/dart'
    prepare_dependency(source, 'dart', verify_only=True)
    if not dart.is_file() or not package_config.is_file():
        raise ValueError('Supply the host Dart executable and gclient-generated package config')
    packages = {item['name']: item for item in json.loads(package_config.read_text())['packages']}
    for name in ('frontend_server', 'front_end', 'kernel', 'vm', 'compiler'):
        if name not in packages:
            raise ValueError('Missing source frontend dependency: ' + name)
        uri = urlparse(urljoin(package_config.as_uri(), packages[name]['rootUri']))
        if uri.scheme != 'file' or Path(unquote(uri.path)).resolve() != source / 'pkg' / name:
            raise ValueError('Frontend dependency is outside this Dart source checkout: ' + name)
    output.mkdir(parents=True, exist_ok=True)
    scratch = output / 'tmp'
    scratch.mkdir(exist_ok=True)
    environment = dict(os.environ, CI='true', TMPDIR=str(scratch), TMP=str(scratch),
                       TEMP=str(scratch), XDG_CONFIG_HOME=str(output / 'config'))
    destination = output / 'frontend_server_aot.dart.snapshot'
    entry = source / 'pkg/frontend_server/bin/frontend_server_starter.dart'
    with (output / 'build.log').open('w') as log:
        subprocess.run([str(dart), 'compile', 'aot-snapshot',
                        '--packages=' + str(package_config),
                        '--output=' + str(destination), str(entry)],
                       cwd=source, env=environment, stdout=log, stderr=log, check=True)
    if not destination.is_file() or destination.stat().st_size == 0:
        raise ValueError('Frontend compiler did not produce an AOT snapshot')
    manifest = root / 'dev/windows_arm32/patches/dart-source-manifest.json'
    report = {'dartRevision': subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip(),
        'sourceManifestSha256': hashlib.sha256(manifest.read_bytes()).hexdigest(),
        'snapshotSha256': hashlib.sha256(destination.read_bytes()).hexdigest(),
        'hostBootstrapDartSha256': hashlib.sha256(dart.read_bytes()).hexdigest(),
        'compiledFromCurrentSource': True, 'targetRuntimeVmCopied': False}
    (output / 'frontend-source-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    return destination


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--dart', type=Path, required=True, help='Pinned host bootstrap Dart executable')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--package-config', type=Path,
                        help='Defaults to the Dart checkout package config produced by gclient')
    args = parser.parse_args()
    configuration = args.package_config or args.root / 'engine/src/flutter/third_party/dart/.dart_tool/package_config.json'
    print(build(args.root, args.dart, args.output, configuration))
