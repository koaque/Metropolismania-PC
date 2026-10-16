# claim

`DATA\BIN\CLAIM.BIN`: the wants ("claims") that buildings and families express. `mm_Claim_GetSectionB/C/D`
return the file's sections; a building type's claim index (`+0x147`) selects its entries in section B, and its
`+0x148` index the wanted types in section C. Entries with bit 15 set point into section D. `mm_Claim_GetLine`
returns a claim's text line.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

6 functions (typed 6). Blocks: 1 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00135860` | `int mm_Claim_PickVariant(uint32_t claim)` | 84 | no | entry variant by bit 0x8000 of a0 from the current speaker table (gp) |
| `0x001358C0` | `int mm_Claim_GetLine(int claim, int line)` | 124 | no | line a1 of claim a0 (8-byte items), variant by bit 0x8000 |
| `0x00135940` | `int mm_Claim_GetSectionC(int index)` | 32 | no | CLAIM.BIN entry a0 via the offset table at file +0xC |
| `0x00135960` | `int mm_Claim_GetSectionD(uint32_t index)` | 40 | no | CLAIM.BIN entry (a0 & 0x7FFF) via the offset table at file +0x10 |
| `0x00135990` | `int mm_Claim_GetSectionB(int index)` | 32 | no | CLAIM.BIN entry a0 via the offset table at file +8 |
| `0x001359B0` | `void mm_Claim_Load(void)` | 184 | yes | loads DATA\BIN\CLAIM.BIN and resolves its section offsets (+8, +0xC, +0x10, +0x14, ...) |
