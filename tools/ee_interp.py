"""A small R5900 integer interpreter that runs the game's own soft-float and libm code from the ELF (original/SLUS_203.77), as an
independent reference for the native platform functions in src/port/platform/ (plan P7.10). It models only what that code uses (the
integer instructions, the second multiply pipeline, paddub as a register move, lq and sq on the low doubleword). Two modes:

  low32=True   what the recompiled build does: sign branches (bltz, bgez, blez, bgtz and their likely forms) test the low 32 bits of
               the register only, and the runtime's fabs handler (0x119990) returns at once and leaves v0 alone
  low32=False  what a PS2 does: sign branches test all 64 bits, and fabs is the original function

The low32 mode reproduces the recompiled build bit for bit (for example pow(10, 3) = 0x41D804AA5D87437D), which is what makes the other
mode a trustworthy reference. Used by tools/soft_float_oracle.py; self test: python tools/ee_interp.py
"""
import struct
import sys
from pathlib import Path

ELF = str(Path(__file__).resolve().parent.parent / "original" / "SLUS_203.77")
M64 = (1 << 64) - 1
M32 = 0xFFFFFFFF
SENTINEL = 0x00F00000
FABS = 0x119990


def sx32(v):
    v &= M32
    return (v | 0xFFFFFFFF00000000) if v & 0x80000000 else v


def s64(v):
    return v - (1 << 64) if v >> 63 else v


def s32(v):
    v &= M32
    return v - (1 << 32) if v >> 31 else v


class Cpu:
    def __init__(self, low32):
        self.low32 = low32
        self.mem = bytearray(32 * 1024 * 1024)
        with open(ELF, "rb") as fh:
            data = fh.read()
        phoff = struct.unpack_from("<I", data, 0x1C)[0]
        phentsize, phnum = struct.unpack_from("<HH", data, 0x2A)
        for i in range(phnum):
            ptype, offset, vaddr, _p, filesz, memsz, _f, _a = struct.unpack_from("<IIIIIIII", data, phoff + i * phentsize)
            if ptype == 1:
                self.mem[vaddr:vaddr + filesz] = data[offset:offset + filesz]
        self.cache = {}
        self.steps = 0

    # memory (little endian)
    def lw(self, a):
        return struct.unpack_from("<I", self.mem, a)[0]

    def ld(self, a):
        return struct.unpack_from("<Q", self.mem, a)[0]

    def decode(self, pc):
        w = self.lw(pc)
        return (w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63, w & 0xFFFF,
                (w & 0xFFFF) - 0x10000 if w & 0x8000 else (w & 0xFFFF), (w & 0x3FFFFFF) << 2, w)

    def sign(self, value):
        """The value as the sign branches see it."""
        return s32(value) if self.low32 else s64(value)

    def call(self, address, args=(), sp=0x01F00000):
        r = [0] * 32
        for i, a in enumerate(args):
            r[4 + i] = a & M64
        r[29] = sp
        r[28] = 0x1C5970
        r[31] = SENTINEL
        self.r = r
        self.hi = self.lo = self.hi1 = self.lo1 = 0
        pc = address
        mem = self.mem
        steps = 0
        while pc != SENTINEL:
            if self.low32 and pc == FABS:
                pc = r[31]  # the runtime's handler: returns at once, v0 untouched
                continue
            ins = self.cache.get(pc)
            if ins is None:
                ins = self.cache[pc] = self.decode(pc)
            pc = self.execute(pc, ins, r, mem)
            steps += 1
            if steps > 5_000_000:
                raise RuntimeError("runaway at 0x%X" % pc)
        self.steps = steps
        return r[2]

    def execute(self, pc, ins, r, mem, in_delay=False):
        op, rs, rt, rd, sa, fn, imm, simm, target, w = ins
        nxt = pc + 4
        taken_target = None
        likely = False
        link = None
        if op == 0:
            if fn == 0:
                v = sx32(r[rt] << sa)
            elif fn == 2:
                v = sx32((r[rt] & M32) >> sa)
            elif fn == 3:
                v = sx32(s32(r[rt]) >> sa)
            elif fn == 4:
                v = sx32(r[rt] << (r[rs] & 31))
            elif fn == 6:
                v = sx32((r[rt] & M32) >> (r[rs] & 31))
            elif fn == 7:
                v = sx32(s32(r[rt]) >> (r[rs] & 31))
            elif fn == 8:
                taken_target = r[rs] & M32
                v = None
            elif fn == 9:
                taken_target = r[rs] & M32
                link = (rd, pc + 8)
                v = None
            elif fn == 10:
                v = r[rs] if r[rt] == 0 else r[rd]
            elif fn == 11:
                v = r[rs] if r[rt] != 0 else r[rd]
            elif fn == 15:
                v = None
            elif fn == 16:
                v = self.hi
            elif fn == 17:
                self.hi = r[rs]
                v = None
            elif fn == 18:
                v = self.lo
            elif fn == 19:
                self.lo = r[rs]
                v = None
            elif fn == 20:
                v = (r[rt] << (r[rs] & 63)) & M64
            elif fn == 22:
                v = r[rt] >> (r[rs] & 63)
            elif fn == 23:
                v = (s64(r[rt]) >> (r[rs] & 63)) & M64
            elif fn in (24, 25):
                if fn == 24:
                    prod = s32(r[rs]) * s32(r[rt])
                else:
                    prod = (r[rs] & M32) * (r[rt] & M32)
                self.lo = sx32(prod)
                self.hi = sx32(prod >> 32)
                v = self.lo if rd else None  # the R5900 writes the low half to rd as well
            elif fn in (26, 27):
                if fn == 26:
                    a, b = s32(r[rs]), s32(r[rt])
                else:
                    a, b = r[rs] & M32, r[rt] & M32
                if b == 0:
                    self.lo = sx32(-1 if (fn == 27 or a >= 0) else 1)
                    self.hi = sx32(a)
                elif fn == 26 and a == -(1 << 31) and b == -1:
                    self.lo = sx32(a)
                    self.hi = 0
                else:
                    q = abs(a) // abs(b)
                    if (a < 0) != (b < 0):
                        q = -q
                    self.lo = sx32(q)
                    self.hi = sx32(a - q * b)
                v = None
            elif fn in (32, 33):
                v = sx32(r[rs] + r[rt])
            elif fn in (34, 35):
                v = sx32(r[rs] - r[rt])
            elif fn == 36:
                v = r[rs] & r[rt]
            elif fn == 37:
                v = r[rs] | r[rt]
            elif fn == 38:
                v = r[rs] ^ r[rt]
            elif fn == 39:
                v = ~(r[rs] | r[rt]) & M64
            elif fn == 42:
                v = 1 if s64(r[rs]) < s64(r[rt]) else 0
            elif fn == 43:
                v = 1 if r[rs] < r[rt] else 0
            elif fn in (44, 45):
                v = (r[rs] + r[rt]) & M64
            elif fn in (46, 47):
                v = (r[rs] - r[rt]) & M64
            elif fn == 56:
                v = (r[rt] << sa) & M64
            elif fn == 58:
                v = r[rt] >> sa
            elif fn == 59:
                v = (s64(r[rt]) >> sa) & M64
            elif fn == 60:
                v = (r[rt] << (sa + 32)) & M64
            elif fn == 62:
                v = r[rt] >> (sa + 32)
            elif fn == 63:
                v = (s64(r[rt]) >> (sa + 32)) & M64
            else:
                raise NotImplementedError("SPECIAL fn %d at 0x%X (0x%08X)" % (fn, pc, w))
            if v is not None and rd:
                r[rd] = v
        elif op == 1:
            value = self.sign(r[rs])
            if rt in (0, 2, 16, 18):
                cond = value < 0
            elif rt in (1, 3, 17, 19):
                cond = value >= 0
            else:
                raise NotImplementedError("REGIMM %d at 0x%X" % (rt, pc))
            likely = rt in (2, 3, 18, 19)
            if rt >= 16:
                link = (31, pc + 8)
            if cond:
                taken_target = (pc + 4 + (simm << 2)) & M32
            elif likely:
                return pc + 8
        elif op == 2 or op == 3:
            taken_target = ((pc + 4) & 0xF0000000) | target
            if op == 3:
                link = (31, pc + 8)
        elif op in (4, 5, 6, 7, 20, 21, 22, 23):
            base = op & 7
            if base == 4:
                cond = r[rs] == r[rt]
            elif base == 5:
                cond = r[rs] != r[rt]
            elif base == 6:
                cond = self.sign(r[rs]) <= 0
            else:
                cond = self.sign(r[rs]) > 0
            likely = op >= 20
            if cond:
                taken_target = (pc + 4 + (simm << 2)) & M32
            elif likely:
                return pc + 8
        elif op in (8, 9):
            if rt:
                r[rt] = sx32(r[rs] + simm)
        elif op == 10:
            if rt:
                r[rt] = 1 if s64(r[rs]) < simm else 0
        elif op == 11:
            if rt:
                r[rt] = 1 if r[rs] < (simm & M64) else 0
        elif op == 12:
            if rt:
                r[rt] = r[rs] & imm
        elif op == 13:
            if rt:
                r[rt] = r[rs] | imm
        elif op == 14:
            if rt:
                r[rt] = r[rs] ^ imm
        elif op == 15:
            if rt:
                r[rt] = sx32(imm << 16)
        elif op in (24, 25):
            if rt:
                r[rt] = (r[rs] + simm) & M64
        elif op == 32:
            if rt:
                b = mem[(r[rs] + simm) & M32]
                r[rt] = (b | 0xFFFFFFFFFFFFFF00) if b & 0x80 else b
        elif op == 33:
            if rt:
                h = struct.unpack_from("<H", mem, (r[rs] + simm) & M32)[0]
                r[rt] = (h | 0xFFFFFFFFFFFF0000) if h & 0x8000 else h
        elif op == 35:
            if rt:
                r[rt] = sx32(self.lw((r[rs] + simm) & M32))
        elif op == 36:
            if rt:
                r[rt] = mem[(r[rs] + simm) & M32]
        elif op == 37:
            if rt:
                r[rt] = struct.unpack_from("<H", mem, (r[rs] + simm) & M32)[0]
        elif op == 39:
            if rt:
                r[rt] = self.lw((r[rs] + simm) & M32)
        elif op == 40:
            mem[(r[rs] + simm) & M32] = r[rt] & 0xFF
        elif op == 41:
            struct.pack_into("<H", mem, (r[rs] + simm) & M32, r[rt] & 0xFFFF)
        elif op == 43:
            struct.pack_into("<I", mem, (r[rs] + simm) & M32, r[rt] & M32)
        elif op == 55:
            if rt:
                r[rt] = self.ld((r[rs] + simm) & M32)
        elif op == 63:
            struct.pack_into("<Q", mem, (r[rs] + simm) & M32, r[rt])
        elif op == 0x1E:  # lq: the low doubleword (the upper half is not modelled)
            if rt:
                r[rt] = self.ld(((r[rs] + simm) & M32) & ~15)
        elif op == 0x1F:  # sq
            a = ((r[rs] + simm) & M32) & ~15
            struct.pack_into("<QQ", mem, a, r[rt], 0)
        elif op == 0x33:  # pref
            pass
        elif op == 0x1C:  # MMI: only the second multiply pipeline (HI1, LO1) is modelled
            if fn == 0x10:
                v = self.hi1
            elif fn == 0x11:
                self.hi1 = r[rs]
                v = None
            elif fn == 0x12:
                v = self.lo1
            elif fn == 0x13:
                self.lo1 = r[rs]
                v = None
            elif fn in (0x18, 0x19):
                if fn == 0x18:
                    prod = s32(r[rs]) * s32(r[rt])
                else:
                    prod = (r[rs] & M32) * (r[rt] & M32)
                self.lo1 = sx32(prod)
                self.hi1 = sx32(prod >> 32)
                v = self.lo1 if rd else None
            elif fn == 0x28 and sa == 0x18:  # paddub: unsigned saturating byte adds (gcc uses it with $zero as a register move)
                v = 0
                for k in range(8):
                    v |= min(0xFF, ((r[rs] >> (8 * k)) & 0xFF) + ((r[rt] >> (8 * k)) & 0xFF)) << (8 * k)
            else:
                raise NotImplementedError("MMI fn 0x%X sa 0x%X at 0x%X (0x%08X)" % (fn, sa, pc, w))
            if v is not None and rd:
                r[rd] = v
        else:
            raise NotImplementedError("opcode 0x%X at 0x%X (0x%08X)" % (op, pc, w))
        if taken_target is None:
            return nxt
        # the link register is written when the branch is taken, before the delay slot runs
        if link is not None and link[0]:
            r[link[0]] = link[1]
        slot = self.cache.get(pc + 4)
        if slot is None:
            slot = self.cache[pc + 4] = self.decode(pc + 4)
        self.execute(pc + 4, slot, r, mem, True)
        return taken_target


def soft(bits):
    return struct.unpack("<d", struct.pack("<Q", bits))[0]


def bits(value):
    return struct.unpack("<Q", struct.pack("<d", value))[0]


FUNCTIONS = {
    "dpadd": 0x127D90, "dpsub": 0x127DE8, "dpmul": 0x127E50, "dpdiv": 0x1280D8, "dpcmp": 0x128338, "litodp": 0x128388,
    "dptoli": 0x128440, "d_utod": 0x1000F0, "pow": 0x11A290,
}

if __name__ == "__main__":
    for low32 in (True, False):
        cpu = Cpu(low32)
        print("low32" if low32 else "correct")
        for n in range(0, 7):
            res = cpu.call(FUNCTIONS["pow"], (bits(10.0), bits(float(n))))
            print("  pow(10, %d) = %016X (%r) as int %d, %d steps" % (n, res, soft(res), s32(cpu.call(FUNCTIONS["dptoli"], (res,))), cpu.steps))
