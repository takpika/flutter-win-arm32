#!/usr/bin/env python3
"""Compare native ABI code with and without the scoped SDK adapters."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys


def text_sections(path):
    data = path.read_bytes()
    sections = struct.unpack_from('<H', data, 2)[0]
    optional = struct.unpack_from('<H', data, 16)[0]
    result = []
    for index in range(sections):
        position = 20 + optional + index * 40
        name = data[position:position + 8].rstrip(b'\0')
        size, start = struct.unpack_from('<II', data, position + 16)
        if name == b'.text':
            result.append(data[start:start + size])
    if not result or not any(result):
        raise ValueError('Object contains no code: ' + str(path))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain-config', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    port = Path(__file__).resolve().parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    original = args.toolchain_config.resolve()
    config = json.loads(original.read_text())
    for key in ('compiler', 'llvmBin', 'sysroot', 'resourceDir'):
        value = Path(config[key])
        config[key] = str(value if value.is_absolute() else (original.parent / value).resolve())
    config['includeDirs'] = []
    config['systemIncludeDirs'] = []
    baseline = output / 'baseline-toolchain.json'
    baseline.write_text(json.dumps(config, indent=2) + '\n')
    # The SDK takes only pointers to this type in UWP; provide the same incomplete
    # declaration in the baseline so the comparison isolates overload spelling.
    forward = output / 'baseline-forward.h'
    forward.write_text('typedef struct tagFONTSIGNATURE FONTSIGNATURE;\n')
    records = []
    for platform in ('rt', 'phone'):
        for mode, fixture, flags in (
            ('cxx', 'directwrite_cpp.cpp', []),
            ('cc', 'directwrite_c.c', []),
            ('cxx', 'directwrite_c.c', ['-DCINTERFACE', '-x', 'c++']),
            ('cxx', 'windows_process_abi.cpp', []),
            ('cxx', 'xps_cpp.cpp', []),
            ('cc', 'xps_c.c', []),
            ('cxx', 'xps_c.c', ['-DCINTERFACE', '-x', 'c++']),
        ):
            objects = []
            for adapter in (False, True):
                obj = output / f'{platform}-{mode}-{fixture}-{int(adapter)}.obj'
                command = [sys.executable, str(port / f'{platform}_driver.py'), mode,
                           '-O2', *flags, '-c', str(port / 'tests' / fixture), '-o', str(obj)]
                if platform == 'phone':
                    family = 'WINAPI_FAMILY_APP'
                    if (fixture == 'windows_process_abi.cpp' or fixture.startswith('xps_')) and not adapter:
                        # Compare omitted app-family declarations with the real
                        # desktop declarations of the same Windows ABI.
                        family = 'WINAPI_FAMILY_DESKTOP_APP'
                    command += ['-DFLUTTER_WINDOWS_PHONE', '-DWINAPI_FAMILY=' + family]
                if adapter:
                    command += ['-DWINDOWS_ARM32_SDK_ADAPTER_TEST']
                elif platform == 'phone':
                    command += ['-include', str(forward)]
                environment = dict(os.environ, FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG=
                                   str(original if adapter else baseline))
                subprocess.run(command, env=environment, cwd=output, check=True)
                objects.append(obj)
            if text_sections(objects[0]) != text_sections(objects[1]):
                raise ValueError('SDK adapter changed COM call code: ' + str(objects[1]))
            records.append({'platform': platform, 'mode': mode, 'fixture': fixture,
                            'compiled': True, 'codeMatchesUnadaptedMingw': True})
        assembly = output / f'{platform}-srwlock.s'
        command = [sys.executable, str(port / f'{platform}_driver.py'), 'cxx',
                   '/WX', '-D_WIN32_WINNT=0x0603', '-S',
                   str(port / 'tests/windows_srwlock.cpp'), '-o', str(assembly)]
        if platform == 'phone':
            command += ['-DFLUTTER_WINDOWS_PHONE', '-DWINAPI_FAMILY=WINAPI_FAMILY_APP']
        subprocess.run(command, env=dict(os.environ,
                       FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG=str(original)),
                       cwd=output, check=True)
        if '__imp_ReleaseSRWLockExclusive' not in assembly.read_text():
            raise ValueError('SRW lock call did not retain the native OS import')
        records.append({'platform': platform, 'fixture': 'windows_srwlock.cpp',
                        'compiled': True, 'nativeDllImportRetained': True})
        subprocess.run([sys.executable, str(port / f'{platform}_driver.py'), 'cxx',
                        '-fsyntax-only', str(port / 'tests/windows_com_smartptr.cpp')],
                       env=dict(os.environ, FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG=str(original)),
                       cwd=output, check=True)
        records.append({'platform': platform, 'fixture': 'windows_com_smartptr.cpp',
                        'compiled': True, 'repeatedSdkTypedefSupported': True})
    (output / 'verification.json').write_text(json.dumps({'checks': records}, indent=2) + '\n')
    print('DirectWrite, XPS, Windows process ABI and SRW imports verified on RT and Phone')


if __name__ == '__main__':
    main()
