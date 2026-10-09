#!/usr/bin/env python3
"""Select SDK host inputs pinned to the official Flutter release matrix.

The host is the machine running Flutter, independently of its ARM32 target.
Listing a host here describes the required distribution, not a validated SDK.
"""
import argparse
import json
import platform
from pathlib import Path


def hosts():
    return json.loads(Path(__file__).with_name('sdk-hosts.json').read_text())['hosts']


def current_host():
    system = {'Linux': 'linux', 'Darwin': 'macos', 'Windows': 'windows'}.get(platform.system())
    cpu = {'x86_64': 'x64', 'amd64': 'x64', 'arm64': 'arm64',
           'aarch64': 'arm64'}.get(platform.machine().lower())
    return select_host(f'{system}-{cpu}')


def select_host(identity):
    for host in hosts():
        if host['id'] == identity:
            return host
    raise ValueError('Host has no official SDK in this Flutter version: ' + identity)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', help='Explicit host identity for cross packaging')
    args = parser.parse_args()
    print(json.dumps(select_host(args.host) if args.host else {'include': hosts()}))
