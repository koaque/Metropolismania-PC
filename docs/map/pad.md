# pad

Pad driver on top of libpad. `mm_Pad_Update` turns the button bytes into `(data[2] << 8 | data[3]) ^ 0xFFFF`, so
Cross is `0x0040` and Start is `0x0800`; the analog sticks add bits `0x100000` .. `0x800000`. Focus
(`mm_Pad_TakeFocus`/`ReleaseFocus`) decides which window reads the pad.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

10 functions (named 10). Blocks: 3 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Name | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001578A0` | `mm_Pad_GetData` | 52 | no | port a0's 32-byte pad buffer (0x1F3AC0) when its state is ready (100), else 0 |
| `0x001578E0` | `mm_Pad_Poll` | 924 | yes | scePadRead and the pad state machine: analog mode, actuator alignment, pressure mode |
| `0x00157CB0` | `mm_PadInit` | 200 | yes |  |
| `0x001851F0` | `mm_Pad_ClearBit40` | 40 | no | sets 0x1BDCC0 = 1 and clears button bit 0x40 in the pad words 0x1BDCA0 and 0x1BDCA4 |
| `0x00185220` | `mm_Pad_ReleaseFocus` | 40 | no | drops input focus if owner a0 (or -1 = anyone) holds it (0x1BDCB0/0x1BDCB4) |
| `0x00185250` | `mm_Pad_TakeFocus` | 28 | no | takes input focus for owner a0 with button mask a1 when nobody holds it |
| `0x00185270` | `mm_Pad_GetRepeat` | 92 | no | held buttons with auto-repeat: first repeat after 30 frames, then every 5 |
| `0x001852D0` | `mm_Pad_Pressed` | 60 | no | pressed buttons (0x1BDCA4), minus the focus mask when another owner holds input focus |
| `0x00185310` | `mm_Pad_Held` | 60 | no | held buttons (0x1BDCA0), minus the focus mask when another owner holds input focus |
| `0x00185350` | `mm_Pad_Update` | 400 | yes | reads the pad (0x1578E0/0x1578A0), stores buttons active-high in 0x1BDCA0; in analog mode (id 0x7_) the left stick past 0x40/0xC0 adds direction bits 0x100000-0x800000; then derives pressed (0x1BDCA4) |
