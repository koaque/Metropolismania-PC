#!/usr/bin/env python3
"""Find likely function starts that the function map misses.

Static analysis only finds functions reached by direct calls. Functions reached through pointers
(static constructor tables, vtables, callback tables, thread entry points, interrupt handlers)
are often missing, and the runtime then stops with "missing-target" on the first indirect call.

Two scans over the boot ELF:
  data   aligned 32-bit words outside function bodies whose value points into code
  code   lui/addiu (or lui/ori) pairs inside functions that build a code address

A target is reported when it is not already a function-map entry and looks like a function start:
the word 8 bytes before it is `jr $ra` (previous function ended), or it begins with
`addiu $sp, $sp, -N`. Use --all to report every unmapped target.

Usage: find_code_pointers.py original/SLUS_203.77 config/funcmap.csv [--write config/entry_points.csv]
"""

from __future__ import annotations

import argparse
import csv
import struct
import tomllib
from pathlib import Path

JR_RA = 0x03E00008


# Opcode tables for the R5900 (EE core). Anything else is reserved and means "this is data".
VALID_OPCODES = set(range(0x00, 0x13)) | set(range(0x14, 0x1D)) | {0x1E, 0x1F} | set(range(0x20, 0x30)) | {
    0x31, 0x33, 0x36, 0x37, 0x39, 0x3E, 0x3F}
VALID_SPECIAL = {0x00, 0x02, 0x03, 0x04, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0F,
                 0x10, 0x11, 0x12, 0x13, 0x14, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B} | set(range(0x20, 0x35)) | {
    0x36, 0x38, 0x3A, 0x3B, 0x3C, 0x3E, 0x3F}
VALID_MMI = {0x00, 0x01, 0x04, 0x08, 0x09, 0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x1A, 0x1B,
             0x20, 0x21, 0x28, 0x29, 0x30, 0x31, 0x34, 0x36, 0x37, 0x3C, 0x3E, 0x3F}


def valid_instruction(w: int) -> bool:
    op = w >> 26
    if op not in VALID_OPCODES:
        return False
    if op == 0x00:
        return (w & 0x3F) in VALID_SPECIAL
    if op == 0x1C:
        return (w & 0x3F) in VALID_MMI
    return True


def decodes_as_function(addr: int, word_at, limit: int = 2048) -> bool:
    """True when every instruction from addr up to a return (jr $ra) or tail call (j) plus its
    delay slot is a valid R5900 encoding. Data such as text, floats or tables fails quickly."""
    if word_at(addr) in (None, 0):
        return False
    for i in range(limit):
        w = word_at(addr + 4 * i)
        if w is None or not valid_instruction(w):
            return False
        if w == JR_RA or w >> 26 == 0x02:
            delay = word_at(addr + 4 * i + 4)
            return delay is not None and valid_instruction(delay)
    return False


def load_segment(elf_path: Path) -> tuple[int, bytes]:
    elf = elf_path.read_bytes()
    phoff = struct.unpack_from("<I", elf, 28)[0]
    phentsize, phnum = struct.unpack_from("<HH", elf, 42)
    for i in range(phnum):
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<5I", elf, phoff + i * phentsize)
        if p_type == 1 and p_filesz:
            return p_vaddr, elf[p_offset:p_offset + p_filesz]
    raise ValueError("no loadable segment")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("elf", type=Path)
    parser.add_argument("funcmap", type=Path)
    parser.add_argument("--all", action="store_true", help="report unmapped targets without start-of-function evidence too")
    parser.add_argument("--write", type=Path, help="write candidates as an entry-points CSV")
    parser.add_argument("--layout", type=Path, default=Path("config/layout.toml"), help="layout.toml with data_ranges")
    args = parser.parse_args()

    base, seg = load_segment(args.elf)
    words = struct.unpack("<%dI" % (len(seg) // 4), seg[: len(seg) // 4 * 4])
    end = base + len(words) * 4

    rows = list(csv.DictReader(args.funcmap.open()))
    entries = {int(r["Start"], 16) for r in rows}
    bodies = sorted((int(r["Start"], 16), int(r["End"], 16)) for r in rows
                    if not r["Name"].startswith(("entry_", "caseD_", "default", "switchD")))
    in_body = bytearray(len(words))
    for start, stop in bodies:
        for a in range(max(start, base), min(stop, end), 4):
            in_body[(a - base) // 4] = 1

    def word_at(addr: int) -> int | None:
        if base <= addr < end:
            return words[(addr - base) // 4]
        return None

    def looks_like_start(addr: int) -> str | None:
        # The previous function ends with `jr $ra` or a tail call `j target`, then its delay slot,
        # then optional zero padding up to the alignment boundary.
        for back in (8, 12, 16, 20):
            prev = word_at(addr - back)
            if prev is None:
                break
            padding = [word_at(a) for a in range(addr - back + 8, addr, 4)]
            if any(p != 0 for p in padding):
                break
            if prev == JR_RA:
                return "after jr ra"
            if prev >> 26 == 0x02:
                return "after tail call"
        w = word_at(addr)
        if w is not None and (w & 0xFFFF0000) == 0x27BD0000 and (w & 0x8000):
            return "addiu sp,sp,-N"
        return None

    # Code can also live outside Ghidra's function bodies (that is the point of this tool), so any
    # aligned address in the segment counts, except ranges config/layout.toml marks as data.
    data_ranges = []
    if args.layout and args.layout.exists():
        layout = tomllib.loads(args.layout.read_text())
        data_ranges = [(int(s, 16), int(e, 16)) for s, e in layout.get("data_ranges", [])]

    def code_target(value: int) -> bool:
        return (base <= value < end and value % 4 == 0
                and not any(s <= value < e for s, e in data_ranges))

    found: dict[int, set[str]] = {}

    # Scan 1: pointers stored as data.
    for i, w in enumerate(words):
        if in_body[i] or not code_target(w) or w in entries:
            continue
        found.setdefault(w, set()).add(f"data@0x{base + i * 4:08X}")

    # Scan 2: lui/addiu and lui/ori address construction inside code.
    lui: dict[int, int] = {}
    for i, w in enumerate(words):
        if not in_body[i]:
            lui.clear()
            continue
        op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
        if op == 0x0F:  # lui
            lui[rt] = imm << 16
            continue
        if op in (0x09, 0x0D) and rs in lui:  # addiu / ori
            value = (lui[rs] + (imm - 0x10000 if (op == 0x09 and imm & 0x8000) else imm)) & 0xFFFFFFFF
            if code_target(value) and value not in entries:
                found.setdefault(value, set()).add(f"code@0x{base + i * 4:08X}")
        if op == 0x02 or op == 0x03 or (op == 0 and (w & 0x3F) in (0x08, 0x09)):
            lui.clear()  # control flow: forget tracked registers after the delay slot
        elif op not in (0x2B, 0x3F, 0x1F, 0x39, 0x29, 0x28):  # stores keep registers intact
            if op in (0x09, 0x0D, 0x0F, 0x23, 0x24, 0x25, 0x37, 0x1E):
                lui.pop(rt, None)
            elif op == 0:
                lui.pop((w >> 11) & 31, None)

    results = []
    rejected = 0
    for target in sorted(found):
        evidence = looks_like_start(target)
        if evidence and not decodes_as_function(target, word_at):
            evidence = None
            rejected += 1
        if evidence or args.all:
            results.append((target, evidence or "-", sorted(found[target])))
    print(f"(rejected {rejected} target(s) that do not decode as a valid R5900 function)")

    for target, evidence, refs in results:
        print(f"0x{target:08X}  {evidence:15s}  {len(refs)} ref(s): {' '.join(refs[:4])}{' ...' if len(refs) > 4 else ''}")
    print(f"{len(results)} candidate(s) from {len(found)} unmapped code target(s)")

    if args.write:
        lines = ["# Extra function starts for ps2_recomp, generated by tools/find_code_pointers.py.",
                 "# Functions reached only through pointers (ctor tables, vtables, callbacks).",
                 "address,evidence,refs"]
        lines += [f"0x{t:08X},{e},{' '.join(r[:3])}" for t, e, r in results]
        args.write.write_text("\n".join(lines) + "\n")
        print(f"wrote {args.write}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
