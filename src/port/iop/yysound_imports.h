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

        // ioman: the file functions of the IOP (flags 1 is read only; lseek's origin 0 is the start, 2 the end)
        inline int32_t open(uint32_t name, int32_t flags) { return detail::toInt(detail::call(stub::open, 2, name, static_cast<uint32_t>(flags))); }
        inline int32_t close(int32_t fd) { return detail::toInt(detail::call(stub::close, 1, static_cast<uint32_t>(fd))); }
        inline int32_t read(int32_t fd, uint32_t buffer, uint32_t size)
        {
            return detail::toInt(detail::call(stub::read, 3, static_cast<uint32_t>(fd), buffer, size));
        }
        inline int32_t lseek(int32_t fd, int32_t position, int32_t origin)
        {
            return detail::toInt(detail::call(stub::lseek, 3, static_cast<uint32_t>(fd), static_cast<uint32_t>(position), static_cast<uint32_t>(origin)));
        }

        // sysmem
        inline uint32_t AllocSysMemory(int32_t type, uint32_t size, uint32_t address)
        {
            return detail::call(stub::AllocSysMemory, 3, static_cast<uint32_t>(type), size, address);
        }
        inline int32_t FreeSysMemory(uint32_t address) { return detail::toInt(detail::call(stub::FreeSysMemory, 1, address)); }

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

        // libsd
        inline int32_t sceSdSetCoreAttr(uint32_t attribute, uint32_t value) { return detail::toInt(detail::call(stub::sceSdSetCoreAttr, 2, attribute, value)); }
        inline uint32_t sceSdNote2Pitch(uint32_t centerNote, uint32_t centerFine, uint32_t note, uint32_t fine)
        {
            return detail::call(stub::sceSdNote2Pitch, 4, centerNote, centerFine, note, fine);
        }
        inline void sceSdSetAddr(uint32_t entry, uint32_t value) { detail::call(stub::sceSdSetAddr, 2, entry, value); }
        inline void sceSdSetParam(uint32_t entry, uint32_t value) { detail::call(stub::sceSdSetParam, 2, entry, value); }
        inline int32_t sceSdSetEffectAttr(int32_t core, IopPtr<sceSdEffectAttr> attributes)
        {
            return detail::toInt(detail::call(stub::sceSdSetEffectAttr, 2, static_cast<uint32_t>(core), attributes.address));
        }

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
        inline int32_t sceHSyn_VoiceTrans(int32_t channel, uint32_t source, uint32_t destination, uint32_t size)
        {
            return detail::toInt(detail::call(stub::sceHSyn_VoiceTrans, 4, static_cast<uint32_t>(channel), source, destination, size));
        }
        // five arguments: the fifth goes on the stack
        inline int32_t sceHSyn_SERetrieveVoiceNumberByID(IopPtr<sceCslCtx> context, int32_t channel, int32_t id, uint32_t buffer, int32_t count)
        {
            const uint32_t fifth = static_cast<uint32_t>(count);
            g_context->stackArguments(&fifth, 1);
            return detail::toInt(detail::call(stub::sceHSyn_SERetrieveVoiceNumberByID, 4, context.address, static_cast<uint32_t>(channel),
                                              static_cast<uint32_t>(id), buffer));
        }
        inline int32_t sceHSyn_SESetMaxVoices(int32_t voices) { return detail::toInt(detail::call(stub::sceHSyn_SESetMaxVoices, 1, static_cast<uint32_t>(voices))); }
        inline int32_t sceHSyn_ResetAllControler(IopPtr<sceCslCtx> context, int32_t channel)
        {
            return detail::toInt(detail::call(stub::sceHSyn_ResetAllControler, 2, context.address, static_cast<uint32_t>(channel)));
        }
        inline int32_t sceMidi_SelectMidi(IopPtr<sceCslCtx> context, int32_t port, int32_t song)
        {
            return detail::toInt(detail::call(stub::sceMidi_SelectMidi, 3, context.address, static_cast<uint32_t>(port), static_cast<uint32_t>(song)));
        }
        inline int32_t sceMidi_MidiSetLocation(IopPtr<sceCslCtx> context, int32_t port, int32_t location)
        {
            return detail::toInt(detail::call(stub::sceMidi_MidiSetLocation, 3, context.address, static_cast<uint32_t>(port), static_cast<uint32_t>(location)));
        }
        inline int32_t sceMidi_MidiPlaySwitch(IopPtr<sceCslCtx> context, int32_t port, int32_t on)
        {
            return detail::toInt(detail::call(stub::sceMidi_MidiPlaySwitch, 3, context.address, static_cast<uint32_t>(port), static_cast<uint32_t>(on)));
        }
        inline int32_t sceMidi_MidiVolumeChange(IopPtr<sceCslCtx> context, int32_t port, uint32_t channel, uint32_t volume)
        {
            return detail::toInt(detail::call(stub::sceMidi_MidiVolumeChange, 4, context.address, static_cast<uint32_t>(port), channel, volume));
        }
        // five arguments: the fifth goes on the stack
        inline int32_t sceSEIn_PutMsg(IopPtr<sceCslCtx> context, int32_t channel, int32_t id, uint32_t message, int32_t size)
        {
            const uint32_t fifth = static_cast<uint32_t>(size);
            g_context->stackArguments(&fifth, 1);
            return detail::toInt(detail::call(stub::sceSEIn_PutMsg, 4, context.address, static_cast<uint32_t>(channel), static_cast<uint32_t>(id), message));
        }
        inline int32_t sceSEIn_Init(IopPtr<sceCslCtx> context) { return detail::toInt(detail::call(stub::sceSEIn_Init, 1, context.address)); }
    }
}
