"""Ahead-of-time translation of the IOP modules (plan P5.3).

usage: python tools/iop_aot.py [--out DIR] [--only NAME[,NAME...]] [--manifest]

Reads the modules from original/SYS, relocates each at the address it is loaded at, finds its code and writes one C++ file per module to
work/iop_aot/ (never committed: it is machine-translated game code, AGENTS.md rule 8). The generated code runs the module natively and
behaves exactly like the interpreter in core/iop_cpu.cpp: one cycle per instruction, the branch and load delay slots, the traps, and the
points where a run stops (the instruction budget, the next due event, an instruction that needs the emulator's per-step work). The
generated code includes only ps2x/iop/iop_aot.h; the emulator side is in patches/0139-iop-native-hook.patch.

The structure of a generated file:
  - a block is the code from a leader (a function entry, a branch target, a return point, ...) to the next branch or jump with its delay slot;
  - one C++ function per block, written as straight-line code in which every instruction is a label, so a run can start (resume) at any
    instruction of the block: after the instruction the code counts it and, when the budget is used up, writes the exact processor state
    (pc, a branch waiting for its delay slot, a load waiting to land) and returns;
  - a table from each word of the image to (block, index), and a run function that dispatches from cpu.pc until something stops it.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import irx_inspect as ii  # noqa: E402

# the modules in the order the game loads them, with the address each is loaded at (the load log; a different disc image or load order
# changes the hash and the emulator then leaves the translation off, with a log line)
MODULES = [
    ("SIO2MAN", 0x10000),
    ("MCMAN", 0x11000),
    ("MCSERV", 0x36700),
    ("PADMAN", 0x39F00),
    ("LIBSD", 0x43C00),
    ("SDRDRV", 0x48700),
    ("MODMIDI", 0x49900),
    ("MODHSYN", 0x4D500),
    ("MODSEIN", 0x5C600),
    ("YYSOUND", 0x5E300),
]

SHF_EXECINSTR = 4


def u32(image, offset):
    return struct.unpack_from("<I", image, offset)[0]


def s16(value):
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


class Insn:
    """One decoded instruction at an absolute address."""

    def __init__(self, address, word):
        self.address = address
        self.word = word
        self.op = word >> 26
        self.rs = (word >> 21) & 31
        self.rt = (word >> 16) & 31
        self.rd = (word >> 11) & 31
        self.sa = (word >> 6) & 31
        self.funct = word & 63
        self.imm = word & 0xFFFF
        self.simm = s16(word)
        self.target26 = (word & 0x03FFFFFF) << 2
        self.kind = self.classify()

    def classify(self):
        """'branch' (conditional, relative), 'jump' (j, jal, jr, jalr), 'plain'."""
        op = self.op
        if op == 0 and self.funct in (0x08, 0x09):
            return "jump"
        if op in (2, 3):
            return "jump"
        if op in (4, 5, 6, 7):
            return "branch"
        if op == 1 and self.rt in (0, 1, 0x10, 0x11):
            return "branch"
        return "plain"

    def is_unconditional(self):
        # j, jal, jr, jalr and the always-taken beq r, r
        if self.kind == "jump":
            return True
        return self.op == 4 and self.rs == self.rt

    def branch_target(self):
        return (self.address + 4 + (self.simm << 2)) & 0xFFFFFFFF

    def jump_target(self):
        return ((self.address + 4) & 0xF0000000) | self.target26

    def links(self):
        """True for the jumps and branches that write a return address (the return point is address + 8)."""
        if self.op == 3:
            return True
        if self.op == 0 and self.funct == 0x09:
            return True
        return self.op == 1 and self.rt in (0x10, 0x11)

    def is_load(self):
        """(instruction that schedules a delayed load into rt) -> rt, or None"""
        if self.op in (0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26):
            return self.rt
        if self.op == 0x10 and self.rs == 0:
            return self.rt
        return None


# ---------------------------------------------------------------- discovery


class CodeMap:
    """Which addresses of a module image can be code."""

    def in_text(self, address):
        offset = address - self.base
        return any(lo <= offset < hi for lo, hi in self.text) and (address & 3) == 0

    def code_ok(self, address):
        """An address in text that is neither inside an import or export record nor an import stub."""
        return self.in_text(address) and address not in self.stubs and not any(lo <= address < hi for lo, hi in self.tables)

    def word(self, address):
        return u32(self.image, address - self.base)


class RawModule(CodeMap):
    """A flat code image (tools/iop_aot_fuzz.py): all of it is text, the seeds are given."""

    def __init__(self, name, base, image, seeds):
        self.name = name
        self.base = base
        self.image = bytes(image)
        self.image_size = len(self.image)
        self.words = (self.image_size + 3) // 4
        self.hash = ii.fnv1a64(self.image)
        self.text = [(0, self.image_size)]
        self.stubs = set()
        self.tables = []
        self._seeds = set(seeds)

    def seeds(self):
        return set(self._seeds)


class Module(CodeMap):
    def __init__(self, name, base):
        self.name = name
        self.base = base
        self.irx = ii.Irx(ROOT / "original" / "SYS" / f"{name}.IRX")
        self.image, unresolved = ii.relocated_image(self.irx, base)
        if unresolved:
            raise ValueError(f"{name}: {unresolved} unresolved HI16 relocation(s)")
        loads = [p for p in self.irx.phdrs if p["type"] == ii.PT_LOAD and p["memsz"]]
        min_v = min(p["vaddr"] for p in loads)
        self.image_size = max(p["vaddr"] + p["memsz"] for p in loads) - min_v
        self.words = (self.image_size + 3) // 4
        self.hash = ii.fnv1a64(self.image[:self.image_size])
        # executable sections: where code can be
        self.text = [(s["addr"], s["addr"] + s["size"]) for s in self.irx.sections if s["flags"] & SHF_EXECINSTR and s["size"]]
        self.stubs = set()
        self.tables = []  # the import and export records inside .text: data, not code
        for lib in self.irx.libraries():
            if lib["kind"] == "import":
                for offset, _ordinal in lib["stubs"]:
                    self.stubs.add(base + offset)
                self.tables.append((lib["offset"] + base, lib["offset"] + base + 20 + 8 * len(lib["stubs"])))
            else:
                self.tables.append((lib["offset"] + base, lib["offset"] + base + 20 + 4 * (max([o for _a, o in lib["stubs"]], default=0) + 2)))

    def code_pointers(self):
        """Addresses in text that the relocations make into values: function pointers (HI16/LO16 pairs) and code addresses held in
        data (R_MIPS_32): the only ways an indirect jump or call can reach code."""
        out = set()
        irx = self.irx
        symbols = irx.symbols()
        relocatable = irx.type in (ii.ET_SCE_IOPRELEXEC, ii.ET_SCE_IOPRELEXEC2)
        pending = []
        pairs = []
        for sec in irx.sections:
            if sec["type"] not in (ii.SHT_REL, ii.SHT_RELA) or sec["info"] >= len(irx.sections):
                continue
            entry = max(sec["entsize"], 12 if sec["type"] == ii.SHT_RELA else 8)
            for pos in range(0, sec["size"] - entry + 1, entry):
                r_offset, r_info = struct.unpack_from("<II", irx.data, sec["offset"] + pos)
                rtype, sym_index = r_info & 0xFF, r_info >> 8
                place = r_offset if relocatable else irx.sections[sec["info"]]["addr"] + r_offset
                if rtype == ii.R_MIPS_32 or rtype == ii.R_MIPS_REL32:
                    value = u32(self.image, place)
                    if self.in_text(value):
                        out.add(value)
                elif rtype == ii.R_MIPS_HI16:
                    pending.append((place, sym_index))
                elif rtype == ii.R_MIPS_LO16:
                    keep = []
                    for hi_place, hi_sym in pending:
                        if hi_sym != sym_index:
                            keep.append((hi_place, hi_sym))
                        else:
                            pairs.append((hi_place, place))
                    pending = keep
        for hi_place, lo_place in pairs:
            hi = (u32(self.image, hi_place) & 0xFFFF) << 16
            full = (hi + s16(u32(self.image, lo_place))) & 0xFFFFFFFF
            if self.in_text(full):
                out.add(full)
        return out

    def seeds(self):
        out = set()
        info = self.irx.module_info()
        if info and self.in_text(self.base + info["entry"]):
            out.add(self.base + info["entry"])
        for lib in self.irx.libraries():
            if lib["kind"] == "export":
                for offset, _ordinal in lib["stubs"]:
                    value = u32(self.image, offset)
                    if self.in_text(value):
                        out.add(value)
        out |= self.code_pointers()
        return out


def discover(module):
    """Reachable instructions and the block leaders of the module."""
    insns = {}
    leaders = set()
    work = []

    def push(address, leader=True):
        if module.code_ok(address):
            if leader:
                leaders.add(address)
            work.append(address)

    for seed in module.seeds():
        push(seed)
    while work:
        address = work.pop()
        while True:
            if address in insns or not module.code_ok(address):
                break
            insn = Insn(address, module.word(address))
            insns[address] = insn
            if insn.kind == "plain":
                address += 4
                continue
            # a branch or jump: the delay slot follows and belongs to it
            slot_address = address + 4
            if not module.code_ok(slot_address):
                break
            slot = Insn(slot_address, module.word(slot_address))
            insns.setdefault(slot_address, slot)
            if slot.kind != "plain":
                raise ValueError(f"{module.name}: branch in the delay slot at {slot_address:#x}")
            if insn.kind == "branch":
                push(insn.branch_target())
            elif insn.op in (2, 3):
                push(insn.jump_target())
            if insn.links():
                push(address + 8)
            if insn.is_unconditional() and not insn.links():
                break
            if insn.links() and insn.kind == "jump":
                break
            # conditional branch (or branch and link, whose return point is its fall-through): the code goes on after the slot
            leaders.add(address + 8)
            address += 8
    return insns, leaders


def build_blocks(module, insns, leaders):
    """[(leader, [Insn, ...])]: each block is straight-line code that ends with a branch or jump and its delay slot, or where the next leader
    begins."""
    blocks = []
    for leader in sorted(leaders):
        if leader not in insns:
            continue
        body = []
        address = leader
        while True:
            insn = insns[address]
            body.append(insn)
            if insn.kind != "plain":
                body.append(insns[address + 4])
                break
            nxt = address + 4
            if nxt in leaders or nxt not in insns:
                break
            address = nxt
        blocks.append((leader, body))
    return blocks


# ---------------------------------------------------------------- code generation


def rv(n):
    return "0u" if n == 0 else f"cpu.gpr[{n}]"


def wr(n, expr):
    return f"(void)({expr});" if n == 0 else f"cpu.gpr[{n}] = {expr};"


class Gen:
    """Emits the C++ of one instruction. `ds` is the C++ expression that says whether the instruction is executed as a delay slot of a taken
    branch (what the interpreter calls wasDelaySlot), used by the traps."""

    def __init__(self, insn, ds):
        self.i = insn
        self.ds = ds
        self.code = []
        self.dest = 0          # the register the instruction writes (the interpreter's writtenReg), 0 for none
        self.load = None       # rt of a delayed load (value in `t`)
        self.cond = None       # for a branch or jump: C++ condition ("" for always) and target expression
        self.target = None
        self.stop = False      # the instruction needs the emulator's per-step work after it (status register changes)

    def add(self, line):
        self.code.append(line)

    def trap(self, code, bad=None):
        """A statement that raises the trap and ends the run."""
        pc = f"0x{self.i.address:08X}u"
        if bad is None:
            return f"AOT_TRAP({code}u, {pc}, {self.ds})"
        return f"AOT_TRAP_ADDR({code}u, {pc}, {self.ds}, {bad})"

    def emit(self):
        i = self.i
        op, rs, rt, rd, sa, f = i.op, i.rs, i.rt, i.rd, i.sa, i.funct
        if op == 0:
            self.special()
        elif op == 1:
            self.regimm()
        elif op in (2, 3):
            if op == 3:
                self.add(f"cpu.gpr[31] = 0x{(i.address + 8) & 0xFFFFFFFF:08X}u;")
                self.dest = 31
            self.cond = ""
            self.target = f"0x{i.jump_target():08X}u"
        elif op in (4, 5, 6, 7):
            a, b = rv(rs), rv(rt)
            cond = {4: f"{a} == {b}", 5: f"{a} != {b}", 6: f"(int32_t){a} <= 0", 7: f"(int32_t){a} > 0"}[op]
            if op == 4 and rs == rt:
                cond = ""
            self.cond = cond
            self.target = f"0x{i.branch_target():08X}u"
        elif op == 0x08:
            self.add(f"{{ int32_t r; if (aotAddOverflow((int32_t){rv(rs)}, {i.simm}, r)) {self.trap(12)} else {wr(rt, '(uint32_t)r')} }}")
            self.dest = rt
        elif op == 0x09:
            self.add(wr(rt, f"{rv(rs)} + {i.simm & 0xFFFFFFFF}u"))
            self.dest = rt
        elif op == 0x0A:
            self.add(wr(rt, f"((int32_t){rv(rs)} < {i.simm}) ? 1u : 0u"))
            self.dest = rt
        elif op == 0x0B:
            self.add(wr(rt, f"({rv(rs)} < {i.simm & 0xFFFFFFFF}u) ? 1u : 0u"))
            self.dest = rt
        elif op == 0x0C:
            self.add(wr(rt, f"{rv(rs)} & {i.imm}u"))
            self.dest = rt
        elif op == 0x0D:
            self.add(wr(rt, f"{rv(rs)} | {i.imm}u"))
            self.dest = rt
        elif op == 0x0E:
            self.add(wr(rt, f"{rv(rs)} ^ {i.imm}u"))
            self.dest = rt
        elif op == 0x0F:
            self.add(wr(rt, f"{(i.imm << 16) & 0xFFFFFFFF}u"))
            self.dest = rt
        elif op == 0x10:
            self.cop0()
        elif op in (0x20, 0x24):
            conv = "(uint32_t)(int32_t)(int8_t)v" if op == 0x20 else "v"
            self.add(f"{{ uint32_t a = {rv(rs)} + {i.simm & 0xFFFFFFFF}u; uint32_t v = aotLoad8(x, a, lim); t = {conv}; }}")
            self.load = rt
        elif op in (0x21, 0x25):
            conv = "(uint32_t)(int32_t)(int16_t)v" if op == 0x21 else "v"
            self.add(f"{{ uint32_t a = {rv(rs)} + {i.simm & 0xFFFFFFFF}u; if (a & 1u) {self.trap(4, 'a')} uint32_t v = aotLoad16(x, a, lim); t = {conv}; }}")
            self.load = rt
        elif op in (0x22, 0x26):
            fn = "aotLwl" if op == 0x22 else "aotLwr"
            self.add(f"{{ uint32_t a = {rv(rs)} + {i.simm & 0xFFFFFFFF}u; uint32_t m = aotLoad32(x, a & ~3u, lim); t = {fn}({rv(rt)}, m, a); }}")
            self.load = rt
        elif op == 0x23:
            self.add(f"{{ uint32_t a = {rv(rs)} + {i.simm & 0xFFFFFFFF}u; if (a & 3u) {self.trap(4, 'a')} t = aotLoad32(x, a, lim); }}")
            self.load = rt
        elif op == 0x28:
            self.add(f"{{ uint32_t a = {rv(rs)} + {i.simm & 0xFFFFFFFF}u; aotStore8(x, a, {rv(rt)}, lim); }}")
        elif op == 0x29:
            self.add(f"{{ uint32_t a = {rv(rs)} + {i.simm & 0xFFFFFFFF}u; if (a & 1u) {self.trap(5, 'a')} aotStore16(x, a, {rv(rt)}, lim); }}")
        elif op in (0x2A, 0x2E):
            fn = "aotSwl" if op == 0x2A else "aotSwr"
            self.add(f"{{ uint32_t a = {rv(rs)} + {i.simm & 0xFFFFFFFF}u; uint32_t m = aotLoad32(x, a & ~3u, lim); aotStore32(x, a & ~3u, {fn}(m, {rv(rt)}, a), lim); }}")
        elif op == 0x2B:
            self.add(f"{{ uint32_t a = {rv(rs)} + {i.simm & 0xFFFFFFFF}u; if (a & 3u) {self.trap(5, 'a')} aotStore32(x, a, {rv(rt)}, lim); }}")
        else:
            self.add(self.trap(10))
        if self.load == 0:
            self.load = None  # a load into r0 reads (side effects) but nothing lands
        return self

    def special(self):
        i = self.i
        rs, rt, rd, sa, f = i.rs, i.rt, i.rd, i.sa, i.funct
        a, b = rv(rs), rv(rt)

        def w(expr):
            self.add(wr(rd, expr))
            self.dest = rd

        if f == 0x00:
            w(f"{b} << {sa}")
        elif f == 0x02:
            w(f"{b} >> {sa}")
        elif f == 0x03:
            w(f"(uint32_t)((int32_t){b} >> {sa})")
        elif f == 0x04:
            w(f"{b} << ({a} & 31u)")
        elif f == 0x06:
            w(f"{b} >> ({a} & 31u)")
        elif f == 0x07:
            w(f"(uint32_t)((int32_t){b} >> ({a} & 31u))")
        elif f == 0x08:
            self.cond = ""
            self.add(f"bt = {a};")
            self.target = "bt"
        elif f == 0x09:
            link = rd if rd else 31
            self.add(f"cpu.gpr[{link}] = 0x{(i.address + 8) & 0xFFFFFFFF:08X}u;")
            self.dest = link
            self.add(f"bt = {rv(rs)};")
            self.cond = ""
            self.target = "bt"
        elif f == 0x0C:
            self.add(self.trap(8))
        elif f == 0x0D:
            self.add(self.trap(9))
        elif f == 0x10:
            w("cpu.hi")
        elif f == 0x11:
            self.add(f"cpu.hi = {a};")
        elif f == 0x12:
            w("cpu.lo")
        elif f == 0x13:
            self.add(f"cpu.lo = {a};")
        elif f == 0x18:
            self.add(f"aotMult(cpu, {a}, {b});")
        elif f == 0x19:
            self.add(f"aotMultu(cpu, {a}, {b});")
        elif f == 0x1A:
            self.add(f"aotDiv(cpu, {a}, {b});")
        elif f == 0x1B:
            self.add(f"aotDivu(cpu, {a}, {b});")
        elif f == 0x20:
            self.add(f"{{ int32_t r; if (aotAddOverflow((int32_t){a}, (int32_t){b}, r)) {self.trap(12)} else {wr(rd, '(uint32_t)r')} }}")
            self.dest = rd
        elif f == 0x21:
            w(f"{a} + {b}")
        elif f == 0x22:
            self.add(f"{{ int32_t r; if (aotSubOverflow((int32_t){a}, (int32_t){b}, r)) {self.trap(12)} else {wr(rd, '(uint32_t)r')} }}")
            self.dest = rd
        elif f == 0x23:
            w(f"{a} - {b}")
        elif f == 0x24:
            w(f"{a} & {b}")
        elif f == 0x25:
            w(f"{a} | {b}")
        elif f == 0x26:
            w(f"{a} ^ {b}")
        elif f == 0x27:
            w(f"~({a} | {b})")
        elif f == 0x2A:
            w(f"((int32_t){a} < (int32_t){b}) ? 1u : 0u")
        elif f == 0x2B:
            w(f"({a} < {b}) ? 1u : 0u")
        else:
            self.add(self.trap(10))

    def regimm(self):
        i = self.i
        rs, rt = i.rs, i.rt
        if rt in (0x10, 0x11):
            self.add(f"cpu.gpr[31] = 0x{(i.address + 8) & 0xFFFFFFFF:08X}u;")
            self.dest = 31
        if rt in (0x00, 0x10):
            self.cond = f"(int32_t){rv(rs)} < 0"
        elif rt in (0x01, 0x11):
            self.cond = f"(int32_t){rv(rs)} >= 0"
        else:
            self.add(self.trap(10))
            return
        self.target = f"0x{i.branch_target():08X}u"

    def cop0(self):
        i = self.i
        rs, rt, rd = i.rs, i.rt, i.rd
        if rs == 0x00:
            self.add(f"t = cpu.cop0[{rd}];")
            self.load = rt
        elif rs == 0x04:
            self.add(f"cpu.cop0[{rd}] = {rv(rt)};")
            if rd in (12, 13):
                self.stop = True
        elif rs == 0x10 and i.funct == 0x10:
            self.add("{ uint32_t s = cpu.cop0[12]; cpu.cop0[12] = (s & ~0x0Fu) | ((s >> 2u) & 0x0Fu); }")
            self.stop = True
        else:
            self.add(self.trap(10))


def block_function(module, bid, leader, body, delay_of):
    """The C++ of one block. delay_of[k] is the index of the branch whose delay slot instruction k is."""
    out = []
    n = len(body)
    gens = []
    for k, insn in enumerate(body):
        branch = delay_of.get(k)
        if branch is None:
            ds = "bp"
        else:
            ds = "true" if body[branch].is_unconditional() else "(cc || bp)"
        gens.append(Gen(insn, ds).emit())

    # a run that resumes at instruction k with a delayed load waiting from the instruction before it (in the same block)
    pred_load = {}
    for k in range(1, n):
        if gens[k - 1].load:
            pred_load[k] = gens[k - 1].load

    out.append(f"static uint32_t b{bid}(AotContext &x, AotCpu &cpu, uint32_t limit, uint32_t entry)  // 0x{leader:08X}")
    out.append("{")
    out.append("    uint32_t n = 0, lv = 0, t = 0, bt = cpu.branchTarget, pr = cpu.pendingLoadReg, pv = cpu.pendingLoadValue, lim;")
    out.append("    bool hp = cpu.pendingLoad, bp = cpu.branchPending, cc = false, first;")
    out.append("    cpu.pendingLoad = false;")
    out.append("    cpu.branchPending = false;")
    special = []
    for k in range(1, n):
        if k in delay_of:
            special.append(f"    case {k}: cc = bp; bp = false; break;")
        elif k in pred_load:
            special.append(f"    case {k}: if (hp) {{ lv = pv; hp = false; }} break;")
    if special:
        out.append("    switch (entry)")
        out.append("    {")
        out.extend(special)
        out.append("    default: break;")
        out.append("    }")
    out.append("    first = hp || bp;")
    out.append("    lim = first ? 1u : limit;")
    if n > 1:
        out.append("    switch (entry)")
        out.append("    {")
        for k in range(1, n):
            out.append(f"    case {k}: goto I{k};")
        out.append("    default: break;")
        out.append("    }")

    for k, insn in enumerate(body):
        g = gens[k]
        if k > 0:
            out.append(f"I{k}:")
        for line in g.code:
            out.append("    " + line)
        if g.cond is not None:
            cond = "true" if g.cond == "" else f"({g.cond})"
            out.append(f"    cc = {cond}; bt = {g.target};")
        # the load that was waiting from the previous instruction lands (unless this one wrote the same register)
        if k in pred_load and pred_load[k] != g.dest:
            out.append(f"    cpu.gpr[{pred_load[k]}] = lv;")
        if g.load:
            out.append("    lv = t;")
        if g.stop:
            out.append("    x.stop = true; lim = 0;")
        kind = 1 if g.cond is not None else (2 if k in delay_of else 0)
        out.append(f"    if (++n >= lim && aotAfter(x, cpu, n, limit, lim, first, hp, bp, pr, pv, bt, cc, lv, {g.dest}u, {kind}u, 0x{insn.address + 4:08X}u, {g.load or 0}u)) return n;")
    last = body[-1]
    last_g = gens[-1]
    last_cc = "cc" if delay_of.get(n - 1) is not None else "false"
    out.append(f"    return aotEnd(cpu, n, {last_cc}, bt, 0x{last.address + 4:08X}u, {last_g.load or 0}u, lv);")
    out.append("}")
    return out


def generate(module, export=None):
    insns, leaders = discover(module)
    blocks = build_blocks(module, insns, leaders)
    # table: word index -> (block id + 1, index in block); a leader's entry is its own block's first instruction
    block_of = [0] * module.words
    index_of = [0] * module.words
    for bid, (leader, body) in enumerate(blocks):
        for k, insn in enumerate(body):
            w = (insn.address - module.base) // 4
            if insn.address in leaders and k != 0:
                continue  # a leader inside another block (a delay slot that is also a leader): its own block owns the entry
            block_of[w] = bid + 1
            index_of[w] = k
    out = []
    out.append(f"// Generated by tools/iop_aot.py from {module.name}.IRX at 0x{module.base:X} (image hash 0x{module.hash:016X}). Do not edit; never commit.")
    out.append(f"// {len(insns)} instruction(s) in {len(blocks)} block(s).")
    out.append("")
    out.append('#include "ps2x/iop/iop_aot.h"')
    out.append("")
    out.append("using namespace ps2x::iop;")
    out.append("")
    out.append("namespace")
    out.append("{")
    out.append(f"    constexpr uint32_t kBase = 0x{module.base:X}u;")
    out.append(f"    constexpr uint32_t kWords = {module.words}u;")
    out.append("")
    stats = {"instructions": len(insns), "blocks": len(blocks)}
    for bid, (leader, body) in enumerate(blocks):
        delay_of = {}
        for k, insn in enumerate(body):
            if insn.kind != "plain" and k + 1 < len(body):
                delay_of[k + 1] = k
        for line in block_function(module, bid, leader, body, delay_of):
            out.append(line)
        out.append("")
    out.append("    using BlockFunction = uint32_t (*)(AotContext &, AotCpu &, uint32_t, uint32_t);")
    out.append("    const BlockFunction kBlocks[] = {")
    for bid in range(len(blocks)):
        out.append(f"        b{bid},")
    out.append("    };")
    out.append("    const uint16_t kBlockOf[kWords] = {")
    for i in range(0, len(block_of), 16):
        out.append("        " + ", ".join(str(v) for v in block_of[i:i + 16]) + ",")
    out.append("    };")
    out.append("    const uint16_t kIndexOf[kWords] = {")
    for i in range(0, len(index_of), 16):
        out.append("        " + ", ".join(str(v) for v in index_of[i:i + 16]) + ",")
    out.append("    };")
    out.append("")
    out.append("    uint32_t run(AotContext &x, AotCpu &cpu, uint32_t limit)")
    out.append("    {")
    out.append("        uint32_t n = 0;")
    out.append("        cpu.exception = false;")
    out.append("        while (n < limit)")
    out.append("        {")
    out.append("            const uint32_t offset = (cpu.pc & 0x1FFFFFFFu) - kBase;")
    out.append("            if (offset >= kWords * 4u || (offset & 3u) != 0u)")
    out.append("                break;")
    out.append("            const uint32_t word = offset >> 2;")
    out.append("            const uint32_t block = kBlockOf[word];")
    out.append("            if (block == 0u || x.native[word] != 0u)")
    out.append("                break;")
    out.append("            n += kBlocks[block - 1u](x, cpu, limit - n, kIndexOf[word]);")
    out.append("            if (x.stop || cpu.exception)")
    out.append("                break;")
    out.append("        }")
    out.append("        return n;")
    out.append("    }")
    out.append("")
    out.append(f"    const AotModule kModule = {{\"{module.name}.IRX\", 0x{module.base:X}u, {module.image_size}u, 0x{module.hash:016X}ull, kWords, &run}};")
    out.append("    const AotRegistrar kRegistrar{kModule};")
    out.append("}")
    if export:
        out.append(f"AotRun g_aot_run_{export} = &run;")
    out.append("")
    return "\n".join(out), stats


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=str(ROOT / "work" / "iop_aot"))
    parser.add_argument("--only", help="comma separated module names")
    parser.add_argument("--raw", help="translate a flat code image instead (used by tools/iop_aot_fuzz.py): the file")
    parser.add_argument("--base", help="with --raw: the address of the image (hex)")
    parser.add_argument("--name", default="FUZZ", help="with --raw: the module name")
    parser.add_argument("--seeds", help="with --raw: a JSON list of code addresses to start from")
    args = parser.parse_args()
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    if args.raw:
        module = RawModule(args.name, int(args.base, 16), Path(args.raw).read_bytes(), json.loads(Path(args.seeds).read_text()) if args.seeds else [int(args.base, 16)])
        text, stats = generate(module, export=args.name)
        path = out_dir / f"{args.name}.cpp"
        path.write_text(text, encoding="utf-8", newline="\n")
        print(f"{path}: {stats['instructions']} instruction(s) in {stats['blocks']} block(s)")
        return
    only = {name.strip().upper() for name in args.only.split(",")} if args.only else None
    manifest = {}
    for name, base in MODULES:
        if only and name not in only:
            continue
        module = Module(name, base)
        text, stats = generate(module)
        path = out_dir / f"{name}.cpp"
        path.write_text(text, encoding="utf-8", newline="\n")
        manifest[name] = dict(stats, base=base, hash=f"0x{module.hash:016X}", bytes=len(text))
        print(f"{path}: {stats['instructions']} instruction(s) in {stats['blocks']} block(s), {len(text) // 1024} KB")
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
