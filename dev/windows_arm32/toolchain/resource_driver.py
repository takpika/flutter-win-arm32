#!/usr/bin/env python3
"""Compile unchanged Windows resources with the SDK's native host tools."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

config_path = Path(os.environ.get('FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG', Path(__file__).parent / 'toolchain.json')).resolve()
configuration = json.loads(config_path.read_text())
sdk = Path(configuration['sysroot'])
if not sdk.is_absolute():
    sdk = (config_path.parent / sdk).resolve()
compiler = sdk / 'bin/armv7-w64-mingw32-windres'
arguments = sys.argv[1:]
sources = [Path(arg) for arg in arguments if arg.lower().endswith('.rc') and Path(arg).is_file()]
if not sources:
    raise SystemExit(subprocess.call([str(compiler), *arguments]))
if len(sources) != 1:
    raise ValueError('Expected one resource compiler input')
source = sources[0]
payload = source.read_bytes()
encoding = 'utf-16' if payload.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8'
text = payload.decode(encoding)

def resource_path(match):
    # llvm-rc on Unix treats Windows separators literally. Normalize only
    # resource file names; preserve resource IDs, strings and binary contents.
    path = match[2].replace('\\\\', '/').replace('\\', '/')
    absolute = (source.resolve().parent / path).resolve()
    return match[1] + '"' + absolute.as_posix() + '"'

text = re.sub(r'(?im)(\b(?:ICON|BITMAP|CURSOR|FONT|RCDATA|HTML|MESSAGETABLE)\s+)"([^"\r\n]+)"',
              resource_path, text)
output_directory = Path(arguments[-1]).absolute().parent
output_directory.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='windows-arm-rc-', dir=output_directory) as directory:
    staged = Path(directory) / source.name
    staged.write_bytes(text.encode(encoding))
    arguments = [str(staged) if argument == str(source) else argument for argument in arguments]
    environment = dict(os.environ, TMPDIR=directory, TMP=directory, TEMP=directory)
    if os.name == 'nt' or sys.platform == 'darwin':
        suffix = '.exe' if os.name == 'nt' else ''
        llvm = Path(configuration['llvmBin'])
        clang = Path(configuration['compiler'])
        if not llvm.is_absolute():
            llvm = (config_path.parent / llvm).resolve()
        if not clang.is_absolute():
            clang = (config_path.parent / clang).resolve()
        # CMake's GNU resource rule supplies preprocessor flags followed by
        # input/output paths. Keep every define/include and quoted macro value.
        preprocessor = arguments[:-2]
        if preprocessor[:2] != ['-O', 'coff']:
            raise ValueError('Expected the standard CMake COFF resource rule')
        preprocessor = preprocessor[2:]
        utf8 = Path(directory) / 'input.rc'
        utf8.write_text(text, encoding='utf-8')
        expanded, resource = (Path(directory) / name for name in ('expanded.rc', 'compiled.res'))
        subprocess.run([str(clang), '--target=armv7-w64-mingw32', '--sysroot='+str(sdk),
                        '-DRC_INVOKED', '-E', '-x', 'c',
                        '-I', str(source.resolve().parent),
                        '-I', str(sdk/'generic-w64-mingw32/include'),
                        *preprocessor, str(utf8), '-o', str(expanded)], env=environment, check=True)
        subprocess.run([str(llvm/('llvm-rc'+suffix)), '/no-preprocess', '/FO', str(resource),
                        str(expanded)], env=environment, check=True)
        subprocess.run([str(llvm/('llvm-cvtres'+suffix)), '/machine:arm', '/timestamp:0',
                        '/out:'+str(Path(arguments[-1]).absolute()), str(resource)],
                       env=environment, check=True)
        raise SystemExit(0)
    # Preserve CMake's literal quoted macro values instead of applying GNU
    # windres' additional shell-unescape pass to them.
    raise SystemExit(subprocess.call([str(compiler), '--use-temp-file', '-I', str(source.resolve().parent),
                                    '-I', str(sdk / 'generic-w64-mingw32/include'),
                                    *arguments], env=environment))
