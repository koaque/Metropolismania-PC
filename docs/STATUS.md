# Status

Last updated: 2026-10-15. Upstream PS2Recomp pinned at `eb8688b2` (2026-10-14).

## Milestones

| | Milestone | State | Notes |
|---|---|---|---|
| M0 | Repo, research, pipeline | Done | [RESEARCH.md](RESEARCH.md) |
| M1 | Toolchain | Done | Ghidra 12.1.4 + EE Reloaded 2.1.38, MSVC 14.38, PS2Recomp tools build |
| M2 | Extract and analyze | Done | 2,091 functions; 569 named (558 from PS2Recomp's SDK signature scan, 11 by hand); VU microcode located |
| M3 | First recompile and build | Done | Recompile: 0 errors, 467 control-flow warnings, 14,380 C++ files. `ps2EntryRunner.exe` builds with MSVC (RelWithDebInfo, 0 warnings) |
| M4 | Boot to Natsume / Media Factory logos | Next | First blocker known (below) |
| M5 | Title screen and input | Not started | |
| M6 | City rendering (VU1 path, GS) | Not started | |
| M7 | Movies (PSS), sound (YYSOUND, HD/BD/SQ), memory card | Not started | |
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
