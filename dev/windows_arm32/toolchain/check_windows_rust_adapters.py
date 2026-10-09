#!/usr/bin/env python3
"""Exercise SDK Rust forwarding and Cargo argument dispatch after relocation."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

from package_compiler import sha
from prepare_cargokit_rustup import prepare
from windows_launchers import write_launcher

PYTHON_SHA256 = 'df901e84a896ff1ee720ad03377e0c8d8c2244fda79808aeeaff6316df1cb75c'


def check(distribution, launcher, python_archive, output):
    distribution, launcher, python_archive, output = (p.resolve() for p in
                                                     (distribution, launcher, python_archive, output))
    if sha(python_archive) != PYTHON_SHA256 or output.exists():
        raise ValueError('Unexpected Python input or existing verification output')
    output.mkdir(parents=True)
    stage = output / 'SDK original'
    (stage / 'python').mkdir(parents=True)
    with zipfile.ZipFile(python_archive) as archive:
        for entry in archive.namelist():
            if Path(entry).is_absolute() or '..' in Path(entry).parts or '\\' in entry:
                raise ValueError('Unsafe Python archive member')
        archive.extractall(stage / 'python')
    shutil.copytree(distribution, stage / 'rust', copy_function=os.link)
    tools = stage / 'tools'
    tools.mkdir()
    source = Path(__file__).resolve().parent
    for name in ('cargo_toolchain.py', 'prepare_cargo_lock.py', 'rustc_sysroot.py'):
        shutil.copy2(source / name, tools / name)
    write_launcher(tools / 'rustc-wrapper.exe', launcher, stage / 'python/python.exe', tools / 'rustc_sysroot.py')
    target = 'thumbv7a-rt-windows-msvc'
    upstream = subprocess.check_output(['wine', str(distribution / 'bin/rustc.exe'),
        '-Zunstable-options', '--print', 'target-spec-json', '--target', 'thumbv7a-pc-windows-msvc'],
        env=dict(os.environ, RUSTC_BOOTSTRAP='1'), text=True)
    (tools / (target + '.json')).write_text(upstream)
    configuration = tools / 'cargo.json'
    configuration.write_text(json.dumps({
        'toolchains': {'stable': {'rustc': '../rust/bin/rustc.exe',
                                 'cargo': '../rust/bin/cargo.exe', 'sysroot': '../rust'}},
        'targets': {target: {'spec': target + '.json'}},
        'rustcWrapper': 'rustc-wrapper.exe', 'temporaryDirectory': '../tmp', 'cargoHome': '../cargo',
    }))
    probe = tools / 'cargo_probe.py'
    probe.write_text(
        'import json, os, sys\nfrom pathlib import Path\nfrom cargo_toolchain import invocation\n'
        'assert sys.argv[1] == "--configuration"\n'
        'command, env = invocation(Path(sys.argv[2]), sys.argv[3:], os.environ)\n'
        'assert env["RUSTDOC"].endswith("rustdoc.exe") and Path(env["RUSTDOC"]).is_file()\n'
        'assert env["RUSTC_WRAPPER"].endswith("rustc-wrapper.exe")\n'
        'assert Path(command[0]).is_file()\nprint("SDK_CARGO_DISPATCH_PROOF")\n', encoding='utf-8')
    # The probe uses the real dispatch implementation and real cargo --version;
    # compilation with the SDK ARM32 linker remains a separate integration gate.
    prepare(configuration, stage / 'rustup', driver=probe,
            windows_launcher=launcher, python=stage / 'python/python.exe', runner=('wine',))
    relocated = output / "SDK moved 日本語 ' $literal"
    stage.rename(relocated)
    original_info = subprocess.check_output(['wine', str(distribution / 'bin/rustc.exe'), '-vV'], text=True)
    host = next(line.removeprefix('host: ') for line in original_info.splitlines() if line.startswith('host: '))
    binary = relocated / ('rustup/toolchains/stable-' + host) / 'bin'
    def run(path, arguments, environment=None):
        result = subprocess.run(['wine', str(path), *arguments], env=environment,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
        if result.returncode:
            raise ValueError('Native Rust adapter failed: ' + path.name + ': ' + result.stderr[-1500:])
        return result.stdout
    if run(binary / 'rustc.exe', ['-vV']) != original_info:
        raise ValueError('Relocated Rust compiler forwarding changed identity')
    if 'rustdoc ' not in run(binary / 'rustdoc.exe', ['--version']):
        raise ValueError('Relocated Rustdoc forwarding failed')
    if 'SDK_CARGO_DISPATCH_PROOF' not in run(binary / 'cargo.exe', ['build', '--target', target]):
        raise ValueError('Relocated Cargo adapter dispatch failed')
    root = subprocess.check_output(['winepath', '-w', str(relocated / 'rust')], text=True).strip()
    environment = dict(os.environ, FLUTTER_WINDOWS_ARM32_RUST_SYSROOT=root)
    if run(relocated / 'tools/rustc-wrapper.exe', [root + r'\bin\rustc.exe', '-vV'], environment) != original_info:
        raise ValueError('Native Rust sysroot wrapper changed compiler invocation')
    report = {'rustcForwardingVerified': True, 'rustdocForwardingVerified': True,
              'cargoDispatchInvocationVerified': True, 'rustcSysrootWrapperVerified': True,
              'unicodeRelocationVerified': True, 'fullSdkArm32BuildVerified': False,
              'executionEnvironment': 'Wine', 'systemLibrariesCopied': False}
    (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('distribution', 'launcher', 'python-archive', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(check(args.distribution, args.launcher, args.python_archive, args.output)))
