#!/usr/bin/env python3
"""Copy save folders from a PS2 memory card image into the runner's memory card folder.

usage: mc_extract.py <card.ps2> [--list] [--prefix BASLUS-20377] [--name FOLDER ...] [--out original/mc0]

The runner keeps memory cards as host folders (original/mc0 for port 0, original/mc1 for port 1).
This reads a raw card image as PCSX2 writes it (8 MB, with or without the 16-byte ECC per page),
walks its file system and writes each matching save folder below --out. The card image is only read.
Saves are your own game data: original/ is git-ignored, keep them there.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAGIC = b"Sony PS2 Memory Card Format"
ENTRY = 512  # directory entry size
MODE_EXISTS, MODE_DIR, MODE_FILE = 0x8000, 0x0020, 0x0010


class Card:
    def __init__(self, path: Path):
        self.data = path.read_bytes()
        if not self.data.startswith(MAGIC):
            raise ValueError(f"{path}: not a formatted PS2 memory card")
        self.page_len, self.pages_per_cluster = struct.unpack_from("<HH", self.data, 0x28)
        # Pages are stored with 16 bytes of ECC after each one when the image size says so.
        pages = struct.unpack_from("<I", self.data, 0x30)[0] * self.pages_per_cluster
        self.raw_page = self.page_len + 16 if len(self.data) >= pages * (self.page_len + 16) else self.page_len
        self.cluster_size = self.page_len * self.pages_per_cluster
        self.alloc_offset, _, self.root_cluster = struct.unpack_from("<III", self.data, 0x34)
        self.ifc_list = struct.unpack_from("<32I", self.data, 0x50)
        self.per_cluster = self.cluster_size // 4

    def cluster(self, n: int) -> bytes:
        base = n * self.pages_per_cluster
        return b"".join(self.data[(base + i) * self.raw_page:(base + i) * self.raw_page + self.page_len]
                        for i in range(self.pages_per_cluster))

    def fat(self, c: int) -> int:
        index = c // self.per_cluster
        indirect = self.cluster(self.ifc_list[index // self.per_cluster])
        table = struct.unpack_from("<I", indirect, (index % self.per_cluster) * 4)[0]
        return struct.unpack_from("<I", self.cluster(table), (c % self.per_cluster) * 4)[0]

    def read(self, first: int, length: int) -> bytes:
        out, c = [], first
        while len(out) * self.cluster_size < length:
            out.append(self.cluster(c + self.alloc_offset))
            entry = self.fat(c)
            if entry == 0xFFFFFFFF or not entry & 0x80000000:
                break
            c = entry & 0x7FFFFFFF
        return b"".join(out)[:length]

    def entries(self, first: int, count: int):
        raw = self.read(first, count * ENTRY)
        for i in range(count):
            e = raw[i * ENTRY:(i + 1) * ENTRY]
            mode, _, length = struct.unpack_from("<HHI", e, 0)
            cluster = struct.unpack_from("<I", e, 0x10)[0]
            name = e[0x40:0x60].split(b"\0")[0].decode("latin1")
            if mode & MODE_EXISTS and name not in (".", ".."):
                yield mode, length, cluster, name

    def root(self):
        dot = self.read(self.root_cluster, ENTRY)
        count = struct.unpack_from("<I", dot, 4)[0]
        return list(self.entries(self.root_cluster, count))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("card", type=Path)
    ap.add_argument("--list", action="store_true", help="only list the save folders")
    ap.add_argument("--prefix", default="BASLUS-20377", help="copy folders whose name starts with this (default: this game)")
    ap.add_argument("--name", action="append", help="copy only these folders (repeatable)")
    ap.add_argument("--out", type=Path, default=ROOT / "original/mc0")
    args = ap.parse_args()

    card = Card(args.card)
    copied = 0
    for mode, length, cluster, name in card.root():
        if not mode & MODE_DIR:
            continue
        wanted = name in args.name if args.name else name.startswith(args.prefix)
        files = [(n, card.read(c, l)) for m, l, c, n in card.entries(cluster, length) if m & MODE_FILE]
        print(f"{'*' if wanted else ' '} {name}: {', '.join(f'{n} ({len(d)})' for n, d in files)}")
        if not wanted or args.list:
            continue
        dest = args.out / name
        dest.mkdir(parents=True, exist_ok=True)
        for n, d in files:
            (dest / n).write_bytes(d)
        copied += 1
    if not args.list:
        print(f"[mc_extract] {copied} save folder(s) written to {args.out}")


if __name__ == "__main__":
    sys.exit(main())
