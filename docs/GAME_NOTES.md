# Game notes: Metropolismania (USA)

Everything here is for **SLUS-20377, disc VER 1.02** (boot ELF `SLUS_203.77`, file CRC32 `4FA3F727`, PCSX2 CRC `46FFB836`, SHA-1 `87ac40401d22a5745d09d94f52f6c1519748de8b`). Addresses do not carry over to other regions or revisions.

## Disc layout

CD-ROM, Mode 2, 2352-byte sectors, ISO 9660 volume `1_02`, publisher `NATSUME INC.`, mastered 2002-08-12. 143 files.

| Path | Contents |
|---|---|
| `SYSTEM.CNF` | `BOOT2 = cdrom0:\SLUS_203.77;1`, `VER = 1.02`, `VMODE = NTSC` |
| `SLUS_203.77` | Boot ELF, 777,264 bytes |
| `SYS/` | `IOPRP243.IMG` and IOP modules (below) |
| `DATA/` | `FONT1022.GF`, `FONT1220.GF`, `FONT1222.GF`, `GROUND.TEX`, `SPLASH.TEX`, `SPOT.TEX`, `WATER.TEX`, `STATIC.ICO` (memory card icon) |
| `DATA/A3D/` | `CHAR.A3D` (15 MB), `CHAROBJ.A3D`, `OBJECT.A3D`, `SKY.A3D`, `JUNK.A3D`, `DESTRUCT.A3D` |
| `DATA/A3D/INTRO/` | Title, loading and logo assets (`NATSUME.TEX`, `MF_US.TEX`, `LOGO.TEX`, `TITLE.A3D`, `TITLE.TEX`, ...) |
| `DATA/BIN/` | Game tables: `WORDS*.BIN` (text), `FAMILY.BIN`, `GOODS.BIN`, `EVENT.BIN`, `SCHEDULE.BIN`, `STAGE.BIN`, `PROFILE.BIN`, `CLAIM.BIN`, `COLLI.A3L`, `MYHOME.A3L`, ... |
| `DATA/MPEG/` | `OPENING.PSS`, `MISSION1-5.PSS`, `ENDING.PSS` (about 470 MB total) |
| `DATA/SOUND/` | `SE.BD` plus `HD/`, `BD/`, `SQ/` folders (24 songs each: synth header, sample bank, MIDI sequence) |

## Boot ELF

- One `PT_LOAD` segment: vaddr `0x00100000`, 0xBDA00 bytes in the file, 0x618280 bytes in memory (BSS up to `0x00718280`). The segment is read/write/execute, so Ghidra treats data as code unless told otherwise (see `config/layout.toml`).
- Stripped: `.symtab` and `.strtab` are empty, and there is no `.mdebug`.
- Compiler: Metrowerks CodeWarrior, `.comment` = "MW MIPS C Compiler (2.4.1.01)". The Metrowerks C++ runtime is linked (`__construct_array`, `__throw`, exception tables).
- Sony SDK 2.4.x libraries: libkernl 2430, libcdvd 2420, libgraph 2400, libdma 2400, libipu 2400, libmc 2430, libpad 2410, libscf 2400, libsdr 2400. libvu0 and libmpeg are also present.
- Ghidra finds 2,025 functions on its own. Applying the PS2Recomp analyzer's SDK names adds 66 more, giving **2,091 functions**, plus 12,288 executable labels (resume points and switch cases).

### Memory map highlights

| Range | Contents |
|---|---|
| `0x00100008` | `_start` (entry) |
| `0x00100000-0x00102700` | Metrowerks C++ runtime |
| `0x00102710-0x00105820` | libgraph, libdma, libvu0 |
| `0x00105880-0x0010D6E8` | libmpeg / libipu (movie decoding) |
| `0x0010D960-0x0010E1D0` | Kernel syscall wrappers (`SetGsCrt`, `CreateThread`, `FlushCache`, ...) |
| `0x00110000-0x00116xxx` | libkernl SIF (RPC, IOP heap, module loading, IOP reboot) |
| `0x0011A490-0x0011B9F0` | libcdvd |
| `0x0011BBF8-0x0011DC90` | libpad, libmc |
| `0x001A3C58` | End of EE code (`sceScfGetLocalTimefromRTC` ends here) |
| `0x001A3E98-0x001AB1B8` | **VU microcode** (about 29 KB, 26 programs end with an E-bit upper instruction; two large blocks of about 11.5 KB and 10.5 KB fit VU1's 16 KB micro memory) |
| `0x001AB5C0` | String `PsIIlibkernl2430`, then more code from `0x001AB5D8` |
| `0x001BAE80`, `0x001BCD90` | Static initializers (code placed after data by the Metrowerks linker) |

## Boot sequence (from call analysis)

`mm_InitSystemModules` (`0x00185740`):
1. `sceSifInitRpc`.
2. `sceSifRebootIop("cdrom0:\SYS\IOPRP243.IMG;1")` and `sceSifSyncIop`.
3. `sceSifInitIopHeap`, `sceFsReset`.
4. `sceSifLoadModule` for `SIO2MAN`, `PADMAN`, `MCMAN`, `MCSERV`.
5. `sceCdInit`, `sceCdMmode`.
6. `sceGsResetGraph`, `sceGsResetPath`, `sceGsSetDefDBuffDc`, `sceDmaReset`.
7. Calls `mm_InitSound`.

`mm_InitSound` (`0x0018F4A0`) calls `mm_LoadSoundModules` (`0x0018F280`), which loads `LIBSD`, `SDRDRV`, `MODHSYN`, `MODMIDI`, `MODSEIN` and `YYSOUND` with `sceSifLoadModule`, printing "loading xxx.irx failed" on error. It then binds the game's sound RPC with `sceSifBindRpc`.

`mm_OpenDiscFile` (`0x0019ED60`) resolves files with `sceCdSearchFile` and falls back to `sceOpen`/`sceLseek` ("Cannot open '%s'(sceCdSearchFile)").

## Game flow (from Ghidra decompilation)

- `mm_Main` (`0x0012AE20`): `mm_InitSystemModules`, then builds the game object on its stack (vtable `0x001BCE60`) and enters `mm_MainLoop` (`0x001854E0`), which never returns.
- `mm_MainLoop`, every frame: pad update, file pump (`mm_FsPump`), memory card queue pump (`mm_McQueuePump`), sound paths, `sceGsSyncPath`, then the game object's `+0xC` method `mm_GameUpdate` (`0x0012ABA0`), then double-buffer swap of a 640x448 interlaced display (640x224 fields).
- Display: interlaced, SMODE2 = 3 (one 640x224 buffer per field, read every line). Two double buffers exist:
  - `0x001F5790`, swapped by `mm_MainLoop` once per game frame (`sceGsSetHalfOffset` = `0x00103730` and `0x00103998` on the draw environments, then `sceGsSwapDBuffDc`).
  - `0x00717DC0`, swapped by the VSync interrupt handler `0x0019C4C0` on every field, picking the buffer from the GS CSR FIELD bit. The handler then DMA-sends a prebuilt display list for that field: `FUN_001A0980(0x00717DA0)` returns the current frame packet, and the two field packets sit `0x1C900` bytes apart (`+0x40` and `+0x1C940`). The city uses this path, so the same scene packet is re-sent (VIF1, VU1 and GS work included) on every field while the game logic runs much slower.
  - Buffer layout from `sceGsSetDefDBuffDc` (HLE in the runtime): frame buffers at pages `0x0` and `0x46`, Z buffer at `0x8C`. The odd-field buffer is drawn with OFY `0x7908` (half a line lower than `0x7900`).
- `mm_GameUpdate` is a state machine at `object+4`: 0/1 create scene A, 2 run scene A, 3/7 create scene B, 4 run scene B, 5/6 create and run scene C. Scene A is the boot/title flow; its sub-state lives at `sceneA+0xC0`:
  0 load title assets (`DATA\A3D\INTRO\*`, ground/water textures) and start `TITLE.SQ/HD/BD`; 1 memory card check; 2-5 logos and title; 6 title menu; 9/10 New Game / Continue (calls the city init functions, with a loading gauge); 11/12 hand over to scene B.
- Memory card access goes through a 64-entry async command queue at `0x001F5CF0` (`mm_McQueue*`), pumped once per frame with non-blocking `sceMcSync`. The card manager (`mm_McManagerUpdate`) lists each card one entry per call with `sceMcGetDir(port, "*", mode = 0, 1, 2, ...)` and looks for the game's save directory and its `icon.sys` (964 bytes).
- Pad: `mm_PadUpdate` turns the pad's button bytes into `(data[2] << 8 | data[3]) ^ 0xFFFF`, so Cross = `0x0040`, Start = `0x0800`, and analog sticks add bits `0x100000`-`0x800000`.
- Files: `mm_FsOpen` (`0x00184590`) opens `cdrom0:\...` with `sceOpen`; reads are pumped asynchronously by `mm_FsPump`.

## Hand-identified functions (config/symbols.csv, source = manual)

| Address | Name | Evidence |
|---|---|---|
| `0x0011B360` | `sceCdDiskReady` | Contains "Libcdvd bind err CdDiskReady" |
| `0x001159B8` | `sceSifLoadModule` | 32-byte wrapper directly after `_sceSifLoadModule` (0x00115790), passes a stack slot for the module result; called 12 times by the sound loader with `cdrom0:\SYS\*.IRX` paths |
| `0x001159D8` | `sceSifLoadStartModule` | Sibling wrapper of `_sceSifLoadModule` that passes the caller's result pointer through |
| `0x00185740` | `mm_InitSystemModules` | References `IOPRP243.IMG`, `SIO2MAN/PADMAN/MCMAN/MCSERV.IRX`; calls the SIF, CDVD and GS init functions above |
| `0x0018F280` | `mm_LoadSoundModules` | References the six sound IRX paths and their "loading ... failed" messages |
| `0x0018F4A0` | `mm_InitSound` | Calls `mm_LoadSoundModules`, `sceSifInitRpc`, `sceSifBindRpc` ("error: sceSifBindRpc") |
| `0x0018E1B0` | `mm_SetSoundDataPaths` | References `cdrom0:\DATA\SOUND\{,HD,BD,SQ}\` |
| `0x0019DE10` | `mm_PlayMissionMovie` | References `DATA\MPEG\MISSION` and `ENDING.PSS` |
| `0x0019DFA0` | `mm_PlayOpeningMovie` | References `DATA\MPEG\OPENING.PSS` |
| `0x0019ED60` | `mm_OpenDiscFile` | See boot sequence |

Independent check: the SDK functions found from their own diagnostic strings (`sceGsSyncPath`, `sceGsPutDrawEnv`, `sceGsSetDefDispEnv`, `sceDmaSend` and the rest of libdma, `sceCdSearchFile`, `sceCdInit`, `sceCdReadClock`, `sceCdSt*`, `scePadInit`, `scePadPortOpen`, `sceMcInit`, `sceSdRemoteInit`) match the PS2Recomp analyzer's signature names at the same addresses in every case.

## Known patches from PCSX2

From `PCSX2/pcsx2_patches` (`SLUS-20377_46FFB836.pnach`, widescreen by Arapapa):
- `0x0012A92C`: X field of view constant (`lui t2, 0x3F80` to `0x3F40` for 16:9).
- `0x00185E04`: render fix (`lui v1, 0x3F80` to `0x3FB0`).
- `0x0012A42C`: zoom (optional).

## Leads for later milestones

- `ps2_analyzer` suggests 21 "potential self-modifying code" NOP patches (`0x0011AC68` ... `0x0013E238`). Checked: all 21 are ordinary `sw` stores to globals (libcdvd command state, the libmc busy flag in every `sceMc*` call, `__malloc_lock`/`__malloc_unlock`). They are flagged only because the single load segment is marked executable, so writes into `.data` look like writes into code. **Do not apply them**: they would break memory cards and malloc locking.
- `ps2_analyzer` also emits 216 instruction-to-MMIO hints and one jump table at `0x001B54F0` (89 entries). The Ghidra flow does not use them yet.
- `YYSOUND.IRX` (187 KB) is a custom sound driver, the largest IOP module; expect sound work there (M7).
- Texture format (`.TEX`): see [rootlm/metropolismaniaresearch](https://github.com/rootlm/metropolismaniaresearch) for the palettized layout.
