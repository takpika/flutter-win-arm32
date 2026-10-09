#!/usr/bin/env python3
"""Verify all official SDK hosts and their common physical runtime evidence."""
import argparse
import json
from pathlib import Path
import tempfile

from sdk_hosts import hosts, select_host, verify_native_host
from verify_release import archive_members, verify


def verify_source_build(run, repository, approved_revision, host):
    """Bind a producer to the currently published port commit, not a branch label."""
    workflow = ('windows-arm32-engine.yml' if host == 'linux-x64' else
                'windows-arm32-windows-sdk.yml' if host == 'windows-x64' else
                'windows-arm32-macos-sdk.yml' if host in ('macos-x64', 'macos-arm64') else None)
    if (workflow is None or run.get('status') != 'completed' or run.get('conclusion') != 'success'
            or run.get('event') not in ('push', 'workflow_dispatch')
            or run.get('head_repository', {}).get('full_name') != repository
            or run.get('head_sha') != approved_revision
            or run.get('path') != '.github/workflows/' + workflow):
        raise ValueError('SDK producer differs from the approved source, repository or workflow')


def verify_set(manifest, output):
    manifest, output = manifest.resolve(), output.resolve()
    records = json.loads(manifest.read_text())
    expected = {host['id'] for host in hosts()}
    entries = records.get('hosts', [])
    identities = [entry['host'] for entry in entries]
    if (records.get('schemaVersion') != 1 or len(identities) != len(expected)
            or set(identities) != expected):
        raise ValueError('Provide exactly one validated archive for every official SDK host')
    reports = {}
    common_revision = None
    common_runtime = None
    output.parent.mkdir(parents=True, exist_ok=True)
    for entry in entries:
        host = select_host(entry['host'])
        archive = (manifest.parent / entry['archive']).resolve()
        validation = (manifest.parent / entry['validationRecord']).resolve()
        report = verify(archive, validation, entry['buildRunId'], entry['sourceRevision'])
        suffix = '.exe' if host['os'] == 'windows' else ''
        bootstrap_name = 'flutter/bin/cache/windows-arm32-host-bootstrap.json'
        binaries = {
            'flutter/bin/cache/dart-sdk/bin/dart' + suffix,
            'flutter/bin/cache/artifacts/engine/windows-arm-release/gen_snapshot_raw' + suffix,
        }
        found = set()
        bootstrap = None
        with tempfile.TemporaryDirectory(prefix='sdk-host-audit-', dir=output.parent) as scratch:
            for name, link, regular, size, read in archive_members(archive):
                if name != bootstrap_name and name not in binaries:
                    continue
                if not regular or link is not None or size > 256 * 1024 * 1024:
                    raise ValueError('Invalid SDK host metadata or executable: ' + name)
                if name == bootstrap_name:
                    if size > 1024 * 1024:
                        raise ValueError('Invalid SDK host bootstrap record')
                    bootstrap = json.loads(read())
                else:
                    path = Path(scratch) / Path(name).name
                    path.write_bytes(read())
                    verify_native_host(path, host)
                    found.add(name)
        if (found != binaries or not bootstrap or bootstrap.get('sdkHost') != host['id']
                or bootstrap.get('sourceRevision') != entry['sourceRevision']):
            raise ValueError('SDK archive does not match its declared host or source revision')
        if common_revision is None:
            common_revision = report['sourceRevision']
            common_runtime = report['physicalRuntimeDigests']
        elif (common_revision != report['sourceRevision']
              or common_runtime != report['physicalRuntimeDigests']):
            raise ValueError('SDK hosts must share one source revision and identical RT/Phone runtimes')
        reports[host['id']] = report
    result = {'schemaVersion': 1, 'sourceRevision': common_revision,
              'hosts': reports, 'allOfficialHostsVerified': True,
              'commonRuntimeBytesVerified': True, 'releaseReady': True}
    output.write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    verify_set(args.manifest, args.output)
