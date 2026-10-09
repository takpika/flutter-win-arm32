#!/usr/bin/env python3
"""Link official Windows GNU Rust host tools with the SDK's common LLVM driver."""
import json
import os
from pathlib import Path
import sys
from gnu_driver import expand, main

if __name__ == '__main__':
    configuration = Path(os.environ['FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG']).resolve()
    settings = json.loads(configuration.read_text())
    resource = Path(settings.get('runtimeResourceDir', settings['resourceDir']))
    if not resource.is_absolute():
        resource = configuration.parent / resource
    builtins = resource / 'lib/windows/libclang_rt.builtins-x86_64.a'
    sysroot = Path(settings['sysroot'])
    if not sysroot.is_absolute():
        sysroot = configuration.parent / sysroot
    unwind = sysroot / 'x86_64-w64-mingw32/lib/libunwind.a'
    for library in (builtins, unwind):
        if not library.is_file():
            raise ValueError('Missing Windows x64 runtime archive: ' + str(library))
    # Official GNU Rust names GCC's runtime libraries explicitly. Supply their
    # LLVM equivalents, including real unwinding support, from the shared SDK.
    libraries = {'-lgcc': str(builtins), '-lgcc_eh': str(unwind)}
    sys.argv = [sys.argv[0], 'link',
                *(libraries.get(argument, argument) for argument in expand(sys.argv[1:])),
                '-Wl,-Bstatic']
    raise SystemExit(main('rt', target_cpu='x64'))
