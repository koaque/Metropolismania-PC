"""Compares two capture runs state for state.

Each run is a folder of difftest records of the same function (made by `pipeline.py capture`, usually GameUpdate, which
the game reaches through its vtable every frame). Records with the same file name were taken at the same call, so they are
the same frame. For each pair the tool lists the RDRAM bytes that differ, outside the guest stack (whose free part holds
leftovers of whatever ran last), grouped into ranges and named: static addresses by the nearest label or function in
config/symbols.csv, and, for GameUpdate records, map objects and people through the game object (a0).

Typical use (the deterministic timer keeps the game's timer-based randomness out of the comparison):

    pipeline.py capture --autoplay load --functions GameUpdate --every 300 --max 40 \
        --env PS2X_TIMER0=frame --env MMPC_CAPTURE_KEEP_PORTS=1 --env MMPC_CAPTURE_DIR=<dir A>
    pipeline.py capture ... (the same, without MMPC_CAPTURE_KEEP_PORTS) --env MMPC_CAPTURE_DIR=<dir B>
    python tools/lockstep_compare.py <dir A>/GameUpdate <dir B>/GameUpdate

Options: --stack-from HEX (default 1E00000: the stack area left out), --ignore HEX:LEN,... (ranges known to differ),
--detail NAME (list the ranges of that record; by default those of the first pair that differs), --max-ranges N.
Exit status 0 when every pair is identical outside the stack and the ignored ranges and neither run has records the other
lacks, 1 otherwise. The records are game-derived and stay in work/.
"""
import argparse
import bisect
import csv
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_record(path):
    """The record's RDRAM, scratchpad and argument registers (layout: src/port/difftest.cpp writeRecord)."""
    data = path.read_bytes()
    _, ctx_size, packed, scratch_size = struct.unpack_from('<4I', data, 4)
    start = 4 + 16 + ctx_size
    rdram = zlib.decompress(data[start:start + packed], -15)
    scratch = data[start + packed:start + packed + scratch_size]
    a0 = struct.unpack_from('<I', data, 20 + 4 * 16)[0]
    return rdram, scratch, a0


def load_labels():
    labels = []
    with open(ROOT / 'config/symbols.csv', encoding='utf-8') as f:
        for row in csv.reader(line for line in f if not line.startswith('#')):
            if len(row) >= 2 and row[0].startswith('0x'):
                labels.append((int(row[0], 16), row[1]))
    labels.sort()
    return labels


class Namer:
    """Names an address: a static label, or a map object or person of the city scene the record's game object holds."""

    def __init__(self, labels, rdram, game):
        self.labels = labels
        self.addresses = [a for a, _ in labels]
        self.objects = self.people = self.object_count = 0
        word = lambda a: struct.unpack_from('<I', rdram, a & 0x1FFFFFF)[0]
        try:
            city = word(game + 8) if 0 < game < 0x2000000 else 0
            if 0 < city < 0x2000000:
                cmap = word(city + 0x50)  # CityScene.map
                if 0 < cmap < 0x2000000:
                    self.objects = word(cmap + 0x8)        # CityMap.objects, MapObject 0x1C bytes each
                    self.object_count = word(cmap + 0x24)  # CityMap.objectCount
                    self.people = word(cmap + 0x68)        # CityMap.people, MapPerson 0x30 bytes each
        except struct.error:
            pass

    def __call__(self, addr):
        if self.people and self.people <= addr < self.people + 0x30 * 6001:
            index, offset = divmod(addr - self.people, 0x30)
            return f'MapPerson[{index}]+{offset:#x}'
        if self.objects and self.objects <= addr < self.objects + 0x1C * self.object_count:
            index, offset = divmod(addr - self.objects, 0x1C)
            return f'MapObject[{index}]+{offset:#x}'
        if addr >= 0x200000:
            return 'heap'
        i = bisect.bisect_right(self.addresses, addr) - 1
        if i < 0:
            return '?'
        base, name = self.labels[i]
        return f'{name}+{addr - base:#x}'


def differing_ranges(a, b, limit, ignored):
    import numpy as np
    xa = np.frombuffer(a[:limit], dtype=np.uint8)
    xb = np.frombuffer(b[:limit], dtype=np.uint8)
    mask = xa != xb
    for start, length in ignored:
        mask[start:start + length] = False
    idx = np.nonzero(mask)[0]
    ranges = []
    for i in idx.tolist():
        if ranges and i - ranges[-1][1] <= 8:
            ranges[-1][1] = i
        else:
            ranges.append([i, i])
    return idx.size, ranges


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('a', type=Path)
    ap.add_argument('b', type=Path)
    ap.add_argument('--stack-from', default='1E00000')
    ap.add_argument('--ignore', default='')
    ap.add_argument('--detail')
    ap.add_argument('--max-ranges', type=int, default=40)
    args = ap.parse_args()
    stack_from = int(args.stack_from, 16)
    ignored = []
    for item in filter(None, args.ignore.split(',')):
        address, _, length = item.partition(':')
        ignored.append((int(address, 16), int(length or '4', 0)))
    labels = load_labels()

    names_a = {p.name for p in args.a.glob('*.mmdt')}
    names_b = {p.name for p in args.b.glob('*.mmdt')}
    pairs = sorted(args.a / name for name in names_a & names_b)
    if not pairs:
        sys.exit(f'no records with the same name in {args.a} and {args.b}')
    # A run that stopped early (or took more records) is a difference too: the frames it lacks were not compared.
    only_a, only_b = sorted(names_a - names_b), sorted(names_b - names_a)
    for side, folder, names in (('A', args.a, only_a), ('B', args.b, only_b)):
        if names:
            print(f'{len(names)} record(s) only in {side} ({folder}): {", ".join(names[:8])}{" ..." if len(names) > 8 else ""}')
    identical, shown = 0, False
    for path in pairs:
        ra, sa, game = load_record(path)
        rb, sb, _ = load_record(args.b / path.name)
        count, ranges = differing_ranges(ra, rb, stack_from, ignored)
        scratch = 'same' if sa == sb else 'differs'
        print(f'{path.name}: {count} bytes in {len(ranges)} ranges differ below {stack_from:#x}; scratchpad {scratch}')
        if count == 0 and sa == sb:
            identical += 1
        if (args.detail == path.name) or (args.detail is None and not shown and ranges):
            shown = True
            name = Namer(labels, ra, game)
            for start, end in ranges[:args.max_ranges]:
                print(f'    {start:#09x}-{end:#09x} ({end - start + 1:5d})  {name(start):34s} '
                      f'A {ra[start:min(end + 1, start + 12)].hex()}  B {rb[start:min(end + 1, start + 12)].hex()}')
            if len(ranges) > args.max_ranges:
                print(f'    ... {len(ranges) - args.max_ranges} more')
    print(f'{identical} of {len(pairs)} pairs identical' + (', and records missing on one side' if only_a or only_b else ''))
    return 0 if identical == len(pairs) and not only_a and not only_b else 1


if __name__ == '__main__':
    sys.exit(main())
