# Metropolismania PC

A native PC version of **Metropolismania** (PlayStation 2, Indi, 2001), being rebuilt as readable C++ source code, one function at a time.

> [!IMPORTANT]
> This repository contains **no game files**: no disc image, executable, IOP modules, movies, textures or BIOS, and none of the code generated from them. You need **your own copy** of Metropolismania for the PS2 (USA, SLUS-20377). Everything game-derived is produced locally from your disc and stays out of Git.

> [!WARNING]
> Early, experimental work. The game boots and reaches the city, but it is not ready to play through. See [docs/STATUS.md](docs/STATUS.md).

## The game

Metropolismania (街ingメーカー, *Machi-ing Maker*, in Japan) was made by Indi and published by Media Factory in Japan (2001) and by Natsume in North America and Europe (2002). It is a city builder played on foot. You are the town planner: you walk the streets, put up homes, shops and offices, and then have to talk people into actually living and working in them. A town only grows if its residents are happy.

Taking the game apart shows how much simulation sits under that small premise:

- **Families, not population numbers.** Every resident belongs to a family from `FAMILY.BIN`, with a home building, members, jobs and a daily schedule. Each family has a friendship meter (0 to 120); close friends get listed in your Town's Pages, where you can phone them, and they can introduce other families or agree to favours.
- **Buildings with opinions.** Businesses raise claims when their needs go unmet, and a claim meter keeps rising until the problem is fixed.
- **A calendar and a mood.** Each in-game day is a month, with weather, holiday months and a town approval rating from -99 to 120. At 118 and up the town counts as popular and families start calling to ask about moving in.
- **96 kinds of events.** Paydays and farewell parties, but also a thief, a forgetful granny, missing children, a crazy cult, a caveman and an alien, each with its own handlers.
- **Talking is the core.** Visiting a building or stopping someone in the street opens a talk view, and conversations run on the game's own little script language stored in `WORDS*.BIN`.
- **Mission goals per town.** Each stage sets a population target and a theme share (high-class, business, leisure, fishing, farming or industrial).

The full picture, subsystem by subsystem, is in [docs/map](docs/map/README.md). Disc layout and data formats are in [docs/GAME_NOTES.md](docs/GAME_NOTES.md).

## The goal

It already runs well in PCSX2, so this project is not about making the game playable. It is about understanding it completely and giving it a native, readable codebase that can be fixed, modded and extended: widescreen, higher resolutions and other platforms without an emulator.

The approach is the one OpenRCT2 used for RollerCoaster Tycoon 2: keep the game running at every step and replace its code piece by piece.

1. **Map everything.** Every function in the game is named, classified by subsystem and given a full C prototype (`config/signatures.txt`), with its structs in `config/types/`. This step is done for all 1,481 game functions.
2. **Rewrite by hand.** [src/port](src/port/README.md) holds the game's code as readable C++, organised by subsystem (city, families, events, talks, the WORDS script, saves, sound, UI). Each rewritten function takes over from the original at its address.
3. **Prove it matches.** Every rewritten function is checked against the original by differential tests: calls recorded from a running game are replayed through both versions, and memory, registers and results must come out identical before the function counts as done.

Progress per subsystem is tracked in [docs/PORT_STATUS.md](docs/PORT_STATUS.md). As far as we could find, this is the first decompilation or recompilation project for the game ([research notes](docs/RESEARCH.md)).

## Status

| Area | State |
|---|---|
| Boot, logos, title screen, memory card check | Works |
| Input | Works (keyboard, pad, scripted) |
| The city | Reached: HUD, cursor and 3D terrain render, about 20 fps (CPU rendering) |
| Movies, music, sound effects | Play, through a new SPU2 emulation (with reverb) |
| Saves | Memory card check passes; loading a save from a PCSX2 memory card works |
| Mapping (names, types, prototypes) | Done for every game function |
| Readable source port | In progress: 1,048 of the game's 1,481 functions rewritten and verified (71%) ([details](docs/PORT_STATUS.md)) |
| VU1 microcode and sound driver translation | Not started |
| Widescreen, hi-res, Linux | Not started |

## How it works

The starting point is a static recompilation made with [PS2Recomp](https://github.com/ran-j/PS2Recomp): the game's MIPS R5900 code is translated to C++ from your disc and linked against PS2Recomp's runtime, which provides memory, the kernel, graphics, VU1, DMA and an IOP that runs the game's original IOP modules (sound, pad, memory card). That machine-translated code is the scaffolding that keeps the game running. The readable port in `src/port/` replaces it function by function; once a function is rewritten, its generated version is kept only as the reference the differential tests compare against.

- `tools/extract_disc.py` reads the files straight from your BIN/CUE or ISO.
- Ghidra (with the Emotion Engine extension) finds the functions in the stripped executable and applies the names, types and prototypes from `config/`.
- Game-specific fixes live in `config/` and `src/overrides/`; changes to PS2Recomp itself live in `patches/`.

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
python tools/pipeline.py stage              # generated code + overrides + src/port -> runtime tree
python tools/pipeline.py build              # ps2EntryRunner
python tools/pipeline.py run                # timed run, log in logs/
```

To copy your saves over from PCSX2: `python tools/mc_extract.py path/to/Mcd001.ps2`. The port's own tests (`capture`, `difftest`) are described in [src/port/README.md](src/port/README.md).

`original/`, `generated/`, `work/`, `build/` and `logs/` are git-ignored.

## Credits

- [PS2Recomp](https://github.com/ran-j/PS2Recomp) by ran-j and contributors, the recompiler and runtime this project starts from.
- Open PS2Recomp pull requests carried in `patches/` until they are merged: VU0/VU1, VIF and GIF fixes by @VariableDeclared, @yanm1103, @Fiztba, @luisbraganca and @drakolordx7 (see each patch header).
- [psx-spx](https://psx-spx.consoledev.net/) (SPU voice pipeline, envelope and interpolation details) for the SPU2 emulation.
- [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) by chaoticgd.
- [PCSX2](https://github.com/PCSX2/pcsx2), the reference for how the hardware behaves, and Arapapa's widescreen patch for the known FOV addresses.
- [rootlm/metropolismaniaresearch](https://github.com/rootlm/metropolismaniaresearch) for early notes on the texture format.
- [OpenRCT2](https://github.com/OpenRCT2/OpenRCT2), whose function-by-function approach this port follows.

## License and disclaimer

The code in this repository is licensed under the [GNU GPL v3](LICENSE), the same license as PS2Recomp, whose runtime it links against.

This is a fan preservation project. It is not affiliated with or endorsed by Indi, Media Factory, Natsume, D3 Publisher or Sony Interactive Entertainment. "PlayStation" and "PS2" are trademarks of Sony Interactive Entertainment. Metropolismania is the property of its respective owners. Do not ask for or share game files.
