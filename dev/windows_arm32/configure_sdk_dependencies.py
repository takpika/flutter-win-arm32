#!/usr/bin/env python3
"""Connect prepared SDK-contained host tools to the RT and Phone build flow."""
import argparse
import json
import os
import platform
from pathlib import Path
import sys

from assemble_sdk import contained

sys.path.insert(0, str(Path(__file__).resolve().parent / 'toolchain'))
from package_compiler import TOOLS, sha
from prepare_cargokit_rustup import prepare as prepare_rustup
from prepare_rust import prepare as prepare_targets
from prepare_rust_sysroot import prepare as prepare_sysroot


def configure(args):
    sdk = args.sdk.resolve()
    linux = platform.system() == 'Linux'
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
    dependencies = contained(sdk, args.rust_dependencies)
    distributions = {name: contained(sdk, directory) for name, directory in (
        ('stable', args.rust_stable), ('1.93.1', args.rust_193))}
    for directory in (compiler, gnu, windows, atl, cppwinrt, dependencies,
                      *distributions.values(), python.resolve().parent.parent):
        for path in directory.rglob('*'):
            if path.is_symlink() and not path.resolve().is_relative_to(sdk):
                raise ValueError('External prepared SDK dependency link: ' + str(path))
    # Validate distributions before generating any configuration or std sources.
    distribution = json.loads((compiler / 'distribution-manifest.json').read_text())
    source = json.loads((compiler / 'source-provenance.json').read_text())
    if not source.get('sourceBuilt') or source['compilerSha256'] != sha(compiler / 'bin/clang'):
        raise ValueError('SDK compiler does not match its source-build record')
    for name, record in distribution['files'].items():
        original = compiler / name
        path = contained(sdk, original)
        if 'symlink' in record and (not original.is_symlink() or os.readlink(original) != record['symlink']):
            raise ValueError('SDK compiler distribution link changed: ' + name)
        if 'sha256' in record and sha(path) != record['sha256']:
            raise ValueError('SDK compiler distribution file changed: ' + name)
    for name in TOOLS:
        contained(sdk, compiler / 'bin' / name)
    resources = list((compiler / 'lib/clang').glob('*/include/stddef.h'))
    if len(resources) != 1:
        raise ValueError('Select one complete compiler resource directory')
    for name, version in (('stable', '1.95.0'), ('1.93.1', '1.93.1')):
        directory = distributions[name]
        record = json.loads((directory / 'rust-distribution-provenance.json').read_text())
        expected_host = 'x86_64-unknown-linux-gnu' if linux else 'aarch64-apple-darwin'
        if record['rustVersion'] != version or record['host'] != expected_host:
            raise ValueError('Unexpected Rust SDK distribution: ' + name)
        for relative, expected in record['files'].items():
            if sha(contained(sdk, directory / relative)) != expected:
                raise ValueError('Rust SDK distribution changed: ' + relative)
    python_root = python.resolve().parent.parent
    python_record = json.loads((python_root / 'python-provenance.json').read_text())
    for relative, expected in python_record['files'].items():
        if sha(contained(sdk, python_root / relative)) != expected:
            raise ValueError('Python SDK distribution changed: ' + relative)
    for path in (windows / 'include/windows.h', atl / 'include/atlbase.h',
                 atl / 'lib/libatls.a', cppwinrt / 'winrt/base.h',
                 sdk / 'bin/cache/dart-sdk/bin/dart',
                 dependencies / '.cargo/sdk-patches.toml'):
        contained(sdk, path)
    for executable in (python, packager, compiler / 'bin/clang',
                       *(directory / 'bin/cargo' for directory in distributions.values())):
        if not os.access(executable, os.X_OK):
            raise ValueError('SDK build dependency is not executable: ' + str(executable))
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
        'compiler': relative(compiler / 'bin/clang'), 'llvmBin': relative(compiler / 'bin'),
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
        prepare_sysroot(directory / 'bin/rustc', sysroot)
        target_directory = output / ('rust-targets-' + version)
        prepare_targets(target_directory, directory / 'bin/rustc', native)
        selected_targets = {}
        for family in ('rt', 'phone'):
            target = 'thumbv7a-' + family + '-windows-msvc'
            suffix = target.replace('-', '_')
            selected_targets[target] = {
                'spec': relative(target_directory / (target + '.json')),
                'environmentPaths': {
                    'CC_' + suffix: relative(target_directory / ('clang-cc-' + family)),
                    'AR_' + suffix: relative(compiler / 'bin/llvm-ar'),
                },
                'environmentValues': {
                    'CFLAGS_' + suffix: '-mthumb -march=armv7-a -mfpu=neon -mimplicit-it=always',
                },
            }
        toolchains[name] = {'cargo': relative(directory / 'bin/cargo'),
                            'rustc': relative(directory / 'bin/rustc'),
                            'sysroot': relative(sysroot), 'targets': selected_targets}
        if name == 'stable':
            targets = selected_targets
    mutable = sdk / '.windows-arm32-cache'
    for name in ('cargo', 'pub', 'tmp'):
        (mutable / name).mkdir(parents=True, exist_ok=True)
    dispatch = write('cargo-toolchains.json', {
        'toolchains': toolchains, 'targets': targets,
        'cargoHome': relative(mutable / 'cargo'), 'temporaryDirectory': relative(mutable / 'tmp'),
        'rustcWrapper': relative(sdk / 'dev/windows_arm32/toolchain/rustc_sysroot.py'),
        'cargoConfigs': [relative(dependencies / '.cargo/sdk-patches.toml')],
    })
    driver = contained(sdk, sdk / 'dev/windows_arm32/toolchain/cargo_toolchain.py')
    rustup = prepare_rustup(dispatch, output / 'rustup', driver=driver)
    backend = write('cargo-build.json', {
        'dart': relative(sdk / 'bin/cache/dart-sdk/bin/dart'),
        'pubCache': relative(mutable / 'pub'), 'rustupHome': relative(rustup),
        'cargoConfiguration': relative(dispatch),
    })
    for path in output.rglob('*'):
        if path.is_symlink() and not path.resolve().is_relative_to(sdk):
            raise ValueError('External generated SDK dependency link: ' + str(path))
    return write('sdk-dependencies.json', {
        'toolchainConfig': relative(native), 'cargoBuildConfiguration': relative(backend),
        'llvmBin': relative(compiler / 'bin'), 'cppwinrtInclude': relative(cppwinrt),
        'python': relative(python), 'packager': relative(packager),
        'hostCpu': 'x64' if linux else 'arm64', 'packagerKind': args.packager_kind,
        'sourceBuiltCompilerVerified': True, 'applicationBuildVerified': False,
        'releaseReady': False,
    })


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'output', 'compiler', 'gnu-sysroot', 'windows-sdk', 'atl',
                 'cppwinrt', 'python', 'packager', 'rust-dependencies', 'rust-stable', 'rust-193'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--packager-kind', choices=('makemsix', 'makeappx'), default='makemsix')
    print(configure(parser.parse_args()))
