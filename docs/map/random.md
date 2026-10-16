# random

The game's random number generator and timer mixing.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

4 functions (typed 1, verified 3). Blocks: 0 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00184D30` | `int mm_Random_TimerMix(void)` | 164 | no | mixes RCNT0 into a five-word running sum at 0x1B2520 and returns it |
| `0x00184DE0` | `int64_t mm_Random_Next(void)` | 80 | no | subtractive generator (Knuth/Numerical Recipes ran3, 55 entries at 0x1F55D0, modulus 1e9) |
| `0x00184E30` | `void mm_Random_Seed(int seed)` | 160 | no | seeds the 55-entry table from a0 and stirs it three times |
| `0x00184ED0` | `void mm_Random_Stir(void)` | 140 | no | one ran3 refill pass over the 55 entries |
