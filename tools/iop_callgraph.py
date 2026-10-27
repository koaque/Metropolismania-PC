#!/usr/bin/env python3
"""Call graph of an IOP module from its Ghidra decompilation (plan P5): which functions of the module each function calls, which imports,
and a leaf-first order. Reads work/iop/ghidra_<module>/decomp/index.txt and the function list in config/iop/<module>_functions.csv.

    python tools/iop_callgraph.py yysound            prints the table and writes work/iop/<module>_callgraph.txt
"""
import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    module = sys.argv[1] if len(sys.argv) > 1 else "yysound"
    decomp = ROOT / "work" / "iop" / f"ghidra_{module}" / "decomp"
    functions = {}
    with open(ROOT / "config" / "iop" / f"{module}_functions.csv", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            functions[row["name"]] = row
    imports = set()
    symbols = ROOT / "work" / "iop" / f"ghidra_{module}" / "symbols.csv"
    for line in symbols.read_text(encoding="utf-8").splitlines():
        parts = line.split(",")
        if len(parts) >= 4 and parts[3] == "import":
            imports.add(parts[1])
    files = {}
    for line in (decomp / "index.txt").read_text(encoding="utf-8").splitlines():
        entry, name, file = line.split("\t")
        files[name] = file
    calls, used_imports, referenced = {}, {}, {}
    for name in functions:
        text = (decomp / files[name]).read_text(encoding="utf-8") if name in files else ""
        body = text.split("\n", 2)[-1]
        calls[name] = sorted({other for other in functions if other != name and re.search(rf"\b{re.escape(other)}\b", body)})
        used_imports[name] = sorted({imp for imp in imports if re.search(rf"\b{re.escape(imp)}\b", body)})
        # a function whose name appears without a call (a thread entry, a callback passed to an import)
        referenced[name] = sorted({other for other in functions if re.search(rf"\b{re.escape(other)}\b(?!\s*\()", body) and other != name and other in calls[name]})
    # leaf-first order: a function after everything it calls (cycles broken arbitrarily)
    order, state = [], {}

    def visit(n):
        if state.get(n):
            return
        state[n] = 1
        for c in calls[n]:
            visit(c)
        order.append(n)

    for n in sorted(functions, key=lambda n: int(functions[n]["offset"], 16)):
        visit(n)
    lines = [f"{'function':<26} {'size':>5}  calls (module)  |  imports"]
    for n in order:
        lines.append(f"{n:<26} {functions[n]['size']:>5}  {', '.join(calls[n]) or '-'}  |  {', '.join(used_imports[n]) or '-'}")
    callers = {n: [m for m in functions if n in calls[m]] for n in functions}
    lines.append("")
    lines.append("not called by another function of the module (entry points, thread bodies, callbacks, RPC handlers):")
    lines.append("  " + ", ".join(n for n in functions if not callers[n]))
    out = ROOT / "work" / "iop" / f"{module}_callgraph.txt"
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    print(f"\n{out}")


if __name__ == "__main__":
    main()
