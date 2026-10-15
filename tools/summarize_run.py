#!/usr/bin/env python3
"""Summarize a ps2EntryRunner log for boot triage.

Prints the tag histogram ([tag] prefixes), unique warnings/errors in first-seen order (with
counts), unimplemented stubs and syscalls, missing branch targets, IOP module activity and the
last runtime tick. Usage: summarize_run.py logs/run-*.log  (default: newest log in logs/)
"""

from __future__ import annotations

import re
import sys
from collections import Counter, OrderedDict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INTERESTING = re.compile(r"(?i)(warn|error|todo|unimplemented|missing|fail|not found|unhandled|unknown|abort|exception|assert)")
HEX = re.compile(r"0x[0-9a-fA-F]+|\b[0-9]+\b")


def main() -> int:
    if len(sys.argv) > 1:
        path = Path(sys.argv[1])
    else:
        path = max((ROOT / "logs").glob("run-*.log"), key=lambda p: p.stat().st_mtime)
    lines = path.read_text(errors="replace").splitlines()
    print(f"{path.name}: {len(lines)} lines")

    tags = Counter(m.group(1) for line in lines if (m := re.match(r"\s*\[([A-Za-z0-9_:\-]+)", line)))
    print("tags:", ", ".join(f"{t}={n}" for t, n in tags.most_common(25)))

    stubs = Counter(m.group(1) for line in lines if (m := re.search(r"Unimplemented PS2 stub called\. name=(\S+)", line)))
    if stubs:
        print("unimplemented stubs:", ", ".join(f"{n}x{c}" for n, c in stubs.most_common()))
    syscalls = Counter(m.group(0) for line in lines if (m := re.search(r"\[Syscall TODO\][^\n]{0,80}", line)))
    if syscalls:
        print("syscall TODOs:", "; ".join(f"{s} x{c}" for s, c in syscalls.most_common(10)))

    for line in lines:
        if "missing-target" in line or "No exact recompiled function" in line:
            print("first missing target:", line[:300])
            break

    # Unique interesting lines, numbers normalized so repeats collapse.
    seen: "OrderedDict[str, list]" = OrderedDict()
    for line in lines:
        if line.startswith("INFO:") or "[run:tick]" in line or not INTERESTING.search(line):
            continue
        key = HEX.sub("#", line)[:200]
        if key in seen:
            seen[key][1] += 1
        else:
            seen[key] = [line[:260], 1]
    print(f"unique warning/error lines ({len(seen)}):")
    for text, count in list(seen.values())[:60]:
        print(f"  x{count:<5} {text}")

    iop = [line for line in lines if re.search(r"(?i)iop|irx|module", line) and not line.startswith("INFO:")]
    if iop:
        print(f"IOP/module lines ({len(iop)}), first 25:")
        for line in iop[:25]:
            print("  " + line[:220])

    ticks = [line for line in lines if "[run:tick]" in line]
    if ticks:
        print("last tick:", ticks[-1][:300])
    tail = [line for line in lines if not line.startswith("INFO:") and "[run:tick]" not in line][-8:]
    print("last non-tick lines:")
    for line in tail:
        print("  " + line[:260])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
