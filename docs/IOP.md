# The IOP side

The PS2's I/O processor runs ten modules from `SYS/` on this disc (`SIO2MAN`, `MCMAN`, `MCSERV`, `PADMAN`, `LIBSD`, `SDRDRV`,
`MODMIDI`, `MODHSYN`, `MODSEIN`, `YYSOUND`). In this port they run on `ps2xIOP`'s R3000A interpreter, with the IOP kernel
(interrupts, threads, timers, file access, SIF) provided by the runtime's own imports and an SPU2 core. Plan P5 replaces that:
`YYSOUND.IRX`, the game's own sound driver, becomes readable C++ (`src/port/iop/`), and all ten modules are translated ahead of time
into generated C++ that stays in `work/iop_aot/` (game-derived, never committed). This page records what the modules are and how
the work is verified; the plan and its progress are in the repository history.

## The ten modules

| Module | Code | Calls into the kernel through | Notes |
|---|---|---|---|
| YYSOUND | 56,416 bytes | thbase, timrman, ioman, sifcmd, sifman, sysmem, intrman, libsd, modhsyn, modmidi, modsein | The game's sound driver (Indi Software, `yysound.irx 2002.05.24`). Carries debug info. |
| MCMAN, MCSERV, PADMAN, SIO2MAN | 68,832 / 4,656 / 32,192 / 3,856 | the usual SDK libraries | Sony SDK modules, no symbols |
| LIBSD, SDRDRV, MODHSYN, MODMIDI, MODSEIN | 16,208 / 3,680 / 45,600 / 13,568 / 7,264 | | Sony sound modules, no symbols |

Together 252,272 bytes of code (63,068 instructions). `python tools/irx_inspect.py summary` prints the sections, relocations,
imports and exports of each.

## YYSOUND's debug info

`YYSOUND.IRX` was linked with its GCC 2 debug information (stabs inside an ECOFF `.mdebug` section, source file `yysound.c`).
The nine Sony modules have none. From it `tools/irx_inspect.py` reads:

- the 114 functions with name, size, return type, parameter names and types, locals, and the source line of every statement
  (`yysound.c` lines 534 to 5266, about 4,700 lines of C);
- the layout of every type the source declares (the command structures `ST_COMMAND_LIST`, `ST_WAVE_PLAY`, `ST_MIDI_TRACK` and so
  on, the Sony API structures it uses);
- the 49 static variables with their types and addresses (`mCommandList` is 17 `ST_COMMAND_LIST`, `midiEnv` 16 `sceMidiEnv`, ...).

What is written, and where:

| File | Tracked | Content |
|---|---|---|
| `config/iop/yysound_functions.csv` | yes | offset in the module, name, size, source lines, C++ signature |
| `config/iop/yysound_globals.csv` | yes | offset, name, size, section, type |
| `config/iop/yysound_types.h` | yes | the struct, union and enum layouts, with `static_assert`s for every size and offset (pointers are 32 bit IOP addresses, `IopPtr<T>`) |
| `work/iop/ghidra_yysound/` | no | the relocated flat image, symbols, plain C types, prototypes and data types for Ghidra, and the decompilation of every function |

The relocated image is built the way the emulator's loader builds it (`IopModuleLoader`): the entry points it gives agree with the
emulator's load log (YYSOUND at 0x5E300, entry 0x6BBB8; LIBSD at 0x43C00, entry 0x43CA0; MCMAN at 0x11000, entry 0x11168), no
HI16 relocation is left unpaired and every `jal` lands in the module's code.

To read the code: `python tools/irx_inspect.py ghidra original/SYS/YYSOUND.IRX --base 0x5E300`, then Ghidra's `analyzeHeadless`
with `-loader BinaryLoader -loader-baseAddr 0x5E300 -processor MIPS:LE:32:default` on the flat image and the scripts
`ApplySymbols.java`, `ApplyTypes.java`, `ApplyDataTypes.java` and `DecompileAll.java` from `tools/ghidra/` (a separate project,
`work/ghidra_iop`, because the GhidraMCP server serves one program at a time). `python tools/iop_callgraph.py yysound` writes the call
graph and a leaf-first order.

## How YYSOUND runs

- `start` (the module entry, called at load with no IOP thread) calls `Init`, which sets up the module contexts (`set_module_context`,
  `set_midi`, `set_hsyn`, `set_sein`, `set_spu2`, `BuffInit`) and then starts three threads and two hard timers:
  - the **timer thread** (`atick`, priority 0x1D): `SleepThread()` at the head of an endless loop; a hard timer's handler
    (`timer_handler`, interrupt context) wakes it with `iWakeupThread`; the loop body runs the voice, MIDI and wave idle functions,
    the command list, and ticks `modsein`, `modmidi` and `modhsyn`;
  - the **load thread** (`Load_atick`, priority 0x1F): the same shape with a second timer, running the file-loading commands;
  - the **EE thread** (`sce_ee_loop`, priority 0x1E): registers the RPC server 0x12346 with `soundFunc` and enters `sceSifRpcLoop`,
    which the runtime handles; `soundFunc` queues the EE's commands (`AddRequest`).
- Every effect on the hardware goes through an import (it uses no hardware address itself), so a native version needs only the
  56 imports: libsd 6, modhsyn 9, modmidi 9, modsein 3, sysmem 3, intrman 2, ioman 4, sifcmd 4, sifman 1, stdio 1, sysclib 1,
  thbase 7, timrman 6.
- The only blocking calls in its own code are the two `SleepThread()` at the heads of the thread loops, which is what lets the port
  run each thread body as a step that is entered when the thread wakes (the same machine-level behavior, no stack to keep).

## The readable port of YYSOUND

`src/port/iop/yysound.cpp` is the driver as C++ with its own names, parameters and structures. Each function is registered with
`IOP_FN(yysound, Name)` and replaces the original at its address (`patches/0139-iop-native-hook.patch`):

- The emulator keeps a table of replacements by address, checked in `step()` next to the import stubs. A module's replacements take
  effect only when it loads with the image whose hash is in `config/iop/yysound_functions.h` (the hash of the relocated image, which
  `tools/irx_inspect.py` computes and the emulator checks before the module starts), so a different build of the module can never run
  against these offsets. The loaded image was compared byte for byte with the one `irx_inspect.py flat` builds: identical.
- A replacement is plain C++ over the module's RAM: the structures of `yysound_types.h` and the variables of `yysound_globals.h` are
  laid over the IOP's 2 MB (`IopPtr<T>`, `iop::Var<T>`, `iop::ArrayVar<T, N>`). An import is its stub in the module's import table, run as
  guest code (`callImport`); a function that is not ported yet is called the same way (`call`). A local whose address goes to guest code
  sits in a `Frame` of the original's size, so the same stale stack bytes are under it.
- `PS2X_IOP_NATIVE=0` turns all replacements off, a comma separated list of names turns on only those.

### Verifying a replacement

`PS2X_IOP_VERIFY=1` (the first `PS2X_IOP_VERIFY_FIRST` calls of each replacement, default 64, then every `PS2X_IOP_VERIFY_EVERY`-th, 256):

1. the original runs first, as the interpreter runs it, for real, with RAM before and after kept; the imports the module's code calls
   happen once and are logged (arguments, results, and the RAM each one changed);
2. RAM goes back, the replacement runs, and the module's imports are answered from the log (a different import, other arguments or a
   different order is a difference);
3. the two are compared: all of RAM, the registers the ABI keeps (v0 only for a function that returns something, s0 to s7, gp, sp, fp,
   ra), where the function returns, and the import sequence. The stack below the entry stack pointer and the 16 bytes of argument home
   area above it are dead after the return and left out;
4. the original's state is what continues, so a verified run is the same run as one without replacements.

Differences are reported as `[iop:verify] MISMATCH <function> (call N); ...` and a summary per function (calls, verified, mismatches,
the original's average instruction count) every five seconds of IOP time. A function that blocks (the original yields) is counted as not
verifiable and runs as the original that time.

First gate (2026-10-27): `timer_handler` (interrupt context, one import), `ClearMidiTrack`, `ClearWaveTrack`, `AddFilePutPos`,
`AddRequestPutPos` and `CmdMidiOpen` (three `strcpy` imports) through the title, a new game and the movie start: 0 mismatches in
every verified call.
