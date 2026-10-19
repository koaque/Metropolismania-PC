// Friend of VU1Interpreter (patches/0135): the pre-timed programs read its decoder and run on its state. Everything that
// names a private member of the interpreter is a member of this struct (vu1aot_graph.cpp, vu1aot_exec.cpp,
// vu1aot_runtime.cpp).

#pragma once

#include "vu1aot.h"

#include "runtime/ps2_vu1.h"

#include <vector>

struct Vu1AotAccess
{
    // The decoder's view of the pair at `pc`.
    static vu1aot::PairInfo describe(const VU1Interpreter &vu, const uint8_t *code, uint32_t pc);

    // What a program needs at run time besides its nodes.
    struct ExecInfo;
    static void prepare(vu1aot::Graph &graph);

    // Runs a program on the interpreter's state from `args` (the run about to start: state and scheduler set up).
    // Done: the state, the data memory and the cycle counter are what the interpreter would have left, and the XGKICK packets
    // are in `packets` (not sent). Bail: the run met something the model does not cover; the caller restores its snapshot.
    static vu1aot::RunResult run(VU1Interpreter &vu, const vu1aot::Graph &graph, const VU1Interpreter::AotRun &args,
                                 std::vector<std::vector<uint8_t>> &packets);

    // The interpreter's own run, with the packets collected instead of sent (verification).
    static void interpret(VU1Interpreter &vu, const VU1Interpreter::AotRun &args, std::vector<std::vector<uint8_t>> &packets);

    // Whether a run can be handed to a program: pipelines idle, no branch pending, no XGKICK in flight.
    static bool startsIdle(const VU1Interpreter &vu);

    // Everything a program changes outside the state struct and the data memory, to put back when it bails.
    struct Snapshot
    {
        VU1State state;
        uint64_t cycle;
        uint64_t efuResourceReady; // queueP sets it from the cycle it runs at
        uint32_t workingClip;
        int32_t viBranchBackupValue;
        uint8_t viBranchBackupReg;
        bool viBranchBackupValid;
        bool pendingHaltD, pendingHaltT, stopRequested;
        std::vector<uint8_t> data;
    };
    static void save(const VU1Interpreter &vu, const uint8_t *data, uint32_t dataSize, Snapshot &out);
    static void restore(VU1Interpreter &vu, uint8_t *data, const Snapshot &in);

    // Sends collected packets the way the interpreter does when an XGKICK finishes.
    static void emit(VU1Interpreter &vu, const std::vector<std::vector<uint8_t>> &packets);

    // Compares two outcomes of the same run (state, data, cycle, interpreter fields, packets); empty when equal.
    static std::string diff(const Snapshot &a, const std::vector<std::vector<uint8_t>> &packetsA, const Snapshot &b,
                            const std::vector<std::vector<uint8_t>> &packetsB);

    static uint64_t cycle(const VU1Interpreter &vu) { return vu.m_cycle; }
};
