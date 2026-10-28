#!/usr/bin/env python3
"""Inspect the IOP modules (IRX files) of the user's disc (plan P5).

    python tools/irx_inspect.py summary [FILE...]          sections, relocations, imports, exports, symbols of each IRX
    python tools/irx_inspect.py flat FILE --base 0x5E300 -o OUT
                                                           the module as the emulator loads it (segments copied and relocated)
    python tools/irx_inspect.py stabs FILE [-o OUT.json]   YYSOUND's debug info: functions, parameters, locals, line numbers, types
    python tools/irx_inspect.py export FILE                writes config/iop/<module>_functions.csv and config/iop/<module>_types.h

Only YYSOUND.IRX has debug info (GCC 2 stabs inside an ECOFF .mdebug section, source file yysound.c); the nine Sony modules are
stripped. Everything written to work/ is game-derived and stays out of git; the function list and the struct header in config/iop/
are the same kind of data as config/funcmap.csv and config/types/ (names and layouts, no code).

The relocation code mirrors ps2xIOP's IopModuleLoader (work/PS2Recomp/ps2xIOP/src/emulator/services/iop_module_loader.cpp).
"""
import argparse
import csv
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SYS_DIR = ROOT / "original" / "SYS"
WORK_IOP = ROOT / "work" / "iop"
CONFIG_IOP = ROOT / "config" / "iop"

PT_LOAD = 1
PT_SCE_IOPMOD = 0x70000080
SHT_SYMTAB, SHT_REL, SHT_RELA, SHT_NOBITS = 2, 9, 4, 8
R_MIPS_16, R_MIPS_32, R_MIPS_REL32, R_MIPS_26, R_MIPS_HI16, R_MIPS_LO16 = 1, 2, 3, 4, 5, 6
ET_SCE_IOPRELEXEC, ET_SCE_IOPRELEXEC2 = 0xFF80, 0xFF81


def align_up(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1) if alignment > 1 else value


class Irx:
    """A parsed IRX (a 32 bit little endian MIPS ELF with Sony's module info segment)."""

    def __init__(self, path):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        d = self.data
        if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
            raise ValueError(f"{path}: not a 32 bit little endian ELF")
        (self.type, self.machine, _v, self.entry, self.phoff, self.shoff, _flags, _eh, self.phentsize, self.phnum,
         self.shentsize, self.shnum, self.shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", d, 16)
        self.phdrs = []
        for i in range(self.phnum):
            vals = struct.unpack_from("<IIIIIIII", d, self.phoff + i * self.phentsize)
            self.phdrs.append(dict(zip(("type", "offset", "vaddr", "paddr", "filesz", "memsz", "flags", "align"), vals)))
        self.sections = []
        for i in range(self.shnum):
            vals = struct.unpack_from("<IIIIIIIIII", d, self.shoff + i * self.shentsize)
            self.sections.append(dict(zip(("name_off", "type", "flags", "addr", "offset", "size", "link", "info", "addralign", "entsize"), vals)))
        stro = self.sections[self.shstrndx]["offset"]
        for s in self.sections:
            s["name"] = self.cstring(stro + s["name_off"])

    def cstring(self, offset):
        end = self.data.index(b"\0", offset)
        return self.data[offset:end].decode("latin-1")

    def section(self, name):
        for s in self.sections:
            if s["name"] == name:
                return s
        return None

    def section_bytes(self, name):
        s = self.section(name)
        return self.data[s["offset"]:s["offset"] + s["size"]] if s else b""

    def module_info(self):
        """The PT_SCE_IOPMOD segment: entry, gp and the sizes; image offsets (relative to the load base)."""
        for p in self.phdrs:
            if p["type"] == PT_SCE_IOPMOD:
                off = p["offset"]
                _info, entry, gp, text, data, bss, version = struct.unpack_from("<IIIIIIH", self.data, off)
                name = self.data[off + 26:off + p["filesz"]].split(b"\0")[0].decode("latin-1")
                return {"entry": entry, "gp": gp, "text_size": text, "data_size": data, "bss_size": bss, "version": version, "name": name}
        return None

    def symbols(self):
        sec = self.section(".symtab")
        if not sec:
            return []
        strtab = self.sections[sec["link"]]["offset"]
        out = []
        for i in range(sec["size"] // 16):
            name, value, size, info, _other, shndx = struct.unpack_from("<IIIBBH", self.data, sec["offset"] + 16 * i)
            out.append({"name": self.cstring(strtab + name), "value": value, "size": size, "type": info & 15, "bind": info >> 4, "shndx": shndx})
        return out

    def load_span(self):
        loads = [p for p in self.phdrs if p["type"] == PT_LOAD and p["memsz"]]
        min_v = min(p["vaddr"] for p in loads)
        max_v = max(p["vaddr"] + p["memsz"] for p in loads)
        return min_v, align_up(max_v - min_v, 0x100)

    def libraries(self):
        """Import and export tables found in .text: [{'kind': 'import'|'export', 'name', 'version', 'offset', 'stubs': [(offset, ordinal)]}].
        An import record is the 20 byte 0x41e00000 header followed by 8 byte stubs (jr ra; addiu zero, zero, ordinal); an export
        record is the 0x41c00000 header followed by the exported function addresses, ended by a zero word."""
        text = self.section_bytes(".text")
        base = self.section(".text")["addr"]
        out = []
        i = 0
        while i + 20 <= len(text):
            magic = struct.unpack_from("<I", text, i)[0]
            if magic in (0x41E00000, 0x41C00000):
                version, _flags = struct.unpack_from("<HH", text, i + 8)
                name = text[i + 12:i + 20].split(b"\0")[0].decode("latin-1")
                j = i + 20
                entries = []
                if magic == 0x41E00000:
                    while j + 8 <= len(text):
                        a, b = struct.unpack_from("<II", text, j)
                        if a == 0x03E00008 and (b >> 16) == 0x2400:
                            entries.append((base + j, b & 0xFFFF))
                            j += 8
                        else:
                            break
                else:
                    # ordinals 0 to 3 are the start, reserved and shutdown slots and may be zero; the list ends at the next zero
                    ordinal = 0
                    while j + 4 <= len(text) and (ordinal < 4 or struct.unpack_from("<I", text, j)[0] != 0):
                        if struct.unpack_from("<I", text, j)[0] != 0:
                            entries.append((base + j, ordinal))
                        ordinal += 1
                        j += 4
                    j += 4
                out.append({"kind": "import" if magic == 0x41E00000 else "export", "name": name, "version": version, "offset": base + i, "stubs": entries})
                i = j
            else:
                i += 4
        return out


def relocated_image(irx, base):
    """The module as IopModuleLoader leaves it in IOP RAM at `base`: the PT_LOAD segments copied (memsz beyond filesz zeroed), then every
    REL entry applied with the loader's rules for Sony relocatable images (offsets are image offsets, symbol 0 means the load base,
    HI16 is held until the next LO16 of the same symbol). Returns (bytes, unresolved HI16 count)."""
    min_v, span = irx.load_span()
    if min_v != 0:
        raise ValueError("expected a module that starts at image offset 0")
    img = bytearray(span)
    for p in irx.phdrs:
        if p["type"] == PT_LOAD and p["memsz"]:
            img[p["vaddr"]:p["vaddr"] + p["filesz"]] = irx.data[p["offset"]:p["offset"] + p["filesz"]]
    delta = base - min_v
    relocatable = irx.type in (ET_SCE_IOPRELEXEC, ET_SCE_IOPRELEXEC2)
    symbols = irx.symbols()
    pending = []

    def word_at(offset):
        return struct.unpack_from("<I", img, offset)[0]

    def put(offset, value):
        struct.pack_into("<I", img, offset, value & 0xFFFFFFFF)

    for sec in irx.sections:
        if sec["type"] not in (SHT_REL, SHT_RELA) or sec["info"] >= len(irx.sections):
            continue
        entry = max(sec["entsize"], 12 if sec["type"] == SHT_RELA else 8)
        for pos in range(0, sec["size"] - entry + 1, entry):
            r_offset, r_info = struct.unpack_from("<II", irx.data, sec["offset"] + pos)
            addend_explicit = struct.unpack_from("<i", irx.data, sec["offset"] + pos + 8)[0] if sec["type"] == SHT_RELA else 0
            rtype, sym_index = r_info & 0xFF, r_info >> 8
            symbol_value = base if relocatable else 0
            if sym_index < len(symbols) and (not relocatable or sym_index != 0):
                sym = symbols[sym_index]
                symbol_value = sym["value"] + (delta if sym["shndx"] != 0 else 0)
            place = r_offset if relocatable else irx.sections[sec["info"]]["addr"] + r_offset
            word = word_at(place)
            addend = addend_explicit if sec["type"] == SHT_RELA else struct.unpack("<i", struct.pack("<I", word))[0]
            if rtype in (R_MIPS_32, R_MIPS_REL32):
                put(place, addend + symbol_value)
            elif rtype == R_MIPS_26:
                target = ((word & 0x03FFFFFF) << 2) + symbol_value
                put(place, (word & 0xFC000000) | ((target >> 2) & 0x03FFFFFF))
            elif rtype == R_MIPS_HI16:
                pending.append((place, symbol_value, sym_index))
            elif rtype == R_MIPS_LO16:
                lo = struct.unpack("<h", struct.pack("<H", word & 0xFFFF))[0]
                keep = []
                for hi_place, hi_value, hi_sym in pending:
                    if hi_sym != sym_index:
                        keep.append((hi_place, hi_value, hi_sym))
                        continue
                    hi_word = word_at(hi_place)
                    hi = struct.unpack("<h", struct.pack("<H", hi_word & 0xFFFF))[0] << 16
                    full = hi + lo + hi_value
                    put(hi_place, (hi_word & 0xFFFF0000) | (((full + 0x8000) >> 16) & 0xFFFF))
                pending = keep
                put(place, (word & 0xFFFF0000) | ((lo + symbol_value) & 0xFFFF))
            elif rtype == R_MIPS_16:
                put(place, (word & 0xFFFF0000) | ((addend + symbol_value) & 0xFFFF))
            elif rtype != 0:
                raise ValueError(f"unsupported relocation type {rtype}")
    return bytes(img), len(pending)


# ---------------------------------------------------------------- ECOFF symbolic information (stabs)

# GCC puts each stab into the ECOFF local symbol table as a symbol with index = stab type + 0x8F300
STAB_CODE_MASK = 0x8F300
N_GSYM, N_FUN, N_STSYM, N_LCSYM, N_RSYM, N_LSYM, N_PSYM, N_LBRAC, N_RBRAC = 0x20, 0x24, 0x26, 0x28, 0x40, 0x80, 0xA0, 0xC0, 0xE0
ST_LABEL, ST_PROC, ST_END = 5, 6, 8


class Ecoff:
    """The ECOFF symbolic header and local symbols of an IRX .mdebug section."""

    def __init__(self, irx):
        sec = irx.section(".mdebug")
        if not sec:
            raise ValueError(f"{irx.path.name} has no .mdebug section")
        d = irx.data
        off = sec["offset"]
        magic = struct.unpack_from("<H", d, off)[0]
        if magic != 0x7009:
            raise ValueError("bad ECOFF symbolic header magic")
        f = struct.unpack_from("<" + "I" * 23, d, off + 4)
        names = ("ilineMax cbLine cbLineOffset idnMax cbDnOffset ipdMax cbPdOffset isymMax cbSymOffset ioptMax cbOptOffset iauxMax cbAuxOffset "
                 "issMax cbSsOffset issExtMax cbSsExtOffset ifdMax cbFdOffset crfd cbRfdOffset iextMax cbExtOffset").split()
        self.hdr = dict(zip(names, f))
        h = self.hdr
        self.symbols = []
        for i in range(h["isymMax"]):
            iss, value, word = struct.unpack_from("<III", d, h["cbSymOffset"] + 12 * i)
            end = d.index(b"\0", h["cbSsOffset"] + iss)
            self.symbols.append({"i": i, "name": d[h["cbSsOffset"] + iss:end].decode("latin-1"), "value": value, "st": word & 63,
                                 "sc": (word >> 6) & 31, "index": word >> 12})
        self.procedures = []
        for i in range(h["ipdMax"]):
            adr, isym, iline, regmask, regoffset, iopt, fregmask, fregoffset, frameoffset, framereg, pcreg, lnlow, lnhigh, cbline = \
                struct.unpack_from("<IiiIiiIiiHHiiI", d, h["cbPdOffset"] + 52 * i)
            self.procedures.append({"adr": adr, "isym": isym, "regmask": regmask, "regoffset": regoffset, "frame": frameoffset,
                                    "framereg": framereg, "pcreg": pcreg})

    def stab(self, sym):
        """(stab code, string) of a stab symbol, or None. A string that ends in a backslash goes on in the next stab symbol (the
        assembler splits long strings; the joined string is what the symbol that started it returns, the rest return None)."""
        if "joined" not in sym:
            self._join()
        return sym["joined"]

    def _join(self):
        carry = None
        for sym in self.symbols:
            sym["joined"] = None
            if sym["index"] < STAB_CODE_MASK or not sym["name"]:
                continue
            code, text = sym["index"] - STAB_CODE_MASK, sym["name"]
            if carry is not None:
                carry[1]["joined"] = (carry[0], carry[1]["joined"][1] + text)
                if text.endswith("\\"):
                    carry[1]["joined"] = (carry[0], carry[1]["joined"][1][:-1])
                else:
                    carry = None
                continue
            if text.endswith("\\"):
                sym["joined"] = (code, text[:-1])
                carry = (code, sym)
            else:
                sym["joined"] = (code, text)


class Types:
    """The type table of the GNU stabs strings (typedefs t, tags T, ranges, pointers, arrays, structs, unions, enums, functions)."""

    def __init__(self):
        self.by_number = {}   # number -> description dict
        self.names = {}       # number -> typedef or tag name
        self.letters = {}     # number -> "t" (typedef) or "T" (tag)
        self.primary = {}
        self.globals = []     # the module's static variables: name, type number, address, section

    # a type description is one of
    #   {'k': 'int', 'size': n, 'signed': bool}   {'k': 'float', 'size': n}   {'k': 'ptr', 'to': n}   {'k': 'alias', 'to': n}
    #   {'k': 'array', 'count': n, 'of': n}   {'k': 'struct'|'union', 'size': n, 'fields': [(name, type, bitpos, bitsize)]}
    #   {'k': 'enum', 'values': [(name, value)]}   {'k': 'func', 'ret': n}   {'k': 'void'}   {'k': 'fwd', 'tag': 'name'}
    def parse_definition(self, text):
        """Parses 'name:t5=...' or 'name:T5=...'; returns the (kind letter, number) it defined or None for variables."""
        m = re.match(r"([^:]*):([tT])(\d+)=?", text)
        if not m:
            return None
        name, letter, number = m.group(1), m.group(2), int(m.group(3))
        self.names.setdefault(number, name)
        self.letters.setdefault(number, letter)
        if m.end() < len(text):
            self._type_body = text
            self._pos = m.end()
            self._define(number)
        return letter, number

    def parse_type_ref(self, text):
        """Parses the type of a variable ('p286=*205', '280', 'r3'...) after its class letter; returns the type number."""
        self._type_body = text
        self._pos = 0
        return self._ref()

    # -- recursive descent over the stabs type grammar
    def _peek(self):
        return self._type_body[self._pos] if self._pos < len(self._type_body) else ""

    def _number(self):
        m = re.compile(r"-?\d+").match(self._type_body, self._pos)
        if not m:
            raise ValueError(f"number expected at {self._type_body[self._pos:self._pos + 20]!r} in {self._type_body[:80]!r}")
        self._pos = m.end()
        return int(m.group(0))

    def _expect(self, ch):
        if self._peek() != ch:
            raise ValueError(f"{ch!r} expected at {self._type_body[self._pos:self._pos + 20]!r} in {self._type_body[:80]!r}")
        self._pos += 1

    def _ref(self):
        number = self._number()
        if self._peek() == "=":
            self._pos += 1
            self._define(number)
        return number

    def _define(self, number):
        c = self._peek()
        if c.isdigit() or c == "-":
            self.by_number[number] = {"k": "alias", "to": self._ref()}
        elif c == "*":
            self._pos += 1
            self.by_number[number] = {"k": "ptr", "to": self._ref()}
        elif c == "r":
            self._pos += 1
            base = self._ref()
            self._expect(";")
            lo = self._number_or_octal()
            self._expect(";")
            hi = self._number_or_octal()
            self._expect(";")
            if hi == 0 and lo > 0:
                self.by_number[number] = {"k": "float", "size": lo}
            else:
                if lo == 0 and hi == -1:          # unsigned int and unsigned long: written as 0 to -1
                    size, signed = 4, False
                elif hi >= 2 ** 32 or lo >= 2 ** 32:  # long long, limits written in octal
                    size, signed = 8, lo != 0
                elif hi > 65535 or lo < -32768:
                    size, signed = 4, lo < 0
                elif hi > 255 or lo < -128:
                    size, signed = 2, lo < 0
                else:
                    size, signed = 1, lo < 0
                self.by_number[number] = {"k": "int", "size": size, "signed": signed, "range": (lo, hi), "base": base}
        elif c == "a":
            self._pos += 1
            if self._peek() == "r":  # the index range is written in line: ar<base>;<lo>;<hi>;<element>
                self._pos += 1
                self._ref()
                self._expect(";")
                lo = self._number_or_octal()
                self._expect(";")
                hi = self._number_or_octal()
                self._expect(";")
            else:
                index = self._ref()
                lo, hi = self.by_number.get(index, {}).get("range", (0, -1))
            element = self._ref()
            self.by_number[number] = {"k": "array", "count": hi - lo + 1, "of": element}
        elif c in "su":
            self._pos += 1
            size = self._number()
            fields = []
            while self._peek() != ";":
                m = re.compile(r"([^:;]*):").match(self._type_body, self._pos)
                self._pos = m.end()
                fname = m.group(1)
                ftype = self._ref()
                self._expect(",")
                bitpos = self._number()
                self._expect(",")
                bitsize = self._number()
                self._expect(";")
                fields.append((fname, ftype, bitpos, bitsize))
            self._expect(";")
            self.by_number[number] = {"k": "struct" if c == "s" else "union", "size": size, "fields": fields}
        elif c == "e":
            self._pos += 1
            values = []
            while self._peek() != ";":
                m = re.compile(r"([^:;]*):(-?\d+),").match(self._type_body, self._pos)
                self._pos = m.end()
                values.append((m.group(1), int(m.group(2))))
            self._expect(";")
            self.by_number[number] = {"k": "enum", "values": values}
        elif c == "f":
            self._pos += 1
            self.by_number[number] = {"k": "func", "ret": self._ref()}
        elif c == "x":
            self._pos += 1
            kind = self._peek()
            self._pos += 1
            m = re.compile(r"([^:;]*):").match(self._type_body, self._pos)
            self._pos = m.end()
            self.by_number[number] = {"k": "fwd", "tag": m.group(1), "kind": kind}
        else:
            raise ValueError(f"unknown type code {c!r} in {self._type_body[:80]!r}")

    def _number_or_octal(self):
        m = re.compile(r"-?\d+").match(self._type_body, self._pos)
        text = m.group(0)
        self._pos = m.end()
        # GCC writes long long limits in octal with a leading 0
        return int(text, 8) if len(text) > 1 and text.lstrip("-").startswith("0") else int(text)

    # -- resolution helpers
    def resolve(self, number):
        seen = 0
        while True:
            t = self.by_number.get(number)
            if t is None:
                return number, {"k": "void"}
            if t["k"] == "alias" and seen < 50:
                number = t["to"]
                seen += 1
                continue
            if t["k"] == "fwd":
                for n, name in self.names.items():
                    if name == t["tag"] and self.by_number.get(n, {}).get("k") in ("struct", "union"):
                        return n, self.by_number[n]
            return number, t

    def size_of(self, number):
        number, t = self.resolve(number)
        k = t["k"]
        if k in ("int", "float"):
            return t["size"]
        if k == "ptr" or k == "func":
            return 4
        if k in ("struct", "union"):
            return t["size"]
        if k == "array":
            return t["count"] * self.size_of(t["of"])
        if k == "enum":
            return 4
        return 0

    def name_of(self, number):
        return self.names.get(number)

    BUILTIN_CXX = {"char": "char", "signed char": "int8_t", "unsigned char": "uint8_t", "short int": "int16_t", "short unsigned int": "uint16_t",
                   "int": "int32_t", "unsigned int": "uint32_t", "long int": "int32_t", "long unsigned int": "uint32_t",
                   "long long int": "int64_t", "long long unsigned int": "uint64_t", "float": "float", "double": "double",
                   "long double": "double", "void": "void"}

    # typedef names of the SDK's libc headers: emitted as the plain integer type so they cannot clash with the host's own
    LIBC_TYPEDEFS = {"size_t", "ssize_t", "u_char", "u_short", "u_int", "u_long", "unchar", "ushort", "uint", "ulong", "caddr_t"}

    def align_of(self, number):
        n, t = self.resolve(number)
        k = t["k"]
        if k in ("int", "float"):
            return min(t["size"], 4)
        if k in ("ptr", "func", "enum"):
            return 4
        if k == "array":
            return self.align_of(t["of"])
        if k in ("struct", "union"):
            return max((self.align_of(f[1]) for f in t["fields"]), default=1)
        return 1

    def layout_mismatches(self, number):
        """Members whose offset, or a struct whose size, differs from what a C++ compiler would lay out (natural alignment); the
        generated header would fail its static_asserts for these."""
        n, t = self.resolve(number)
        bad = []
        offset = 0
        for fname, ftype, bitpos, bitsize in t["fields"]:
            size = self.size_of(ftype)
            if t["k"] == "struct":
                offset = align_up(offset, self.align_of(ftype))
                if offset * 8 != bitpos:
                    bad.append(f"{self.primary.get(n)}::{fname} at +{bitpos / 8} (natural +{offset})")
                offset += size
        if t["k"] == "struct":
            size = align_up(offset, self.align_of(number))
            if size != t["size"]:
                bad.append(f"{self.primary.get(n)} size {t['size']} (natural {size})")
        return bad

    def reachable(self, roots):
        """Every type number reachable from the root type numbers (through aliases, pointers, arrays, struct members, return types)."""
        seen = set()
        stack = list(roots)
        while stack:
            number = stack.pop()
            if number in seen:
                continue
            seen.add(number)
            t = self.by_number.get(number)
            if t is None:
                continue
            k = t["k"]
            if k == "alias" or k == "ptr":
                stack.append(t["to"])
            elif k == "array":
                stack.append(t["of"])
            elif k == "func":
                stack.append(t["ret"])
            elif k in ("struct", "union"):
                stack.extend(f[1] for f in t["fields"])
            elif k == "fwd":
                stack.append(self.resolve(number)[0])
        return seen

    def finish(self):
        """Call once every definition is parsed: picks the name each struct, union and enum is declared under (its tag, else its first typedef)."""
        self.primary = {}
        for letter in ("T", "t"):
            for number, name in self.names.items():
                if self.letters.get(number) != letter:
                    continue
                n, t = self.resolve(number)
                if t["k"] in ("struct", "union", "enum") and n not in self.primary:
                    self.primary[n] = name

    C_BUILTIN = {"char": "char", "signed char": "signed char", "unsigned char": "unsigned char", "short int": "short",
                 "short unsigned int": "unsigned short", "int": "int", "unsigned int": "unsigned int", "long int": "int",
                 "long unsigned int": "unsigned int", "long long int": "long long", "long long unsigned int": "unsigned long long",
                 "float": "float", "double": "double", "long double": "double", "void": "void"}
    style = "cxx"   # "c" renders plain C with raw pointers (for Ghidra's parser), "cxx" the IopPtr form of the port

    def pointer_text(self, number):
        """The pointer type a typedef of a pointer or function pointer stands for (never the typedef name itself)."""
        _n, t = self.resolve(number)
        void_pointer = "void *" if self.style == "c" else "IopPtr<void>"
        if t["k"] == "func":
            return void_pointer
        _tn, target = self.resolve(t["to"])
        if target["k"] in ("func", "void"):
            return void_pointer
        base, dims = self._cxx(t["to"])
        if dims:
            return void_pointer
        return f"{base} *" if self.style == "c" else f"IopPtr<{base}>"

    def cxx_decl(self, number, name=""):
        """A C++ declaration of `name` with the type; pointers are IopPtr<T> (32 bit IOP addresses), function pointers IopPtr<void>."""
        base, dims = self._cxx(number)
        return f"{base} {name}{dims}".rstrip()

    def _cxx(self, number):
        n, t = self.resolve(number)
        k = t["k"]
        plain_c = self.style == "c"
        builtin = self.C_BUILTIN if plain_c else self.BUILTIN_CXX
        void_pointer = "void *" if plain_c else "IopPtr<void>"
        if k in ("struct", "union", "enum"):
            return self.primary.get(n, f"anonymous_{n}"), ""
        tname = self.names.get(number)
        if tname and tname not in self.BUILTIN_CXX and tname not in self.LIBC_TYPEDEFS and k in ("int", "float", "void", "ptr"):
            return tname, ""
        if k in ("int", "float", "void"):
            for candidate in (self.names.get(number), self.names.get(n)):
                if candidate in builtin:
                    return builtin[candidate], ""
            if k == "float":
                return ("float" if t["size"] == 4 else "double"), ""
            if k == "int":
                if plain_c:
                    signed_names = {1: "char", 2: "short", 4: "int", 8: "long long"}
                    unsigned_names = {1: "unsigned char", 2: "unsigned short", 4: "unsigned int", 8: "unsigned long long"}
                    return (signed_names if t["signed"] else unsigned_names)[t["size"]], ""
                prefix = "" if t["signed"] else "u"
                return f"{prefix}int{t['size'] * 8}_t", ""
            return "void", ""
        if k == "ptr":
            _tn, target = self.resolve(t["to"])
            if target["k"] in ("func", "void"):
                return void_pointer, ""
            base, dims = self._cxx(t["to"])
            if dims:
                return void_pointer, ""
            return (f"{base} *" if plain_c else f"IopPtr<{base}>"), ""
        if k == "array":
            base, dims = self._cxx(t["of"])
            return base, f"[{t['count']}]" + dims
        if k == "func":
            return void_pointer, ""
        return "void", ""


def parse_debug_info(irx):
    """Functions (address, size, name, return type, parameters, locals, source lines) and the type table of an IRX with stabs."""
    ecoff = Ecoff(irx)
    types = Types()
    functions = []
    current = None
    variables = []  # (function or None, code, name, type string, value)
    for sym in ecoff.symbols:
        stab = ecoff.stab(sym)
        if sym["st"] == ST_PROC:
            current = {"name": sym["name"], "address": sym["value"], "size": 0, "lines": [], "params": [], "locals": [], "return": None,
                       "frame": ecoff.procedures[sym["index"]]["frame"] if sym["index"] < len(ecoff.procedures) else 0}
            functions.append(current)
            continue
        if sym["st"] == ST_LABEL and sym["name"].startswith("$LM") and sym["index"] < STAB_CODE_MASK and current is not None:
            current["lines"].append((sym["value"], sym["index"]))
            continue
        if sym["st"] == ST_END and current is not None and sym["name"] == current["name"]:
            current["size"] = sym["value"]
            continue
        if sym["st"] == ST_LABEL and sym["name"].endswith(".c"):
            current = None
            continue
        if stab is None:
            continue
        code, text = stab
        if code == N_FUN:
            continue_to = text.split(":F", 1)
            target = next((f for f in functions if f["name"] == continue_to[0]), None)
            if target is not None and len(continue_to) == 2:
                current = target
                target["return_text"] = continue_to[1]
            continue
        if code in (N_LSYM, N_PSYM, N_RSYM, N_GSYM, N_STSYM, N_LCSYM):
            variables.append((current, code, text, sym["value"]))
    # first all type definitions (they are global even when they appear in a function's stabs), then the variables
    for _fn, code, text, _value in variables:
        if code == N_LSYM and re.match(r"[^:]*:[tT]\d+", text):
            types.parse_definition(text)
    for fn, code, text, value in variables:
        if code == N_LSYM and re.match(r"[^:]*:[tT]\d+", text):
            continue
        name, _, rest = text.partition(":")
        klass = ""
        if rest[:1] in "pPrRVGSf":
            klass, rest = rest[0], rest[1:]
        number = types.parse_type_ref(rest)
        entry = {"name": name, "type": number, "class": klass, "value": value if value < 0x80000000 else value - (1 << 32)}
        if code in (N_GSYM, N_STSYM, N_LCSYM):
            # the value is an offset in the section the variable lives in: .data (N_STSYM), .bss (N_LCSYM); N_GSYM is not used by this module
            section = irx.section(".bss" if code == N_LCSYM else ".data")
            entry["address"] = section["addr"] + value
            entry["section"] = section["name"]
            types.globals.append(entry)
            continue
        if fn is None:
            continue
        if code == N_PSYM or klass in ("p", "P"):
            fn["params"].append(entry)
        else:
            fn["locals"].append(entry)
    for fn in functions:
        if fn.get("return_text") is not None:
            fn["return"] = types.parse_type_ref(fn["return_text"])
    types.finish()
    # the real frame size is the `addiu sp, sp, -N` of the prologue (the procedure descriptor's frame size is wrong for functions that use $fp)
    image0, _unresolved = relocated_image(irx, 0)
    for fn in functions:
        fn["frame"] = 0
        for offset in range(fn["address"], min(fn["address"] + 32, fn["address"] + max(fn["size"], 4)), 4):
            word = struct.unpack_from("<I", image0, offset)[0]
            if word >> 16 == 0x27BD and word & 0x8000:  # addiu sp, sp, -N
                fn["frame"] = 0x10000 - (word & 0xFFFF)
                break
    # an initialized table declared without a length (TBL_CHOON[]) gets the room up to the next variable or the end of its section
    ordered = sorted(types.globals, key=lambda g: (g["section"], g["address"]))
    for i, g in enumerate(ordered):
        _n, t = types.resolve(g["type"])
        if t["k"] == "array" and t["count"] <= 0:
            section = irx.section(g["section"])
            limit = section["addr"] + section["size"]
            if i + 1 < len(ordered) and ordered[i + 1]["section"] == g["section"]:
                limit = ordered[i + 1]["address"]
            element = types.size_of(t["of"]) or 1
            number = max(types.by_number) + 1
            types.by_number[number] = {"k": "array", "count": (limit - g["address"]) // element, "of": t["of"]}
            g["type"] = number
    return ecoff, types, functions


def describe_function(types, fn):
    ret = types.cxx_decl(fn["return"], "") if fn["return"] is not None else "int"
    params = ", ".join(types.cxx_decl(p["type"], p["name"]) for p in fn["params"]) or "void"
    return f"{ret} {fn['name']}({params})"


def stabs_json(irx):
    ecoff, types, functions = parse_debug_info(irx)
    return {
        "module": irx.module_info(),
        "functions": [dict(fn, signature=describe_function(types, fn), line_first=min((l for _a, l in fn["lines"]), default=0),
                           line_last=max((l for _a, l in fn["lines"]), default=0)) for fn in functions],
        "types": {str(n): {"name": types.names.get(n), **{k: v for k, v in t.items()}} for n, t in types.by_number.items()},
    }


# ---------------------------------------------------------------- commands

def c_types_header(types, reached):
    """The same types as plain C (typedef struct, raw 32 bit pointers) for Ghidra's C parser."""
    types.style = "c"
    try:
        structs = {n: types.resolve(n)[1] for n in types.primary if n in reached}
        out = []
        for n, name in sorted(types.primary.items(), key=lambda kv: kv[1]):
            if n in structs and structs[n]["k"] in ("struct", "union"):
                out.append(f"typedef {structs[n]['k']} {name} {name};")
        for number, name in sorted(types.names.items()):
            n, t = types.resolve(number)
            if name in Types.BUILTIN_CXX or name in Types.LIBC_TYPEDEFS or (number not in reached and n not in reached):
                continue
            if t["k"] in ("int", "float"):
                builtin_name = types.names.get(n)
                target = Types.C_BUILTIN[builtin_name] if builtin_name in Types.C_BUILTIN else types._cxx(n)[0]
                if target != name:
                    out.append(f"typedef {target} {name};")
            elif t["k"] in ("struct", "union", "enum") and types.primary.get(n) != name:
                out.append(f"typedef {t['k']} {types.primary[n]} {name};")
            elif t["k"] in ("ptr", "func"):
                out.append(f"typedef {types.pointer_text(number)} {name};")
        for n, name in sorted(types.primary.items(), key=lambda kv: kv[1]):
            if n in structs and structs[n]["k"] == "enum":
                out.append(f"enum {name} {{ " + ", ".join(f"{v} = {x}" for v, x in structs[n]["values"]) + " };")
        order, visiting = [], set()

        def visit(n):
            if n in order or n in visiting:
                return
            visiting.add(n)
            for _fname, ftype, _bp, _bs in structs[n]["fields"]:
                m, mt = types.resolve(ftype)
                while mt["k"] == "array":
                    m, mt = types.resolve(mt["of"])
                if mt["k"] in ("struct", "union") and m in structs:
                    visit(m)
            visiting.discard(n)
            order.append(n)

        for n in sorted(structs):
            if structs[n]["k"] in ("struct", "union"):
                visit(n)
        for n in order:
            t = structs[n]
            out.append(f"{t['k']} {types.primary[n]} {{")
            for fname, ftype, _bp, _bs in t["fields"]:
                _m, ft = types.resolve(ftype)
                if ft["k"] == "array" and ft["count"] <= 0:
                    continue
                out.append(f"    {types.cxx_decl(ftype, fname)};")
            out.append("};")
        return "\n".join(out) + "\n"
    finally:
        types.style = "cxx"


def cmd_ghidra(args):
    """Everything the IRX needs for a Ghidra project: the relocated flat image, symbol names (functions, import stubs, data) at their
    absolute addresses, the types as plain C and one prototype per function. Written to work/iop/ghidra_<module>/ (game-derived)."""
    irx = Irx(args.file)
    base = int(args.base, 0)
    stem = Path(args.file).stem.lower()
    out = WORK_IOP / f"ghidra_{stem}"
    (out / "types").mkdir(parents=True, exist_ok=True)
    image, _unresolved = relocated_image(irx, base)
    (out / f"{stem}.flat").write_bytes(image)
    ecoff, types, functions = parse_debug_info(irx)
    text_index = irx.sections.index(irx.section(".text"))
    elf_names = {s["value"]: s["name"] for s in irx.symbols() if s["name"] and s["type"] == 1 and s["shndx"] == text_index}
    symbols = ["# Names for " + irx.path.name + " at its load base (written by tools/irx_inspect.py ghidra)."]
    for fn in sorted(functions, key=lambda f: f["address"]):
        symbols.append(f"0x{base + fn['address']:08X},{fn['name']},func,stabs")
    for lib in irx.libraries():
        if lib["kind"] == "import":
            for offset, ordinal in lib["stubs"]:
                name = elf_names.get(offset, f"{lib['name']}_{ordinal}")
                symbols.append(f"0x{base + offset:08X},{name},func,import")
    for s in irx.symbols():
        if s["name"] and s["type"] == 0 and s["shndx"] not in (0, 0xFFF1) and not s["name"].startswith(("gcc2", "__gnu")) and s["value"]:
            symbols.append(f"0x{base + s['value']:08X},{s['name']},label,elf")
    for g in types.globals:
        symbols.append(f"0x{base + g['address']:08X},{g['name']},label,stabs")
    (out / "symbols.csv").write_text("\n".join(symbols) + "\n", encoding="utf-8")
    roots = [fn["return"] for fn in functions if fn["return"] is not None] + [v["type"] for fn in functions for v in fn["params"] + fn["locals"]]
    roots += [g["type"] for g in types.globals]
    reached = types.reachable(roots)
    (out / "types" / f"{stem}.h").write_text(c_types_header(types, reached), encoding="utf-8")
    sigs = ["# One prototype per function (plain C), from the debug info."]
    types.style = "c"
    for fn in sorted(functions, key=lambda f: f["address"]):
        sigs.append(f"0x{base + fn['address']:08X} {describe_function(types, fn)};")
    datatypes = ["# One variable per line: 0xADDR <type>, applied by tools/ghidra/ApplyDataTypes.java."]
    for g in sorted(types.globals, key=lambda g: g["address"]):
        datatypes.append(f"0x{base + g['address']:08X} {types.cxx_decl(g['type'], '')}")
    (out / "datatypes.txt").write_text("\n".join(datatypes) + "\n", encoding="utf-8")
    types.style = "cxx"
    (out / "signatures.txt").write_text("\n".join(sigs) + "\n", encoding="utf-8")
    print(f"{out}: flat image, {len(symbols) - 1} symbol(s), {len(functions)} prototype(s), types")


def cmd_summary(args):
    files = [Path(f) for f in args.files] or sorted(SYS_DIR.glob("*.IRX"))
    total = 0
    for f in files:
        irx = Irx(f)
        info = irx.module_info()
        text = irx.section(".text")
        rel = sum(s["size"] // (s["entsize"] or 8) for s in irx.sections if s["type"] == SHT_REL)
        libs = irx.libraries()
        total += text["size"]
        print(f"{f.name}: type 0x{irx.type:X}, code {text['size']} bytes, relocations {rel}, entry 0x{info['entry']:X}, gp 0x{info['gp']:X}, "
              f"load span {irx.load_span()[1]} bytes, symbols {len(irx.symbols())}, {'with' if irx.section('.mdebug') else 'no'} debug info")
        for lib in libs:
            print(f"    {lib['kind']:6} {lib['name']:<10} v{lib['version']:04X} {len(lib['stubs'])} function(s)")
    print(f"code in {len(files)} module(s): {total} bytes ({total // 4} instructions)")


def cmd_flat(args):
    irx = Irx(args.file)
    base = int(args.base, 0)
    image, unresolved = relocated_image(irx, base)
    text = irx.section(".text")
    # sanity: every JAL in the code lands inside the code or on an import stub
    bad = 0
    for off in range(text["addr"], text["addr"] + text["size"], 4):
        word = struct.unpack_from("<I", image, off)[0]
        if word >> 26 == 3:
            target = ((word & 0x03FFFFFF) << 2)
            if not (base <= target < base + text["size"]) or target & 3:
                bad += 1
    out = Path(args.output) if args.output else WORK_IOP / (Path(args.file).stem + ".flat")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(image)
    info = irx.module_info()
    print(f"{out}: {len(image)} bytes at 0x{base:X}, entry 0x{base + info['entry']:X}, gp 0x{base + info['gp']:X}, "
          f"unresolved HI16 {unresolved}, JAL targets outside the code {bad}")


def cmd_stabs(args):
    irx = Irx(args.file)
    data = stabs_json(irx)
    out = Path(args.output) if args.output else WORK_IOP / (Path(args.file).stem.lower() + "_stabs.json")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(data, indent=1))
    funcs = data["functions"]
    print(f"{out}: {len(funcs)} function(s), {len(data['types'])} type(s)")
    for fn in funcs[:int(args.show)]:
        print(f"  0x{fn['address']:05X} {fn['size']:5}  lines {fn['line_first']}-{fn['line_last']}  {fn['signature']}")


def fnv1a64(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def module_strings(irx, base):
    """The NUL terminated strings of .rodata: (absolute address, identifier, text). The identifier is made from the first words of the text."""
    rodata = irx.section(".rodata")
    data = irx.section_bytes(".rodata")
    out, seen = [], set()
    i = 0
    while i < len(data):
        if data[i] == 0:
            i += 1
            continue
        end = data.index(b"\0", i) if b"\0" in data[i:] else len(data)
        raw = data[i:end]
        text = raw.decode("latin-1")
        words = re.findall(r"[A-Za-z0-9]+", text)[:9]
        name = "_".join(words) if words else "string"
        if name[0].isdigit():
            name = "s_" + name
        if name in seen:
            name = f"{name}_{rodata['addr'] + i:X}"
        seen.add(name)
        out.append((base + rodata["addr"] + i, name, text))
        i = end + 1
    return out


def functions_header(irx, base, functions):
    """C++ constants for the readable port of a module: the name the emulator knows it by, where it loads it, the hash of its relocated
    image (the emulator checks it before it lets the port's functions replace the module's), every function's offset in the image and
    every import stub's offset (the stubs are callable guest code: the port calls an import by calling its stub)."""
    min_v, _span = irx.load_span()
    loads = [p for p in irx.phdrs if p["type"] == PT_LOAD and p["memsz"]]
    image_size = max(p["vaddr"] + p["memsz"] for p in loads) - min_v
    image, _unresolved = relocated_image(irx, base)
    text_index = irx.sections.index(irx.section(".text"))
    stub_names = {s["value"]: s["name"] for s in irx.symbols() if s["name"] and s["type"] == 1 and s["shndx"] == text_index}
    lines = [f"// Generated by tools/irx_inspect.py export --base 0x{base:X} from {irx.path.name}. Do not edit.",
             "#pragma once", "", "#include <cstdint>", "", "namespace iop::" + irx.path.stem.lower(), "{",
             f'    inline constexpr const char *kModule = "{irx.path.name}";',
             f"    inline constexpr uint32_t kBase = 0x{base:X};",
             f"    inline constexpr uint32_t kImageSize = {image_size};",
             f"    inline constexpr uint64_t kImageHash = 0x{fnv1a64(image[:image_size]):016X}ull;  // FNV-1a over the relocated image before the module starts",
             "", "    // offsets of the module's functions", "    namespace offset", "    {"]
    for fn in sorted(functions, key=lambda f: f["address"]):
        lines.append(f"        inline constexpr uint32_t {fn['name']} = 0x{fn['address']:X};")
    lines += ["    }", "", "    // size of each function's stack frame (the original moves sp down by this much)", "    namespace frame", "    {"]
    for fn in sorted(functions, key=lambda f: f["address"]):
        lines.append(f"        inline constexpr uint32_t {fn['name']} = {fn['frame']};")
    lines += ["    }", "", "    // stack locals: offset from the stack pointer at entry (the original's debug info names them; the frame is below it)", "    namespace local", "    {"]
    for fn in sorted(functions, key=lambda f: f["address"]):
        stack = [v for v in fn["locals"] + fn["params"] if v["class"] not in ("r", "R", "p", "P") and v["value"] != 0]
        if not stack:
            continue
        lines.append(f"        namespace {fn['name']}")
        lines.append("        {")
        used = {}
        for v in stack:
            # the same name in two blocks of a function (a loop's counter): the later ones get a number
            used[v["name"]] = used.get(v["name"], 0) + 1
            name = v["name"] if used[v["name"]] == 1 else f"{v['name']}_{used[v['name']]}"
            lines.append(f"            inline constexpr int32_t {name} = {v['value']};")
        lines.append("        }")
    lines += ["    }", "", "    // strings of .rodata, by their absolute IOP address", "    namespace str", "    {"]
    for address, name, text in module_strings(irx, base):
        shown = text.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace('"', '\\"')
        lines.append(f"        inline constexpr uint32_t {name} = 0x{address:X};  // \"{shown}\"")
    lines += ["    }", "", "    // offsets of the module's import stubs (jr ra; the emulator answers the import when the IOP reaches the stub)", "    namespace stub", "    {"]
    for lib in irx.libraries():
        if lib["kind"] != "import":
            continue
        lines.append(f"        // {lib['name']}")
        for offset, ordinal in lib["stubs"]:
            name = stub_names.get(offset, f"{lib['name']}_{ordinal}")
            lines.append(f"        inline constexpr uint32_t {name} = 0x{offset:X};")
    lines += ["    }", "}"]
    return "\n".join(lines) + "\n"


def globals_header_text(irx, types, base):
    """The module's static variables as iop::Var<T> and iop::ArrayVar<T, N> at their absolute addresses."""
    stem = irx.path.stem.lower()
    lines = [f"// Generated by tools/irx_inspect.py export --base 0x{base:X} from {irx.path.name}. Do not edit.",
             "#pragma once", "", '#include "iop_ptr.h"', f'#include "{stem}_types.h"', "", f"namespace iop::{stem}", "{"]
    for g in sorted(types.globals, key=lambda g: g["address"]):
        _n, t = types.resolve(g["type"])
        address = f"0x{base + g['address']:X}"
        if t["k"] == "array":
            lines.append(f"    inline ArrayVar<{types.cxx_decl(t['of'], '')}, {t['count']}> {g['name']}{{{address}}};")
        else:
            lines.append(f"    inline Var<{types.cxx_decl(g['type'], '')}> {g['name']}{{{address}}};")
    lines.append("}")
    return "\n".join(lines) + "\n"


def cmd_export(args):
    irx = Irx(args.file)
    ecoff, types, functions = parse_debug_info(irx)
    stem = Path(args.file).stem.lower()
    CONFIG_IOP.mkdir(parents=True, exist_ok=True)
    funcs_path = CONFIG_IOP / f"{stem}_functions.csv"
    with open(funcs_path, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(["offset", "name", "size", "frame", "line_first", "line_last", "signature"])
        for fn in sorted(functions, key=lambda f: f["address"]):
            lines = [l for _a, l in fn["lines"]]
            w.writerow([f"0x{fn['address']:05X}", fn["name"], fn["size"], fn["frame"], min(lines, default=0), max(lines, default=0), describe_function(types, fn)])
    print(f"{funcs_path}: {len(functions)} function(s)")
    if args.base is not None:
        header_path = CONFIG_IOP / f"{stem}_functions.h"
        header_path.write_text(functions_header(irx, int(args.base, 0), functions), encoding="utf-8")
        print(f"{header_path}: offsets of {len(functions)} function(s), the import stubs, the image hash")
        globals_header = CONFIG_IOP / f"{stem}_globals.h"
        globals_header.write_text(globals_header_text(irx, types, int(args.base, 0)), encoding="utf-8")
        print(f"{globals_header}: {len(types.globals)} variable(s) at their addresses")
    globals_path = CONFIG_IOP / f"{stem}_globals.csv"
    with open(globals_path, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(["offset", "name", "size", "section", "type"])
        for g in sorted(types.globals, key=lambda g: g["address"]):
            w.writerow([f"0x{g['address']:05X}", g["name"], types.size_of(g["type"]), g["section"], types.cxx_decl(g["type"], "")])
    print(f"{globals_path}: {len(types.globals)} variable(s)")
    types_path = CONFIG_IOP / f"{stem}_types.h"
    roots = [fn["return"] for fn in functions if fn["return"] is not None]
    roots += [v["type"] for fn in functions for v in fn["params"] + fn["locals"]] + [g["type"] for g in types.globals]
    reached = types.reachable(roots)
    for n in sorted(types.primary):
        if n in reached:
            for problem in types.layout_mismatches(n):
                print("layout differs from natural alignment:", problem)
    types_path.write_text(types_header(types, Path(args.file).name, reached), encoding="utf-8")
    print(f"{types_path}: {sum(1 for n in types.primary if n in reached)} struct, union and enum type(s) used by the functions")


def types_header(types, module_name, reached):
    """A C++ header with the module's struct, union and enum types (declared under their tag or first typedef name), the other typedef names,
    and static_asserts for every size and every byte aligned member offset."""
    lines = [f"// Generated by tools/irx_inspect.py export from the debug info of {module_name}. Do not edit.",
             "// The layouts of the types the module's source declares; pointers are 32 bit IOP addresses (IopPtr<T>).",
             "#pragma once", "", "#include <cstddef>", "#include <cstdint>", "", '#include "iop_ptr.h"', "",
             "namespace iop::" + Path(module_name).stem.lower(), "{"]
    structs = {n: types.resolve(n)[1] for n in types.primary if n in reached}
    for n, name in sorted(types.primary.items(), key=lambda kv: kv[1]):
        if n in structs and structs[n]["k"] in ("struct", "union"):
            lines.append(f"{structs[n]['k']} {name};")
    lines.append("")
    # typedef names that are not the primary name of a struct, union or enum
    done = set()
    for number, name in sorted(types.names.items()):
        n, t = types.resolve(number)
        if name in done or name in Types.BUILTIN_CXX or name in Types.LIBC_TYPEDEFS or (number not in reached and n not in reached):
            continue
        done.add(name)
        if t["k"] in ("struct", "union", "enum"):
            if types.primary.get(n) != name:
                lines.append(f"using {name} = {types.primary[n]};")
        elif t["k"] in ("int", "float"):
            target = types._cxx(n)[0] if types.names.get(n) not in Types.BUILTIN_CXX else Types.BUILTIN_CXX[types.names[n]]
            if target != name:
                lines.append(f"using {name} = {target};")
        elif t["k"] in ("ptr", "func"):
            lines.append(f"using {name} = {types.pointer_text(number)};")
    lines.append("")
    # enums first, then structs and unions ordered so that a type is defined after those it holds by value
    for n, name in sorted(types.primary.items(), key=lambda kv: kv[1]):
        if n not in structs:
            continue
        t = structs[n]
        if t["k"] == "enum":
            lines.append(f"enum {name} : int32_t")
            lines.append("{")
            for vname, value in t["values"]:
                lines.append(f"    {vname} = {value},")
            lines.append("};")
            lines.append("")
    order, visiting = [], set()

    def visit(n):
        if n in order or n in visiting:
            return
        visiting.add(n)
        for _fname, ftype, _bp, _bs in structs[n]["fields"]:
            m, mt = types.resolve(ftype)
            while mt["k"] == "array":
                m, mt = types.resolve(mt["of"])
            if mt["k"] in ("struct", "union") and m in structs:
                visit(m)
        visiting.discard(n)
        order.append(n)

    for n, name in sorted(types.primary.items(), key=lambda kv: kv[1]):
        if n in structs and structs[n]["k"] in ("struct", "union"):
            visit(n)
    for n in order:
        t, name = structs[n], types.primary[n]
        lines.append(f"{t['k']} {name}")
        lines.append("{")
        for fname, ftype, bitpos, bitsize in t["fields"]:
            _m, ft = types.resolve(ftype)
            if ft["k"] == "array" and ft["count"] <= 0:
                lines.append(f"    // {fname}: array declared without a length, +0x{bitpos // 8:X}")
                continue
            lines.append(f"    {types.cxx_decl(ftype, fname)};  // +0x{bitpos // 8:X}")
        lines.append("};")
        lines.append(f"static_assert(sizeof({name}) == {t['size']}, \"{name} size\");")
        for fname, ftype, bitpos, bitsize in t["fields"]:
            _m, ft = types.resolve(ftype)
            if bitpos % 8 == 0 and not (ft["k"] == "array" and ft["count"] <= 0):
                lines.append(f"static_assert(offsetof({name}, {fname}) == 0x{bitpos // 8:X}, \"{name}::{fname}\");")
        lines.append("")
    lines.append("}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("summary")
    p.add_argument("files", nargs="*")
    p.set_defaults(func=cmd_summary)
    p = sub.add_parser("flat")
    p.add_argument("file")
    p.add_argument("--base", required=True)
    p.add_argument("-o", "--output")
    p.set_defaults(func=cmd_flat)
    p = sub.add_parser("stabs")
    p.add_argument("file")
    p.add_argument("-o", "--output")
    p.add_argument("--show", default="0")
    p.set_defaults(func=cmd_stabs)
    p = sub.add_parser("ghidra")
    p.add_argument("file")
    p.add_argument("--base", required=True)
    p.set_defaults(func=cmd_ghidra)
    p = sub.add_parser("export")
    p.add_argument("file")
    p.add_argument("--base", help="the address the emulator loads the module at; also writes the functions header (offsets, import stubs, image hash)")
    p.set_defaults(func=cmd_export)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
