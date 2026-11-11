// Differential test against the user's relocated MODMIDI, using the existing
// R3000A interpreter. No binary, decompiled body or original capture is embedded.
#include "modmidi_tempo.h"
#include "ps2x/iop/iop_native.h"
#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_memory.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using ps2x::iop::detail::IopCpuCore;
using ps2x::iop::detail::IopCpuState;
using ps2x::iop::detail::IopMemory;

namespace
{
    constexpr uint32_t kContext = 0x120000;
    constexpr uint32_t kGroup = 0x120100;
    constexpr uint32_t kDescriptors = 0x120200;
    constexpr uint32_t kEnvironment = 0x121000;
    constexpr uint32_t kStack = 0x1F0000;
    constexpr uint32_t kReturn = 0x100000;

    struct Context final : ps2x::iop::NativeContext
    {
        IopMemory &memory;
        IopCpuState &cpu;
        uint32_t charged = 0;
        bool jumped = false;
        Context(IopMemory &m, IopCpuState &c) : memory(m), cpu(c) {}
        uint8_t *ram() override { return memory.ramData(); }
        uint32_t reg(unsigned i) const override { return cpu.gpr.at(i); }
        void setReg(unsigned i, uint32_t value) override { if (i) cpu.gpr.at(i) = value; }
        uint32_t callGuest(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override
        {
            throw std::runtime_error("unexpected guest call");
        }
        void argumentCount(unsigned) override { throw std::runtime_error("unexpected import"); }
        void stackArguments(const uint32_t *, unsigned) override { throw std::runtime_error("unexpected stack arguments"); }
        void jump(uint32_t address) override { cpu.pc = address; cpu.branchPending = false; jumped = true; }
        void charge(uint32_t count) override { charged += count; }
    };

    uint32_t runOriginal(IopMemory &memory, IopCpuState &cpu)
    {
        IopCpuCore core(memory);
        uint32_t count = 0;
        while (cpu.pc != kReturn && !cpu.exception && !cpu.stopped && count < 500)
        {
            (void)core.executeInstruction(cpu);
            ++count;
        }
        if (count == 500)
            throw std::runtime_error("original did not return or trap");
        return count;
    }

    struct Case
    {
        uint32_t context = kContext;
        uint32_t port = 0;
        uint32_t count = 8;
        uint32_t sequence = 0;
        uint32_t period = 500000;
        uint32_t divisor = 480;
        uint32_t scale = 256;
        uint32_t tempo = 120;
        uint32_t alias = 0;
        uint32_t group = kGroup;
        uint32_t descriptors = kDescriptors;
        uint32_t environment = kEnvironment;
        uint32_t stack = kStack;
    };

    struct Stats { unsigned calls = 0, fallback = 0, traps = 0; };

    void check(const std::vector<uint8_t> &image, const std::vector<uint8_t> &noise,
               IopMemory &reference, IopMemory &native, const Case &test, bool setter,
               std::mt19937 &random, Stats &stats)
    {
        std::memcpy(reference.ramData(), noise.data(), noise.size());
        if (!reference.writeRam(iop::midi::kModuleBase, image.data(), image.size()))
            throw std::runtime_error("image does not fit RAM");
        reference.write32(kContext + 4, test.group | test.alias);
        reference.write32(kGroup, test.count);
        reference.write32(kGroup + 4, test.descriptors | test.alias);
        for (unsigned port = 0; port < 4; ++port)
            reference.write32(kDescriptors + (port * 2 + 1) * 8 + 4, test.environment | test.alias);
        reference.write32(kEnvironment + 4, test.sequence);
        reference.write32(kEnvironment + 0x7C, test.period);
        reference.write32(kEnvironment + 0x78, test.divisor);
        reference.write32(kEnvironment + 0x80, test.scale);
        std::memcpy(native.ramData(), reference.ramData(), IopMemory::RamSize);

        IopCpuState original;
        for (uint32_t &reg : original.gpr) reg = random();
        original.gpr[0] = 0;
        original.gpr[4] = test.context == 0 ? 0 : test.context | test.alias;
        original.gpr[5] = test.port;
        original.gpr[6] = test.tempo;
        original.gpr[28] = iop::midi::kModuleBase + 48096;
        original.gpr[29] = test.stack;
        original.gpr[31] = kReturn;
        original.pc = iop::midi::kModuleBase + (setter ? iop::midi::kSetTempoOffset : iop::midi::kGetTempoOffset);
        IopCpuState replacement = original;
        const uint32_t originalCost = runOriginal(reference, original);
        Context context(native, replacement);
        (setter ? iop::midi::setAbsoluteTempoNative : iop::midi::getAbsoluteTempoNative)(context);
        if (context.jumped)
        {
            ++stats.fallback;
            context.charged += runOriginal(native, replacement);
        }
        else
        {
            replacement.pc = replacement.gpr[31];
            replacement.branchPending = false;
        }
        ++stats.calls;
        stats.traps += original.exception ? 1u : 0u;
        auto fail = [&](const char *reason)
        {
            char message[400];
            std::snprintf(message, sizeof(message),
                          "%s %s case=%u context=%08X port=%08X period=%u tempo=%08X sequence=%08X costs=%u/%u fallback=%d",
                          setter ? "set" : "get", reason, stats.calls, test.context, test.port, test.period, test.tempo,
                          test.sequence, context.charged, originalCost, context.jumped);
            throw std::runtime_error(message);
        };
        if (context.charged != originalCost) fail("instruction cost");
        if (replacement.pc != original.pc || replacement.exception != original.exception || replacement.stopped != original.stopped)
            fail("control flow");
        constexpr unsigned compared[] = {2, 16, 17, 18, 19, 20, 21, 22, 23, 28, 29, 30, 31};
        for (unsigned reg : compared)
            if (replacement.gpr[reg] != original.gpr[reg]) fail("ABI register");
        if (context.jumped)
        {
            if (replacement.gpr != original.gpr || replacement.cop0 != original.cop0 ||
                replacement.hi != original.hi || replacement.lo != original.lo ||
                replacement.pendingLoad != original.pendingLoad || replacement.branchPending != original.branchPending)
                fail("fallback processor state");
        }
        // Follow the existing verifier's dead-stack boundary. Fallback is fully exact.
        const uint32_t deadBegin = kStack + 16 - 0x800, deadEnd = kStack + 16;
        const bool memoryDiffers = context.jumped
            ? std::memcmp(native.ramData(), reference.ramData(), IopMemory::RamSize) != 0
            : std::memcmp(native.ramData(), reference.ramData(), deadBegin) != 0 ||
              std::memcmp(native.ramData() + deadEnd, reference.ramData() + deadEnd, IopMemory::RamSize - deadEnd) != 0;
        if (memoryDiffers)
        {
            for (uint32_t pos = 0; pos < IopMemory::RamSize; ++pos)
            {
                if (!context.jumped && pos >= deadBegin && pos < deadEnd) continue;
                if (native.ramData()[pos] != reference.ramData()[pos])
                {
                    std::fprintf(stderr, "RAM difference at %08X\n", pos);
                    fail("memory");
                }
            }
        }
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("usage: iop_midi_tempo_test relocated-module.bin");
        std::ifstream file(argv[1], std::ios::binary);
        if (!file) throw std::runtime_error("cannot open module image");
        std::vector<uint8_t> image{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        std::mt19937 random(0x4D494449);
        std::vector<uint8_t> noise(IopMemory::RamSize);
        for (uint8_t &byte : noise) byte = static_cast<uint8_t>(random());
        auto original = std::make_unique<IopMemory>();
        auto native = std::make_unique<IopMemory>();
        Stats stats;
        auto both = [&](const Case &test)
        {
            check(image, noise, *original, *native, test, false, random, stats);
            check(image, noise, *original, *native, test, true, random, stats);
        };
        both(Case{});
        for (uint32_t tempo = 0; tempo < 256; ++tempo)
        {
            Case test; test.tempo = tempo; test.period = tempo ? 60000000u / tempo : 0; both(test);
            test.tempo |= 0xFFFF0000u; both(test);
        }
        for (uint32_t period : {0u, 1u, 235293u, 235294u, 235295u, 3000000u, 3000001u, 0xFFFFFFFFu})
        {
            Case test; test.period = period; both(test);
        }
        for (uint32_t port : {0u, 1u, 2u, 3u, 4u, 0xFFFFFFFFu, 0x80000000u})
        {
            Case test; test.port = port; both(test);
            test.count = 0; both(test);
        }
        for (uint32_t alias : {0u, 0x80000000u, 0xA0000000u})
        {
            Case test; test.alias = alias; both(test);
            test.context = 0; both(test);
        }
        for (uint32_t divisor : {1u, 2u, 127u, 128u, 255u, 256u, 480u, 0xFFFFFFFFu})
            for (uint32_t scale : {1u, 2u, 127u, 128u, 255u, 256u, 480u, 0xFFFFFFFFu})
            {
                Case test; test.divisor = divisor; test.scale = scale; test.tempo = 1; both(test);
            }
        for (unsigned kind = 0; kind < 15; ++kind)
        {
            Case test;
            if (kind == 0) test.sequence = 0xFFFFFFFFu;
            if (kind == 1) test.divisor = 0;
            if (kind == 2) test.scale = 0;
            if (kind == 3) test.context += 1;
            if (kind == 4) test.group += 1;
            if (kind == 5) test.descriptors += 1;
            if (kind == 6) test.environment += 1;
            if (kind == 7) test.context = kStack - 12;
            if (kind == 8) test.group = kStack - 28;
            if (kind == 9) test.descriptors = kStack - 20;
            if (kind == 10) test.environment = kStack - 0x7C;
            if (kind == 11) test.stack += 1;
            if (kind == 12) test.context = 0x1F801000;
            if (kind == 13) test.group = IopMemory::RamSize - 1;
            if (kind == 14) test.environment = IopMemory::RamSize - 0x7C;
            both(test);
        }
        for (unsigned trial = 0; trial < 4096; ++trial)
        {
            Case test;
            test.tempo = random(); test.period = random(); test.divisor = random(); test.scale = random();
            test.port = random() % 5; test.sequence = random();
            test.alias = (trial % 3 == 0) ? 0x80000000u : (trial % 3 == 1 ? 0xA0000000u : 0u);
            both(test);
        }
        std::printf("PASS %u calls, %u original fallbacks, %u original exceptions; RAM, ABI and instruction counts agree\n",
                    stats.calls, stats.fallback, stats.traps);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
