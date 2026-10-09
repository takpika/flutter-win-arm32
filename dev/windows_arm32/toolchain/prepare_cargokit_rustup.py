#!/usr/bin/env python3
"""Prepare private Rustup launchers for original Cargokit build commands.

Only use this home for the generated SDK bridge's RustBuilder.build(). It does
not install toolchains or targets and must not be used for Rustup updates.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
from windows_launchers import write_launcher
import subprocess


def prepare(configuration, output, driver=None, *, windows_launcher=None, python=None, runner=(),
            cross_prepare=False):
    if os.name != 'posix' and windows_launcher is None:
        raise ValueError('This launcher preparation is for Unix cross-build hosts')
    configuration = configuration.resolve()
    output = output.resolve()
    config = json.loads(configuration.read_text())
    driver = (Path(__file__).with_name('cargo_toolchain.py') if driver is None else driver).resolve()
    if not driver.is_file():
        raise ValueError('Missing SDK Cargo dispatch implementation')
    if windows_launcher is not None and python is None:
        raise ValueError('Windows Rustup adapters require the SDK Python interpreter')
    if cross_prepare and (windows_launcher is not None or runner):
        raise ValueError('Cross-prepared Rustup homes do not execute destination tools')
    suffix = '.exe' if windows_launcher is not None else ''
    records = []
    for name, selected in config['toolchains'].items():
        if not re.fullmatch(r'stable|beta|nightly|\d+\.\d+\.\d+', name):
            raise ValueError('Unsupported SDK Rustup toolchain name: ' + name)
        def path(value):
            value = Path(value)
            return value if value.is_absolute() else configuration.parent / value
        compiler = path(selected['rustc']).resolve()
        if cross_prepare:
            from rust_cross_identity import load
            identity = load(compiler)
            host, release = identity['host'], identity['rustVersion']
        else:
            info = subprocess.check_output([*runner, str(compiler), '-vV'], text=True)
            host = next(line.removeprefix('host: ') for line in info.splitlines()
                        if line.startswith('host: '))
            release = next(line.removeprefix('release: ') for line in info.splitlines()
                           if line.startswith('release: '))
        if windows_launcher is not None and host not in ('x86_64-pc-windows-msvc', 'x86_64-pc-windows-gnu'):
            raise ValueError('Windows SDK Rustup adapter requires a Windows x64 compiler')
        if name[0].isdigit() and release != name:
            raise ValueError('Pinned Rust compiler version does not match: ' + name)
        if name.startswith(('beta', 'nightly')) and name.split('-')[0] not in release:
            raise ValueError('Rust compiler channel does not match: ' + name)
        toolchain = output / 'toolchains' / (name + '-' + host)
        binary = toolchain / 'bin'
        binary.mkdir(parents=True, exist_ok=True)
        for destination, source in [(binary / ('rustc' + suffix), compiler),
                                    (binary / ('rustdoc' + suffix), compiler.with_name('rustdoc' + suffix)),
                                    (toolchain / 'lib', compiler.parent.parent / 'lib')]:
            if not source.exists():
                raise ValueError('Missing installed Rust dependency: ' + str(source))
            if windows_launcher is not None:
                if destination.exists() or destination.is_symlink():
                    raise ValueError('Windows SDK Rustup dependency already exists: ' + str(destination))
                if source.is_dir():
                    # Windows ZIP distributions cannot rely on Unix symlinks.
                    shutil.copytree(source, destination)
                else:
                    forwarding = binary / 'run_native.py'
                    if not forwarding.exists():
                        forwarding.write_text('import subprocess, sys\nraise SystemExit(subprocess.call(sys.argv[1:]))\n')
                    write_launcher(destination, windows_launcher, python, forwarding, arguments=(source,))
                continue
            if destination.is_symlink() and destination.resolve() == source.resolve():
                continue
            if destination.exists() or destination.is_symlink():
                raise ValueError('Refusing to replace existing SDK dependency: ' + str(destination))
            destination.symlink_to(os.path.relpath(source, destination.parent),
                                   target_is_directory=source.is_dir())
        if windows_launcher is not None:
            write_launcher(binary / 'cargo.exe', windows_launcher, python, driver,
                           arguments=('--configuration', configuration, 'run', name, 'cargo'))
        else:
            launcher = binary / 'cargo'
            launcher.write_text('#!/usr/bin/env python3\nimport subprocess, sys\nfrom pathlib import Path\n'
                                'directory = Path(__file__).resolve().parent\n'
                                'raise SystemExit(subprocess.call([sys.executable, '
                                'str(directory / ' + repr(os.path.relpath(driver, binary))
                                + '), "--configuration", str(directory / '
                                + repr(os.path.relpath(configuration, binary)) + '), "run", ' + repr(name)
                                + ', "cargo", *sys.argv[1:]]))\n')
            launcher.chmod(0o755)
        records.append({'name': name, 'host': host, 'release': release,
                        'installedCompiler': os.path.relpath(compiler, output)})
    hosts = {record['host'] for record in records}
    if len(hosts) != 1:
        raise ValueError('SDK Rustup home requires one shared native compiler host')
    (output / 'settings.toml').write_text(
        'version = "12"\nprofile = "minimal"\ndefault_host_triple = ' +
        json.dumps(hosts.pop()) + '\n')
    (output / 'sdk-provenance.json').write_text(json.dumps({
        'toolchains': records, 'configuration': os.path.relpath(configuration, output),
        'installedCompilerPathsRelativeToOutput': True,
        'crossPreparation': cross_prepare, 'compilerExecutionVerified': not cross_prepare,
        'scope': 'Generated SDK bridge RustBuilder.build only; no Rustup installation or updates',
    }, indent=2) + '\n')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--windows-launcher', type=Path)
    parser.add_argument('--python', type=Path)
    parser.add_argument('--runner', action='append', default=[])
    args = parser.parse_args()
    print(prepare(args.configuration, args.output, windows_launcher=args.windows_launcher,
                  python=args.python, runner=args.runner))
