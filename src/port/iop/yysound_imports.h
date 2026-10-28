// The imports YYSOUND calls, as functions of the port (plan P5). An import is its stub in the module's import table, run as guest code: the
// emulator answers the call when the IOP reaches the stub. The number after each name is how many of a0 to a3 the module passes; the
// original leaves leftovers in the other argument registers, which the verifier does not compare.

#pragma once

#include "iop_port.h"

#include "yysound_functions.h"

namespace iop::yysound
{
    namespace import
    {
        namespace detail
        {
            inline uint32_t call(uint32_t stubOffset, unsigned arguments, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0)
            {
                return callImport(kBase + stubOffset, arguments, a0, a1, a2, a3);
            }
            inline int32_t toInt(uint32_t value) { return static_cast<int32_t>(value); }
        }

        // stdio
        inline void printf(uint32_t format) { detail::call(stub::printf, 1, format); }
        inline void printf(uint32_t format, uint32_t a1) { detail::call(stub::printf, 2, format, a1); }
        inline void printf(uint32_t format, uint32_t a1, uint32_t a2) { detail::call(stub::printf, 3, format, a1, a2); }
        inline void printf(uint32_t format, uint32_t a1, uint32_t a2, uint32_t a3) { detail::call(stub::printf, 4, format, a1, a2, a3); }

        // sysclib
        inline void strcpy(uint32_t destination, uint32_t source) { detail::call(stub::strcpy, 2, destination, source); }

        // intrman
        inline int32_t CpuSuspendIntr(IopPtr<int32_t> oldState) { return detail::toInt(detail::call(stub::CpuSuspendIntr, 1, oldState.address)); }
        inline int32_t CpuResumeIntr(int32_t oldState) { return detail::toInt(detail::call(stub::CpuResumeIntr, 1, static_cast<uint32_t>(oldState))); }

        // thbase
        inline int32_t iWakeupThread(int32_t thread) { return detail::toInt(detail::call(stub::iWakeupThread, 1, static_cast<uint32_t>(thread))); }
        inline int32_t USec2SysClock(uint32_t microseconds, IopPtr<SysClock> clock)
        {
            return detail::toInt(detail::call(stub::USec2SysClock, 2, microseconds, clock.address));
        }

        // timrman
        inline int32_t AllocHardTimer(int32_t source, int32_t size, int32_t prescale)
        {
            return detail::toInt(detail::call(stub::AllocHardTimer, 3, static_cast<uint32_t>(source), static_cast<uint32_t>(size), static_cast<uint32_t>(prescale)));
        }
        inline int32_t FreeHardTimer(int32_t timer) { return detail::toInt(detail::call(stub::FreeHardTimer, 1, static_cast<uint32_t>(timer))); }
        inline int32_t SetTimerHandler(int32_t timer, uint32_t compare, uint32_t handler, uint32_t common)
        {
            return detail::toInt(detail::call(stub::SetTimerHandler, 4, static_cast<uint32_t>(timer), compare, handler, common));
        }
        inline int32_t SetupHardTimer(int32_t timer, int32_t source, int32_t mode, int32_t count)
        {
            return detail::toInt(detail::call(stub::SetupHardTimer, 4, static_cast<uint32_t>(timer), static_cast<uint32_t>(source), static_cast<uint32_t>(mode),
                                              static_cast<uint32_t>(count)));
        }
        inline int32_t StartHardTimer(int32_t timer) { return detail::toInt(detail::call(stub::StartHardTimer, 1, static_cast<uint32_t>(timer))); }
        inline int32_t StopHardTimer(int32_t timer) { return detail::toInt(detail::call(stub::StopHardTimer, 1, static_cast<uint32_t>(timer))); }

        // sifman
        inline int32_t sceSifSetDma(IopPtr<sceSifDmaData> data, int32_t count)
        {
            return detail::toInt(detail::call(stub::sceSifSetDma, 2, data.address, static_cast<uint32_t>(count)));
        }

        // modmidi, modhsyn, modsein
        inline int32_t sceMidi_Init(IopPtr<sceCslCtx> context, uint32_t clock) { return detail::toInt(detail::call(stub::sceMidi_Init, 2, context.address, clock)); }
        inline int32_t sceHSyn_Init(IopPtr<sceCslCtx> context, uint32_t clock) { return detail::toInt(detail::call(stub::sceHSyn_Init, 2, context.address, clock)); }
        inline int32_t sceHSyn_SetVoiceStatBuffer(IopPtr<sceHSyn_VoiceStat> buffer)
        {
            return detail::toInt(detail::call(stub::sceHSyn_SetVoiceStatBuffer, 1, buffer.address));
        }
        inline int32_t sceHSyn_SESetMaxVoices(int32_t voices) { return detail::toInt(detail::call(stub::sceHSyn_SESetMaxVoices, 1, static_cast<uint32_t>(voices))); }
        inline int32_t sceSEIn_Init(IopPtr<sceCslCtx> context) { return detail::toInt(detail::call(stub::sceSEIn_Init, 1, context.address)); }
    }
}
