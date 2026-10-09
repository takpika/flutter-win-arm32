#!/usr/bin/env python3
import argparse
import re
import subprocess
import tempfile
from pathlib import Path


TEXT_RE = re.compile(
    r"\[\s*\d+\]\s+\.text\s+\S+\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)"
)
COFF_TEXT_RE = re.compile(
    r"Name:\s+\.text\b(?:(?!Section \{).)*?RawDataSize:\s+([0-9]+)",
    re.S,
)


def run(cmd):
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def text_info(path, readelf):
    result = run([readelf, "-S", str(path)])
    if result.returncode != 0:
        raise SystemExit(result.stderr)
    for line in result.stdout.splitlines():
        match = TEXT_RE.search(line)
        if match:
            return tuple(int(group, 16) for group in match.groups())
    match = COFF_TEXT_RE.search(result.stdout)
    if match:
        return 0, 0, int(match.group(1), 10)
    raise SystemExit(f"no .text section in {path}")


def dump_text(path, objcopy, out):
    result = run([objcopy, "--strip-all", f"--dump-section", f".text={out}", str(path),
                  str(out) + ".reencoded"])
    if result.returncode != 0:
        raise SystemExit(result.stderr)
    data = Path(out).read_bytes()
    Path(out).unlink()
    Path(str(out) + ".reencoded").unlink()
    return data


def parse_range(item):
    start, stop = item.split(":", 1)
    return int(start, 16), int(stop, 16)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("original_app_so")
    parser.add_argument("thumb_obj")
    parser.add_argument("patched_app_so")
    parser.add_argument("--range", action="append", required=True)
    parser.add_argument("--readelf", default="llvm-readelf")
    parser.add_argument("--objcopy", default="llvm-objcopy")
    args = parser.parse_args()

    ranges = [parse_range(item) for item in args.range]
    base = ranges[0][0]

    patched_addr, _patched_off, patched_size = text_info(args.patched_app_so, args.readelf)
    original_addr, _original_off, original_size = text_info(args.original_app_so, args.readelf)
    thumb_addr, _thumb_off, thumb_size = text_info(args.thumb_obj, args.readelf)

    if patched_addr != original_addr or patched_size != original_size:
        raise SystemExit(
            f"patched .text shape changed: original=0x{original_addr:x}/0x{original_size:x} "
            f"patched=0x{patched_addr:x}/0x{patched_size:x}"
        )
    if thumb_addr not in (0, base):
        raise SystemExit(f"thumb object .text addr 0x{thumb_addr:x} != 0 or first code range 0x{base:x}")

    with tempfile.TemporaryDirectory(prefix="thumb-check-", dir=Path(args.patched_app_so).resolve().parent) as td:
        original_text = dump_text(args.original_app_so, args.objcopy, Path(td) / "original.text")
        patched_text = dump_text(args.patched_app_so, args.objcopy, Path(td) / "patched.text")
        thumb_text = dump_text(args.thumb_obj, args.objcopy, Path(td) / "thumb.text")

    patched_bytes = 0
    mismatches = []
    preserved_gaps = []
    cursor = ranges[0][0]
    for start, stop in ranges:
        if start > cursor:
            a = cursor - patched_addr
            b = start - patched_addr
            if original_text[a:b] != patched_text[a:b]:
                mismatches.append((cursor, start, "gap-not-preserved"))
            preserved_gaps.append((cursor, start))
        a = start - patched_addr
        b = stop - patched_addr
        ta = start - base
        tb = stop - base
        if thumb_text[ta:tb] != patched_text[a:b]:
            mismatches.append((start, stop, "code-range-not-thumb-object"))
        patched_bytes += stop - start
        cursor = stop

    if mismatches:
        for start, stop, reason in mismatches[:20]:
            print(f"mismatch 0x{start:x}:0x{stop:x} {reason}")
        raise SystemExit("VERDICT=fail")

    print(f"input={args.patched_app_so}")
    print(f"text_addr=0x{patched_addr:x}")
    print(f"text_size=0x{patched_size:x}")
    print(f"thumb_obj_text_addr=0x{thumb_addr:x}")
    print(f"thumb_obj_text_size=0x{thumb_size:x}")
    print(f"patched_code_bytes=0x{patched_bytes:x}")
    for start, stop in preserved_gaps:
        if start != stop:
            print(f"preserved_non_code_gap=0x{start:x}:0x{stop:x}")
    print("VERDICT=pass")


if __name__ == "__main__":
    raise SystemExit(main())
