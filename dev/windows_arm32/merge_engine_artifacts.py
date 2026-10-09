#!/usr/bin/env python3
"""Combine verified RT/Phone CI outputs for the SDK assembly entry point."""
import argparse
import json
from pathlib import Path
import shutil

from build_engine import artifacts, digest


def merge(rt, phone, output, host_cpu):
    inputs = {'rt': rt.resolve(), 'phone': phone.resolve()}
    output = output.resolve()
    if output.exists():
        raise ValueError('Use a new output directory; existing builds are preserved')
    reports = {}
    frontends = {}
    verified = {}
    for family, directory in inputs.items():
        report = json.loads((directory / 'engine-build-provenance.json').read_text())
        if not report['freshBuildInvocation'] or set(report['families']) != {family}:
            raise ValueError('Expected a source-build record for only ' + family)
        actual = artifacts(directory, family, host_cpu, compiler_platform=True)
        if actual != report['families'][family]['artifacts']:
            raise ValueError('Artifacts differ from source-build record: ' + family)
        frontend = json.loads((directory / 'frontend-source-provenance.json').read_text())
        if not frontend['compiledFromCurrentSource']:
            raise ValueError('Frontend was not compiled from source: ' + family)
        if digest(directory / 'frontend_server_aot.dart.snapshot') != frontend['snapshotSha256']:
            raise ValueError('Frontend output changed: ' + family)
        if frontend['sourceManifestSha256'] != report['sourceManifests']['dart-source-manifest.json']:
            raise ValueError('Frontend and engine use different Dart patches: ' + family)
        reports[family], frontends[family], verified[family] = report, frontend, actual
    for key in ('frameworkRevision', 'toolchainConfigSha256', 'sourceManifests'):
        if reports['rt'][key] != reports['phone'][key]:
            raise ValueError('RT and Phone source builds differ: ' + key)
    for key in ('dartRevision', 'sourceManifestSha256'):
        if frontends['rt'][key] != frontends['phone'][key]:
            raise ValueError('RT and Phone frontends differ: ' + key)
    for name in ('platform_strong.dill', 'vm_outline_strong.dill'):
        if digest(inputs['rt'] / 'flutter_patched_sdk' / name) != digest(inputs['phone'] / 'flutter_patched_sdk' / name):
            raise ValueError('Compiler platform differs between families: ' + name)
    licenses = ('Flutter-LICENSE.txt', 'Flutter-ThirdParty-LICENSE.txt', 'LLVM-LICENSE.txt')
    for name in licenses:
        if digest(inputs['rt'] / name) != digest(inputs['phone'] / name):
            raise ValueError('License notices differ: ' + name)
    # All source/byte checks precede output creation. No extra DLLs from the
    # input directories are copied into the combined distribution.
    output.mkdir(parents=True)
    for family, directory in inputs.items():
        destination = output / 'out' / ('win_release_arm_' + family)
        for name in verified[family]:
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(directory / name, target)
        evidence = output / 'source-build-records' / family
        evidence.mkdir(parents=True)
        for name in ('engine-build-provenance.json', 'frontend-source-provenance.json'):
            shutil.copy2(directory / name, evidence)
    frontend = output / 'frontend'
    frontend.mkdir()
    for name in ('frontend_server_aot.dart.snapshot', 'frontend-source-provenance.json'):
        shutil.copy2(inputs['rt'] / name, frontend)
    for name in licenses:
        shutil.copy2(inputs['rt'] / name, output)
    combined = dict(reports['rt'])
    combined['families'] = {family: report['families'][family] for family, report in reports.items()}
    combined['mergedCiSourceBuilds'] = True
    (output / 'engine-build-provenance.json').write_text(json.dumps(combined, indent=2) + '\n')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rt', type=Path, required=True)
    parser.add_argument('--phone', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--host-cpu', choices=('arm64', 'x64'), required=True)
    args = parser.parse_args()
    print(merge(args.rt, args.phone, args.output, args.host_cpu))
