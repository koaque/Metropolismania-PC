#!/usr/bin/env python3
"""Capture the ps2EntryRunner window to PNG on Windows (no third-party packages).

Uses PrintWindow with PW_RENDERFULLCONTENT, so the capture works even when other windows overlap
the game. Usage: screenshot.py <out.png> [--title-prefix PS2-Recomp] [--pid N]
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import struct
import sys
import zlib
from pathlib import Path

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
PW_RENDERFULLCONTENT = 2


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG), ("biPlanes", wt.WORD),
                ("biBitCount", wt.WORD), ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
                ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                ("biClrImportant", wt.DWORD)]


def find_window(title_prefix: str, pid: int | None) -> int | None:
    found: list[int] = []
    proc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def callback(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        length = user32.GetWindowTextLengthW(hwnd)
        buf = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buf, length + 1)
        if pid is not None:
            owner = wt.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value != pid:
                return True
        if buf.value.startswith(title_prefix):
            found.append(hwnd)
            return False
        return True

    user32.EnumWindows(proc(callback), 0)
    return found[0] if found else None


def capture(hwnd: int) -> tuple[int, int, bytes]:
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    width, height = rect.right - rect.left, rect.bottom - rect.top
    hdc_window = user32.GetDC(hwnd)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_window)
    bitmap = gdi32.CreateCompatibleBitmap(hdc_window, width, height)
    gdi32.SelectObject(hdc_mem, bitmap)
    # PW_CLIENTONLY (1) | PW_RENDERFULLCONTENT (2)
    if not user32.PrintWindow(hwnd, hdc_mem, 1 | PW_RENDERFULLCONTENT):
        gdi32.BitBlt(hdc_mem, 0, 0, width, height, hdc_window, 0, 0, 0x00CC0020)
    header = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
    buffer = ctypes.create_string_buffer(width * height * 4)
    gdi32.GetDIBits(hdc_mem, bitmap, 0, height, buffer, ctypes.byref(header), 0)
    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(hdc_mem)
    user32.ReleaseDC(hwnd, hdc_window)
    return width, height, buffer.raw


def write_png(path: Path, width: int, height: int, bgra: bytes) -> None:
    rows = bytearray()
    stride = width * 4
    for y in range(height):
        row = bgra[y * stride:(y + 1) * stride]
        rows.append(0)
        rgb = bytearray(width * 3)
        rgb[0::3] = row[2::4]
        rgb[1::3] = row[1::4]
        rgb[2::3] = row[0::4]
        rows += rgb

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--title-prefix", default="PS2-Recomp")
    parser.add_argument("--pid", type=int)
    parser.add_argument("--burst", type=int, default=1, help="capture N frames (out gets _NNN suffixes)")
    parser.add_argument("--interval-ms", type=int, default=50)
    args = parser.parse_args()
    ctypes.windll.user32.SetProcessDPIAware()
    hwnd = find_window(args.title_prefix, args.pid)
    if not hwnd:
        print("window not found", file=sys.stderr)
        return 1
    if args.burst <= 1:
        width, height, data = capture(hwnd)
        if width <= 0 or height <= 0:
            print("window has no client area", file=sys.stderr)
            return 1
        write_png(args.out, width, height, data)
        print(f"{args.out} {width}x{height}")
        return 0

    # Burst: capture first (fast), encode afterwards, and print per-frame brightness so flicker
    # (frames that drop to black, or alternate) shows up without looking at every image.
    import time

    frames = []
    for _ in range(args.burst):
        t0 = time.perf_counter()
        frames.append(capture(hwnd))
        time.sleep(max(0.0, args.interval_ms / 1000.0 - (time.perf_counter() - t0)))
    for i, (width, height, data) in enumerate(frames):
        out = args.out.with_name(f"{args.out.stem}_{i:03d}{args.out.suffix}")
        write_png(out, width, height, data)
        luma = sum(data[1::40]) / max(1, len(data[1::40]))  # green channel sample
        print(f"{out.name} mean_green={luma:.1f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
