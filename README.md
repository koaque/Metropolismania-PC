# Metropolismania PC

A native PC port of **Metropolismania** (PlayStation 2, Indi / Natsume, 2002), made by statically recompiling the game's MIPS R5900 code to C++ with [PS2Recomp](https://github.com/ran-j/PS2Recomp).

> [!IMPORTANT]
> This repository contains **no game files**: no disc image, executable, IOP modules, movies, textures or BIOS, and none of the C++ generated from them. You need **your own copy** of Metropolismania for the PS2 (USA, SLUS-20377). Everything game-derived is produced locally from your disc and stays out of Git.

> [!WARNING]
> Very early, experimental work. Nothing is playable yet. See [docs/STATUS.md](docs/STATUS.md).

## Why

Metropolismania (Machi-ing Maker in Japan) is a small, odd city builder where you grow a town by befriending its residents. It already runs well in PCSX2, so this project is not about making it playable. It is about a native build: preservation, modding, widescreen and higher resolution without an emulator.

As far as we could find, this is the first recompilation or decompilation project for the game ([research notes](docs/RESEARCH.md)).

## Status

| Milestone | State |
|---|---|
| Research, repo, pipeline | Done |
| Disc extraction, Ghidra analysis, function map (2,249 functions) | Done |
| First recompile (0 errors) and native build | Done: the runner builds, opens its window and starts executing |
| Boot to logos, title screen, memory card check | Done: Natsume logo, title, New Game |
| Input | Works (keyboard, pad, scripted) |
| City game (scene C) | Reached: HUD, cursor and the 3D terrain render, about 20 fps (CPU rendering on several threads) |
| Movies, sound, saves | In progress: music, sound and the PSS movies (with their audio) play through a new SPU2 emulation; memory card check passes |
| Readable source port | Mapping done: every function is named and classified, every game function has a full C prototype (`config/signatures.txt`), and all are listed with notes in [docs/map](docs/map/README.md) (progress in [docs/PORT_STATUS.md](docs/PORT_STATUS.md)); porting next |
| Widescreen, hi-res, Linux | Not started |

## Requirements (Windows, for now)

- Your own Metropolismania disc dump (USA, SLUS-20377, disc VER 1.02), as BIN/CUE or ISO.
- Visual Studio 2022 with the C++ x64 workload (MSVC, CMake and Ninja come with it).
- CMake 3.21 or newer, Python 3.11 or newer, Git.
- Ghidra 12.1.4 with JDK 21, plus the [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) extension (v2.1.38, Ghidra 12.1.4 build).

## Building

```sh
git clone --recurse-submodules https://github.com/koaque/Metropolismania-PC.git
cd Metropolismania-PC
```

Tell the pipeline where Ghidra lives by creating `work/local.json`:

```json
{ "ghidra_install_dir": "C:/path/to/ghidra_12.1.4_PUBLIC" }
```

Then run the steps, or `all` in one go:

```sh
python tools/pipeline.py bootstrap          # pinned PS2Recomp + patches/ -> work/PS2Recomp
python tools/pipeline.py tools              # build ps2_analyzer and ps2_recomp
python tools/pipeline.py extract path/to/your/game.cue   # -> original/ (checks the build is SLUS-20377 v1.02)
python tools/pipeline.py symbols            # SDK names from PS2Recomp's signature scan -> config/symbols.csv
python tools/pipeline.py analyze            # Ghidra headless -> config/funcmap.csv
python tools/pipeline.py recompile          # -> generated/
python tools/pipeline.py stage              # generated code + overrides -> runtime tree
python tools/pipeline.py build              # ps2EntryRunner
python tools/pipeline.py run                # timed run, log in logs/
```

`original/`, `generated/`, `work/`, `build/` and `logs/` are git-ignored.

## How it works

1. `tools/extract_disc.py` reads the ISO 9660 filesystem straight from your BIN/CUE or ISO and pulls out the boot ELF and IOP modules.
2. Ghidra (with the Emotion Engine extension) finds function boundaries in the stripped ELF. `config/symbols.csv` names SDK functions first, so the exporter can bind them to PS2Recomp's runtime handlers.
3. `ps2_recomp` translates every function to C++. The runtime provides memory, kernel syscalls, the GS, VU1, DMA and an IOP that runs the game's original IRX modules.
4. Game-specific fixes live in `config/` and `src/overrides/`; changes to PS2Recomp itself live in `patches/`.

More detail: [AGENTS.md](AGENTS.md), [docs/GAME_NOTES.md](docs/GAME_NOTES.md), [docs/map](docs/map/README.md).

## Credits

- [PS2Recomp](https://github.com/ran-j/PS2Recomp) by ran-j and contributors, which does the heavy lifting.
- Open PS2Recomp pull requests carried in `patches/` until they are merged: VU0/VU1, VIF and GIF fixes by @VariableDeclared, @yanm1103, @Fiztba, @luisbraganca and @drakolordx7 (see each patch header).
- [psx-spx](https://psx-spx.consoledev.net/) (SPU voice pipeline, envelope and interpolation details) for the SPU2 emulation.
- [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) by chaoticgd.
- [PCSX2](https://github.com/PCSX2/pcsx2), the reference for how the hardware behaves, and Arapapa's widescreen patch for the known FOV addresses.
- [rootlm/metropolismaniaresearch](https://github.com/rootlm/metropolismaniaresearch) for early notes on the texture format.

## License and disclaimer

The code in this repository is licensed under the [GNU GPL v3](LICENSE), the same license as PS2Recomp, whose runtime it links against.

This is a fan preservation project. It is not affiliated with or endorsed by Indi, Media Factory, Natsume, D3 Publisher or Sony Interactive Entertainment. "PlayStation" and "PS2" are trademarks of Sony Interactive Entertainment. Metropolismania is the property of its respective owners. Do not ask for or share game files.
