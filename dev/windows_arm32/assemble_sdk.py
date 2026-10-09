#!/usr/bin/env python3
"""Install both source-built engine families into a prepared SDK.

Host compiler, Windows headers, Rust, Python and Appx packaging dependencies
must already be staged inside the SDK. This does not download or publish them.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tomllib

from build_engine import artifacts, digest
from install_engine_cache import install

sys.path.insert(0, str(Path(__file__).resolve().parent / 'toolchain'))
from prepare_cmake import prepare as prepare_cmake
from prepare_phone import prepare as prepare_phone


def contained(sdk, value, base=None, *, preserve_symlink=False):
    path = Path(value)
    if not path.is_absolute():
        path = (base or sdk) / path
    resolved = path.resolve()
    if not resolved.is_relative_to(sdk) or not resolved.exists():
        raise ValueError('Missing or external SDK dependency: ' + str(path))
    if preserve_symlink:
        path = Path(os.path.abspath(path))
        if not path.is_relative_to(sdk) or not path.parent.resolve().is_relative_to(sdk):
            raise ValueError('External SDK interpreter environment: ' + str(path))
        return path
    return resolved


def configuration_paths(sdk, file, scalar, arrays=()):
    values = json.loads(file.read_text())
    for key in scalar:
        contained(sdk, values[key], file.parent)
    for key in arrays:
        for value in values.get(key, []):
            contained(sdk, value, file.parent)
    return values


def assemble(args):
    sdk = args.sdk.resolve()
    for relative in ('bin/flutter', 'bin/cache/dart-sdk/bin/dart',
                     'dev/windows_arm32/build_uwp_host.py'):
        contained(sdk, relative)
    native = contained(sdk, args.toolchain_config)
    native_keys = ('compiler', 'llvmBin', 'sysroot', 'resourceDir')
    if 'runtimeResourceDir' in json.loads(native.read_text()):
        native_keys += ('runtimeResourceDir',)
    configuration_paths(sdk, native, native_keys,
                        ('includeDirs', 'systemIncludeDirs', 'libraryDirs',
                         'rtLibraryDirs', 'phoneLibraryDirs'))
    cargo_build = contained(sdk, args.cargo_build_configuration)
    cargo = configuration_paths(sdk, cargo_build,
                                ('dart', 'pubCache', 'rustupHome', 'cargoConfiguration'))
    dispatch = contained(sdk, cargo['cargoConfiguration'], cargo_build.parent)
    rust = configuration_paths(sdk, dispatch,
                               ('cargoHome', 'temporaryDirectory', 'rustcWrapper'),
                               ('cargoConfigs',))
    # Nested Cargo settings are used in place, so their stored paths must also
    # survive relocation. CMake/Phone settings are rebased by their preparers.
    def relative_reference(value):
        if Path(value).is_absolute():
            raise ValueError('Cargo distribution settings require relative paths: ' + value)
        return contained(sdk, value, dispatch.parent)
    for key in ('cargoHome', 'temporaryDirectory', 'rustcWrapper'):
        relative_reference(rust[key])
    for value in rust.get('cargoConfigs', []):
        patch = relative_reference(value)
        for registry in tomllib.loads(patch.read_text()).get('patch', {}).values():
            for entry in registry.values():
                if isinstance(entry, dict) and 'path' in entry:
                    if Path(entry['path']).is_absolute():
                        raise ValueError('Distribution Cargo patches require relative paths')
                    dependency = contained(sdk, entry['path'], patch.parent.parent)
                    contained(sdk, dependency / 'Cargo.toml')
    for selected in rust['toolchains'].values():
        for key in ('cargo', 'rustc', 'sysroot'):
            relative_reference(selected[key])
        for target in selected.get('targets', rust['targets']).values():
            spec = relative_reference(target['spec'])
            if Path(json.loads(spec.read_text())['linker']).is_absolute():
                raise ValueError('Rust target contains an absolute linker path: ' + str(spec))
            for value in target.get('environmentPaths', {}).values():
                relative_reference(value)
    cppwinrt = contained(sdk, args.cppwinrt_include)
    contained(sdk, cppwinrt / 'winrt/base.h')
    # Launch through the venv path after verifying its interpreter stays in SDK.
    python = contained(sdk, args.python, preserve_symlink=True)
    packager = contained(sdk, args.packager)
    llvm = contained(sdk, args.llvm_bin)
    for executable in (python, packager, llvm / 'llvm-objdump', llvm / 'llvm-mc',
                       llvm / 'llvm-objcopy', llvm / 'llvm-readelf'):
        if not os.access(executable, os.X_OK):
            raise ValueError('SDK tool is not executable: ' + str(executable))
    requirements = Path(__file__).with_name('requirements-phone-packaging.txt')
    pillow_version = requirements.read_text().strip().split('==')[1]
    scratch = sdk / '.windows-arm32-cache/tmp'
    scratch.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, CI='true', TMPDIR=str(scratch), TMP=str(scratch),
                       TEMP=str(scratch), XDG_CONFIG_HOME=str(scratch.parent / 'config'))
    subprocess.run([str(python), '-c',
                    'import PIL,sys; sys.exit(0 if PIL.__version__ == sys.argv[1] else 1)',
                    pillow_version], env=environment, check=True)
    # Audit every input before installing either runtime family.
    engine_report = json.loads((args.engine_build / 'engine-build-provenance.json').read_text())
    if not engine_report['freshBuildInvocation']:
        raise ValueError('SDK assembly requires source-build provenance')
    for family in ('rt', 'phone'):
        actual = artifacts(args.engine_build.resolve() / 'out' / ('win_release_arm_' + family),
                           family, args.host_cpu)
        expected = engine_report['families'][family]['artifacts']
        if any(expected.get(name) != record for name, record in actual.items()):
            raise ValueError('Engine output differs from its source-build record: ' + family)
    for file in (args.frontend_server, args.patched_sdk / 'platform_strong.dill',
                 args.patched_sdk / 'vm_outline_strong.dill'):
        if not file.is_file() or not file.stat().st_size:
            raise ValueError('Missing compiler build output: ' + str(file))
    frontend_report = json.loads(args.frontend_server.with_name('frontend-source-provenance.json').read_text())
    if not frontend_report['compiledFromCurrentSource'] or frontend_report['snapshotSha256'] != digest(args.frontend_server):
        raise ValueError('Frontend differs from its source-build record')
    for family in ('rt', 'phone'):
        expected = engine_report['families'][family]['artifacts']
        for name in ('platform_strong.dill', 'vm_outline_strong.dill'):
            record = expected['flutter_patched_sdk/' + name]
            if record['sha256'] != digest(args.patched_sdk / name):
                raise ValueError('Compiler platform differs between source builds: ' + name)
    output = sdk / 'bin/cache/artifacts/engine'
    release = install(output, args.engine_build, llvm, args.frontend_server,
                      args.patched_sdk, args.host_cpu, require_contained_tools=True, python=python)
    toolchain = prepare_cmake(release / 'toolchain', native, cppwinrt,
                             cargo_build, sdk_root=sdk, python=python)
    prepare_phone(toolchain, toolchain / 'toolchain.json', release / 'phone',
                  python, packager, args.packager_kind, {}, toolchain, cppwinrt)
    files = {str(path.relative_to(sdk)): digest(path) for path in toolchain.rglob('*')
             if path.is_file()}
    report = {'configurationFiles': files, 'bothRuntimeFamiliesInstalled': True,
              'sdkDependencyPathsPreflightPassed': True,
              'applicationBuildVerified': False, 'relocatedSdkBuildVerified': False,
              'releaseReady': False}
    destination = release / 'sdk-assembly.json'
    destination.write_text(json.dumps(report, indent=2) + '\n')
    return destination


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'engine-build', 'frontend-server', 'patched-sdk'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('llvm-bin', 'toolchain-config', 'cppwinrt-include',
                 'cargo-build-configuration', 'python', 'packager'):
        parser.add_argument('--' + name, type=Path)
    parser.add_argument('--host-cpu', choices=('arm64', 'x64'))
    parser.add_argument('--packager-kind', choices=('makeappx', 'makemsix'))
    parser.add_argument('--dependency-config', type=Path,
                        help='SDK-contained settings produced by configure_sdk_dependencies.py')
    args = parser.parse_args()
    if args.dependency_config is not None:
        config_path = contained(args.sdk.resolve(), args.dependency_config)
        values = json.loads(config_path.read_text())
        for name, key in (('llvm_bin', 'llvmBin'), ('toolchain_config', 'toolchainConfig'),
                          ('cppwinrt_include', 'cppwinrtInclude'),
                          ('cargo_build_configuration', 'cargoBuildConfiguration'),
                          ('python', 'python'), ('packager', 'packager')):
            if getattr(args, name) is None:
                value = Path(values[key])
                setattr(args, name, value if value.is_absolute() else config_path.parent / value)
        if args.host_cpu is None:
            args.host_cpu = values['hostCpu']
        if args.packager_kind is None:
            args.packager_kind = values['packagerKind']
    for name in ('llvm_bin', 'toolchain_config', 'cppwinrt_include',
                 'cargo_build_configuration', 'python', 'packager', 'host_cpu', 'packager_kind'):
        if getattr(args, name) is None:
            parser.error('Supply --' + name.replace('_', '-') + ' or --dependency-config')
    if args.host_cpu not in ('arm64', 'x64') or args.packager_kind not in ('makeappx', 'makemsix'):
        parser.error('Unsupported host CPU or packager kind in dependency configuration')
    print(assemble(args))
