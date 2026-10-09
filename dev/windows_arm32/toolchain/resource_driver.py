#!/usr/bin/env python3
"""Compile unchanged Windows resources on a Unix host with the ARM SDK."""
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
    # Preserve CMake's literal quoted macro values instead of applying GNU
    # windres' additional shell-unescape pass to them.
    raise SystemExit(subprocess.call([str(compiler), '--use-temp-file', '-I', str(source.resolve().parent),
                                    '-I', str(sdk / 'generic-w64-mingw32/include'),
                                    *arguments], env=environment))
