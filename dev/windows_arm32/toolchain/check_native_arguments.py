#!/usr/bin/env python3
"""Exercise long GNU Clang arguments with Unicode and quoting on the SDK host."""
import json
import os
from pathlib import Path
import subprocess
import sys

from gnu_driver import invoke


def check(configuration, output):
    configuration, output = configuration.resolve(), output.resolve()
    settings = json.loads(configuration.read_text())
    compiler = Path(settings['compiler'])
    if not compiler.is_absolute():
        compiler = configuration.parent / compiler
    work = output / "native arguments 日本語 '"
    work.mkdir(parents=True, exist_ok=True)
    source, preprocessed, obj = (work / name for name in ('input.c', 'expanded.i', 'proof.obj'))
    literal = '"space 日本語 apostrophe \' backslash \\\\"'
    source.write_text('const char *value = STRING;\n'
                      '#if SDK_ARG_2599 != 2599\n#error argument truncated\n#endif\n'
                      'int proof(void){return SDK_ARG_2599;}\n', encoding='utf-8')
    command = [str(compiler), '--target=x86_64-w64-mingw32', '-x', 'c', '-E', '-P',
               *(f'-DSDK_ARG_{i}={i}' for i in range(2600)), '-DSTRING=' + literal,
               str(source), '-o', str(preprocessed)]
    length = len(subprocess.list2cmdline(command).encode('utf-16-le')) // 2
    if length <= 32767:
        raise ValueError('Long argument probe does not exceed the Windows command limit')
    environment = dict(os.environ, TMPDIR=str(work))
    if invoke(command, environment):
        raise ValueError('Long native Clang argument invocation failed')
    text = preprocessed.read_text(encoding='utf-8')
    if literal not in text or 'return 2599;' not in text:
        raise ValueError('Native Clang arguments changed Unicode, quoting or trailing macros')
    if invoke([str(compiler), '--target=x86_64-w64-mingw32', '-x', 'cpp-output',
               '-c', str(preprocessed), '-o', str(obj)], environment):
        raise ValueError('Native Clang failed to compile preserved arguments')
    if obj.read_bytes()[:2] != b'\x64\x86':
        raise ValueError('Argument probe did not produce a Windows x64 object')
    (output / 'native-arguments-verification.json').write_text(json.dumps({
        'commandUtf16Units': length, 'unicodeAndQuotingPreserved': True,
        'trailingArgumentPreserved': True, 'nativeCoffCompilationVerified': True,
        'windowsResponseFileVerified': os.name == 'nt'}, indent=2) + '\n')


if __name__ == '__main__':
    check(Path(sys.argv[1]), Path(sys.argv[2]))
