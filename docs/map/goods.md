# goods

Goods (shop items and gifts): 0x88-byte records. Group offsets at `0x001BDA9C` (one u16 per group), records at
`0x001BDAA0`, names at `0x001BDAA4`. `mm_Goods_GetPrice` and `mm_Goods_GetRecordOffset` (by building type and
time of year) are used by the talk script's shopping choices.
- A record (`GoodsRecord`, GOODS.BIN section B) holds its name's offset into the names, the price as digits and a power
  of ten, its kind, a value by kind, a count and the description. Kinds (`mm_WordsFn_BuyGoods`): 4, 5, 10 and 11
  laptop modules; 6 to 8 bags (the value is the bag size); 0x10 presents; 0x11 and 0x13 food to carry, 0x12 and 0x14
  food eaten at once (`mm_Stamina_Eat` adds 1000 per point of the value); 0x15 clothes (the value
  is the player's outfit, `Bag.outfit`; the one taken off goes back into the bag); 0x16 music (the value is the track); 0x17 and
  0x1A to 0x1C things to place; 0x19 houses (the value is the family that applies).
- A bag slot (`BagSlot`) keeps the goods group (the building type that sells it) and index, the stage and month it was
  bought in, the seller's family and the record's count.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

3 functions (verified 3). Blocks: 1 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0013E380` | `int mm_Goods_GetPrice(int item)` | 72 | no | (item) price of goods item `item`: byte +2 x 10^(byte +3) |
| `0x0013E3D0` | `int mm_Goods_GetRecordOffset(int group, int family, int season, int32_t *offset)` | 132 | no | (group, family, season, offset) advances *offset by four records times the group's perVariant for the shop's family (modulo its variants), and by four per season when perVariant is two or more (GoodsGroup); returns the record's address |
| `0x0013E460` | `void mm_Goods_Load(void)` | 192 | yes | loads DATA\BIN\GOODS.BIN and resolves its section offsets (0x1BDA9C..0x1BDAB4) |
