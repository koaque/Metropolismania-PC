#!/usr/bin/env python3
"""Print functions for mapping: graph context plus Ghidra's decompilation, in address order.

usage: map_view.py <from> [<to>] [--max-lines N] [--unnamed] [--only 0xA,0xB,...]

<from>/<to> are addresses (hex); without <to>, only the function at <from>. Reads
work/map/graph.json and work/decomp/ (pipeline.py map). Callers, callees and function-pointer
uses are shown by name, so re-run pipeline.py map after naming a batch for better context.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PLACEHOLDER = re.compile(r"^(FUN|fn|entry)_[0-9A-Fa-f]{6,8}$")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("start")
    ap.add_argument("end", nargs="?")
    ap.add_argument("--max-lines", type=int, default=120)
    ap.add_argument("--unnamed", action="store_true", help="only functions that still have a placeholder name")
    ap.add_argument("--only", help="comma-separated entry addresses")
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")

    graph = json.loads((ROOT / "work/map/graph.json").read_text(encoding="utf-8"))
    funcs = {int(f["entry"], 16): f for f in graph["functions"]}
    names = {a: f["name"] for a, f in funcs.items()}
    # Names applied since the last pipeline.py map are in config/symbols.csv already.
    import csv
    sym_lines = [l for l in (ROOT / "config/symbols.csv").read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]
    for row in csv.reader(sym_lines):
        if row[0].lower() != "address" and int(row[0], 16) in names:
            names[int(row[0], 16)] = row[1]
    for a, f in funcs.items():
        f["name"] = names[a]

    def nm(addr_hex: str) -> str:
        a = int(addr_hex, 16)
        return names.get(a, addr_hex)

    lo = int(args.start, 16)
    hi = int(args.end, 16) if args.end else lo
    only = {int(x, 16) for x in args.only.split(",")} if args.only else None
    for addr in sorted(funcs):
        if only is not None:
            if addr not in only:
                continue
        elif not lo <= addr <= hi:
            continue
        f = funcs[addr]
        if args.unnamed and not PLACEHOLDER.match(f["name"]):
            continue
        callers = ", ".join(nm(c) for c in f.get("callers", [])) or "-"
        taken = ", ".join(nm(c) for c in f.get("taken_by", []))
        callees = ", ".join(f"{nm(c)}" + (f" x{n}" if n > 1 else "") for c, n in f["callees"].items()) or "-"
        print(f"== 0x{addr:08X} {f['name']} ({f['size']} B, {f['instructions']} insns)")
        print(f"   callers: {callers}" + (f" | address taken by: {taken}" if taken else ""))
        print(f"   calls: {callees}" + (f" | indirect calls: {f['indirect_calls']}" if f["indirect_calls"] else ""))
        if f["func_refs"]:
            print("   takes address of: " + ", ".join(f"{v}" for v in f["func_refs"].values()))
        if f["globals"]:
            print("   globals: " + ", ".join(f"{k}:{v}" for k, v in f["globals"].items()))
        if f["data_refs"]:
            print("   data addresses: " + ", ".join(f["data_refs"]))
        for k, v in f["strings"].items():
            print(f"   string {k}: {v!r}")
        path = ROOT / "work/decomp" / f"{addr:08X}.c"
        if path.exists():
            body = [line for line in path.read_text(encoding="utf-8").splitlines()[1:] if line.strip()]
            for line in body[: args.max_lines]:
                print("   | " + line)
            if len(body) > args.max_lines:
                print(f"   | ... ({len(body) - args.max_lines} more lines)")
        print()


if __name__ == "__main__":
    main()
