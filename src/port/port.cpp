// Registry of ported functions and their installation into the runtime (see abi.h).

#include "port.h"

#include "abi.h"
#include "difftest.h"

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

    void enterPorted(PS2Runtime *runtime) { runtime->eeScheduler().enterNoPreempt(); }
    void leavePorted(PS2Runtime *runtime) { runtime->eeScheduler().leaveNoPreempt(); }

    void fatal(const char *what, uint32_t address)
    {
        std::cerr << "[port] fatal: " << what << " (0x" << std::hex << std::setw(8) << std::setfill('0') << address
                  << std::dec << ")" << std::endl;
        std::abort();
    }

    void calleeDidNotReturn(uint32_t address, uint32_t pc)
    {
        std::cerr << "[port] fatal: guest function 0x" << std::hex << address << " called from ported code did not return (pc 0x"
                  << pc << ")" << std::dec << std::endl;
        std::abort();
    }

    void reachedThreadSwitch(uint32_t address)
    {
        std::cerr << "[port] fatal: ported function " << nameOf(address) << " (0x" << std::hex << address << std::dec
                  << ") reached a thread switch; blocking code waits for native threading (plan P6)" << std::endl;
        std::abort();
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
