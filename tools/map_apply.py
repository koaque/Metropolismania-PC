#!/usr/bin/env python3
"""Apply a batch of names, subsystems and notes from the mapping work.

usage: map_apply.py <batch file> [--dry-run]

Batch file lines: 0xADDR | name | subsystem | notes      ('#' starts a comment)
  name       new function name, or '-' to keep the current one. New names must start with mm_
             (game code) or sdk_ (SDK code the signature scan did not name), so they can never
             collide with the runtime's HLE handlers, which ps2_recomp binds by name.
  subsystem  e.g. movie, city, ui, sdk:libmpeg; '-' keeps the current one.
  notes      free text (what it does, evidence); '-' keeps the current notes.

Writes config/symbols.csv (rows become source 'manual') and the subsystem/notes columns of
config/port_status.csv. Names from the SDK signature scan are never replaced.
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IDENT = re.compile(r"^(mm|sdk)_[A-Za-z0-9_]+$")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("batch")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    sym_path = ROOT / "config/symbols.csv"
    sym_lines = sym_path.read_text(encoding="utf-8").splitlines()
    header = [line for line in sym_lines if line.startswith("#")]
    rows: dict[int, list[str]] = {}
    for row in csv.reader(line for line in sym_lines if line and not line.startswith("#")):
        if row[0].lower() == "address":
            continue
        rows[int(row[0], 16)] = (row + ["func", "manual"])[:4]

    status_path = ROOT / "config/port_status.csv"
    with status_path.open(encoding="utf-8", newline="") as fh:
        reader = csv.DictReader(fh)
        columns = reader.fieldnames
        status = {int(r["address"], 16): r for r in reader}

    used = {r[1]: a for a, r in rows.items()}
    errors = []
    changes = 0
    for lineno, raw in enumerate(Path(args.batch).read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|", 3)]
        if len(parts) != 4:
            errors.append(f"line {lineno}: expected 4 fields: {line}")
            continue
        addr = int(parts[0], 16)
        name, subsystem, notes = parts[1], parts[2], parts[3]
        if addr not in status:
            errors.append(f"line {lineno}: 0x{addr:08X} is not a function entry")
            continue
        if name != "-":
            existing = rows.get(addr)
            if existing and existing[3] == "ps2_analyzer":
                errors.append(f"line {lineno}: 0x{addr:08X} keeps its SDK scan name {existing[1]}")
                continue
            if not IDENT.match(name):
                errors.append(f"line {lineno}: name must start with mm_ or sdk_: {name}")
                continue
            if name in used and used[name] != addr:
                errors.append(f"line {lineno}: {name} already names 0x{used[name]:08X}")
                continue
            if existing:
                used.pop(existing[1], None)
            rows[addr] = [f"0x{addr:08X}", name, "func", "manual"]
            used[name] = addr
            status[addr]["name"] = name
        if subsystem != "-":
            status[addr]["subsystem"] = subsystem
        if notes != "-":
            status[addr]["notes"] = notes
        changes += 1

    if errors:
        print("\n".join(errors), file=sys.stderr)
        sys.exit(1)
    if args.dry_run:
        print(f"[map_apply] {changes} line(s) valid (dry run)")
        return
    sym_path.write_text("\n".join(header + ["address,name,kind,source"] + [",".join(rows[a]) for a in sorted(rows)]) + "\n",
                        encoding="utf-8", newline="\n")
    with status_path.open("w", encoding="utf-8", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=columns, lineterminator="\n")
        w.writeheader()
        w.writerows(status[a] for a in sorted(status))
    print(f"[map_apply] {changes} function(s) updated")


if __name__ == "__main__":
    main()
