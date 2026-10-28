// Friend of VU1Interpreter (patches/0135): the pre-timed programs read its decoder and run on its state. Everything that
// names a private member of the interpreter is a member of this struct (vu1aot_graph.cpp, vu1aot_exec.cpp,
// vu1aot_runtime.cpp).

#pragma once

#include "vu1aot.h"
#include "vu1aot_exec.h"

#include "runtime/ps2_vu1.h"

#include <string>
#include <vector>

struct Vu1AotAccess
{
    // The decoder's view of the pair at `pc`.
    static vu1aot::PairInfo describe(const VU1Interpreter &vu, const uint8_t *code, uint32_t pc);

    // What a program needs at run time besides its nodes: the nodes in executable form, and their commits.
    struct ExecInfo
    {
        std::vector<vu1aot::XNode> nodes;
        std::vector<vu1aot::Commit> commits;
    };
    // allowNative false sends every op through the interpreter's own functions (the reference of the fuzz test).
    static void prepare(vu1aot::Graph &graph, bool allowNative = true);

    // The interpreter's own execUpper/execLower as ops, with the entries they queue moved into the program's slots.
    static void generalUpper(vu1aot::Ctx &c, const vu1aot::XNode &n);
    static void generalLower(vu1aot::Ctx &c, const vu1aot::XNode &n);
    static void harvestFlags(vu1aot::Ctx &c, uint32_t firstPos, uint32_t expected);

    // Runs a program on the interpreter's state from `args` (the run about to start: state and scheduler set up).
    // Done: the state, the data memory and the cycle counter are what the interpreter would have left, and the XGKICK packets
    // are in `packets` (not sent). Bail: the run met something the model does not cover; the caller restores its snapshot.
    static vu1aot::RunResult run(VU1Interpreter &vu, const vu1aot::Graph &graph, const VU1Interpreter::AotRun &args,
                                 std::vector<std::vector<uint8_t>> &packets);

    // One program run, for the graph executor (run) and the compiled programs (vu1aot_emit.cpp): the set-up before the first
    // pair, what happens between pairs that is not the node's own work, the end, and what is put back after.
    struct Frame
    {
        VU1Interpreter &vu;
        const VU1Interpreter::AotRun &a;
        VU1State &st;
        vu1aot::Ctx c;
        const bool *xgkickActive; // an XGKICK is copying its packet
        uint64_t startCycle;
        uint64_t cyc = 0; // cycles since the run began, up to the issue of the current pair
        int previousRounding;
        bool vuRounding;

        Frame(VU1Interpreter &vu, const VU1Interpreter::AotRun &a, std::vector<std::vector<uint8_t>> &packets);
        // The XGKICK copy goes on for `cycles` cycles. False when it stopped the interpreter (the run bails).
        bool progress(uint32_t cycles)
        {
            for (uint32_t k = 0; k < cycles; ++k)
                vu.progressXgkick();
            return !vu.m_stopRequested;
        }
        // What the interpreter's own functions look at before a pair runs through them.
        void syncForInterpreter(uint32_t pc)
        {
            st.pc = pc;
            vu.m_cycle = startCycle + cyc;
        }
        // An End node, after its end commits: drains the XGKICK and leaves what the interpreter leaves.
        vu1aot::RunResult end(uint32_t pc, uint32_t target, uint32_t endExtra, bool inSlot, uint8_t backupAfterTaken, uint8_t backupAfterFall);
        // Puts back what the interpreter's queues hold; returns `result`.
        vu1aot::RunResult finish(vu1aot::RunResult result);
    };

    // The C++ of a compiled program for a prepared, supported graph (vu1aot_emit.cpp): the constant nodes and the function
    // `name`, which does what run() does for this graph.
    static std::string emitFunction(const vu1aot::Graph &graph, const std::string &name);

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
