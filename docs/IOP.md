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
  Guest code it calls can block too: `sceHSyn_VoiceTrans` (the transfer of sound data to SPU memory) sleeps the load thread until the
  DMA is done, inside `OpenFileSPU` and `RunCopyJointSPU`, under `RunMidiOpen`, `RunWaveOpen` and `RunCommand`. Those run on a fiber
  (below).

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

### Threads and blocking

- A thread body is a pair of functions: the entry (`atick`, `Load_atick`, `sce_ee_loop`) does what the original's prologue and first
  statements do and ends by jumping to an import stub with the return address the original's `jal` would have set (`SleepThread`, or
  `sceSifRpcLoop` which never returns); the second (`atick_resume`, `Load_atick_resume`) is registered at the address after that `jal`
  and is entered when the thread is woken, runs one pass of the loop body and sleeps again the same way. Both are registered with
  `IOP_FN_THREAD(module, name, stub, blocking)`, and the verifier compares them at the stub.
- A function registered as blocking (`IOP_FN_BLOCKING`, or the `blocking` flag of a thread body) runs on a host fiber, one per IOP
  thread, when the IOP thread's own cpu reaches it. If guest code it called puts the thread to sleep, the emulator parks the fiber
  with the function's C++ frames as they are, runs other threads, and switches back into the fiber when the thread is ready, which
  carries on where the guest code slept. The call stacks of the emulator's synchronous calls come from a pool, so a parked call keeps
  its own. A blocking function called from the interpreter's slice of another native (not from the thread's cpu) runs where it is;
  one that then blocks is an error (the emulator logs it and aborts).
- Verifying a blocking function: when the original blocks, the call is counted as not verifiable and runs as the original that time.

### How far the verifier reaches, and what it cannot compare

- **Coverage** (`PS2X_IOP_COVERAGE=<file>` with `PS2X_IOP_VERIFY=1`, `tools/iop_coverage.py`): the words of YYSOUND the original executed in
  verified calls, merged over several runs, per function. `PS2X_IOP_COVERAGE_ALL=<file>` records every word that runs at all (use it
  with `PS2X_IOP_NATIVE=0` to see what the game reaches). `config/iop/port_status.csv` is the result: all 114 functions are ported;
  across the movie, explore and probe runs the verifier saw 85% of the words of the original execute (11,873 of 13,897), with 0 mismatches.
  Never reached: `repeatCB` (a MIDI loop callback), `MidiLoop` (a MIDI song that ends) and `ReportFileRequestList` (a debug print); 24
  functions are executed in full and the rest in part (error paths and rare branches are the unreached words).
- **Whole run, every call** (`PS2X_IOP_VERIFY=1 PS2X_IOP_VERIFY_ALL=1`): through the title, a new game, the movie and the entry to the city,
  34,198 calls of `timer_handler`, 22,798 of `atick_resume`, 11,364 of `Load_atick_resume` (35 more block in the original on an SPU transfer
  and run as the original) and 103 of `soundFunc` were each compared with the original: 0 differences. That run is the original's run, call
  for call (the original's state is what continues), which an audio comparison cannot show: two captures of the SPU2 mix (`PS2X_SPU2_WAV`),
  one with `PS2X_IOP_NATIVE=0` and one with the port, have the same loudness over time but not the same samples, because a replacement
  charges the average instruction count of the original, not the exact one.
- **Probes** (`PS2X_IOP_PROBE=<file>`, `tools/iop_probe.py`): commands injected into `soundFunc` as if the EE had sent them, for the paths
  the game does not take in the scripted runs (spoken text, wave stop, pitch, tempo, joint files, the track queries, every error, the
  shutdown). With `PS2X_IOP_VERIFY_ALL=atick_resume,Load_atick_resume,soundFunc` every call is verified from the first injection to three
  seconds after the last. `PS2X_IOP_TRACE_NATIVE=soundFunc` logs the real traffic (arguments and the first 96 bytes of the data).
- **Cost table** (`tools/iop_costs.py` makes `config/iop/yysound_costs.h` from verify logs): the instructions the original takes in each
  function entered from guest code, imports not counted (a replacement runs the imports itself), averaged over the verified calls. The port
  charges that much IOP time, so timers and DMA completions keep the schedule the original gave them (it is an average: the original's
  count depends on the data).
- **What cannot be compared.** The original's functions push registers and keep locals on the stack, so the bytes a call leaves in dead
  stack are not the ones a replacement leaves. Where the original reads a local it never set, it copies those bytes, and the two
  differ. Found by the probes: `WaveLoop` and `VoiceIdle` build a play request from a local whose `jno` is never set (only a joint file looks at
  it), and `CmdWavePlay` builds its volume and pitch requests from locals whose `id` is never set (it lands in `ee_put[4]` and in the request,
  where the EE's check id is compared with it). The port reads the same stale stack but cannot have the same bytes under it, so these
  fields are left out of the comparison of `atick_resume` (`IopSubsystem::registerVerifyIgnore`), and only there: `soundFunc`, whose callers
  leave the same stack, is compared in full.
- Two verifier lessons: a call's imports are replayed from the original's run, with the RAM each changed taken in whole words (an import
  writes words, and a stale neighbor byte in the replacement would otherwise change the word); and every function that calls one that
  keeps locals on the stack, or passes stack arguments, needs a `Frame` of the original's size, or the callee's locals sit at other
  addresses (the import arguments then differ).

## The translated modules (P5.3)

All ten modules (SIO2MAN, MCMAN, MCSERV, PADMAN, LIBSD, SDRDRV, MODMIDI, MODHSYN, MODSEIN, YYSOUND: 60,861 reachable instructions) are
translated ahead of time into C++ that runs them natively, behaving exactly like the interpreter in `core/iop_cpu.cpp`.

- **Making it**: `python tools/pipeline.py iop-aot` (`tools/iop_aot.py`) reads the IRX files in `original/SYS`, relocates each at the
  address the game loads it at, finds its code by reachability (the entry, the exports, every code address a relocation makes into a
  value, and the branch targets), and writes `work/iop_aot/<MODULE>.cpp` (21 MB in all). It is machine-translated game code, so it is
  never committed (AGENTS.md rule 8); `pipeline.py stage` copies it into the runtime tree and `pipeline.py build` compiles it. The
  generated code includes only `ps2x/iop/iop_aot.h`; the emulator side is in `patches/0139-iop-native-hook.patch`.
- **How it matches the interpreter**: a block is the code from a leader to the next branch or jump with its delay slot, written as a C++
  function of straight-line code in which every instruction is a label, so a run can end and resume at any instruction. After each
  instruction the code counts it and, when the budget is used up, writes the exact processor state: the pc, a branch waiting for its
  delay slot, a load waiting to land. The budget is the smaller of the slice (256 instructions) and the cycles to the next due event
  (a DMA interrupt or a guest callback), so slices, event service and thread switches fall at the instruction the interpreter stops at.
  An instruction that needs the emulator's per-step work after it (a hardware access, a write to the status or cause register)
  ends the run. A native replacement of the YYSOUND port is still taken at its address (a mask of the words that have one).
  The emulator arms a module's translation only when the loaded image has the hash and address it was translated for (a different disc
  leaves the interpreter on, with a log line); `PS2X_IOP_AOT=0` turns the translation off.
- **Checking it**:
  - `PS2X_IOP_VERIFY=1` also checks translated runs (the first runs of a module and every `PS2X_IOP_VERIFY_EVERY`-th, or all of them in
    a `PS2X_IOP_VERIFY_ALL` window): the same run is done again by the interpreter from the same state and the processor, all of RAM and
    the ownership flags are compared; the interpreter's state continues. A run that touched hardware cannot be repeated (the devices
    have changed): it is counted as not verifiable.
  - `PS2X_IOP_TRACE_HASH=<ms>` logs, every n milliseconds of IOP time, the cycle and instruction counts and a hash of RAM, the ownership
    flags and every thread's processor state. Two runs of a scenario that is deterministic (the title screen left alone; once the movie
    is playing two interpreter runs differ too) give the same lines if, and only if, the execution is the same: with the translation
    on and off, 235 of 235 lines are identical, with all ten modules translated, and the SPU2 mix captured with `PS2X_SPU2_WAV` is
    byte-identical over 11 MB (58 seconds of stereo). This is what checks the hardware code (LIBSD, SDRDRV, SIO2MAN, MCMAN, PADMAN)
    that the run verifier cannot repeat. It found the one place where the translation differed: a DMA a hardware store starts is timed
    from the cycle before the store is counted.
  - `tools/iop_aot_fuzz.py` (harness in `tools/iop_aot_fuzz/`): random programs with every instruction form (traps, delay slots, loads and
    stores with misaligned and hardware addresses) translated and run from random registers in runs of random length, the interpreter
    stepped the same number of instructions after each run, the whole state compared: 150 programs of 400 trials agree. A random
    program that stores into its own code is out of scope (the translation is of the code as it was) and ends the trial.
  - `PS2X_IOP_AOT_STATS=1` counts, per module, the instructions that ran translated and those the interpreter ran inside the module
    (the gate: 0). Over the title, new game, movie and city entry, and the explore and probe runs, no instruction of the ten modules
    is interpreted. `python tools/generated_audit.py --iop-log <runner log>` prints the table from such a log and fails when a
    module interpreted anything or a verified run differed.
- **What it does not do**: code that writes into its own text (none of the modules does) would run as it was translated; an
  instruction at an address the static pass did not reach is not translated, and the gate shows it.

