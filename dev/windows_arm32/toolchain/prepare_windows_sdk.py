#!/usr/bin/env python3
"""Prepare ANGLE's SDK headers without copying any Windows system library."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import urllib.request
import zipfile
from generate_xaml_headers import generate
from generate_xps_header import generate as generate_xps
from generate_uia_headers import generate as generate_uia

SDK_VERSION = '10.0.22621.1'
SDK_SHA256 = '180172b69e7a3ff74262919985fa1d3b1c20a135570de269514afe754a36d80f'
SDK_URL = ('https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.cpp/'
           + SDK_VERSION + '/microsoft.windows.sdk.cpp.' + SDK_VERSION + '.nupkg')
PREFIX = 'c/Include/10.0.22621.0/'


def prepare_foundation(include, mingw_sysroot):
    """Keep both WinRT Boolean and Byte boxing interfaces in the private SDK."""
    source = mingw_sysroot / 'generic-w64-mingw32/include/windows.foundation.h'
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    if source_hash != '30d962aaea8f28c16310076ce987bdfb0fe3267ea072d236e610620f1ac5b4e7':
        raise ValueError('Use the unmodified pinned MinGW WinRT header')
    original = source.read_text()
    # WIDL emits boolean (an unsigned-char typedef) as a C++ template argument,
    # colliding with BYTE. C++/WinRT projects Boolean as bool. Only correct the
    # template spelling; preserve the C ABI, both interface bodies and UUIDs.
    replacements = {'IReference<boolean >': ('IReference<bool >', 3),
                    'IReference_impl<boolean >': ('IReference_impl<bool >', 1)}
    updated = original
    for old, (new, expected) in replacements.items():
        # Count actual declarations and macros, excluding generated comments.
        lines = updated.splitlines(keepends=True)
        count = sum(line.count(old) for line in lines if not line.lstrip().startswith(('*', '/')))
        if count != expected:
            raise ValueError('Unexpected MinGW WinRT Boolean declaration: ' + old)
        updated = ''.join(line if line.lstrip().startswith(('*', '/')) else line.replace(old, new)
                          for line in lines)
    destination = include / 'windows.foundation.h'
    destination.write_text(updated)
    return {'sourceSha256': source_hash,
            'preparedSha256': hashlib.sha256(destination.read_bytes()).hexdigest(),
            'booleanAndByteInterfacesPreserved': True}


def prepare(output, package=None, mingw_sysroot=None):
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    previous = {}
    provenance_path = output / 'sdk-provenance.json'
    if provenance_path.exists():
        stat = provenance_path.stat()
        previous[provenance_path] = (hashlib.sha256(provenance_path.read_bytes()).digest(),
                                     stat.st_atime_ns, stat.st_mtime_ns)
    for directory in (output / 'include', output / 'inputs'):
        if directory.exists():
            for path in directory.rglob('*'):
                if path.is_file():
                    stat = path.stat()
                    previous[path] = (hashlib.sha256(path.read_bytes()).digest(),
                                      stat.st_atime_ns, stat.st_mtime_ns)
    if package is None:
        package = output / 'downloads' / ('windows-sdk-cpp-' + SDK_VERSION + '.nupkg')
        package.parent.mkdir(parents=True, exist_ok=True)
        if not package.exists():
            partial = package.with_suffix('.partial')
            try:
                with urllib.request.urlopen(SDK_URL) as response, partial.open('wb') as stream:
                    shutil.copyfileobj(response, stream)
                if hashlib.sha256(partial.read_bytes()).hexdigest() != SDK_SHA256:
                    raise ValueError('Microsoft SDK package SHA256 mismatch')
                partial.replace(package)
            finally:
                partial.unlink(missing_ok=True)
    if hashlib.sha256(package.read_bytes()).hexdigest() != SDK_SHA256:
        raise ValueError('Microsoft SDK package SHA256 mismatch')
    include = output / 'include'
    inputs = output / 'inputs'
    selected = {
        'cppwinrt/winrt/impl/windows.ui.xaml.0.h': inputs / 'projection/impl/Windows.UI.Xaml.0.h',
        'cppwinrt/winrt/impl/windows.ui.xaml.controls.0.h': inputs / 'projection/impl/Windows.UI.Xaml.Controls.0.h',
        'winrt/windows.ui.xaml.h': inputs / 'windows.ui.xaml.h',
        'winrt/windows.ui.xaml.controls.h': inputs / 'windows.ui.xaml.controls.h',
        'um/windows.ui.xaml.media.dxinterop.h': inputs / 'dxinterop.h',
        'cppwinrt/LICENSE.txt': output / 'CppWinRT-LICENSE.txt',
        'winrt/wrl.h': include / 'wrl-sdk-original.h',
        'winrt/roerrorapi.h': include / 'roerrorapi.h',
        'um/restrictederrorinfo.h': include / 'restrictedErrorInfo.h',
        'um/xpsobjectmodel.h': inputs / 'xpsobjectmodel.h',
        'um/uiautomationcore.h': inputs / 'UIAutomationCore.h',
        'um/propvarutil.h': inputs / 'propvarutil.h',
    }
    hashes = {}
    with zipfile.ZipFile(package) as archive:
        names = {name.lower(): name for name in archive.namelist()}
        for name in archive.namelist():
            if name.lower().startswith((PREFIX + 'winrt/wrl/').lower()) and name.lower().endswith('.h'):
                destination = include / name[len(PREFIX + 'winrt/'):]
                if name.lower().endswith('/wrl/client.h'):
                    destination = include / 'wrl-client-sdk-original.h'
                if name.lower().endswith('/wrl/implements.h'):
                    destination = include / 'wrl-implements-sdk-original.h'
                selected[name[len(PREFIX):]] = destination
        for name, destination in selected.items():
            data = archive.read(names[(PREFIX + name).lower()])
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            hashes[str(destination.relative_to(output))] = hashlib.sha256(data).hexdigest()
    generate(inputs / 'projection', inputs / 'windows.ui.xaml.h',
             inputs / 'windows.ui.xaml.controls.h', inputs / 'dxinterop.h', include)
    generate_xps(inputs / 'xpsobjectmodel.h', include / 'XpsObjectModel.h')
    generate_uia(inputs / 'UIAutomationCore.h', inputs / 'propvarutil.h', include)
    for name in ('uiautomation.h', 'propvarutil.h'):
        hashes['include/' + name] = hashlib.sha256((include / name).read_bytes()).hexdigest()
    hashes['include/XpsObjectModel.h'] = hashlib.sha256(
        (include / 'XpsObjectModel.h').read_bytes()).hexdigest()
    for name in ('wrl.h', 'wrl_compat.h', 'wrl/client.h', 'wrl/implements.h',
                 'intrin.h', 'dwrite_3.h', 'windows.h', 'sal.h', 'atlbase.h',
                 'atl-crt-compat.h', 'oleauto.h', 'guiddef.h', 'synchapi.h', 'comdef.h', '__iterator/iterator.h'):
        source = Path(__file__).resolve().parent / 'sdk' / name
        (include / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, include / name)
        hashes['include/' + name] = hashlib.sha256(source.read_bytes()).hexdigest()
    foundation = prepare_foundation(include, mingw_sysroot.resolve()) if mingw_sysroot else None
    if foundation:
        hashes['include/windows.foundation.h'] = foundation['preparedSha256']
    (output / 'sdk-provenance.json').write_text(json.dumps({
        'packageVersion': SDK_VERSION, 'packageUrl': SDK_URL, 'packageSha256': SDK_SHA256,
        'sdkLicenseUrl': 'https://aka.ms/WinSDKLicenseURL',
        'extractedSourceHashes': hashes, 'systemLibrariesCopied': False,
        'microsoftWrlHeadersUnmodified': True,
        'mingwWinRTFoundation': foundation,
    }, indent=2) + '\n')
    for path, (digest, accessed, modified) in previous.items():
        if path.exists() and hashlib.sha256(path.read_bytes()).digest() == digest:
            os.utime(path, ns=(accessed, modified))
    return include


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sdk-package', type=Path)
    parser.add_argument('--mingw-sysroot', type=Path,
                        help='Prepare WinRT template declarations in the SDK-private overlay')
    args = parser.parse_args()
    print(prepare(args.output, args.sdk_package, args.mingw_sysroot))
