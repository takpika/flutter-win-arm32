#!/usr/bin/env python3
"""Cross-build the full upstream Windows MSIX packager in a private checkout."""
import argparse
import json
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import urllib.request
import zipfile

from package_compiler import sha
from prepare_msix import REVISION, REPOSITORY
from prepare_windows_sdk import SDK_SHA256, PREFIX as SDK_PREFIX
from windows_native_dependencies import audit
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sdk_hosts import select_host, verify_native_host

SDK_X64_URL = ('https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.cpp.x64/'
               '10.0.22621.1/microsoft.windows.sdk.cpp.x64.10.0.22621.1.nupkg')
SDK_X64_SHA256 = '6c4c3efb8a62a10d0ea8627a5fb247e9cdd8e4119ee823468a51f3d599802ff8'


def build(args):
    output = args.output.resolve()
    configuration = args.toolchain_config.resolve()
    if args.jobs < 1 or not configuration.is_file():
        raise ValueError('Supply the private source-built compiler configuration and positive job count')
    scratch = output / 'tmp'
    scratch.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, TMPDIR=str(scratch), TMP=str(scratch), TEMP=str(scratch))
    identity = sha(configuration)
    if sha(args.sdk_package) != SDK_SHA256:
        raise ValueError('Windows SDK declaration package differs from the pinned source')
    headers = output / 'sdk-headers'
    headers.mkdir(exist_ok=True)
    with zipfile.ZipFile(args.sdk_package) as archive:
        names = {name.lower(): name for name in archive.namelist()}
        data = archive.read(names[(SDK_PREFIX + 'um/AppxPackaging.h').lower()])
    declaration = headers / 'AppxPackaging.h'
    declaration.write_bytes(data)
    # The GNU SDK's uuid archive does not contain these Appx interface IDs.
    # Use the matching official SDK's constant definitions at link time only;
    # neither its library nor any Windows system DLL enters the distribution.
    package = output / 'windows-sdk-x64.nupkg'
    if not package.exists():
        with urllib.request.urlopen(SDK_X64_URL, timeout=60) as response, package.open('wb') as target:
            shutil.copyfileobj(response, target)
    if sha(package) != SDK_X64_SHA256:
        raise ValueError('Windows SDK x64 library package differs from its pinned source')
    guid_library = output / 'sdk-uuid.lib'
    with zipfile.ZipFile(package) as archive:
        guid_library.write_bytes(archive.read('c/um/x64/Uuid.Lib'))
    config = json.loads(configuration.read_text())
    llvm = Path(config['llvmBin'])
    if not llvm.is_absolute():
        llvm = (configuration.parent / llvm).resolve()
    gnu = Path(config['sysroot'])
    if not gnu.is_absolute():
        gnu = (configuration.parent / gnu).resolve()
    delayimp = gnu / 'x86_64-w64-mingw32/include/delayimp.h'
    original_delayimp = delayimp.read_text()
    updated_delayimp = original_delayimp
    for hook in ('__pfnDliNotifyHook2', '__pfnDliFailureHook2'):
        original = 'ExternC PfnDliHook ' + hook + ';'
        if updated_delayimp.count(original) != 1:
            raise ValueError('Unexpected GNU delay-load hook declaration: ' + hook)
        updated_delayimp = updated_delayimp.replace(original, 'ExternC const PfnDliHook ' + hook + ';')
    # MSVC declares these hooks const. Preserve that declaration and the
    # upstream hook definition, without editing the shared GNU installation.
    (headers / 'delayimp.h').write_text(updated_delayimp)
    source = output / 'source'
    if not source.exists():
        subprocess.run(['git', 'init', str(source)], env=environment, check=True)
        subprocess.run(['git', '-C', str(source), 'fetch', '--depth', '1', args.source_repository, REVISION],
                       env=environment, check=True)
        subprocess.run(['git', '-C', str(source), 'checkout', '--detach', 'FETCH_HEAD'],
                       env=environment, check=True)
    if subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() != REVISION:
        raise ValueError('MSIX source revision differs from the pinned upstream')
    def source_changes():
        changes = subprocess.check_output(['git', '-C', str(source), 'diff', '--name-status'], text=True).strip()
        if changes not in ('', 'D\tlib/zlib/zconf.h'):
            raise ValueError('Refusing modified MSIX sources: ' + changes)
        return changes
    source_changes()
    drivers = Path(__file__).resolve().parent
    wrappers = {}
    for language in ('c', 'cxx'):
        wrapper = output / (language + '-' + identity + '.py')
        wrapper.write_text(
            'import os, sys\n'
            f'os.environ["FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG"] = {str(configuration)!r}\n'
            'os.environ["FLUTTER_WINDOWS_ARM32_PLATFORM"] = "rt"\n'
            f'sys.path.insert(0, {str(drivers)!r})\n'
            'from cmake_driver import run\n'
            f'raise SystemExit(run(target_cpu="x64", language={language!r}))\n')
        wrappers[language] = wrapper
    toolchain = output / 'windows-x64.cmake'
    naming = output / 'windows-library-names.cmake'
    # Upstream's export definition and loader use MSIX.dll, as with MSVC.
    # Override only this private project's GNU default "lib" DLL prefix.
    naming.write_text('set(CMAKE_SHARED_LIBRARY_PREFIX "")\n'
                      'set(CMAKE_SHARED_LIBRARY_PREFIX_C "")\n'
                      'set(CMAKE_SHARED_LIBRARY_PREFIX_CXX "")\n')
    entry = drivers / 'windows_utf8_cli_entry.cc'
    entry_config = output / 'windows-utf8-entry.cmake'
    # Upstream consumes UTF-8 argv. The Windows narrow CRT entry loses paths
    # outside the system ANSI code page. Adapt only this host executable's
    # entry, preserving every original source byte and the shared compiler.
    entry_config.write_text(
        'function(flutter_msix_utf8_entry)\n'
        '  if(TARGET makemsix)\n'
        f'    set_property(SOURCE {json.dumps(str(source / "src/makemsix/main.cpp"))} '
        'TARGET_DIRECTORY makemsix APPEND PROPERTY COMPILE_DEFINITIONS "main=flutter_msix_utf8_main")\n'
        f'    target_sources(makemsix PRIVATE {json.dumps(str(entry))})\n'
        '  endif()\nendfunction()\n'
        'cmake_language(DEFER CALL flutter_msix_utf8_entry)\n')
    # Keep exceptions, RTTI decisions and LTO in the upstream sources/settings.
    # The adapter translates MSVC options into the shared compiler's GNU ABI.
    toolchain.write_text(
        'set(CMAKE_SYSTEM_NAME Windows)\nset(CMAKE_SYSTEM_PROCESSOR AMD64)\n'
        f'set(CMAKE_USER_MAKE_RULES_OVERRIDE {json.dumps(str(naming))})\n'
        f'set(CMAKE_PROJECT_INCLUDE {json.dumps(str(entry_config))})\n'
        f'set(CMAKE_C_COMPILER {json.dumps(sys.executable)})\n'
        f'set(CMAKE_C_COMPILER_ARG1 {json.dumps(str(wrappers["c"]))})\n'
        f'set(CMAKE_CXX_COMPILER {json.dumps(sys.executable)})\n'
        f'set(CMAKE_CXX_COMPILER_ARG1 {json.dumps(str(wrappers["cxx"]))})\n'
        f'set(CMAKE_AR {json.dumps(str(llvm / "llvm-ar"))})\n'
        f'set(CMAKE_RANLIB {json.dumps(str(llvm / "llvm-ranlib"))})\n')
    settings = ['-DWIN32=ON', '-DMSIX_PACK=ON', '-DSKIP_BUNDLES=OFF',
                '-DXML_PARSER=' + args.xml_parser, '-DCRYPTO_LIB=crypt32', '-DUSE_VALIDATION_PARSER=ON',
                '-DUSE_MSIX_SDK_ZLIB=ON', '-DUSE_SHARED_ZLIB=OFF',
                '-DCMAKE_SHARED_LINKER_FLAGS=' + shlex.quote(str(guid_library)),
                '-DCMAKE_EXE_LINKER_FLAGS=' + shlex.quote(str(guid_library)),
                '-DCMAKE_C_FLAGS=-DWIN32 -DUNICODE -D_UNICODE',
                # MinGW groups COM macros under STDMETHODCALLTYPE's guard.
                # Establish all Windows types/macros before MSIX defines it.
                '-DCMAKE_CXX_FLAGS=-DWIN32 -DUNICODE -D_UNICODE -include windows.h -include wrl/client.h -fexceptions -isystem '
                + shlex.quote(str(headers)) + ' -include '
                + shlex.quote(str(drivers / 'msvc_utf16_stl_compat.h')) + ' -include '
                + shlex.quote(str(drivers / 'windows_msix_utf8_file_compat.h'))]
    directory = output / 'build'
    subprocess.run(['cmake', '-G', 'Ninja', '-S', str(source), '-B', str(directory),
                    '-DCMAKE_TOOLCHAIN_FILE=' + str(toolchain), '-DCMAKE_BUILD_TYPE=Release', *settings],
                   env=environment, check=True)
    cache = {}
    for line in (directory / 'CMakeCache.txt').read_text().splitlines():
        if ':' in line and '=' in line and not line.startswith(('#', '//')):
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    required = {'WIN32': 'ON', 'MSIX_PACK': 'ON', 'SKIP_BUNDLES': 'OFF',
                'XML_PARSER': args.xml_parser, 'CRYPTO_LIB': 'crypt32',
                'USE_VALIDATION_PARSER': 'ON', 'USE_MSIX_SDK_ZLIB': 'ON',
                'USE_SHARED_ZLIB': 'OFF'}
    if any(cache.get(key) != value for key, value in required.items()):
        raise ValueError('Upstream MSIX capability configuration differs')
    if args.configure_only:
        return {'configured': True, 'sourceBuilt': False}
    subprocess.run(['cmake', '--build', str(directory), '--target', 'makemsix',
                    '--parallel', str(args.jobs)], env=environment, check=True)
    if sha(configuration) != identity:
        raise ValueError('Compiler configuration changed during the MSIX build')
    changes = source_changes()
    distribution = output / 'distribution'
    if distribution.exists():
        raise ValueError('MSIX staging directory already exists')
    (distribution / 'bin').mkdir(parents=True)
    host = select_host('windows-x64')
    libraries = [name for name in ('msix.dll', 'libmsix.dll')
                 if (directory / 'bin' / name).is_file()]
    if len(libraries) != 1:
        raise ValueError('Expected one unambiguous private MSIX runtime DLL')
    names = ['makemsix.exe', *libraries]
    for name in names:
        path = directory / 'bin' / name
        verify_native_host(path, host)
        shutil.copy2(path, distribution / 'bin' / name)
    native = {name: audit(distribution / 'bin' / name, set(libraries), llvm / 'llvm-readobj')
              for name in names}
    for name in ('LICENSE', 'THIRD PARTY CODE NOTICE'):
        shutil.copy2(source / name, distribution / name)
    report = {'host': host['id'], 'sourceRevision': REVISION, 'sourceBuilt': True,
              'cmakeFeatureSettings': settings, 'nativeDependencies': native,
              'upstreamBuildGeneratedSourceChanges': changes, 'systemLibrariesCopied': False,
              'runtimeExecutionVerified': False,
              'sdkPackageSha256': SDK_SHA256,
              'sdkX64LibraryPackageSha256': SDK_X64_SHA256,
              'sdkGuidLibrarySha256': sha(guid_library),
              'sdkGuidLibraryCopiedToDistribution': False,
              'privateWindowsLibraryNamingSha256': sha(naming),
              'privateUtf8CliEntrySha256': sha(entry),
              'privateUtf8CliEntryConfigurationSha256': sha(entry_config),
              'privateUtf8FileCompatibilitySha256': sha(drivers / 'windows_msix_utf8_file_compat.h'),
              'sdkDeclarations': {name: sha(headers / name)
                                  for name in ('AppxPackaging.h', 'delayimp.h')},
              'gnuDelayimpSourceSha256': sha(delayimp),
              'files': {str(path.relative_to(distribution)): sha(path)
                        for path in distribution.rglob('*') if path.is_file()}}
    (distribution / 'msix-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xml-parser', choices=('msxml6', 'xerces'), default='msxml6',
                        help='Upstream XML backend; full schema validation stays enabled')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--toolchain-config', type=Path, required=True)
    parser.add_argument('--sdk-package', type=Path, required=True)
    parser.add_argument('--source-repository', default=REPOSITORY)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--configure-only', action='store_true')
    print(json.dumps(build(parser.parse_args()), indent=2))
