# game

The game object (vtable `0x001BCE60`) lives on `mm_Main`'s stack. `mm_GameUpdate` (`0x0012ABA0`) is the scene
state machine: scene A (logos, memory card check, title; see `scene`), scene B (stage introduction and mission
briefing) and scene C (the city, `mm_CityScene_*` in `city`).

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

3 functions (ported 1, verified 2). Blocks: 2 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0012ABA0` | `void mm_GameUpdate(Game *game)` | 572 | yes | Game class (vtable 0x1BCE60) slot +0xC, once per frame: sound update, then the scene state machine (+4): creates/updates/deletes the title, intro and city scenes |
| `0x0012ADE0` | `void mm_GameInit(Game *game)` | 52 | no | Game slot +8: sound state init, scene state 0, no scenes |
| `0x0012AE20` | `int mm_Main(void)` | 100 | yes | probes the heap, runs static init, constructs the App on the stack, switches it to the Game vtable 0x1BCE60, runs mm_MainLoop, destroys it |
