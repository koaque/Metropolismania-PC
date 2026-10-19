// Pre-timed VU1 programs: finding and running them for the interpreter's runs (plan P4, M3.4). See vu1aot.h.
//
// install() puts a hook on the interpreter (VU1Interpreter::setAotHook). For each run the hook finds the program for the
// entry PC whose code (the bytes of the pairs it reaches) is what is in the VU1 code memory now, building it the first
// time, and runs it on a snapshot of the state and the data memory. A run that meets something the model does not cover
// bails: the snapshot goes back and the interpreter runs as if nothing had happened.
//
//   PS2X_VU1_AOT=0       the hook is not installed
//   PS2X_VU1_VERIFY=1    every program run is also run by the interpreter from the same snapshot and the outcomes are
//                        compared (state, data memory, cycles, packets); the interpreter's outcome is the one kept
//   PS2X_GS_TIMING=1     a line of counters and times every 2 seconds (the GS statistics PS2X_GS_STATS=1 slow the game down)

#include "vu1aot.h"
#include "vu1aot_access.h"

#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <unordered_map>

namespace vu1aot
{
    namespace
    {
        constexpr uint32_t kCodeSize = 0x4000;
        constexpr uint32_t kDataSize = 0x4000;
        constexpr size_t kMaxVersionsPerEntry = 16;

        struct Counters
        {
            uint64_t runs = 0;       // runs a program completed
            uint64_t cycles = 0;
            double seconds = 0.0;
            double emitSeconds = 0.0; // sending the XGKICK packets to the GS
            uint64_t bails = 0;      // programs that bailed (the interpreter ran instead)
            uint64_t notIdle = 0;    // runs that did not start with idle pipelines
            uint64_t noProgram = 0;  // no usable program for the entry (not built, unsupported, too many versions)
            uint64_t built = 0;
            uint64_t verified = 0;
            uint64_t mismatches = 0;
        };
        Counters g_counters;
        std::chrono::steady_clock::time_point g_lastLog = std::chrono::steady_clock::now();

        bool statsOn()
        {
            static const bool on = []() {
                const char *value = std::getenv("PS2X_GS_TIMING");
                return value != nullptr && value[0] == '1';
            }();
            return on;
        }

        bool verifyOn()
        {
            static const bool on = []() {
                const char *value = std::getenv("PS2X_VU1_VERIFY");
                return value != nullptr && value[0] == '1';
            }();
            return on;
        }

        uint64_t hashRange(const uint8_t *code, uint32_t lo, uint32_t hi)
        {
            uint64_t hash = 1469598103934665603ull;
            for (uint32_t i = lo; i < hi; ++i)
                hash = (hash ^ code[i]) * 1099511628211ull;
            return hash;
        }

        struct Program
        {
            Graph graph;
            uint64_t hash = 0;
            uint64_t runs = 0, bails = 0;
        };

        struct Entry
        {
            uint64_t generation = ~0ull;
            Program *active = nullptr; // for `generation`; null when none is usable
            std::vector<std::unique_ptr<Program>> versions;
        };

        class Runtime
        {
        public:
            bool onRun(VU1Interpreter &vu, const VU1Interpreter::AotRun &a)
            {
                if (a.codeSize != kCodeSize || a.dataSize != kDataSize)
                    return false;
                if (!Vu1AotAccess::startsIdle(vu))
                {
                    ++g_counters.notIdle;
                    return false;
                }
                Program *program = find(vu, a);
                if (program == nullptr)
                {
                    ++g_counters.noProgram;
                    return false;
                }

                const auto begin = statsOn() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                Vu1AotAccess::save(vu, a.data, a.dataSize, m_snapshot);
                m_packets.clear();
                const RunResult result = Vu1AotAccess::run(vu, program->graph, a, m_packets);
                if (result == RunResult::Bail)
                {
                    Vu1AotAccess::restore(vu, a.data, m_snapshot);
                    ++program->bails;
                    ++g_counters.bails;
                    return false;
                }
                ++program->runs;
                ++g_counters.runs;
                g_counters.cycles += Vu1AotAccess::cycle(vu) - m_snapshot.cycle;
                if (statsOn())
                    g_counters.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();

                if (verifyOn())
                    verify(vu, a, *program);
                else
                {
                    const auto emitBegin = statsOn() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                    Vu1AotAccess::emit(vu, m_packets);
                    if (statsOn())
                        g_counters.emitSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - emitBegin).count();
                }
                if (statsOn())
                    log();
                return true;
            }

        private:
            std::unordered_map<uint32_t, Entry> m_entries;
            Vu1AotAccess::Snapshot m_snapshot;
            std::vector<std::vector<uint8_t>> m_packets;

            Program *find(VU1Interpreter &vu, const VU1Interpreter::AotRun &a)
            {
                const uint32_t entryPc = vu.state().pc;
                Entry &entry = m_entries[entryPc];
                const bool tracked = a.memory != nullptr && a.code == a.memory->getVU1Code();
                const uint64_t generation = tracked ? a.memory->getVU1CodeGeneration() : ~0ull;
                if (tracked && entry.generation == generation)
                    return entry.active;
                entry.active = nullptr;
                for (const auto &version : entry.versions)
                    if (hashRange(a.code, version->graph.codeLo, version->graph.codeHi) == version->hash)
                    {
                        entry.active = version.get();
                        break;
                    }
                if (entry.active == nullptr && entry.versions.size() < kMaxVersionsPerEntry)
                {
                    auto program = std::make_unique<Program>();
                    program->graph = buildGraph(vu, a.code, a.codeSize, entryPc);
                    if (program->graph.supported)
                        Vu1AotAccess::prepare(program->graph);
                    program->hash = hashRange(a.code, program->graph.codeLo, program->graph.codeHi);
                    ++g_counters.built;
                    entry.versions.push_back(std::move(program));
                    entry.active = entry.versions.back().get();
                }
                if (entry.active != nullptr && !entry.active->graph.supported)
                    entry.active = nullptr;
                entry.generation = generation;
                return entry.active;
            }

            // The program has run (its outcome is on `vu`). Runs the interpreter from the same snapshot and compares.
            void verify(VU1Interpreter &vu, const VU1Interpreter::AotRun &a, Program &program)
            {
                Vu1AotAccess::Snapshot mine, reference;
                Vu1AotAccess::save(vu, a.data, a.dataSize, mine);
                const std::vector<std::vector<uint8_t>> minePackets = m_packets;
                Vu1AotAccess::restore(vu, a.data, m_snapshot);
                m_packets.clear();
                Vu1AotAccess::interpret(vu, a, m_packets);
                Vu1AotAccess::save(vu, a.data, a.dataSize, reference);
                ++g_counters.verified;
                const std::string problem = Vu1AotAccess::diff(mine, minePackets, reference, m_packets);
                if (!problem.empty())
                {
                    ++g_counters.mismatches;
                    if (g_counters.mismatches <= 10)
                    {
                        std::cerr << "[vu1aot] MISMATCH entry 0x" << std::hex << program.graph.entryPc << std::dec << ": " << problem << std::endl;
                        // Where the model and the interpreter part ways: the interpreter's pair trace from the same snapshot.
                        Vu1AotAccess::restore(vu, a.data, m_snapshot);
                        std::vector<std::pair<uint32_t, uint64_t>> trace;
                        std::vector<uint32_t> packetBytes;
                        std::vector<std::vector<uint8_t>> scratch;
                        vu.setPairTrace(&trace);
                        Vu1AotAccess::interpret(vu, a, scratch);
                        vu.setPairTrace(nullptr);
                        for (const auto &packet : scratch)
                            packetBytes.push_back(static_cast<uint32_t>(packet.size()));
                        std::cerr << "    trace check: " << checkTrace(program.graph, trace, m_snapshot.cycle, Vu1AotAccess::cycle(vu) - m_snapshot.cycle, packetBytes)
                                  << " (" << trace.size() << " pairs, " << scratch.size() << " packet(s))" << std::endl;
                        m_packets = scratch;
                    }
                }
                Vu1AotAccess::emit(vu, m_packets);
            }

            void log()
            {
                const auto now = std::chrono::steady_clock::now();
                if (now - g_lastLog < std::chrono::seconds(2))
                    return;
                g_lastLog = now;
                std::cerr << statsText() << std::endl;
                g_counters = Counters{};
            }
        };
    }

    void install(VU1Interpreter &vu)
    {
        const char *off = std::getenv("PS2X_VU1_AOT");
        if (off != nullptr && off[0] == '0')
            return;
        auto runtime = std::make_shared<Runtime>();
        vu.setAotHook([runtime](VU1Interpreter &interpreter, const VU1Interpreter::AotRun &run) { return runtime->onRun(interpreter, run); });
    }

    std::string statsText()
    {
        char text[400];
        std::snprintf(text, sizeof(text),
                      "[vu1aot:stats] runs=%llu cycles=%llu aot_ms=%llu Mcyc/s(aot)=%.1f emit_ms=%llu bails=%llu not_idle=%llu no_program=%llu "
                      "built=%llu verified=%llu mismatches=%llu",
                      static_cast<unsigned long long>(g_counters.runs), static_cast<unsigned long long>(g_counters.cycles),
                      static_cast<unsigned long long>(g_counters.seconds * 1000.0),
                      g_counters.seconds > 0.0 ? g_counters.cycles / g_counters.seconds / 1e6 : 0.0,
                      static_cast<unsigned long long>(g_counters.emitSeconds * 1000.0), static_cast<unsigned long long>(g_counters.bails),
                      static_cast<unsigned long long>(g_counters.notIdle), static_cast<unsigned long long>(g_counters.noProgram),
                      static_cast<unsigned long long>(g_counters.built), static_cast<unsigned long long>(g_counters.verified),
                      static_cast<unsigned long long>(g_counters.mismatches));
        return text;
    }
}
