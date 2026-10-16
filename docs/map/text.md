# text

Number and string formatting for the UI: money, thousands separators, padded numbers, digit splitting.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

4 functions (named 4). Blocks: 0 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Name | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001831E0` | `mm_Text_FormatMoney` | 144 | no | '$' + mm_Text_FormatThousands(value); right-aligns to width a2 with spaces unless a3 != 0 |
| `0x00183270` | `mm_Text_FormatThousands` | 296 | no | decimal string with ',' every three digits; negative values via abs (0x11E810) |
| `0x001833A0` | `mm_Text_SplitDigits` | 168 | no | number to per-digit values (0-9, 10 for a leading blank) for digit sprites, width a2, powers of ten at 0x1B9AB0 |
| `0x00183450` | `mm_Text_FormatPadded` | 176 | no | right-aligned decimal with leading spaces, width a2 (powers of ten at 0x1B9AB0) |
