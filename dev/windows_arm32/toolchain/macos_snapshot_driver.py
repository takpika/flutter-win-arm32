#!/usr/bin/env python3
"""Run the shared native LLVM tools for an isolated macOS generator build."""
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    configuration = json.loads(Path(os.environ[
        'FLUTTER_WINDOWS_ARM32_MACOS_SNAPSHOT_CONFIG']).read_text())
    kind, *arguments = sys.argv[1:]
    if kind == 'ar':
        return subprocess.call([configuration['archiver'], *arguments])
    if kind not in ('cc', 'cxx', 'asm', 'objc', 'objcxx', 'link', 'solink'):
        raise ValueError('Unknown macOS snapshot compiler operation: ' + kind)
    flags = ['--target=' + configuration['target'], '-isysroot', configuration['sdk'],
             '-resource-dir', configuration['resourceDir']]
    if kind in ('cxx', 'objcxx', 'link', 'solink'):
        flags += ['--driver-mode=g++']
    if kind in ('link', 'solink'):
        flags += ['-fuse-ld=lld', '--ld-path=' + configuration['linker'], '-Wl,-adhoc_codesign']
    # GN also supplies an unversioned target triple. Keep this toolchain's
    # explicit deployment target authoritative rather than the host default.
    return subprocess.call([configuration['compiler'], *arguments, *flags])


if __name__ == '__main__':
    raise SystemExit(main())
