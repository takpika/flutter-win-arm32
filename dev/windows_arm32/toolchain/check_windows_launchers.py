#!/usr/bin/env python3
"""Exercise Windows SDK tool dispatch, Unicode arguments and relocation in Wine."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import zipfile

from windows_launchers import write_launcher

PYTHON_URL = 'https://www.python.org/ftp/python/3.14.6/python-3.14.6-embed-amd64.zip'
PYTHON_SHA256 = 'df901e84a896ff1ee720ad03377e0c8d8c2244fda79808aeeaff6316df1cb75c'


def check(launcher, python_archive, output, wine):
    if hashlib.sha256(python_archive.read_bytes()).hexdigest() != PYTHON_SHA256:
        raise ValueError('Official Windows Python input checksum mismatch')
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    stage = output / 'SDK original'
    stage.mkdir()
    python = stage / 'python'
    python.mkdir()
    with zipfile.ZipFile(python_archive) as source:
        for member in source.infolist():
            path = PurePosixPath(member.filename)
            if path.is_absolute() or '..' in path.parts or '\\' in member.filename:
                raise ValueError('Unsafe Python archive member')
        source.extractall(python)
    tools = stage / 'tools'
    tools.mkdir()
    probe = tools / 'probe.py'
    probe.write_text(
        'import json, os, sys\nfrom pathlib import Path\n'
        'Path(sys.argv[1]).write_text(json.dumps({"arguments": sys.argv[2:], '
        '"environment": os.environ["SDK_PROOF_DIRECTORY"]}, ensure_ascii=False), encoding="utf-8")\n'
        'print("SDK_STDOUT_PROOF")\nprint("SDK_STDERR_PROOF", file=sys.stderr)\n'
        'raise SystemExit(37)\n', encoding='utf-8')
    result = tools / 'result.json'
    write_launcher(tools / 'tool shim.exe', launcher, python / 'python.exe', probe,
                   arguments=(result,), environment_paths={'SDK_PROOF_DIRECTORY': '..'})
    relocated = output / "SDK moved 日本語 ' $literal"
    stage.rename(relocated)
    prefix = output / 'wine-prefix'
    temporary = output / 'tmp'
    temporary.mkdir()
    environment = dict(os.environ, WINEPREFIX=str(prefix), WINEARCH='win64',
                       TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary))
    arguments = ['日本語', '', 'two words', '"quoted"', 'C:\\trailing\\',
                 'spaces and slash\\', '\\"', '$literal `literal`', 'line\nnext']
    invocation = subprocess.run([wine, str(relocated / 'tools/tool shim.exe'), *arguments],
                                env=environment, capture_output=True, text=True)
    if invocation.returncode != 37 or 'SDK_STDOUT_PROOF' not in invocation.stdout or 'SDK_STDERR_PROOF' not in invocation.stderr:
        raise ValueError('Windows launcher did not preserve exit code and standard streams: '
                         + repr((invocation.returncode, invocation.stdout, invocation.stderr)))
    record = json.loads((relocated / 'tools/result.json').read_text(encoding='utf-8'))
    if record['arguments'] != arguments:
        raise ValueError('Windows launcher changed command-line arguments')
    if not record['environment'].endswith(relocated.name):
        raise ValueError('Windows SDK environment path did not relocate')
    report = {'windowsLauncherSha256': hashlib.sha256(launcher.read_bytes()).hexdigest(),
              'pythonSha256': PYTHON_SHA256, 'unicodeAndSpecialArgumentsPreserved': True,
              'exitCodePreserved': True, 'standardStreamsPreserved': True,
              'relocationVerified': True, 'systemLibrariesCopied': False,
              'executionEnvironment': 'Wine; native Windows SDK application builds still required'}
    (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--launcher', type=Path, required=True)
    parser.add_argument('--python-archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--wine', default='wine')
    args = parser.parse_args()
    print(json.dumps(check(args.launcher, args.python_archive, args.output, args.wine)))
