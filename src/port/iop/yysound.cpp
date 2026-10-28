// YYSOUND.IRX (Indi Software, yysound.irx 2002.05.24), the game's sound driver, as readable C++ (plan P5). Every function keeps the name,
// parameters, locals and structures of the module's own debug info (config/iop/yysound_functions.csv, yysound_types.h, yysound_globals.h) and
// replaces the original at its address; PS2X_IOP_VERIFY=1 runs the original beside each one and compares the result.

#include "iop_port.h"

#include "yysound_functions.h"
#include "yysound_globals.h"
#include "yysound_imports.h"

#include <cstddef>

namespace iop::yysound
{
    namespace
    {
        // Functions of the module that are not ported yet run as the original.
        int32_t CmdMidiClose(IopPtr<ST_MIDI_CLOSE> data) { return static_cast<int32_t>(call(kBase + offset::CmdMidiClose, data.address)); }

        // The address of a function of the module, as it is handed to the kernel as a callback.
        constexpr uint32_t code(uint32_t functionOffset) { return kBase + functionOffset; }
    }

    // ---- hard timers: one ticks the sound (timer thread), the other the file loading (load thread)

    // The handler of the hard timers, in interrupt context: wakes the thread that waits for the tick and returns the timer's count.
    UINT timer_handler(IopPtr<void> common)
    {
        const IopPtr<TimerCtx> tc{common.address};
        import::iWakeupThread(tc->thread_id);
        return tc->count;
    }

    int32_t start_timer()
    {
        if (import::StartHardTimer(m_timer->timer_id) == 0)
            return 0;
        import::printf(str::T_T_yySound_ERROR_Can_NOT_start_hard_timer);
        return -1;
    }

    int32_t stop_timer()
    {
        const int32_t ret = import::StopHardTimer(m_timer->timer_id);
        if (ret == 0 || ret == -0x9C)
            return 0;
        import::printf(str::T_T_yySound_ERROR_Can_NOT_stop_hard_timer);
        return -1;
    }

    int32_t clear_timer()
    {
        if (stop_timer() == -1)
            return -1;
        if (import::FreeHardTimer(m_timer->timer_id) == 0)
            return 0;
        import::printf(str::T_T_yySound_ERROR_Can_NOT_free_hard_timer);
        return -1;
    }

    // Sets up the timer thread's hard timer: a tick every 4167 microseconds, handled by timer_handler.
    int32_t set_timer()
    {
        Frame stack(frame::set_timer);
        const IopPtr<SysClock> clock{stack.local(local::set_timer::clock)};
        import::USec2SysClock(0x1047, clock);
        m_timer->count = clock->low;
        const int32_t timer_id = import::AllocHardTimer(1, 0x20, 1);
        if (timer_id < 1)
        {
            import::printf(str::T_T_yySound_ERROR_Can_NOT_allocate_hard_timer);
            return -1;
        }
        m_timer->timer_id = timer_id;
        if (import::SetTimerHandler(timer_id, m_timer->count, code(offset::timer_handler), m_timer.address) != 0)
        {
            import::printf(str::T_T_yySound_ERROR_Can_NOT_set_timeup_timer);
            return -1;
        }
        if (import::SetupHardTimer(timer_id, 1, 0, 1) != 0)
        {
            import::printf(str::T_T_yySound_ERROR_Can_NOT_setup_hard_timer);
            return -1;
        }
        return start_timer() == -1 ? -1 : 0;
    }

    int32_t start_load_timer()
    {
        if (import::StartHardTimer(m_load->timer_id) == 0)
            return 0;
        import::printf(str::T_T_yySound_ERROR_Can_NOT_start_hard_load);
        return -1;
    }

    int32_t stop_load_timer()
    {
        const int32_t ret = import::StopHardTimer(m_load->timer_id);
        if (ret == 0 || ret == -0x9C)
            return 0;
        import::printf(str::T_T_yySound_ERROR_Can_NOT_stop_hard_load);
        return -1;
    }

    int32_t clear_load_timer()
    {
        if (stop_load_timer() == -1)
            return -1;
        if (import::FreeHardTimer(m_load->timer_id) == 0)
            return 0;
        import::printf(str::T_T_yySound_ERROR_Can_NOT_free_hard_load);
        return -1;
    }

    // Sets up the load thread's hard timer: a tick every 8333 microseconds.
    int32_t set_load_timer()
    {
        Frame stack(frame::set_load_timer);
        const IopPtr<SysClock> clock{stack.local(local::set_load_timer::clock)};
        import::USec2SysClock(0x208D, clock);
        m_load->count = clock->low;
        const int32_t timer_id = import::AllocHardTimer(1, 0x20, 1);
        if (timer_id < 1)
        {
            import::printf(str::T_T_yySound_ERROR_Can_NOT_allocate_hard_load);
            return -1;
        }
        m_load->timer_id = timer_id;
        if (import::SetTimerHandler(timer_id, m_load->count, code(offset::timer_handler), m_load.address) != 0)
        {
            import::printf(str::T_T_yySound_ERROR_Can_NOT_set_timeup_load);
            return -1;
        }
        if (import::SetupHardTimer(timer_id, 1, 0, 1) != 0)
        {
            import::printf(str::T_T_yySound_ERROR_Can_NOT_setup_hard_load);
            return -1;
        }
        return start_load_timer() == -1 ? -1 : 0;
    }

    // ---- the MIDI, hardware synthesizer and sound effect modules

    int32_t metaMsgCB(uint8_t metaNo, IopPtr<uint8_t> bf, uint32_t len, uint32_t private_data)
    {
        (void)metaNo;
        (void)bf;
        (void)len;
        (void)private_data;
        return 1;
    }

    int32_t repeatCB(IopPtr<sceMidiLoopInfo> pInf, uint32_t private_data)
    {
        (void)pInf;
        (void)private_data;
        return 1;
    }

    // Lays out the buffer contexts the three modules (modmidi, modsein, modhsyn) are given: which buffers each group has, their semaphores
    // (none) and what they point to (the environments and the stream buffers).
    void set_module_context()
    {
        midiCtx->extmod = {};
        midiCtx->callBack = {};
        midiCtx->buffGrpNum = 2;
        midiCtx->buffGrp = midiGrp.ptr();
        midiGrp[0].buffNum = 0x20;
        midiGrp[0].buffCtx = minBfCtx.ptr();
        for (int32_t i = 0; i < 0x10; i = i + 1)
        {
            minBfCtx[i * 2].sema = 0;
            minBfCtx[i * 2].buff = {};
            minBfCtx[i * 2 + 1].sema = 0;
            minBfCtx[i * 2 + 1].buff = midiEnv.at(i);
        }
        midiGrp[1].buffNum = 0x10;
        midiGrp[1].buffCtx = moutBfCtx.ptr();
        for (int32_t i = 0; i < 0x10; i = i + 1)
        {
            moutBfCtx[i].sema = 0;
            moutBfCtx[i].buff = streamBf.at(i);
        }
        seinCtx->extmod = {};
        seinCtx->callBack = {};
        seinCtx->buffGrpNum = 2;
        seinCtx->buffGrp = seinGrp.ptr();
        seinGrp[0].buffNum = 0;
        seinGrp[0].buffCtx = {};
        seinGrp[1].buffNum = 0x10;
        seinGrp[1].buffCtx = seinBfCtx.ptr();
        for (int32_t i = 0; i < 0x10; i = i + 1)
        {
            seinBfCtx[i].sema = 0;
            seinBfCtx[i].buff = streamBf.at(0x10 + i); // the stream buffers after the first sixteen
        }
        synthCtx->extmod = {};
        synthCtx->callBack = {};
        synthCtx->buffGrpNum = 1;
        synthCtx->buffGrp = synthGrp.ptr();
        synthGrp->buffNum = 0x40;
        synthGrp->buffCtx = synthBfCtx.ptr();
        for (int32_t i = 0; i < 0x20; i = i + 1)
        {
            synthBfCtx[i * 2].sema = 0;
            synthBfCtx[i * 2].buff = streamBf.at(i);
            synthBfCtx[i * 2 + 1].sema = 0;
            synthBfCtx[i * 2 + 1].buff = synthEnv.at(i);
            // the head of the stream buffer: the 8 bytes 08 04 00 00 00 00 00 00
            streamBf[i][0] = 8;
            streamBf[i][1] = 4;
            streamBf[i][2] = 0;
            streamBf[i][3] = 0;
            streamBf[i][4] = 0;
            streamBf[i][5] = 0;
            streamBf[i][6] = 0;
            streamBf[i][7] = 0;
        }
    }

    // Starts modmidi and gives each of the sixteen environments its callbacks and output ports.
    int32_t set_midi()
    {
        if (import::sceMidi_Init(midiCtx.ptr(), 0x1047) != 0)
        {
            import::printf(str::T_T_yySound_ERROR_sceMidi_Init);
            return -1;
        }
        for (uint32_t i = 0; static_cast<int32_t>(i) < 0x10; i = i + 1)
        {
            midiEnv[i].chMsgCallBackPrivateData = streamBf.at(i).address;
            midiEnv[i].metaMsgCallBack = IopPtr<void>{code(offset::metaMsgCB)};
            midiEnv[i].metaMsgCallBackPrivateData = streamBf.at(i).address;
            midiEnv[i].repeatCallBack = IopPtr<void>{code(offset::repeatCB)};
            midiEnv[i].repeatCallBackPrivateData = streamBf.at(i).address;
            midiEnv[i].excOutPort = static_cast<uint16_t>(1u << (i & 0x1F));
            for (int32_t j = 0; j < 0x10; j = j + 1)
                midiEnv[i].outPort[j] = static_cast<uint16_t>(1u << (i & 0x1F));
        }
        return 0;
    }

    // Starts modhsyn and gives each of the 32 environments its port mode and wave type, then hands it the voice status buffer.
    int32_t set_hsyn(IopPtr<sceHSyn_VoiceStat> pVstat)
    {
        if (import::sceHSyn_Init(synthCtx.ptr(), 0x1047) != 0)
        {
            import::printf(str::T_T_yySound_ERROR_sceHSyn_Init);
            return -1;
        }
        for (int32_t i = 0; i < 0x20; i = i + 1)
        {
            if (0xF < i)
            {
                synthEnv[i].portMode = 1;
                synthEnv[i].waveType = 0;
            }
            synthEnv[i].priority = 0;
            synthEnv[i].lfoWaveNum = 0;
            synthEnv[i].lfoWaveTbl = {};
            synthEnv[i].velocityMapNum = 0;
            synthEnv[i].velocityMapTbl = {};
        }
        import::sceHSyn_SetVoiceStatBuffer(pVstat);
        import::sceHSyn_SESetMaxVoices(0xC);
        return 0;
    }

    int32_t set_sein()
    {
        if (import::sceSEIn_Init(seinCtx.ptr()) == 0)
            return 0;
        import::printf(str::T_T_yySound_ERROR_sceSEIn_Init);
        return -1;
    }

    // ---- sending the state to the EE

    // When the state has changed (m_SendSw) and the EE has told where it wants it (mEEaddress), DMAs the 64 bytes of ee_put there.
    void SendDataForEE()
    {
        if (mEEaddress != 0 && m_SendSw != 0)
        {
            Frame stack(frame::SendDataForEE);
            const IopPtr<sceSifDmaData> sdd{stack.local(local::SendDataForEE::sdd)};
            const IopPtr<int32_t> oldstat{stack.local(local::SendDataForEE::oldstat)};
            m_SendSw = 0;
            sdd->data = ee_put.address;
            sdd->addr = mEEaddress;
            sdd->size = 0x40;
            sdd->mode = 0;
            import::CpuSuspendIntr(oldstat);
            import::sceSifSetDma(sdd, 1);
            import::CpuResumeIntr(*oldstat);
        }
    }

    // ---- tracks

    void ClearMidiTrack(IopPtr<ST_MIDI_TRACK> pMT)
    {
        pMT->fno = -1;
        pMT->volume = -1;
        pMT->tempo = 0;
        pMT->busy = 0;
    }

    void ClearWaveTrack(IopPtr<ST_WAVE_TRACK> pWT)
    {
        pWT->fno = -1;
        pWT->lvolume = -1;
        pWT->rvolume = -1;
        pWT->pitch = 0;
        pWT->busy = 0;
    }

    // ---- the command queues

    // Takes the next slot of the file list; -3 when the list is full (it holds 32), else 1.
    int32_t AddFilePutPos()
    {
        const BYTE previous = mFileList->PutPos;
        mFileList->PutPos = static_cast<BYTE>(mFileList->PutPos + 1);
        if (0x1F < mFileList->PutPos)
            mFileList->PutPos = 0;
        if (mFileList->PutPos == mFileList->GetPos)
        {
            mFileList->PutPos = previous;
            return -3;
        }
        return 1;
    }

    // Takes the next slot of a command list (64 entries); -3 when it is full, else counts the request for the EE and asks for the data to be sent.
    int32_t AddRequestPutPos(IopPtr<ST_COMMAND_LIST> pCL, BYTE fno)
    {
        (void)fno;
        const BYTE previous = pCL->PutPos;
        pCL->PutPos = static_cast<BYTE>(pCL->PutPos + 1);
        if (0x3F < pCL->PutPos)
            pCL->PutPos = 0;
        if (pCL->PutPos == pCL->GetPos)
        {
            pCL->PutPos = previous;
            return -3;
        }
        ee_put[3] = ee_put[3] + 1;
        m_SendSw = 1;
        return 1;
    }

    // ---- commands from the EE

    // Opens a MIDI file: closes it first if it is open, queues the file names and the request. Returns 1 plus the request's id, or a negative error.
    int32_t CmdMidiOpen(IopPtr<ST_MIDI_OPEN> data)
    {
        ee_put[4] = data->id;
        if ((mMidiFile[data->fno].open & 2) != 0)
        {
            Frame stack(frame::CmdMidiOpen);
            const IopPtr<ST_MIDI_CLOSE> mc{stack.local(local::CmdMidiOpen::mc)};
            mc->fno = data->fno;
            const int32_t ret = CmdMidiClose(mc);
            if (ret != 1)
                return ret;
        }
        const uint32_t entry = mFileList.address + offsetof(ST_FILE_LIST, list) + mFileList->PutPos * static_cast<uint32_t>(sizeof(ST_FILE_ONE));
        import::strcpy(entry + offsetof(ST_FILE_ONE, SQname), data.address + offsetof(ST_MIDI_OPEN, sqname));
        import::strcpy(entry + offsetof(ST_FILE_ONE, HDname), data.address + offsetof(ST_MIDI_OPEN, hdname));
        import::strcpy(entry + offsetof(ST_FILE_ONE, BDname), data.address + offsetof(ST_MIDI_OPEN, bdname));
        mMidiFile[data->fno].open |= 2;
        const BYTE fno = data->fno;
        ST_COMMAND_LIST &commands = mCommandList[fno + 8];
        ST_COMMAND_ONE &request = commands.list[commands.PutPos];
        request.command = 0x1001;
        request.process = 0;
        request.fileNo = mFileList->PutPos;
        request.id = data->id;
        if (AddFilePutPos() == -3)
            return -3;
        return AddRequestPutPos(mCommandList.at(fno + 8), data->fno) + data->id;
    }
}

IOP_FN(yysound, timer_handler)
IOP_FN(yysound, start_timer)
IOP_FN(yysound, stop_timer)
IOP_FN(yysound, clear_timer)
IOP_FN(yysound, set_timer)
IOP_FN(yysound, start_load_timer)
IOP_FN(yysound, stop_load_timer)
IOP_FN(yysound, clear_load_timer)
IOP_FN(yysound, set_load_timer)
IOP_FN(yysound, metaMsgCB)
IOP_FN(yysound, repeatCB)
IOP_FN(yysound, set_module_context)
IOP_FN(yysound, set_midi)
IOP_FN(yysound, set_hsyn)
IOP_FN(yysound, set_sein)
IOP_FN(yysound, SendDataForEE)
IOP_FN(yysound, ClearMidiTrack)
IOP_FN(yysound, ClearWaveTrack)
IOP_FN(yysound, AddFilePutPos)
IOP_FN(yysound, AddRequestPutPos)
IOP_FN(yysound, CmdMidiOpen)
