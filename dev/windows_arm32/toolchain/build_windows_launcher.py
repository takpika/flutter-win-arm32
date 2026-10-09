#!/usr/bin/env python3
"""Cross-build the SDK's shared Windows Python launcher with a selected compiler."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(compiler, output, flags=()):
    # Target-prefixed MinGW driver aliases select the target from argv[0].
    compiler, output = Path(os.path.abspath(compiler)), output.resolve()
    source = Path(__file__).with_name('windows_python_launcher.c')
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.parent / 'tmp'
    temporary.mkdir(exist_ok=True)
    environment = dict(os.environ, TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary))
    original = digest(source)
    command = [str(compiler), *flags, '-municode', '-O2', '-Wall', '-Wextra', '-Werror',
               str(source), '-o', str(output)]
    subprocess.run(command, env=environment, check=True)
    image = output.read_bytes()
    pe = int.from_bytes(image[60:64], 'little')
    if image[:2] != b'MZ' or image[pe:pe + 6] != b'PE\0\0\x64\x86':
        raise ValueError('Launcher compiler did not produce a Windows x64 PE image')
    if digest(source) != original:
        raise ValueError('Launcher source changed during compilation')
    provenance = {'sourceSha256': original, 'compilerSha256': digest(compiler),
                  'compilerIdentity': subprocess.check_output([str(compiler), '--version'], text=True),
                  'flags': list(flags), 'outputSha256': digest(output),
                  'host': 'windows-x64', 'sourceBuilt': True, 'systemLibrariesCopied': False,
                  'runtimeInvocationVerified': False}
    output.with_suffix('.provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--flag', action='append', default=[])
    args = parser.parse_args()
    print(build(args.compiler, args.output, args.flag))
