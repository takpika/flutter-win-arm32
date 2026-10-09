#!/usr/bin/env python3
"""Build a Linux host generator for a destination SDK's frontend AOT."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import runpy
import shutil
import subprocess
import sys

from prepare_sources import prepare_sources
from sdk_hosts import select_host, verify_native_host


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def frontend_vm_source(original, output, revision):
    # A frontend snapshot runs in the official host Dart SDK. Its generator
    # must use that VM's source and snapshot format, independently of the ARM32
    # application VM. Keep both source trees intact in separate build areas.
    source = output / 'sources'
    source.mkdir(exist_ok=True)
    for item in original.iterdir():
        if item.name != 'flutter' and not (source / item.name).exists():
            (source / item.name).symlink_to(item, target_is_directory=item.is_dir())
    flutter = source / 'flutter'
    flutter.mkdir(exist_ok=True)
    for item in (original / 'flutter').iterdir():
        if item.name != 'third_party' and not (flutter / item.name).exists():
            (flutter / item.name).symlink_to(item, target_is_directory=item.is_dir())
    third_party = flutter / 'third_party'
    third_party.mkdir(exist_ok=True)
    for item in (original / 'flutter/third_party').iterdir():
        if item.name != 'dart' and not (third_party / item.name).exists():
            (third_party / item.name).symlink_to(item, target_is_directory=item.is_dir())
    dart = third_party / 'dart'
    if not dart.exists():
        subprocess.run(['git', 'clone', '--shared', '--no-checkout',
                        str(original / 'flutter/third_party/dart'), str(dart)], check=True)
        subprocess.run(['git', '-C', str(dart), 'checkout', '--detach', revision], check=True)
    actual = subprocess.check_output(['git', '-C', str(dart), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != revision or subprocess.check_output(
            ['git', '-C', str(dart), 'status', '--porcelain', '--untracked-files=no'], text=True).strip():
        raise ValueError('Frontend host VM must be the unmodified pinned upstream source')
    # gclient generates this ignored GN input; it is not in the upstream Git
    # checkout. Copy only that build configuration, preserving VM source bytes.
    generated = Path('build/config/gclient_args.gni')
    shutil.copy2(original / 'flutter/third_party/dart' / generated, dart / generated)
    return source


def build(args):
    if platform.system() != 'Linux' or platform.machine().lower() not in ('x86_64', 'amd64'):
        raise ValueError('Frontend cross generators are built in the Ubuntu amd64 container')
    host = select_host(args.target_host)
    root = Path(__file__).resolve().parents[2]
    source = root / 'engine/src'
    compiler = args.compiler_root.resolve()
    verify_native_host(compiler / 'bin/clang', select_host('linux-x64'))
    output = args.output.resolve()
    temporary = output / 'tmp'
    temporary.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary))
    prepare_sources(root)
    dart_manifest = root / 'dev/windows_arm32/patches/dart-source-manifest.json'
    host_vm_revision = json.loads(dart_manifest.read_text())['upstreamRevision']
    original_source = source
    source = frontend_vm_source(original_source, output, host_vm_revision)
    # The GN Linux toolchain resolves its compiler through buildtools_path.
    # Select the shared compiler in a private overlay, never replace gclient's
    # installed compiler or any source-directory link.
    tools = output / 'buildtools'
    tools.mkdir(exist_ok=True)
    original = original_source / 'flutter/buildtools'
    for item in original.iterdir():
        if item.name != 'linux-x64':
            target = tools / item.name
            if not target.exists():
                target.symlink_to(item, target_is_directory=item.is_dir())
    linux = tools / 'linux-x64'
    linux.mkdir(exist_ok=True)
    for item in (original / 'linux-x64').iterdir():
        if item.name == 'clang':
            continue
        target = linux / item.name
        selected = item
        if target.is_symlink():
            if target.resolve() != selected.resolve():
                raise ValueError('Existing frontend compiler overlay differs')
        elif target.exists():
            raise ValueError('Refusing to replace a frontend buildtools entry')
        else:
            target.symlink_to(selected, target_is_directory=selected.is_dir())
    toolchain = linux / 'clang'
    if toolchain.is_symlink() or (toolchain / 'bin').is_symlink():
        raise ValueError('Use a fresh private overlay; never write through a compiler symlink')
    (toolchain / 'bin').mkdir(parents=True, exist_ok=True)
    for item in compiler.iterdir():
        if item.name != 'bin' and not (toolchain / item.name).exists():
            (toolchain / item.name).symlink_to(item, target_is_directory=item.is_dir())
    for item in (compiler / 'bin').iterdir():
        target = toolchain / 'bin' / item.name
        if item.name in ('clang', 'clang++'):
            # Keep LTO enabled and use the source-built LLD, rather than the
            # system GNU linker which requires an unshipped LLVMgold plugin.
            target.write_text(
                f'#!{sys.executable}\nimport os, sys\n'
                'args = sys.argv[1:]\n'
                'if not any(x in args for x in ("-c", "-E", "-S", "-fsyntax-only", "--version", "-print-resource-dir")):\n'
                f'    args.append({("--ld-path=" + str(compiler / "bin/ld.lld"))!r})\n'
                f'os.execv({str(item)!r}, [{str(item)!r}, *args])\n')
            target.chmod(0o755)
        elif not target.exists():
            target.symlink_to(item)
    # Framework content/version metadata belongs to the original SDK checkout;
    # the private root selects only the frontend tool's actual compile inputs.
    gn = runpy.run_path(str(original_source / 'flutter/tools/gn'))
    options = gn['parse_args'](['gn', '--runtime-mode=release', '--no-enable-unittests', '--no-rbe', '--ide='])
    settings = gn['to_gn_args'](options)
    settings.update(target_os={'windows': 'win', 'macos': 'mac', 'linux': 'linux'}[host['os']],
                    target_cpu=host['cpu'], dart_target_arch=host['cpu'],
                    current_os='linux', current_cpu='x64', buildtools_path=str(tools),
                    use_default_linux_sysroot=False, python_path=sys.executable)
    dotfile = output / 'frontend-generator.gn'
    dotfile.write_text('buildconfig = "//build/config/BUILDCONFIG.gn"\n'
                      'secondary_source = "//flutter/build/secondary/"\n'
                      f'script_executable = {json.dumps(sys.executable)}\n'
                      'root = "//flutter/third_party/dart/runtime/bin:gen_snapshot"\n')
    inputs = {p.name: sha(p) for p in sorted((root / 'dev/windows_arm32/patches').glob('*-source-manifest.json'))}
    directory = output / 'build'
    subprocess.run([str(source / 'flutter/third_party/gn/gn'), 'gen', str(directory),
                    '--root=' + str(source), '--dotfile=' + str(dotfile),
                    '--args=' + ' '.join(gn['to_command_line'](settings))], cwd=source,
                   env=environment, check=True)
    if args.configure_only:
        return {'configured': True, 'sourceBuilt': False}
    subprocess.run([args.ninja, '-C', str(directory), '-j', str(args.jobs), 'gen_snapshot'],
                   env=environment, check=True)
    generator = directory / 'gen_snapshot'
    verify_native_host(generator, select_host('linux-x64'))
    if inputs != {p.name: sha(p) for p in sorted((root / 'dev/windows_arm32/patches').glob('*-source-manifest.json'))}:
        raise ValueError('Dart build inputs changed during compilation')
    # Do not replace or spoof the snapshot hash: compute it from the actual
    # pristine VM sources used by this tool.
    host_snapshot_version = subprocess.check_output([sys.executable, '-c',
        'import runpy, sys; sys.path.insert(0, sys.argv[1]); '
        'print(runpy.run_path(sys.argv[1] + "/make_version.py")["MakeSnapshotHashString"]())',
        str(source / 'flutter/third_party/dart/tools')], env=environment, text=True).strip()
    if host_snapshot_version != '1ce86630892e2dca9a8543fdb8ed8e22':
        raise ValueError('Frontend generator VM does not match the official host snapshot format')
    frontend_vm_source(original_source, output, host_vm_revision)
    report = {'buildHost': 'linux-x64', 'aotDestinationHost': host['id'], 'sourceBuilt': True,
              'hostVmUpstreamRevision': host_vm_revision, 'hostVmSourceUnmodified': True,
              'hostVmSnapshotVersion': host_snapshot_version,
              'frontendCompilerSourceManifestSha256': sha(dart_manifest),
              'compilerSha256': sha(compiler / 'bin/clang'),
              'generatorSha256': sha(generator), 'builderSha256': sha(Path(__file__)),
              'gnArgumentsSha256': sha(directory / 'args.gn'),
              'destinationFrontendRuntimeVerified': False, 'sdkHostGenerator': False}
    (output / 'frontend-generator-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target-host', required=True)
    parser.add_argument('--compiler-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--configure-only', action='store_true')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    print(json.dumps(build(args), indent=2))
