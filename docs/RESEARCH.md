# Research

Background research done on 2026-10-15 before starting the project. Facts about the game come from inspecting a retail USA disc (SLUS-20377, disc VER 1.02); everything else is linked to its source.

## Is anyone else doing this?

As of 2026-10-15, no recompilation, decompilation or source port of Metropolismania was found in:

- GitHub repository and code search ("metropolismania", "machi-ing maker", "SLUS-20377", "SLUS_203.77").
- [recomp.fyi](https://recomp.fyi), the largest project directory (2,396 rows, no match).
- [Recompendium](https://nio03.github.io/unricopie/en/recomps/) (144 recomp projects, 2 of them PS2).
- [Held Games' recompiled games list](https://heldgames.com/guides/recompiled-games-list).

Prior reverse engineering does exist: [rootlm/metropolismaniaresearch](https://github.com/rootlm/metropolismaniaresearch) (2017) is a small converter from the game's `.TEX` format to BMP (8-bit palettized: width at offset 20, height at 24, a 256-entry RGBA palette at 32, then pixels).

PCSX2 already runs the game well: its [GameIndex](https://github.com/PCSX2/pcsx2/blob/master/bin/resources/GameIndex.yaml) rates SLUS-20377 compatibility 5 with no graphics fixes, and a [16:9 patch](https://github.com/PCSX2/pcsx2_patches/blob/main/patches/SLUS-20377_46FFB836.pnach) exists. A native port is about preservation, modding, widescreen, higher resolution and running without an emulator, not about making the game playable.

## The game

| | |
|---|---|
| Title | Metropolismania (JP: 街ingメーカー, Machi-ing Maker) |
| Developer | Indi |
| Publishers | Media Factory (JP, 2001-09-27), Natsume (NA 2002-10-17, EU 2002-11-08) |
| Serials | SLUS-20377 (USA), SLES-51162 (EU) |
| Related | Metropolismania 2 (PS2, 2006/2007); Simple 2000 Vol. 39 "Machi-ing Maker++" (SLPM-62398) |

Source: [Wikipedia](https://en.wikipedia.org/wiki/Metropolismania), PCSX2 GameIndex.

### What is on the disc (USA, VER 1.02)

- CD-ROM (Mode 2, 2352-byte sectors), 143 files, about 515 MB, mastered 2002-08-12.
- Boot ELF `SLUS_203.77`: 777,264 bytes, entry `0x00100008`, a single load segment at `0x00100000` (0xBDA00 bytes of code and data, 0x618280 bytes in memory with BSS).
  - **Stripped**: the symbol table is empty and there is no `.mdebug` section.
  - Built with **Metrowerks CodeWarrior** (`.comment`: "MW MIPS C Compiler (2.4.1.01)").
  - Sony SDK 2.4.x libraries, from their version strings: libkernl 2430, libcdvd 2420, libgraph 2400, libdma 2400, libipu 2400, libmc 2430, libpad 2410, libscf 2400, libsdr 2400.
  - Roughly 2,200 functions (2,263 `jr $ra` instructions).
  - Uses VU0 macro mode heavily (about 1,077 COP2 instructions), and appears to carry VU1 microcode (about 1,100 VU NOP pairs, 83 VIF `MPG`-looking words).
- IOP: `IOPRP243.IMG` replacement image plus `LIBSD`, `MCMAN`, `MCSERV`, `MODHSYN`, `MODMIDI`, `MODSEIN`, `PADMAN`, `SDRDRV`, `SIO2MAN` and a custom 187 KB `YYSOUND.IRX`.
- Data:
  - `.PSS` movies (MPEG-2, decoded with libipu), about 470 MB of the disc.
  - `.A3D` / `.A3L`: custom 3D formats.
  - `.TEX`: textures.
  - `.GF`: fonts.
  - `HD`/`BD`/`SQ`: Sony synth header, bank and MIDI sequence files.
  - `.BIN` tables (words, families, goods, events, schedules).

Details: [GAME_NOTES.md](GAME_NOTES.md).

## The static recompilation scene (October 2026)

Static recompilation translates a console executable into C (or C++) ahead of time and links it with a runtime that reimplements the hardware and OS. The result is a native program built from the user's own copy of the game. N64Recomp (Zelda64Recomp, about 55 N64 projects) started the wave. XenonRecomp and ReXGlue (Xbox 360, about 40 projects), psxrecomp (PS1), xboxrecomp, snesrecomp and others followed.

### PS2

- **[PS2Recomp](https://github.com/ran-j/PS2Recomp)** (GPL-3.0, about 3.5k stars, active).
  - `ps2xAnalyzer` scans the ELF, `ps2xRecomp` emits C++ (one C++ statement per R5900 instruction), and `ps2xRuntime` provides memory, syscalls, a GS frontend with a CPU backend, a VU1 interpreter, pad, audio and FFmpeg-backed MPEG.
  - `ps2xIOP` runs the game's original IRX modules on an R3000A interpreter.
  - For stripped games, the recommended flow is a Ghidra function map, `handler@0xADDR` stub bindings, and per-game override modules. See the upstream [walkthrough](https://github.com/ran-j/PS2Recomp/wiki/PS2Recomp-Stripped-Game-Walkthrough-For-LLMs).
- Game projects on PS2Recomp worth learning from (all bring-your-own-disc, none commit generated code):
  - [Gui-Tora/dmc-recomp](https://github.com/Gui-Tora/dmc-recomp) (Devil May Cry): pinned upstream plus a `patches/` folder and a pipeline script; boots to the title screen, not playable.
  - [KIexster/god-of-war-recomp](https://github.com/KIexster/god-of-war-recomp): function map + TOML template + overrides; reaches gameplay at about 12 to 25 fps.
  - [Waffle565/God-Of-War-Native-Port](https://github.com/Waffle565/God-Of-War-Native-Port): the most advanced. VU1 microcode and IOP modules are compiled ahead of time to C++, with a native OpenGL GS renderer; 60 fps in tested zones.
  - [z3xox/BT3-Recomp](https://github.com/z3xox/BT3-Recomp) (Budokai Tenkaichi 3): OpenGL renderer, an experimental paraLLEl-GS Vulkan backend, and a front-end installer.
- Problems these projects keep hitting:
  - Function boundaries in stripped ELFs.
  - VU1 interpreter speed (the top bottleneck in DMC and GoW).
  - GS correctness.
  - PSS/IPU movie decode.
  - Sound driver behaviour.

### Why Metropolismania is a reasonable target

- Small executable (about a third the size of God of War's 6,414 functions).
- One CD, one ELF, no overlays seen so far.
- Standard Sony libraries (libgraph, libdma, libipu, libmc, libpad, libsdr) that PS2Recomp already knows.
- A slow-paced city builder, so perfect frame pacing matters less than in an action game.
- PCSX2 runs it perfectly, so there is a reliable reference to compare against.

### Risks

- Stripped ELF: function discovery depends on Ghidra analysis and manual hints.
- VU1 microcode: likely needed for the 3D city view; the VU1 interpreter is slow, and ahead-of-time compilation may be needed later.
- Custom `YYSOUND.IRX` sound driver: it either runs on the IOP interpreter or needs a high-level replacement.
- Metrowerks code generation is less common in the PS2 scene than GCC/SN.

## Tools chosen (all free)

| Need | Choice | Alternatives considered |
|---|---|---|
| Recompiler + runtime | PS2Recomp (pinned submodule) | Full decompilation first (much slower). [decomp.me](https://decomp.me) has `mwcps2-2.4-001213`, close to this game's compiler, so a matching decomp stays possible later. |
| Disassembler | [Ghidra](https://github.com/NationalSecurityAgency/ghidra) 12.1.4 + [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) v2.1.38 | IDA Pro, Binary Ninja (paid); PS2Recomp's own analyzer (weaker on stripped ELFs) |
| Agent access to Ghidra | [LaurieWired/GhidraMCP](https://github.com/LaurieWired/GhidraMCP) 2.0 (headless via PyGhidra) | bethington/ghidra-mcp, GhydraMCP |
| Reference | Official PCSX2 | Third-party PCSX2 builds with debug servers (not used) |
| Compiler | MSVC (Visual Studio 2022) + Ninja | clang-cl (faster on very large generated trees) |

## Sources

- PS2Recomp: https://github.com/ran-j/PS2Recomp and its wiki
- Recompendium catalog: https://nio03.github.io/unricopie/en/recomps/
- recomp.fyi directory and `.recomp.json` spec: https://recomp.fyi
- Held Games recompiled games list: https://heldgames.com/guides/recompiled-games-list
- PCSX2 GameIndex and patches: https://github.com/PCSX2/pcsx2, https://github.com/PCSX2/pcsx2_patches
- Retroreversing PS2 unstripped list (Metropolismania not on it): https://www.retroreversing.com/ps2-unstripped
- Metropolismania on Wikipedia: https://en.wikipedia.org/wiki/Metropolismania
- Prior RE: https://github.com/rootlm/metropolismaniaresearch
