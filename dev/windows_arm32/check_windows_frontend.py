#!/usr/bin/env python3
"""Run the generated Windows frontend on the official Windows Dart runtime."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from sdk_hosts import select_host, verify_native_host


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def winpath(path):
    return 'Z:' + str(path.resolve()).replace('/', '\\')


def check(args):
    verify_native_host(args.runtime, select_host('windows-x64'))
    root = Path(__file__).resolve().parent
    manifest = root / 'patches/dart-source-manifest.json'
    frontend = args.frontend.resolve()
    record = json.loads(frontend.with_name('frontend-source-provenance.json').read_text())
    if (record.get('sdkHost') != 'windows-x64' or not record['compiledFromCurrentSource']
            or record['snapshotSha256'] != sha(frontend)
            or record['sourceManifestSha256'] != sha(manifest)):
        raise ValueError('Frontend does not match this port and Windows host')
    platform = args.patched_sdk.resolve()
    engine = json.loads(args.engine_provenance.read_text())
    if engine['sourceManifests']['dart-source-manifest.json'] != sha(manifest):
        raise ValueError('Platform does not match the current compiler source')
    for name in ('platform_strong.dill', 'vm_outline_strong.dill'):
        expected = engine['families']['rt']['artifacts']['flutter_patched_sdk/' + name]
        if expected['sha256'] != sha(platform / name):
            raise ValueError('Source-built platform bytes changed: ' + name)
    output = args.output.resolve()
    scratch = output / 'tmp'
    scratch.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, CI='true', TMPDIR=str(scratch), TMP=str(scratch),
                       TEMP=str(scratch), WINEPREFIX=str(output / 'wine'), WINEDEBUG='-all')
    version = subprocess.check_output(['wine', str(args.runtime.resolve()), '--version'],
                                      env=environment, text=True, timeout=120).strip()
    if not version.startswith('Dart SDK version: 3.10.9 '):
        raise ValueError('Windows host Dart differs from Flutter 3.38.10')
    source, packages, kernel = output / 'proof.dart', output / 'package_config.json', output / 'proof.dill'
    source.write_text('void main() { print("Windows frontend 日本語"); }\n')
    packages.write_text('{"configVersion":2,"packages":[]}\n')
    result = subprocess.run(['wine', str(args.runtime.resolve()), winpath(frontend),
        '--target=flutter', '--aot', '--tfa', '--target-os=windows',
        '--sdk-root=' + winpath(platform) + '\\', '--packages=' + winpath(packages),
        '--output-dill=' + winpath(kernel), winpath(source)],
        env=environment, capture_output=True, text=True, timeout=180)
    (output / 'runtime-compile.log').write_text(result.stdout + result.stderr)
    if result.returncode != 0 or not kernel.is_file() or kernel.stat().st_size == 0:
        raise ValueError('Native Windows frontend did not compile the Flutter ARM32 fixture')
    proof = {'nativeWindowsFrontendExecutionPassed': True,
             'portedFlutterWindowsArm32KernelCompilationPassed': True,
             'runtimeVersion': version, 'runtimeSha256': sha(args.runtime),
             'snapshotSha256': sha(frontend), 'kernelSha256': sha(kernel),
             'sourceManifestSha256': sha(manifest), 'executionEnvironment': 'Wine 11',
             'fullSdkApplicationBuildVerified': False}
    (output / 'verification.json').write_text(json.dumps(proof, indent=2) + '\n')
    return proof


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('runtime', 'frontend', 'patched-sdk', 'engine-provenance', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    print(json.dumps(check(parser.parse_args()), indent=2))
