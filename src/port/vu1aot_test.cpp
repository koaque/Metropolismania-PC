// Checks of the pre-timed VU1 programs against the interpreter (plan P4, M3). See vu1aot.h.
//
// MMPC_VU1_AOT=timing (with MMPC_VU1_TEST=<records>): for every record, the interpreter runs it with a pair trace and the
// program graph built for its code and entry PC must reproduce the trace: the same pairs in the same order, issuing at
// the same cycles, ending at the same cycle.

#include "vu1aot.h"
#include "vu1record.h"
#include "vu1test.h"

#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_vu1.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <unordered_map>

namespace port
{
    namespace
    {
        uint64_t hashCode(const std::vector<uint8_t> &code)
        {
            uint64_t key = 1469598103934665603ull;
            for (const uint8_t byte : code)
                key = (key ^ byte) * 1099511628211ull;
            return key;
        }

        struct Program
        {
            vu1aot::Graph graph;
            double buildSeconds = 0.0;
        };

        size_t checkTiming(const std::vector<Vu1Record> &records)
        {
            const auto gsOwner = std::make_unique<GS>(); // too large for the stack
            GS &gs = *gsOwner;
            std::unordered_map<uint64_t, std::unique_ptr<VU1Interpreter>> interpreters;
            std::unordered_map<uint64_t, Program> programs; // by code hash and entry PC
            size_t failed = 0, checked = 0, unsupported = 0;
            for (const Vu1Record &record : records)
            {
                const uint64_t codeKey = hashCode(record.code);
                std::unique_ptr<VU1Interpreter> &slot = interpreters[codeKey];
                if (!slot)
                {
                    slot = std::make_unique<VU1Interpreter>(VU1Interpreter::Unit::VU1);
                    slot->useStandaloneCode(true);
                    slot->setImmediateWrites(true);
                }
                VU1Interpreter &vu1 = *slot;
                const uint32_t entry = record.kind == 1 ? record.before.pc : record.startPC;
                const uint64_t programKey = codeKey ^ (static_cast<uint64_t>(entry) * 0x9E3779B97F4A7C15ull);
                auto found = programs.find(programKey);
                if (found == programs.end())
                {
                    Program program;
                    const auto begin = std::chrono::steady_clock::now();
                    program.graph = vu1aot::buildGraph(vu1, record.code.data(), static_cast<uint32_t>(record.code.size()), entry);
                    program.buildSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
                    std::cerr << "[vu1aot] code " << std::hex << codeKey << " entry 0x" << entry << std::dec << ": "
                              << (program.graph.supported ? "" : "NOT SUPPORTED (" + program.graph.whyNot + "), ") << program.graph.nodes.size()
                              << " nodes, code 0x" << std::hex << program.graph.codeLo << "-0x" << program.graph.codeHi << std::dec << ", built in "
                              << program.buildSeconds * 1000.0 << " ms" << std::endl;
                    found = programs.emplace(programKey, std::move(program)).first;
                }
                const Program &program = found->second;
                if (!program.graph.supported)
                {
                    ++unsupported;
                    continue;
                }

                std::vector<uint8_t> code = record.code, data = record.dataBefore;
                std::vector<std::pair<uint32_t, uint64_t>> trace;
                std::vector<uint32_t> packetBytes;
                vu1.state() = record.before;
                vu1.setCycleCount(record.cyclesBefore);
                if (record.kind == 1)
                    vu1.restoreIdleScheduler(record.scheduler);
                vu1.setPacketObserver([&packetBytes](const uint8_t *, uint32_t size) { packetBytes.push_back(size); }, true);
                vu1.setPairTrace(&trace);
                if (record.kind == 1)
                    vu1.resume(code.data(), static_cast<uint32_t>(code.size()), data.data(), static_cast<uint32_t>(data.size()), gs, nullptr, record.top,
                               record.itop, record.maxCycles);
                else
                    vu1.execute(code.data(), static_cast<uint32_t>(code.size()), data.data(), static_cast<uint32_t>(data.size()), gs, nullptr,
                                record.startPC, record.top, record.itop, record.maxCycles);
                vu1.setPairTrace(nullptr);
                vu1.setPacketObserver(nullptr, false);

                const std::string diff = vu1aot::checkTrace(program.graph, trace, record.cyclesBefore, vu1.cycleCount() - record.cyclesBefore, packetBytes);
                ++checked;
                if (!diff.empty())
                {
                    ++failed;
                    if (failed <= 12)
                        std::cerr << "[vu1aot] FAIL " << record.name << ": " << diff << std::endl;
                }
            }
            std::cerr << "[vu1aot] timing: " << checked << " record(s) checked, " << failed << " failed, " << unsupported
                      << " skipped (unsupported program), " << programs.size() << " program(s)" << std::endl;
            return failed;
        }
    }

    size_t runVu1AotChecks(const std::vector<Vu1Record> &records)
    {
        const char *mode = std::getenv("MMPC_VU1_AOT");
        if (mode == nullptr || mode[0] == '\0')
            return 0;
        if (std::strcmp(mode, "timing") == 0)
            return checkTiming(records);
        if (std::strcmp(mode, "run") == 0)
            return 0; // the replay itself goes through the programs (vu1test.cpp)
        std::cerr << "[vu1aot] unknown MMPC_VU1_AOT mode '" << mode << "'" << std::endl;
        return 1;
    }
}
