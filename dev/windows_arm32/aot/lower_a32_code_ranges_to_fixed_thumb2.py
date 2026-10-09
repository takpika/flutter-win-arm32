#!/usr/bin/env python3
import argparse
import re
import subprocess
from pathlib import Path


OBJDUMP_RE = re.compile(r"^\s*([0-9a-fA-F]+):\s+([0-9a-fA-F]{8})\s+(.+)$")
BRANCH_RE = re.compile(
    r"^(b|beq|bne|bhs|blo|bhi|bls|bgt|blt|bge|ble|bmi|bpl|bvs|bvc|bl)\s+0x([0-9a-fA-F]+)"
)
PC_MOV_RE = re.compile(r"^mov\s+(r[0-9]+|r1[0-4]|ip|lr|r12),\s*pc$")


def run(cmd, **kwargs):
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)


def disassemble(objdump, path, start, stop):
    result = run(
        [
            objdump,
            "-D",
            "--triple=armv7",
            f"--start-address=0x{start:x}",
            f"--stop-address=0x{stop:x}",
            path,
        ]
    )
    if result.returncode != 0:
        raise SystemExit(result.stderr)
    rows = []
    for line in result.stdout.splitlines():
        match = OBJDUMP_RE.match(line)
        if not match:
            continue
        addr, word, asm = match.groups()
        asm = asm.split("@", 1)[0].strip().replace("\t", " ")
        rows.append((int(addr, 16), word.lower(), asm))
    return rows


def label_for(addr):
    return f"L_{addr:08x}"


def normalize(addr, asm):
    if asm == "<unknown>":
        raise SystemExit(f"Cannot translate unknown A32 instruction at 0x{addr:x}")
    pc_mov = PC_MOV_RE.match(asm)
    if pc_mov:
        dst = pc_mov.group(1)
        target = addr + 8
        if dst == "lr":
            return f"adr.w {dst}, {label_for(target)} + 1"
        return f"adr.w {dst}, {label_for(target)}"
    branch = BRANCH_RE.match(asm)
    if branch:
        op, target = branch.groups()
        target_addr = int(target, 16)
        if op == "bl":
            return f"bl {label_for(target_addr)}"
        return f"{op}.w {label_for(target_addr)}"
    if asm == "sub sp, r11, #0":
        return "mov sp, r11"
    if asm == "pop {r11, lr}":
        return "pop.w {r11, lr}"
    return asm


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("app_so")
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument("--llvm-mc", default="llvm-mc")
    parser.add_argument("--objcopy", default="llvm-objcopy")
    parser.add_argument("--range", action="append", required=True, help="code range as start:stop hex")
    parser.add_argument("--out-asm", required=True)
    parser.add_argument("--out-obj", required=True)
    parser.add_argument("--assembly-stdin", action="store_true",
                        help="Pass assembly directly to LLVM instead of storing a temporary file")
    args = parser.parse_args()

    ranges = []
    for item in args.range:
        start_s, stop_s = item.split(":", 1)
        ranges.append((int(start_s, 16), int(stop_s, 16)))

    all_rows = []
    for start, stop in ranges:
        rows = disassemble(args.objdump, args.app_so, start, stop)
        all_rows.extend((start, stop, addr, word, asm) for addr, word, asm in rows)

    lines = [".syntax unified", ".thumb", ".text"]

    current_range = None
    emitted = 0
    for start, stop, addr, _word, asm in all_rows:
        if current_range != (start, stop):
            current_range = (start, stop)
            lines.append(f".org 0x{start:x} - 0x{ranges[0][0]:x}")
        lines.append(f".org 0x{addr:x} - 0x{ranges[0][0]:x}")
        lines.append(f"{label_for(addr)}:")
        lowered = normalize(addr, asm)
        lines.append(f"  {lowered}")
        lines.append("  .p2align 2")
        lines.append(f".org 0x{addr + 4:x} - 0x{ranges[0][0]:x}")
        emitted += 1

    assembly = "\n".join(lines) + "\n"
    if args.assembly_stdin:
        result = run([args.llvm_mc, "-triple=thumbv7-windows-msvc", "-filetype=obj",
                      "-", "-o", args.out_obj], input=assembly)
    else:
        Path(args.out_asm).write_text(assembly, encoding="utf-8")
        result = run([args.llvm_mc, "-triple=thumbv7-windows-msvc", "-filetype=obj",
                      args.out_asm, "-o", args.out_obj])
    print(f"emitted={emitted}")
    print(f"out_asm={args.out_asm}")
    print(f"out_obj={args.out_obj}")
    print(f"assemble_returncode={result.returncode}")
    if result.stdout:
        print(result.stdout)
    if result.stderr:
        print(result.stderr)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
