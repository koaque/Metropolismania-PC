// Registration of the IOP port's native replacements with the IOP emulator (plan P5). See iop_port.h.

#include "iop_port.h"

#include "yysound_functions.h"

#include "ps2x/iop/iop_subsystem.h"

#include <cstdio>
#include <cstdlib>

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
            subsystem->registerNative(registration.module, registration.offset, registration.function, registration.name, registration.cost, registration.hasResult);
    }
}
