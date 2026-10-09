#!/usr/bin/env python3
"""Select SDK host inputs pinned to the official Flutter release matrix.

The host is the machine running Flutter, independently of its ARM32 target.
Listing a host here describes the required distribution, not a validated SDK.
"""
import argparse
import json
import platform
from pathlib import Path
import struct


def hosts():
    return json.loads(Path(__file__).with_name('sdk-hosts.json').read_text())['hosts']


def artifact_host_cpu(report):
    """Identify the engine build host independently of the destination SDK."""
    cpus = set()
    for family in report['families'].values():
        found = [cpu for cpu in ('x64', 'arm64')
                 if 'clang_' + cpu + '/gen_snapshot' in family['artifacts']]
        if len(found) != 1:
            raise ValueError('Engine build must identify exactly one host generator')
        cpus.update(found)
    if len(cpus) != 1:
        raise ValueError('RT and Phone engine build host CPUs differ')
    return cpus.pop()


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


def verify_native_host(path, host):
    """Reject a host tool renamed from a different OS or architecture."""
    with Path(path).open('rb') as source:
        header = source.read(4096)
        if header[:2] == b'MZ' and len(header) >= 64:
            source.seek(int.from_bytes(header[60:64], 'little'))
            pe = source.read(6)
            actual = {'windows-x64'} if pe == b'PE\0\0\x64\x86' else set()
        elif header[:4] == b'\x7fELF' and len(header) >= 20:
            actual = {'linux-x64'} if header[4:6] == b'\x02\x01' and header[18:20] == b'\x3e\0' else set()
        else:
            cpus = set()
            if header[:4] == bytes.fromhex('cffaedfe') and len(header) >= 8:
                cpus.add(struct.unpack_from('<I', header, 4)[0])
            elif header[:4] in (bytes.fromhex('cafebabe'), bytes.fromhex('cafebabf'),
                                bytes.fromhex('bebafeca'), bytes.fromhex('bfbafeca')):
                endian = '>' if header[0] == 0xca else '<'
                count = struct.unpack_from(endian + 'I', header, 4)[0]
                width = 32 if header[:4] in (bytes.fromhex('cafebabf'), bytes.fromhex('bfbafeca')) else 20
                if count > 64 or len(header) < 8 + count * width:
                    raise ValueError('Invalid universal Mach-O header: ' + str(path))
                cpus = {struct.unpack_from(endian + 'I', header, 8 + i * width)[0] for i in range(count)}
            actual = {name for cpu, name in ((0x01000007, 'macos-x64'), (0x0100000c, 'macos-arm64')) if cpu in cpus}
    if host['id'] not in actual:
        raise ValueError('Native tool does not match SDK host ' + host['id'] + ': ' + str(path))
    return actual


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', help='Explicit host identity for cross packaging')
    args = parser.parse_args()
    print(json.dumps(select_host(args.host) if args.host else {'include': hosts()}))
