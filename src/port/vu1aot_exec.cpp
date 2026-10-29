// Pre-timed VU1 programs: running a graph (plan P4, M3.3 and M3.5). See vu1aot.h and vu1aot_exec.h.
//
// A run follows the graph from its entry node. What the interpreter works out as it goes (hazards, the queues that delay
// flag, Q, P, store, VI and vf/ACC results) the graph already holds: how many cycles to advance before each pair, which
// results commit before it, and where a branch leads. What is left is the arithmetic: the native ops of vu1aot_ops.cpp
// for what this game runs, the interpreter's own execUpper/execLower (here) for the rest, run on the interpreter's state
// with the entries they queue moved into the program's slots. So the state, the data memory, the cycle count and the
// packets come out as they do in the interpreter, and anything the graph does not cover (Bail) is left to the
// interpreter.

#include "vu1aot.h"
#include "vu1aot_access.h"
#include "vu1aot_exec.h"
#include "vu1aot_ops.h"

#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"

#include <algorithm>
#include <bit>
#include <cfenv>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr uint8_t laneBit(uint32_t component) { return static_cast<uint8_t>(1u << (3u - component)); }
}

// ---- the interpreter's own ops, for what vu1aot_ops.cpp does not write ----

void Vu1AotAccess::harvestFlags(vu1aot::Ctx &c, uint32_t firstPos, uint32_t expected)
{
    VU1Interpreter &vu = *static_cast<VU1Interpreter *>(c.vu);
    uint32_t pos = firstPos;
    uint32_t seen = 0;
    while (vu.m_flagCount != 0u)
    {
        const VU1Interpreter::FlagPipelineEntry &e = vu.m_flagPipeline[vu.m_flagHead];
        vu1aot::FlagData &d = c.ring[(c.head + pos) & (vu1aot::kMaxFlags - 1)];
        d.mac = e.mac;
        d.status = e.status;
        d.extra = e.extraSticky;
        d.clip = e.clip;
        ++pos;
        ++seen;
        vu.m_flagHead = (vu.m_flagHead + 1u) % VU1Interpreter::kMaxFlagEntries;
        --vu.m_flagCount;
    }
    if (seen != expected)
        c.failed = true; // the decoder queued something the model did not expect
}

void Vu1AotAccess::flushXgkick(vu1aot::Ctx &c)
{
    VU1Interpreter &vu = *static_cast<VU1Interpreter *>(c.vu);
    vu.m_xgkick.cycleCredit += c.xgkickPending - 1u;
    c.xgkickPending = 0u;
    vu.progressXgkick();
}

void Vu1AotAccess::generalUpper(vu1aot::Ctx &c, const vu1aot::XNode &n)
{
    VU1Interpreter &vu = *static_cast<VU1Interpreter *>(c.vu);
    if (c.xgkickPending != 0u)
        flushXgkick(c);
    vu.execUpper(n.upperWord);
    harvestFlags(c, n.flagBase, n.upperPushes);
    if (vu.m_stopRequested)
        c.failed = true;
}

void Vu1AotAccess::generalLower(vu1aot::Ctx &c, const vu1aot::XNode &n)
{
    using namespace vu1aot;
    VU1Interpreter &vu = *static_cast<VU1Interpreter *>(c.vu);
    const VU1Interpreter::AotRun &a = *static_cast<const VU1Interpreter::AotRun *>(c.run);
    if (c.xgkickPending != 0u)
        flushXgkick(c);
    vu.execLower(n.lowerWord, a.data, a.dataSize, *a.gs, a.memory, n.upperWord);
    harvestFlags(c, n.lowerFlagBase, n.lowerPushes);
    // Stores land at once; Q and P results wait in the program's slots for their commit.
    for (uint32_t live = vu.m_storeValid; live != 0u; live &= live - 1u)
    {
        VU1Interpreter::PendingStore &store = vu.m_storePipeline[static_cast<size_t>(std::countr_zero(live))];
        if (store.address + 16u <= a.dataSize)
        {
            if (c.log != nullptr)
                c.log->save(a.data, store.address);
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
        c.qValue = vu.m_fdiv.value;
        c.qStatusDi = vu.m_fdiv.statusDi;
        vu.m_fdiv = {};
    }
    if (vu.m_efuValid != 0u)
    {
        for (uint32_t slot = 0; slot < kEfuSlots; ++slot)
            if (vu.m_efu[slot].valid)
            {
                if (n.efuSlot != 0xFF)
                    c.pValue[n.efuSlot] = vu.m_efu[slot].value;
                vu.m_efu[slot] = {};
            }
        vu.m_efuValid = 0u;
    }
    if (vu.m_stopRequested)
        c.failed = true;
}

void Vu1AotAccess::prepare(vu1aot::Graph &graph, bool allowNative)
{
    using namespace vu1aot;
    auto info = std::make_shared<ExecInfo>();
    info->nodes.resize(graph.nodes.size());
    for (size_t i = 0; i < graph.nodes.size(); ++i)
    {
        const Node &n = graph.nodes[i];
        XNode &x = info->nodes[i];
        x.kind = static_cast<uint8_t>(n.kind);
        if (n.kind == NodeKind::Bail)
            continue;
        const PairInfo &p = n.pair;
        x.pc = n.pc;
        x.advance = n.advance;
        x.target = n.target;
        x.next = n.next;
        x.nextTaken = n.nextTaken;
        x.upperWord = p.upper;
        x.lowerWord = p.lower;
        x.flagBase = n.flagBase;
        x.upperPushes = n.upperPushes;
        x.lowerPushes = n.lowerPushes;
        x.lowerFlagBase = static_cast<uint8_t>(n.flagBase + n.upperPushes);
        x.writtenVi = n.writtenVi;
        x.readBackupReg = n.readBackupReg;
        x.recordBackup = (n.writtenVi != 0 && p.delaysNextBranchRead) ? 1 : 0;
        x.backupAfterTaken = n.backupAfterTaken;
        x.backupAfterFall = n.backupAfterFall;
        x.efuSlot = n.efuSlot >= 0 ? static_cast<uint8_t>(n.efuSlot) : 0xFF;
        x.endExtra = n.endExtra;
        x.commitStart = static_cast<uint32_t>(info->commits.size());
        x.commitCount = static_cast<uint8_t>(n.commits.size());
        info->commits.insert(info->commits.end(), n.commits.begin(), n.commits.end());
        x.endCommitStart = static_cast<uint32_t>(info->commits.size());
        x.endCommitCount = static_cast<uint8_t>(n.endCommits.size());
        info->commits.insert(info->commits.end(), n.endCommits.begin(), n.endCommits.end());
        if (n.xgkick)
            x.flags |= XfXgkick;
        if (n.backward)
            x.flags |= XfBackward;
        if (n.always)
            x.flags |= XfAlways;
        if (n.inDelaySlot)
            x.flags |= XfInDelaySlot;
        if (p.iBit)
        {
            x.form = FormIBit;
            float immediate;
            std::memcpy(&immediate, &p.lower, sizeof(immediate));
            // VU1Interpreter::normalizeOperand
            uint32_t bits;
            std::memcpy(&bits, &immediate, sizeof(bits));
            const uint32_t exponent = (bits >> 23) & 0xFFu;
            if (exponent == 0u)
                bits &= 0x80000000u;
            else if (exponent == 0xFFu)
                bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
            std::memcpy(&x.iValue, &bits, sizeof(bits));
        }
        else if (p.upperVfShadowReg != 0)
        {
            x.form = FormShadow;
            x.shadowReg = p.upperVfShadowReg;
        }
        const Natives ops = chooseOps(p.upper, p.lower, p.iBit, p.lowerNop, n, allowNative);
        x.upper = ops.upperGeneral ? &Vu1AotAccess::generalUpper : ops.upper;
        x.lower = ops.lowerGeneral ? &Vu1AotAccess::generalLower : ops.lower;
        if (ops.upperGeneral || ops.lowerGeneral || ops.upperWritesVf0)
            x.flags |= XfResetZero;
    }
    graph.exec = info;
}

Vu1AotAccess::Frame::Frame(VU1Interpreter &vuIn, const VU1Interpreter::AotRun &args, std::vector<std::vector<uint8_t>> &packets)
    : vu(vuIn), a(args), st(vuIn.m_state), xgkickActive(&vuIn.m_xgkick.active), startCycle(vuIn.m_cycle)
{
    c.st = &st;
    c.data = a.data;
    c.vu = &vu;
    c.run = &a;
    c.workingClip = &vu.m_workingClip;
    c.log = s_storeLog;
    c.flushXgkick = &Vu1AotAccess::flushXgkick;
    vu.m_activeVuData = a.data;
    vu.m_activeVuDataSize = a.dataSize;
    vu.m_activeGs = a.gs;
    vu.m_activeMemory = a.memory;
    vu.m_packetSink = &packets;
    previousRounding = std::fegetround();
    vuRounding = std::fesetround(FE_TOWARDZERO) == 0;
}

vu1aot::RunResult Vu1AotAccess::Frame::end(uint32_t pc, uint32_t target, uint32_t endExtra, bool inSlot, uint8_t backupAfterTaken,
                                           uint8_t backupAfterFall)
{
    using vu1aot::RunResult;
    uint32_t extraCycles = 0;
    if (c.xgkickPending != 0u)
        flushXgkick(c);
    if (*xgkickActive)
        for (uint32_t k = 0; k < endExtra; ++k)
            vu.progressXgkick();
    while (*xgkickActive && !vu.m_stopRequested)
    {
        // Inside a tag's data the cycles up to its last qword go in one call, as in Frame::progress: the credit is below 2 here,
        // so 2 * qwords - credit cycles copy exactly the qwords before the last one, and nothing ends or stops before that.
        const VU1Interpreter::XgkickPipeline &x = vu.m_xgkick;
        if (x.currentTagEnd != 0u)
        {
            const uint32_t beforeLast = (x.currentTagEnd - x.copiedBytes) / 16u - 1u;
            if (beforeLast != 0u)
            {
                const uint32_t cycles = 2u * beforeLast - x.cycleCredit;
                vu.m_xgkick.cycleCredit += cycles - 1u;
                vu.progressXgkick();
                extraCycles += cycles;
                continue;
            }
        }
        vu.progressXgkick();
        ++extraCycles;
    }
    const uint64_t total = cyc + endExtra + extraCycles;
    if (vu.m_stopRequested || total >= a.maxCycles)
        return RunResult::Bail;
    // What the interpreter leaves behind: the next pc, the last taken branch's fields, the VI backup of the last pair.
    const bool jumped = inSlot && c.taken;
    uint32_t nextPc = pc + 8u;
    if (nextPc >= a.codeSize)
        nextPc = 0u;
    st.pc = jumped ? target : nextPc;
    if (jumped)
    {
        st.branchTarget = target;
        st.branchDelay = 0u;
    }
    const uint8_t backup = inSlot ? (c.taken ? backupAfterTaken : backupAfterFall) : backupAfterFall;
    vu.m_viBranchBackupValid = backup != 0;
    vu.m_viBranchBackupReg = backup;
    vu.m_viBranchBackupValue = backup != 0 ? c.oldVi : 0;
    vu.m_cycle = startCycle + total;
    st.cycles = vu.m_cycle;
    st.ebit = false;
    st.haltAfterDelaySlot = false;
    vu.m_pendingHaltD = false;
    vu.m_pendingHaltT = false;
    return RunResult::Done;
}

vu1aot::RunResult Vu1AotAccess::Frame::finish(vu1aot::RunResult result)
{
    // Whatever the interpreter's queues still hold is not part of the run.
    vu.m_flagCount = 0u;
    vu.m_flagHead = 0u;
    vu.m_fdiv = {};
    vu.m_efu = {};
    vu.m_efuValid = 0u;
    vu.m_storeValid = 0u;
    vu.m_nextCommitCycle = UINT64_MAX;
    if (result != vu1aot::RunResult::Done)
        vu.m_xgkick.active = false;
    vu.m_packetSink = nullptr;
    if (vuRounding && previousRounding != -1)
        std::fesetround(previousRounding);
    return result;
}

vu1aot::RunResult Vu1AotAccess::run(VU1Interpreter &vu, const vu1aot::Graph &g, const VU1Interpreter::AotRun &a,
                                    std::vector<std::vector<uint8_t>> &packets)
{
    using namespace vu1aot;
    const ExecInfo &ex = *static_cast<const ExecInfo *>(g.exec.get());
    const XNode *nodes = ex.nodes.data();
    const Commit *commits = ex.commits.data();
    Frame f(vu, a, packets);
    Ctx &c = f.c;
    VU1State &st = f.st;

    const auto applyCommit = [&](const Commit &k) {
        switch (k.kind)
        {
        case CommitFlag:
            ops::commitFlag(c, st, k.writes);
            break;
        case CommitQ:
            ops::commitQ(c, st);
            break;
        default:
            ops::commitP(c, st, k.slot);
            break;
        }
    };

    int cur = g.entry;
    while (cur >= 0)
    {
        const XNode &n = nodes[cur];
        if (n.kind == static_cast<uint8_t>(NodeKind::Bail))
            break;
        f.cyc += n.advance;
        if (*f.xgkickActive && !f.progress(n.advance))
            break;
        for (uint32_t k = 0; k < n.commitCount; ++k)
            applyCommit(commits[n.commitStart + k]);
        if ((n.flags & XfXgkick) != 0 && *f.xgkickActive)
            break; // a second XGKICK waits for the first one: left to the interpreter
        if (n.writtenVi != 0)
            c.oldVi = st.vi[n.writtenVi];
        f.syncForInterpreter(n.pc);

        switch (n.form)
        {
        case FormNormal:
            if (n.upper != nullptr)
                n.upper(c, n);
            if (n.lower != nullptr)
                n.lower(c, n);
            break;
        case FormIBit:
            if (n.upper != nullptr)
                n.upper(c, n);
            st.i = n.iValue;
            break;
        default:
        {
            float oldVf[4], upperVf[4];
            std::memcpy(oldVf, st.vf[n.shadowReg], sizeof(oldVf));
            if (n.upper != nullptr)
                n.upper(c, n);
            std::memcpy(upperVf, st.vf[n.shadowReg], sizeof(upperVf));
            std::memcpy(st.vf[n.shadowReg], oldVf, sizeof(oldVf));
            if (n.lower != nullptr)
                n.lower(c, n);
            std::memcpy(st.vf[n.shadowReg], upperVf, sizeof(upperVf));
            break;
        }
        }
        if (c.failed)
            break;
        if (n.recordBackup != 0)
            c.backupValue = c.oldVi;
        if ((n.flags & XfResetZero) != 0)
            ops::resetZero(st);

        const NodeKind kind = static_cast<NodeKind>(n.kind);
        if (kind == NodeKind::End)
        {
            for (uint32_t k = 0; k < n.endCommitCount; ++k)
                applyCommit(commits[n.endCommitStart + k]);
            return f.finish(f.end(n.pc, n.target, n.endExtra, (n.flags & XfInDelaySlot) != 0, n.backupAfterTaken, n.backupAfterFall));
        }
        if (kind == NodeKind::DelaySlot)
        {
            const bool always = (n.flags & XfAlways) != 0;
            if (always && !c.taken)
                break;
            if (c.taken)
            {
                if ((n.flags & XfBackward) != 0 && f.cyc + 8192u >= a.maxCycles)
                    break; // a long loop: the interpreter's cycle budget may cut it
                st.branchTarget = n.target;
                st.branchDelay = 0u;
            }
            cur = (c.taken && !always) ? n.nextTaken : n.next;
        }
        else
            cur = n.next;
    }
    return f.finish(RunResult::Bail);
}

void Vu1AotAccess::interpret(VU1Interpreter &vu, const VU1Interpreter::AotRun &a, std::vector<std::vector<uint8_t>> &packets)
{
    vu.m_packetSink = &packets;
    vu.run(a.code, a.codeSize, a.data, a.dataSize, *a.gs, a.memory, a.maxCycles);
    vu.m_packetSink = nullptr;
}

bool Vu1AotAccess::startsIdle(const VU1Interpreter &vu)
{
    // The programs write vf and ACC results at once, which is only the interpreter's model with immediate writes on.
    return vu.m_immediateWrites && !vu.pipelinesPending() && !vu.m_state.branchPending && !vu.m_stopRequested && !vu.m_viBranchBackupValid;
}

void Vu1AotAccess::saveState(const VU1Interpreter &vu, Snapshot &out)
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
}

void Vu1AotAccess::restoreState(VU1Interpreter &vu, const Snapshot &in)
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
    vu.m_xgkick.active = false;
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
