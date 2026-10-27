#!/usr/bin/env python3
"""Per-function inventory of the map and the readable port.

Writes config/port_status.csv (one row per function) and docs/PORT_STATUS.md (summary).
Run through `pipeline.py status` (or `pipeline.py map`, which refreshes the call graph first).

Inputs:
  work/map/graph.json       functions, call graph (tools/ghidra/ExportGraph.java)
  config/symbols.csv        name sources (manual, ps2_analyzer, pointer_sweep)
  config/signatures.txt     functions with an applied C prototype
  src/port/                 PORT_FN(0xADDR, ...) registrations
  work/difftest/results.json  difftest outcome per function, if present
  config/port_status.csv    the hand-kept columns (subsystem, notes) of the previous run

Status levels: unnamed < named < typed < ported < verified.
  unnamed   Ghidra or pointer-sweep placeholder name (FUN_, fn_)
  named     a real name (manual or SDK signature scan)
  typed     also has a prototype in config/signatures.txt
  ported    also has a readable C++ version registered in src/port/
  verified  ported and its last difftest run matched on every record
Origin: sdk for names from the SDK signature scan or a subsystem starting with "sdk"; game when a
non-sdk subsystem is set; unclassified otherwise.
Blocking: yes when the function can reach a call that switches EE threads (see BLOCKING_CALLS);
indirect when it makes calls Ghidra could not resolve and is not already yes.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import defaultdict
from pathlib import Path

# EE calls after which the current thread may not be the one that resumes (the scheduler can switch).
# Ported code cannot survive a switch until native threading (plan P6), so these mark functions to defer.
BLOCKING_CALLS = {
    "WaitSema", "SignalSema", "SleepThread", "WakeupThread", "DelayThread", "RotateThreadReadyQueue",
    "ChangeThreadPriority", "StartThread", "ExitThread", "ExitDeleteThread", "TerminateThread",
    "SuspendThread", "ResumeThread", "WaitEventFlag", "SetEventFlag", "ClearEventFlag",
    "sceSifCallRpc", "sceSifBindRpc", "sceSifSyncIop", "sceSifRebootIop", "sceSifLoadModule",
    "sceSifLoadStartModule", "sceCdSync", "sceCdSyncS", "sceCdStRead", "sceGsSyncV", "sceGsSyncPath",
    "sceMpegGetPicture", "sceMpegDemuxPss", "sceMpegDemuxPssRing",
}
PLACEHOLDER = re.compile(r"^(FUN|fn|entry|thunk_FUN)_[0-9A-Fa-f]{6,8}$")
PORT_FN = re.compile(r"^\s*PORT_FN\((\w+)\)", re.M)
PROTO_NAME = re.compile(r"^(0x[0-9A-Fa-f]{8}) .*?\b(mm_|sdk_)(\w+)\(")
SIGNATURE_FILES = ("config/signatures.txt", "config/platform_signatures.txt")  # the game's, then the platform layer's
STATUS_ORDER = ["unnamed", "named", "typed", "ported", "verified"]
COLUMNS = ["address", "name", "size", "origin", "subsystem", "status", "blocking", "callers", "callees", "notes"]


def load_symbols(path: Path) -> dict[int, tuple[str, str]]:
    out: dict[int, tuple[str, str]] = {}
    if not path.exists():
        return out
    lines = [line for line in path.read_text(encoding="utf-8").splitlines() if line and not line.startswith("#")]
    for row in csv.reader(lines):
        if row[0].lower() == "address":
            continue
        out[int(row[0], 16)] = (row[1], row[3] if len(row) > 3 else "manual")
    return out


def load_signatures(path: Path) -> set[int]:
    out: set[int] = set()
    if not path.exists():
        return out
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            out.add(int(line.split()[0], 16))
    return out


def load_ported(port_dir: Path, signature_paths: list[Path], sizes: dict[int, int]) -> set[int]:
    """Addresses of the functions src/port/ registers with PORT_FN(Name). Name is the game name without
    "mm_", or an SDK function's sdk_ name (tools/port_gen.py adds _<ADDR> to the smaller of two functions
    that share a name)."""
    by_name: dict[str, list[int]] = defaultdict(list)
    for signatures_path in signature_paths:
        if not signatures_path.exists():
            continue
        for line in signatures_path.read_text(encoding="utf-8").splitlines():
            m = PROTO_NAME.match(line)
            if m:
                by_name[m.group(3) if m.group(2) == "mm_" else m.group(2) + m.group(3)].append(int(m.group(1), 16))
    out: set[int] = set()
    if not port_dir.exists():
        return out
    for path in port_dir.rglob("*.cpp"):
        for m in PORT_FN.finditer(path.read_text(encoding="utf-8", errors="replace")):
            name = m.group(1)
            base, _, suffix = name.rpartition("_")
            if base and suffix and all(c in "0123456789ABCDEF" for c in suffix) and int(suffix, 16) in by_name.get(base, []):
                out.add(int(suffix, 16))
            elif len(by_name.get(name, [])) == 1:
                out.add(by_name[name][0])
            elif by_name.get(name):
                out.add(max(by_name[name], key=lambda a: sizes.get(a, 0)))  # as port_gen: the larger one
    return out


def load_previous(path: Path) -> dict[int, dict[str, str]]:
    if not path.exists():
        return {}
    with path.open(encoding="utf-8", newline="") as fh:
        return {int(r["address"], 16): r for r in csv.DictReader(fh)}


def blocking_flags(funcs: dict[int, dict]) -> dict[int, str]:
    callers: dict[int, set[int]] = defaultdict(set)
    for addr, f in funcs.items():
        for callee in f["callees"]:
            callers[int(callee, 16)].add(addr)
    flags = {addr: "no" for addr in funcs}
    work = [a for a, f in funcs.items() if f["name"] in BLOCKING_CALLS]
    for a in work:
        flags[a] = "yes"
    while work:
        a = work.pop()
        for c in callers.get(a, ()):
            if flags[c] != "yes":
                flags[c] = "yes"
                work.append(c)
    for addr, f in funcs.items():
        if flags[addr] == "no" and f.get("indirect_calls", 0) > 0:
            flags[addr] = "indirect"
    return flags


def pct(n: int, d: int) -> str:
    return f"{100.0 * n / d:.1f}%" if d else "-"


def apply_inlined(path: Path, rows: list[dict[str, str]]) -> None:
    """config/inlined.txt: a function whose code ported callers carry themselves counts as ported, verified when all of them are."""
    if not path.exists():
        return
    by_address = {r["address"]: r for r in rows}
    by_name = {r["name"]: r for r in rows}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#")[0].strip()
        if not line:
            continue
        head, _, tail = line.partition(":")
        address = int(head.split()[0], 16)
        row = by_address.get(f"0x{address:08X}")
        callers = [by_name.get(c.strip()) for c in tail.split(",") if c.strip()]
        if row is None or not callers or any(c is None for c in callers):
            raise SystemExit(f"port_status: config/inlined.txt names an unknown function: {line}")
        row["status"] = "verified" if all(c["status"] == "verified" for c in callers) else "ported"
        if not row["notes"]:
            row["notes"] = "inlined into " + ", ".join(c["name"] for c in callers)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    args = ap.parse_args()
    root = Path(args.root)

    graph_path = root / "work/map/graph.json"
    if not graph_path.exists():
        raise SystemExit("work/map/graph.json missing; run: pipeline.py map")
    graph = json.loads(graph_path.read_text(encoding="utf-8"))
    funcs = {int(f["entry"], 16): f for f in graph["functions"]}

    symbols = load_symbols(root / "config/symbols.csv")
    signatures = set()
    for signature_file in SIGNATURE_FILES:
        signatures |= load_signatures(root / signature_file)
    ported = load_ported(root / "src/port", [root / s for s in SIGNATURE_FILES], {a: f["size"] for a, f in funcs.items()})
    results_path = root / "work/difftest/results.json"
    results = json.loads(results_path.read_text(encoding="utf-8")) if results_path.exists() else {}
    previous = load_previous(root / "config/port_status.csv")
    blocking = blocking_flags(funcs)

    rows = []
    for addr in sorted(funcs):
        f = funcs[addr]
        # config/symbols.csv is the source of names; the graph only learns a rename at the next pipeline.py map.
        name, source = symbols.get(addr, (f["name"], ""))
        prev = previous.get(addr, {})
        subsystem = prev.get("subsystem", "")
        notes = prev.get("notes", "")
        if source == "ps2_analyzer" or subsystem.startswith("sdk"):
            origin = "sdk"
        elif subsystem:
            origin = "game"
        else:
            origin = "unclassified"
        status = "unnamed" if PLACEHOLDER.match(name) else "named"
        if status == "named" and addr in signatures:
            status = "typed"
        if addr in ported:
            status = "ported"
            outcome = results.get(f"0x{addr:08X}")
            if outcome and outcome.get("pass"):
                status = "verified"
        rows.append({
            "address": f"0x{addr:08X}", "name": name, "size": str(f["size"]), "origin": origin,
            "subsystem": subsystem, "status": status, "blocking": blocking[addr],
            "callers": str(len(f.get("callers", []))), "callees": str(len(f["callees"])), "notes": notes,
        })
    # Rows added by map_apply.py --create are not Ghidra functions until the next pipeline.py map;
    # keep them so their subsystem and notes survive a status run in between.
    for addr, prev in previous.items():
        if addr not in funcs and (prev.get("subsystem") or prev.get("notes")):
            rows.append({c: prev.get(c, "") for c in COLUMNS})
    rows.sort(key=lambda r: int(r["address"], 16))
    apply_inlined(root / "config/inlined.txt", rows)

    out_csv = root / "config/port_status.csv"
    with out_csv.open("w", encoding="utf-8", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=COLUMNS, lineterminator="\n")
        w.writeheader()
        w.writerows(rows)

    # Summary.
    by_origin: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    by_subsystem: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    blocking_count: dict[str, int] = defaultdict(int)
    for r in rows:
        level = STATUS_ORDER.index(r["status"])
        for key in (by_origin[r["origin"]], by_subsystem[r["subsystem"] or "(unclassified)"]):
            key["total"] += 1
            for i, s in enumerate(STATUS_ORDER[1:], start=1):
                if level >= i:
                    key[s] += 1
        if r["origin"] != "sdk":
            blocking_count[r["blocking"]] += 1

    lines = [
        "# Port status",
        "",
        "Generated by `tools/port_status.py` (`pipeline.py status`); do not edit by hand.",
        "Each level includes the ones after it: a ported function is also named and typed.",
        "See `config/port_status.csv` for every function.",
        "",
        "## By origin",
        "",
        "| Origin | Functions | Named | Typed | Ported | Verified |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for origin in ("game", "unclassified", "sdk"):
        c = by_origin.get(origin)
        if not c:
            continue
        t = c["total"]
        lines.append(f"| {origin} | {t} | {pct(c['named'], t)} | {pct(c['typed'], t)} | {pct(c['ported'], t)} | {pct(c['verified'], t)} |")
    lines += [
        "",
        "Blocking (game and unclassified functions that can reach a thread switch, deferred to native threading): "
        + ", ".join(f"{k} {v}" for k, v in sorted(blocking_count.items())),
        "",
        "## By subsystem",
        "",
        "| Subsystem | Functions | Named | Typed | Ported | Verified |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for name in sorted(by_subsystem):
        c = by_subsystem[name]
        t = c["total"]
        lines.append(f"| {name} | {t} | {pct(c['named'], t)} | {pct(c['typed'], t)} | {pct(c['ported'], t)} | {pct(c['verified'], t)} |")
    (root / "docs/PORT_STATUS.md").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    total = len(rows)
    print(f"[port_status] {total} functions; config/port_status.csv and docs/PORT_STATUS.md written")


if __name__ == "__main__":
    main()
