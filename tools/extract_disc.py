#!/usr/bin/env python3
"""Extract files from a PS2 disc image (BIN/CUE or ISO) without third-party tools.

Supports plain 2048-byte ISO images and raw 2352-byte BIN images (Mode 1 or
Mode 2 Form 1). By default it pulls SYSTEM.CNF, the boot ELF named in it, and
everything under SYS/ into the output folder. Use --all to extract the whole
disc.

The output folder (original/ by default) is git-ignored. Never commit it.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import struct
import sys
import zlib
from pathlib import Path

RAW_SECTOR = 2352
USER_SECTOR = 2048

# Known-good boot ELF for Metropolismania (USA), SLUS-20377, disc VER 1.02.
KNOWN_ELFS = {
    "SLUS_203.77": {"crc32": 0x4FA3F727, "pcsx2_crc": 0x46FFB836},
}


class DiscImage:
    def __init__(self, path: Path):
        self.path = path
        self.fh = path.open("rb")
        self.raw, self.data_offset = self._detect_layout()

    def _detect_layout(self) -> tuple[bool, int]:
        size = self.path.stat().st_size
        # Plain ISO: primary volume descriptor at LBA 16.
        self.fh.seek(16 * USER_SECTOR)
        if self.fh.read(6)[1:6] == b"CD001" and size % USER_SECTOR == 0:
            return False, 0
        # Raw sectors: 12-byte sync pattern, mode byte at offset 15.
        self.fh.seek(16 * RAW_SECTOR)
        header = self.fh.read(16)
        if header[:12] == b"\x00" + b"\xff" * 10 + b"\x00":
            mode = header[15]
            offset = 16 if mode == 1 else 24
            self.fh.seek(16 * RAW_SECTOR + offset)
            if self.fh.read(6)[1:6] == b"CD001":
                return True, offset
        raise ValueError(f"{self.path}: not a recognised ISO 9660 / raw BIN image")

    def sector(self, lba: int) -> bytes:
        if self.raw:
            self.fh.seek(lba * RAW_SECTOR + self.data_offset)
        else:
            self.fh.seek(lba * USER_SECTOR)
        return self.fh.read(USER_SECTOR)

    def read(self, lba: int, size: int) -> bytes:
        out = bytearray()
        for i in range((size + USER_SECTOR - 1) // USER_SECTOR):
            out += self.sector(lba + i)
        return bytes(out[:size])

    def list_files(self) -> dict[str, tuple[int, int]]:
        pvd = self.sector(16)
        root = pvd[156:190]
        files: dict[str, tuple[int, int]] = {}
        self._walk(struct.unpack("<I", root[2:6])[0], struct.unpack("<I", root[10:14])[0], "", files)
        return files

    def _walk(self, lba: int, size: int, path: str, files: dict[str, tuple[int, int]]) -> None:
        data = self.read(lba, size)
        i = 0
        while i < len(data):
            length = data[i]
            if length == 0:
                # Records never cross sector boundaries; skip the padding.
                i = (i // USER_SECTOR + 1) * USER_SECTOR
                continue
            rec = data[i:i + length]
            i += length
            ext_lba = struct.unpack("<I", rec[2:6])[0]
            ext_size = struct.unpack("<I", rec[10:14])[0]
            flags = rec[25]
            name = rec[33:33 + rec[32]]
            if name in (b"\x00", b"\x01"):
                continue
            full = f"{path}/{name.decode('ascii', 'replace').split(';')[0]}"
            if flags & 2:
                self._walk(ext_lba, ext_size, full, files)
            else:
                files[full.lstrip("/")] = (ext_lba, ext_size)


def resolve_image(path: Path) -> Path:
    """Accept a .cue, .bin or .iso. A cue whose FILE entry is missing falls back
    to a .bin with the same stem, which covers renamed dumps."""
    if path.suffix.lower() != ".cue":
        return path
    text = path.read_text(errors="replace")
    match = re.search(r'FILE\s+"([^"]+)"', text)
    candidates = []
    if match:
        candidates.append(path.parent / match.group(1))
    candidates.append(path.with_suffix(".bin"))
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise FileNotFoundError(f"{path}: none of {', '.join(str(c) for c in candidates)} exist")


def pcsx2_crc(elf: bytes) -> int:
    """PCSX2's game CRC: XOR of every little-endian 32-bit word of the ELF."""
    crc = 0
    for (word,) in struct.iter_unpack("<I", elf[: len(elf) // 4 * 4]):
        crc ^= word
    return crc


def boot_elf_name(cnf: bytes) -> str:
    match = re.search(rb"BOOT2\s*=\s*cdrom0:\\?([^;\r\n]+)", cnf)
    if not match:
        raise ValueError("SYSTEM.CNF has no BOOT2 line")
    return match.group(1).decode("ascii").lstrip("\\")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image", type=Path, help=".cue, .bin or .iso of your own disc")
    parser.add_argument("-o", "--out", type=Path, default=Path("original"), help="output folder (default: original/)")
    parser.add_argument("--all", action="store_true", help="extract every file on the disc")
    parser.add_argument("--list", action="store_true", help="only list files")
    parser.add_argument("--hash-image", action="store_true", help="also print the SHA-1 of the whole image file")
    args = parser.parse_args()

    image_path = resolve_image(args.image)
    disc = DiscImage(image_path)
    files = disc.list_files()
    print(f"image: {image_path} ({'raw 2352' if disc.raw else 'ISO 2048'}), {len(files)} files")

    if args.list:
        for name, (_, size) in sorted(files.items()):
            print(f"{size:>12}  {name}")
        return 0

    cnf = disc.read(*files["SYSTEM.CNF"])
    elf_name = boot_elf_name(cnf)
    wanted = sorted(files) if args.all else ["SYSTEM.CNF", elf_name] + sorted(n for n in files if n.startswith("SYS/"))

    for name in wanted:
        dest = args.out / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(disc.read(*files[name]))
    print(f"extracted {len(wanted)} files to {args.out}")

    elf = disc.read(*files[elf_name])
    crc32 = zlib.crc32(elf) & 0xFFFFFFFF
    p2crc = pcsx2_crc(elf)
    print(f"boot ELF: {elf_name}  size={len(elf)}  crc32={crc32:08X}  pcsx2_crc={p2crc:08X}  sha1={hashlib.sha1(elf).hexdigest()}")
    known = KNOWN_ELFS.get(elf_name)
    if known is None:
        print("warning: this is not the supported build (SLUS_203.77). Addresses in config/ will not match.")
    elif known["pcsx2_crc"] != p2crc or (known["crc32"] is not None and known["crc32"] != crc32):
        print("warning: boot ELF does not match the supported SLUS-20377 v1.02 build.")
    else:
        print("boot ELF matches the supported build (SLUS-20377 v1.02).")

    if args.hash_image:
        sha1 = hashlib.sha1()
        with image_path.open("rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                sha1.update(chunk)
        print(f"image sha1: {sha1.hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
