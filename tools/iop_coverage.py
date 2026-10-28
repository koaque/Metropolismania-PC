"""Reports which words of YYSOUND the verifier has seen the original execute.

usage: python tools/iop_coverage.py [--csv config/iop/port_status.csv] DUMP [DUMP...]

A dump is what PS2X_IOP_COVERAGE=<file> writes during a PS2X_IOP_VERIFY run: the module's base and size (32 bit each), then one byte
per word (1: the original executed it in a verified call). Several dumps are merged. The report lists, per function of
config/iop/yysound_functions.csv, the share of its words executed, and the functions never reached or only partly reached.
With --csv the per-function result is also written as a table (name, offset, words, words executed, share, state).
Calls that were not verified (the verifier checks the first calls of each function and then every n-th) are not counted, so this is
the code the comparison has actually covered.
"""
import csv
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def load(paths):
    words = None
    base = 0
    for path in paths:
        with open(path, "rb") as handle:
            data = handle.read()
        base, size = struct.unpack_from("<II", data, 0)
        body = data[8:]
        if words is None:
            words = bytearray(len(body))
        for i, value in enumerate(body[:len(words)]):
            if value:
                words[i] = 1
    return base, words


def main(paths, csv_path=None):
    base, words = load(paths)
    functions = []
    with open(os.path.join(ROOT, "config", "iop", "yysound_functions.csv"), newline="") as handle:
        for row in csv.DictReader(handle):
            functions.append((int(row["offset"], 16), row["name"], int(row["size"])))
    total_words = 0
    total_seen = 0
    report = []
    for offset, name, size in functions:
        first = offset // 4
        count = size // 4
        seen = sum(1 for i in range(first, first + count) if i < len(words) and words[i])
        total_words += count
        total_seen += seen
        report.append((name, offset, count, seen))
    if csv_path:
        with open(csv_path, "w", newline="") as handle:
            handle.write("name,offset,words,executed,share,state\n")
            for name, offset, count, seen in report:
                state = "ported; all of the original's code seen by the verifier" if seen == count else ("ported; part of the original's code seen" if seen else "ported; never reached under the verifier")
                handle.write("%s,0x%05X,%d,%d,%.0f%%,%s\n" % (name, offset, count, seen, 100.0 * seen / count, state))
    never = [r for r in report if r[3] == 0]
    partial = [r for r in report if 0 < r[3] < r[2]]
    full = [r for r in report if r[3] == r[2]]
    print("module base 0x%X, %d functions, %d words, %d executed (%.1f%%)" % (base, len(report), total_words, total_seen, 100.0 * total_seen / total_words))
    print("fully executed: %d functions" % len(full))
    print("never executed: %d functions" % len(never))
    for name, offset, count, _seen in never:
        print("  %-24s 0x%05X  %4d words" % (name, offset, count))
    print("partly executed: %d functions" % len(partial))
    for name, offset, count, seen in sorted(partial, key=lambda r: r[3] / r[2]):
        print("  %-24s 0x%05X  %4d of %4d words (%.0f%%)" % (name, offset, seen, count, 100.0 * seen / count))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    args = sys.argv[1:]
    csv_path = None
    if args and args[0] == "--csv":
        csv_path = args[1]
        args = args[2:]
    main(args, csv_path)
