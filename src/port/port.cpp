// Registry of ported functions and their installation into the runtime (see abi.h).

#include "port.h"

#include "abi.h"
#include "difftest.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace port
{
    namespace
    {
        std::vector<Ported> &registry()
        {
            static std::vector<Ported> entries;
            return entries;
        }

        // MMPC_PORT_DISABLE=0xADDR,0xADDR,... (or "all") keeps the generated function at those
        // addresses, to bisect a problem down to one ported function.
        bool disabled(uint32_t address)
        {
            static const std::unordered_set<uint32_t> list = []()
            {
                std::unordered_set<uint32_t> out;
                const char *value = std::getenv("MMPC_PORT_DISABLE");
                if (!value)
                    return out;
                if (std::strcmp(value, "all") == 0)
                {
                    out.insert(0xFFFFFFFFu);
                    return out;
                }
                std::string text(value);
                size_t start = 0;
                while (start < text.size())
                {
                    const size_t end = text.find(',', start);
                    const std::string item = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                    if (!item.empty())
                        out.insert(static_cast<uint32_t>(std::stoul(item, nullptr, 0)));
                    if (end == std::string::npos)
                        break;
                    start = end + 1;
                }
                return out;
            }();
            return list.count(0xFFFFFFFFu) != 0 || list.count(address) != 0;
        }
    }

    const std::vector<Ported> &ported() { return registry(); }

    const char *nameOf(uint32_t address)
    {
        for (const Ported &p : registry())
            if (p.address == address)
                return p.name;
        return "?";
    }

    void registerPorted(uint32_t address, const char *name, GuestFn wrapper, ResultKind result, MutateFn mutate)
    {
        registry().push_back({address, name, wrapper, result, nullptr, mutate});
    }

    namespace
    {
        uint32_t nextRandom(uint32_t &state)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }
    }

    void fuzzFloat(float &value, uint32_t &state)
    {
        switch (nextRandom(state) % 10)
        {
        case 0:
        case 1:
        case 2: value = 0.0f; break;
        case 3: value = -value; break;
        case 4: value = 1.0f; break;
        case 5: value = -1.0f; break;
        case 6: value = static_cast<float>(static_cast<int>(nextRandom(state) % 2001) - 1000) / 8.0f; break;
        case 7: value = value * 2.0f; break;
        case 8: value = static_cast<float>(static_cast<int>(nextRandom(state) % 129) - 64); break;
        default: break; // as recorded
        }
    }

    int64_t fuzzInt(int64_t value, int bytes, bool isSigned, uint32_t &state)
    {
        int64_t v = value;
        switch (nextRandom(state) % 10)
        {
        case 0: v = 0; break;
        case 1: v = 1; break;
        case 2: v = -1; break;
        case 3: v = value + 1; break;
        case 4: v = value - 1; break;
        case 5: v = nextRandom(state) % 256; break;
        case 6: v = nextRandom(state) % 65536; break;
        case 7: v = static_cast<int32_t>(nextRandom(state)); break;
        case 8: v = (nextRandom(state) & 1) ? 0x80 : 0xFF; break;
        default: break; // as recorded
        }
        // Keep it a value of the parameter's type, as a caller would pass it.
        if (bytes < 8)
        {
            const int bits = bytes * 8;
            const uint64_t mask = (uint64_t{1} << bits) - 1;
            uint64_t u = static_cast<uint64_t>(v) & mask;
            if (isSigned && (u >> (bits - 1)) != 0)
                u |= ~mask;
            v = static_cast<int64_t>(u);
        }
        return v;
    }

    namespace
    {
        bool g_faultsThrow = false;

        [[noreturn]] void fault(const std::string &message)
        {
            if (g_faultsThrow)
                throw Fault(message);
            std::cerr << "[port] fatal: " << message << std::endl;
            std::abort();
        }

        std::string hex(uint32_t value)
        {
            char text[16];
            std::snprintf(text, sizeof(text), "0x%08X", value);
            return text;
        }
    }

    void setFaultsThrow(bool on) { g_faultsThrow = on; }

    void fiberEnter(GuestFiber &fiber)
    {
        CallState *own = reinterpret_cast<CallState *>(fiber.user);
        CallState *dispatcher = own + 1;
        *dispatcher = g_call;
        g_call = *own;
    }

    void fiberLeave(GuestFiber &fiber)
    {
        CallState *own = reinterpret_cast<CallState *>(fiber.user);
        CallState *dispatcher = own + 1;
        *own = g_call;
        g_call = *dispatcher;
    }

    void enterPorted(PS2Runtime *runtime) { runtime->eeScheduler().enterNoPreempt(); }
    void leavePorted(PS2Runtime *runtime) { runtime->eeScheduler().leaveNoPreempt(); }

    void fatal(const char *what, uint32_t address)
    {
        fault(std::string(what) + " (" + hex(address) + ") in " + nameOf(g_call.function));
    }

    // Kernel segments 2 and 3 (0xC0000000 and up) map to nothing in the runtime: reads give 0 and writes
    // are dropped. Ported code gets a scratch area for them that is cleared on every use, which behaves
    // the same for the small reads and writes game code makes. Real hardware (I/O, VU, GS, BIOS) is an
    // error: ported code must reach it through the runtime, as the original does.
    uint8_t *special(uint32_t address)
    {
        if (address < 0xC0000000u)
            fatal("guest pointer to a hardware address", address);
        alignas(16) static uint8_t unmapped[256];
        std::memset(unmapped, 0, sizeof(unmapped));
        static int reported = 0;
        if (reported < 16)
        {
            ++reported;
            std::cerr << "[port] " << nameOf(g_call.function) << " touched unmapped address " << hex(address)
                      << " (reads 0, writes dropped, as in the runtime)" << std::endl;
        }
        return unmapped;
    }

    void calleeDidNotReturn(uint32_t address, uint32_t pc)
    {
        fault("guest function " + hex(address) + " called from ported code did not return (pc " + hex(pc) + ")");
    }

    void reachedThreadSwitch(uint32_t address)
    {
        fault(std::string("ported function ") + nameOf(address) + " (" + hex(address) +
              ") reached a thread switch; blocking code waits for native threading (plan P6)");
    }

    void install(PS2Runtime &runtime)
    {
        runtime.eeScheduler().setFiberHooks({&fiberEnter, &fiberLeave});
        g_rdram = runtime.memory().getRDRAM();
        g_scratchpad = runtime.memory().getScratchpad();
        // A capture records the original code, so nothing is replaced while one runs.
        const bool capture = captureRequested();
        size_t installed = 0, skipped = 0;
        for (Ported &p : registry())
        {
            p.original = runtime.lookupFunction(p.address);
            if (capture || disabled(p.address))
            {
                ++skipped;
                continue;
            }
            if (!runtime.registerFunction(p.address, p.wrapper))
                fatal("cannot register ported function", p.address);
            ++installed;
        }
        if (!registry().empty())
            std::cerr << "[port] " << installed << " ported function(s) installed, " << skipped << " left as generated code"
                      << (capture ? " (capture run)" : " (MMPC_PORT_DISABLE)") << std::endl;
    }
}
