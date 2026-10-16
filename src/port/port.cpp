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

    void registerPorted(uint32_t address, const char *name, GuestFn wrapper, ResultKind result)
    {
        registry().push_back({address, name, wrapper, result, nullptr});
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
