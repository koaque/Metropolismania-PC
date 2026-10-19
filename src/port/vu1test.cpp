// Replay of captured VU1 program runs (plan P4).
//
// PS2X_VU1_CAPTURE=<dir> (patches/0132) writes whole MSCAL and MSCNT runs of the VU1 interpreter: code, data memory,
// registers and flags before and after, the idle scheduler a resume starts from, the cycle count, and every packet XGKICK
// sent. A run is a function of that input (execute() clears the pipelines), so MMPC_VU1_TEST=<dir> can replay the records without the game: it runs each
// through the interpreter and compares the state, the data memory, the cycles and the packets with what was
// recorded. A record the replay does not reproduce would mean the capture is not faithful; a candidate executor
// (plan milestones M2 and M3) is judged by the same comparison. The process exits with the number of failing records.
// Records are game-derived: they stay in work/vu1_aot/ and are never committed.

#include "vu1test.h"

#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_vu1.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace port
{
    namespace
    {
        struct Vu1Record
        {
            std::string name;
            uint32_t kind = 0; // 0 MSCAL, 1 MSCNT
            uint32_t startPC = 0, top = 0, itop = 0, maxCycles = 0;
            uint64_t cyclesBefore = 0, cyclesAfter = 0;
            VU1State before{}, after{};
            VU1Interpreter::IdleScheduler scheduler{};
            std::vector<uint8_t> code, dataBefore, dataAfter;
            std::vector<std::vector<uint8_t>> packets;
        };

        bool loadRecord(const std::filesystem::path &path, Vu1Record &record)
        {
            std::ifstream in(path, std::ios::binary);
            char magic[4];
            in.read(magic, 4);
            if (!in || std::memcmp(magic, "VU1R", 4) != 0)
                return false;
            const auto u32 = [&] {
                uint32_t value = 0;
                in.read(reinterpret_cast<char *>(&value), sizeof(value));
                return value;
            };
            const auto u64 = [&] {
                uint64_t value = 0;
                in.read(reinterpret_cast<char *>(&value), sizeof(value));
                return value;
            };
            if (u32() != 2u)
                return false;
            record.name = path.filename().string();
            record.kind = u32();
            record.startPC = u32();
            record.top = u32();
            record.itop = u32();
            record.maxCycles = u32();
            const uint32_t codeSize = u32(), dataSize = u32(), stateSize = u32(), schedulerSize = u32();
            if (stateSize != sizeof(VU1State) || schedulerSize != sizeof(VU1Interpreter::IdleScheduler))
            {
                std::cerr << "[vu1test] " << record.name << ": state size " << stateSize << " (now " << sizeof(VU1State) << "), scheduler size "
                          << schedulerSize << " (now " << sizeof(VU1Interpreter::IdleScheduler) << "): the interpreter changed since the capture"
                          << std::endl;
                return false;
            }
            record.cyclesBefore = u64();
            record.cyclesAfter = u64();
            in.read(reinterpret_cast<char *>(&record.before), sizeof(VU1State));
            in.read(reinterpret_cast<char *>(&record.scheduler), sizeof(VU1Interpreter::IdleScheduler));
            record.code.resize(codeSize);
            in.read(reinterpret_cast<char *>(record.code.data()), codeSize);
            record.dataBefore.resize(dataSize);
            in.read(reinterpret_cast<char *>(record.dataBefore.data()), dataSize);
            in.read(reinterpret_cast<char *>(&record.after), sizeof(VU1State));
            record.dataAfter.resize(dataSize);
            in.read(reinterpret_cast<char *>(record.dataAfter.data()), dataSize);
            const uint32_t packets = u32();
            for (uint32_t i = 0; i < packets && in; ++i)
            {
                const uint32_t size = u32();
                record.packets.emplace_back(size);
                in.read(reinterpret_cast<char *>(record.packets.back().data()), size);
            }
            return static_cast<bool>(in);
        }

        std::string stateOffsetName(size_t offset)
        {
            const auto in = [&](size_t start, size_t size) { return offset >= start && offset < start + size; };
            char text[64];
            if (in(offsetof(VU1State, vf), sizeof(VU1State::vf)))
            {
                const size_t k = (offset - offsetof(VU1State, vf)) / sizeof(float);
                std::snprintf(text, sizeof(text), "vf[%zu][%zu]", k / 4, k % 4);
            }
            else if (in(offsetof(VU1State, vi), sizeof(VU1State::vi)))
                std::snprintf(text, sizeof(text), "vi[%zu]", (offset - offsetof(VU1State, vi)) / sizeof(int32_t));
            else if (in(offsetof(VU1State, acc), sizeof(VU1State::acc)))
                std::snprintf(text, sizeof(text), "acc[%zu]", (offset - offsetof(VU1State, acc)) / sizeof(float));
            else if (in(offsetof(VU1State, q), 4))
                return "q";
            else if (in(offsetof(VU1State, p), 4))
                return "p";
            else if (in(offsetof(VU1State, i), 4))
                return "i";
            else if (in(offsetof(VU1State, r), 4))
                return "r";
            else if (in(offsetof(VU1State, pc), 4))
                return "pc";
            else if (in(offsetof(VU1State, mac), 4))
                return "mac";
            else if (in(offsetof(VU1State, clip), 4))
                return "clip";
            else if (in(offsetof(VU1State, status), 4))
                return "status";
            else if (in(offsetof(VU1State, cycles), 8))
                return "cycles";
            else
                std::snprintf(text, sizeof(text), "byte %zu", offset);
            return text;
        }

        struct Outcome
        {
            VU1State state{};
            uint64_t cycles = 0;
            std::vector<uint8_t> data;
            std::vector<std::vector<uint8_t>> packets;
        };

        // The reference: the interpreter as the runtime runs it.
        Outcome runReference(VU1Interpreter &vu1, GS &gs, const Vu1Record &record, double &seconds)
        {
            Outcome outcome;
            outcome.data = record.dataBefore;
            std::vector<uint8_t> code = record.code;
            vu1.state() = record.before;
            vu1.setCycleCount(record.cyclesBefore);
            if (record.kind == 1)
                vu1.restoreIdleScheduler(record.scheduler);
            vu1.setPacketObserver([&outcome](const uint8_t *packet, uint32_t size) { outcome.packets.emplace_back(packet, packet + size); }, true);
            const auto begin = std::chrono::steady_clock::now();
            if (record.kind == 1)
                vu1.resume(code.data(), static_cast<uint32_t>(code.size()), outcome.data.data(), static_cast<uint32_t>(outcome.data.size()), gs,
                           nullptr, record.top, record.itop, record.maxCycles);
            else
                vu1.execute(code.data(), static_cast<uint32_t>(code.size()), outcome.data.data(), static_cast<uint32_t>(outcome.data.size()), gs,
                            nullptr, record.startPC, record.top, record.itop, record.maxCycles);
            seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
            vu1.setPacketObserver(nullptr, false);
            outcome.state = vu1.state();
            outcome.cycles = vu1.cycleCount();
            return outcome;
        }

        // Empty when the outcome is what was recorded, else what differs (first few).
        std::vector<std::string> compare(const Vu1Record &record, const Outcome &outcome)
        {
            std::vector<std::string> diffs;
            if (outcome.cycles != record.cyclesAfter)
                diffs.push_back("cycles " + std::to_string(record.cyclesAfter) + " recorded, " + std::to_string(outcome.cycles) + " replayed");
            const auto *a = reinterpret_cast<const uint8_t *>(&record.after);
            const auto *b = reinterpret_cast<const uint8_t *>(&outcome.state);
            size_t shown = 0, total = 0;
            for (size_t i = 0; i < sizeof(VU1State); ++i)
                if (a[i] != b[i])
                {
                    if (shown++ < 6)
                        diffs.push_back("state " + stateOffsetName(i) + " differs");
                    ++total;
                }
            if (total > shown)
                diffs.push_back("state: " + std::to_string(total) + " bytes differ in all");
            size_t dataDiffs = 0, firstData = 0;
            for (size_t i = 0; i < record.dataAfter.size(); ++i)
                if (record.dataAfter[i] != outcome.data[i])
                {
                    if (dataDiffs++ == 0)
                        firstData = i;
                }
            if (dataDiffs != 0)
                diffs.push_back("data memory: " + std::to_string(dataDiffs) + " bytes differ, first at 0x" + [&] {
                    char text[16];
                    std::snprintf(text, sizeof(text), "%zx", firstData);
                    return std::string(text);
                }());
            if (outcome.packets.size() != record.packets.size())
                diffs.push_back("packets: " + std::to_string(record.packets.size()) + " recorded, " + std::to_string(outcome.packets.size()) + " replayed");
            else
                for (size_t i = 0; i < record.packets.size(); ++i)
                    if (record.packets[i] != outcome.packets[i])
                    {
                        diffs.push_back("packet " + std::to_string(i) + " differs");
                        break;
                    }
            return diffs;
        }
    }

    void runVu1TestIfRequested()
    {
        const char *dirText = std::getenv("MMPC_VU1_TEST");
        if (dirText == nullptr || dirText[0] == '\0')
            return;
        std::vector<std::filesystem::path> files;
        for (const auto &entry : std::filesystem::directory_iterator(dirText))
            if (entry.path().extension() == ".vu1r")
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        GS gs; // never touched: the packets are discarded
        // The specialized upper-pipe executors against the general one on random instructions and registers.
        {
            const auto checker = std::make_unique<VU1Interpreter>(VU1Interpreter::Unit::VU1); // too large for the stack
            std::string firstFailure;
            const uint32_t cases = std::getenv("MMPC_VU1_FUZZ") != nullptr ? static_cast<uint32_t>(std::atoi(std::getenv("MMPC_VU1_FUZZ"))) : 200000u;
            const uint32_t differences = checker->checkFmacExecutors(12345u, cases, firstFailure);
            std::cerr << "[vu1test] fmac fuzz: " << cases << " instructions, " << differences << " differ" << std::endl;
            if (differences != 0)
                std::cerr << "    " << firstFailure << std::endl;
        }
        std::vector<Vu1Record> records;
        size_t failed = 0, unreadable = 0;
        for (const auto &file : files)
        {
            Vu1Record record;
            if (loadRecord(file, record))
                records.push_back(std::move(record));
            else
                ++unreadable;
        }
        // MMPC_VU1_TEST_REPEAT=N runs every record N times (only the first is compared), for a steadier speed figure.
        const int repeat = std::getenv("MMPC_VU1_TEST_REPEAT") != nullptr ? std::max(1, std::atoi(std::getenv("MMPC_VU1_TEST_REPEAT"))) : 1;
        // MMPC_VU1_IMMEDIATE=0 replays with queued vf/ACC writes (the model the unit tests check); the default is the runtime's.
        const bool immediateWrites = std::getenv("MMPC_VU1_IMMEDIATE") == nullptr || std::atoi(std::getenv("MMPC_VU1_IMMEDIATE")) != 0;
        double seconds = 0.0;
        uint64_t cycles = 0;
        // One interpreter per distinct code image, kept across records and passes as the runtime keeps its own: its decoded
        // pairs are then built once, as in the game. Everything a run depends on is set from the record.
        std::unordered_map<uint64_t, std::unique_ptr<VU1Interpreter>> interpreters;
        for (int pass = 0; pass < repeat; ++pass)
            for (const Vu1Record &record : records)
            {
                uint64_t codeKey = 1469598103934665603ull;
                for (const uint8_t byte : record.code)
                    codeKey = (codeKey ^ byte) * 1099511628211ull;
                std::unique_ptr<VU1Interpreter> &slot = interpreters[codeKey];
                if (!slot)
                {
                    slot = std::make_unique<VU1Interpreter>(VU1Interpreter::Unit::VU1);
                    slot->useStandaloneCode(true);
                    slot->setImmediateWrites(immediateWrites);
                }
                VU1Interpreter &vu1 = *slot;
                const Outcome outcome = runReference(vu1, gs, record, seconds);
                cycles += record.cyclesAfter - record.cyclesBefore;
                if (pass != 0)
                    continue;
                const std::vector<std::string> diffs = compare(record, outcome);
                if (!diffs.empty())
                {
                    ++failed;
                    std::cerr << "[vu1test] FAIL " << record.name << std::endl;
                    for (const std::string &diff : diffs)
                        std::cerr << "    " << diff << std::endl;
                }
            }
        std::cerr << "[vu1test] " << records.size() << " record(s), " << failed << " failed, " << unreadable << " unreadable; "
                  << (immediateWrites ? "immediate" : "queued") << " writes, "
                  << cycles << " cycles in " << seconds << " s = " << (seconds > 0.0 ? static_cast<double>(cycles) / seconds / 1e6 : 0.0)
                  << " Mcyc/s" << std::endl;
        std::exit(static_cast<int>(std::min<size_t>(failed + unreadable, 125)));
    }
}
