#!/usr/bin/env python3
"""Give the unchanged Windows font subsetter relative ASCII file arguments."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(raw, arguments):
    raw = raw.resolve()
    if len(arguments) != 2:
        return subprocess.call([str(raw), *arguments])
    output, source = (Path(value).absolute() for value in arguments)
    # The old tool accepts narrow argv. Its current directory is still passed
    # through CreateProcessW, so relative ASCII names preserve a Unicode parent.
    scratch = raw.parent / '.windows-arm32-cache/font-subset'
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch) as temporary:
        directory = Path(temporary)
        shutil.copyfile(source, directory / 'input.ttf')
        result = subprocess.call([str(raw), 'output.ttf', 'input.ttf'], cwd=directory,
                                 stdin=sys.stdin, stdout=sys.stdout, stderr=sys.stderr)
        if result == 0:
            shutil.copyfile(directory / 'output.ttf', output)
        return result


if __name__ == '__main__':
    raise SystemExit(run(Path(sys.argv[1]), sys.argv[2:]))
