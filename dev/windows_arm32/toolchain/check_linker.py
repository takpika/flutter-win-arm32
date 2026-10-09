#!/usr/bin/env python3
"""Check COFF code, data and forwarder exports across Windows architectures."""
import argparse
import os
from pathlib import Path
import struct
import subprocess


def exports(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from('<H', data, optional)[0]
    directory = optional + (96 if magic == 0x10b else 112)
    export_rva, export_size = struct.unpack_from('<II', data, directory)
    sections = optional + optional_size

    def offset(rva):
        for index in range(count):
            size, address, raw_size, raw = struct.unpack_from(
                '<IIII', data, sections + index * 40 + 8)
            if address <= rva < address + max(size, raw_size):
                return raw + rva - address
        raise ValueError('RVA outside image sections')

    def text(rva):
        start = offset(rva)
        return data[start:data.index(b'\0', start)].decode('ascii')

    table = offset(export_rva)
    _, _, _, _, _, _, _, names_count, addresses, names, ordinals = \
        struct.unpack_from('<IIHHIIIIIII', data, table)
    result = {}
    for index in range(names_count):
        name = text(struct.unpack_from('<I', data, offset(names) + index * 4)[0])
        ordinal = struct.unpack_from('<H', data, offset(ordinals) + index * 2)[0]
        rva = struct.unpack_from('<I', data, offset(addresses) + ordinal * 4)[0]
        result[name] = text(rva) if export_rva <= rva < export_rva + export_size else rva
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--llvm-bin', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    tools, output = args.llvm_bin.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    temporary = output / 'tmp'
    temporary.mkdir(exist_ok=True)
    env = dict(os.environ, TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary))
    targets = [('x86', 'i686-pc-windows-msvc', 'ret'),
               ('x64', 'x86_64-pc-windows-msvc', 'ret'),
               ('arm64', 'aarch64-pc-windows-msvc', 'ret'),
               ('arm', 'thumbv7-pc-windows-msvc', 'bx lr')]
    forwarded = {'forward1': 'one.FuncA', 'forward2': 'two.FuncBB',
                 'forward3': 'three.FuncCCC'}
    for name, target, instruction in targets:
        prefix = '_' if name == 'x86' else ''
        source = output / (name + '.s')
        features = '.def @feat.00; .scl 3; .type 0; .endef\n.set @feat.00, 1\n' if name == 'x86' else ''
        source.write_text(features + '.text\n.p2align 2\n.globl ' + prefix + 'callable\n' +
                          prefix + 'callable:\n  ' + instruction + '\n' +
                          '.data\n.p2align 2\n.globl ' + prefix + 'exported_data\n' +
                          prefix + 'exported_data:\n  .long 7\n')
        obj, dll = output / (name + '.obj'), output / (name + '.dll')
        subprocess.run([str(tools / 'llvm-mc'), '-triple=' + target,
                        '-filetype=obj', str(source), '-o', str(obj)], env=env, check=True)
        subprocess.run([str(tools / 'lld-link'), '/dll', '/noentry', '/out:' + str(dll),
                        str(obj), '/export:callable', '/export:exported_data,DATA',
                        *['/export:' + key + '=' + value for key, value in forwarded.items()]],
                       env=env, check=True)
        actual = exports(dll)
        assert all(actual[key] == value for key, value in forwarded.items()), actual
        assert isinstance(actual['callable'], int) and actual['callable'] & 1 == (name == 'arm'), actual
        assert isinstance(actual['exported_data'], int) and actual['exported_data'] & 1 == 0, actual
        print(name + ': code/data address bits and exact forwarder strings passed', flush=True)


if __name__ == '__main__':
    main()
