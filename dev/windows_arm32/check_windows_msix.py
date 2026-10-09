#!/usr/bin/env python3
"""Repackage a real Phone bundle with the native Windows MSIX tool under Wine."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import zipfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def verify(distribution, fixture, output, wine_prefix=None, xml_runtime_record=None):
    output.mkdir(parents=True, exist_ok=False)
    prefix = wine_prefix.resolve() if wine_prefix else output / 'wine'
    with tarfile.open(fixture) as archive:
        archive.extractall(output, filter='data')
    bundle = output / 'bundle'
    if not (bundle / 'AppxManifest.xml').is_file():
        raise ValueError('Missing actual Phone application manifest')
    moved = output / 'Phone bundle 日本語'
    bundle.rename(moved)
    expected = {path.relative_to(moved).as_posix(): digest(path.read_bytes())
                for path in moved.rglob('*') if path.is_file()}
    package = output / 'Phone package 日本語.appx'
    command = ['wine', str((distribution / 'bin/makemsix.exe').resolve()),
               'pack', '-d', str(moved.resolve()), '-p', str(package.resolve())]
    result = subprocess.run(command, capture_output=True, text=True, timeout=300,
                            env=dict(os.environ, WINEDEBUG='-all',
                                     WINEPREFIX=str(prefix)))
    (output / 'pack-stdout.log').write_text(result.stdout)
    (output / 'pack-stderr.log').write_text(result.stderr)
    result.check_returncode()
    with zipfile.ZipFile(package) as archive:
        if archive.testzip() is not None:
            raise ValueError('Package ZIP integrity failed')
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError('Duplicate package members')
        actual = {name: digest(archive.read(name)) for name in expected}
        if actual != expected:
            raise ValueError('Windows packager changed application contents')
        for required in ('AppxBlockMap.xml', '[Content_Types].xml'):
            if required not in names:
                raise ValueError('Missing generated package metadata: ' + required)
    environment = dict(os.environ, WINEDEBUG='-all',
                       WINEPREFIX=str(prefix))
    executable = str((distribution / 'bin/makemsix.exe').resolve())
    # These SDK build outputs are unsigned. First prove that the normal
    # signature requirement is still enforced, then use upstream's explicit
    # unsigned-package option solely for this extraction test.
    rejected = subprocess.run(['wine', executable, 'unpack', '-p', str(package),
                               '-d', str(output / 'signed-required')],
                              env=environment, capture_output=True, text=True, timeout=300)
    (output / 'signature-required-stdout.log').write_text(rejected.stdout)
    (output / 'signature-required-stderr.log').write_text(rejected.stderr)
    if rejected.returncode == 0:
        raise ValueError('Unsigned package accepted without the explicit unsigned option')
    # Pinned upstream MsixErrors.hpp: MissingAppxSignatureP7X = 0x8BAD0031.
    if '0x8bad0031' not in (rejected.stdout + rejected.stderr).lower():
        raise ValueError('Unsigned-package rejection was not the missing-signature error')
    extracted = output / 'Native unpack 日本語'
    unpacked = subprocess.run(['wine', executable, 'unpack', '-ss',
                               '-p', str(package), '-d', str(extracted)],
                              env=environment, capture_output=True, text=True, timeout=300)
    (output / 'unpack-stdout.log').write_text(unpacked.stdout)
    (output / 'unpack-stderr.log').write_text(unpacked.stderr)
    unpacked.check_returncode()
    actual = {name: digest((extracted / name).read_bytes()) for name in expected}
    if actual != expected:
        raise ValueError('Native Windows extraction changed application contents')
    report = {
        'nativeWindowsPackagingVerified': True,
        'verificationRuntime': 'Wine 11 on Ubuntu amd64',
        'unicodeInputAndOutputPathsVerified': True,
        'fixtureSha256': digest(fixture.read_bytes()),
        'packagerSha256': digest((distribution / 'bin/makemsix.exe').read_bytes()),
        'packageSha256': digest(package.read_bytes()),
        'applicationFileHashes': expected,
        'applicationContentsUnmodified': True,
        'nativeUnpackVerified': True,
        'unsignedPackageRejectedWithDefaultSignaturePolicy': True,
        'unsignedExtractionOption': '-ss (upstream test option; binary validation features retained)',
        'packageInstallationVerified': False,
        'fullSdkApplicationBuildVerified': False,
    }
    if xml_runtime_record is not None:
        report['ciWindowsXmlRuntime'] = json.loads(xml_runtime_record.read_text())
    (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--distribution', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--wine-prefix', type=Path)
    parser.add_argument('--xml-runtime-record', type=Path)
    args = parser.parse_args()
    verify(args.distribution.resolve(), args.fixture.resolve(), args.output.resolve(),
           args.wine_prefix, args.xml_runtime_record)
