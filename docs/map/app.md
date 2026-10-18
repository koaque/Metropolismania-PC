# app

The application object, built by `mm_Main`. `mm_App_ctor` (`0x00185740`) reboots the IOP with `IOPRP243.IMG`,
loads `SIO2MAN`, `PADMAN`, `MCMAN` and `MCSERV`, and sets up the GS, pads and the file queue.

`mm_MainLoop` (`0x001854E0`) never returns. Every frame it updates the pads, pumps the file queue
(`mm_FileQueue_Pump`) and the memory card queue (`mm_McQueue_Pump`), flushes the YYSound command ring
(`mm_SoundCmd_Flush`), waits on `sceGsSyncPath`, calls the game object's per-frame method (`mm_GameUpdate`) and
swaps the double buffer of the 640x448 interlaced display.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

4 functions (typed 3, verified 1). Blocks: 3 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001854E0` | `void mm_MainLoop(int app)` | 460 | yes | calls app vtable +8 once, sets up the display (640x448), then per frame: pad, file queue, memory card queue, sound, kick the DMA list, flip it, app vtable +0xC (the game update), swap buffers on vsync |
| `0x001856C0` | `void mm_App_OnInit(void)` | 8 | no | App base class (vtable 0x1BD8D0) slot +8; empty |
| `0x001856D0` | `int mm_App_dtor(int app, int16_t deleteFlag)` | 108 | yes | vtable 0x1BD8D0; shuts down sound (0x18F450), frees the DMA lists and the font; deletes this when the flag is > 0 |
| `0x00185740` | `int mm_App_ctor(int app)` | 644 | yes | (was mm_InitSystemModules) vtable 0x1BD8D0; boots the IOP with IOPRP243.IMG, inits CD/SIF/file system, seeds the random generator with 12345, loads SIO2MAN/MCMAN/MCSERV/PADMAN, inits pad, file queue, font, memory card queue, DMA lists and sound |
