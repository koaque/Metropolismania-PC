// Compiled VU1 programs (plan P8 step 4).
//
// The graph executor (vu1aot_exec.cpp) follows a program's node graph through tables of op pointers. A compiled program is
// the same graph written out as C++ by vu1aot_emit.cpp, one labelled block per node: the ops are called directly on constant
// nodes, so the compiler folds the register numbers, lane masks and slots, and leaves out flag results that are never read.
// It does exactly what the executor does for that graph, step for step.
//
// The C++ is written from the game's VU1 microcode, so it is game-derived: `pipeline.py vu1-emit` writes it to
// work/vu1_aot/gen/ (MMPC_VU1_EMIT=<dir> on any run), `pipeline.py stage` builds it in, and it is never committed. Each file
// registers its program at start-up. The runtime uses one only for a graph it has built itself with the same entry, the same
// code bytes, and the same emitted text (the text hash), so a change to the graph builder, the op choice or the emitter
// makes an old file unused rather than wrong. PS2X_VU1_COMPILED=0 runs every program in the executor instead.

#pragma once

#include "vu1aot.h"
#include "vu1aot_access.h"
#include "vu1aot_exec.h"
#include "vu1aot_ops.h"

#include "runtime/ps2_vu1.h"

#include <bit>
#include <cstdint>
#include <string>
#include <vector>

namespace vu1aot
{
    using CompiledRun = RunResult (*)(VU1Interpreter &vu, const VU1Interpreter::AotRun &args, std::vector<std::vector<uint8_t>> &packets);

    struct CompiledProgram
    {
        uint32_t entryPc;
        uint32_t codeLo, codeHi; // Graph::codeLo, codeHi
        uint64_t codeHash;       // of the code bytes from codeLo to codeHi, as the runtime hashes them
        uint64_t sourceHash;     // textHash of the text emitFunction wrote
        CompiledRun run;
    };

    struct CompiledRegistrar
    {
        explicit CompiledRegistrar(const CompiledProgram &program);
    };
    const std::vector<CompiledProgram> &compiledPrograms();

    // FNV-1a, 64 bits.
    uint64_t textHash(const std::string &text);
    // The function name of the compiled program for an entry and a code hash.
    std::string compiledName(uint32_t entryPc, uint64_t codeHash);
    // A whole file for a prepared, supported graph: the program, and its registration. Written to `dir` unless the file is
    // already there with the same text.
    void emitProgramFile(const Graph &graph, uint64_t codeHash, const std::string &dir);
}
