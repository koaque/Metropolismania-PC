"""A differential test of the translated IOP code against the interpreter, on random programs (plan P5.3).

usage: python tools/iop_aot_fuzz.py [--programs N] [--trials T] [--words W] [--seed S]

For each program: a random flat image of R3000A code (every instruction form the interpreter knows, the traps, branches and jumps with
their delay slots, loads and stores that hit RAM, misaligned addresses and a hardware register), translated by tools/iop_aot.py --raw and
built into tools/iop_aot_fuzz/harness.cpp, which runs it from random registers and memory in runs of random length, steps the interpreter
the same number of instructions after each run and compares the whole state. Needs build/ps2recomp/ps2xIOP/ps2_iop.lib (pipeline.py build).
The programs contain no game data.
"""
import argparse
import random
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import pipeline  # noqa: E402

BASE = 0x100000
WORK = ROOT / "work" / "iop_aot_fuzz"
IOP = ROOT / "work" / "PS2Recomp" / "ps2xIOP"
LIB = ROOT / "build" / "ps2recomp" / "ps2xIOP" / "ps2_iop.lib"


def r_type(funct, rs=0, rt=0, rd=0, sa=0):
    return (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | funct


def i_type(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


def j_type(op, target):
    return (op << 26) | ((target >> 2) & 0x3FFFFFF)


class Gen:
    def __init__(self, rng):
        self.rng = rng

    def reg(self, allow_base=False):
        r = self.rng.randrange(32)
        while not allow_base and 16 <= r < 20:
            r = self.rng.randrange(32)
        return r

    def src(self):
        return self.rng.randrange(32)

    def plain(self):
        rng = self.rng
        kind = rng.choices(["alu", "imm", "load", "store", "cop0", "mult", "rare"], weights=[28, 22, 14, 12, 5, 8, 1.5])[0]
        if kind == "alu":
            funct = rng.choice([0x00, 0x02, 0x03, 0x04, 0x06, 0x07, 0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2A, 0x2B])
            if funct in (0x00, 0x02, 0x03):
                return r_type(funct, 0, self.src(), self.reg(), rng.randrange(32))
            if funct in (0x11, 0x13):
                return r_type(funct, self.src())
            if funct in (0x10, 0x12):
                return r_type(funct, 0, 0, self.reg())
            return r_type(funct, self.src(), self.src(), self.reg())
        if kind == "imm":
            op = rng.choice([8, 9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF])
            imm = rng.choice([0, 1, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(0x10000), rng.randrange(0x10000)])
            return i_type(op, self.src(), self.reg(), imm)
        if kind in ("load", "store"):
            op = rng.choice([0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26] if kind == "load" else [0x28, 0x29, 0x2A, 0x2B, 0x2E])
            base = rng.randrange(16, 20) if rng.random() < 0.95 else rng.randrange(32)
            offset = rng.randrange(-0x40, 0x40)
            if rng.random() < 0.92:
                offset &= ~3  # most accesses are aligned: a misaligned halfword or word traps and ends the trial
            return i_type(op, base, self.reg() if kind == "load" else self.src(), offset)
        if kind == "cop0":
            which = rng.choice(["mfc0", "mfc0", "mtc0", "rfe"])
            if which == "rfe":
                return 0x42000010
            return (0x10 << 26) | ((0 if which == "mfc0" else 4) << 21) | (self.reg() << 16) | (rng.choice([8, 12, 13, 14, rng.randrange(32)]) << 11)
        if kind == "mult":
            return r_type(rng.choice([0x18, 0x19, 0x1A, 0x1B]), self.src(), self.src())
        # rare: traps and reserved forms
        which = rng.choice(["syscall", "break", "reserved_op", "reserved_special", "reserved_regimm", "cop2"])
        if which == "syscall":
            return r_type(0x0C)
        if which == "break":
            return r_type(0x0D)
        if which == "reserved_op":
            return i_type(rng.choice([0x11, 0x13, 0x14, 0x18, 0x1F, 0x27, 0x2C, 0x30, 0x3B, 0x3F]), self.src(), self.src(), rng.randrange(0x10000))
        if which == "reserved_special":
            return r_type(rng.choice([0x01, 0x05, 0x0A, 0x0B, 0x0E, 0x14, 0x1C, 0x28, 0x2C, 0x3F]), self.src(), self.src(), self.reg())
        if which == "reserved_regimm":
            return i_type(1, self.src(), rng.choice([2, 3, 0x12, 0x1F]), 0)
        return (0x12 << 26) | rng.randrange(1 << 26)


def build(rng, count):
    """(words, seeds): count items, each a plain instruction or a branch group, then a jump out of the image."""
    gen = Gen(rng)
    items = []  # lists of words or callables(address, starts) -> words
    for _ in range(count):
        roll = rng.random()
        if roll < 0.72:
            items.append(("plain", gen.plain()))
        elif roll < 0.88:
            items.append(("branch", rng.choice([4, 5, 6, 7, 0x100, 0x101, 0x110, 0x111]), gen.src(), gen.src(), gen.plain()))
        elif roll < 0.94:
            items.append(("jump", rng.choice([2, 3]), gen.plain()))
        else:
            items.append(("jr", rng.choice(["jr", "jalr"]), gen.reg(), gen.reg(), gen.plain()))
    sizes = {"plain": 1, "branch": 2, "jump": 2, "jr": 4}
    starts = []
    address = BASE
    for item in items:
        starts.append(address)
        address += 4 * sizes[item[0]]
    end = address + 8 * 4  # outside the code
    words = []
    seeds = {BASE}
    for index, item in enumerate(items):
        here = starts[index]

        def forward():
            if index + 1 >= len(items):
                return end
            return starts[rng.randrange(index + 1, min(len(items), index + 12))]

        if item[0] == "plain":
            words.append(item[1])
        elif item[0] == "branch":
            code, rs, rt, slot = item[1], item[2], item[3], item[4]
            target = forward()
            offset = (target - (here + 4)) // 4
            if code in (0x100, 0x101, 0x110, 0x111):
                words.append(i_type(1, rs, code & 0xFF, offset))
            elif code in (6, 7):
                words.append(i_type(code, rs, 0, offset))
            else:
                words.append(i_type(code, rs, rt if rng.random() < 0.6 else rs, offset))
            words.append(slot)
            seeds.add(target)
        elif item[0] == "jump":
            target = forward()
            words.append(j_type(item[1], target))
            words.append(item[2])
            seeds.add(target)
        else:
            _kind, form, link, rtmp, slot = item
            target = forward()
            rtmp = 8 + (rtmp % 8)
            words.append(i_type(0xF, 0, rtmp, target >> 16))
            words.append(i_type(0xD, rtmp, rtmp, target & 0xFFFF))
            words.append(r_type(0x08, rtmp) if form == "jr" else r_type(0x09, rtmp, 0, link))
            words.append(slot)
            seeds.add(target)
    words.append(j_type(2, end))
    words.append(0)
    return words, seeds


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    if result.returncode != 0:
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--programs", type=int, default=20)
    parser.add_argument("--trials", type=int, default=300)
    parser.add_argument("--words", type=int, default=400)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--program", type=int, help="run only this program number of the seed (to look at a failure again)")
    args = parser.parse_args()
    if not LIB.exists():
        sys.exit(f"{LIB} not found: run pipeline.py build first")
    WORK.mkdir(parents=True, exist_ok=True)
    env = pipeline.msvc_env()
    cl = shutil.which("cl", path=env["PATH"]) or "cl"
    link = shutil.which("link", path=env["PATH"]) or "link"
    flags = ["/nologo", "/std:c++20", "/EHsc", "/O2", "/MD", "/W3", f"/I{IOP / 'include'}", f"/I{IOP / 'src'}"]
    harness_obj = WORK / "harness.obj"
    if run([cl, *flags, "/c", str(ROOT / "tools" / "iop_aot_fuzz" / "harness.cpp"), f"/Fo{harness_obj}"], env=env, cwd=WORK).returncode != 0:
        sys.exit("the harness did not compile")
    failures = 0
    for number in ([args.program] if args.program is not None else range(args.programs)):
        seed = args.seed * 1000 + number
        rng = random.Random(seed)
        words, seeds = build(rng, args.words)
        image = b"".join(w.to_bytes(4, "little") for w in words)
        (WORK / "prog.bin").write_bytes(image)
        (WORK / "seeds.json").write_text(str(sorted(seeds)).replace("'", ""))
        translate = run([sys.executable, "-I", str(ROOT / "tools" / "iop_aot.py"), "--raw", str(WORK / "prog.bin"), "--base", f"{BASE:x}", "--name", "FUZZ",
                         "--seeds", str(WORK / "seeds.json"), "--out", str(WORK)])
        if translate.returncode != 0:
            failures += 1
            continue
        if run([cl, *flags, "/c", str(WORK / "FUZZ.cpp"), f"/Fo{WORK / 'FUZZ.obj'}"], env=env, cwd=WORK).returncode != 0:
            failures += 1
            continue
        if run([link, "/nologo", str(harness_obj), str(WORK / "FUZZ.obj"), str(LIB), f"/OUT:{WORK / 'harness.exe'}"], env=env, cwd=WORK).returncode != 0:
            failures += 1
            continue
        result = run([str(WORK / "harness.exe"), str(WORK / "prog.bin"), f"{BASE:x}", str(args.trials), str(seed)], cwd=WORK)
        line = result.stdout.strip().splitlines()[-1] if result.stdout.strip() else "(no output)"
        print(f"program {number} (seed {seed}): {line}")
        if result.returncode != 0:
            failures += 1
            keep = WORK / f"fail_{seed}.bin"
            keep.write_bytes(image)
    print("FAILED: %d of %d program(s)" % (failures, args.programs) if failures else "all %d program(s) agree" % args.programs)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
