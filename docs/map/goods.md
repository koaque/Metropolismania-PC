# goods

Goods (shop items and gifts): 0x88-byte records. Group offsets at `0x001BDA9C` (one u16 per group), records at
`0x001BDAA0`, names at `0x001BDAA4`. `mm_Goods_GetPrice` and `mm_Goods_GetRecordOffset` (by building type and
time of year) are used by the talk script's shopping choices.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

3 functions (verified 3). Blocks: 1 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0013E380` | `int mm_Goods_GetPrice(int item)` | 72 | no | price of goods item a0: byte +2 x 10^(byte +3) |
| `0x0013E3D0` | `int mm_Goods_GetRecordOffset(int group, int a1, int a2, int32_t *offset)` | 132 | no | advances *a3 to the record of item a1/a2 in group a0 |
| `0x0013E460` | `void mm_Goods_Load(void)` | 192 | yes | loads DATA\BIN\GOODS.BIN and resolves its section offsets (0x1BDA9C..0x1BDAB4) |
