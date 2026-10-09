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
from sdk_hosts import current_host, select_host, verify_native_host, artifact_host_cpu
from install_engine_cache import install

sys.path.insert(0, str(Path(__file__).resolve().parent / 'toolchain'))
from prepare_cmake import prepare as prepare_cmake
from prepare_phone import prepare as prepare_phone
from prepare_python import verify_cross_python


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
    host = select_host(args.host) if getattr(args, 'host', None) else current_host()
    cross_prepare = getattr(args, 'cross_prepare', False)
    if cross_prepare and (host['os'] != 'macos' or host['id'] == current_host()['id']):
        raise ValueError('Cross assembly is for a macOS SDK distinct from the executing host')
    if args.host_cpu != host['cpu']:
        raise ValueError('Destination SDK host and selected host CPU differ')
    extension = '.exe' if host['os'] == 'windows' else ''
    launcher_input = getattr(args, 'windows_launcher', None)
    if extension and launcher_input is None:
        raise ValueError('Windows SDK assembly requires the source-built native Python launcher')
    windows_launcher = contained(sdk, launcher_input) if extension else None
    for relative in ('bin/flutter', 'bin/cache/dart-sdk/bin/dart' + extension,
                     'dev/windows_arm32/build_uwp_host.py'):
        contained(sdk, relative)
    for tool in ('dart', 'dartaotruntime'):
        verify_native_host(contained(sdk, 'bin/cache/dart-sdk/bin/' + tool + extension), host)
    native = contained(sdk, args.toolchain_config)
    native_keys = ('compiler', 'llvmBin', 'sysroot', 'resourceDir')
    if 'runtimeResourceDir' in json.loads(native.read_text()):
        native_keys += ('runtimeResourceDir',)
    configuration_paths(sdk, native, native_keys,
                        ('includeDirs', 'systemIncludeDirs', 'libraryDirs',
                         'rtLibraryDirs', 'phoneLibraryDirs'))
    cargo_build = contained(sdk, args.cargo_build_configuration)
    cargo_keys = ('dart', 'pubCache', 'rustupHome', 'cargoConfiguration')
    if 'rustupExecutable' in json.loads(cargo_build.read_text()):
        cargo_keys += ('rustupExecutable',)
    cargo = configuration_paths(sdk, cargo_build,
                                cargo_keys)
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
    for executable in (python, packager, llvm / ('llvm-objdump' + extension), llvm / ('llvm-mc' + extension),
                       llvm / ('llvm-objcopy' + extension), llvm / ('llvm-readelf' + extension)):
        if not os.access(executable, os.X_OK):
            raise ValueError('SDK tool is not executable: ' + str(executable))
        verify_native_host(executable, host)
    requirements = Path(__file__).with_name('requirements-phone-packaging.txt')
    pillow_version = requirements.read_text().strip().split('==')[1]
    scratch = sdk / '.windows-arm32-cache/tmp'
    scratch.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, CI='true', TMPDIR=str(scratch), TMP=str(scratch),
                       TEMP=str(scratch), XDG_CONFIG_HOME=str(scratch.parent / 'config'))
    if cross_prepare:
        verify_cross_python(python.resolve().parent.parent, host, pillow_version)
    else:
        subprocess.run([str(python), '-c',
                        'import PIL,sys; sys.exit(0 if PIL.__version__ == sys.argv[1] else 1)',
                        pillow_version], env=environment, check=True)
    # Audit every input before installing either runtime family.
    engine_report = json.loads((args.engine_build / 'engine-build-provenance.json').read_text())
    if not engine_report['freshBuildInvocation']:
        raise ValueError('SDK assembly requires source-build provenance')
    engine_cpu = artifact_host_cpu(engine_report)
    for family in ('rt', 'phone'):
        actual = artifacts(args.engine_build.resolve() / 'out' / ('win_release_arm_' + family),
                           family, engine_cpu)
        expected = engine_report['families'][family]['artifacts']
        if any(expected.get(name) != record for name, record in actual.items()):
            raise ValueError('Engine output differs from its source-build record: ' + family)
    for file in (args.frontend_server, args.patched_sdk / 'platform_strong.dill',
                 args.patched_sdk / 'vm_outline_strong.dill'):
        if not file.is_file() or not file.stat().st_size:
            raise ValueError('Missing compiler build output: ' + str(file))
    frontend_report = json.loads(args.frontend_server.with_name('frontend-source-provenance.json').read_text())
    if (not frontend_report['compiledFromCurrentSource'] or
            frontend_report['snapshotSha256'] != digest(args.frontend_server) or
            frontend_report['sourceManifestSha256'] != engine_report['sourceManifests']['dart-source-manifest.json']):
        raise ValueError('Frontend differs from its source-build record')
    if frontend_report.get('sdkHost') is not None:
        if frontend_report['sdkHost'] != host['id']:
            raise ValueError('Frontend was generated for a different SDK host')
    elif frontend_report.get('hostBootstrapDartSha256') != digest(
            sdk / ('bin/cache/dart-sdk/bin/dart' + extension)):
        # Existing native build records identify the host by the exact official
        # bootstrap binary. Cross builds explicitly record the destination.
        raise ValueError('Native frontend bootstrap differs from the SDK host Dart')
    for family in ('rt', 'phone'):
        expected = engine_report['families'][family]['artifacts']
        for name in ('platform_strong.dill', 'vm_outline_strong.dill'):
            record = expected['flutter_patched_sdk/' + name]
            if record['sha256'] != digest(args.patched_sdk / name):
                raise ValueError('Compiler platform differs between source builds: ' + name)
    output = sdk / 'bin/cache/artifacts/engine'
    release = install(output, args.engine_build, llvm, args.frontend_server,
                      args.patched_sdk, args.host_cpu, require_contained_tools=True, python=python,
                      windows_launcher=windows_launcher,
                      host_generator=getattr(args, 'host_generator', None),
                      host_generator_provenance=getattr(args, 'host_generator_provenance', None))
    toolchain = prepare_cmake(release / 'toolchain', native, cppwinrt,
                             cargo_build, sdk_root=sdk, python=python, windows_launcher=windows_launcher)
    prepare_phone(toolchain, toolchain / 'toolchain.json', release / 'phone',
                  python, packager, args.packager_kind, {}, toolchain, cppwinrt,
                  cross_prepare_host=host['id'] if cross_prepare else None)
    files = {str(path.relative_to(sdk)): digest(path) for path in toolchain.rglob('*')
             if path.is_file()}
    report = {'configurationFiles': files, 'bothRuntimeFamiliesInstalled': True,
              'sdkDependencyPathsPreflightPassed': True,
              'applicationBuildVerified': False, 'relocatedSdkBuildVerified': False,
              'releaseReady': False}
    report['phoneImageDependencyExecutionVerified'] = not cross_prepare
    report['phoneImageDependencyMetadataVerified'] = cross_prepare
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
    parser.add_argument('--host', help='Destination SDK host, independently of the assembly machine')
    parser.add_argument('--cross-prepare', action='store_true',
                        help='Audit pinned macOS Python/Pillow files; require destination execution separately')
    parser.add_argument('--packager-kind', choices=('makeappx', 'makemsix'))
    parser.add_argument('--windows-launcher', type=Path)
    parser.add_argument('--host-generator', type=Path,
                        help='Source-built ARM32 generator for the destination SDK host')
    parser.add_argument('--host-generator-provenance', type=Path,
                        help='Matching source-build record for --host-generator')
    parser.add_argument('--dependency-config', type=Path,
                        help='SDK-contained settings produced by configure_sdk_dependencies.py')
    args = parser.parse_args()
    if args.dependency_config is not None:
        config_path = contained(args.sdk.resolve(), args.dependency_config)
        values = json.loads(config_path.read_text())
        configured_host = select_host(values['hostOs'] + '-' + values['hostCpu'])
        if args.host is not None and args.host != configured_host['id']:
            parser.error('Destination SDK host differs from --dependency-config')
        args.host = configured_host['id']
        for name, key in (('llvm_bin', 'llvmBin'), ('toolchain_config', 'toolchainConfig'),
                          ('cppwinrt_include', 'cppwinrtInclude'),
                          ('cargo_build_configuration', 'cargoBuildConfiguration'),
                          ('python', 'python'), ('packager', 'packager'), ('windows_launcher', 'windowsLauncher')):
            if getattr(args, name) is None and key in values:
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
