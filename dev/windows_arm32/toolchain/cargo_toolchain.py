#!/usr/bin/env python3
"""Dispatch a Cargokit Cargo build to an explicitly installed SDK toolchain."""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess


def invocation(configuration, arguments, environment, *, prepare_sdk_lock=False):
    config = json.loads(configuration.read_text())
    def path(value):
        value = Path(value)
        return value if value.is_absolute() else (configuration.parent / value).resolve()
    if len(arguments) < 5 or arguments[0] != 'run' or arguments[2:4] != ['cargo', 'build']:
        raise ValueError('SDK dispatch requires rustup run TOOLCHAIN cargo build')
    selected = config['toolchains'].get(arguments[1])
    if selected is None:
        raise ValueError('Rust toolchain is not installed in this SDK: ' + arguments[1])
    cargo_args = list(arguments[3:])
    targets = [cargo_args[index + 1] for index, value in enumerate(cargo_args[:-1])
               if value == '--target']
    targets += [value.split('=', 1)[1] for value in cargo_args if value.startswith('--target=')]
    if len(targets) != 1 or targets[0] not in config['targets']:
        raise ValueError('Supply exactly one installed SDK ARM32 target')
    target = selected.get('targets', config['targets'])[targets[0]]
    spec = path(target['spec'])
    if spec.stem != targets[0]:
        raise ValueError('Installed Rust target name does not match its JSON filename')
    cargo, rustc, sysroot = (path(selected[name]) for name in ('cargo', 'rustc', 'sysroot'))
    wrapper = path(config['rustcWrapper'])
    for required in (cargo, rustc, rustc.with_name('rustdoc'), wrapper, spec,
                     sysroot / 'lib/rustlib/src/rust/library/Cargo.toml'):
        if not required.is_file():
            raise ValueError('Missing SDK Cargo dependency: ' + str(required))
    temporary = path(config['temporaryDirectory'])
    cargo_home = path(config['cargoHome'])
    temporary.mkdir(parents=True, exist_ok=True)
    cargo_home.mkdir(parents=True, exist_ok=True)
    result = dict(environment)
    # Respect Cargo's original encoded-flags precedence and retain package flags.
    if 'CARGO_ENCODED_RUSTFLAGS' in result:
        flags = result['CARGO_ENCODED_RUSTFLAGS'].split('\x1f')
    else:
        flags = shlex.split(result.get('RUSTFLAGS', ''))
    if any(value == '--sysroot' or value.startswith('--sysroot=') for value in flags):
        raise ValueError('A package-selected Rust sysroot conflicts with the installed SDK')
    flags += ['-Zunstable-options', '--sysroot=' + str(sysroot), '--cfg', 'windows_raw_dylib']
    result.update(CARGO_ENCODED_RUSTFLAGS='\x1f'.join(flags), RUSTC=str(rustc),
                  RUSTDOC=str(rustc.with_name('rustdoc')), RUSTC_BOOTSTRAP='1',
                  RUST_TARGET_PATH=os.pathsep.join(filter(None, (
                      str(spec.parent), result.get('RUST_TARGET_PATH', '')))),
                  PATH=os.pathsep.join(filter(None, (
                      str(spec.parent), str(cargo.parent), result.get('PATH', '')))),
                  CARGO_HOME=str(cargo_home),
                  TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary), CI='true')
    # Cargo probes the host sysroot as well as the target flags when building std.
    if result.get('RUSTC_WRAPPER') and result['RUSTC_WRAPPER'] != str(wrapper):
        result['FLUTTER_WINDOWS_ARM32_UPSTREAM_RUSTC_WRAPPER'] = result['RUSTC_WRAPPER']
    result['RUSTC_WRAPPER'] = str(wrapper)
    result['FLUTTER_WINDOWS_ARM32_RUST_SYSROOT'] = str(sysroot)
    # Cargo's cached host probe does not track this wrapper's sysroot selection.
    # Re-query compiler information when using the SDK's redirected sysroot.
    result['CARGO_CACHE_RUSTC_INFO'] = '0'
    for key, value in target.get('environmentPaths', {}).items():
        result[key] = str(path(value))
    result.update(target.get('environmentValues', {}))
    if '--locked' not in cargo_args and '--frozen' not in cargo_args:
        cargo_args.append('--locked')
    cargo_args += ['-Z', 'build-std=std,panic_abort']
    from prepare_cargo_lock import cargo_lock_options
    version = subprocess.check_output([str(cargo), '--version'], text=True).split()[1]
    if tuple(int(value) for value in version.split('.')[:2]) >= (1, 95):
        cargo_args += ['-Z', 'json-target-spec']
    for value in config.get('cargoConfigs', []):
        cargo_args += ['--config', str(path(value))]
    if config.get('cargoConfigs'):
        if result.get('CARGO_RESOLVER_LOCKFILE_PATH'):
            raise ValueError('A package-selected Cargo lockfile conflicts with SDK dependency patches')
        def option(name):
            values = [cargo_args[index + 1] for index, value in enumerate(cargo_args[:-1])
                      if value == name]
            values += [value.split('=', 1)[1] for value in cargo_args if value.startswith(name + '=')]
            if len(values) != 1:
                raise ValueError('Supply exactly one ' + name + ' for SDK dependency patches')
            return Path(values[0]).resolve()
        manifest = option('--manifest-path')
        output = option('--target-dir') / 'sdk-locks' / hashlib.sha256(str(manifest).encode()).hexdigest()[:16]
        result['FLUTTER_WINDOWS_ARM32_SDK_LOCK_DIRECTORY'] = str(output)
        resolver = output / 'resolver.toml'
        if prepare_sdk_lock:
            from prepare_cargo_lock import prepare
            if '--offline' in cargo_args or '--frozen' in cargo_args:
                result['CARGO_NET_OFFLINE'] = 'true'
            resolver = prepare(cargo, manifest, output, targets[0],
                               [path(value) for value in config['cargoConfigs']], result)
        if prepare_sdk_lock or resolver.is_file():
            cargo_args += cargo_lock_options(cargo, resolver)
        elif tuple(int(value) for value in version.split('.')[:2]) >= (1, 95):
            cargo_args += ['-Z', 'lockfile-path', '--config', str(resolver)]
        else:
            # Initial planning only; the locked preparation produces this file.
            cargo_args += ['-Z', 'unstable-options', '--lockfile-path', str(output / 'Cargo.lock')]
    return [str(cargo), *cargo_args], result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', type=Path, required=True)
    parser.add_argument('--print-command', action='store_true')
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command, environment = invocation(args.configuration.resolve(), args.arguments, os.environ)
    if args.print_command:
        print(json.dumps(command))
    else:
        with ExitStack() as scope:
            directory = environment.get('FLUTTER_WINDOWS_ARM32_SDK_LOCK_DIRECTORY')
            if directory:
                Path(directory).mkdir(parents=True, exist_ok=True)
                guard = scope.enter_context((Path(directory) / 'sdk-lock.guard').open('a+b'))
                if os.name == 'posix':
                    import fcntl
                    fcntl.flock(guard, fcntl.LOCK_EX)
                else:
                    import msvcrt
                    if guard.seek(0, 2) == 0:
                        guard.write(b'\0')
                        guard.flush()
                    guard.seek(0)
                    msvcrt.locking(guard.fileno(), msvcrt.LK_LOCK, 1)
                command, environment = invocation(args.configuration.resolve(), args.arguments,
                                                  os.environ, prepare_sdk_lock=True)
            raise SystemExit(subprocess.call(command, env=environment))
