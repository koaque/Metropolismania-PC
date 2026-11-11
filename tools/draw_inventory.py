"""Inventory of what a frame asks of the graphics hardware (plan N2: the feature list a native renderer must cover).

Reads difftest records (pipeline.py capture, RDRAM at a function's entry) and walks the frame's DMA chain the way the VIF1
channel would: the DMA tags (CNT, NEXT, REF, REFE, CALL, RET, END, with the tag's upper half sent to the VIF as two codes),
then the VIF command stream across tags (STCYCL, OFFSET, BASE, ITOP, STMOD, MSKPATH3, MARK, FLUSH*, MSCAL, MSCALF, MSCNT,
STMASK, STROW, STCOL, MPG, DIRECT, DIRECTHL, UNPACK), and the GIF packets sent directly (PACKED, REGLIST, IMAGE). It counts:
which VU1 program each MSCAL/MSCNT runs (the microcode packet last uploaded, by address), the UNPACK formats, the GS
registers written and their distinct values for the state that matters to a renderer (TEX0 formats, ALPHA, TEST, TEX1,
CLAMP, FBA, PABE, FOGCOL, SCISSOR, FRAME, ZBUF, XYOFFSET, TEXA, TEXCLUT), the primitive types drawn directly, image uploads
(BITBLTBUF/TRXREG), and textures read from a frame buffer's pages (frame-buffer feedback).

Which list: for records of DmaList_KickFrame the list being kicked (DMA list index), for GameUpdate records the list kicked
at the start of that frame (the other one); --start HEX walks any chain.

usage: python tools/draw_inventory.py RECORD.mmdt [...] [--start HEX] [--json OUT.json]

The records are game-derived: keep them and the output in work/.
"""
import argparse
import collections
import json
import struct
import sys
import zlib
from pathlib import Path

DMA_LISTS = 0x001BD9C0      # globals::kDmaLists: the two list addresses
DMA_LIST_INDEX = 0x001BDCEC  # globals::dmaListIndex

# The game's 13 VU1 microcode upload packets (docs/RENDER_PIPELINE.md).
PROGRAMS = {
    0x1A3C80: 'models (people, player, menus)', 0x1A4780: 'models (buildings, objects)', 0x1A5EB0: 'terrain ground',
    0x1A6440: 'water', 0x1A76F0: 'cliff sides', 0x1A7B30: 'placement highlight', 0x1A8330: 'models unlit (claim met, marker)',
    0x1A8A00: 'rain', 0x1A8E20: 'rain ground', 0x1A91C0: 'lit roads', 0x1A9A90: 'snow', 0x1A9F10: 'snow ground',
    0x1AA340: 'water bottom',
}
PRIM_TYPES = ['point', 'line', 'line strip', 'triangle', 'triangle strip', 'triangle fan', 'sprite', 'prohibited']
GS_REGS = {
    0x00: 'PRIM', 0x01: 'RGBAQ', 0x02: 'ST', 0x03: 'UV', 0x04: 'XYZF2', 0x05: 'XYZ2', 0x06: 'TEX0_1', 0x07: 'TEX0_2',
    0x08: 'CLAMP_1', 0x09: 'CLAMP_2', 0x0A: 'FOG', 0x0C: 'XYZF3', 0x0D: 'XYZ3', 0x14: 'TEX1_1', 0x15: 'TEX1_2',
    0x16: 'TEX2_1', 0x17: 'TEX2_2', 0x18: 'XYOFFSET_1', 0x19: 'XYOFFSET_2', 0x1A: 'PRMODECONT', 0x1B: 'PRMODE',
    0x1C: 'TEXCLUT', 0x22: 'SCANMSK', 0x34: 'MIPTBP1_1', 0x35: 'MIPTBP1_2', 0x36: 'MIPTBP2_1', 0x37: 'MIPTBP2_2',
    0x3B: 'TEXA', 0x3D: 'FOGCOL', 0x3F: 'TEXFLUSH', 0x40: 'SCISSOR_1', 0x41: 'SCISSOR_2', 0x42: 'ALPHA_1', 0x43: 'ALPHA_2',
    0x44: 'DIMX', 0x45: 'DTHE', 0x46: 'COLCLAMP', 0x47: 'TEST_1', 0x48: 'TEST_2', 0x49: 'PABE', 0x4A: 'FBA_1', 0x4B: 'FBA_2',
    0x4C: 'FRAME_1', 0x4D: 'FRAME_2', 0x4E: 'ZBUF_1', 0x4F: 'ZBUF_2', 0x50: 'BITBLTBUF', 0x51: 'TRXPOS', 0x52: 'TRXREG',
    0x53: 'TRXDIR', 0x54: 'HWREG', 0x60: 'SIGNAL', 0x61: 'FINISH', 0x62: 'LABEL',
}
STATE_REGS = {'TEX0_1', 'TEX0_2', 'TEX1_1', 'TEX1_2', 'CLAMP_1', 'CLAMP_2', 'ALPHA_1', 'ALPHA_2', 'TEST_1', 'TEST_2', 'FBA_1',
              'FBA_2', 'PABE', 'FOGCOL', 'SCISSOR_1', 'SCISSOR_2', 'FRAME_1', 'FRAME_2', 'ZBUF_1', 'ZBUF_2', 'XYOFFSET_1',
              'XYOFFSET_2', 'TEXA', 'TEXCLUT', 'COLCLAMP', 'DTHE', 'PRMODECONT'}
PSM = {0x00: 'CT32', 0x01: 'CT24', 0x02: 'CT16', 0x0A: 'CT16S', 0x13: 'T8', 0x14: 'T4', 0x1B: 'T8H', 0x24: 'T4HL',
       0x2C: 'T4HH', 0x30: 'Z32', 0x31: 'Z24', 0x32: 'Z16', 0x3A: 'Z16S'}


def load_record(path):
    data = path.read_bytes()
    _, ctx_size, packed, _ = struct.unpack_from('<4I', data, 4)
    start = 4 + 16 + ctx_size
    return zlib.decompress(data[start:start + packed], -15)


class Inventory:
    def __init__(self):
        self.tags = collections.Counter()
        self.vif = collections.Counter()
        self.unpack = collections.Counter()
        self.program_runs = collections.Counter()
        self.program_uploads = collections.Counter()
        self.gif_modes = collections.Counter()
        self.prims = collections.Counter()
        self.prim_words = collections.Counter()
        self.regs = collections.Counter()
        self.state = collections.defaultdict(collections.Counter)
        self.images = collections.Counter()
        self.feedback = collections.Counter()
        self.frames = set()
        self.notes = []

    def describe(self, reg, value):
        if reg.startswith('TEX0'):
            tbp, tbw, psm = value & 0x3FFF, (value >> 14) & 0x3F, (value >> 20) & 0x3F
            tw, th, tcc, tfx = (value >> 26) & 0xF, (value >> 30) & 0xF, (value >> 34) & 1, (value >> 35) & 3
            cbp, cpsm, csm = (value >> 37) & 0x3FFF, (value >> 51) & 0xF, (value >> 55) & 1
            return f'{PSM.get(psm, hex(psm))} {1 << tw}x{1 << th} tcc{tcc} tfx{tfx}' + (f' clut {PSM.get(cpsm, hex(cpsm))} csm{csm}' if psm in (0x13, 0x14, 0x1B, 0x24, 0x2C) else ''), tbp
        if reg.startswith('ALPHA'):
            a, b, c, d, fix = value & 3, (value >> 2) & 3, (value >> 4) & 3, (value >> 6) & 3, (value >> 32) & 0xFF
            names = ['Cs', 'Cd', '0', '?']
            return f'({names[a]}-{names[b]})*{["As", "Ad", "FIX", "?"][c]}+{names[d]}' + (f' FIX={fix}' if c == 2 else ''), None
        if reg.startswith('TEST'):
            ate, atst, aref, afail = value & 1, (value >> 1) & 7, (value >> 4) & 0xFF, (value >> 12) & 3
            date, datm, zte, ztst = (value >> 14) & 1, (value >> 15) & 1, (value >> 16) & 1, (value >> 17) & 3
            return (f'atest {["never", "always", "less", "lequal", "equal", "gequal", "greater", "notequal"][atst]} {aref} '
                    f'fail {["keep", "fb", "zb", "rgb"][afail]}' if ate else 'no atest') + (f' date{datm}' if date else '') + \
                   (f' z {["never", "always", "gequal", "greater"][ztst]}' if zte else ' no z'), None
        if reg.startswith('FRAME'):
            fbp, fbw, psm, fbmsk = value & 0x1FF, (value >> 16) & 0x3F, (value >> 24) & 0x3F, value >> 32
            self.frames.add(fbp)
            self.record_frames.append((fbp, max(fbw, 1)))
            return f'fbp {fbp:#x} w{fbw} {PSM.get(psm, hex(psm))} mask {fbmsk:#x}', None
        if reg.startswith('TEX1'):
            return f'lcm{value & 1} mxl{(value >> 2) & 7} mmag{(value >> 5) & 1} mmin{(value >> 6) & 7} l{(value >> 19) & 3}', None
        if reg.startswith('CLAMP'):
            return f'wms{value & 3} wmt{(value >> 2) & 3}', None
        return f'{value:#x}', None

    def gs_write(self, reg_id, value):
        reg = GS_REGS.get(reg_id, f'reg{reg_id:#x}')
        self.regs[reg] += 1
        if reg == 'PRIM':
            self.prim_words[value & 0x7FF] += 1
        if reg in STATE_REGS:
            text, tbp = self.describe(reg, value)
            self.state[reg][text] += 1
            if tbp is not None:
                # Frame-buffer feedback: the texture starts inside a buffer drawn into earlier in this frame's list, or
                # inside one of the two 640x224 field buffers (pages 0x0 and 0x46, patches/0112). A buffer is taken as fbw
                # pages wide and 14 high (448 lines of CT32: a page is 64 x 32 pixels, 32 texture blocks).
                for fbp, fbw in self.record_frames + [(0x0, 10), (0x46, 10)]:
                    pages = fbw * (7 if fbp in (0x0, 0x46) else 14)
                    if fbp * 32 <= tbp < (fbp + pages) * 32:
                        self.feedback[f'TEX0 {text} at {tbp:#x} in the buffer at page {fbp:#x}'] += 1
                        break
        if reg == 'BITBLTBUF':
            self.last_blt = value
        if reg == 'TRXREG':
            dbp, dpsm = (self.last_blt >> 32) & 0x3FFF, (self.last_blt >> 56) & 0x3F
            self.images[f'{PSM.get(dpsm, hex(dpsm))} {value & 0xFFF}x{(value >> 32) & 0xFFF}'] += 1

    def gif(self, data, pos, end):
        """GIF packets in data[pos:end] (one DIRECT's worth); returns nothing, counts what it sees."""
        while pos + 16 <= end:
            lo, hi = struct.unpack_from('<QQ', data, pos)
            pos += 16
            nloop, eop, pre, prim, flg, nreg = lo & 0x7FFF, (lo >> 15) & 1, (lo >> 46) & 1, (lo >> 47) & 0x7FF, (lo >> 58) & 3, (lo >> 60) & 0xF
            nreg = nreg or 16
            self.gif_modes[['PACKED', 'REGLIST', 'IMAGE', 'IMAGE'][flg]] += 1
            if pre and flg != 2:
                self.prims[PRIM_TYPES[prim & 7]] += nloop
                self.prim_words[prim] += 1
            regs = [(hi >> (4 * i)) & 0xF for i in range(nreg)]
            if flg == 0:
                for _ in range(nloop):
                    for r in regs:
                        if pos + 16 > end:
                            return
                        a, b = struct.unpack_from('<QQ', data, pos)
                        pos += 16
                        if r == 0xE:
                            self.gs_write(b & 0xFF, a)
                        elif r == 0x0:
                            self.gs_write(0x00, a)
                        elif r in (0x4, 0x5, 0xC, 0xD):
                            self.vertices += 1
                        elif r in (0x6, 0x7, 0x8, 0x9):
                            self.gs_write({6: 0x06, 7: 0x07, 8: 0x08, 9: 0x09}[r], a)
            elif flg == 1:
                words = nloop * nreg
                pos += ((words + 1) // 2) * 16
            else:
                pos += nloop * 16
            if eop and pos >= end:
                return

    def finish_record(self):
        self.record_frames = []


def walk(rdram, start, inv, max_tags=200000):
    """Follows the DMA chain from `start` and feeds the VIF stream to the inventory."""
    stream = bytearray()
    origins = []  # (offset in stream, the address the bytes came from) for MPG program identification
    addr, stack, tags = start & 0x1FFFFFF0, [], 0
    while tags < max_tags:
        tags += 1
        if addr + 16 > len(rdram):
            inv.notes.append(f'tag address {addr:#x} out of range')
            break
        lo, hi = struct.unpack_from('<QQ', rdram, addr)
        qwc, tag_id, tag_addr = lo & 0xFFFF, (lo >> 28) & 7, (lo >> 32) & 0x7FFFFFF0
        inv.tags[['REFE', 'CNT', 'NEXT', 'REF', 'REFS', 'CALL', 'RET', 'END'][tag_id]] += 1
        # TTE: the tag's upper half goes to the VIF as two codes; the lower half (the tag itself) is skipped but still takes its
        # place in the qword, so the stream keeps the transfer's 16-byte alignment (DIRECT data starts on a qword).
        origins.append((len(stream), addr))
        stream += struct.pack('<QQ', 0, hi)
        if tag_id in (0, 3, 4):  # REFE, REF, REFS: data elsewhere
            data_addr, next_addr = tag_addr, addr + 16
        else:
            data_addr, next_addr = addr + 16, addr + 16 + qwc * 16
        origins.append((len(stream), data_addr))
        stream += rdram[data_addr & 0x1FFFFFF:(data_addr & 0x1FFFFFF) + qwc * 16]
        if tag_id == 0:
            break
        if tag_id == 1 or tag_id == 3 or tag_id == 4:
            addr = next_addr
        elif tag_id == 2:
            addr = tag_addr
        elif tag_id == 5:
            stack.append(next_addr)
            addr = tag_addr
        elif tag_id == 6:
            if not stack:
                break
            addr = stack.pop()
        else:  # END
            break
    vif(bytes(stream), origins, inv)


def source_of(origins, offset):
    best = None
    for start, address in origins:
        if start <= offset:
            best = address + (offset - start)
        else:
            break
    return best


def vif(stream, origins, inv):
    pos, program = 0, None
    cl = wl = 1
    while pos + 4 <= len(stream):
        code = struct.unpack_from('<I', stream, pos)[0]
        imm, num, cmd = code & 0xFFFF, (code >> 16) & 0xFF, (code >> 24) & 0x7F
        pos += 4
        if cmd == 0x01:
            cl, wl = imm & 0xFF, (imm >> 8) & 0xFF
        if 0x60 <= cmd <= 0x7F:
            vn, vl = (cmd >> 2) & 3, cmd & 3
            names = {(0, 0): 'S-32', (0, 1): 'S-16', (0, 2): 'S-8', (1, 0): 'V2-32', (1, 1): 'V2-16', (1, 2): 'V2-8',
                     (2, 0): 'V3-32', (2, 1): 'V3-16', (2, 2): 'V3-8', (3, 0): 'V4-32', (3, 1): 'V4-16', (3, 2): 'V4-8',
                     (3, 3): 'V4-5'}
            inv.unpack[names.get((vn, vl), f'?{vn}{vl}') + (' masked' if cmd & 0x10 else '')] += 1
            n = num or 256
            if wl <= cl:
                vectors = n
            else:
                vectors = cl * (n // wl) + min(n % wl, cl)
            if vl == 3:  # V4-5: 16 bits a vector
                size = ((vectors * 16 + 31) // 32) * 4
            else:
                size = ((vectors * (vn + 1) * (32 >> vl) + 31) // 32) * 4
            pos += size
            inv.vif['UNPACK'] += 1
            continue
        name = {0x00: 'NOP', 0x01: 'STCYCL', 0x02: 'OFFSET', 0x03: 'BASE', 0x04: 'ITOP', 0x05: 'STMOD', 0x06: 'MSKPATH3',
                0x07: 'MARK', 0x10: 'FLUSHE', 0x11: 'FLUSH', 0x13: 'FLUSHA', 0x14: 'MSCAL', 0x15: 'MSCALF', 0x17: 'MSCNT',
                0x20: 'STMASK', 0x30: 'STROW', 0x31: 'STCOL', 0x4A: 'MPG', 0x50: 'DIRECT', 0x51: 'DIRECTHL'}.get(cmd)
        if name is None:
            inv.notes.append(f'unknown VIF code {code:#010x} at stream offset {pos - 4}')
            break
        inv.vif[name] += 1
        if cmd == 0x20:
            pos += 4
        elif cmd in (0x30, 0x31):
            pos += 16
        elif cmd == 0x4A:
            source = source_of(origins, pos - 4)
            # the upload packet is the CALLed block: the program's address is the nearest known packet at or below
            program = max((a for a in PROGRAMS if source is not None and a <= source < a + 0x2000), default=source)
            inv.program_uploads[program] += 1
            pos += (num or 256) * 8
        elif cmd in (0x14, 0x15, 0x17):
            inv.program_runs[(program, 'MSCAL' if cmd != 0x17 else 'MSCNT')] += 1
        elif cmd in (0x50, 0x51):
            # DIRECT data goes to the GIF's path 2 in order; one GIF packet (an image's data, say) can go on across
            # several DIRECTs, so the payloads are joined and parsed as one stream afterwards.
            pos = (pos + 15) & ~15
            size = (imm or 65536) * 16
            inv.path2 += stream[pos:pos + size]
            pos += size
    inv.gif(bytes(inv.path2), 0, len(inv.path2))
    inv.path2 = bytearray()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('records', nargs='+', type=Path)
    ap.add_argument('--start')
    ap.add_argument('--json', type=Path)
    args = ap.parse_args()
    total = Inventory()
    for path in args.records:
        rdram = load_record(path)
        if args.start:
            start = int(args.start, 16)
        else:
            index = struct.unpack_from('<I', rdram, DMA_LIST_INDEX)[0] & 1
            if 'GameUpdate' in str(path):
                index ^= 1
            start = struct.unpack_from('<I', rdram, DMA_LISTS + 4 * index)[0]
        total.tex_bases, total.vertices, total.last_blt = [], getattr(total, 'vertices', 0), 0
        total.record_frames = []
        total.path2 = bytearray()
        walk(rdram, start, total)
        total.finish_record()
    out = {
        'records': len(args.records),
        'dma_tags': dict(total.tags), 'vif_codes': dict(total.vif), 'unpack_formats': dict(total.unpack),
        'program_uploads': {f'{a:#x} {PROGRAMS.get(a, "?")}': n for a, n in total.program_uploads.items()},
        'program_runs': {f'{a:#x} {PROGRAMS.get(a, "?")} {k}' if a is not None else f'none {k}': n for (a, k), n in total.program_runs.items()},
        'gif_modes': dict(total.gif_modes), 'direct_prims': dict(total.prims),
        'prim_words': {f'{p:#05x} {PRIM_TYPES[p & 7]}' + ''.join(f' {f}' for b, f in ((3, 'gouraud'), (4, 'tex'), (5, 'fog'), (6, 'blend'), (7, 'aa'), (8, 'uv'), (9, 'ctx2'), (10, 'fix')) if p >> b & 1): n for p, n in total.prim_words.items()},
        'gs_registers': dict(total.regs),
        'gs_state': {reg: dict(c.most_common()) for reg, c in sorted(total.state.items())},
        'image_uploads': dict(total.images), 'frame_buffer_textures': dict(total.feedback),
        'frame_buffers': sorted(f'{f:#x}' for f in total.frames), 'notes': total.notes[:20],
    }
    text = json.dumps(out, indent=1)
    if args.json:
        args.json.write_text(text, encoding='utf-8')
    print(text)


if __name__ == '__main__':
    sys.exit(main())
