// Registration of the IOP port's native replacements with the IOP emulator (plan P5). See iop_port.h.

#include "iop_port.h"

#include "yysound_costs.h"
#include "yysound_functions.h"
#include "yysound_globals.h"

#include "ps2x/iop/iop_subsystem.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace iop
{
    std::vector<Registration> &registry()
    {
        static std::vector<Registration> functions;
        return functions;
    }

    void fatal(const char *what, uint32_t value)
    {
        std::fprintf(stderr, "[iop] fatal: %s (0x%08X)\n", what, value);
        std::fflush(stderr);
        std::exit(3);
    }

    void install(PS2Runtime &runtime)
    {
        ps2x::iop::IopSubsystem *subsystem = runtime.iopSubsystem();
        if (subsystem == nullptr)
            return;
        subsystem->registerNativeModule(yysound::kModule, yysound::kImageHash, yysound::kImageSize);
        for (const Registration &registration : registry())
        {
            // the instructions the original takes (tools/iop_costs.py, measured by the verifier), charged as IOP time
            uint32_t cost = registration.cost;
            for (const yysound::FunctionCost *entry = yysound::kCosts; entry->name != nullptr; ++entry)
            {
                if (std::strcmp(entry->name, registration.name) == 0)
                    cost = entry->cost;
            }
            subsystem->registerNative(registration.module, registration.offset, registration.function, registration.name, cost, registration.hasResult, registration.parkStub, registration.mayBlock);
        }
        // WaveLoop and VoiceIdle build a play request from a local structure whose `jno` they never set, and CmdWavePlay builds its volume and pitch
        // requests from locals whose `id` it never sets: the original copies whatever the stack held (the leftovers of the functions it called
        // before, which a replacement does not leave). Only a joint file looks at the jno; the id lands in ee_put[4] and in the request, and is
        // compared with the id the EE waits for (CmdSetCheckID). The verifier does not compare these fields after a tick (the original's stay).
        subsystem->registerVerifyIgnore(yysound::kModule, "atick_resume", yysound::ee_put.address + 4 * 4 - yysound::kBase, 4);
        for (uint32_t list = 0; list < yysound::mCommandList.size(); ++list)
        {
            for (uint32_t slot = 0; slot < 64; ++slot)
            {
                const uint32_t address = yysound::mCommandList.address + list * static_cast<uint32_t>(sizeof(yysound::ST_COMMAND_LIST)) +
                                         static_cast<uint32_t>(offsetof(yysound::ST_COMMAND_LIST, list)) + slot * static_cast<uint32_t>(sizeof(yysound::ST_COMMAND_ONE)) +
                                         static_cast<uint32_t>(offsetof(yysound::ST_COMMAND_ONE, jno));
                subsystem->registerVerifyIgnore(yysound::kModule, "atick_resume", address - yysound::kBase, 2);
                subsystem->registerVerifyIgnore(yysound::kModule, "atick_resume",
                                                address - static_cast<uint32_t>(offsetof(yysound::ST_COMMAND_ONE, jno)) + static_cast<uint32_t>(offsetof(yysound::ST_COMMAND_ONE, id)) - yysound::kBase, 2);
            }
        }
    }
}
