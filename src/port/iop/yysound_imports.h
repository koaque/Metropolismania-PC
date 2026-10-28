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
        // a fifth argument goes on the stack
        inline void printf(uint32_t format, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
        {
            g_context->stackArguments(&a4, 1);
            detail::call(stub::printf, 4, format, a1, a2, a3);
        }

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

        inline int32_t QueryTotalFreeMemSize() { return detail::toInt(detail::call(stub::QueryTotalFreeMemSize, 0)); }

        // sysclib
        inline void strcpy(uint32_t destination, uint32_t source) { detail::call(stub::strcpy, 2, destination, source); }

        // intrman
        inline int32_t CpuSuspendIntr(IopPtr<int32_t> oldState) { return detail::toInt(detail::call(stub::CpuSuspendIntr, 1, oldState.address)); }
        inline int32_t CpuResumeIntr(int32_t oldState) { return detail::toInt(detail::call(stub::CpuResumeIntr, 1, static_cast<uint32_t>(oldState))); }

        // thbase
        inline int32_t CreateThread(IopPtr<ThreadParam> param) { return detail::toInt(detail::call(stub::CreateThread, 1, param.address)); }
        inline int32_t StartThread(int32_t thread, uint32_t argument)
        {
            return detail::toInt(detail::call(stub::StartThread, 2, static_cast<uint32_t>(thread), argument));
        }
        inline int32_t GetThreadId() { return detail::toInt(detail::call(stub::GetThreadId, 0)); }
        inline int32_t DeleteThread(int32_t thread) { return detail::toInt(detail::call(stub::DeleteThread, 1, static_cast<uint32_t>(thread))); }
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
        inline int32_t sceSdInit(uint32_t flag) { return detail::toInt(detail::call(stub::sceSdInit, 1, flag)); }
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
        inline int32_t sceHSyn_SetVolume(IopPtr<sceCslCtx> context, int32_t port, uint32_t volume)
        {
            return detail::toInt(detail::call(stub::sceHSyn_SetVolume, 3, context.address, static_cast<uint32_t>(port), volume));
        }
        inline int32_t sceMidi_MidiGetAbsoluteTempo(IopPtr<sceCslCtx> context, int32_t port)
        {
            return detail::toInt(detail::call(stub::sceMidi_MidiGetAbsoluteTempo, 2, context.address, static_cast<uint32_t>(port)));
        }
        inline int32_t sceMidi_Load(IopPtr<sceCslCtx> context, int32_t port)
        {
            return detail::toInt(detail::call(stub::sceMidi_Load, 2, context.address, static_cast<uint32_t>(port)));
        }
        inline int32_t sceMidi_MidiSetAbsoluteTempo(IopPtr<sceCslCtx> context, int32_t port, uint32_t tempo)
        {
            return detail::toInt(detail::call(stub::sceMidi_MidiSetAbsoluteTempo, 3, context.address, static_cast<uint32_t>(port), tempo));
        }
        // five arguments: the fifth goes on the stack
        inline int32_t sceHSyn_Load(IopPtr<sceCslCtx> context, int32_t port, uint32_t body, uint32_t head, uint32_t flag)
        {
            g_context->stackArguments(&flag, 1);
            return detail::toInt(detail::call(stub::sceHSyn_Load, 4, context.address, static_cast<uint32_t>(port), body, head));
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
        inline int32_t sceSEIn_ATick(IopPtr<sceCslCtx> context) { return detail::toInt(detail::call(stub::sceSEIn_ATick, 1, context.address)); }
        inline int32_t sceMidi_ATick(IopPtr<sceCslCtx> context) { return detail::toInt(detail::call(stub::sceMidi_ATick, 1, context.address)); }
        inline int32_t sceHSyn_ATick(IopPtr<sceCslCtx> context) { return detail::toInt(detail::call(stub::sceHSyn_ATick, 1, context.address)); }

        // sifcmd
        inline int32_t sceSifInitRpc(uint32_t mode) { return detail::toInt(detail::call(stub::sceSifInitRpc, 1, mode)); }
        inline int32_t sceSifSetRpcQueue(IopPtr<_sif_queue_data> queue, int32_t thread)
        {
            return detail::toInt(detail::call(stub::sceSifSetRpcQueue, 2, queue.address, static_cast<uint32_t>(thread)));
        }
        // seven arguments: the fifth to the seventh go on the stack
        inline int32_t sceSifRegisterRpc(IopPtr<_sif_serve_data> serve, uint32_t id, uint32_t function, uint32_t buffer, uint32_t connectFunction,
                                         uint32_t connectBuffer, IopPtr<_sif_queue_data> queue)
        {
            const uint32_t rest[3] = {connectFunction, connectBuffer, queue.address};
            g_context->stackArguments(rest, 3);
            return detail::toInt(detail::call(stub::sceSifRegisterRpc, 4, serve.address, id, function, buffer));
        }
        inline int32_t sceSEIn_Init(IopPtr<sceCslCtx> context) { return detail::toInt(detail::call(stub::sceSEIn_Init, 1, context.address)); }
    }
}
