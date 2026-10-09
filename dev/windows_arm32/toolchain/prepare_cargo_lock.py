#!/usr/bin/env python3
"""Keep the original Cargo graph pinned while using SDK dependency patches."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tomllib


def cargo_lock_options(cargo, resolver):
    # Cargo before 1.95 accepts the unstable CLI path; newer Cargo moved it
    # into resolver configuration. Select by the actual installed executable.
    version = subprocess.check_output([str(cargo), "--version"], text=True).split()[1]
    major, minor = (int(value) for value in version.split(".")[:2])
    if (major, minor) < (1, 95):
        lock = tomllib.loads(resolver.read_text())["resolver"]["lockfile-path"]
        return ["-Z", "unstable-options", "--lockfile-path", lock]
    return ["-Z", "lockfile-path", "-Z", "json-target-spec", "--config", str(resolver)]

def prepare(cargo, manifest, output, target, patches, environment):
    manifest = manifest.resolve()
    manifest_hash = hashlib.sha256(manifest.read_bytes()).hexdigest()
    # No dependency resolution or build scripts are run on the original lock.
    # Cargo's locked metadata lookup identifies its actual workspace location.
    probe = subprocess.run([str(cargo), 'metadata', '--no-deps', '--format-version',
                            '1', '--locked', '--manifest-path', str(manifest)],
                           env=environment, text=True, capture_output=True, check=True)
    workspace = Path(json.loads(probe.stdout)['workspace_root'])
    original = workspace / 'Cargo.lock'
    if not original.is_file():
        raise ValueError('SDK dependency patches require an original Cargo.lock')
    original_bytes = original.read_bytes()
    output = output.resolve()
    sidecar = output / 'Cargo.lock'
    if sidecar == original.resolve() or sidecar.is_symlink():
        raise ValueError('SDK lockfile must be separate from the original lockfile')
    output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(original, sidecar)
    resolver = output / 'resolver.toml'
    resolver.write_text('[resolver]\nlockfile-path = ' +
                        json.dumps(str(sidecar), ensure_ascii=False) + '\n')
    arguments = [str(cargo), 'metadata', '--format-version', '1',
                 '--filter-platform', target, '--manifest-path', str(manifest),
                 *cargo_lock_options(cargo, resolver)]
    allowed = set()
    for patch in patches:
        arguments += ['--config', str(patch)]
        for registry in tomllib.loads(patch.read_text()).get('patch', {}).values():
            for entry in registry.values():
                if isinstance(entry, dict) and 'path' in entry:
                    directory = Path(entry['path'])
                    if not directory.is_absolute():
                        directory = patch.parent.parent / directory
                    package = tomllib.loads((directory / 'Cargo.toml').read_text())['package']
                    allowed.add((package['name'], package['version']))
    result = subprocess.run(arguments, env=environment, text=True,
                            capture_output=True, check=True)
    def entries(data):
        packages = tomllib.loads(data)['package']
        values = {(entry['name'], entry['version']): entry for entry in packages}
        if len(values) != len(packages):
            raise ValueError('SDK lock validation needs distinct name/version package identities')
        return values
    before, after = entries(original_bytes.decode()), entries(sidecar.read_text())
    if before.keys() != after.keys():
        raise ValueError('SDK patches changed locked dependency names or versions')
    substitutions = []
    for key, entry in before.items():
        if entry != after[key]:
            if key not in allowed:
                raise ValueError('SDK patches changed an unpatched lock entry: ' + str(key))
            expected = {name: value for name, value in entry.items()
                        if name not in ('source', 'checksum')}
            if expected != after[key]:
                raise ValueError('SDK patch changed more than its source: ' + str(key))
            substitutions.append({'name': key[0], 'version': key[1]})
    if (original.read_bytes() != original_bytes or
            hashlib.sha256(manifest.read_bytes()).hexdigest() != manifest_hash):
        raise RuntimeError('Original Cargo manifest or lockfile changed')
    (output / 'sdk-lock-provenance.json').write_text(json.dumps({
        'originalManifest': str(manifest), 'originalLockfile': str(original),
        'originalLockfileSha256': hashlib.sha256(original_bytes).hexdigest(),
        'originalFilesUnchanged': True, 'dependencyNamesAndVersionsUnchanged': True,
        'packageCount': len(before), 'sdkSubstitutions': substitutions,
    }, indent=2) + '\n')
    (output / 'metadata-diagnostics.txt').write_text(result.stderr)
    return resolver
