#!/usr/bin/env python3
"""Which generated (recompiled) EE functions still run? The exit check of plan P7.

usage: generated_audit.py [--counts FILE [--counts FILE ...]]

  --counts FILE   the file a run wrote with MMPC_GENERATED_ENTRIES=FILE (the entry counts of every function that is not
                  ported, by address; give several to merge runs). Prints the functions that were entered, split into
                  recompiled code and the generated wrappers of the runtime's own handlers (the stubs of the recompiler
                  config). This is the truth about what runs; the static list below only finds candidates.
  (no option)     static: the recompiled SDK functions that ported code can reach, found from the call graph
                  (work/map/graph.json: calls and function pointers), the code addresses src/port/ names itself (a number or
                  addr::<name>) and the tail jumps read from generated/*.cpp, not
                  entering the handlers' stubs and not looking inside a function that is already ported. What it prints is
                  the worklist of the platform layer, with each function's status.

Needs config/funcmap.csv, config/port_status.csv, work/metropolismania.toml, work/map/graph.json (and generated/ for
the tail jumps). Run from anywhere; paths are relative to the repository root.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def load_functions() -> tuple[list[int], dict[int, tuple[str, int, int]]]:
    starts: list[int] = []
    info: dict[int, tuple[str, int, int]] = {}
    with (ROOT / "config/funcmap.csv").open(encoding="utf-8", newline="") as fh:
        for row in csv.DictReader(fh):
            start = int(row["Start"], 16)
            info[start] = (row["Name"], int(row["End"], 16), int(row["Size"]))
    starts = sorted(info)
    return starts, info


def containing(starts: list[int], info: dict[int, tuple[str, int, int]], address: int) -> int | None:
    i = bisect.bisect_right(starts, address) - 1
    if i < 0:
        return None
    start = starts[i]
    return start if address < info[start][1] else None


def load_status() -> dict[int, dict[str, str]]:
    with (ROOT / "config/port_status.csv").open(encoding="utf-8", newline="") as fh:
        return {int(r["address"], 16): r for r in csv.DictReader(fh)}


def load_stubs() -> set[int]:
    text = (ROOT / "work/metropolismania.toml").read_text(encoding="utf-8", errors="replace")
    m = re.search(r"^stubs\s*=\s*\[(.*?)\]", text, re.S | re.M)
    return {int(a, 16) for a in re.findall(r"@(0x[0-9A-Fa-f]+)", m.group(1))} if m else set()


def tail_jumps(starts: list[int], info: dict[int, tuple[str, int, int]]) -> dict[int, set[int]]:
    """Function -> functions it jumps to with a plain `j` (the call graph does not record those)."""
    out: dict[int, set[int]] = {}
    pattern = re.compile(r"^\s*// 0x([0-9a-f]+): 0x[0-9a-f]+\s+j\s+func_([0-9A-Fa-f]+)\s*$")
    for path in (ROOT / "generated").glob("*_0x*.cpp"):
        m = re.search(r"_0x([0-9a-f]+)\.cpp$", path.name)
        if not m:
            continue
        owner = int(m.group(1), 16)
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            j = pattern.match(line)
            if j:
                target = containing(starts, info, int(j.group(2), 16))
                if target is not None and target != owner:
                    out.setdefault(owner, set()).add(target)
    return out


def source_references(starts: list[int], info: dict[int, tuple[str, int, int]]) -> set[int]:
    """Functions that src/port/ names without a call the graph knows: a code address written as a number (a guest call or a
    function pointer handed to the kernel) or as mm::addr::<name>. Only exact function starts count."""
    port = ROOT / "src/port"
    by_name: dict[str, int] = {}
    header = port / "game_functions.h"
    if header.exists():
        for m in re.finditer(r"inline constexpr uint32_t (\w+) = 0x([0-9A-Fa-f]+)u;", header.read_text(encoding="utf-8")):
            by_name[m.group(1)] = int(m.group(2), 16)
    out: set[int] = set()
    for path in port.rglob("*"):
        if path.suffix not in (".cpp", ".h") or path == header or path.name == "game_types.h":
            continue
        text = re.sub(r"//[^\n]*", "", path.read_text(encoding="utf-8", errors="replace"))  # a comment names addresses too
        for m in re.finditer(r"\b0x([0-9A-Fa-f]{5,8})u?\b", text):
            a = int(m.group(1), 16)
            if a in info:
                out.add(a)
        for m in re.finditer(r"\baddr::(\w+)", text):
            if m.group(1) in by_name:
                out.add(by_name[m.group(1)])
    return out


def describe(address: int, info, status) -> str:
    name, _, size = info.get(address, ("?", 0, 0))
    row = status.get(address, {})
    return f"0x{address:08X} {name:<40} {size:5d} B  {row.get('origin', '?'):<5} {row.get('status', '?')}"


def counts_mode(paths: list[Path]) -> int:
    starts, info = load_functions()
    status = load_status()
    stubs = load_stubs()
    per_function: dict[int, int] = {}
    for path in paths:
        for line in path.read_text(encoding="utf-8").splitlines():
            address, calls = line.split()
            start = containing(starts, info, int(address, 16))
            if start is not None:
                per_function[start] = per_function.get(start, 0) + int(calls)
    generated = {a: n for a, n in per_function.items() if a not in stubs}
    wrappers = {a: n for a, n in per_function.items() if a in stubs}
    print(f"entered through the function table: {len(per_function)} functions; recompiled code {len(generated)}, "
          f"generated wrappers of runtime handlers {len(wrappers)}")
    print("\nrecompiled functions that ran (the ones P7 has to replace):")
    for a, n in sorted(generated.items(), key=lambda kv: -kv[1]):
        print(f"  {describe(a, info, status)}  entries {n}")
    unexpected = [a for a in generated if status.get(a, {}).get("origin") == "game"]
    if unexpected:
        print(f"\nGAME functions entered as generated code: {len(unexpected)}")
    return 1 if generated else 0


def static_mode() -> int:
    starts, info = load_functions()
    status = load_status()
    stubs = load_stubs()
    graph = {int(f["entry"], 16): f for f in json.loads((ROOT / "work/map/graph.json").read_text(encoding="utf-8"))["functions"]}
    jumps = tail_jumps(starts, info) if (ROOT / "generated").exists() else {}

    def edges(a: int) -> set[int]:
        f = graph.get(a)
        out = set(jumps.get(a, ()))
        if f:
            out |= {int(c, 16) for c in f["callees"]} | {int(x, 16) for x in f.get("func_refs", {})}
        return out

    def ported(a: int) -> bool:
        return status.get(a, {}).get("status") in ("ported", "verified")

    sdk = {a for a, r in status.items() if r.get("origin") == "sdk"}
    roots = {c for a, r in status.items() if r.get("origin") == "game" for c in edges(a) if c in sdk}
    roots.add(0x00100008)  # _start
    roots |= {a for a in source_references(starts, info) if a in sdk}
    reached: set[int] = set()
    todo = [a for a in roots if a not in stubs]
    while todo:
        a = todo.pop()
        if a in reached or a in stubs:
            continue
        reached.add(a)
        if ported(a):
            continue  # replaced: what it called does not run
        todo += [c for c in edges(a) if c in sdk or c not in status]
    left = sorted((a for a in reached if not ported(a)), key=lambda a: info.get(a, ("", 0, 0))[2], reverse=True)
    done = [a for a in reached if ported(a)]
    print(f"statically reachable recompiled SDK functions: {len(reached)} (ported {len(done)}, still to replace {len(left)}, "
          f"{sum(info.get(a, ('', 0, 0))[2] for a in left)} bytes)")
    for a in left:
        print(f"  {describe(a, info, status)}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--counts", action="append", help="a file written by MMPC_GENERATED_ENTRIES (repeat it to merge several runs)")
    args = ap.parse_args()
    return counts_mode([Path(c) for c in args.counts]) if args.counts else static_mode()


if __name__ == "__main__":
    sys.exit(main())
