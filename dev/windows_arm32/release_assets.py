"""Transport large verified SDK archives within GitHub's per-asset size limit."""
import hashlib
import json
from pathlib import Path
import re

from verify_release import digest


def prepare_assets(archive, maximum=2 * 1024**3 - 1, part_size=1024**3):
    archive = Path(archive)
    if archive.stat().st_size <= maximum:
        return [archive], ''
    if not 0 < part_size <= maximum or not re.fullmatch(r'[A-Za-z0-9._-]+', archive.name):
        raise ValueError('Invalid release part size or archive name')
    expected = digest(archive)
    parts = []
    combined = hashlib.sha256()
    with archive.open('rb') as source:
        while source.tell() < archive.stat().st_size:
            path = archive.with_name(archive.name + '.part' + str(len(parts) + 1).zfill(3))
            checksum = hashlib.sha256()
            with path.open('xb') as output:
                remaining = part_size
                while remaining:
                    block = source.read(min(remaining, 1024 * 1024))
                    if not block:
                        break
                    output.write(block)
                    checksum.update(block)
                    remaining -= len(block)
            parts.append({'name': path.name, 'bytes': path.stat().st_size,
                          'sha256': checksum.hexdigest()})
    # Verify the stored transport bytes, not only the data sent to write().
    for part in parts:
        path = archive.with_name(part['name'])
        if digest(path) != part['sha256']:
            raise ValueError('Release part checksum failed')
        with path.open('rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                combined.update(block)
    if combined.hexdigest() != expected:
        raise ValueError('Release parts do not reconstruct the verified SDK')
    manifest = archive.with_name(archive.name + '.parts.json')
    with manifest.open('x') as output:
        json.dump({'schemaVersion': 1, 'archive': archive.name,
                   'bytes': archive.stat().st_size, 'sha256': expected,
                   'parts': parts}, output, indent=2)
        output.write('\n')
    names = [part['name'] for part in parts]
    if archive.name.endswith('.zip') and 'windows-x64' in archive.name:
        command = 'cmd /c copy /b ' + '+'.join(names) + ' ' + archive.name
    else:
        command = 'cat ' + ' '.join(names) + ' > ' + archive.name
    instructions = ('\nDownload every `' + archive.name + '.part*` file into one directory, '
        'then reconstruct the original archive there:\n\n```text\n' + command + '\n```\n\n'
        'Verify the reconstructed archive against `' + archive.name + '.sha256` '
        'before extracting it. The parts manifest records every part checksum; '
        'the reconstructed SDK is byte-identical to the validated archive.\n')
    return [archive.with_name(name) for name in names] + [manifest], instructions
