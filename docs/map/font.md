# font

Font loading and glyph lookup for the text boxes.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

3 functions (typed 2, verified 1). Blocks: 2 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00184BF0` | `int mm_Font_GetGlyph(uint32_t ch)` | 28 | no | font data (gp global) + (char & 0xFF) * 0x90 |
| `0x00184C10` | `int mm_Font_Free(void)` | 44 | yes | frees the loaded font |
| `0x00184C40` | `int mm_Font_Load(void)` | 132 | yes | loads DATA\FONT1222.GF (sprintf "%d" of 11222 with the first digit skipped) with mm_File_LoadWhole |
