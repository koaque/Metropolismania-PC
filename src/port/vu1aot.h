// Pre-timed VU1 programs (plan P4, M3).
//
// Every VU1 run the game makes starts with idle pipelines (execute() clears them; the game's MSCNT resumes follow an
// E-bit end that drained them) and ends drained. The cycle a pair issues at, the cycles it stalls, and the moment each
// queued result (MAC/status/clip flags, Q, P) becomes visible then depend only on the path through the program, never
// on the data. So the interpreter's scheduling can be worked out once per (program, entry PC) as a graph of nodes
// (pc, timing state): each node has its stall and the results that commit before it, and a run only has to follow the
// graph and do the arithmetic. This file is the model and the graph builder (M3.1); the arithmetic is vu1aot_ops.h and
// the run is vu1aot_exec.cpp.
//
// The model mirrors VU1Interpreter::run, calculatePairReadyCycle, markPairWrites and commitReadyPipelines with the
// interpreter's own decoder (Vu1AotAccess is its friend), and is checked against the interpreter's pair trace.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class VU1Interpreter;

namespace vu1aot
{
    // The interpreter's Pipeline enum for the lower half.
    enum LowerPipe : uint8_t
    {
        PipeNone = 0,
        PipeFmac,
        PipeLsu,
        PipeFdiv,
        PipeEfu,
        PipeIalu,
        PipeBranch,
        PipeXgkick
    };

    // What the model needs of one instruction pair, out of VU1Interpreter's decoder.
    struct PairInfo
    {
        uint32_t lower = 0, upper = 0;
        bool iBit = false, eBit = false, dBit = false, tBit = false;
        bool reserved = false;
        bool lowerNop = false;
        bool waitQ = false, waitP = false, delaysNextBranchRead = false;
        uint8_t lowerPipe = PipeNone;
        uint8_t lowerLatency = 0;
        uint16_t lowerViWrite = 0; // VI registers the lower half writes (usage, VI0 included)
        uint8_t vfReadSlots[16]{}; // reg * 4 + component
        uint8_t vfReadSlotCount = 0;
        uint16_t viReadMask = 0; // VI0 left out
        uint8_t accReadMask = 0; // bit = component
        uint8_t lowerWriteSlots[4]{};
        uint8_t lowerWriteCount = 0, lowerWriteLatency = 0;
        uint8_t upperWriteSlots[4]{};
        uint8_t upperWriteCount = 0, upperWriteLatency = 0;
        uint16_t viWriteMask = 0; // VI0 left out
        uint8_t viWriteLatency = 0;
        uint8_t accWriteMask = 0; // bit = component
        uint8_t upperVfShadowReg = 0, suppressedLowerVf = 0;
    };

    PairInfo describePair(const VU1Interpreter &vu, const uint8_t *code, uint32_t pc);

    constexpr uint32_t kFmacLatency = 4;
    constexpr uint32_t kMaxFlags = 8;
    constexpr uint32_t kEfuSlots = 2;

    // A queued flag result. src: where its value comes from when it commits.
    enum FlagSrc : uint8_t
    {
        FlagFmac = 0, // the MAC and status of an arithmetic upper op
        FlagClip,     // a CLIP result
        FlagFsset,    // an FSSET immediate
        FlagFcset     // an FCSET immediate
    };
    enum FlagWrites : uint8_t
    {
        WritesMac = 1,
        WritesStatus = 2,
        WritesSticky = 4,
        WritesClip = 8
    };
    struct FlagEntry
    {
        uint8_t ready = 0; // cycles until it commits
        uint8_t src = 0;
        uint8_t writes = 0;
    };

    // The scheduler state at a pair boundary, as cycles until each thing is ready (0 = ready now).
    struct TimingState
    {
        uint8_t vf[32][4]{};
        uint8_t vi[16]{};
        uint8_t acc[4]{};
        uint8_t drain = 0;   // cycles until every vf, ACC and VI write has landed
        uint8_t efuRes = 0;  // cycles until the EFU takes another operation
        uint8_t fdiv = 0;    // cycles until Q commits (0 = no DIV in flight)
        uint8_t efu[kEfuSlots]{}; // cycles until P commits, per slot (0 = free)
        uint8_t flagCount = 0;
        FlagEntry flags[kMaxFlags]{};
        uint8_t branchBackupReg = 0; // the VI whose previous value a branch in the next pair reads (0 = none)
    };

    // A result that commits between two pairs.
    enum CommitKind : uint8_t
    {
        CommitFlag = 0, // the oldest queued flag result
        CommitQ,
        CommitP         // slot in `slot`
    };
    struct Commit
    {
        uint8_t kind = 0;
        uint8_t slot = 0;
        uint8_t src = 0;    // flags: FlagSrc
        uint8_t writes = 0; // flags: FlagWrites
    };

    enum class NodeKind : uint8_t
    {
        Plain,     // continues at pc + 8
        Branch,    // sets up a branch; the next node is its delay slot
        DelaySlot, // the pair after a branch: continues at `target` (`nextTaken`, or `next` if always) if taken, else pc + 8 (`next`)
        End,       // the run ends after this pair
        Bail       // the model cannot time what follows: a run that gets here is rolled back and left to the interpreter
    };

    struct Node
    {
        uint16_t pc = 0;
        NodeKind kind = NodeKind::Plain;
        bool entry = false;
        bool xgkick = false;      // the lower half is XGKICK
        bool backward = false;    // DelaySlot: the branch goes to a lower or equal pc (a loop edge)
        bool always = false;      // DelaySlot: the branch is always taken (B, BAL): the only successor is `next`
        uint16_t advance = 0;     // cycles from the previous pair's issue to this one's (1 + stall; the entry has stall only)
        uint16_t stall = 0;
        uint16_t target = 0;      // Branch: where it goes; DelaySlot and End: where the branch before it went
        bool inDelaySlot = false; // this pair is a delay slot (so `target` is meaningful)
        uint8_t readBackupReg = 0; // a branch here reads this VI as it was before the previous pair wrote it (0 = none)
        uint8_t backupAfterTaken = 0, backupAfterFall = 0; // the same, set by this pair for the next
        std::vector<Commit> commits; // before the pair issues, in order
        // Flag results pushed by this pair (in order): src of each
        std::vector<FlagEntry> pushes;
        int8_t fdivLatency = 0;   // DIV/SQRT/RSQRT: cycles until Q commits
        int8_t efuSlot = -1;      // EFU op: the slot taken
        uint8_t efuLatency = 0;
        int next = -1, nextTaken = -1;
        // End
        uint16_t endExtra = 0;       // cycles from this pair's issue until everything has drained
        std::vector<Commit> endCommits;
        PairInfo pair;
    };

    struct Graph
    {
        std::vector<Node> nodes;
        int entry = -1;
        uint32_t entryPc = 0;
        uint32_t codeLo = 0, codeHi = 0; // byte range of the pairs reached
        bool supported = true;   // false when the entry itself cannot be timed
        uint32_t bailNodes = 0;  // paths cut short (see Bail)
        std::string whyNot;      // the first reason a path was cut
    };

    // Explores the program from `entryPc` with idle pipelines. A path the model cannot time exactly (a reserved
    // instruction, D/T bits, a register jump, a branch in a delay slot, a full queue, running off the code, too many nodes)
    // ends in a Bail node; the data decides at run time whether the run takes it.
    Graph buildGraph(const VU1Interpreter &decoder, const uint8_t *code, uint32_t codeSize, uint32_t entryPc);

    // The interpreter's pair trace (pc and issue cycle of every pair, from the cycle the run began) against the graph.
    // Returns an empty string when the graph reproduces it, else the first difference.
    // `drainedAt`: the cycle the run ended at (cycles after - cycles before).
    // `xgkickBytes`: sizes of the packets XGKICK sent (their copy time can extend the end).
    std::string checkTrace(const Graph &graph, const std::vector<std::pair<uint32_t, uint64_t>> &trace, uint64_t startCycle,
                           uint64_t drainedAt, const std::vector<uint32_t> &xgkickBytes);
}
