# AGENTS.md

Conventions for anyone (people or coding agents) working in this repository.

## What this repo is

A static recompilation of **Metropolismania** (PS2, USA, SLUS-20377, disc VER 1.02) built on [PS2Recomp](https://github.com/ran-j/PS2Recomp). The game's MIPS R5900 code is translated to C++ from the user's own disc and linked against PS2Recomp's runtime.

## Hard rules

1. **Never commit game data.** No disc images, ELF, IRX, IOPRP image, movies, textures, BIOS, memory cards, save states or anything extracted from the disc. `original/` is git-ignored for this reason.
2. **Never commit generated code.** `generated/` and `work/` are produced locally from the user's ELF.
3. **Never commit Sony SDK documents** or anything copied from them.
4. **Addresses are build-specific.** Every address in `config/`, `src/overrides/` and `src/port/` is for SLUS_203.77 with file CRC32 `4FA3F727` (PCSX2 CRC `46FFB836`). Other regions or revisions need their own maps.
5. **Don't edit generated output.** Fixes go in, in order of preference:
   - `config/metropolismania.template.toml`: `handler@0xADDR` stubs, entry points, instruction patches.
   - `config/symbols.csv`: names applied to the Ghidra project before export.
   - `src/overrides/`: per-game C++ overrides, matched by ELF name, entry point and CRC32.
   - `patches/`: changes to PS2Recomp itself. Keep them small and send them upstream when they are general.
6. **Pinned upstream.** `ext/PS2Recomp` is a submodule pinned in `upstream.lock.json`. Bump both together, on purpose.
   - `patches/00NN-upstream-prNNN.patch` are open, unmerged PS2Recomp pull requests that this game needs, applied in number order. Each file starts with the PR link, title and author. Drop a patch once its PR is merged and the pin is bumped past it.
   - `patches/01NN-*.patch` are this project's own runtime changes, applied after the upstream ones. Send the general ones upstream.
7. **Writing style.** No em dashes in docs, comments or commit messages.
8. **The readable port.** `src/port/` holds the game's code rewritten by hand as readable C++, one function at a time (published in this repo by the owner's decision, 2026-10-16). Every ported function replaces its generated counterpart by address and must match it on the differential tests before it counts as done.
   - Raw decompiler output (`work/decomp/`) is a reading aid only. Never commit it, and never paste it into `src/port/` unedited.
   - Captures made from a running game (`work/difftest/`) and code generated from the game's VU1 microcode or IOP modules (`work/vu1_aot/`, `work/iop_aot/`) are game-derived and stay in `work/`.

## Layout

| Path | Tracked | Purpose |
|---|---|---|
| `ext/PS2Recomp/` | submodule | Pristine upstream at the pinned commit |
| `patches/` | yes | Our patches to upstream, applied by `pipeline.py bootstrap` |
| `config/` | yes | Recompiler config template, Ghidra function map, symbol names and signatures, `types/` (game structs for Ghidra), `port_status.csv` (per-function map and port state) |
| `src/overrides/` | yes | Game override module(s) |
| `src/port/` | yes | Readable C++ port of the game's code (rule 8) |
| `tools/` | yes | `extract_disc.py`, `pipeline.py`, Ghidra scripts |
| `docs/` | yes | Research, game notes, status |
| `original/` | no | Files extracted from your disc |
| `work/` | no | Upstream copy + patches + generated code, Ghidra project, rendered config, `local.json` |
| `generated/` | no | `ps2_recomp` output |
| `build/` | no | CMake build tree |
| `logs/` | no | Build and run logs |

## Pipeline

```
python tools/pipeline.py bootstrap   # ext/PS2Recomp + patches/ -> work/PS2Recomp
python tools/pipeline.py tools       # build ps2_analyzer, ps2_recomp
python tools/pipeline.py extract     # disc image -> original/
python tools/pipeline.py analyze     # Ghidra headless -> config/funcmap.csv
python tools/pipeline.py recompile   # -> generated/
python tools/pipeline.py stage       # generated/ + src/overrides -> runtime tree
python tools/pipeline.py build       # ps2EntryRunner
python tools/pipeline.py run         # timed run, log in logs/
```

`work/local.json` holds machine paths, for example `{"ghidra_install_dir": "C:/tools/ghidra_12.1.4_PUBLIC"}`.

## Debugging order (from the upstream stripped-game walkthrough)

1. `Function at address 0x... not found`: improve the function map or add an `entry_points` hint.
2. `Unimplemented PS2 stub called`: bind `handler@0xADDR`, or triage with `ret0`/`ret1`/`reta0` and replace later.
3. `[Syscall TODO]`: implement the syscall (a patch to upstream).
4. `PC not updating`: inspect the generated function at that PC; check raw handler bindings without a `$ra` fallback.

Use PCSX2 running the same disc as ground truth.
