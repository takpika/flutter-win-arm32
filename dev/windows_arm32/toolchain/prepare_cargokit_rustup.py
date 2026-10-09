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
import subprocess


def prepare(configuration, output, driver=None):
    if os.name != 'posix':
        raise ValueError('This launcher preparation is for Unix cross-build hosts')
    configuration = configuration.resolve()
    output = output.resolve()
    config = json.loads(configuration.read_text())
    driver = (Path(__file__).with_name('cargo_toolchain.py') if driver is None else driver).resolve()
    if not driver.is_file():
        raise ValueError('Missing SDK Cargo dispatch implementation')
    records = []
    for name, selected in config['toolchains'].items():
        if not re.fullmatch(r'stable|beta|nightly|\d+\.\d+\.\d+', name):
            raise ValueError('Unsupported SDK Rustup toolchain name: ' + name)
        def path(value):
            value = Path(value)
            return value if value.is_absolute() else configuration.parent / value
        compiler = path(selected['rustc']).resolve()
        info = subprocess.check_output([str(compiler), '-vV'], text=True)
        host = next(line.removeprefix('host: ') for line in info.splitlines()
                    if line.startswith('host: '))
        release = next(line.removeprefix('release: ') for line in info.splitlines()
                       if line.startswith('release: '))
        if name[0].isdigit() and release != name:
            raise ValueError('Pinned Rust compiler version does not match: ' + name)
        if name.startswith(('beta', 'nightly')) and name.split('-')[0] not in release:
            raise ValueError('Rust compiler channel does not match: ' + name)
        toolchain = output / 'toolchains' / (name + '-' + host)
        binary = toolchain / 'bin'
        binary.mkdir(parents=True, exist_ok=True)
        for destination, source in [(binary / 'rustc', compiler),
                                    (binary / 'rustdoc', compiler.with_name('rustdoc')),
                                    (toolchain / 'lib', compiler.parent.parent / 'lib')]:
            if not source.exists():
                raise ValueError('Missing installed Rust dependency: ' + str(source))
            if destination.is_symlink() and destination.resolve() == source.resolve():
                continue
            if destination.exists() or destination.is_symlink():
                raise ValueError('Refusing to replace existing SDK dependency: ' + str(destination))
            destination.symlink_to(os.path.relpath(source, destination.parent),
                                   target_is_directory=source.is_dir())
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
    (output / 'settings.toml').write_text('version = "12"\nprofile = "minimal"\n')
    (output / 'sdk-provenance.json').write_text(json.dumps({
        'toolchains': records, 'configuration': os.path.relpath(configuration, output),
        'installedCompilerPathsRelativeToOutput': True,
        'scope': 'Generated SDK bridge RustBuilder.build only; no Rustup installation or updates',
    }, indent=2) + '\n')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(prepare(args.configuration, args.output))
