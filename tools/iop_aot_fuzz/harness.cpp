// A differential test of the translated code against the interpreter on random programs (tools/iop_aot_fuzz.py builds and runs it).
//
// The program is a flat image the translator has turned into C++ (g_aot_run_FUZZ). Each trial starts both sides from the same random
// registers, memory and status register, then runs the translated side in runs of random length (so a run ends, and the next begins, at
// any instruction: between a branch and its delay slot, after a load, ...) and, after each run, steps the interpreter the same number of
// instructions and compares everything: the processor, all of RAM and the ownership flags.

#include "ps2x/iop/iop_aot.h"

#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_memory.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

using namespace ps2x::iop;
using namespace ps2x::iop::detail;

extern AotRun g_aot_run_FUZZ;

static_assert(sizeof(AotCpu) == sizeof(IopCpuState), "AotCpu mirrors IopCpuState");

namespace
{
    constexpr uint32_t kData = 0x180000u; // the area the programs' loads and stores use

    IopMemory *g_memory = nullptr; // the translated side's memory
    IopCpuCore *g_core = nullptr;

    uint32_t read8(AotContext &x, uint32_t a)
    {
        if (g_memory->isHardwareAddress(a))
        {
            x.stop = true;
            ++x.hardware;
        }
        return g_memory->read8(a);
    }
    uint32_t read16(AotContext &x, uint32_t a)
    {
        if (g_memory->isHardwareAddress(a))
        {
            x.stop = true;
            ++x.hardware;
        }
        return g_memory->read16(a);
    }
    uint32_t read32(AotContext &x, uint32_t a)
    {
        if (g_memory->isHardwareAddress(a))
        {
            x.stop = true;
            ++x.hardware;
        }
        return g_memory->read32(a);
    }
    void write8(AotContext &x, uint32_t a, uint32_t v)
    {
        if (g_memory->isHardwareAddress(a))
        {
            x.stop = true;
            ++x.hardware;
        }
        g_memory->write8(a, static_cast<uint8_t>(v));
    }
    void write16(AotContext &x, uint32_t a, uint32_t v)
    {
        if (g_memory->isHardwareAddress(a))
        {
            x.stop = true;
            ++x.hardware;
        }
        g_memory->write16(a, static_cast<uint16_t>(v));
    }
    void write32(AotContext &x, uint32_t a, uint32_t v)
    {
        if (g_memory->isHardwareAddress(a))
        {
            x.stop = true;
            ++x.hardware;
        }
        g_memory->write32(a, v);
    }
    void raise(AotContext &, AotCpu &cpu, uint32_t code, uint32_t pc, uint32_t delaySlot, uint32_t bad, uint32_t hasBad)
    {
        g_core->raiseException(reinterpret_cast<IopCpuState &>(cpu), code, pc, delaySlot != 0u, hasBad != 0u ? std::optional<uint32_t>(bad) : std::nullopt);
    }

    std::string hex(uint32_t value)
    {
        char text[16];
        std::snprintf(text, sizeof(text), "0x%08X", value);
        return text;
    }

    std::string compare(const IopCpuState &a, const IopCpuState &b, const IopMemory &ma, const IopMemory &mb)
    {
        std::string out;
        for (unsigned i = 0; i < 32u; ++i)
            if (a.gpr[i] != b.gpr[i])
                out += " r" + std::to_string(i) + " " + hex(b.gpr[i]) + "/" + hex(a.gpr[i]);
        if (a.hi != b.hi)
            out += " hi " + hex(b.hi) + "/" + hex(a.hi);
        if (a.lo != b.lo)
            out += " lo " + hex(b.lo) + "/" + hex(a.lo);
        if (a.pc != b.pc)
            out += " pc " + hex(b.pc) + "/" + hex(a.pc);
        for (unsigned i = 0; i < 32u; ++i)
            if (a.cop0[i] != b.cop0[i])
                out += " cop0[" + std::to_string(i) + "] " + hex(b.cop0[i]) + "/" + hex(a.cop0[i]);
        if (a.pendingLoad != b.pendingLoad || (a.pendingLoad && (a.pendingLoadReg != b.pendingLoadReg || a.pendingLoadValue != b.pendingLoadValue)))
            out += " pendingLoad " + std::to_string(b.pendingLoad) + ":" + std::to_string(b.pendingLoadReg) + ":" + hex(b.pendingLoadValue) + "/" +
                   std::to_string(a.pendingLoad) + ":" + std::to_string(a.pendingLoadReg) + ":" + hex(a.pendingLoadValue);
        // (the target means something only while a branch is waiting for its delay slot)
        if (a.branchPending != b.branchPending || (a.branchPending && a.branchTarget != b.branchTarget))
            out += " branch " + std::to_string(b.branchPending) + ":" + hex(b.branchTarget) + "/" + std::to_string(a.branchPending) + ":" + hex(a.branchTarget);
        if (a.exception != b.exception)
            out += " exception " + std::to_string(b.exception) + "/" + std::to_string(a.exception);
        const auto ra = ma.ram();
        const auto rb = mb.ram();
        if (std::memcmp(ra.data(), rb.data(), ra.size()) != 0)
        {
            for (size_t i = 0; i < ra.size(); ++i)
                if (ra[i] != rb[i])
                {
                    out += " ram[" + hex(static_cast<uint32_t>(i)) + "] " + hex(rb[i]) + "/" + hex(ra[i]);
                    break;
                }
        }
        if (std::memcmp(const_cast<IopMemory &>(ma).ownedData(), const_cast<IopMemory &>(mb).ownedData(), IopMemory::RamSize) != 0)
            out += " owned flags differ";
        return out;
    }
}

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        std::fprintf(stderr, "usage: harness IMAGE BASE_HEX TRIALS SEED\n");
        return 2;
    }
    std::ifstream file(argv[1], std::ios::binary);
    const std::vector<char> image((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const uint32_t base = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 16));
    const int trials = std::atoi(argv[3]);
    const uint32_t seed = static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10));
    const uint32_t words = static_cast<uint32_t>((image.size() + 3) / 4);

    auto memoryA = std::make_unique<IopMemory>(); // the interpreter's
    auto memoryB = std::make_unique<IopMemory>(); // the translation's
    IopCpuCore coreA(*memoryA);
    IopCpuCore coreB(*memoryB);
    g_memory = memoryB.get();
    g_core = &coreB;

    std::vector<uint8_t> nativeMask(words, 0u);
    AotContext context;
    context.native = nativeMask.data();
    context.read8 = &read8;
    context.read16 = &read16;
    context.read32 = &read32;
    context.write8 = &write8;
    context.write16 = &write16;
    context.write32 = &write32;
    context.raise = &raise;

    std::mt19937 rng(seed);
    uint64_t instructions = 0;
    uint64_t runs = 0;
    uint64_t refused = 0;
    uint64_t selfModified = 0;
    for (int trial = 0; trial < trials; ++trial)
    {
        memoryA->reset();
        memoryB->reset();
        for (uint32_t i = 0; i < words; ++i)
        {
            uint32_t word;
            std::memcpy(&word, image.data() + i * 4, sizeof(word));
            memoryA->write32(base + i * 4, word);
            memoryB->write32(base + i * 4, word);
        }
        for (uint32_t i = 0; i < 0x300; ++i)
        {
            const uint8_t byte = static_cast<uint8_t>(rng());
            memoryA->write8(kData + i, byte);
            memoryB->write8(kData + i, byte);
        }
        IopCpuState cpuA{};
        for (unsigned r = 1; r < 32u; ++r)
            cpuA.gpr[r] = rng();
        for (unsigned r = 16; r < 20u; ++r)
            cpuA.gpr[r] = kData + 0x100u + ((rng() % 4u == 0u) ? (rng() % 4u) : 0u) + (r - 16u) * 0x10u; // the base registers of loads and stores
        cpuA.hi = rng();
        cpuA.lo = rng();
        cpuA.cop0[12] = rng() & 0x3Fu;
        cpuA.cop0[13] = rng() & 0x7Cu;
        cpuA.pc = base;
        context.ram = memoryB->ramData();
        context.owned = memoryB->ownedData();
        IopCpuState cpuB = cpuA;
        uint64_t total = 0;
        std::string last;
        while (cpuB.pc >= base && cpuB.pc < base + words * 4u && total < 20000u)
        {
            const uint32_t limit = (rng() % 3u == 0u) ? 1u + rng() % 3u : 1u + rng() % 40u;
            context.stop = false;
            const uint32_t n = g_aot_run_FUZZ(context, reinterpret_cast<AotCpu &>(cpuB), limit);
            ++runs;
            if (n == 0u)
            {
                ++refused;
                break;
            }
            std::string trace;
            const IopCpuState before = cpuA;
            for (uint32_t i = 0; i < n; ++i)
            {
                char text[40];
                std::snprintf(text, sizeof(text), " %08X:%08X", cpuA.pc, memoryA->read32(cpuA.pc));
                trace += text;
                coreA.executeInstruction(cpuA);
            }
            total += n;
            instructions += n;
            // a store into the program itself (a random base register can aim there): the translation is of the original code, so such a trial
            // is not a fair comparison; it ends here
            if (std::memcmp(memoryB->ram().data() + base, image.data(), image.size()) != 0)
            {
                ++selfModified;
                break;
            }
            const std::string diff = compare(cpuA, cpuB, *memoryA, *memoryB);
            if (!diff.empty())
            {
                std::printf("MISMATCH trial %d seed %u after %llu instruction(s) (run of %u from %08X, entry state: pendingLoad %d branchPending %d target %08X):%s\n  executed:%s\n",
                            trial, seed, static_cast<unsigned long long>(total), n, before.pc, before.pendingLoad, before.branchPending, before.branchTarget, diff.c_str(),
                            trace.c_str());
                return 1;
            }
            last = hex(cpuA.pc);
            if (cpuA.exception)
                break;
        }
    }
    std::printf("ok: %d trial(s), %llu run(s), %llu instruction(s), %llu run(s) refused, %llu trial(s) ended by a store into the code\n", trials,
                static_cast<unsigned long long>(runs), static_cast<unsigned long long>(instructions), static_cast<unsigned long long>(refused),
                static_cast<unsigned long long>(selfModified));
    return 0;
}
