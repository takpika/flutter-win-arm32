#!/usr/bin/env python3
"""Keep Cargo host and target std-source probes on the installed SDK sysroot."""
import os
import re
import subprocess
import sys

if __name__ == '__main__':
    command = sys.argv[1:]
    # Cargo may nest a workspace wrapper between this wrapper and rustc.
    compiler_index = 1 if os.environ.get('RUSTC_WORKSPACE_WRAPPER') == command[0] else 0
    arguments = command[compiler_index + 1:]
    host_linker = os.environ.get('FLUTTER_WINDOWS_ARM32_HOST_LINKER')
    targets = [arguments[i+1] for i,a in enumerate(arguments[:-1]) if a == '--target']
    targets += [a.split('=',1)[1] for a in arguments if a.startswith('--target=')]
    if os.name == 'nt' and host_linker and (not targets or targets == ['x86_64-pc-windows-gnu']):
        command += ['-C', 'linker='+host_linker, '-C', 'linker-flavor=gcc', '-C', 'link-self-contained=no']
    root = os.environ['FLUTTER_WINDOWS_ARM32_RUST_SYSROOT']
    if not any(value == '--sysroot' or value.startswith('--sysroot=') for value in arguments):
        command.insert(compiler_index + 1, '--sysroot=' + root)
    upstream = os.environ.get('FLUTTER_WINDOWS_ARM32_UPSTREAM_RUSTC_WRAPPER')
    if upstream:
        command.insert(0, upstream)
    options = {}
    if os.name == 'posix':
        inherited = set()
        for read_fd, write_fd in re.findall(
                r'--jobserver-(?:auth|fds)=(\d+),(\d+)', os.environ.get('CARGO_MAKEFLAGS', '')):
            for value in (read_fd, write_fd):
                descriptor = int(value)
                try:
                    os.fstat(descriptor)
                except OSError:
                    continue
                inherited.add(descriptor)
        options['pass_fds'] = tuple(sorted(inherited))
    raise SystemExit(subprocess.call(command, **options))
