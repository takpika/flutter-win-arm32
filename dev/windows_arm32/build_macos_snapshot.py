#!/usr/bin/env python3
"""Build a native macOS ARM32 Windows AOT generator in a private GN overlay."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import sys

from prepare_sources import prepare_sources
from sdk_hosts import current_host, select_host, verify_native_host


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_bitcode_toolchain(tools, driver, output, environment, deployment_target=None):
    """Catch incompatible archive readers before compiling the precompiler."""
    versions = {}
    for name in ('compiler', 'archiver', 'linker'):
        text = subprocess.check_output([str(tools[name]), '--version'],
                                       env=environment, text=True)
        match = re.search(r'\b(?:clang version|LLVM version|LLD)\s+(\d+)\.', text)
        if not match:
            raise ValueError('Cannot identify LLVM tool version: ' + name)
        versions[name] = int(match[1])
    # An older archive reader can silently omit symbols from newer bitcode.
    # A trivial module may pass even when full Dart API modules do not.
    if any(versions[name] < versions['compiler'] for name in ('archiver', 'linker')):
        raise ValueError('Incompatible LLVM bitcode tool versions: ' + str(versions))
    output.mkdir()
    if deployment_target is not None:
        target = re.fullmatch(r'(arm64|x86_64)-apple-macos(\d+)\.(\d+)', deployment_target)
        if target is None:
            raise ValueError('Unsupported macOS deployment target: ' + deployment_target)
        expected = int(target[2]) * 10000 + int(target[3]) * 100
        # Match GN's unversioned triple as well as the configured driver.
        macros = subprocess.check_output(
            [sys.executable, str(driver), 'cxx', '--target=' + target[1] + '-apple-macos',
             '-dM', '-E', '-x', 'c++', '-'], input='', text=True, env=environment)
        if f'#define __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ {expected}' not in macros.splitlines():
            raise ValueError('GN compiler flags override the configured macOS deployment target')
    # Intrinsic declarations must match the compiler's builtins as well. A
    # scalar-only fixture misses incompatible resource headers on another CPU.
    (output / 'value.c').write_text(
        '#if defined(__x86_64__)\n#include <immintrin.h>\n'
        '#elif defined(__aarch64__)\n#include <arm_neon.h>\n#endif\n'
        'int value(int* p) { *p = 42; return *p; }\n')
    (output / 'main.c').write_text(
        'int value(int*); int main(void) { int n = 0; return value(&n) != 42 || n != 42; }\n')
    prefix = [sys.executable, str(driver)]
    commands = [prefix + ['cc', '-O2', '-flto', '-c', name + '.c', '-o', name + '.o']
                for name in ('value', 'main')]
    commands += [prefix + ['ar', 'rcsT', 'value.a', 'value.o'],
                 prefix + ['link', '-flto', '-nostdlib++', 'main.o', 'value.a', '-o', 'check']]
    with (output / 'build.log').open('w') as log:
        try:
            for command in commands:
                subprocess.run(command, cwd=output, env=environment,
                               stdout=log, stderr=log, check=True)
        except subprocess.CalledProcessError as error:
            raise ValueError('Compiler, archiver and linker cannot exchange LLVM bitcode; '
                             'use compatible LLVM tools. See ' + str(output / 'build.log')) from error


def overlay(original, destination, replacements, relative=Path()):
    """Overlay only build files; source and installed tool directories stay intact."""
    target = destination / relative
    target.mkdir(parents=True)
    for entry in (original / relative).iterdir():
        name = relative / entry.name
        selected = destination / name
        if str(name) in replacements:
            selected.write_text(replacements[str(name)])
        elif any(value.startswith(str(name) + '/') for value in replacements):
            overlay(original, destination, replacements, name)
        else:
            selected.symlink_to(entry, target_is_directory=entry.is_dir())


def build(args):
    host = select_host(args.host)
    if host['os'] != 'macos' or args.jobs < 1:
        raise ValueError('Select a macOS host and a positive job count')
    root = Path(__file__).resolve().parents[2]
    original = root / 'engine/src'
    output, sdk, configuration = (path.resolve() for path in
                                  (args.output, args.sdk, args.toolchain_config))
    if output.exists():
        raise ValueError('Use a new isolated snapshot build directory')
    raw = json.loads(configuration.read_text())
    def configured(value):
        path = Path(value)
        return path.resolve() if path.is_absolute() else (configuration.parent / path).resolve()
    compiler, llvm = configured(raw['compiler']), configured(raw['llvmBin'])
    resource = configured(raw['resourceDir']) if 'resourceDir' in raw else Path(
        subprocess.check_output([str(compiler), '-print-resource-dir'], text=True).strip())
    tools = {'compiler': compiler, 'archiver': llvm / 'llvm-ar',
             'linker': llvm / 'ld64.lld', 'objdump': llvm / 'llvm-objdump'}
    for path in tools.values():
        verify_native_host(path, current_host())
    tools_hashes = {path: sha(path) for path in tools.values()}
    for path in (resource / 'include/stdarg.h', sdk / 'usr/include/stdio.h', sdk / 'SDKSettings.json',
                 sdk / 'System/Library/Frameworks/CoreServices.framework/CoreServices.tbd'):
        if not path.is_file():
            raise ValueError('Missing compiler or SDK development declaration: ' + str(path))
    prepare_sources(root)
    cpu = 'arm64' if host['cpu'] == 'arm64' else 'x86_64'
    minimum = '11.0' if host['cpu'] == 'arm64' else '10.15'
    toolchain = {key: str(value) for key, value in tools.items()}
    toolchain.update(sdk=str(sdk), resourceDir=str(resource), target=cpu + '-apple-macos' + minimum)
    temporary = output / 'tmp'
    temporary.mkdir(parents=True)
    environment = dict(os.environ, TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary),
                       PYTHONDONTWRITEBYTECODE='1')
    directory = Path(__file__).resolve().parent
    driver = directory / 'toolchain/macos_snapshot_driver.py'
    fragment = directory / 'toolchain/macos_snapshot.gn'
    host_inputs = {str(path.relative_to(root)): sha(path) for path in
                   (Path(__file__).resolve(), driver, fragment)}
    build_configuration = output / 'toolchain.json'
    build_configuration.write_text(json.dumps(toolchain, indent=2) + '\n')
    identity = sha(build_configuration)
    driver_directory = output / ('toolchain-' + hashlib.sha256(
        (identity + sha(driver) + sha(fragment)).encode()).hexdigest())
    driver_directory.mkdir()
    configured_driver = driver_directory / 'driver.py'
    configured_driver.write_text('import os, runpy\n' +
        f'os.environ["FLUTTER_WINDOWS_ARM32_MACOS_SNAPSHOT_CONFIG"] = {str(build_configuration)!r}\n' +
        f'# toolchain SHA-256: {identity}; driver SHA-256: {sha(driver)}\n' +
        f'runpy.run_path({str(driver)!r}, run_name="__main__")\n')
    check_bitcode_toolchain(tools, configured_driver, temporary / 'bitcode-check', environment,
                           toolchain['target'])
    darwin = original / 'build/config/darwin/darwin_sdk.gni'
    text = darwin.read_text()
    if text.count('assert(host_os == "mac")') != 1:
        raise ValueError('Darwin SDK build configuration changed; review the cross-build overlay')
    source = output / 'sources'
    overlay(original, source, {
        'build/config/darwin/darwin_sdk.gni': text.replace(
            'assert(host_os == "mac")', 'assert(host_os == "mac" || host_os == "linux")'),
        'build/toolchain/custom/BUILD.gn': fragment.read_text(),
    })
    manifests = directory / 'patches'
    inputs = {path.name: sha(path) for path in sorted(manifests.glob('*-source-manifest.json'))}
    gn = runpy.run_path(str(original / 'flutter/tools/gn'))
    options = gn['parse_args'](['gn', '--target-os=win', '--windows-cpu=arm',
        '--runtime-mode=release', '--no-enable-unittests', '--no-rbe', '--ide='])
    settings = gn['to_gn_args'](options)
    settings.update(current_os='mac', current_cpu=host['cpu'], dart_target_arch='arm',
        use_default_linux_sysroot=False, custom_toolchain=str(driver_directory),
        custom_target_triple=toolchain['target'], custom_sysroot=str(sdk),
        mac_sdk_path=str(sdk), mac_platform_path=str(sdk),
        mac_host_toolchain_path=str(llvm.parent), mac_deployment_target=minimum + '.0',
        python_path=sys.executable)
    dotfile = output / 'snapshot.gn'
    dotfile.write_text('buildconfig = "//build/config/BUILDCONFIG.gn"\n' +
        'secondary_source = "//flutter/build/secondary/"\n' +
        f'script_executable = {json.dumps(sys.executable)}\n' +
        'root = "//flutter/third_party/dart/runtime/bin:gen_snapshot"\n')
    build_dir = output / 'build'
    subprocess.run([str(original / 'flutter/third_party/gn/gn'), 'gen', str(build_dir),
        '--root=' + str(source), '--dotfile=' + str(dotfile),
        '--args=' + ' '.join(gn['to_command_line'](settings))],
        cwd=source, env=environment, check=True)
    if args.configure_only:
        return {'configured': True, 'sourceBuilt': False, 'host': host['id']}
    subprocess.run([args.ninja, '-C', str(build_dir), '-j', str(args.jobs), 'gen_snapshot'],
                   env=environment, check=True)
    generator = build_dir / 'gen_snapshot'
    verify_native_host(generator, host)
    sys.path.insert(0, str(directory / 'toolchain'))
    from macho_native_dependencies import audit
    imports = audit(generator, set(), host, tools['objdump'])
    if (inputs != {path.name: sha(path) for path in sorted(manifests.glob('*-source-manifest.json'))}
            or host_inputs != {name: sha(root / name) for name in host_inputs}
            or sha(build_configuration) != identity
            or any(sha(path) != sha_value for path, sha_value in tools_hashes.items())):
        raise ValueError('Snapshot build inputs changed during compilation')
    shutil.copy2(generator, output / 'gen_snapshot')
    report = {'host': host['id'], 'dartTargetArchitecture': 'arm', 'sourceBuilt': True,
              'bitcodeToolchainVerified': True,
              'runtimeExecutionVerified': False, 'sourceManifests': inputs,
              'hostBuildInputs': host_inputs, 'toolchainConfigurationSha256': identity,
              'buildToolsSha256': {name: tools_hashes[path] for name, path in tools.items()},
              'sdkSettingsSha256': sha(sdk / 'SDKSettings.json'),
              'generatorSha256': sha(generator), 'gnArgumentsSha256': sha(build_dir / 'args.gn'),
              'nativeDependencies': imports, 'systemLibrariesCopied': False}
    (output / 'snapshot-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', choices=('macos-x64', 'macos-arm64'), required=True)
    for name in ('output', 'sdk', 'toolchain-config'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--configure-only', action='store_true')
    print(json.dumps(build(parser.parse_args()), indent=2))
