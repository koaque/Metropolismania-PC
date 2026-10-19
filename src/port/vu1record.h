// A captured VU1 program run (PS2X_VU1_CAPTURE, patches/0132 and 0134) and its loader, shared by the replay (vu1test.cpp)
// and the pre-timed program tests (vu1aot_test.cpp). Records are game-derived: they stay in work/vu1_aot/ and are never
// committed.

#pragma once

#include "runtime/ps2_vu1.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace port
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

    inline bool loadVu1Record(const std::filesystem::path &path, Vu1Record &record)
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
}
