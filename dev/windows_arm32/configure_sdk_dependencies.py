#!/usr/bin/env python3
"""Connect prepared SDK-contained host tools to the RT and Phone build flow."""
import argparse
import json
import os
from pathlib import Path
import sys

from assemble_sdk import contained
from sdk_hosts import current_host, select_host, verify_native_host

sys.path.insert(0, str(Path(__file__).resolve().parent / 'toolchain'))
from package_compiler import TOOLS, sha
from prepare_cargokit_rustup import prepare as prepare_rustup
from prepare_rust import prepare as prepare_targets
from prepare_rust_sysroot import prepare as prepare_sysroot
from windows_launchers import write_launcher


def configure(args):
    sdk = args.sdk.resolve()
    host = select_host(args.host) if getattr(args, 'host', None) else current_host()
    linux = host['os'] == 'linux'
    windows_host = host['os'] == 'windows'
    extension = '.exe' if windows_host else ''
    def executable(directory, name):
        return directory / 'bin' / (name + extension)
    output = Path(os.path.abspath(args.output))
    if not output.is_relative_to(sdk) or not output.parent.resolve().is_relative_to(sdk):
        raise ValueError('SDK dependency configuration output must be inside the SDK')
    if output.exists() or output.is_symlink():
        raise ValueError('SDK dependency configuration output already exists')
    compiler = contained(sdk, args.compiler)
    gnu = contained(sdk, args.gnu_sysroot)
    windows = contained(sdk, args.windows_sdk)
    atl = contained(sdk, args.atl)
    cppwinrt = contained(sdk, args.cppwinrt)
    python = contained(sdk, args.python, preserve_symlink=True)
    packager = contained(sdk, args.packager)
    launcher_input = getattr(args, 'windows_launcher', None)
    if windows_host and launcher_input is None:
        raise ValueError('Windows SDK assembly requires the source-built native Python launcher')
    windows_launcher = contained(sdk, launcher_input) if windows_host else None
    if windows_host:
        font_tool = contained(sdk, sdk / 'bin/cache/artifacts/engine/windows-x64/font-subset.exe')
        raw_font_tool = font_tool.with_name('font-subset-raw.exe')
        if raw_font_tool.exists():
            raise ValueError('Windows font path adapter is already installed')
        original_font_digest = sha(font_tool)
        font_tool.rename(raw_font_tool)
        write_launcher(font_tool, windows_launcher, python,
                       Path(__file__).parent / 'toolchain/font_subset_paths.py',
                       arguments=(raw_font_tool,))
        font_tool.with_name('font-subset-paths.json').write_text(json.dumps({
            'originalBinarySha256': original_font_digest,
            'originalBinaryUnmodified': sha(raw_font_tool) == original_font_digest,
            'unicodePathAdapter': True, 'fontSubsettingEnabled': True,
        }, indent=2) + '\n')
    python_root = python.resolve().parent if windows_host else python.resolve().parent.parent
    dependencies = contained(sdk, args.rust_dependencies)
    distributions = {name: contained(sdk, directory) for name, directory in (
        ('stable', args.rust_stable), ('1.93.1', args.rust_193))}
    references = {'stable': getattr(args, 'reference_rust_stable', None),
                  '1.93.1': getattr(args, 'reference_rust_193', None)}
    if any(references.values()) and (not all(references.values()) or host['os'] != 'macos'):
        raise ValueError('macOS cross preparation requires both source-matched Rust references')
    for directory in (compiler, gnu, windows, atl, cppwinrt, dependencies,
                      *distributions.values(), python_root):
        for path in directory.rglob('*'):
            if path.is_symlink() and not path.resolve().is_relative_to(sdk):
                raise ValueError('External prepared SDK dependency link: ' + str(path))
    # Validate distributions before generating any configuration or std sources.
    distribution = json.loads((compiler / 'distribution-manifest.json').read_text())
    source = json.loads((compiler / 'source-provenance.json').read_text())
    if not source.get('sourceBuilt') or source['compilerSha256'] != sha(executable(compiler, 'clang')):
        raise ValueError('SDK compiler does not match its source-build record')
    for name, record in distribution['files'].items():
        original = compiler / name
        path = contained(sdk, original)
        if 'symlink' in record and (not original.is_symlink() or os.readlink(original) != record['symlink']):
            raise ValueError('SDK compiler distribution link changed: ' + name)
        if 'sha256' in record and sha(path) != record['sha256']:
            raise ValueError('SDK compiler distribution file changed: ' + name)
    for name in TOOLS:
        contained(sdk, executable(compiler, name))
    resources = list((compiler / 'lib/clang').glob('*/include/stddef.h'))
    if len(resources) != 1:
        raise ValueError('Select one complete compiler resource directory')
    for name, version in (('stable', '1.95.0'), ('1.93.1', '1.93.1')):
        directory = distributions[name]
        record = json.loads((directory / 'rust-distribution-provenance.json').read_text())
        expected_host = host['rustHost']
        if record['rustVersion'] != version or record['host'] != expected_host:
            raise ValueError('Unexpected Rust SDK distribution: ' + name)
        for relative, expected in record['files'].items():
            if sha(contained(sdk, directory / relative)) != expected:
                raise ValueError('Rust SDK distribution changed: ' + relative)
    python_record = json.loads((python_root / 'python-provenance.json').read_text())
    for relative, expected in python_record['files'].items():
        if sha(contained(sdk, python_root / relative)) != expected:
            raise ValueError('Python SDK distribution changed: ' + relative)
    for path in (windows / 'include/windows.h', atl / 'include/atlbase.h',
                 atl / 'lib/libatls.a', cppwinrt / 'winrt/base.h',
                 sdk / ('bin/cache/dart-sdk/bin/dart' + extension),
                 dependencies / '.cargo/sdk-patches.toml'):
        contained(sdk, path)
    for dependency_executable in (python, packager, executable(compiler, 'clang'),
                       *(executable(directory, 'cargo') for directory in distributions.values())):
        if not os.access(dependency_executable, os.X_OK):
            raise ValueError('SDK build dependency is not executable: ' + str(dependency_executable))
        verify_native_host(dependency_executable, host)
    output.mkdir(parents=True)
    def relative(path):
        return os.path.relpath(path, output)
    def write(name, values):
        path = output / name
        path.write_text(json.dumps(values, indent=2) + '\n')
        return path
    runtimes = list(gnu.glob('lib/clang/*/lib/windows/libclang_rt.builtins-arm.a'))
    if len(runtimes) != 1:
        raise ValueError('Expected one Windows ARM compiler runtime in the GNU SDK')
    contained(sdk, runtimes[0])
    native = write('native-toolchain.json', {
        'compiler': relative(executable(compiler, 'clang')), 'llvmBin': relative(compiler / 'bin'),
        'sysroot': relative(gnu), 'resourceDir': relative(resources[0].parent.parent),
        'runtimeResourceDir': relative(runtimes[0].parents[2]),
        'systemIncludeDirs': [relative(windows / 'include'), relative(atl / 'include')],
        'rtLibraryDirs': [relative(atl / 'lib')],
    })
    toolchains = {}
    targets = {}
    for name, directory in distributions.items():
        version = '1.95.0' if name == 'stable' else name
        sysroot = output / ('rust-sysroot-' + version)
        # Both the std source and the relative host-library link stay in SDK.
        prepare_sysroot(executable(directory, 'rustc'), sysroot, copy_host_stdlib=windows_host,
                        cross_prepare=references[name] is not None)
        target_directory = output / ('rust-targets-' + version)
        prepare_targets(target_directory, executable(directory, 'rustc'), native,
                        windows_launcher=windows_launcher, python=python if windows_host else None,
                        reference_rustc=references[name])
        selected_targets = {}
        for family in ('rt', 'phone'):
            target = 'thumbv7a-' + family + '-windows-msvc'
            suffix = target.replace('-', '_')
            selected_targets[target] = {
                'spec': relative(target_directory / (target + '.json')),
                'environmentPaths': {
                    'CC_' + suffix: relative(target_directory / ('clang-cc-' + family + extension)),
                    'AR_' + suffix: relative(executable(compiler, 'llvm-ar')),
                },
                'environmentValues': {
                    'CFLAGS_' + suffix: '-mthumb -march=armv7-a -mfpu=neon -mimplicit-it=always',
                },
            }
        toolchains[name] = {'cargo': relative(executable(directory, 'cargo')),
                            'rustc': relative(executable(directory, 'rustc')),
                            'sysroot': relative(sysroot), 'targets': selected_targets}
        if windows_host:
            toolchains[name]['hostLinker'] = relative(target_directory / 'rust-host-link.exe')
        if name == 'stable':
            targets = selected_targets
    mutable = sdk / '.windows-arm32-cache'
    for name in ('cargo', 'pub', 'tmp'):
        (mutable / name).mkdir(parents=True, exist_ok=True)
    rustc_wrapper = sdk / 'dev/windows_arm32/toolchain/rustc_sysroot.py'
    if windows_host:
        rustc_wrapper = write_launcher(output / 'rustc-wrapper.exe', windows_launcher, python, rustc_wrapper)
    dispatch = write('cargo-toolchains.json', {
        'toolchains': toolchains, 'targets': targets,
        'cargoHome': relative(mutable / 'cargo'), 'temporaryDirectory': relative(mutable / 'tmp'),
        'rustcWrapper': relative(rustc_wrapper),
        'cargoConfigs': [relative(dependencies / '.cargo/sdk-patches.toml')],
    })
    driver = contained(sdk, sdk / 'dev/windows_arm32/toolchain/cargo_toolchain.py')
    rustup = prepare_rustup(dispatch, output / 'rustup', driver=driver,
                            windows_launcher=windows_launcher, python=python if windows_host else None,
                            cross_prepare=any(references.values()))
    backend = write('cargo-build.json', {
        'dart': relative(sdk / ('bin/cache/dart-sdk/bin/dart' + extension)),
        'pubCache': relative(mutable / 'pub'), 'rustupHome': relative(rustup),
        'cargoConfiguration': relative(dispatch),
        **({'rustupExecutable': relative(contained(sdk, executable(args.rust_stable, 'rustup')))}
           if linux or windows_host or any(references.values()) else {}),
    })
    for path in output.rglob('*'):
        if path.is_symlink() and not path.resolve().is_relative_to(sdk):
            raise ValueError('External generated SDK dependency link: ' + str(path))
    return write('sdk-dependencies.json', {
        'toolchainConfig': relative(native), 'cargoBuildConfiguration': relative(backend),
        'llvmBin': relative(compiler / 'bin'), 'cppwinrtInclude': relative(cppwinrt),
        'python': relative(python), 'packager': relative(packager),
        'hostOs': host['os'], 'hostCpu': host['cpu'], 'packagerKind': args.packager_kind,
        **({'windowsLauncher': relative(windows_launcher)} if windows_host else {}),
        'sourceBuiltCompilerVerified': True, 'applicationBuildVerified': False,
        'releaseReady': False,
    })


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'output', 'compiler', 'gnu-sysroot', 'windows-sdk', 'atl',
                 'cppwinrt', 'python', 'packager', 'rust-dependencies', 'rust-stable', 'rust-193'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--windows-launcher', type=Path)
    parser.add_argument('--host', help='Destination SDK host, independently of the preparation machine')
    parser.add_argument('--reference-rust-stable', type=Path,
                        help='Preparation-only native Rust 1.95.0 reference for a macOS SDK')
    parser.add_argument('--reference-rust-193', type=Path,
                        help='Preparation-only native Rust 1.93.1 reference for a macOS SDK')
    parser.add_argument('--packager-kind', choices=('makemsix', 'makeappx'), default='makemsix')
    print(configure(parser.parse_args()))
