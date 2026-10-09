#!/usr/bin/env python3
"""Exercise the source-built Windows generator with a real ARM32 AOT input."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def windows_path(path):
    # Wine's standard Z: mapping exposes the container filesystem.
    return 'Z:' + str(path.resolve()).replace('/', '\\')


def check(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    scratch = output / 'tmp'
    scratch.mkdir(exist_ok=True)
    environment = dict(os.environ, TMPDIR=str(scratch), TMP=str(scratch), TEMP=str(scratch),
                       WINEPREFIX=str(output / 'wine'), WINEDEBUG='-all', CI='true')
    version = subprocess.check_output([str(args.dart.resolve()), '--version'],
                                      env=environment, text=True).strip()
    if not version.startswith('Dart SDK version: 3.10.9 '):
        raise ValueError('Bootstrap Dart version does not match Flutter 3.38.10')
    source = output / 'snapshot-proof.dart'
    source.write_text('void main() { print("Windows ARM32 snapshot 日本語"); }\n')
    kernel = output / 'snapshot-proof.dill'
    frontend = args.frontend_server.resolve()
    record = json.loads(frontend.with_name('frontend-source-provenance.json').read_text())
    manifest = Path(__file__).resolve().parent / 'patches/dart-source-manifest.json'
    if (not record['compiledFromCurrentSource'] or record['snapshotSha256'] != sha(frontend)
            or record['sourceManifestSha256'] != sha(manifest)):
        raise ValueError('Frontend does not match the current port ABI metadata')
    packages = output / 'package_config.json'
    packages.write_text('{"configVersion":2,"packages":[]}\n')
    subprocess.run([str(args.dart.resolve().with_name('dartaotruntime')), str(frontend),
                    '--sdk-root', str(args.patched_sdk.resolve()) + '/', '--target=flutter',
                    '--aot', '--tfa', '--target-os=windows', '--packages=' + str(packages),
                    '--output-dill=' + str(kernel), str(source)], cwd=output,
                   env=environment, check=True)
    image = output / 'snapshot-proof.so'
    result = subprocess.run(['wine', str(args.generator.resolve()),
                             '--snapshot_kind=app-aot-elf', '--elf=' + windows_path(image),
                             windows_path(kernel)], env=environment,
                            cwd=output, capture_output=True, text=True, timeout=180)
    (output / 'aot-stdout.log').write_text(result.stdout)
    (output / 'aot-stderr.log').write_text(result.stderr)
    if result.returncode != 0:
        raise ValueError('Windows generator could not compile the ARM32 AOT input: ' + str(result.returncode))
    header = image.read_bytes()[:52]
    if (header[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', header, 18)[0] != 40):
        raise ValueError('Windows generator did not produce a little-endian ARM32 ELF image')
    proof = {'nativeWindowsGeneratorAotExecutionPassed': True, 'elfMachine': 'ARM',
             'generatorSha256': sha(args.generator), 'bootstrapDartVersion': version,
             'bootstrapDartSha256': sha(args.dart), 'inputKernelSha256': sha(kernel),
             'frontendSha256': sha(frontend), 'dartSourceManifestSha256': sha(manifest),
             'aotImageSha256': sha(image), 'runtimeEnvironment': 'Wine 11',
             'thumb2ConversionVerified': False, 'fullSdkApplicationBuildVerified': False}
    (output / 'aot-verification.json').write_text(json.dumps(proof, indent=2) + '\n')
    return proof


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generator', type=Path, required=True)
    parser.add_argument('--dart', type=Path, required=True)
    parser.add_argument('--frontend-server', type=Path, required=True)
    parser.add_argument('--patched-sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    print(json.dumps(check(parser.parse_args()), indent=2))
