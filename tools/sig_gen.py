#!/usr/bin/env python3
"""Seed config/signatures.txt with prototypes for game functions that have none yet.

usage: sig_gen.py [--dry-run]

Inputs: work/map/inferred_protos.jsonl (pipeline.py script ProbePrototypes.java <file>), the
decompiles in work/decomp/, config/this_types.txt and config/types/*.h. For each game function
without a line in signatures.txt it writes one built from the decompiler's own parameter list:

  - integer parameters keep their argument register order (a0..a3, t0..t3); a skipped register
    becomes an "unused" parameter so the later ones stay in place; float parameters follow (f12..)
  - Ghidra's guesses map to plain C types (undefined4/undefined8/long -> int, ulong -> uint32_t,
    undefined1[16] * -> Vec4 *, ...)
  - a parameter becomes a struct pointer when a this_types.txt rule names it, or when the
    decompile casts it to one of the structs in config/types/
  - struct pointers are named after the struct (city, map, view ...); other parameters after
    their register, which is how the notes in port_status.csv refer to them

Existing lines are never changed: the seeded lines are a starting point to review and edit.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INT_REGS = ["a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3"]
FLOAT_REGS = [f"f{n}" for n in range(12, 20)]

SCALAR = {
    "undefined8": "int", "long": "int", "int": "int", "undefined4": "int", "undefined": "int",
    "ulong": "uint32_t", "uint": "uint32_t", "short": "int16_t", "ushort": "uint16_t",
    "undefined2": "uint16_t", "char": "char", "byte": "uint8_t", "undefined1": "uint8_t",
    "bool": "int", "float": "float", "double": "double",
}
POINTER = {
    "int *": "int32_t *", "undefined4 *": "uint32_t *", "uint *": "uint32_t *", "ushort *": "uint16_t *",
    "short *": "int16_t *", "char *": "char *", "byte *": "uint8_t *", "undefined1 *": "uint8_t *",
    "float *": "float *", "undefined1[16] *": "Vec4 *", "undefined8 *": "uint64_t *", "ulong *": "uint64_t *",
    "long *": "int64_t *", "undefined *": "void *", "undefined2 *": "uint16_t *",
}
NAMES = {
    "CityScene": "city", "CityMap": "map", "MapTile": "tile", "MapObject": "obj", "MapPerson": "person",
    "BuildingType": "btype", "Family": "family", "FamilyMember": "member", "Widget": "widget",
    "TalkSession": "talk", "TalkView": "view", "WordsCtx": "ctx", "Stamina": "stamina",
}


def c_type(ghidra_type: str) -> str:
    t = ghidra_type.strip()
    if t in SCALAR:
        return SCALAR[t]
    if t in POINTER:
        return POINTER[t]
    if t.endswith("*"):
        return "void *"
    return "int"


def infer_struct(text: str, pname: str, structs: set[str]) -> str | None:
    """Struct type of a parameter from its decompile: a cast to a known struct, or the city map and
    city scene layouts (objects 0x1C at +8, people 0x30 at +0x68, tiles 0x18 at +4; the map at +0x50)."""
    p = re.escape(pname)
    m = re.search(r"\((\w+) \*\)" + p + r"\b", text)
    if m and m.group(1) in structs:
        return m.group(1)
    if re.search(r"\(CityMap \*\*\)\(" + p + r" \+ 0x50\)", text) or \
            re.search(r"\(" + p + r" \+ 0x50\)\) \+ (0x68|8|4)\)", text):
        return "CityScene"
    if re.search(p + r" \+ 0x68\) \+ [^;]{0,40}\* 0x30", text) or \
            re.search(p + r" \+ 8\) \+ [^;]{0,40}\* 0x1c", text) or \
            re.search(p + r" \+ 4\) \+ [^;]{0,40}\* 0x18", text):
        return "CityMap"
    # a map object: its tile word at +4 split into x (low 7 bits) and z
    if re.search(r"\(" + p + r" \+ 4\)[^;]{0,4}& 0x7f", text) or re.search(r"\b" + p + r"\[2\] & 0x7f", text):
        return "MapObject"
    # a person: family at +0x2A and member at +0x2E, or the character type table lookup at +0x24
    if (re.search(r"\(" + p + r" \+ 0x2a\)", text) and re.search(r"\(" + p + r" \+ 0x2e\)", text)) or \
            re.search(r"0x1bda3c \+ \(uint\)\*\(byte \*\)\(" + p + r" \+ 0x24\)", text):
        return "MapPerson"
    return None


def load_structs() -> set[str]:
    names = set()
    for header in (ROOT / "config/types").glob("*.h"):
        names |= set(re.findall(r"^struct (\w+) \{", header.read_text(encoding="utf-8"), re.M))
    return names


def load_this_rules() -> list[tuple[str, str, str, set[str]]]:
    rules = []
    path = ROOT / "config/this_types.txt"
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        except_ = set(parts[4].split(",")) if len(parts) >= 5 and parts[3] == "except" else set()
        rules.append((parts[0], parts[1], parts[2], except_))
    return rules


PARAM = re.compile(r"^(?P<type>.*?)\s*(?P<ptr>\**)(?P<name>\w+)$")


def refresh(structs: set[str], dry: bool) -> None:
    """Upgrade plain integer parameters of the seeded lines to struct pointers where the current
    decompiles (with the seeded prototypes applied) now show the struct; nothing else changes."""
    sig_path = ROOT / "config/signatures.txt"
    lines = sig_path.read_text(encoding="utf-8").splitlines()
    marker = next(i for i, l in enumerate(lines) if l.startswith("# Seeded by tools/sig_gen.py"))
    changed = 0
    for i in range(marker + 1, len(lines)):
        line = lines[i]
        if not line.startswith("0x"):
            continue
        addr = int(line.split()[0], 16)
        decomp = ROOT / f"work/decomp/{addr:08X}.c"
        if not decomp.exists():
            continue
        text = decomp.read_text(encoding="utf-8", errors="replace")
        head, args = line.rsplit("(", 1)
        args = args.rstrip(");")
        if args.strip() == "void":
            continue
        params = [a.strip() for a in args.split(",")]
        used = {PARAM.match(p).group("name") for p in params}
        new = []
        for prm in params:
            m = PARAM.match(prm)
            name = m.group("name")
            plain = m.group("type") in ("int", "uint32_t") and not m.group("ptr")
            word_ptr = m.group("type") in ("uint16_t", "int32_t", "uint32_t") and m.group("ptr") == "*"
            if (plain or word_ptr) and name in INT_REGS:
                struct = infer_struct(text, name, structs)
                if struct:
                    base = NAMES.get(struct, struct.lower()) if struct != "Vec4" else name
                    pname = base if base not in used or base == name else f"{base}_{name}"
                    used.add(pname)
                    prm = f"{struct} *{pname}"
            new.append(prm)
        rebuilt = f"{head}({', '.join(new)});"
        if rebuilt != line:
            changed += 1
            if dry and changed <= 40:
                print(rebuilt)
            lines[i] = rebuilt
    print(f"[sig_gen] refresh: {changed} prototype(s) gained struct pointers")
    if not dry:
        sig_path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--refresh", action="store_true", help="upgrade seeded lines from the current decompiles")
    args = ap.parse_args()
    if args.refresh:
        refresh(load_structs(), args.dry_run)
        return

    structs = load_structs()
    rules = load_this_rules()
    with (ROOT / "config/port_status.csv").open(encoding="utf-8", newline="") as fh:
        status = {int(r["address"], 16): r for r in csv.DictReader(fh)}
    sig_path = ROOT / "config/signatures.txt"
    sig_lines = sig_path.read_text(encoding="utf-8").splitlines()
    have = {int(l.split()[0], 16) for l in sig_lines if l.strip() and not l.startswith("#")}
    probes = [json.loads(l) for l in (ROOT / "work/map/inferred_protos.jsonl").open(encoding="utf-8")]

    out = []
    for p in probes:
        addr = int(p["entry"], 16)
        row = status.get(addr)
        if addr in have or not row or row["origin"] != "game" or "params" not in p:
            continue
        name = row["name"]
        if not name.startswith("mm_"):
            continue
        ints: dict[str, tuple[str, str]] = {}
        floats: dict[str, tuple[str, str]] = {}
        for i, prm in enumerate(p["params"]):
            reg = prm["storage"].split(":")[0].replace("_lo", "")
            (floats if reg in FLOAT_REGS else ints)[reg] = (c_type(prm["type"]), prm["name"])
        if any(r not in INT_REGS and r not in FLOAT_REGS for r in list(ints) + list(floats)):
            continue  # stack parameters: leave for hand-written prototypes
        decomp = ROOT / f"work/decomp/{addr:08X}.c"
        text = decomp.read_text(encoding="utf-8", errors="replace") if decomp.exists() else ""
        rule = next(((s, n) for pre, s, n, exc in rules if name.startswith(pre) and name not in exc), None)
        this_struct, this_name = rule if rule else (None, None)

        params = []
        last_int = max((INT_REGS.index(r) for r in ints), default=-1)
        for idx in range(last_int + 1):
            reg = INT_REGS[idx]
            if reg not in ints:
                params.append(f"int unused_{reg}")
                continue
            ctype, pname = ints[reg]
            struct = None
            pname_out = None
            if idx == 0 and this_struct:
                struct, pname_out = this_struct, this_name
            else:
                # the decompile names the parameter after its register once a seeded prototype is applied
                struct = infer_struct(text, pname, structs) or infer_struct(text, reg, structs)
            if struct:
                params.append(f"{struct} *{pname_out or NAMES.get(struct, struct.lower())}")
            else:
                params.append(f"{ctype} {reg}" if not ctype.endswith("*") else f"{ctype}{reg}")
        last_float = max((FLOAT_REGS.index(r) for r in floats), default=-1)
        for idx in range(last_float + 1):
            reg = FLOAT_REGS[idx]
            params.append(f"float {reg}" if reg in floats else f"float unused_{reg}")
        ret = c_type(p["ret"]) if p["ret"] != "void" else "void"
        sep = "" if ret.endswith("*") else " "
        out.append(f"0x{addr:08X} {ret}{sep}{name}({', '.join(params) if params else 'void'});")

    print(f"[sig_gen] {len(out)} prototype(s) seeded for game functions ({len(have)} already present)")
    if args.dry_run:
        print("\n".join(out[:40]))
        return
    text = sig_path.read_text(encoding="utf-8").rstrip("\n") + "\n"
    text += "# Seeded by tools/sig_gen.py from the decompiler's parameter lists; reviewed by hand per subsystem.\n"
    sig_path.write_text(text + "\n".join(out) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
