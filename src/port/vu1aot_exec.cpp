// Pre-timed VU1 programs: running a graph (plan P4, M3.3). See vu1aot.h.
//
// A run follows the graph from its entry node. What the interpreter works out as it goes (hazards, the queues that delay
// flag, Q, P, store, VI and vf/ACC results) the graph already holds: how many cycles to advance before each pair, which
// results commit before it, and where a branch leads. What is left is the arithmetic, which is the interpreter's own
// (execUpper, the specialized FMAC executors, execLower) run on the interpreter's state, with the entries those queue
// picked up and put in the program's slots instead. So the state, the data memory, the cycle count and the packets come
// out as they do in the interpreter, and anything the graph does not cover (Bail) is left to the interpreter.

#include "vu1aot.h"
#include "vu1aot_access.h"

#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"

#include <algorithm>
#include <bit>
#include <cfenv>
#include <cstdio>
#include <cstring>

struct Vu1AotAccess::ExecInfo
{
    std::vector<VU1Interpreter::FmacFn> upperFast; // per node: the specialized executor of the upper half, if any
};

void Vu1AotAccess::prepare(vu1aot::Graph &graph)
{
    auto info = std::make_shared<ExecInfo>();
    info->upperFast.assign(graph.nodes.size(), nullptr);
    for (size_t i = 0; i < graph.nodes.size(); ++i)
        if (graph.nodes[i].kind != vu1aot::NodeKind::Bail)
            info->upperFast[i] = VU1Interpreter::classifyFmac(graph.nodes[i].pair.upper);
    graph.exec = info;
}

namespace
{
    constexpr uint8_t laneBit(uint32_t component) { return static_cast<uint8_t>(1u << (3u - component)); }
}

vu1aot::RunResult Vu1AotAccess::run(VU1Interpreter &vu, const vu1aot::Graph &g, const VU1Interpreter::AotRun &a,
                                    std::vector<std::vector<uint8_t>> &packets)
{
    using namespace vu1aot;
    struct FlagData
    {
        uint32_t mac, status, extra, clip;
    };

    VU1State &st = vu.m_state;
    const ExecInfo &ex = *static_cast<const ExecInfo *>(g.exec.get());
    FlagData ring[kMaxFlags];
    uint32_t head = 0; // ring position of the oldest queued flag result
    float qValue = 0.0f;
    uint32_t qStatusDi = 0;
    float pValue[kEfuSlots] = {};
    const uint64_t startCycle = vu.m_cycle;
    uint64_t cyc = 0;
    uint32_t extraCycles = 0;
    bool taken = false;
    RunResult result = RunResult::Bail;

    vu.m_activeVuData = a.data;
    vu.m_activeVuDataSize = a.dataSize;
    vu.m_activeGs = a.gs;
    vu.m_activeMemory = a.memory;
    vu.m_packetSink = &packets;
    const int previousRounding = std::fegetround();
    const bool vuRounding = std::fesetround(FE_TOWARDZERO) == 0;

    const auto applyCommit = [&](const Commit &c) {
        switch (c.kind)
        {
        case CommitFlag:
        {
            const FlagData &d = ring[head & (kMaxFlags - 1)];
            ++head;
            if ((c.writes & WritesMac) != 0)
                st.mac = d.mac;
            if ((c.writes & WritesStatus) != 0)
            {
                const uint32_t current = d.status & 0xFu;
                st.status = (st.status & 0xFF0u) | current | ((current | d.extra) << 6);
            }
            if ((c.writes & WritesSticky) != 0)
                st.status = (st.status & 0x03Fu) | (d.status & 0xFC0u);
            if ((c.writes & WritesClip) != 0)
                st.clip = d.clip;
            break;
        }
        case CommitQ:
        {
            st.q = qValue;
            const uint32_t di = qStatusDi & 0x30u;
            st.status = (st.status & 0xFCFu) | di | (di << 6);
            break;
        }
        default:
            st.p = pValue[c.slot];
            break;
        }
    };

    int cur = g.entry;
    while (cur >= 0)
    {
        const Node &n = g.nodes[cur];
        if (n.kind == NodeKind::Bail)
            break;
        cyc += n.advance;
        vu.m_cycle = startCycle + cyc;
        st.cycles = vu.m_cycle;
        if (vu.m_xgkick.active)
        {
            for (uint32_t k = 0; k < n.advance; ++k)
                vu.progressXgkick();
            if (vu.m_stopRequested)
                break;
        }
        for (const Commit &c : n.commits)
            applyCommit(c);
        if (n.xgkick && vu.m_xgkick.active)
            break; // a second XGKICK waits for the first one: left to the interpreter

        const PairInfo &p = n.pair;
        st.pc = n.pc;
        const bool inTakenDelaySlot = st.branchPending && st.branchDelay == 0u;
        const uint8_t writtenVi = n.writtenVi;
        const int32_t oldVi = writtenVi != 0 ? st.vi[writtenVi] : 0;
        uint32_t flagPos = n.flagBase;

        const auto harvestFlags = [&] {
            while (vu.m_flagCount != 0u)
            {
                const VU1Interpreter::FlagPipelineEntry &e = vu.m_flagPipeline[vu.m_flagHead];
                FlagData &d = ring[(head + flagPos) & (kMaxFlags - 1)];
                d.mac = e.mac;
                d.status = e.status;
                d.extra = e.extraSticky;
                d.clip = e.clip;
                ++flagPos;
                vu.m_flagHead = (vu.m_flagHead + 1u) % VU1Interpreter::kMaxFlagEntries;
                --vu.m_flagCount;
            }
        };
        const auto runUpper = [&] {
            if (const VU1Interpreter::FmacFn fast = ex.upperFast[cur])
                (vu.*fast)(p.upper);
            else
                vu.execUpper(p.upper);
            harvestFlags();
        };
        const auto runLower = [&] {
            vu.execLower(p.lower, a.data, a.dataSize, *a.gs, a.memory, p.upper);
            harvestFlags();
            // Stores land at once; Q and P results wait in the program's slots for their commit.
            for (uint32_t live = vu.m_storeValid; live != 0u; live &= live - 1u)
            {
                VU1Interpreter::PendingStore &store = vu.m_storePipeline[static_cast<size_t>(std::countr_zero(live))];
                if (store.address + 16u <= a.dataSize)
                {
                    uint32_t words[4];
                    std::memcpy(words, a.data + store.address, sizeof(words));
                    for (uint32_t component = 0; component < 4u; ++component)
                        if ((store.laneMask & laneBit(component)) != 0u)
                            words[component] = store.words[component];
                    std::memcpy(a.data + store.address, words, sizeof(words));
                }
                store = {};
            }
            vu.m_storeValid = 0u;
            if (vu.m_fdiv.valid)
            {
                qValue = vu.m_fdiv.value;
                qStatusDi = vu.m_fdiv.statusDi;
                vu.m_fdiv = {};
            }
            if (vu.m_efuValid != 0u)
            {
                for (uint32_t slot = 0; slot < kEfuSlots; ++slot)
                    if (vu.m_efu[slot].valid)
                    {
                        if (n.efuSlot >= 0)
                            pValue[n.efuSlot] = vu.m_efu[slot].value;
                        vu.m_efu[slot] = {};
                    }
                vu.m_efuValid = 0u;
            }
        };

        if (p.iBit)
        {
            runUpper();
            float immediate = 0.0f;
            std::memcpy(&immediate, &p.lower, sizeof(immediate));
            st.i = vu.normalizeOperand(immediate);
        }
        else if (p.upperVfShadowReg != 0u)
        {
            float oldVf[4], upperVf[4];
            std::memcpy(oldVf, st.vf[p.upperVfShadowReg], sizeof(oldVf));
            runUpper();
            std::memcpy(upperVf, st.vf[p.upperVfShadowReg], sizeof(upperVf));
            std::memcpy(st.vf[p.upperVfShadowReg], oldVf, sizeof(oldVf));
            runLower();
            std::memcpy(st.vf[p.upperVfShadowReg], upperVf, sizeof(upperVf));
        }
        else
        {
            runUpper();
            if (!p.lowerNop)
                runLower();
        }
        if (vu.m_stopRequested || flagPos - n.flagBase != n.pushes.size())
            break; // a reserved instruction, or the decoder queued something the model did not

        vu.m_viBranchBackupValid = false;
        if (writtenVi != 0 && p.delaysNextBranchRead && !inTakenDelaySlot)
            vu.recordViWriteForBranch(writtenVi, oldVi);
        st.vf[0][0] = 0.0f;
        st.vf[0][1] = 0.0f;
        st.vf[0][2] = 0.0f;
        st.vf[0][3] = 1.0f;
        st.vi[0] = 0;

        uint32_t nextPc = n.pc + 8u;
        if (nextPc >= a.codeSize)
            nextPc = 0u;
        st.pc = nextPc;
        taken = st.branchPending;
        if (st.branchPending)
        {
            if (st.branchDelay == 0u)
            {
                st.pc = st.branchTarget & 0x3FFFu;
                st.branchPending = false;
            }
            else
                --st.branchDelay;
        }

        if (n.kind == NodeKind::End)
        {
            for (const Commit &c : n.endCommits)
                applyCommit(c);
            if (vu.m_xgkick.active)
                for (uint32_t k = 0; k < n.endExtra; ++k)
                    vu.progressXgkick();
            while (vu.m_xgkick.active && !vu.m_stopRequested)
            {
                vu.progressXgkick();
                ++extraCycles;
            }
            const uint64_t total = cyc + n.endExtra + extraCycles;
            if (vu.m_stopRequested || total >= a.maxCycles)
                break;
            vu.m_cycle = startCycle + total;
            st.cycles = vu.m_cycle;
            st.ebit = false;
            st.haltAfterDelaySlot = false;
            vu.m_pendingHaltD = false;
            vu.m_pendingHaltT = false;
            result = RunResult::Done;
            break;
        }
        if (n.kind == NodeKind::DelaySlot)
        {
            if (n.always && !taken)
                break;
            if (taken && st.pc != n.target)
                break;
            if (taken && n.backward && cyc + 8192u >= a.maxCycles)
                break; // a long loop: the interpreter's cycle budget may cut it
            cur = (taken && !n.always) ? n.nextTaken : n.next;
        }
        else
            cur = n.next;
    }

    // Whatever the interpreter's queues still hold is not part of the run.
    vu.m_flagCount = 0u;
    vu.m_flagHead = 0u;
    vu.m_fdiv = {};
    vu.m_efu = {};
    vu.m_efuValid = 0u;
    vu.m_storeValid = 0u;
    vu.m_nextCommitCycle = UINT64_MAX;
    if (result != RunResult::Done)
        vu.m_xgkick.active = false;
    vu.m_packetSink = nullptr;
    if (vuRounding && previousRounding != -1)
        std::fesetround(previousRounding);
    return result;
}

void Vu1AotAccess::interpret(VU1Interpreter &vu, const VU1Interpreter::AotRun &a, std::vector<std::vector<uint8_t>> &packets)
{
    vu.m_packetSink = &packets;
    vu.run(a.code, a.codeSize, a.data, a.dataSize, *a.gs, a.memory, a.maxCycles);
    vu.m_packetSink = nullptr;
}

bool Vu1AotAccess::startsIdle(const VU1Interpreter &vu)
{
    return !vu.pipelinesPending() && !vu.m_state.branchPending && !vu.m_stopRequested && !vu.m_viBranchBackupValid;
}

void Vu1AotAccess::save(const VU1Interpreter &vu, const uint8_t *data, uint32_t dataSize, Snapshot &out)
{
    out.state = vu.m_state;
    out.cycle = vu.m_cycle;
    out.efuResourceReady = vu.m_efuResourceReady;
    out.workingClip = vu.m_workingClip;
    out.viBranchBackupValue = vu.m_viBranchBackupValue;
    out.viBranchBackupReg = vu.m_viBranchBackupReg;
    out.viBranchBackupValid = vu.m_viBranchBackupValid;
    out.pendingHaltD = vu.m_pendingHaltD;
    out.pendingHaltT = vu.m_pendingHaltT;
    out.stopRequested = vu.m_stopRequested;
    out.data.assign(data, data + dataSize);
}

void Vu1AotAccess::restore(VU1Interpreter &vu, uint8_t *data, const Snapshot &in)
{
    vu.m_state = in.state;
    vu.m_cycle = in.cycle;
    vu.m_efuResourceReady = in.efuResourceReady;
    vu.m_workingClip = in.workingClip;
    vu.m_viBranchBackupValue = in.viBranchBackupValue;
    vu.m_viBranchBackupReg = in.viBranchBackupReg;
    vu.m_viBranchBackupValid = in.viBranchBackupValid;
    vu.m_pendingHaltD = in.pendingHaltD;
    vu.m_pendingHaltT = in.pendingHaltT;
    vu.m_stopRequested = in.stopRequested;
    std::memcpy(data, in.data.data(), in.data.size());
    vu.m_xgkick.active = false;
}

void Vu1AotAccess::emit(VU1Interpreter &vu, const std::vector<std::vector<uint8_t>> &packets)
{
    for (const std::vector<uint8_t> &packet : packets)
    {
        const uint32_t size = static_cast<uint32_t>(packet.size());
        if (vu.m_packetObserver)
            vu.m_packetObserver(packet.data(), size);
        if (vu.m_discardPackets)
            continue;
        if (vu.m_activeMemory)
            vu.m_activeMemory->submitGifPacket(GifPathId::Path1, packet.data(), size);
        else if (vu.m_activeGs)
            vu.m_activeGs->processGIFPacket(packet.data(), size);
    }
}

std::string Vu1AotAccess::diff(const Snapshot &a, const std::vector<std::vector<uint8_t>> &packetsA, const Snapshot &b,
                               const std::vector<std::vector<uint8_t>> &packetsB)
{
    std::string out;
    char text[128];
    if (a.cycle != b.cycle)
    {
        std::snprintf(text, sizeof(text), "cycles %llu vs %llu; ", static_cast<unsigned long long>(a.cycle), static_cast<unsigned long long>(b.cycle));
        out += text;
    }
    const auto *sa = reinterpret_cast<const uint8_t *>(&a.state);
    const auto *sb = reinterpret_cast<const uint8_t *>(&b.state);
    size_t shown = 0, total = 0;
    for (size_t i = 0; i < sizeof(VU1State); ++i)
        if (sa[i] != sb[i])
        {
            if (shown++ < 4)
            {
                const auto in = [&](size_t start, size_t size) { return i >= start && i < start + size; };
                if (in(offsetof(VU1State, vf), sizeof(VU1State::vf)))
                    std::snprintf(text, sizeof(text), "vf[%zu][%zu]; ", (i - offsetof(VU1State, vf)) / 16, ((i - offsetof(VU1State, vf)) / 4) % 4);
                else if (in(offsetof(VU1State, vi), sizeof(VU1State::vi)))
                    std::snprintf(text, sizeof(text), "vi[%zu]; ", (i - offsetof(VU1State, vi)) / 4);
                else if (in(offsetof(VU1State, acc), sizeof(VU1State::acc)))
                    std::snprintf(text, sizeof(text), "acc; ");
                else if (in(offsetof(VU1State, q), 4))
                    std::snprintf(text, sizeof(text), "q; ");
                else if (in(offsetof(VU1State, p), 4))
                    std::snprintf(text, sizeof(text), "p; ");
                else if (in(offsetof(VU1State, i), 4))
                    std::snprintf(text, sizeof(text), "i; ");
                else if (in(offsetof(VU1State, r), 4))
                    std::snprintf(text, sizeof(text), "r; ");
                else if (in(offsetof(VU1State, pc), 4))
                    std::snprintf(text, sizeof(text), "pc; ");
                else if (in(offsetof(VU1State, mac), 4))
                    std::snprintf(text, sizeof(text), "mac; ");
                else if (in(offsetof(VU1State, clip), 4))
                    std::snprintf(text, sizeof(text), "clip; ");
                else if (in(offsetof(VU1State, status), 4))
                    std::snprintf(text, sizeof(text), "status; ");
                else if (in(offsetof(VU1State, branchTarget), 4))
                    std::snprintf(text, sizeof(text), "branchTarget; ");
                else
                    std::snprintf(text, sizeof(text), "state byte %zu; ", i);
                out += text;
            }
            ++total;
        }
    if (total > shown)
    {
        std::snprintf(text, sizeof(text), "%zu state bytes in all; ", total);
        out += text;
    }
    size_t dataDiffs = 0, firstData = 0;
    for (size_t i = 0; i < a.data.size() && i < b.data.size(); ++i)
        if (a.data[i] != b.data[i] && dataDiffs++ == 0)
            firstData = i;
    if (dataDiffs != 0)
    {
        std::snprintf(text, sizeof(text), "data memory: %zu bytes differ, first at 0x%zx; ", dataDiffs, firstData);
        out += text;
    }
    if (a.workingClip != b.workingClip)
        out += "working clip; ";
    if (a.viBranchBackupValid != b.viBranchBackupValid ||
        (a.viBranchBackupValid && (a.viBranchBackupReg != b.viBranchBackupReg || a.viBranchBackupValue != b.viBranchBackupValue)))
        out += "VI branch backup; ";
    if (a.pendingHaltD != b.pendingHaltD || a.pendingHaltT != b.pendingHaltT || a.stopRequested != b.stopRequested)
        out += "halt flags; ";
    if (packetsA.size() != packetsB.size())
        out += "packet count " + std::to_string(packetsA.size()) + " vs " + std::to_string(packetsB.size()) + "; ";
    else
        for (size_t i = 0; i < packetsA.size(); ++i)
            if (packetsA[i] != packetsB[i])
            {
                out += "packet " + std::to_string(i) + " differs; ";
                break;
            }
    return out;
}
