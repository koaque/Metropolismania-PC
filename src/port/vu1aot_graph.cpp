// Pre-timed VU1 programs: the timing model and the graph builder (plan P4, M3.1). See vu1aot.h.

#include "vu1aot.h"
#include "vu1aot_access.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <string>

vu1aot::PairInfo Vu1AotAccess::describe(const VU1Interpreter &vu, const uint8_t *code, uint32_t pc)
{
    static_assert(vu1aot::kFmacLatency == VU1Interpreter::kFmacLatency);
    static_assert(vu1aot::kMaxFlags == VU1Interpreter::kMaxFlagEntries);
    static_assert(vu1aot::PipeFmac == VU1Interpreter::PipelineFmac && vu1aot::PipeFdiv == VU1Interpreter::PipelineFdiv &&
                  vu1aot::PipeEfu == VU1Interpreter::PipelineEfu && vu1aot::PipeBranch == VU1Interpreter::PipelineBranch &&
                  vu1aot::PipeXgkick == VU1Interpreter::PipelineXgkick);
    static_assert(vu1aot::kEfuSlots == 2);
    const VU1Interpreter::DecodedInstructionPair p = vu.decodeInstructionPair(code, pc);
    vu1aot::PairInfo info;
    info.lower = p.lower;
    info.upper = p.upper;
    info.iBit = p.iBit;
    info.eBit = p.eBit;
    info.dBit = p.dBit;
    info.tBit = p.tBit;
    info.reserved = p.upperUsage.reserved || p.lowerUsage.reserved;
    info.lowerNop = p.lowerNop;
    info.waitQ = p.lowerUsage.waitQ;
    info.waitP = p.lowerUsage.waitP;
    info.delaysNextBranchRead = p.lowerUsage.delaysNextBranchRead;
    info.lowerPipe = static_cast<uint8_t>(p.lowerUsage.pipeline);
    info.lowerLatency = p.lowerUsage.latency;
    info.lowerViWrite = p.lowerUsage.viWrite;
    std::copy(p.vfReadSlots.begin(), p.vfReadSlots.end(), info.vfReadSlots);
    info.vfReadSlotCount = p.vfReadSlotCount;
    info.viReadMask = p.viReadMask;
    info.accReadMask = p.accReadMask;
    std::copy(p.lowerWriteSlots.begin(), p.lowerWriteSlots.end(), info.lowerWriteSlots);
    info.lowerWriteCount = p.lowerWriteCount;
    info.lowerWriteLatency = p.lowerWriteLatency;
    std::copy(p.upperWriteSlots.begin(), p.upperWriteSlots.end(), info.upperWriteSlots);
    info.upperWriteCount = p.upperWriteCount;
    info.upperWriteLatency = p.upperWriteLatency;
    info.viWriteMask = p.viWriteMask;
    info.viWriteLatency = p.viWriteLatency;
    info.accWriteMask = p.accWriteMask;
    info.upperVfShadowReg = p.upperVfShadowReg;
    info.suppressedLowerVf = p.suppressedLowerVf;
    return info;
}

namespace vu1aot
{
    PairInfo describePair(const VU1Interpreter &vu, const uint8_t *code, uint32_t pc)
    {
        return Vu1AotAccess::describe(vu, code, pc);
    }

    namespace
    {
        constexpr uint8_t kCtrlDelaySlot = 1; // the pair is the delay slot of a branch (a target is set)
        constexpr uint8_t kCtrlEnd = 2;       // the pair before it had the E bit: the run ends after it
        constexpr uint8_t kCtrlEntry = 4;     // the first pair of the run
        constexpr uint8_t kCtrlAlways = 8;    // the delay slot of a branch that is always taken (B, BAL)
        constexpr uint32_t kMaxNodes = 40000;
        constexpr uint32_t kPcMask = 0x3FFFu;

        uint32_t laneBit(uint32_t component) { return 8u >> component; }

        // Which upper ops queue an arithmetic flag result (MAC and status): the ones execUpper runs through applyFmacDest and
        // applyFmacDestAcc, with a non-empty dest.
        bool upperPushesFmacFlags(uint32_t upper)
        {
            const uint32_t op = upper & 0x3Fu;
            const uint32_t dest = (upper >> 21) & 0xFu;
            if (dest == 0u)
                return false;
            if (op < 0x3Cu)
            {
                if (op <= 0x0Fu)
                    return true;
                if (op >= 0x18u && op <= 0x1Cu)
                    return true;
                if (op == 0x1Eu)
                    return true;
                if (op >= 0x20u && op <= 0x2Au)
                    return true;
                return op == 0x2Cu || op == 0x2Du || op == 0x2Eu;
            }
            const uint32_t special = (upper & 3u) | ((upper >> 4) & 0x7Cu);
            if (special <= 0x0Fu)
                return true;
            if (special >= 0x18u && special <= 0x1Cu)
                return true;
            if (special == 0x1Eu)
                return true;
            if (special >= 0x20u && special <= 0x2Au)
                return true;
            return special >= 0x2Cu && special <= 0x2Eu;
        }

        bool upperIsClip(uint32_t upper)
        {
            return (upper & 0x3Fu) >= 0x3Cu && ((upper & 3u) | ((upper >> 4) & 0x7Cu)) == 0x1Fu;
        }

        // The lower half: the primary opcode, and for the special group the selector the interpreter uses.
        uint32_t lowerPrimary(const PairInfo &p) { return (p.lower >> 25) & 0x7Fu; }
        uint32_t lowerSpecial(const PairInfo &p)
        {
            if (lowerPrimary(p) != 0x40u || (p.lower & 0x3Fu) < 0x3Cu)
                return 0xFFFFu;
            return (p.lower & 3u) | ((p.lower >> 4) & 0x7Cu);
        }

        bool lowerIsBranch(const PairInfo &p)
        {
            if (p.lowerNop)
                return false;
            switch (lowerPrimary(p))
            {
            case 0x20: case 0x21: case 0x28: case 0x29: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
                return true;
            default:
                return false;
            }
        }

        bool lowerIsJump(const PairInfo &p)
        {
            return !p.lowerNop && (lowerPrimary(p) == 0x24u || lowerPrimary(p) == 0x25u);
        }

        bool lowerIsUnconditionalBranch(const PairInfo &p) { return lowerPrimary(p) == 0x20u || lowerPrimary(p) == 0x21u; }

        int imm11(uint32_t lower) { return static_cast<int>(static_cast<int32_t>(lower << 21) >> 21); }

        // Cycles to advance: everything counts down; what reaches zero commits, flags first, then Q, then P.
        void advance(TimingState &s, uint32_t cycles, std::vector<Commit> &commits)
        {
            for (uint32_t step = 0; step < cycles; ++step)
            {
                for (auto &row : s.vf)
                    for (uint8_t &v : row)
                        v = v != 0 ? v - 1 : 0;
                for (uint8_t &v : s.vi)
                    v = v != 0 ? v - 1 : 0;
                for (uint8_t &v : s.acc)
                    v = v != 0 ? v - 1 : 0;
                s.drain = s.drain != 0 ? s.drain - 1 : 0;
                s.efuRes = s.efuRes != 0 ? s.efuRes - 1 : 0;
                for (uint32_t i = 0; i < s.flagCount; ++i)
                    s.flags[i].ready = s.flags[i].ready != 0 ? s.flags[i].ready - 1 : 0;
                while (s.flagCount != 0 && s.flags[0].ready == 0)
                {
                    Commit c;
                    c.kind = CommitFlag;
                    c.src = s.flags[0].src;
                    c.writes = s.flags[0].writes;
                    commits.push_back(c);
                    for (uint32_t i = 1; i < s.flagCount; ++i)
                        s.flags[i - 1] = s.flags[i];
                    --s.flagCount;
                    s.flags[s.flagCount] = FlagEntry{};
                }
                if (s.fdiv != 0 && --s.fdiv == 0)
                {
                    Commit c;
                    c.kind = CommitQ;
                    commits.push_back(c);
                }
                for (uint32_t slot = 0; slot < kEfuSlots; ++slot)
                    if (s.efu[slot] != 0 && --s.efu[slot] == 0)
                    {
                        Commit c;
                        c.kind = CommitP;
                        c.slot = static_cast<uint8_t>(slot);
                        commits.push_back(c);
                    }
            }
        }

        // calculatePairReadyCycle, as cycles from now.
        uint32_t computeStall(const TimingState &s, const PairInfo &p)
        {
            uint32_t ready = 0;
            for (uint32_t i = 0; i < p.vfReadSlotCount; ++i)
                ready = std::max<uint32_t>(ready, s.vf[p.vfReadSlots[i] >> 2][p.vfReadSlots[i] & 3]);
            for (uint32_t reads = p.viReadMask; reads != 0; reads &= reads - 1)
                ready = std::max<uint32_t>(ready, s.vi[std::countr_zero(reads)]);
            for (uint32_t reads = p.accReadMask; reads != 0; reads &= reads - 1)
                ready = std::max<uint32_t>(ready, s.acc[std::countr_zero(reads)]);
            if (p.lowerPipe == PipeFdiv && s.fdiv != 0)
                ready = std::max<uint32_t>(ready, s.fdiv);
            if (p.lowerPipe == PipeEfu)
                ready = std::max<uint32_t>(ready, s.efuRes);
            if (p.waitQ && s.fdiv != 0)
                ready = std::max<uint32_t>(ready, s.fdiv);
            if (p.waitP)
                for (uint32_t slot = 0; slot < kEfuSlots; ++slot)
                    ready = std::max<uint32_t>(ready, s.efu[slot]);
            return ready;
        }

        uint32_t maxPending(const TimingState &s)
        {
            uint32_t most = std::max<uint32_t>(s.drain, s.fdiv);
            for (uint32_t slot = 0; slot < kEfuSlots; ++slot)
                most = std::max<uint32_t>(most, s.efu[slot]);
            for (uint32_t i = 0; i < s.flagCount; ++i)
                most = std::max<uint32_t>(most, s.flags[i].ready);
            return most;
        }

        struct Key
        {
            uint16_t pc = 0;
            uint8_t ctrl = 0;
            uint16_t target = 0;
            TimingState s;
        };

        std::string serialize(const Key &k)
        {
            std::string b;
            b.reserve(200);
            const auto put = [&](const void *data, size_t size) { b.append(static_cast<const char *>(data), size); };
            put(&k.pc, 2);
            put(&k.ctrl, 1);
            put(&k.target, 2);
            put(k.s.vf, sizeof(k.s.vf));
            put(k.s.vi, sizeof(k.s.vi));
            put(k.s.acc, sizeof(k.s.acc));
            put(&k.s.drain, 1);
            put(&k.s.efuRes, 1);
            put(&k.s.fdiv, 1);
            put(k.s.efu, sizeof(k.s.efu));
            put(&k.s.flagCount, 1);
            for (uint32_t i = 0; i < k.s.flagCount; ++i)
            {
                put(&k.s.flags[i].ready, 1);
                put(&k.s.flags[i].src, 1);
                put(&k.s.flags[i].writes, 1);
            }
            put(&k.s.branchBackupReg, 1);
            return b;
        }

        struct Builder
        {
            const VU1Interpreter &decoder;
            const uint8_t *code;
            uint32_t codeSize;
            Graph &graph;
            std::unordered_map<std::string, int> index;
            std::vector<Key> keys;
            std::vector<PairInfo> pairs;
            std::vector<uint8_t> pairKnown;

            const PairInfo &pairAt(uint32_t pc)
            {
                if (pairKnown[pc / 8] == 0)
                {
                    pairs[pc / 8] = describePair(decoder, code, pc);
                    pairKnown[pc / 8] = 1;
                }
                return pairs[pc / 8];
            }

            // A path the model cannot time becomes a Bail node: a run that reaches it is rolled back and left to the interpreter.
            void note(const std::string &why)
            {
                if (graph.whyNot.empty())
                    graph.whyNot = why;
                ++graph.bailNodes;
            }

            void bail(int id, Node &node, const std::string &why)
            {
                note(why);
                node.kind = NodeKind::Bail;
                graph.nodes[id] = node;
            }

            // The node for a key, made if it is new. A branch backup nobody can read is dropped so equal futures share a node.
            int nodeFor(Key key)
            {
                if (key.pc + 8u > codeSize)
                {
                    note("runs off the end of the code");
                    return -1;
                }
                const PairInfo &next = pairAt(key.pc);
                if (!lowerIsBranch(next))
                    key.s.branchBackupReg = 0;
                const std::string bytes = serialize(key);
                const auto found = index.find(bytes);
                if (found != index.end())
                    return found->second;
                if (graph.nodes.size() >= kMaxNodes)
                {
                    note("more than " + std::to_string(kMaxNodes) + " nodes");
                    return -1;
                }
                const int id = static_cast<int>(graph.nodes.size());
                index.emplace(bytes, id);
                graph.nodes.emplace_back();
                keys.push_back(key);
                graph.codeLo = std::min<uint32_t>(graph.codeLo, key.pc);
                graph.codeHi = std::max<uint32_t>(graph.codeHi, key.pc + 8u);
                return id;
            }

            void expand(int id)
            {
                const Key key = keys[id];
                const PairInfo info = pairAt(key.pc);
                Node node;
                node.pc = key.pc;
                node.entry = (key.ctrl & kCtrlEntry) != 0;
                node.inDelaySlot = (key.ctrl & kCtrlDelaySlot) != 0;
                node.target = key.target;
                node.pair = info;
                const bool endsHere = (key.ctrl & kCtrlEnd) != 0;

                if (info.reserved)
                    return bail(id, node, "reserved instruction at pc 0x" + hex(key.pc));
                if (info.dBit || info.tBit)
                    return bail(id, node, "D or T bit at pc 0x" + hex(key.pc));
                if (lowerIsJump(info))
                    return bail(id, node, "register jump at pc 0x" + hex(key.pc));
                const bool isBranch = lowerIsBranch(info);
                if (isBranch && (node.inDelaySlot || endsHere))
                    return bail(id, node, "branch in a delay slot or after the end at pc 0x" + hex(key.pc));

                TimingState s = key.s;
                std::vector<Commit> commits;
                advance(s, node.entry ? 0 : 1, commits);
                const uint32_t stall = computeStall(s, info);
                advance(s, stall, commits);
                node.stall = static_cast<uint16_t>(stall);
                node.advance = static_cast<uint16_t>((node.entry ? 0 : 1) + stall);
                node.commits = commits;
                node.readBackupReg = key.s.branchBackupReg;
                node.xgkick = info.lowerPipe == PipeXgkick;
                node.flagBase = s.flagCount;
                uint8_t writtenVi = 0;
                if (const uint32_t viWrites = info.lowerViWrite & 0xFFFEu; viWrites != 0)
                    writtenVi = static_cast<uint8_t>(std::countr_zero(viWrites));
                node.writtenVi = writtenVi;
                // The interpreter keeps the old value of a VI until the write lands; the program's run writes it at once. They
                // differ only for the value a branch right after this pair reads, if an earlier write to the same VI is
                // still in flight here. That case is left to the interpreter.
                if (writtenVi != 0 && info.delaysNextBranchRead && s.vi[writtenVi] != 0)
                    return bail(id, node, "VI write over one in flight at pc 0x" + hex(key.pc));

                // What the pair queues (markPairWrites and the queue functions), at this issue cycle.
                if (info.lowerWriteCount != 0)
                    for (uint32_t i = 0; i < info.lowerWriteCount; ++i)
                        s.vf[info.lowerWriteSlots[i] >> 2][info.lowerWriteSlots[i] & 3] = info.lowerWriteLatency;
                if (info.lowerWriteCount != 0)
                    s.drain = std::max<uint32_t>(s.drain, info.lowerWriteLatency);
                if (info.upperWriteCount != 0)
                {
                    for (uint32_t i = 0; i < info.upperWriteCount; ++i)
                        s.vf[info.upperWriteSlots[i] >> 2][info.upperWriteSlots[i] & 3] = info.upperWriteLatency;
                    s.drain = std::max<uint32_t>(s.drain, info.upperWriteLatency);
                }
                if (info.viWriteMask != 0)
                {
                    for (uint32_t writes = info.viWriteMask; writes != 0; writes &= writes - 1)
                        s.vi[std::countr_zero(writes)] = info.viWriteLatency;
                    s.drain = std::max<uint32_t>(s.drain, info.viWriteLatency);
                }
                if (info.accWriteMask != 0)
                {
                    for (uint32_t writes = info.accWriteMask; writes != 0; writes &= writes - 1)
                        s.acc[std::countr_zero(writes)] = 1;
                    s.drain = std::max<uint32_t>(s.drain, 1);
                }

                std::string problem;
                const auto pushFlag = [&](FlagSrc src, uint8_t writes) {
                    if (s.flagCount == kMaxFlags)
                    {
                        problem = "flag queue full at pc 0x" + hex(key.pc);
                        return;
                    }
                    FlagEntry e;
                    e.ready = kFmacLatency;
                    e.src = src;
                    e.writes = writes;
                    s.flags[s.flagCount++] = e;
                    node.pushes.push_back(e);
                };
                const uint32_t firstNew = s.flagCount;
                if (upperPushesFmacFlags(info.upper))
                    pushFlag(FlagFmac, WritesMac | WritesStatus);
                else if (upperIsClip(info.upper))
                    pushFlag(FlagClip, WritesClip);
                node.upperPushes = static_cast<uint8_t>(node.pushes.size());
                if (!info.lowerNop)
                {
                    const uint32_t primary = lowerPrimary(info);
                    if (primary == 0x15u) // FSSET: drops the status write of this cycle's other results
                    {
                        for (uint32_t i = firstNew; i < s.flagCount; ++i)
                            s.flags[i].writes &= static_cast<uint8_t>(~WritesStatus);
                        pushFlag(FlagFsset, WritesSticky);
                    }
                    else if (primary == 0x11u) // FCSET: drops the clip write of this cycle's other results
                    {
                        for (uint32_t i = firstNew; i < s.flagCount; ++i)
                            s.flags[i].writes &= static_cast<uint8_t>(~WritesClip);
                        pushFlag(FlagFcset, WritesClip);
                    }
                    node.lowerPushes = static_cast<uint8_t>(node.pushes.size() - node.upperPushes);
                    const uint32_t special = lowerSpecial(info);
                    if (special == 0x38u || special == 0x39u || special == 0x3Au)
                    {
                        node.fdivLatency = static_cast<int8_t>(special == 0x3Au ? 13 : 7);
                        s.fdiv = static_cast<uint8_t>(node.fdivLatency);
                    }
                    else if (info.lowerPipe == PipeEfu && !info.waitP)
                    {
                        int slot = -1;
                        for (uint32_t i = 0; i < kEfuSlots; ++i)
                            if (s.efu[i] == 0)
                            {
                                slot = static_cast<int>(i);
                                break;
                            }
                        if (slot < 0)
                            problem = "both EFU slots busy at pc 0x" + hex(key.pc);
                        else
                        {
                            node.efuSlot = static_cast<int8_t>(slot);
                            node.efuLatency = info.lowerLatency;
                            s.efu[slot] = info.lowerLatency;
                            s.efuRes = info.lowerLatency > 0 ? info.lowerLatency - 1 : 0;
                        }
                    }
                }
                if (!problem.empty())
                    return bail(id, node, problem);

                // The previous value of a VI written by this pair, for a branch in the next one (unless this is the delay slot of a
                // branch that was taken, where the target starts a new block).
                const uint8_t backup = (writtenVi != 0 && info.delaysNextBranchRead) ? writtenVi : 0;
                node.backupAfterFall = backup;
                node.backupAfterTaken = node.inDelaySlot ? 0 : backup;

                const uint8_t endBit = info.eBit && !endsHere ? kCtrlEnd : 0;
                const uint32_t nextPc = key.pc + 8u;
                if (endsHere)
                {
                    node.kind = NodeKind::End;
                    const uint32_t extra = std::max<uint32_t>(1u, maxPending(s));
                    node.endExtra = static_cast<uint16_t>(extra);
                    TimingState drained = s;
                    advance(drained, extra, node.endCommits);
                    graph.nodes[id] = node;
                    return;
                }
                if (isBranch)
                {
                    node.kind = NodeKind::Branch;
                    node.target = static_cast<uint16_t>((key.pc + 8u + static_cast<uint32_t>(imm11(info.lower) * 8)) & kPcMask);
                    Key ds;
                    ds.pc = static_cast<uint16_t>(nextPc);
                    ds.ctrl = static_cast<uint8_t>(kCtrlDelaySlot | endBit | (lowerIsUnconditionalBranch(info) ? kCtrlAlways : 0));
                    ds.target = node.target;
                    ds.s = s;
                    ds.s.branchBackupReg = backup; // the delay slot is the pair after the writer
                    const int n = nodeFor(ds);
                    graph.nodes[id] = node;
                    graph.nodes[id].next = n;
                    return;
                }
                if (node.inDelaySlot)
                {
                    node.kind = NodeKind::DelaySlot;
                    Key taken;
                    taken.pc = key.target;
                    taken.ctrl = endBit;
                    taken.s = s;
                    taken.s.branchBackupReg = node.backupAfterTaken;
                    node.backward = key.target <= key.pc;
                    if ((key.ctrl & kCtrlAlways) != 0)
                    {
                        node.always = true;
                        const int b = nodeFor(taken);
                        graph.nodes[id] = node;
                        graph.nodes[id].next = b;
                        return;
                    }
                    Key fall;
                    fall.pc = static_cast<uint16_t>(nextPc);
                    fall.ctrl = endBit;
                    fall.s = s;
                    fall.s.branchBackupReg = node.backupAfterFall;
                    const int a = nodeFor(fall);
                    const int b = nodeFor(taken);
                    graph.nodes[id] = node;
                    graph.nodes[id].next = a;
                    graph.nodes[id].nextTaken = b;
                    return;
                }
                node.kind = NodeKind::Plain;
                Key plain;
                plain.pc = static_cast<uint16_t>(nextPc);
                plain.ctrl = endBit;
                plain.s = s;
                plain.s.branchBackupReg = backup;
                const int n = nodeFor(plain);
                graph.nodes[id] = node;
                graph.nodes[id].next = n;
            }

            static std::string hex(uint32_t value)
            {
                char text[16];
                std::snprintf(text, sizeof(text), "%x", value);
                return text;
            }
        };
    }

    Graph buildGraph(const VU1Interpreter &decoder, const uint8_t *code, uint32_t codeSize, uint32_t entryPc)
    {
        Graph graph;
        graph.entryPc = entryPc;
        graph.codeLo = entryPc;
        graph.codeHi = entryPc;
        Builder builder{decoder, code, codeSize, graph, {}, {}, {}, {}};
        builder.pairs.resize(codeSize / 8 + 1);
        builder.pairKnown.assign(codeSize / 8 + 1, 0);
        Key start;
        start.pc = static_cast<uint16_t>(entryPc & kPcMask);
        start.ctrl = kCtrlEntry;
        graph.entry = builder.nodeFor(start);
        if (graph.entry < 0)
            graph.supported = false;
        for (size_t id = 0; id < graph.nodes.size() && graph.supported; ++id)
            builder.expand(static_cast<int>(id));
        if (graph.supported && !graph.nodes.empty() && graph.nodes[graph.entry].kind == NodeKind::Bail)
            graph.supported = false;
        return graph;
    }

    std::string checkTrace(const Graph &graph, const std::vector<std::pair<uint32_t, uint64_t>> &trace, uint64_t startCycle,
                           uint64_t drainedAt, const std::vector<uint32_t> &xgkickBytes)
    {
        if (!graph.supported)
            return "graph not supported: " + graph.whyNot;
        if (trace.empty())
            return "empty trace";
        const auto fmt = [](const char *format, auto... args) {
            char text[160];
            std::snprintf(text, sizeof(text), format, args...);
            return std::string(text);
        };
        int cur = graph.entry;
        uint64_t cycle = 0;
        uint64_t xgkickIssue = 0;
        bool kicked = false;
        for (size_t i = 0; i < trace.size(); ++i)
        {
            const Node &node = graph.nodes[cur];
            if (node.kind == NodeKind::Bail)
                return fmt("pair %zu (pc 0x%x): the model gives up here (%s)", i, node.pc, graph.whyNot.c_str());
            if (node.pc != trace[i].first)
                return fmt("pair %zu: model at pc 0x%x, interpreter at 0x%x", i, node.pc, trace[i].first);
            cycle += node.advance;
            const uint64_t seen = trace[i].second - startCycle;
            if (cycle != seen)
                return fmt("pair %zu (pc 0x%x): model issues at cycle %llu, interpreter at %llu", i, node.pc,
                           static_cast<unsigned long long>(cycle), static_cast<unsigned long long>(seen));
            if (node.xgkick)
            {
                kicked = true;
                xgkickIssue = cycle;
            }
            if (node.kind == NodeKind::End)
            {
                if (i + 1 != trace.size())
                    return fmt("pair %zu: model ends the run, interpreter goes on for %zu more pairs", i, trace.size() - i - 1);
                uint64_t end = cycle + node.endExtra;
                if (kicked && !xgkickBytes.empty())
                {
                    // The copy takes one qword per two cycles, the first at the boundary after the kick.
                    const uint64_t qwords = (xgkickBytes.back() + 15u) / 16u;
                    end = std::max<uint64_t>(end, xgkickIssue + 2u * qwords - 1u);
                }
                if (end != drainedAt)
                    return fmt("end: model drains at cycle %llu, interpreter at %llu", static_cast<unsigned long long>(end),
                               static_cast<unsigned long long>(drainedAt));
                return {};
            }
            if (i + 1 >= trace.size())
                return fmt("pair %zu (pc 0x%x): the interpreter stopped but the model has not ended", i, node.pc);
            const uint32_t nextPc = trace[i + 1].first;
            if (node.kind == NodeKind::DelaySlot && node.nextTaken >= 0 && graph.nodes[node.nextTaken].pc == nextPc &&
                graph.nodes[node.next].pc != nextPc)
                cur = node.nextTaken;
            else
                cur = node.next;
            if (cur < 0)
                return fmt("pair %zu: no successor", i);
        }
        return "trace ended before the model's End";
    }
}
