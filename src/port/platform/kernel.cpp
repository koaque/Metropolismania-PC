// The platform layer, EE kernel library and the interrupt-safe system call wrappers (plan P7), written from what they do and proven
// against the recompiled functions by the difftest. Prototypes are in config/platform_signatures.txt. The interrupt enable is bit
// 16 of the COP0 status word (EIE) in the context the runtime keeps for the thread; DI and EI clear and set it.

#include "game_functions.h"
#include "sdk.h"

namespace mm
{
    namespace
    {
        constexpr uint32_t kInterruptEnable = 0x10000u; // COP0 status, EIE

        // The system call wrappers the runtime handles itself (the original calls them with the interrupts off).
        constexpr uint32_t kEnableIntcCall = 0x0010DAA0u;
        constexpr uint32_t kDisableIntcCall = 0x0010DAB0u;
        constexpr uint32_t kEnableDmacCall = 0x0010DAC0u;
        constexpr uint32_t kDisableDmacCall = 0x0010DAD0u;

        // Runs a system call wrapper with the interrupts off, and back on if they were on.
        int withInterruptsOff(uint32_t call, int argument)
        {
            const bool wereOn = (port::g_call.ctx->cop0_status & kInterruptEnable) != 0;
            if (wereOn)
                sdk_DIntr();
            const int result = port::callGuest<int>(call, argument);
            if (wereOn)
                sdk_EIntr();
            return result;
        }
    }

    // DIntr: turns the interrupts off. 1 if they were on.
    int sdk_DIntr()
    {
        R5900Context *ctx = port::g_call.ctx;
        if ((ctx->cop0_status & kInterruptEnable) == 0)
            return 0;
        ctx->cop0_status &= ~kInterruptEnable;
        return 1;
    }

    // EIntr: turns the interrupts on. 1 if they were on already.
    int sdk_EIntr()
    {
        R5900Context *ctx = port::g_call.ctx;
        const bool wereOn = (ctx->cop0_status & kInterruptEnable) != 0;
        ctx->cop0_status |= kInterruptEnable;
        return wereOn ? 1 : 0;
    }

    int sdk_EnableIntc(int cause) { return withInterruptsOff(kEnableIntcCall, cause); }
    int sdk_DisableIntc(int cause) { return withInterruptsOff(kDisableIntcCall, cause); }
    int sdk_EnableDmac(int channel) { return withInterruptsOff(kEnableDmacCall, channel); }
    int sdk_DisableDmac(int channel) { return withInterruptsOff(kDisableDmacCall, channel); }

    PORT_FN(sdk_DIntr);
    PORT_FN(sdk_EIntr);
    PORT_FN(sdk_EnableIntc);
    PORT_FN(sdk_DisableIntc);
    PORT_FN(sdk_EnableDmac);
    PORT_FN(sdk_DisableDmac);
}
