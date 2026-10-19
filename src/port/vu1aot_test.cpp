// Checks of the pre-timed VU1 programs against the interpreter (plan P4, M3). See vu1aot.h.
//
// MMPC_VU1_AOT=fuzz (with MMPC_VU1_TEST=<records>, MMPC_VU1_FUZZ=<cases>): random instruction pairs on random state through
// the native ops and through the interpreter's own execUpper/execLower; everything must come out the same.
//
// MMPC_VU1_AOT=timing (with MMPC_VU1_TEST=<records>): for every record, the interpreter runs it with a pair trace and the
// program graph built for its code and entry PC must reproduce the trace: the same pairs in the same order, issuing at
// the same cycles, ending at the same cycle.

#include "vu1aot.h"
#include "vu1aot_access.h"
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

    namespace
    {
        // Random pairs on random state, through the native ops and through the interpreter's own functions.
        size_t fuzzOps(uint32_t seed, uint32_t cases)
        {
            uint64_t rng = 0x9E3779B97F4A7C15ull ^ (static_cast<uint64_t>(seed) << 32 | seed);
            const auto next = [&rng]() {
                rng ^= rng << 13;
                rng ^= rng >> 7;
                rng ^= rng << 17;
                return static_cast<uint32_t>(rng >> 16);
            };
            const auto randomFloatBits = [&]() {
                static const uint32_t special[] = {0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu, 0x00800000u,
                                                   0x80800000u, 0x3F800000u, 0xBF800000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F800000u, 0xFF800000u,
                                                   0x7FC00000u, 0x7F000000u, 0xFF000000u, 0x00FFFFFFu, 0x3F000000u, 0x40000000u, 0x7E800000u,
                                                   0x01000000u, 0x00000000u, 0x3F800000u};
                const uint32_t pick = next();
                if ((pick & 7u) < 3u)
                    return special[(pick >> 3) % (sizeof(special) / sizeof(special[0]))];
                if ((pick & 7u) < 5u)
                    return next();
                return ((next() & 1u) << 31) | ((1u + next() % 253u) << 23) | (next() & 0x7FFFFFu);
            };
            const auto vu = std::make_unique<VU1Interpreter>(VU1Interpreter::Unit::VU1);
            vu->useStandaloneCode(true);
            vu->setImmediateWrites(true);
            const auto gsOwner = std::make_unique<GS>();
            std::vector<uint8_t> code(0x4000, 0), data(0x4000), dataReference(0x4000);

            // The lower halves the native ops cover: primary opcodes, integer functions, and the special group.
            static const uint32_t primaries[] = {0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x10, 0x12, 0x13, 0x14, 0x16, 0x17, 0x18, 0x1A, 0x1B, 0x1C};
            static const uint32_t functs[] = {0x30, 0x31, 0x32, 0x34, 0x35};
            static const uint32_t specials[] = {0x30, 0x31, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x64, 0x68, 0x69, 0x70, 0x79, 0x7B};
            const auto randomLower = [&]() {
                const uint32_t kind = next() % 8u;
                const uint32_t bits = next();
                if (kind < 3u)
                    return (primaries[next() % (sizeof(primaries) / sizeof(primaries[0]))] << 25) | (bits & 0x1FFFFFFu);
                if (kind < 4u)
                    return (0x40u << 25) | (bits & 0x1FFFFC0u) | functs[next() % (sizeof(functs) / sizeof(functs[0]))];
                const uint32_t sel = specials[next() % (sizeof(specials) / sizeof(specials[0]))];
                return (0x40u << 25) | (bits & 0x1FFF800u) | ((sel >> 2) << 6) | (0x3Cu + (sel & 3u));
            };
            const auto randomUpper = [&]() {
                const uint32_t bits = next() & 0x07FFFFFFu; // no I, E, M, D or T bit
                const uint32_t kind = next() % 4u;
                if (kind == 0u)
                {
                    const uint32_t sel = next() % 0x30u;
                    return (bits & 0x07FFF000u) | ((sel >> 2) << 6) | (0x3Cu + (sel & 3u));
                }
                return (bits & 0x07FFFFC0u) | (next() % 0x3Cu);
            };

            size_t failures = 0, tested = 0, skipped = 0;
            for (uint32_t n = 0; n < cases; ++n)
            {
                uint32_t lower = randomLower();
                uint32_t upper = randomUpper();
                const bool iBit = (next() % 16u) == 0u;
                if (iBit)
                {
                    upper |= 0x80000000u;
                    lower = randomFloatBits();
                }
                const uint32_t pairs[3][2] = {{lower, upper}, {0x8000033Cu, 0x400002FFu}, {0x8000033Cu, 0x000002FFu}};
                std::memcpy(code.data(), pairs, sizeof(pairs));
                vu1aot::Graph graph = vu1aot::buildGraph(*vu, code.data(), static_cast<uint32_t>(code.size()), 0);
                if (!graph.supported || graph.bailNodes != 0)
                {
                    ++skipped;
                    continue;
                }
                // Random state
                VU1State state{};
                for (auto &reg : state.vf)
                    for (float &lane : reg)
                    {
                        const uint32_t bits = randomFloatBits();
                        std::memcpy(&lane, &bits, sizeof(lane));
                    }
                for (int32_t &reg : state.vi)
                    reg = (next() % 3u) == 0u ? static_cast<int32_t>(next()) : static_cast<int32_t>(static_cast<int16_t>(next() % 1024u));
                for (float &lane : state.acc)
                {
                    const uint32_t bits = randomFloatBits();
                    std::memcpy(&lane, &bits, sizeof(lane));
                }
                {
                    const uint32_t q = randomFloatBits(), p = randomFloatBits(), i = randomFloatBits();
                    std::memcpy(&state.q, &q, 4);
                    std::memcpy(&state.p, &p, 4);
                    std::memcpy(&state.i, &i, 4);
                }
                state.r = 0x3F800000u | (next() & 0x7FFFFFu);
                state.mac = next() & 0xFFFFu;
                state.clip = next() & 0xFFFFFFu;
                state.status = next() & 0xFFFu;
                state.top = next() & 0x3FFu;
                state.itop = next() & 0x3FFu;
                state.vf[0][0] = state.vf[0][1] = state.vf[0][2] = 0.0f;
                state.vf[0][3] = 1.0f;
                state.vi[0] = 0;
                for (uint8_t &byte : data)
                    byte = static_cast<uint8_t>(next());
                dataReference = data;

                Vu1AotAccess::Snapshot before, mine, reference;
                vu->state() = state;
                vu->setCycleCount(0);
                Vu1AotAccess::save(*vu, data.data(), 0x4000, before);
                std::vector<std::vector<uint8_t>> minePackets, referencePackets;
                const VU1Interpreter::AotRun run{false, code.data(), 0x4000, data.data(), 0x4000, gsOwner.get(), nullptr, 0, 65536};
                const auto execute = [&](bool native, Vu1AotAccess::Snapshot &out, std::vector<uint8_t> &memory,
                                         std::vector<std::vector<uint8_t>> &packets) {
                    Vu1AotAccess::restore(*vu, memory.data(), before);
                    Vu1AotAccess::prepare(graph, native);
                    const VU1Interpreter::AotRun r{false, code.data(), 0x4000, memory.data(), 0x4000, gsOwner.get(), nullptr, 0, 65536};
                    packets.clear();
                    const vu1aot::RunResult result = Vu1AotAccess::run(*vu, graph, r, packets);
                    Vu1AotAccess::save(*vu, memory.data(), 0x4000, out);
                    return result;
                };
                (void)run;
                const vu1aot::RunResult a = execute(true, mine, data, minePackets);
                const vu1aot::RunResult b = execute(false, reference, dataReference, referencePackets);
                ++tested;
                if (a != b)
                {
                    if (failures++ < 5)
                        std::cerr << "[vu1aot] fuzz: upper 0x" << std::hex << upper << " lower 0x" << lower << std::dec << ": native "
                                  << (a == vu1aot::RunResult::Done ? "done" : "bail") << ", general " << (b == vu1aot::RunResult::Done ? "done" : "bail")
                                  << std::endl;
                    continue;
                }
                if (a == vu1aot::RunResult::Bail)
                    continue;
                const std::string problem = Vu1AotAccess::diff(mine, minePackets, reference, referencePackets);
                if (!problem.empty() && failures++ < 8)
                {
                    std::cerr << "[vu1aot] fuzz case " << n << ": upper 0x" << std::hex << upper << " lower 0x" << lower << std::dec << ": " << problem << std::endl;
                    for (int r = 0; r < 16; ++r)
                        if (mine.state.vi[r] != reference.state.vi[r])
                            std::cerr << "    vi[" << r << "] " << mine.state.vi[r] << " (native) vs " << reference.state.vi[r] << " (interpreter); before " << state.vi[r] << std::endl;
                    uint32_t qa, qb;
                    std::memcpy(&qa, &mine.state.q, 4);
                    std::memcpy(&qb, &reference.state.q, 4);
                    if (qa != qb)
                    {
                        const uint32_t fsf = (lower >> 21) & 3u, ftf = (lower >> 23) & 3u, fs = (lower >> 11) & 0x1Fu, ft = (lower >> 16) & 0x1Fu;
                        uint32_t sbits, tbits;
                        std::memcpy(&sbits, &state.vf[fs][fsf], 4);
                        std::memcpy(&tbits, &state.vf[ft][ftf], 4);
                        std::cerr << std::hex << "    q " << qa << " (native) vs " << qb << " (interpreter); fs lane bits " << sbits << ", ft lane bits " << tbits << std::dec << std::endl;
                    }
                }
            }
            std::cerr << "[vu1aot] fuzz: " << tested << " pairs tested, " << failures << " differ, " << skipped << " skipped" << std::endl;
            return failures;
        }
    }

    size_t runVu1AotChecks(const std::vector<Vu1Record> &records)
    {
        const char *mode = std::getenv("MMPC_VU1_AOT");
        if (mode == nullptr || mode[0] == '\0')
            return 0;
        if (std::strcmp(mode, "timing") == 0)
            return checkTiming(records);
        if (std::strcmp(mode, "fuzz") == 0)
            return fuzzOps(777u, std::getenv("MMPC_VU1_FUZZ") != nullptr ? static_cast<uint32_t>(std::atoi(std::getenv("MMPC_VU1_FUZZ"))) : 200000u);
        if (std::strcmp(mode, "run") == 0)
            return 0; // the replay itself goes through the programs (vu1test.cpp)
        std::cerr << "[vu1aot] unknown MMPC_VU1_AOT mode '" << mode << "'" << std::endl;
        return 1;
    }
}
