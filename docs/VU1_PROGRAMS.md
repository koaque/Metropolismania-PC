# Pre-timed VU1 programs

The city's frame time was dominated by the VU1 interpreter: about 190 host cycles for every VU cycle, most of them the cycle-accurate scheduling model (hazards, the queues that delay flag, Q, P, store, VI and vf/ACC results, XGKICK progress) rather than the arithmetic. This page describes what replaced it on the hot path, how it is checked, and what it gained. The interpreter is still there as the reference and as the fallback.

Code: `src/port/vu1aot*.cpp|h` (the programs), `src/port/vu1test.cpp` and `vu1aot_test.cpp` (the checks), `patches/0132` to `0135` and `0146` (the runtime side). Capture files and compiled programs are game-derived and stay in `work/vu1_aot/`.

## The idea

Every VU1 run the game makes starts with idle pipelines (`execute()` clears them; an MSCNT resume follows an E-bit end that drained them, and the live city run showed no resume with a busy pipeline) and ends drained. From idle, the cycle each pair issues at, the cycles it stalls, and the moment each queued result becomes visible depend only on the path through the program, never on the data. So the scheduling is worked out once per program and entry PC as a graph:

* A node is a pair at a pc in a timing state (cycles until each vf lane, VI, ACC is ready, the queued flag results, Q, P, the EFU, the VI branch quirk).
* It holds the cycles to advance before the pair issues, the results that commit before it (flags first, then Q, then P, as the interpreter's commit does), the flag, Q and P entries the pair queues, and its successors (a branch has a delay slot node that picks its way at run time).
* The timing model is the interpreter's `calculatePairReadyCycle`, `markPairWrites` and `commitReadyPipelines` over the interpreter's own usage decoder. A path it cannot time exactly (reserved instruction, D/T bits, a register jump, a branch in a delay slot, a full queue, running off the code) ends in a Bail node.

A run follows the graph and does the arithmetic. The arithmetic is the interpreter's: `src/port/vu1aot_ops.cpp` has native versions of what this game runs (written once without the queues, results landing at once, which is exact because every instruction that reads a vf, ACC or VI declares it and so waits for it), and everything else goes through `execUpper` and `execLower` themselves with the queued entries moved into the program's slots. The FMAC kernels are shared with the interpreter (`runtime/ps2_vu1_fmac.h`).

A program runs on a snapshot (state, the 16 KB data memory, a few interpreter fields). A run that reaches a Bail node, a second XGKICK while one is still copying, the cycle budget or an op the model cannot do restores the snapshot and the interpreter runs it as if nothing had happened. XGKICK packets are collected during the run and sent when it completes. The program for an entry PC is found by the hash of the code range it reaches and rebuilt when the VU1 code changes; building one takes under a millisecond.

## How it is checked

All of it against the interpreter, which stays the definition of correct:

* `pipeline.py vu1-test --aot timing`: for every record the interpreter's pair trace (pc and issue cycle of every pair) against the graph, and the end cycle including the XGKICK copy. 105 of 105 records, all 39 (code image, entry) graphs.
* `pipeline.py vu1-test --aot run`: every record replayed through the programs; state, data memory, cycles and packets must equal what the game run recorded.
* `pipeline.py vu1-test --aot fuzz --fuzz N`: random instruction pairs on random state (zeros, denormals, huge, infinite and NaN values among them) through the native ops and through the interpreter's own functions; everything is compared. 2.0 million pairs, no differences. The interpreter's FMAC executors have their own fuzz (`checkFmacExecutors`) that still runs first.
* `PS2X_VU1_VERIFY=1`: in the game, every program run is also run by the interpreter from the same snapshot and the outcomes compared (state, data memory, cycles, packets, the interpreter's clip and branch backup fields). Boot, title, a new game and the explore autoplay of a save: about a million verified runs, no mismatch, no bail.

`PS2X_VU1_AOT=0` turns the programs off. `PS2X_GS_TIMING=1` prints, every 2 seconds, the programs' counters and times (VU1 time, time handing packets to the GS, bails) and the GS front-end's time; `MMPC_FRAME_STATS=1` prints the frame rate, how long the frame kick took and how the frame intervals fall.

## What it found about the interpreter

An upper op whose result goes to vf0 clobbers it for the lower half of the same pair (hardware ignores writes to vf0; the interpreter puts vf0 back only after the pair). The programs do the same so that they stay equal to the interpreter. Worth fixing in both.

## Results

Explore autoplay in the city (`MMPC_FRAME_STATS=1`, windows whose frames take real work):

| | frame rate | frame kick |
|---|---|---|
| interpreter only | 19.0 fps on average (16.8 at worst) | 48.5 ms |
| pre-timed programs | 28.6 fps on average, 30 in lighter views | 18.3 ms |

The VU1 work alone runs at about 62 million VU cycles per second against 20 for the interpreter. In the lighter views the game now holds its 30 fps (vsync paced). In the heaviest views (about 55 million VU cycles and 60 MB of GIF data per 2 seconds) the frame is about 38 ms: roughly 17 ms of VU1, 3 ms of GS front-end and 10 ms at the end of the frame waiting for the software rasterizer to finish what the GS worker has queued. So the next limit is the CPU rasterizer, then the VU1 programs again.

(These figures were measured on city frames that the DMA chain limit fixed on 2026-10-28 had cut short; see `docs/STATUS.md` item 17. Whole frames carry about 1.7 million VU cycles each.)

## Compiled programs

Following the graph costs two indirect calls per pair plus the node bookkeeping, and with whole city frames that was 31 ms of a 47 ms frame. So each graph is now also written out as C++ (`src/port/vu1aot_emit.cpp`, `vu1aot_compiled.h`): one labelled block per node that does what the executor does at that node, in the same order, with the ops (`src/port/vu1aot_ops.h`, the same functions the executor calls) called directly on a constant node. The compiler then folds the register numbers, lane masks and slots, and drops the flag work no commit reads. Only what depends on the data stays a test: a branch taken, an XGKICK in flight, the interpreter stopping.

The C++ comes from the game's microcode, so it is game-derived and never committed: `pipeline.py vu1-emit` writes the programs the records use to `work/vu1_aot/gen/`, a game run with `--env MMPC_VU1_EMIT=<that folder>` adds the ones other scenes use, and `pipeline.py stage` builds them in. A compiled program is used only for a graph the runtime has built itself with the same entry, the same code bytes and the same emitted text (its hash is stored in the file), so a change to the graph builder, the op choice or the emitter leaves an old file unused (`stale` in the counters) rather than wrong; a program with no compiled version runs in the executor. `PS2X_VU1_COMPILED=0` runs everything in the executor.

Checked like the executor: the 105 records replay exactly through the compiled programs, and `PS2X_VU1_VERIFY=1` over 140 s of explore autoplay compared 753,605 compiled runs with the interpreter, no mismatch. 29 programs cover the city (18 from the records, 11 more from a live run).

| explore autoplay, frame skip off | VU1 rate | VU1 time per 2 s | city frame rate (lowest window) |
|---|---|---|---|
| graph executor | 66 M VU cycles/s | 1.3 s | 21.1 fps (17.5) |
| compiled programs | 136 M VU cycles/s | 0.7 s | 28.2 fps (22.3) |

The frame rate is now held by the GS: its worker thread is idle about 1% of the time, and the game thread waits 0.6 to 0.9 s of every 2 s for it.
