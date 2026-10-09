#!/usr/bin/env python3
import argparse
import re
import struct
import subprocess
import tempfile
from pathlib import Path


def run(cmd):
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def text_info(objdump, path):
    result = run([objdump, "-h", path])
    if result.returncode != 0:
        raise SystemExit(result.stderr)
    for line in result.stdout.splitlines():
        match = re.match(r"\s*\d+\s+\.text\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+", line)
        if match:
            size = int(match.group(1), 16)
            addr = int(match.group(2), 16)
            return addr, size
    raise SystemExit(f"{path}: .text section not found")


def dump_text(objcopy, path, out):
    # objcopy defaults to rewriting its input when no output is supplied.
    result = run([objcopy, "--strip-all", "--dump-section", f".text={out}", path,
                  str(out) + ".reencoded"])
    if result.returncode != 0:
        raise SystemExit(result.stderr)
    Path(str(out) + ".reencoded").unlink()


def elf_text_offset(data):
    if data[:6] != b"\x7fELF\x01\x01":
        raise SystemExit("expected little-endian ELF32 input")
    section_offset = struct.unpack_from("<I", data, 32)[0]
    entry_size, count, names_index = struct.unpack_from("<HHH", data, 46)
    sections = [struct.unpack_from("<10I", data, section_offset + i * entry_size)
                for i in range(count)]
    names = sections[names_index]
    strings = data[names[4]:names[4] + names[5]]
    for section in sections:
        if strings[section[0]:].split(b"\0", 1)[0] == b".text":
            return section[4], section[5]
    raise SystemExit("no ELF .text section")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("app_so")
    parser.add_argument("thumb_obj")
    parser.add_argument("out_so")
    parser.add_argument("--range", action="append", required=True)
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument("--objcopy", default="llvm-objcopy")
    args = parser.parse_args()

    ranges = []
    for item in args.range:
        start_s, stop_s = item.split(":", 1)
        ranges.append((int(start_s, 16), int(stop_s, 16)))
    base = min(start for start, _ in ranges)

    text_addr, text_size = text_info(args.objdump, args.app_so)
    thumb_addr, thumb_size = text_info(args.objdump, args.thumb_obj)
    if thumb_addr != 0:
        raise SystemExit(f"{args.thumb_obj}: expected .text VMA 0, got 0x{thumb_addr:x}")

    with tempfile.TemporaryDirectory(prefix="thumb-patch-", dir=Path(args.out_so).resolve().parent) as tmp:
        tmp = Path(tmp)
        original_text = tmp / "original.text"
        thumb_text = tmp / "thumb.text"
        patched_text = tmp / "patched.text"
        dump_text(args.objcopy, args.app_so, original_text)
        dump_text(args.objcopy, args.thumb_obj, thumb_text)

        original = bytearray(original_text.read_bytes())
        thumb = thumb_text.read_bytes()
        if len(original) != text_size:
            raise SystemExit(f"unexpected original .text size: {len(original)} != {text_size}")
        if len(thumb) != thumb_size:
            raise SystemExit(f"unexpected thumb .text size: {len(thumb)} != {thumb_size}")

        patched = 0
        for start, stop in ranges:
            if start < text_addr or stop > text_addr + text_size:
                raise SystemExit(f"range outside .text: 0x{start:x}:0x{stop:x}")
            src_start = start - base
            src_stop = stop - base
            dst_start = start - text_addr
            dst_stop = stop - text_addr
            if src_stop > len(thumb):
                raise SystemExit(f"thumb object too small for range 0x{start:x}:0x{stop:x}")
            original[dst_start:dst_stop] = thumb[src_start:src_stop]
            patched += stop - start

        # Preserve every header, symbol, data byte and section offset. objcopy
        # --update-section may otherwise canonicalize unrelated ELF metadata.
        image = bytearray(Path(args.app_so).read_bytes())
        offset, size = elf_text_offset(image)
        if size != text_size:
            raise SystemExit("ELF .text size disagrees with objdump")
        image[offset:offset + size] = original
        Path(args.out_so).write_bytes(image)

    print(f"input={args.app_so}")
    print(f"thumb_obj={args.thumb_obj}")
    print(f"output={args.out_so}")
    print(f"text_addr=0x{text_addr:x}")
    print(f"text_size=0x{text_size:x}")
    print(f"patched_bytes=0x{patched:x}")


if __name__ == "__main__":
    raise SystemExit(main())
