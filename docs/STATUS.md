# Status

Last updated: 2026-10-15. Upstream PS2Recomp pinned at `eb8688b2` (2026-10-14).

## Milestones

| | Milestone | State | Notes |
|---|---|---|---|
| M0 | Repo, research, pipeline | Done | [RESEARCH.md](RESEARCH.md) |
| M1 | Toolchain | Done | Ghidra 12.1.4 + EE Reloaded 2.1.38, MSVC 14.38, PS2Recomp tools build |
| M2 | Extract and analyze | Done | 2,091 functions; 569 named (558 from PS2Recomp's SDK signature scan, 11 by hand); VU microcode located |
| M3 | First recompile and build | Done | Recompile: 0 errors, 467 control-flow warnings, 14,380 C++ files. `ps2EntryRunner.exe` builds with MSVC (RelWithDebInfo, 0 warnings) |
| M4 | Boot to logos, title, memory card check | Done | Natsume logo, title screen, memory card check pass; New Game starts loading |
| M5 | Title screen and input | Done (scripted) | Cross/Start reach New Game; keyboard and pad work |
| M6 | City rendering (VU1 path, GS) | In progress | City terrain renders (about 40k triangles / 2 s); 4:3 presentation fixed; buildings, people and lighting still to check |
| M7 | Movies (PSS), sound (YYSOUND, HD/BD/SQ), memory card | In progress | Memory card check passes; movies decode only a few frames (demux backpressure), garbled on screen; skip toggle `MMPC_SKIP_MOVIES=1` |
| M8 | Performance, widescreen, hi-res, Linux, installer | Not started | |

## Recompile details (M3)

- Function map: 14,379 entries (2,091 functions plus executable labels used as resume points and switch cases).
- `config/layout.toml` drops two data addresses (`0x001B1790`, `0x001B97F0`) that Ghidra exported as code; they had caused 1,323 unhandled-instruction errors.
- 308 SDK functions are bound to PS2Recomp runtime handlers (HLE). The rest of the SDK code, and all game code, is recompiled as-is.
- 467 warnings are unresolved indirect jumps (`JR`/`JALR`). The recompiler adds fallback dispatch entries for them; switch tables and function pointers are expected in a C++ codebase.

## First smoke run (2026-10-15)

- The runner opens its window (raylib, OpenGL 3.3, 640x448), initializes audio, loads the ELF, applies the `metropolismania-us` override and starts at `0x00100008`.
- It stops at once with `guest-branch:missing-target`: an indirect call from `0x00100A34` (inside `__initialize_cpp_rts`, the Metrowerks static-constructor loop) to `0x001BAEA0`.
- Cause: the static-constructor table at `0x001BCDD0` lists `0x001BAE80`, `0x001BAEA0`, `0x001BAEC0` and `0x001BCD90`. Two of them (`0x001BAEA0`, `0x001BAEC0`) are only reachable through that table, so they are missing from the function map.
- Next step (M4): add them as `entry_points`, then keep going down the upstream triage order.

## Boot triage log (M4)

**Where it is now (2026-10-15):** boots, loads all 10 IOP modules on the IOP emulator, shows the **Natsume logo**, a second logo and the **title screen** (with "(c) 2002 Natsume Inc. (c) 2001 Indi Software / MEDIA FACTORY"), passes the memory card check, starts a **New Game**, reaches the intro movie, and (with `MMPC_SKIP_MOVIES=1`) enters the **city game**: the HUD (Month 4, Year 1, Pop 0, Ar 50%) and cursor render over the sky. The **3D city terrain now renders** (after replacing the runtime's HLE `sceVu0` matrix library with the game's own code). Movies still stall.

Known graphics issues: some colours look off (Natsume logo background), dot artifacts on sprites, and the 3D title model (VU1) is missing. The display flicker is fixed (item 11).

0. **Game flow, from the decompiled code.** `main` (`0x0012AE20`) runs `mm_InitSystemModules`, then the main loop `0x001854E0`, which calls the game object's per-frame method `0x0012ABA0` (vtable `0x001BCE60`). That method is a scene state machine: scene A (logos, memory card check, title; `0x00171220`/`0x00170BE0`), scene B (`0x00152910`), scene C (city, `0x00182700`). Trace with `pipeline.py run --env MMPC_TRACE=1`.
1. **Pointer-only functions.** Instead of adding the two constructors by hand, `tools/find_code_pointers.py` (run by `pipeline.py pointers`) scans the ELF for code addresses stored in data or built with `lui`/`addiu`. It keeps targets that start right after a previous function's return or tail call, and that decode as valid R5900 code up to their own return. Two rounds added **97 functions**: the static constructors, standard library and game vtables, and a 76-entry handler table at `0x001B1BF0`. Map now: 2,189 functions.
2. **IOP file access** (`patches/0100`). The sound driver `YYSOUND.IRX` opens its data with ioman `open`/`read`/`lseek`, which the IOP emulator did not implement. The patch adds read-only file access through the host.
3. **TODO handlers and HLE overrides.** SDK functions whose runtime handler is still a TODO (returns -1), plus any matching `[pipeline].force_recompile` patterns, are left as the game's own recompiled code. `ps2_recomp` binds functions to runtime handlers by *name* as well as by the stub list, so the pipeline renders a copy of the function map (`work/funcmap.rendered.csv`) with those functions renamed `<name>_lle`. (The first version only edited the stub list and silently had no effect.)
4. **Memory card directory listing** (`patches/0102`). The game lists the card one entry per call (`sceMcGetDir` mode 0, 1, 2, ...); the runtime ignored the continue mode and returned the first entry forever, so the memory card check never finished.
5. **Debugger overlay** (`patches/0103`). The runtime's ImGui debugger started open and covered the game; it now starts hidden (F1 toggles, `PS2X_DEBUG_UI=1` opens it).
7. **VIF1 stream desync** (now upstream PR #269, `patches/0025`). When a GIF IMAGE transfer was split over two VIF1 DIRECT commands, the runtime took the remaining image quadwords straight from the VIF stream, swallowing the next DIRECT's VIFcode quadword. The interpreter lost sync and garbage MPG/MSCAL commands overwrote VU1 microcode (VU1 ran data at `pc=0x710`). Found with `PS2X_VIF_TRACE=2` (`patches/0104`). After the fix the title flow completes and the game reaches scene B (intro movie).
9. **The 3D city: HLE `sceVu0` library.** In the city, VU1 kicked about 15 MB of GS packets per 2 s but only about 450 primitives were drawn: every vertex came out of VU1 at the same off-screen point with the no-draw bit set. The game builds its camera and projection matrices with Sony's `sceVu0*` library, which the runtime replaces with C++ HLE versions; with `force_recompile = ["sceVu0*"]` the game's own (VU0 macro-mode) code runs instead, and the city draws about 40,000 triangles per 2 s. The terrain (dirt, forest) now renders.
10. **Performance.** The city runs at about 5 fps (title and menus at the original 30). A sampling profiler (`PS2X_PROFILE=1`, `patches/0108`) showed ~21% of the game thread writing debug logs (the runtime's aggressive logs, now off: city loading dropped from 22 s to under 1 s), ~40% in the VU1 interpreter (about 3-7 million VU cycles per second against ~14 million needed) and ~27% in the software GS rasterizer. A GS worker thread (`patches/0110`) rasterizes in parallel with the game thread and lifts the city from about 5 to about 9 fps. It is on by default now (`PS2X_GS_THREAD=0` turns it off): the city corruption it seemed to cause earlier was the frame buffer 1 / Z buffer overlap fixed in `patches/0112`. With it on, the GS worker is about 96% busy and the game thread spends about two thirds of its time in the VU1 interpreter, so both need to get faster for 30 fps. Next: ahead-of-time compiled VU1 programs (13 microprograms) and fixing the GS worker race.
    Per-function timing (`MMPC_TRACE_CALLS=1`, override module) settles where a city frame goes: over 2.1 s, 12 frames, the VIF1 kick at the end of each frame (`0x00187290`, which starts the frame's VIF1 DMA chain) takes 1,958 ms, because the runtime runs the whole chain (VIF1 unpacks, VU1 microprograms, GS drawing) synchronously inside that store. The game logic (`mm_GameUpdate`) takes 8 ms in total. So city speed is entirely VU1 plus GS throughput: about 163 ms per frame against 33 ms for 30 fps.
    The runtime's vsync is paced by guest progress as well as host time: a slow frame waits for one vsync, so the vsync count follows the frame rate (about 6 per second in the city). Timing that counts vsyncs runs in slow motion instead of skipping frames.
11. **Display and flicker.** Output is scaled to 4:3 (`patches/0106`). The heavy flicker had a real cause: the game double-buffers interlaced fields with `sceGsSetDefDBuffDc` (two 640x224 buffers, the odd one drawn half a line lower), and the runtime's HLE version put the second frame buffer at the Z buffer address. Every other field was drawn into a buffer that the depth writes then wiped, so the display alternated between a picture and a black frame. `patches/0112` places the second buffer one buffer after the first (page `0x46`), with the Z buffer after both (`0x8C`).
    The game draws a new field every vsync from a VSync interrupt handler (`0x0019C4C0`) and flips the display between the two buffers each time. Shown one after the other on a monitor, the two fields shimmer by half a line, so `patches/0113` weaves the newest two fields into one 640x448 frame, as a TV does (on while the display alternates every vsync, with hysteresis; `PS2X_DEINTERLACE=off` shows single fields). The two fields can come from different game frames, so the weave is motion adaptive: where a field changed and its pixel does not fit between its vertical neighbours, the line is interpolated instead. Measured with `tools/screenshot.py --burst` and a combing score over the captures: the city is pixel-stable with full 448-line detail (text and clock edges smooth instead of line-doubled), and on the animated title the adaptive weave combs about 12 times less than a plain weave (0.08 against 0.99). The runtime's line-selecting "bob" output is off by default (`patches/0109`, `PS2X_DEINTERLACE=bob`); it only applies to the other interlace mode (SMODE2.FFMD=0, full-height buffers).
8. **Movies.** `mm_PlayMissionMovie` (`0x0019DE10`) plays `DATA\MPEG\MISSION<n>.PSS` in a blocking loop that polls the pad itself (Start/Cross fades it out). The runtime's MPEG path decodes only a few frames and the picture is garbled, so `MMPC_SKIP_MOVIES=1` (override module) returns "played" at once. With that, the game reaches **scene C, the city**.
6. **Scripted input** (`patches/0101`). `PS2X_PAD_SCRIPT` feeds timed button presses for unattended tests (`pipeline.py run --pad file`).
