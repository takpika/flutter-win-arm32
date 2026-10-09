"""Bind a foreign Rust distribution to a runnable, source-matched compiler.

Rustup's channel manifest describes every host. Matching the parsed manifests
and the executing reference's commit lets preparation inspect common ARM32
targets without executing, or inventing output from, the destination compiler.
Destination execution remains a separate SDK validation requirement.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tomllib

from package_compiler import sha
from sdk_hosts import current_host, verify_native_host

CHANNEL_PATH = Path('lib/rustlib/multirust-channel-manifest.toml')
SOURCE_VERSION = re.compile(r'([0-9]+\.[0-9]+\.[0-9]+) \(([0-9a-f]{7,40}) ([0-9]{4}-[0-9]{2}-[0-9]{2})\)')


def validate_target(upstream):
    expected = {'arch': 'arm', 'binary-format': 'coff', 'env': 'msvc',
                'llvm-target': 'thumbv7a-pc-windows-msvc'}
    if (any(upstream.get(key) != value for key, value in expected.items()) or
            upstream.get('target-pointer-width') not in (32, '32')):
        raise ValueError('Reference does not describe the expected upstream ARM32 target')


def load(rustc):
    """Read cross-preparation evidence bound to this installed distribution."""
    rustc = rustc.resolve()
    root = rustc.parent.parent
    record = json.loads((root / 'rust-distribution-provenance.json').read_text())
    if (record.get('crossPreparation') is not True or
            record.get('compilerExecutionVerified') is not False or
            record.get('compilerIdentity') is not None):
        raise ValueError('Expected explicit cross-preparation evidence')
    from sdk_hosts import hosts
    selected = [host for host in hosts() if host['rustHost'] == record.get('host')]
    if len(selected) != 1 or selected[0]['os'] != 'macos':
        raise ValueError('Unknown cross-prepared Rust host')
    verify_native_host(rustc, selected[0])
    for relative, expected in record['files'].items():
        path = root / relative
        if (Path(relative).is_absolute() or not path.resolve().is_relative_to(root) or
                sha(path) != expected):
            raise ValueError('Cross-prepared Rust identity file changed: ' + relative)
    if record['files'][str(CHANNEL_PATH)] != record['channelManifestSha256']:
        raise ValueError('Rust channel differs from its preparation evidence')
    channel = tomllib.loads((root / CHANNEL_PATH).read_text())
    canonical = json.dumps(channel, sort_keys=True, separators=(',', ':')).encode()
    if hashlib.sha256(canonical).hexdigest() != record['channelManifestContentsSha256']:
        raise ValueError('Rust channel contents differ from preparation')
    fields = dict(line.split(': ', 1) for line in record['referenceCompilerIdentity'].splitlines()
                  if ': ' in line)
    declared = SOURCE_VERSION.fullmatch(channel['pkg']['rustc']['version'])
    if (fields.get('release') != record['rustVersion'] or
            fields.get('host') != record['referenceCompilerHost'] or
            not declared or declared[1] != record['rustVersion'] or
            not fields.get('commit-hash', '').startswith(declared[2]) or
            fields.get('commit-date') != declared[3]):
        raise ValueError('Cross-prepared Rust source identity differs')
    validate_target(record['upstreamTarget'])
    return record


def inspect(rustc, version, host, reference_rustc):
    rustc, reference_rustc = rustc.resolve(), reference_rustc.resolve()
    if host['os'] != 'macos':
        raise ValueError('Foreign Rust preparation currently supports macOS SDK hosts')
    if rustc == reference_rustc:
        raise ValueError('The reference must be distinct from the destination compiler')
    verify_native_host(rustc, host)
    reference_host = current_host()
    if reference_host['id'] == host['id']:
        raise ValueError('Use ordinary native preparation for the executing SDK host')
    verify_native_host(reference_rustc, reference_host)
    manifests = []
    manifest_hashes = []
    for compiler in (rustc, reference_rustc):
        path = compiler.parent.parent / CHANNEL_PATH
        if not path.is_file() or path.stat().st_size > 8 * 1024**2:
            raise ValueError('Supply complete Rustup installations with channel manifests')
        content = path.read_bytes()
        manifests.append(tomllib.loads(content.decode()))
        manifest_hashes.append(hashlib.sha256(content).hexdigest())
    channel, reference_channel = manifests
    # Rustup can serialize TOML table keys in a different order on each host.
    if channel != reference_channel or channel.get('manifest-version') != '2':
        raise ValueError('Destination and reference Rust channels differ')
    declaration = channel['pkg']['rustc']['version']
    match = SOURCE_VERSION.fullmatch(declaration)
    if not match or match[1] != version:
        raise ValueError('Unexpected Rust channel version')
    for component in ('rustc', 'cargo', 'rust-std'):
        target = channel['pkg'][component]['target'].get(host['rustHost'], {})
        if target.get('available') is not True or not target.get('xz_hash'):
            raise ValueError('Rust channel lacks a complete destination component: ' + component)
    if channel['pkg']['rust-src']['target']['*'].get('available') is not True:
        raise ValueError('Rust channel lacks its source component')
    reference_digest = sha(reference_rustc)
    info = subprocess.check_output([str(reference_rustc), '-vV'], text=True)
    fields = dict(line.split(': ', 1) for line in info.splitlines() if ': ' in line)
    if (fields.get('release') != version or fields.get('host') != reference_host['rustHost'] or
            not fields.get('commit-hash', '').startswith(match[2]) or
            fields.get('commit-date') != match[3]):
        raise ValueError('Executing reference differs from the declared Rust source')
    upstream = json.loads(subprocess.check_output([
        str(reference_rustc), '-Zunstable-options', '--print', 'target-spec-json',
        '--target', 'thumbv7a-pc-windows-msvc'],
        env=dict(os.environ, RUSTC_BOOTSTRAP='1'), text=True))
    validate_target(upstream)
    if (sha(reference_rustc) != reference_digest or
            any(sha(compiler.parent.parent / CHANNEL_PATH) != digest
                for compiler, digest in zip((rustc, reference_rustc), manifest_hashes))):
        raise ValueError('Rust reference inputs changed during inspection')
    canonical = json.dumps(channel, sort_keys=True, separators=(',', ':')).encode()
    return {'rustVersion': version, 'host': host['rustHost'],
            'compilerIdentity': None, 'compilerExecutionVerified': False,
            'referenceCompilerIdentity': info, 'referenceCompilerSha256': reference_digest,
            'referenceCompilerHost': fields['host'],
            'channelManifestSha256': manifest_hashes[0],
            'channelManifestContentsSha256': hashlib.sha256(canonical).hexdigest(),
            'upstreamTarget': upstream, 'crossPreparation': True}
