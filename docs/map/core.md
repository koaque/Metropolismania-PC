# core

Shared base classes and startup code: the base object constructor, a pair helper and the static initializer that
constructs the 256 member status entries (`family`).

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

4 functions (named 4). Blocks: 1 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Name | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0012AB40` | `mm_BaseObject_dtor` | 68 | yes | common base destructor: delete when the flag is > 0 |
| `0x0012AB90` | `mm_BaseObject_ctor` | 8 | no | common base constructor: returns this |
| `0x0013BAB0` | `mm_Pair_ctor` | 16 | no | two-byte element constructor (array init) |
| `0x001BAEA0` | `mm_StaticInit_StatusBytes` | 32 | no | constructs the 256 status bytes at 0x1F32F0 |
