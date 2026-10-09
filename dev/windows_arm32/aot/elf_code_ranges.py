import struct

def code_ranges(data):
    if data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected little-endian ELF32 AOT')
    section_offset = struct.unpack_from('<I', data, 32)[0]
    entry_size, count = struct.unpack_from('<HH', data, 46)
    sections = [struct.unpack_from('<10I', data, section_offset + i * entry_size)
                for i in range(count)]
    functions = []
    blobs = {}
    for section in sections:
        if section[1] not in (2, 11):
            continue
        strings_section = sections[section[6]]
        strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name_offset, value, size, info, _, index = struct.unpack_from('<IIIBBH', data, offset)
            name = strings[name_offset:].split(b'\0', 1)[0].decode(errors='replace')
            if name in ('_kDartVmSnapshotInstructions', '_kDartIsolateSnapshotInstructions'):
                blobs[name] = (value, size)
            if section[1] == 2 and info & 15 == 2 and index and size:
                functions.append((value & ~1, size))
    if len(blobs) != 2:
        raise ValueError('Both VM and isolate instruction blobs are required')
    ranges = []
    for _, (address, size) in sorted(blobs.items(), key=lambda entry: entry[1][0]):
        contained = [(a, s) for a, s in functions if address <= a and a + s <= address + size]
        if not contained:
            raise ValueError('Function symbols are required to locate generated code')
        ranges.append((min(a for a, _ in contained), max(a + s for a, s in contained)))
    return ranges
