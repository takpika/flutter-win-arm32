"""Expose private Windows SDK headers with Windows filename semantics."""
import hashlib
import json
import os
from pathlib import Path


def prepare(roots, cache):
    # Preserve include aliases: llvm-mingw's target include directory is a
    # symlink, and Clang searches through that spelling rather than its target.
    roots = sorted({str(Path(root).absolute()) for root in roots if Path(root).is_dir()})
    key = hashlib.sha256(json.dumps(roots).encode()).hexdigest()
    destination = cache / ('windows-headers-' + key + '.json')
    if destination.is_file():
        return destination
    entries = []
    for root in roots:
        for path in sorted(Path(root).rglob('*')):
            if path.is_file():
                entries.append({'type': 'file', 'name': str(path),
                                'external-contents': str(path.resolve())})
    record = {'version': 0, 'case-sensitive': False,
              'use-external-names': False, 'roots': entries}
    cache.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix('.' + str(os.getpid()) + '.tmp')
    try:
        temporary.write_text(json.dumps(record) + '\n')
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)
    return destination
