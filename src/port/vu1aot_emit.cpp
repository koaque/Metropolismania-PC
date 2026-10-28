// Compiled VU1 programs: writing a prepared graph as C++ (plan P8 step 4). See vu1aot_compiled.h.
//
// The function does, node by node and in the same order, what Vu1AotAccess::run does when it reaches that node: advance the
// cycle count and the XGKICK copy, apply the commits, run the two halves, then follow the node's kind. What run() decides
// from the node's fields at run time is decided here once: only what depends on the data (a branch taken or not, an XGKICK
// in flight, the interpreter stopping) is left as a test.

#include "vu1aot_compiled.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace
{
    std::string format(const char *text, ...)
    {
        char buffer[512];
        va_list args;
        va_start(args, text);
        std::vsnprintf(buffer, sizeof(buffer), text, args);
        va_end(args);
        return buffer;
    }

    std::string jumpTo(const std::vector<vu1aot::XNode> &nodes, int next)
    {
        if (next < 0 || nodes[static_cast<size_t>(next)].kind == static_cast<uint8_t>(vu1aot::NodeKind::Bail))
            return "bail";
        return format("n%d", next);
    }
}

std::string Vu1AotAccess::emitFunction(const vu1aot::Graph &graph, const std::string &name)
{
    using namespace vu1aot;
    if (!graph.supported || graph.exec == nullptr || graph.entry < 0)
        return {};
    const ExecInfo &ex = *static_cast<const ExecInfo *>(graph.exec.get());
    const std::vector<XNode> &xs = ex.nodes;
    const std::vector<Commit> &commits = ex.commits;

    std::string decls, body;
    const auto commitText = [&](const Commit &k) {
        switch (k.kind)
        {
        case CommitFlag:
            return format("        ops::commitFlag(c, st, %uu);\n", static_cast<unsigned>(k.writes));
        case CommitQ:
            return std::string("        ops::commitQ(c, st);\n");
        default:
            return format("        ops::commitP(c, st, %uu);\n", static_cast<unsigned>(k.slot));
        }
    };

    for (size_t i = 0; i < xs.size(); ++i)
    {
        const XNode &x = xs[i];
        if (x.kind == static_cast<uint8_t>(NodeKind::Bail))
            continue;
        const Node &node = graph.nodes[i];
        const PairInfo &p = node.pair;
        // The ops prepare() chose, by name.
        const Natives ops = chooseOps(p.upper, p.lower, p.iBit, p.lowerNop, node, true);
        const std::string upper = ops.upperGeneral ? "Vu1AotAccess::generalUpper" : (ops.upperName != nullptr ? ops.upperName : "");
        const std::string lower = ops.lowerGeneral ? "Vu1AotAccess::generalLower" : (ops.lowerName != nullptr ? ops.lowerName : "");
        if ((x.upper != nullptr) != !upper.empty() || (x.lower != nullptr) != !lower.empty())
            return {}; // not what prepare() made: no compiled program rather than a different one
        const bool general = ops.upperGeneral || ops.lowerGeneral;
        const std::string nodeName = format("kN%zu", i);
        if (!upper.empty() || !lower.empty())
            decls += format("    constexpr XNode %s{.upperWord = 0x%08xu, .lowerWord = 0x%08xu, .pc = 0x%04xu, .flagBase = %uu, .lowerFlagBase = %uu, "
                            ".upperPushes = %uu, .lowerPushes = %uu, .readBackupReg = %uu, .efuSlot = %uu};\n",
                            nodeName.c_str(), x.upperWord, x.lowerWord, static_cast<unsigned>(x.pc), static_cast<unsigned>(x.flagBase),
                            static_cast<unsigned>(x.lowerFlagBase), static_cast<unsigned>(x.upperPushes), static_cast<unsigned>(x.lowerPushes),
                            static_cast<unsigned>(x.readBackupReg), static_cast<unsigned>(x.efuSlot));

        static const char *const kKinds[] = {"plain", "branch", "delay slot", "end", "bail"};
        body += format("n%zu: // pc 0x%04x, %s\n    {\n", i, static_cast<unsigned>(x.pc), kKinds[x.kind < 5 ? x.kind : 4]);
        if (x.advance != 0)
        {
            body += format("        f.cyc += %uu;\n", static_cast<unsigned>(x.advance));
            body += format("        if (*xgkick && !f.progress(%uu))\n            goto bail;\n", static_cast<unsigned>(x.advance));
        }
        for (uint32_t k = 0; k < x.commitCount; ++k)
            body += commitText(commits[x.commitStart + k]);
        if ((x.flags & XfXgkick) != 0)
            body += "        if (*xgkick)\n            goto bail; // a second XGKICK waits for the first one: left to the interpreter\n";
        if (x.writtenVi != 0)
            body += format("        c.oldVi = st.vi[%u];\n", static_cast<unsigned>(x.writtenVi));
        if (general)
            body += format("        f.syncForInterpreter(0x%04xu);\n", static_cast<unsigned>(x.pc));
        const auto call = [&](const std::string &op) { return op.empty() ? std::string() : format("        %s(c, %s);\n", op.c_str(), nodeName.c_str()); };
        switch (x.form)
        {
        case FormNormal:
            body += call(upper) + call(lower);
            break;
        case FormIBit:
        {
            uint32_t bits;
            std::memcpy(&bits, &x.iValue, sizeof(bits));
            body += call(upper) + format("        st.i = std::bit_cast<float>(0x%08xu);\n", bits);
            break;
        }
        default:
        {
            const unsigned r = x.shadowReg;
            body += format("        const __m128 shadowOld = _mm_loadu_ps(st.vf[%u]);\n", r) + call(upper) +
                    format("        const __m128 shadowUpper = _mm_loadu_ps(st.vf[%u]);\n        _mm_storeu_ps(st.vf[%u], shadowOld);\n", r, r) +
                    call(lower) + format("        _mm_storeu_ps(st.vf[%u], shadowUpper);\n", r);
            break;
        }
        }
        if (general)
            body += "        if (c.failed)\n            goto bail;\n";
        if (x.recordBackup != 0)
            body += "        c.backupValue = c.oldVi;\n";
        if ((x.flags & XfResetZero) != 0)
            body += "        ops::resetZero(st);\n";

        switch (static_cast<NodeKind>(x.kind))
        {
        case NodeKind::End:
            for (uint32_t k = 0; k < x.endCommitCount; ++k)
                body += commitText(commits[x.endCommitStart + k]);
            body += format("        return f.finish(f.end(0x%04xu, 0x%04xu, %uu, %s, %uu, %uu));\n", static_cast<unsigned>(x.pc),
                           static_cast<unsigned>(x.target), static_cast<unsigned>(x.endExtra), (x.flags & XfInDelaySlot) != 0 ? "true" : "false",
                           static_cast<unsigned>(x.backupAfterTaken), static_cast<unsigned>(x.backupAfterFall));
            break;
        case NodeKind::DelaySlot:
        {
            const std::string loopCheck = (x.flags & XfBackward) != 0
                                              ? "            if (f.cyc + 8192u >= a.maxCycles)\n                goto bail; // a long loop: the interpreter's cycle budget may cut it\n"
                                              : "";
            const std::string setTarget = format("            st.branchTarget = 0x%04xu;\n            st.branchDelay = 0u;\n", static_cast<unsigned>(x.target));
            if ((x.flags & XfAlways) != 0)
            {
                body += "        if (!c.taken)\n            goto bail;\n";
                body += "        {\n" + loopCheck + setTarget + "        }\n";
                body += format("        goto %s;\n", jumpTo(xs, x.next).c_str());
            }
            else
            {
                body += "        if (c.taken)\n        {\n" + loopCheck + setTarget + format("            goto %s;\n        }\n", jumpTo(xs, x.nextTaken).c_str());
                body += format("        goto %s;\n", jumpTo(xs, x.next).c_str());
            }
            break;
        }
        default:
            body += format("        goto %s;\n", jumpTo(xs, x.next).c_str());
            break;
        }
        body += "    }\n";
    }

    std::string out = decls;
    out += "\n    RunResult " + name + "(VU1Interpreter &vu, const VU1Interpreter::AotRun &a, std::vector<std::vector<uint8_t>> &packets)\n    {\n";
    out += "        Vu1AotAccess::Frame f(vu, a, packets);\n        Ctx &c = f.c;\n        VU1State &st = f.st;\n        const bool *const xgkick = f.xgkickActive;\n";
    out += "        (void)c;\n        (void)st;\n        (void)xgkick;\n";
    out += format("        goto %s;\n", jumpTo(xs, graph.entry).c_str());
    out += body;
    out += "    bail:\n        return f.finish(RunResult::Bail);\n    }\n";
    return out;
}

namespace vu1aot
{
    namespace
    {
        std::vector<CompiledProgram> &registry()
        {
            static std::vector<CompiledProgram> programs;
            return programs;
        }
    }

    CompiledRegistrar::CompiledRegistrar(const CompiledProgram &program) { registry().push_back(program); }

    const std::vector<CompiledProgram> &compiledPrograms() { return registry(); }

    uint64_t textHash(const std::string &text)
    {
        uint64_t hash = 1469598103934665603ull;
        for (const char ch : text)
            hash = (hash ^ static_cast<uint8_t>(ch)) * 1099511628211ull;
        return hash;
    }

    std::string compiledName(uint32_t entryPc, uint64_t codeHash)
    {
        return format("vu1prog_%04x_%016llx", entryPc, static_cast<unsigned long long>(codeHash));
    }

    void emitProgramFile(const Graph &graph, uint64_t codeHash, const std::string &dir)
    {
        const std::string name = compiledName(graph.entryPc, codeHash);
        const std::string function = Vu1AotAccess::emitFunction(graph, name);
        if (function.empty())
            return;
        std::string text;
        text += format("// Compiled VU1 program (src/port/vu1aot_compiled.h): entry 0x%04x, code 0x%04x to 0x%04x (hash %016llx), %zu nodes.\n",
                       graph.entryPc, graph.codeLo, graph.codeHi, static_cast<unsigned long long>(codeHash), graph.nodes.size());
        text += "// Written by src/port/vu1aot_emit.cpp from the game's VU1 microcode: game-derived, never committed.\n\n";
        text += "#include \"vu1aot_compiled.h\"\n\n";
        text += "#if defined(_MSC_VER)\n#pragma warning(disable : 4102) // labels of nodes nothing jumps to\n#endif\n\n";
        text += "namespace\n{\n    using namespace vu1aot;\n\n";
        text += function;
        text += format("\n    const CompiledRegistrar kRegistrar{CompiledProgram{0x%04xu, 0x%04xu, 0x%04xu, 0x%016llxull, 0x%016llxull, &%s}};\n}\n",
                       graph.entryPc, graph.codeLo, graph.codeHi, static_cast<unsigned long long>(codeHash),
                       static_cast<unsigned long long>(textHash(function)), name.c_str());
        std::error_code ignored;
        std::filesystem::create_directories(dir, ignored);
        const std::filesystem::path path = std::filesystem::path(dir) / (name + ".cpp");
        {
            std::ifstream existing(path, std::ios::binary);
            if (existing && std::string(std::istreambuf_iterator<char>(existing), {}) == text)
                return;
        }
        std::ofstream(path, std::ios::binary) << text;
    }
}
