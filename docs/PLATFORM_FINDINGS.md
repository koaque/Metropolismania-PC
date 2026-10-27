# Platform findings

What plan P7 (the SDK platform layer in `src/port/platform/`) found out about the runtime and the recompiler while proving each
function against the recompiled one. Each item is behavior of the recompiled build that differs from what a PS2 does. The native
platform functions first reproduced all of it, so that they match the recompiled code exactly (the difftest is the proof). Item 1 is
fixed since then (plan P7.10); items 2 (in the map code) and 3 are still reproduced, and fixing them changes what the game computes,
so each is a separate decision (see "What is left").

## 1. Sign branches test only the low 32 bits (fixed in the natives)

The recompiler translates `bltz`, `bgez`, `blez`, `bgtz` and their likely forms as a test of the low 32 bits of the register
(`GPR_S32(ctx, r) < 0` in the generated code), where the R5900 tests all 64 bits. For a 32 bit value held in a register the two agree.
They differ for a true 64 bit value, and the soft float library keeps 64 bit fractions:

- `dpadd` and `dpsub` (`_fpadd_parts`): `bltzl` on the 64 bit difference of the two fractions. The recompiled code turns round a
  positive difference whose low word looks negative, and does not turn round a negative one whose low word looks positive (the result
  keeps the wrong sign and a huge fraction). About 11 percent of random sums of two doubles with opposite signs come out wrong.
- `_d_utod` (unsigned int to double): `blez` on a zero extended register. A value of 2^31 or more takes the path meant for negative
  numbers, so an odd value loses its lowest bit.
- `pow` inherits both (it is made of `dpadd`, `dpsub`, `dpmul`, `dpdiv`), and also takes |x| from the `fabs` handler of item 2.

The game's only call of `pow` is `Goods_GetPrice` (a goods record's price byte times 10 to the power of its exponent byte).
`pow(10, 0)`, `pow(10, 1)` and `pow(10, 2)` are right in every build because `pow` returns 1, x and x*x for those exponents before it
reaches the general path. For the exponents 3 and more the recompiled build gives `pow(10, 3)` = `0x41D804AA5D87437D` (1,611,835,766.1),
`pow(10, 4)` = 291,689.1 and `pow(10, 5)` = 49,278,878.6. DATA/BIN/GOODS.BIN has 272 records of 0x88 bytes, 11 of them with an
exponent of 3 or more. What `Goods_GetPrice` returns for them (the callers test it against the player's money and add it to totals,
`words.cpp` and `event.cpp`):

| Record | Price byte, exponent | Recompiled build | A PS2 (the library's own code, 64 bit branches) |
|---|---|---|---|
| 4 | 30, 3 | 1,110,432,724 | 29,970 |
| 5 | 30, 4 | 8,750,670 | 299,970 |
| 6 | 80, 4 | 23,335,120 | 799,920 |
| 7 | 10, 5 | 492,788,780 | 999,990 |
| 59 | 10, 4 | 2,916,890 | 99,990 |
| 108, 112 | 50, 3 | -1,012,590,324 | 49,950 |
| 163, 172 | 10, 3 | -1,061,511,524 | 9,990 |
| 168, 173 | 15, 3 | -1,592,267,286 | 14,985 |

The PS2 column is 10^n - 1 times the price byte, not 10^n: the library does not round (its `__pack_d` just shifts the fraction right,
there is no rounding step), so `pow(10, 3)` is `0x408F3FFFFFFFFFFF` (999.9999999999999) and `dptoli` cuts it to 999. That is what the
game's own code computes on a PS2 according to the interpreter below; it has not been compared with a PCSX2 run.

The natives now do it that way: `addParts` and `sdk_d_utod` use the full 64 bit tests, and `pow` takes the absolute value of its
base. `mm::g_recompiledQuirks` (`sdk.h`) puts the recompiled behavior back (both places, and the stale |x| of item 2). The difftest
and the capture runs set it, so the replay of every ported function still compares with the recompiled code exactly; a running
game leaves it off.

How it was checked: `tools/ee_interp.py` is a small R5900 interpreter that runs the library's own functions from the ELF. With
`low32=True` it makes the same two mistakes as the recompiled build and reproduces its results bit for bit (`pow(10, 3)` above);
with `low32=False` it is what a PS2 does. `tools/soft_float_oracle.py` writes 103,000 cases (`dpadd`, `dpsub`, `dpmul`, `dpdiv`,
`dpcmp`, `litodp`, `_d_utod`, `dptoli`, `pow`) with the results of both modes, and the runner compares the natives with them in both
modes: `MMPC_DIFFTEST_ORACLE_FILE=<work/oracle_cases.txt> MMPC_DIFFTEST_NUMERIC_REFERENCE=1 python tools/pipeline.py difftest --only
sdk_pow` (the result lines are in the log it names). All 103,000 agree bit for bit. The same reference check also compares sums
and differences with the host's IEEE doubles (each result is the exact sum cut off toward zero or its neighbour), the integer and
unsigned conversions (exact) and `pow` (within 1e-12 relative).

## 2. The runtime's `fabs` handler (0x00119990) treats a double as a float

The SDK function `fabs` takes a soft double in `a0` and returns it in `v0`. The runtime binds the address to a handler that reads
`f12` as a float and writes `f0`, and leaves `v0` alone. So on a double it returns what `v0` held:

- `pow` takes |x| from this call, so in the recompiled build its `ax` is a leftover register (0x3FE00000, 0x7FF00000 or 0x14, by the
  path taken to the call) instead of |x|. The native `pow` uses the real absolute value (fixed with item 1, because the prices
  need it); with `g_recompiledQuirks` it reproduces the three values.
- the map code (`sdk::fabs(sdk::fptodp(tile.height))`, the tile pull and the sway amount) gets its argument back, so a negative
  height stays negative; `sdk::fabs` in `src/port/sdk.h` is still written as that identity. A correct `fabs` would change the map's
  tile sizes and sway. Not changed.

## 3. `sinf` and `cosf` end in host math

`__kernel_sinf`, `__kernel_cosf` and `__ieee754_rem_pio2f` are handlers of the runtime library, not recompiled code: they call the
host's `sinf` and `cosf` and reduce the angle with a rounded multiple of pi/2. A PS2 runs the library's own routines on its float
unit, so results differ in the last bits. `libm.cpp` keeps calling the three handlers. Not changed.

## What is left

1. `fabs` in the map code: remove `fabs@0x00119990` from the `stubs` list in `config/metropolismania.template.toml` so that the
   recompiled 52 byte function runs (for the replay), and make `sdk::fabs` clear the sign bit.
2. `sinf`, `cosf`: replace the three handlers by the library's routines (written natively, as the rest of the platform layer; the
   interpreter would need the float unit instructions to serve as the reference).

The first changes the map's tile sizes and sway; the second changes `sinf` and `cosf` in the last bits.
