# Platform findings

What plan P7 (the SDK platform layer in `src/port/platform/`) found out about the runtime and the recompiler while proving each
function against the recompiled one. Each item is behavior of the current build that differs from what a PS2 does. The native
platform functions reproduce it, so that they match the recompiled code exactly (the difftest is the proof) and the game does what
it does today. Fixing any of it changes what the game computes, so it is a separate decision (see "What fixing would take").

## 1. Sign branches test only the low 32 bits

The recompiler translates `bltz`, `bgez`, `blez`, `bgtz` and their likely forms as a test of the low 32 bits of the register
(`GPR_S32(ctx, r) < 0` in the generated code), where the R5900 tests all 64 bits. For a 32 bit value held in a register the two agree.
They differ for a true 64 bit value, and the soft float library keeps 64 bit fractions:

- `dpadd` and `dpsub` (`_fpadd_parts`): `bltzl` on the 64 bit difference of the two fractions. When the difference is negative but its
  low word is not, the result keeps the wrong sign and a huge fraction. About 11 percent of random sums of two doubles with opposite
  signs come out wrong.
- `_d_utod` (unsigned int to double): `blez` on a zero extended register. A value of 2^31 or more takes the path meant for negative
  numbers, so an odd value loses its lowest bit.
- `pow` inherits both (it is made of `dpadd`, `dpsub`, `dpmul`, `dpdiv`).

Measured with the numeric fuzz: `pow(10, 3)` gives `0x41D804AA5D87437D` (about 1.48e9) in the recompiled code. `pow(10, 0)`, `pow(10, 1)`
and `pow(10, 2)` are right because `pow` returns 1, x and x*x for those exponents before it reaches the general path. The game's only
call of `pow` is `Goods_GetPrice` (a goods record's price byte times 10 to the power of its exponent byte); I could not tell from the
captured records whether any record has an exponent of 3 or more.

`src/port/platform/softfloat.cpp` marks the two places (`addParts`, `sdk_d_utod`).

## 2. The runtime's `fabs` handler (0x00119990) treats a double as a float

The SDK function `fabs` takes a soft double in `a0` and returns it in `v0`. The runtime binds the address to a handler that reads
`f12` as a float and writes `f0`, and leaves `v0` alone. So on a double it returns what `v0` held:

- the map code (`sdk::fabs(sdk::fptodp(tile.height))`, the tile pull and the sway amount) gets its argument back, so a negative
  height stays negative; `sdk::fabs` in `src/port/sdk.h` is written as that identity;
- `pow` takes |x| from this call, so its `ax` is a leftover register (0x3FE00000, 0x7FF00000 or 0x14, by the path taken to the call)
  instead of |x|; the low word of that register ends up in the result. `softmath.cpp` reproduces the three values.

## 3. `sinf` and `cosf` end in host math

`__kernel_sinf`, `__kernel_cosf` and `__ieee754_rem_pio2f` are handlers of the runtime library, not recompiled code: they call the
host's `sinf` and `cosf` and reduce the angle with a rounded multiple of pi/2. A PS2 runs the library's own routines on its float
unit, so results differ in the last bits. `libm.cpp` keeps calling the three handlers.

## What fixing would take

1. Sign branches: change the recompiler (a patch in `patches/`) to test the whole register, recompile everything, and rerun the full
   difftest. The generated code, which is the difftest's reference, would then be right, and `addParts` and `sdk_d_utod` lose their
   special cases. Risk: code elsewhere may rely on 32 bit results being kept without sign extension; the full difftest and the live
   runs are the check.
2. `fabs`: remove `fabs@0x00119990` from the `stubs` list in `config/metropolismania.template.toml` so that the recompiled 52 byte
   function runs, and make `sdk::fabs` clear the sign bit.
3. `sinf`, `cosf`: replace the three handlers by the library's routines (written natively, as the rest of the platform layer).

Items 1 and 2 change the results of `pow` (and the map's tile sizes and sway for 2); 3 changes `sinf` and `cosf` in the last bits.
