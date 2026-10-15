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
| M6 | City rendering (VU1 path, GS) | In progress | City scene and HUD run; 3D ground/buildings missing; 224-line field display; colour issues |
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

**Where it is now (2026-10-15):** boots, loads all 10 IOP modules on the IOP emulator, shows the **Natsume logo**, a second logo and the **title screen** (with "(c) 2002 Natsume Inc. (c) 2001 Indi Software / MEDIA FACTORY"), passes the memory card check, starts a **New Game**, reaches the intro movie, and (with `MMPC_SKIP_MOVIES=1`) enters the **city game**: the HUD (Month 4, Year 1, Pop 0, Ar 50%) and cursor render over the sky. The 3D city itself (ground, buildings) does not show yet, and movies stall.

Known graphics issues: the game renders interlaced 640x224 fields and the runtime shows them 1:1 (only the middle 224 lines of the window are used), some colours look off (Natsume logo background), dot artifacts on sprites, and the 3D title model (VU1) is missing.

0. **Game flow, from the decompiled code.** `main` (`0x0012AE20`) runs `mm_InitSystemModules`, then the main loop `0x001854E0`, which calls the game object's per-frame method `0x0012ABA0` (vtable `0x001BCE60`). That method is a scene state machine: scene A (logos, memory card check, title; `0x00171220`/`0x00170BE0`), scene B (`0x00152910`), scene C (city, `0x00182700`). Trace with `pipeline.py run --env MMPC_TRACE=1`.
1. **Pointer-only functions.** Instead of adding the two constructors by hand, `tools/find_code_pointers.py` (run by `pipeline.py pointers`) scans the ELF for code addresses stored in data or built with `lui`/`addiu`. It keeps targets that start right after a previous function's return or tail call, and that decode as valid R5900 code up to their own return. Two rounds added **97 functions**: the static constructors, standard library and game vtables, and a 76-entry handler table at `0x001B1BF0`. Map now: 2,189 functions.
2. **IOP file access** (`patches/0001`). The sound driver `YYSOUND.IRX` opens its data with ioman `open`/`read`/`lseek`, which the IOP emulator did not implement. The patch adds read-only file access through the host.
3. **TODO handlers.** 25 SDK functions whose runtime handler is still a TODO (returns -1) are now left as recompiled original code instead (`pipeline.py` decides this from the runtime sources).
4. **Memory card directory listing** (`patches/0003`). The game lists the card one entry per call (`sceMcGetDir` mode 0, 1, 2, ...); the runtime ignored the continue mode and returned the first entry forever, so the memory card check never finished.
5. **Debugger overlay** (`patches/0004`). The runtime's ImGui debugger started open and covered the game; it now starts hidden (F1 toggles, `PS2X_DEBUG_UI=1` opens it).
7. **VIF1 stream desync** (`patches/0006`). When a GIF IMAGE transfer was split over two VIF1 DIRECT commands, the runtime took the remaining image quadwords straight from the VIF stream, swallowing the next DIRECT's VIFcode quadword. The interpreter lost sync and garbage MPG/MSCAL commands overwrote VU1 microcode (VU1 ran data at `pc=0x710`). Found with `PS2X_VIF_TRACE=2` (`patches/0005`). After the fix the title flow completes and the game reaches scene B (intro movie).
8. **Movies.** `mm_PlayMissionMovie` (`0x0019DE10`) plays `DATA\MPEG\MISSION<n>.PSS` in a blocking loop that polls the pad itself (Start/Cross fades it out). The runtime's MPEG path decodes only a few frames and the picture is garbled, so `MMPC_SKIP_MOVIES=1` (override module) returns "played" at once. With that, the game reaches **scene C, the city**.
6. **Scripted input** (`patches/0002`). `PS2X_PAD_SCRIPT` feeds timed button presses for unattended tests (`pipeline.py run --pad file`).
