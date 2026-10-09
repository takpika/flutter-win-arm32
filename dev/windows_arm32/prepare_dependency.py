"""Verify and apply a port patch to an exact upstream dependency revision."""
import hashlib
import json
from pathlib import Path
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def prepare_dependency(checkout, dependency, verify_only=False):
    patches = Path(__file__).resolve().parent / 'patches'
    manifest = json.loads((patches / f'{dependency}-source-manifest.json').read_text())
    revision = subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=checkout, text=True).strip()
    if revision != manifest['upstreamRevision']:
        raise ValueError(f"Expected {dependency} {manifest['upstreamRevision']}, got {revision}")
    for item in manifest.get('restoredFiles', []):
        if digest(checkout / item['path']) != item['upstreamSha256']:
            raise ValueError('Restored upstream source differs: ' + item['path'])
    for path in manifest.get('removedFiles', []):
        if (checkout / path).exists():
            raise ValueError('Retired port source is still present: ' + path)
    files = manifest['files']
    if all(digest(checkout / item['path']) == item['patchedSha256'] for item in files):
        return 'Already prepared; all patched source hashes match'
    if verify_only:
        raise ValueError(f'{dependency} sources do not match the prepared port')
    mismatches = [item['path'] for item in files
                  if digest(checkout / item['path']) != item['originalSha256']]
    if mismatches:
        raise ValueError(f'Refusing to replace modified {dependency} sources: ' + ', '.join(mismatches))
    patch = str(patches / f'{dependency}-windows-arm32.patch')
    subprocess.run(['git', 'apply', '--check', patch], cwd=checkout, check=True)
    subprocess.run(['git', 'apply', patch], cwd=checkout, check=True)
    mismatches = [item['path'] for item in files
                  if digest(checkout / item['path']) != item['patchedSha256']]
    if mismatches:
        raise ValueError('Patched source verification failed: ' + ', '.join(mismatches))
    return f'Prepared and verified {len(files)} shared RT/Phone {dependency} sources'
