# text

Number and string formatting for the UI: money, thousands separators, padded numbers, digit splitting.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

5 functions (verified 5). Blocks: 0 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00183020` | `void mm_Text_FormatTimeRange(char *out, int fromMinute, int toMinute)` | 444 | no | "H:MM" of minute a1, glyph 0xC2 (0x1B9AD8) as separator, then "H:MM" of minute a2, into a0; used by mm_WordsFn_TextOpenHours |
| `0x001831E0` | `void mm_Text_FormatMoney(char *out, int value, int width, int leftAlign)` | 144 | no | '$' + mm_Text_FormatThousands(value); right-aligns to width a2 with spaces unless a3 != 0 |
| `0x00183270` | `int mm_Text_FormatThousands(char *out, int value)` | 296 | no | decimal string with ',' every three digits; negative values via abs (0x11E810) |
| `0x001833A0` | `void mm_Text_SplitDigits(uint8_t *out, int value, int count)` | 168 | no | number to per-digit values (0-9, 10 for a leading blank) for digit sprites, width a2, powers of ten at 0x1B9AB0 |
| `0x00183450` | `void mm_Text_FormatPadded(char *out, int value, int width)` | 176 | no | right-aligned decimal with leading spaces, width a2 (powers of ten at 0x1B9AB0) |
