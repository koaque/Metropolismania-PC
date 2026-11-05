"""Read the game's VU1 microcode and captured VU1 runs: a disassembler, the ELF's microcode upload packets, and a summary of
each record (which programs are loaded, where it starts, what it reads and what it draws).

    python tools/vu1_inspect.py packets [--elf original/SLUS_203.77] [--out work/vu1_dis]
        lists the microcode upload packets (DMA RET tag + VIF MPG) and writes one disassembly per packet
    python tools/vu1_inspect.py records [--dir work/vu1_aot/records] [--elf ...]
        one line per record: kind, entry, loaded packets, TOP/ITOP, data read, GIF output
    python tools/vu1_inspect.py record FILE [--elf ...] [--trace]
        one record in detail: header words, the GIF tags of every packet, and (with --trace) the instructions it reaches
    python tools/vu1_inspect.py dis FILE.vu1r START END
        disassembles a byte range of a record's code memory

Everything this prints comes from the user's own disc or game runs, so its output stays in work/ and is never committed.
"""

import argparse
import hashlib
import os
import struct
import sys

ELF_DEFAULT = "original/SLUS_203.77"
PACKETS_FIRST = 0x001A3C80
PACKETS_END = 0x001AB1C0

# ---------------------------------------------------------------- disassembler

DEST = "xyzw"
BC = "xyzw"


def dest_str(word):
    d = (word >> 21) & 0xF
    return "".join(DEST[i] for i in range(4) if d & (8 >> i))


def vf(n):
    return "vf%02d" % n


def vi(n):
    return "vi%02d" % n


def simm11(word):
    v = word & 0x7FF
    return v - 0x800 if v & 0x400 else v


def f32(word):
    return struct.unpack("<f", struct.pack("<I", word & 0xFFFFFFFF))[0]


def dis_upper(w):
    op = w & 0x3F
    ft, fs, fd = (w >> 16) & 0x1F, (w >> 11) & 0x1F, (w >> 6) & 0x1F
    d = dest_str(w)
    if op < 0x1C:
        name = ["ADD", "SUB", "MADD", "MSUB", "MAX", "MINI", "MUL"][op >> 2] + BC[op & 3]
        return "%s.%s %s, %s, %s%s" % (name, d, vf(fd), vf(fs), vf(ft), BC[op & 3])
    simple = {0x1C: ("MULq", "Q"), 0x1D: ("MAXi", "I"), 0x1E: ("MULi", "I"), 0x1F: ("MINIi", "I"), 0x20: ("ADDq", "Q"),
              0x21: ("MADDq", "Q"), 0x22: ("ADDi", "I"), 0x23: ("MADDi", "I"), 0x24: ("SUBq", "Q"), 0x25: ("MSUBq", "Q"),
              0x26: ("SUBi", "I"), 0x27: ("MSUBi", "I")}
    if op in simple:
        name, src = simple[op]
        return "%s.%s %s, %s, %s" % (name, d, vf(fd), vf(fs), src)
    three = {0x28: "ADD", 0x29: "MADD", 0x2A: "MUL", 0x2B: "MAX", 0x2C: "SUB", 0x2D: "MSUB", 0x2E: "OPMSUB", 0x2F: "MINI"}
    if op in three:
        return "%s.%s %s, %s, %s" % (three[op], d, vf(fd), vf(fs), vf(ft))
    if op >= 0x3C:
        ext = (w & 3) | ((w >> 4) & 0x7C)
        if ext < 0x10 or 0x18 <= ext < 0x1C:
            base = {0: "ADDA", 1: "SUBA", 2: "MADDA", 3: "MSUBA", 6: "MULA"}[ext >> 2]
            return "%s%s.%s ACC, %s, %s%s" % (base, BC[ext & 3], d, vf(fs), vf(ft), BC[ext & 3])
        if 0x10 <= ext < 0x18:
            name = ("ITOF" if ext < 0x14 else "FTOI") + ["0", "4", "12", "15"][ext & 3]
            return "%s.%s %s, %s" % (name, d, vf(ft), vf(fs))
        acc = {0x1C: ("MULAq", "Q"), 0x1E: ("MULAi", "I"), 0x20: ("ADDAq", "Q"), 0x21: ("MADDAq", "Q"), 0x22: ("ADDAi", "I"),
               0x23: ("MADDAi", "I"), 0x24: ("SUBAq", "Q"), 0x25: ("MSUBAq", "Q"), 0x26: ("SUBAi", "I"), 0x27: ("MSUBAi", "I")}
        if ext in acc:
            name, src = acc[ext]
            return "%s.%s ACC, %s, %s" % (name, d, vf(fs), src)
        if ext == 0x1D:
            return "ABS.%s %s, %s" % (d, vf(ft), vf(fs))
        if ext == 0x1F:
            return "CLIPw.xyz %s, %sw" % (vf(fs), vf(ft))
        accs = {0x28: "ADDA", 0x29: "MADDA", 0x2A: "MULA", 0x2C: "SUBA", 0x2D: "MSUBA", 0x2E: "OPMULA"}
        if ext in accs:
            return "%s.%s ACC, %s, %s" % (accs[ext], d, vf(fs), vf(ft))
        if ext in (0x2F, 0x30):
            return "NOP"
    return "??upper %08x" % w


def dis_lower(w, pc):
    if w == 0x8000033C:
        return "NOP"
    hi = (w >> 25) & 0x7F
    it, is_, id_ = (w >> 16) & 0x1F, (w >> 11) & 0x1F, (w >> 6) & 0x1F
    ft, fs = it, is_
    d = dest_str(w)
    fsf, ftf = BC[(w >> 21) & 3], BC[(w >> 23) & 3]
    target = (pc + 8 + simm11(w) * 8) & 0x3FFF
    if hi == 0x40:
        f = w & 0x3F
        if f == 0x30:
            return "IADD %s, %s, %s" % (vi(id_), vi(is_), vi(it))
        if f == 0x31:
            return "ISUB %s, %s, %s" % (vi(id_), vi(is_), vi(it))
        if f == 0x32:
            imm = id_ - 32 if id_ & 0x10 else id_
            return "IADDI %s, %s, %d" % (vi(it), vi(is_), imm)
        if f == 0x34:
            return "IAND %s, %s, %s" % (vi(id_), vi(is_), vi(it))
        if f == 0x35:
            return "IOR %s, %s, %s" % (vi(id_), vi(is_), vi(it))
        if f >= 0x3C:
            e = (w & 3) | ((w >> 4) & 0x7C)
            table = {
                0x30: "MOVE.%s %s, %s" % (d, vf(ft), vf(fs)),
                0x31: "MR32.%s %s, %s" % (d, vf(ft), vf(fs)),
                0x34: "LQI.%s %s, (%s++)" % (d, vf(ft), vi(is_)),
                0x35: "SQI.%s %s, (%s++)" % (d, vf(fs), vi(it)),
                0x36: "LQD.%s %s, (--%s)" % (d, vf(ft), vi(is_)),
                0x37: "SQD.%s %s, (--%s)" % (d, vf(fs), vi(it)),
                0x38: "DIV Q, %s%s, %s%s" % (vf(fs), fsf, vf(ft), ftf),
                0x39: "SQRT Q, %s%s" % (vf(ft), ftf),
                0x3A: "RSQRT Q, %s%s, %s%s" % (vf(fs), fsf, vf(ft), ftf),
                0x3B: "WAITQ",
                0x3C: "MTIR %s, %s%s" % (vi(it), vf(fs), fsf),
                0x3D: "MFIR.%s %s, %s" % (d, vf(ft), vi(is_)),
                0x3E: "ILWR.%s %s, (%s)" % (d, vi(it), vi(is_)),
                0x3F: "ISWR.%s %s, (%s)" % (d, vi(it), vi(is_)),
                0x40: "RNEXT.%s %s, R" % (d, vf(ft)),
                0x41: "RGET.%s %s, R" % (d, vf(ft)),
                0x42: "RINIT R, %s%s" % (vf(fs), fsf),
                0x43: "RXOR R, %s%s" % (vf(fs), fsf),
                0x64: "MFP.%s %s, P" % (d, vf(ft)),
                0x68: "XTOP %s" % vi(it),
                0x69: "XITOP %s" % vi(it),
                0x6C: "XGKICK %s" % vi(is_),
                0x70: "ESADD P, %s" % vf(fs),
                0x71: "ERSADD P, %s" % vf(fs),
                0x72: "ELENG P, %s" % vf(fs),
                0x73: "ERLENG P, %s" % vf(fs),
                0x74: "EATANxy P, %s" % vf(fs),
                0x75: "EATANxz P, %s" % vf(fs),
                0x76: "ESUM P, %s" % vf(fs),
                0x78: "ESQRT P, %s%s" % (vf(fs), fsf),
                0x79: "ERSQRT P, %s%s" % (vf(fs), fsf),
                0x7A: "ERCPR P, %s%s" % (vf(fs), fsf),
                0x7B: "WAITP",
                0x7C: "ESIN P, %s%s" % (vf(fs), fsf),
                0x7D: "EATAN P, %s%s" % (vf(fs), fsf),
                0x7E: "EEXP P, %s%s" % (vf(fs), fsf),
            }
            if e in table:
                return table[e]
        return "??lower %08x" % w
    imm11 = simm11(w)
    imm15 = (((w >> 21) & 0xF) << 11) | (w & 0x7FF)
    imm12 = (((w >> 21) & 1) << 11) | (w & 0x7FF)
    imm24 = w & 0xFFFFFF
    table = {
        0x00: lambda: "LQ.%s %s, %d(%s)" % (d, vf(ft), imm11, vi(is_)),
        0x01: lambda: "SQ.%s %s, %d(%s)" % (d, vf(fs), imm11, vi(it)),
        0x04: lambda: "ILW.%s %s, %d(%s)" % (d, vi(it), imm11, vi(is_)),
        0x05: lambda: "ISW.%s %s, %d(%s)" % (d, vi(it), imm11, vi(is_)),
        0x08: lambda: "IADDIU %s, %s, 0x%x" % (vi(it), vi(is_), imm15),
        0x09: lambda: "ISUBIU %s, %s, 0x%x" % (vi(it), vi(is_), imm15),
        0x10: lambda: "FCEQ vi01, 0x%06x" % imm24,
        0x11: lambda: "FCSET 0x%06x" % imm24,
        0x12: lambda: "FCAND vi01, 0x%06x" % imm24,
        0x13: lambda: "FCOR vi01, 0x%06x" % imm24,
        0x14: lambda: "FSEQ %s, 0x%03x" % (vi(it), imm12),
        0x15: lambda: "FSSET 0x%03x" % imm12,
        0x16: lambda: "FSAND %s, 0x%03x" % (vi(it), imm12),
        0x17: lambda: "FSOR %s, 0x%03x" % (vi(it), imm12),
        0x18: lambda: "FMEQ %s, %s" % (vi(it), vi(is_)),
        0x1A: lambda: "FMAND %s, %s" % (vi(it), vi(is_)),
        0x1B: lambda: "FMOR %s, %s" % (vi(it), vi(is_)),
        0x1C: lambda: "FCGET %s" % vi(it),
        0x20: lambda: "B 0x%04x" % target,
        0x21: lambda: "BAL %s, 0x%04x" % (vi(it), target),
        0x24: lambda: "JR %s" % vi(is_),
        0x25: lambda: "JALR %s, %s" % (vi(it), vi(is_)),
        0x28: lambda: "IBEQ %s, %s, 0x%04x" % (vi(it), vi(is_), target),
        0x29: lambda: "IBNE %s, %s, 0x%04x" % (vi(it), vi(is_), target),
        0x2C: lambda: "IBLTZ %s, 0x%04x" % (vi(is_), target),
        0x2D: lambda: "IBGTZ %s, 0x%04x" % (vi(is_), target),
        0x2E: lambda: "IBLEZ %s, 0x%04x" % (vi(is_), target),
        0x2F: lambda: "IBGEZ %s, 0x%04x" % (vi(is_), target),
    }
    if hi in table:
        return table[hi]()
    return "??lower %08x" % w


def branch_target(lower, upper, pc):
    """The target of a branch pair, or None."""
    if upper & 0x80000000:
        return None
    hi = (lower >> 25) & 0x7F
    if hi in (0x20, 0x21, 0x28, 0x29, 0x2C, 0x2D, 0x2E, 0x2F):
        return (pc + 8 + simm11(lower) * 8) & 0x3FFF
    return None


def dis_pair(code, pc, labels=None):
    lower, upper = struct.unpack_from("<II", code, pc)
    flags = "".join(c for bit, c in ((31, "I"), (30, "E"), (29, "M"), (28, "D"), (27, "T")) if upper & (1 << bit))
    up = dis_upper(upper)
    if upper & 0x80000000:
        low = "LOI %r (0x%08x)" % (f32(lower), lower)
    else:
        low = dis_lower(lower, pc)
    label = ""
    if labels and pc in labels:
        label = "L%04x:\n" % pc
    return "%s  %04x: %-4s %-38s | %s" % (label, pc, flags, up, low)


# ---------------------------------------------------------------- ELF microcode packets

def load_elf(path):
    data = open(path, "rb").read()
    phoff = struct.unpack_from("<I", data, 0x1C)[0]
    count = struct.unpack_from("<H", data, 0x2C)[0]
    segments = []
    for i in range(count):
        kind, offset, vaddr, _, filesz, _, _, _ = struct.unpack_from("<8I", data, phoff + i * 32)
        if kind == 1 and filesz:
            segments.append((vaddr, offset, filesz))

    def read(address, size):
        for vaddr, offset, filesz in segments:
            if vaddr <= address and address + size <= vaddr + filesz:
                return data[offset + address - vaddr: offset + address - vaddr + size]
        raise ValueError("address 0x%08x not in the ELF" % address)

    return read


def parse_packet(read, address):
    """One microcode packet: a DMA tag (RET) whose body is VIF codes. Returns (end, uploads, others) where uploads are
    (vu address, code bytes) and others are the other VIF codes as text."""
    tag = struct.unpack("<Q", read(address, 8))[0]
    qwc = tag & 0xFFFF
    body = read(address + 8, 8 + qwc * 16)
    uploads, others = [], []
    pos = 0
    while pos + 4 <= len(body):
        code = struct.unpack_from("<I", body, pos)[0]
        cmd = (code >> 24) & 0x7F
        pos += 4
        if cmd == 0x4A:
            num = ((code >> 16) & 0xFF) or 256
            # MPG data starts at a 64-bit boundary of the VIF stream (the stream starts 8 bytes into the tag qword)
            if pos % 8:
                pos += 8 - pos % 8
            uploads.append(((code & 0xFFFF) * 8, body[pos: pos + num * 8]))
            pos += num * 8
        elif cmd == 0:
            continue
        else:
            others.append("vif %08x" % code)
    return address + 16 + qwc * 16, uploads, others


def all_packets(read):
    packets = []
    address = PACKETS_FIRST
    while address < PACKETS_END:
        end, uploads, others = parse_packet(read, address)
        packets.append((address, uploads, others))
        address = end
    return packets


def code_image(uploads, base=None):
    image = bytearray(base if base is not None else bytes(0x4000))
    for vu, code in uploads:
        image[vu: vu + len(code)] = code
    return image


def packet_labels(code, start, end):
    labels = set()
    for pc in range(start, end, 8):
        lower, upper = struct.unpack_from("<II", code, pc)
        t = branch_target(lower, upper, pc)
        if t is not None:
            labels.add(t)
    return labels


def cmd_packets(args):
    read = load_elf(args.elf)
    os.makedirs(args.out, exist_ok=True)
    for address, uploads, others in all_packets(read):
        parts = ", ".join("0x%04x..0x%04x" % (vu, vu + len(code)) for vu, code in uploads)
        print("0x%08x: %s%s" % (address, parts, (" + " + " ".join(others)) if others else ""))
        image = code_image(uploads)
        lines = ["; microcode packet 0x%08x (uploads %s)" % (address, parts)]
        for vu, code in uploads:
            labels = packet_labels(image, vu, vu + len(code))
            lines.append("; upload to 0x%04x, %d instructions" % (vu, len(code) // 8))
            for pc in range(vu, vu + len(code), 8):
                lines.append(dis_pair(image, pc, labels))
        with open(os.path.join(args.out, "mc_%08x.txt" % address), "w") as out:
            out.write("\n".join(lines) + "\n")
    print("disassembly in", args.out)


# ---------------------------------------------------------------- records

STATE_SIZE = 660


class Record:
    pass


def load_record(path):
    data = open(path, "rb").read()
    if data[:4] != b"VU1R":
        raise ValueError(path + ": not a VU1 record")
    version, kind, start, top, itop, max_cycles, code_size, data_size, state_size, sched_size = struct.unpack_from("<10I", data, 4)
    if version != 2 or state_size < STATE_SIZE:  # sizeof(VU1State) is 664 with the padding after the last field
        raise ValueError("%s: version %d, state size %d" % (path, version, state_size))
    r = Record()
    r.name = os.path.basename(path)
    r.kind, r.start, r.top, r.itop = kind, start, top, itop
    pos = 44
    r.cycles_before, r.cycles_after = struct.unpack_from("<QQ", data, pos)
    pos += 16
    r.before = data[pos: pos + state_size]
    pos += state_size + sched_size
    r.code = data[pos: pos + code_size]
    pos += code_size
    r.data_before = data[pos: pos + data_size]
    pos += data_size
    r.after = data[pos: pos + state_size]
    pos += state_size
    r.data_after = data[pos: pos + data_size]
    pos += data_size
    count = struct.unpack_from("<I", data, pos)[0]
    pos += 4
    r.packets = []
    for _ in range(count):
        size = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        r.packets.append(data[pos: pos + size])
        pos += size
    return r


def state_vi(state):
    return list(struct.unpack_from("<16i", state, 512))


def state_vf(state):
    return [struct.unpack_from("<4f", state, i * 16) for i in range(32)]


PRIM_TYPES = ["point", "line", "linestrip", "tri", "tristrip", "trifan", "sprite", "prim7"]
REG_NAMES = {0: "PRIM", 1: "RGBAQ", 2: "ST", 3: "UV", 4: "XYZF2", 5: "XYZ2", 6: "TEX0_1", 7: "TEX0_2", 8: "CLAMP_1",
             9: "CLAMP_2", 0xA: "FOG", 0xC: "XYZF3", 0xD: "XYZ3", 0xE: "A+D", 0xF: "NOP"}
AD_NAMES = {0x00: "PRIM", 0x01: "RGBAQ", 0x02: "ST", 0x03: "UV", 0x04: "XYZF2", 0x05: "XYZ2", 0x06: "TEX0_1", 0x07: "TEX0_2",
            0x08: "CLAMP_1", 0x09: "CLAMP_2", 0x0A: "FOG", 0x14: "TEX1_1", 0x15: "TEX1_2", 0x16: "TEX2_1", 0x17: "TEX2_2",
            0x18: "XYOFFSET_1", 0x19: "XYOFFSET_2", 0x1A: "PRMODECONT", 0x1B: "PRMODE", 0x1C: "TEXCLUT", 0x22: "SCANMSK",
            0x34: "MIPTBP1_1", 0x3B: "TEXA", 0x3D: "FOGCOL", 0x3F: "TEXFLUSH", 0x40: "SCISSOR_1", 0x41: "SCISSOR_2",
            0x42: "ALPHA_1", 0x43: "ALPHA_2", 0x44: "DIMX", 0x45: "DTHE", 0x46: "COLCLAMP", 0x47: "TEST_1", 0x48: "TEST_2",
            0x49: "PABE", 0x4A: "FBA_1", 0x4B: "FBA_2", 0x4C: "FRAME_1", 0x4D: "FRAME_2", 0x4E: "ZBUF_1", 0x4F: "ZBUF_2",
            0x50: "BITBLTBUF", 0x51: "TRXPOS", 0x52: "TRXREG", 0x53: "TRXDIR", 0x60: "SIGNAL", 0x61: "FINISH", 0x62: "LABEL"}


def prim_str(prim):
    bits = [PRIM_TYPES[prim & 7]]
    for bit, name in ((3, "gouraud"), (4, "textured"), (5, "fog"), (6, "blend"), (7, "aa"), (8, "uv"), (9, "ctxt2")):
        if prim & (1 << bit):
            bits.append(name)
    return "+".join(bits)


def gif_tags(packet):
    """The GIF tags of an XGKICK packet: dicts with nloop, eop, prim, flg, regs, and A+D register writes."""
    tags = []
    pos = 0
    while pos + 16 <= len(packet):
        low, high = struct.unpack_from("<QQ", packet, pos)
        nloop = low & 0x7FFF
        eop = (low >> 15) & 1
        pre = (low >> 46) & 1
        prim = (low >> 47) & 0x7FF
        flg = (low >> 58) & 3
        nreg = ((low >> 60) & 0xF) or 16
        regs = [(high >> (4 * i)) & 0xF for i in range(nreg)]
        tag = {"nloop": nloop, "eop": eop, "pre": pre, "prim": prim, "flg": flg, "regs": regs, "ad": [], "at": pos, "prims": set()}
        pos += 16
        if flg == 0:
            for _ in range(nloop):
                for reg in regs:
                    if reg == 0xE and pos + 16 <= len(packet):
                        value, address = struct.unpack_from("<QQ", packet, pos)
                        tag["ad"].append((address & 0xFF, value))
                    elif reg == 0 and pos + 8 <= len(packet):
                        tag["prims"].add(struct.unpack_from("<Q", packet, pos)[0] & 0x7FF)
                    pos += 16
        elif flg == 1:
            pos += (nloop * nreg * 8 + 15) // 16 * 16
        else:
            pos += nloop * 16
        tags.append(tag)
        if eop:
            break
    return tags


def tag_str(tag):
    flg = ["packed", "reglist", "image", "image"][tag["flg"]]
    regs = ",".join(REG_NAMES.get(r, "r%x" % r) for r in tag["regs"])
    s = "%s x%d [%s]" % (flg, tag["nloop"], regs)
    if tag["pre"]:
        s = "%s %s" % (prim_str(tag["prim"]), s)
    if tag["prims"]:
        s += " prims " + ",".join(prim_str(p) for p in sorted(tag["prims"]))
    if tag["ad"]:
        s += " {" + " ".join(AD_NAMES.get(a, "0x%02x" % a) for a, _ in tag["ad"][:8]) + ("..." if len(tag["ad"]) > 8 else "") + "}"
    if tag["eop"]:
        s += " EOP"
    return s


def match_packets(code, packets):
    """The microcode packets whose uploads are all present in this code memory."""
    found = []
    for address, uploads, _ in packets:
        if uploads and all(code[vu: vu + len(c)] == c for vu, c in uploads):
            found.append(address)
    return found


def reads_and_writes(r):
    changed = [i for i in range(0, len(r.data_before), 16) if r.data_before[i: i + 16] != r.data_after[i: i + 16]]
    return changed


def ranges(qwords):
    out = []
    for q in qwords:
        if out and q == out[-1][1] + 16:
            out[-1][1] = q
        else:
            out.append([q, q])
    return ", ".join("0x%03x" % (a // 16) if a == b else "0x%03x-0x%03x" % (a // 16, b // 16) for a, b in out)


def record_files(directory):
    return sorted(os.path.join(directory, f) for f in os.listdir(directory) if f.endswith(".vu1r"))


def summarize_packets(r):
    shapes = {}
    for packet in r.packets:
        key = " / ".join(tag_str(t) for t in gif_tags(packet))
        shapes[key] = shapes.get(key, 0) + 1
    return shapes


def cmd_records(args):
    packets = all_packets(load_elf(args.elf))
    for path in record_files(args.dir):
        r = load_record(path)
        loaded = match_packets(r.code, packets)
        written = reads_and_writes(r)
        shapes = summarize_packets(r)
        print("%s  %s 0x%04x  code %s  top 0x%03x itop 0x%03x  cycles %d  packets %d  wrote %s" % (
            r.name, "MSCAL" if r.kind == 0 else "MSCNT", r.start, ",".join("%x" % a for a in loaded) or "?", r.top, r.itop,
            r.cycles_after - r.cycles_before, len(r.packets), ranges(written) if len(written) < 12 else "%d qwords" % len(written)))
        for shape, count in sorted(shapes.items(), key=lambda kv: -kv[1])[:4]:
            print("      %3dx %s" % (count, shape[:200]))


def reached_pcs(code, start):
    """Instructions reachable from start (both ways of every branch, through the end bit's delay slot)."""
    seen, todo = set(), [start]
    while todo:
        pc = todo.pop()
        while 0 <= pc < len(code) and pc not in seen:
            seen.add(pc)
            lower, upper = struct.unpack_from("<II", code, pc)
            hi = (lower >> 25) & 0x7F
            if upper & 0x40000000:  # E bit: this pair and the next run
                seen.add(pc + 8)
                break
            if not upper & 0x80000000:
                t = branch_target(lower, upper, pc)
                if t is not None:
                    todo.append(t)
                    if hi in (0x20, 0x21):
                        seen.add(pc + 8)
                        break
                if hi in (0x24, 0x25):
                    seen.add(pc + 8)
                    break
            pc += 8
    return sorted(seen)


def cmd_record(args):
    packets = all_packets(load_elf(args.elf))
    r = load_record(args.file)
    print("%s: %s 0x%04x, top 0x%03x, itop 0x%03x, cycles %d, code %s" % (
        r.name, "MSCAL" if r.kind == 0 else "MSCNT", r.start, r.top, r.itop, r.cycles_after - r.cycles_before,
        ",".join("%x" % a for a in match_packets(r.code, packets)) or "?"))
    print("vi before:", " ".join("%d:%x" % (i, v & 0xFFFF) for i, v in enumerate(state_vi(r.before)) if v))
    print("vi after: ", " ".join("%d:%x" % (i, v & 0xFFFF) for i, v in enumerate(state_vi(r.after)) if v))
    print("data changed:", ranges(reads_and_writes(r)))
    for i, packet in enumerate(r.packets[: args.packets]):
        print("packet %d (%d bytes):" % (i, len(packet)))
        for tag in gif_tags(packet):
            print("    +0x%03x %s" % (tag["at"], tag_str(tag)))
            for address, value in tag["ad"]:
                print("          %-10s %016x" % (AD_NAMES.get(address, "0x%02x" % address), value))
    if args.trace:
        pcs = reached_pcs(r.code, r.start)
        labels = packet_labels(r.code, min(pcs), max(pcs) + 8)
        for pc in pcs:
            print(dis_pair(r.code, pc, labels))


def cmd_dis(args):
    r = load_record(args.file)
    start, end = int(args.start, 0), int(args.end, 0)
    labels = packet_labels(r.code, start, end)
    for pc in range(start, end, 8):
        print(dis_pair(r.code, pc, labels))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("packets")
    p.add_argument("--elf", default=ELF_DEFAULT)
    p.add_argument("--out", default="work/vu1_dis")
    p = sub.add_parser("records")
    p.add_argument("--elf", default=ELF_DEFAULT)
    p.add_argument("--dir", default="work/vu1_aot/records")
    p = sub.add_parser("record")
    p.add_argument("file")
    p.add_argument("--elf", default=ELF_DEFAULT)
    p.add_argument("--packets", type=int, default=2)
    p.add_argument("--trace", action="store_true")
    p = sub.add_parser("dis")
    p.add_argument("file")
    p.add_argument("start")
    p.add_argument("end")
    args = parser.parse_args()
    {"packets": cmd_packets, "records": cmd_records, "record": cmd_record, "dis": cmd_dis}[args.cmd](args)


if __name__ == "__main__":
    main()
