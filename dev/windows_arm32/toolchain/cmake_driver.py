#!/usr/bin/env python3
"""CMake compiler entry point for the SDK's Windows GNU ARM toolchain."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from gnu_driver import expand, main


def run():
    directory = Path(__file__).resolve().parent
    config_path = Path(os.environ.get('FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG',
                                     directory / 'toolchain.json')).resolve()
    config = json.loads(config_path.read_text())
    arguments = [re.sub(r'^(/wd|/we)"([0-9]+)"$', r'\1\2', value)
                 for value in expand(sys.argv[1:])]
    # MSVC's last command-line macro definition wins. Preserve that behavior.
    definitions = {value[2:].split('=', 1)[0]: index
                   for index, value in enumerate(arguments)
                   if value.startswith(('-D', '/D')) and len(value) > 2}
    arguments = [value for index, value in enumerate(arguments)
                 if not value.startswith(('-D', '/D')) or len(value) <= 2
                 or definitions[value[2:].split('=', 1)[0]] == index]
    mode = 'cxx' if (any(value in arguments for value in ('-c', '-E', '-S', '-fsyntax-only', '--version'))
                     or arguments == ['-v']) else 'link'
    if '-shared' in arguments:
        mode = 'solink'
    if mode in ('link', 'solink'):
        arguments.append('-lruntimeobject')
    if mode == 'link' and '-municode' not in arguments:
        objects = [value for value in arguments if value.endswith(('.obj', '.o')) and Path(value).is_file()]
        if objects:
            llvm = Path(config['llvmBin'])
            if not llvm.is_absolute():
                llvm = (config_path.parent / llvm).resolve()
            symbols = subprocess.check_output([str(llvm / 'llvm-nm'), '--defined-only', *objects], text=True)
            if re.search(r'\bT\s+(?:wWinMain|wmain)(?:@\d+)?$', symbols, re.M):
                arguments.append('-municode')
    platform = os.environ.get('FLUTTER_WINDOWS_ARM32_PLATFORM', 'rt')
    if platform not in ('rt', 'phone'):
        raise ValueError('Unknown SDK native build family: ' + platform)
    sys.argv = [str(directory / (platform + '_driver.py')), mode, *arguments]
    return main(platform)


if __name__ == '__main__':
    raise SystemExit(run())
