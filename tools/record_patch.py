"""Make a difftest record from another: copy work/difftest/<Name>/NNN.mmdt with some RDRAM words or argument
registers changed, to reach a path the captured calls never take (a different object, a counter about to wrap, a
listener moved next to a vehicle). The record keeps the rest of the captured state, so the original and the
ported function still start from a real game state; check with `pipeline.py difftest --changes <Name>` that the
made record does reach the path (a record that changes nothing proves nothing).

usage: record_patch.py IN.mmdt OUT.mmdt [ADDR:u8|u16|u32|i32|f32=VALUE ...] [reg:NAME=VALUE ...] [--show=ADDR:LEN]
  ADDR is a guest address (hex); values are decimal or 0x-hex (f32 takes a float).
  reg:a1=0x7493E0 sets a GPR (sign-extended from 32 bits); OUT may be - to only --show.
  A record can be copied into another function's directory to call that function from the same state."""
import struct, sys, zlib

GPR = {n: i for i, n in enumerate("zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra".split())}


def load(path):
    data = open(path, "rb").read()
    assert data[:4] == b"MMDT", "not a record"
    version, ctx_size, packed_size, spr_size = struct.unpack_from("<4I", data, 4)
    pos = 20
    ctx = bytearray(data[pos:pos + ctx_size]); pos += ctx_size
    packed = data[pos:pos + packed_size]; pos += packed_size
    spr = data[pos:pos + spr_size]
    ram = bytearray(zlib.decompress(packed, -15))
    return version, ctx, ram, spr


def save(path, version, ctx, ram, spr):
    c = zlib.compressobj(1, zlib.DEFLATED, -15)
    packed = c.compress(bytes(ram)) + c.flush()
    with open(path, "wb") as f:
        f.write(b"MMDT")
        f.write(struct.pack("<4I", version, len(ctx), len(packed), len(spr)))
        f.write(ctx)
        f.write(packed)
        f.write(spr)


def main():
    args = sys.argv[1:]
    src, dst = args[0], args[1]
    version, ctx, ram, spr = load(src)
    for a in args[2:]:
        if a.startswith("--show"):
            addr, n = a.split("=", 1)[1].split(":")
            addr, n = int(addr, 16) & 0x1FFFFFF, int(n, 0)
            print(f"{addr:08X}: {ram[addr:addr + n].hex(' ')}")
            continue
        key, value = a.split("=", 1)
        if key.startswith("reg:"):
            r = GPR[key[4:]]
            v = int(value, 0) & 0xFFFFFFFF
            v = v - (1 << 32) if v & 0x80000000 else v
            struct.pack_into("<q", ctx, 16 * r, v)
            continue
        addr, kind = key.split(":")
        addr = int(addr, 16) & 0x1FFFFFF
        fmt = {"u8": "<B", "u16": "<H", "u32": "<I", "i32": "<i", "f32": "<f"}[kind]
        val = float(value) if kind == "f32" else int(value, 0)
        struct.pack_into(fmt, ram, addr, val)
    if dst != "-":
        save(dst, version, ctx, ram, spr)


if __name__ == "__main__":
    main()
