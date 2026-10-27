"""Cases for the soft-float oracle check (plan P7.10). Runs the game's own library code in the interpreter (tools/ee_interp.py), in
both modes, and writes work/oracle_cases.txt: one line per case, "name arg0 arg1 result_with_the_recompiler's_quirks result_as_a_PS2"
(hex; the result of dptoli and dpcmp is the low 32 bits). The runner checks the native functions against it, bit for bit, in both
modes: MMPC_DIFFTEST_ORACLE_FILE=<path> MMPC_DIFFTEST_NUMERIC_REFERENCE=1 python tools/pipeline.py difftest --only sdk_pow
(the results are in the log it names). Needs original/SLUS_203.77. Usage: python -I tools/soft_float_oracle.py [scale]
"""
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from ee_interp import Cpu, FUNCTIONS, bits, soft, sx32, M32, M64

scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
rng = random.Random(20261027)


def normal_bits(exp_lo=-60, exp_hi=60):
    e = rng.randint(exp_lo, exp_hi) + 1023
    return (rng.getrandbits(1) << 63) | (e << 52) | rng.getrandbits(52)


def operand_pair():
    k = rng.randrange(8)
    if k == 0:
        return rng.getrandbits(64), rng.getrandbits(64)  # anything, specials included
    a = normal_bits()
    if k == 1:  # near equal, opposite signs (cancellation)
        b = a ^ (1 << 63)
        b = (b & ~((1 << rng.randint(1, 50)) - 1)) | rng.getrandbits(rng.randint(1, 50))
        return a, b & M64
    if k == 2:  # close exponents
        e = ((a >> 52) & 0x7FF) + rng.randint(-3, 3)
        return a, (rng.getrandbits(1) << 63) | (max(1, min(2046, e)) << 52) | rng.getrandbits(52)
    if k == 3:  # small integers
        return bits(float(rng.randint(-1000, 1000))), bits(float(rng.randint(-1000, 1000)))
    if k == 4:  # one far smaller than the other
        return a, normal_bits(-120, -60)
    if k == 5:
        return normal_bits(-20, 20), normal_bits(-20, 20)
    return a, normal_bits()


def int_value():
    k = rng.randrange(5)
    if k == 0:
        return rng.randint(0, 15)
    if k == 1:
        return (1 << rng.randrange(32)) + rng.randint(-2, 2)
    if k == 2:
        return 0x7FFFFFFF + rng.randint(-2, 2)
    return rng.getrandbits(32)


def pow_pair():
    k = rng.randrange(6)
    if k == 0:
        return bits(10.0), bits(float(rng.randint(0, 12)))
    if k == 1:
        return bits(float(rng.randint(2, 30))), bits(float(rng.randint(0, 12)))
    if k == 2:
        return bits(rng.uniform(0.1, 100.0)), bits(rng.uniform(-5.0, 5.0))
    if k == 3:
        return bits(rng.uniform(0.5, 4.0) * 2.0 ** rng.randint(-20, 20)), bits(float(rng.randint(-8, 8)))
    if k == 4:
        return bits(float(rng.randint(1, 1000000)) / (1 << rng.randint(0, 12))), bits(rng.uniform(0.0, 3.0))
    return normal_bits(-30, 30) & ~(1 << 63), bits(rng.uniform(-3.0, 3.0))


plan = [
    ("dpadd", int(30000 * scale), operand_pair),
    ("dpsub", int(30000 * scale), operand_pair),
    ("dpmul", int(10000 * scale), operand_pair),
    ("dpdiv", int(10000 * scale), operand_pair),
    ("dpcmp", int(5000 * scale), operand_pair),
    ("litodp", int(5000 * scale), lambda: (int_value() | (rng.getrandbits(1) << 31), 0)),
    ("d_utod", int(5000 * scale), lambda: (int_value(), 0)),
    ("dptoli", int(5000 * scale), lambda: (operand_pair()[0], 0)),
    ("pow", int(3000 * scale), pow_pair),
]

cpus = {True: Cpu(True), False: Cpu(False)}
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.makedirs(os.path.join(ROOT, "work"), exist_ok=True)
with open(os.path.join(ROOT, "work", "oracle_cases.txt"), "w") as out:
    for name, count, make in plan:
        for i in range(count):
            a, b = make()
            args = (a, b)
            if name in ("litodp", "d_utod", "dptoli"):
                args = (sx32(a) if name == "litodp" else a,)
            result = {}
            for low32 in (True, False):
                try:
                    result[low32] = cpus[low32].call(FUNCTIONS[name], args)
                except Exception as error:  # a runaway or an unmodelled instruction: report and skip the case
                    print("skipped", name, hex(a), hex(b), error)
                    result = None
                    break
            if result is None:
                continue
            if name in ("dpcmp", "dptoli", "litodp", "d_utod"):
                pass
            out.write("%s %X %X %X %X\n" % (name, a, b, result[True], result[False]))
        print(name, count, flush=True)
