#!/usr/bin/env python3
"""Check MSVC compatibility and unchanged standard language semantics."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('compiler', type=Path)
    parser.add_argument('--sysroot', type=Path, required=True)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('.windows-arm32-compiler-check'))
    args = parser.parse_args()
    fixtures = Path(__file__).resolve().parent / 'tests'
    includes = [args.sysroot / 'armv7-w64-mingw32/include/c++/v1',
                args.sysroot / 'generic-w64-mingw32/include',
                args.sysroot / 'armv7-w64-mingw32/include',
                args.engine / 'shell/platform/common/client_wrapper/include']
    for standard in ('17', '20'):
        for target in ('armv7-w64-windows-gnu', 'x86_64-w64-windows-gnu'):
            for mode in ('ms', 'gnu'):
                command = [str(args.compiler), '-cc1', '-triple', target,
                           '-std=c++' + standard, '-fsyntax-only',
                           '-Werror=implicit-int-conversion', '-verify=' + mode]
                if mode == 'ms':
                    command += ['-fms-compatibility', '-fms-compatibility-version=19.44']
                subprocess.run([*command, str(fixtures / 'ms_integer_promotion_range.cpp')],
                               check=True)
                subprocess.run([*command, '-Werror=implicit-float-conversion',
                                '-Werror=float-conversion',
                                str(fixtures / 'ms_int_float_arithmetic.cpp')], check=True)
                subprocess.run([*command, '-Werror=implicit-float-conversion',
                                str(fixtures / 'ms_float_literal.cpp')], check=True)
                default_command = [arg if arg != '-verify=' + mode else '-verify=default'
                                   for arg in command]
                subprocess.run([*default_command, '-Werror',
                                str(fixtures / 'ms_float_literal.cpp')], check=True)
                subprocess.run([*command, '-Werror=float-conversion',
                                '-Werror=bitfield-constant-conversion',
                                str(fixtures / 'ms_bitfield_conversion.cpp')], check=True)
                if mode == 'ms':
                    warning_error_command = [arg if arg != '-verify=ms' else '-verify=wx'
                                             for arg in command]
                    subprocess.run([*warning_error_command, '-Werror',
                                    '-Werror=implicit-float-conversion',
                                    str(fixtures / 'ms_float_literal.cpp')], check=True)
        for mode in ('standard', 'ms'):
            command = [str(args.compiler), '-cc1', '-std=c++' + standard,
                       '-verify=' + mode, '-verify-ignore-unexpected=note']
            if mode == 'ms':
                command.append('-fms-compatibility')
            subprocess.run([*command, str(fixtures / 'ms_inherited_constructor.cpp')], check=True)
        subprocess.run([str(args.compiler), '--driver-mode=g++',
                        '--target=armv7-w64-windows-gnu', '-std=c++' + standard,
                        '-fms-extensions', '-fms-compatibility',
                        '-fms-compatibility-version=19.44', '-fgnuc-version=4.2.1',
                        '-fsyntax-only',
                        *[arg for path in includes for arg in ('-isystem', str(path.resolve()))],
                        str(fixtures / 'flutter_value_ms_compat.cpp')], check=True)
        print('C++' + standard + ': standard, MSVC and Flutter value checks passed', flush=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    temporary = output / 'tmp'
    temporary.mkdir(exist_ok=True)
    env = dict(os.environ, TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary))
    for target in ('thumbv7-pc-windows-msvc', 'armv7-w64-mingw32'):
        for level in ('0', '1', '2'):
            subprocess.run([str(args.compiler.resolve()), '--target=' + target,
                            '-fms-extensions', '-fasync-exceptions', '-O' + level,
                            '-c', str(fixtures / 'windows_arm32_seh.cpp'),
                            '-o', str(output / (target + '-O' + level + '.obj'))],
                           env=env, check=True)
    subprocess.run([str(args.compiler.resolve()), '--target=armv7-w64-mingw32',
                    '-fexceptions', '-fasync-exceptions', '-O1', '-c',
                    str(fixtures / 'windows_arm32_cxx_async.cpp'),
                    '-o', str(output / 'gnu-cxx-async.obj')], env=env, check=True)
    print('ARM32 SEH at O0/O1/O2 and GNU C++ async cleanup checks passed', flush=True)



if __name__ == '__main__':
    main()
