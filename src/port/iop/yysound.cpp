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

    // ---- files and the SPU memory

    // Reads a whole file into IOP heap memory. Returns its address, or 0 (after a message) when anything fails.
    UINT OpenFileIOP(IopPtr<char> fname)
    {
        Frame stack(frame::OpenFileIOP);
        const IopPtr<int32_t> oldstat{stack.local(local::OpenFileIOP::oldstat)};
        const int32_t fd = import::open(fname.address, 1);
        if (fd < 0)
        {
            import::printf(str::T_T_yySound_ERROR_open_failed);
            return 0;
        }
        const int32_t flen = import::lseek(fd, 0, 2);
        if (flen < 0)
        {
            import::close(fd);
            import::printf(str::T_T_yySound_ERROR_lseek_failed);
            return 0;
        }
        if (import::lseek(fd, 0, 0) < 0)
        {
            import::close(fd);
            import::printf(str::T_T_yySound_ERROR_lseek_failed);
            return 0;
        }
        import::CpuSuspendIntr(oldstat);
        const UINT mem = import::AllocSysMemory(0, static_cast<uint32_t>(flen), 0);
        import::CpuResumeIntr(*oldstat);
        if (mem == 0)
        {
            import::close(fd);
            import::printf(str::T_T_yySound_ERROR_Can_t_alloc_heap_memory);
            return 0;
        }
        if (import::read(fd, mem, static_cast<uint32_t>(flen)) < 0)
        {
            import::close(fd);
            import::CpuSuspendIntr(oldstat);
            import::FreeSysMemory(mem);
            import::CpuResumeIntr(*oldstat);
            import::printf(str::T_T_yySound_ERROR_file_read_failed);
            return 0;
        }
        import::close(fd);
        return mem;
    }

    void CloseFileIOP(UINT mem)
    {
        Frame stack(frame::CloseFileIOP);
        const IopPtr<int32_t> oldstat{stack.local(local::CloseFileIOP::oldstat)};
        import::CpuSuspendIntr(oldstat);
        import::FreeSysMemory(mem);
        import::CpuResumeIntr(*oldstat);
    }

    // Reserves `getsize` bytes of the SPU's sound memory (the list keeps 20 blocks sorted by address, a block being used or free, the last entry the
    // end marker); returns the block's address, or 0 when the list or the memory is full.
    UINT NewSPUMemory(UINT getsize)
    {
        if (mSpuList[0x13].used == 2)
        {
            import::printf(str::T_T_yySound_ERROR_SPU_Memory_Index_Full);
        }
        else
        {
            for (uint16_t pos = 0; pos < 0x13; pos = static_cast<uint16_t>(pos + 1))
            {
                if (mSpuList[pos].used == 2)
                {
                    import::printf(str::T_T_yySound_ERROR_SPU_Memory_Full);
                    return 0;
                }
                if (mSpuList[pos].used == 0)
                {
                    const UINT addr = mSpuList[pos].addr;
                    const UINT size = mSpuList[pos + 1].addr - addr;
                    if (getsize <= size)
                    {
                        if (getsize < size)
                        {
                            // split the free block: everything after it moves up one entry
                            for (uint16_t i = 0x13; pos < i; i = static_cast<uint16_t>(i - 1))
                            {
                                mSpuList[i].addr = mSpuList[i - 1].addr;
                                mSpuList[i].used = mSpuList[i - 1].used;
                            }
                            mSpuList[pos + 1].used = 0;
                            mSpuList[pos + 1].addr = addr + getsize;
                        }
                        mSpuList[pos].used = 1;
                        return addr;
                    }
                }
            }
            import::printf(str::T_T_yySound_ERROR_SPU_Memory_Full);
        }
        return 0;
    }

    // Frees the block at `addr` and merges free neighbours; 1 when there was such a used block, else 0.
    BYTE DeleteSPUMemory(UINT addr)
    {
        int16_t pos = -1;
        for (int16_t i = 0; i <= 0x12; i = static_cast<int16_t>(i + 1))
        {
            if (mSpuList[i].addr == addr)
            {
                pos = i;
                break;
            }
        }
        if (pos == -1)
            return 0;
        if (mSpuList[pos].used == 0)
            return 0;
        mSpuList[pos].used = 0;
        if (pos == 0 || mSpuList[pos - 1].used != 0)
        {
            pos = static_cast<int16_t>(pos + 1);
        }
        else
        {
            for (int16_t i = pos; i < 0x13; i = static_cast<int16_t>(i + 1))
            {
                mSpuList[i].addr = mSpuList[i + 1].addr;
                mSpuList[i].used = mSpuList[i + 1].used;
            }
            mSpuList[0x13].addr = 0;
            mSpuList[0x13].used = 0;
        }
        if (mSpuList[pos].used == 0)
        {
            for (int16_t i = pos; i < 0x13; i = static_cast<int16_t>(i + 1))
            {
                mSpuList[i].addr = mSpuList[i + 1].addr;
                mSpuList[i].used = mSpuList[i + 1].used;
            }
            mSpuList[0x13].addr = 0;
            mSpuList[0x13].used = 0;
        }
        return 1;
    }

    // NOT registered yet: sceHSyn_VoiceTrans blocks the load thread, and a native function cannot be parked (the IOP needs fibers first).
    // Loads a file into the SPU's sound memory a piece per call (m_SpuStep 0: open it and reserve the memory, 1: read 32 KB and send it with
    // sceHSyn_VoiceTrans). Returns 0 while there is more to do, the SPU address when the file is done, -1 on an error; nothing happens while the
    // loading is paused.
    int32_t OpenFileSPU(IopPtr<char> fname)
    {
        if (m_SpuStep == 0)
        {
            if (mLoadPause != 1)
            {
                m_SpuFD = import::open(fname.address, 1);
                if (m_SpuFD < 0)
                {
                    import::printf(str::T_T_yySound_ERROR_open_failed);
                    return -1;
                }
                const int32_t flen = import::lseek(m_SpuFD, 0, 2);
                if (flen < 0)
                {
                    import::close(m_SpuFD);
                    import::printf(str::T_T_yySound_ERROR_lseek_failed);
                    return -1;
                }
                if (import::lseek(m_SpuFD, 0, 0) < 0)
                {
                    import::close(m_SpuFD);
                    import::printf(str::T_T_yySound_ERROR_lseek_failed);
                    return -1;
                }
                m_SpuMem = static_cast<int32_t>(NewSPUMemory((static_cast<UINT>(flen) + 0x3F) & 0xFFFFFFC0u));
                if (m_SpuMem == 0)
                    return -1;
                m_SpuStep = 1;
                m_SpuCpyMem = m_SpuMem;
            }
        }
        else if (m_SpuStep == 1 && mLoadPause != 1)
        {
            const int32_t size = import::read(m_SpuFD, static_cast<uint32_t>(mSpuTransPacketAddr), 0x8000);
            if (size < 1)
            {
                if (size < 0)
                {
                    import::printf(str::T_T_yySound_ERROR_read_BD);
                    import::close(m_SpuFD);
                    DeleteSPUMemory(static_cast<UINT>(m_SpuMem));
                    m_SpuStep = 0;
                    return -1;
                }
                import::close(m_SpuFD);
                m_SpuStep = 0;
                return m_SpuMem;
            }
            int32_t trsize = size + 0x3F;
            if (trsize < 0)
                trsize = size + 0x7E;
            if (import::sceHSyn_VoiceTrans(0, static_cast<uint32_t>(mSpuTransPacketAddr), static_cast<uint32_t>(m_SpuCpyMem), static_cast<uint32_t>((trsize >> 6) << 6)) != 0)
            {
                import::printf(str::T_T_yySound_ERROR_sceHSyn_VoisTrans);
                import::close(m_SpuFD);
                DeleteSPUMemory(static_cast<UINT>(m_SpuMem));
                m_SpuStep = 0;
                return -1;
            }
            m_SpuCpyMem = m_SpuCpyMem + size;
        }
        return 0;
    }

    int32_t CloseFileSPU(UINT mem)
    {
        return DeleteSPUMemory(mem) == 1 ? 0 : -1;
    }

    // Opens a joint file (its index of 4 byte aligned size is read into IOP memory, the body stays open as the wave file's body); 0 or -1.
    int32_t OpenJointFile(IopPtr<char> fname, IopPtr<ST_WAVE_FILE> pWF)
    {
        Frame stack(frame::OpenJointFile);
        const IopPtr<UINT> idxsize{stack.local(local::OpenJointFile::idxsize)};
        const IopPtr<int32_t> oldstat{stack.local(local::OpenJointFile::oldstat)};
        const int32_t file = import::open(fname.address, 1);
        if (file < 0)
            return -1;
        UINT rsize = static_cast<UINT>(import::read(file, idxsize.address, 4));
        if (rsize != 4)
        {
            import::printf(str::T_T_yySound_ERROR_OpenJointFile_ReadError_s, fname.address);
            import::close(file);
            return -1;
        }
        if ((*idxsize & 3) != 0)
        {
            import::printf(str::T_T_yySound_ERROR_OpenJointFile_FileTypeError_s, fname.address);
            import::close(file);
            return -1;
        }
        import::lseek(file, 0, 0);
        import::CpuSuspendIntr(oldstat);
        const UINT mem = import::AllocSysMemory(0, *idxsize, 0);
        import::CpuResumeIntr(*oldstat);
        rsize = static_cast<UINT>(import::read(file, mem, *idxsize));
        if (rsize == *idxsize)
        {
            pWF->index = IopPtr<UINT>{mem};
            pWF->bd_addr = file;
            for (int32_t i = 0; i < 10; i = i + 1)
                pWF->spu[i].spu_addr = 0;
            return 0;
        }
        import::printf(str::T_T_yySound_ERROR_OpenJointFile_ReadError_s, fname.address);
        import::close(file);
        import::CpuSuspendIntr(oldstat);
        import::FreeSysMemory(mem);
        import::CpuResumeIntr(*oldstat);
        return -1;
    }

    void CloseJointFile(IopPtr<ST_WAVE_FILE> pWF)
    {
        Frame stack(frame::CloseJointFile);
        const IopPtr<int32_t> oldstat{stack.local(local::CloseJointFile::oldstat)};
        import::close(pWF->bd_addr);
        import::CpuSuspendIntr(oldstat);
        import::FreeSysMemory(pWF->index.address);
        import::CpuResumeIntr(*oldstat);
        for (int32_t i = 0; i < 10; i = i + 1)
        {
            if (pWF->spu[i].spu_addr != 0)
                DeleteSPUMemory(pWF->spu[i].spu_addr);
        }
    }

    // Clears and turns off the effects of both SPU2 cores, sets their master and effect volumes to full.
    void set_spu2()
    {
        Frame stack(frame::set_spu2);
        const IopPtr<sceSdEffectAttr> r_attr{stack.local(local::set_spu2::r_attr)};
        import::sceSdSetCoreAttr(10, 0);
        for (uint32_t i = 0; static_cast<int32_t>(i) < 2; i = i + 1)
        {
            import::sceSdSetAddr(i & 0xFFFF | 0x1D00, i * 0xFFFE0000u + 0x1FFFFF);
            r_attr->depth_L = 0;
            r_attr->depth_R = 0;
            r_attr->mode = 0x105;
            if (import::sceSdSetEffectAttr(static_cast<int32_t>(i), r_attr) != 0)
                import::printf(str::yySoundIOP_Error_EffectClear);
            r_attr->mode = 0;
            if (import::sceSdSetEffectAttr(static_cast<int32_t>(i), r_attr) != 0)
                import::printf(str::yySoundIOP_Error_EffectOFF);
            import::sceSdSetCoreAttr(i & 0xFFFF | 2, 1);
            import::sceSdSetParam(i & 0xFFFF | 0xB80, 0x3FFF);
            import::sceSdSetParam(i & 0xFFFF | 0xC80, 0x3FFF);
            import::sceSdSetParam(i & 0xFFFF | 0x980, 0x3FFF);
            import::sceSdSetParam(i & 0xFFFF | 0xA80, 0x3FFF);
        }
    }

    // ---- playing state

    // The play id of a wave track (the track number plus 0x10000000), -1 when the track holds no file.
    int32_t GetWavePlayID(BYTE track)
    {
        if (mWaveTrack[track].fno == -1)
            return -1;
        return track + 0x10000000;
    }

    // The number of voices the sound effect module has for the track's play id (it fills mVnum), -1 when the track is not in use.
    int32_t Wave_isInPlay(BYTE track, IopPtr<ST_WAVE_TRACK> pWT)
    {
        Frame stack(frame::Wave_isInPlay);
        const IopPtr<int32_t> oldstat{stack.local(local::Wave_isInPlay::oldstat)};
        if (pWT->fno == -1)
            return -1;
        const int32_t id = GetWavePlayID(track);
        if (id == -1)
            return -1;
        import::CpuSuspendIntr(oldstat);
        mCnum = import::sceHSyn_SERetrieveVoiceNumberByID(synthCtx.ptr(), track + 0x10, id, mVnum.address, 0x10);
        import::CpuResumeIntr(*oldstat);
        return mCnum;
    }

    // 1 when modmidi is playing the track's song (bit 1 of the environment's status), else -1.
    int32_t Midi_isInPlay(BYTE track, IopPtr<ST_MIDI_TRACK> pMT)
    {
        (void)pMT;
        const IopPtr<sceMidiEnv> env{midiCtx->buffGrp->buffCtx[track * 2 + 1].buff.address};
        return (env->status & 2) == 0 ? -1 : 1;
    }

    // ---- starting, stopping and fading

    // Starts the track's MIDI song from the beginning; on a failure the track is cleared and -1 returned.
    int32_t MidiPlayNow(BYTE track, IopPtr<ST_MIDI_TRACK> pMT)
    {
        Frame stack(frame::MidiPlayNow);
        const IopPtr<int32_t> oldstat{stack.local(local::MidiPlayNow::oldstat)};
        import::CpuSuspendIntr(oldstat);
        import::sceHSyn_ResetAllControler(synthCtx.ptr(), track);
        import::CpuResumeIntr(*oldstat);
        if (import::sceMidi_SelectMidi(midiCtx.ptr(), track, 0) != 0)
        {
            ClearMidiTrack(pMT);
            import::printf(str::T_T_yySound_ERROR_sceMidi_SelectMidi);
            return -1;
        }
        if (import::sceMidi_MidiSetLocation(midiCtx.ptr(), track, 0) != 0)
        {
            ClearMidiTrack(pMT);
            import::printf(str::T_T_yySound_ERROR_sceMidi_MidiSetLocation);
            return -1;
        }
        import::CpuSuspendIntr(oldstat);
        const int32_t ret = import::sceMidi_MidiPlaySwitch(midiCtx.ptr(), track, 1);
        import::CpuResumeIntr(*oldstat);
        if (ret == 0)
            return 0;
        ClearMidiTrack(pMT);
        import::printf(str::T_T_yySound_ERROR_sceMidi_MidiPlaySwitch);
        return -1;
    }

    // Starts the wave track's sound effect: a note on message (the program, the note, velocity 0x7F) to the sound effect module.
    int32_t WavePlayNow(BYTE track, IopPtr<ST_WAVE_TRACK> pWT)
    {
        Frame stack(frame::WavePlayNow);
        const IopPtr<int32_t> oldstat{stack.local(local::WavePlayNow::oldstat)};
        import::CpuSuspendIntr(oldstat);
        import::sceHSyn_ResetAllControler(synthCtx.ptr(), track + 0x10);
        const int32_t id = GetWavePlayID(track);
        const int32_t ret = import::sceSEIn_PutMsg(seinCtx.ptr(), track, id,
                                                   (pWT->program & 0x7Fu) << 8 | static_cast<uint32_t>(static_cast<int16_t>(pWT->note & 0x7F)) << 16 | 0xA0u | 0x7F000000u,
                                                   0x40);
        import::CpuResumeIntr(*oldstat);
        if (ret == 0)
            return 0;
        ClearWaveTrack(pWT);
        import::printf(str::T_T_yySound_ERROR_sceSEIn_NoteOn);
        return -1;
    }

    int32_t MidiStopNow(BYTE track, IopPtr<ST_MIDI_TRACK> pMT)
    {
        Frame stack(frame::MidiStopNow);
        const IopPtr<int32_t> oldstat{stack.local(local::MidiStopNow::oldstat)};
        import::CpuSuspendIntr(oldstat);
        import::sceMidi_MidiPlaySwitch(midiCtx.ptr(), track, 0);
        import::CpuResumeIntr(*oldstat);
        ClearMidiTrack(pMT);
        return 0;
    }

    // Stops the wave track's sound effect: the note off message.
    int32_t WaveStopNow(BYTE track, IopPtr<ST_WAVE_TRACK> pWT)
    {
        Frame stack(frame::WaveStopNow);
        const IopPtr<int32_t> oldstat{stack.local(local::WaveStopNow::oldstat)};
        import::CpuSuspendIntr(oldstat);
        const int32_t id = GetWavePlayID(track);
        import::sceSEIn_PutMsg(seinCtx.ptr(), track, id, (pWT->program & 0x7Fu) << 8 | static_cast<uint32_t>(static_cast<int16_t>(pWT->note & 0x7F)) << 16 | 0xA0u, 0);
        import::CpuResumeIntr(*oldstat);
        ClearWaveTrack(pWT);
        return 0;
    }

    // One step of the track's volume fade (volume 0 to 0x7FFF toward the goal by `step`); stops the song when it reaches 0.
    void MidiFader(int32_t track, IopPtr<ST_MIDI_TRACK> pMT)
    {
        Frame stack(frame::MidiFader);
        const IopPtr<int32_t> oldstat{stack.local(local::MidiFader::oldstat)};
        if (pMT->step == 0)
            return;
        if (static_cast<int32_t>(pMT->goal) < static_cast<int32_t>(pMT->volume))
        {
            pMT->volume = static_cast<int16_t>(pMT->volume - pMT->step);
            if (static_cast<int32_t>(pMT->volume) <= static_cast<int32_t>(pMT->goal))
            {
                pMT->volume = static_cast<int16_t>(pMT->goal);
                pMT->step = 0;
            }
        }
        else
        {
            pMT->volume = static_cast<int16_t>(pMT->volume + pMT->step);
            if (static_cast<int32_t>(pMT->goal) <= static_cast<int32_t>(pMT->volume))
            {
                pMT->volume = static_cast<int16_t>(pMT->goal);
                pMT->step = 0;
            }
        }
        if (pMT->volume == 0)
        {
            MidiStopNow(static_cast<BYTE>(track), pMT);
        }
        else
        {
            import::CpuSuspendIntr(oldstat);
            int32_t volume = pMT->volume;
            if (volume < 0)
                volume = volume + 0x7F;
            import::sceMidi_MidiVolumeChange(midiCtx.ptr(), track, 0xFF, static_cast<uint32_t>(volume >> 7 & 0xFF));
            import::CpuResumeIntr(*oldstat);
        }
    }

    // One step of the wave track's left and right volume fades; sets the volume of each voice the track plays on, or stops it when both reach 0.
    void WaveVolumeFader(BYTE track, IopPtr<ST_WAVE_TRACK> pWT)
    {
        if (pWT->lstep == 0 && pWT->rstep == 0)
            return;
        if (pWT->lstep != 0)
        {
            if (static_cast<int32_t>(pWT->lgoal) < static_cast<int32_t>(pWT->lvolume))
            {
                pWT->lvolume = static_cast<int16_t>(pWT->lvolume - pWT->lstep);
                if (static_cast<int32_t>(pWT->lvolume) <= static_cast<int32_t>(pWT->lgoal))
                {
                    pWT->lvolume = static_cast<int16_t>(pWT->lgoal);
                    pWT->lstep = 0;
                }
            }
            else if (static_cast<int32_t>(pWT->lvolume) < static_cast<int32_t>(pWT->lgoal))
            {
                pWT->lvolume = static_cast<int16_t>(pWT->lvolume + pWT->lstep);
                if (static_cast<int32_t>(pWT->lgoal) <= static_cast<int32_t>(pWT->lvolume))
                {
                    pWT->lvolume = static_cast<int16_t>(pWT->lgoal);
                    pWT->lstep = 0;
                }
            }
        }
        if (pWT->rstep != 0)
        {
            if (static_cast<int32_t>(pWT->rgoal) < static_cast<int32_t>(pWT->rvolume))
            {
                pWT->rvolume = static_cast<int16_t>(pWT->rvolume - pWT->rstep);
                if (static_cast<int32_t>(pWT->rvolume) <= static_cast<int32_t>(pWT->rgoal))
                {
                    pWT->rvolume = static_cast<int16_t>(pWT->rgoal);
                    pWT->rstep = 0;
                }
            }
            else if (static_cast<int32_t>(pWT->rvolume) < static_cast<int32_t>(pWT->rgoal))
            {
                pWT->rvolume = static_cast<int16_t>(pWT->rvolume + pWT->rstep);
                if (static_cast<int32_t>(pWT->rgoal) <= static_cast<int32_t>(pWT->rvolume))
                {
                    pWT->rvolume = static_cast<int16_t>(pWT->rgoal);
                    pWT->rstep = 0;
                }
            }
        }
        if (pWT->lvolume == 0 && pWT->rvolume == 0)
        {
            WaveStopNow(track, pWT);
            return;
        }
        for (int32_t k = 0; k < mCnum; k = k + 1)
        {
            const char voice = mVnum[k];
            if (0x17 < voice)
                mVnum[k] = static_cast<char>(mVnum[k] - 0x18);
            const uint16_t core = static_cast<uint16_t>(0x17 < voice);
            if (-1 < pWT->lvolume)
                import::sceSdSetParam(static_cast<uint32_t>(core | static_cast<int16_t>(mVnum[k]) << 1), static_cast<uint32_t>(static_cast<int32_t>(pWT->lvolume)));
            if (-1 < pWT->rvolume)
                import::sceSdSetParam(static_cast<uint32_t>(core | 0x100 | static_cast<int16_t>(mVnum[k]) << 1), static_cast<uint32_t>(static_cast<int32_t>(pWT->rvolume)));
        }
    }

    // One step of the wave track's pitch fade; sets the pitch of each voice the track plays on.
    void WavePitchFader(BYTE track, IopPtr<ST_WAVE_TRACK> pWT)
    {
        (void)track;
        if (pWT->pstep == 0)
            return;
        if (pWT->pgoal < pWT->pitch)
        {
            pWT->pitch = static_cast<int16_t>(pWT->pitch - pWT->pstep);
            if (pWT->pitch <= pWT->pgoal)
            {
                pWT->pitch = pWT->pgoal;
                pWT->pstep = 0;
            }
        }
        else if (pWT->pitch < pWT->pgoal)
        {
            pWT->pitch = static_cast<int16_t>(pWT->pitch + pWT->pstep);
            if (pWT->pgoal <= pWT->pitch)
            {
                pWT->pitch = pWT->pgoal;
                pWT->pstep = 0;
            }
        }
        const uint16_t defpitch = pWT->defpitch;
        const int16_t pitch = pWT->pitch;
        const uint32_t base = import::sceSdNote2Pitch(0x3C, 0, static_cast<uint32_t>(static_cast<int32_t>(pWT->note)), 0);
        uint32_t value = static_cast<uint32_t>(defpitch) + static_cast<uint32_t>(static_cast<int32_t>(pitch)) - 0x1000u + (base & 0xFFFFu);
        if (static_cast<int32_t>(value) < 0)
            value = 0;
        if (0x3FFF < static_cast<int32_t>(value))
            value = 0x3FFF;
        for (int32_t k = 0; k < mCnum; k = k + 1)
        {
            const char voice = mVnum[k];
            if (0x17 < voice)
                mVnum[k] = static_cast<char>(mVnum[k] - 0x18);
            const uint16_t core = static_cast<uint16_t>(0x17 < voice);
            import::sceSdSetParam(static_cast<uint32_t>(core | 0x200 | static_cast<int16_t>(mVnum[k]) << 1), value & 0xFFFFu);
        }
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
IOP_FN(yysound, OpenFileIOP)
IOP_FN(yysound, CloseFileIOP)
IOP_FN(yysound, NewSPUMemory)
IOP_FN(yysound, DeleteSPUMemory)
IOP_FN(yysound, CloseFileSPU)
IOP_FN(yysound, OpenJointFile)
IOP_FN(yysound, CloseJointFile)
IOP_FN(yysound, set_spu2)
IOP_FN(yysound, GetWavePlayID)
IOP_FN(yysound, Wave_isInPlay)
IOP_FN(yysound, Midi_isInPlay)
IOP_FN(yysound, MidiPlayNow)
IOP_FN(yysound, WavePlayNow)
IOP_FN(yysound, MidiStopNow)
IOP_FN(yysound, WaveStopNow)
IOP_FN(yysound, MidiFader)
IOP_FN(yysound, WaveVolumeFader)
IOP_FN(yysound, WavePitchFader)
IOP_FN(yysound, ClearMidiTrack)
IOP_FN(yysound, ClearWaveTrack)
IOP_FN(yysound, AddFilePutPos)
IOP_FN(yysound, AddRequestPutPos)
IOP_FN(yysound, CmdMidiOpen)
