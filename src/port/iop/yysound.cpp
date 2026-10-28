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
        Frame stack(frame::WaveVolumeFader); // WaveStopNow's frame sits below this one, as in the original
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

    // ---- commands from the EE: each queues a request for the timer thread (the commands of file number 0 to 7 for the wave files, 8 to 15 for the
    // MIDI files in mCommandList) or sets the track directly when no file is playing on it

    // Sets the tempo of a MIDI track (of the file it plays, or of the file `fno` when it plays none).
    int32_t CmdMidiSetTempo(IopPtr<ST_MIDI_TEMPO> data, SBYTE fno)
    {
        ee_put[4] = data->id;
        if (data->track < 0x10)
        {
            BYTE file = static_cast<BYTE>(fno);
            if (mMidiTrack[data->track].fno != -1)
                file = static_cast<BYTE>(mMidiTrack[data->track].fno);
            if (file == 0xFF)
            {
                mMidiTrack[data->track].tempo = data->tempo;
            }
            else
            {
                const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(static_cast<int8_t>(file) + 8);
                ST_COMMAND_ONE &request = pCL->list[pCL->PutPos];
                request.command = 0x1026;
                request.process = 0;
                request.track = data->track;
                request.tempo = data->tempo;
                request.id = data->id;
                if (AddRequestPutPos(pCL, file) == -3)
                    return -3;
            }
            return 1;
        }
        import::printf(str::T_T_yySound_ERROR_CmdMidiSetTempo_track_number_over_d, data->track);
        return -1;
    }

    int32_t CmdMidiSetVolume(IopPtr<ST_MIDI_VOL> data, SBYTE fno)
    {
        ee_put[4] = data->id;
        if (data->track < 0x10)
        {
            if (data->volume < 0x4000 && -1 < data->volume)
            {
                BYTE file = static_cast<BYTE>(fno);
                if (mMidiTrack[data->track].fno != -1)
                    file = static_cast<BYTE>(mMidiTrack[data->track].fno);
                if (file == 0xFF)
                {
                    mMidiTrack[data->track].volume = static_cast<SWORD>(data->volume);
                }
                else
                {
                    const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(static_cast<int8_t>(file) + 8);
                    ST_COMMAND_ONE &request = pCL->list[pCL->PutPos];
                    request.command = 0x1020;
                    request.process = 0;
                    request.track = data->track;
                    request.lvolume = static_cast<WORD>(data->volume);
                    request.id = data->id;
                    if (AddRequestPutPos(pCL, file) == -3)
                        return -3;
                }
                return 1;
            }
            import::printf(str::T_T_yySound_ERROR_CmdMidiSetVolume_volume_level_over_d, static_cast<uint32_t>(data->volume));
            return -1;
        }
        import::printf(str::T_T_yySound_ERROR_CmdMidiSetVolume_track_number_over_d, data->track);
        return -1;
    }

    int32_t CmdWaveSetPitch(IopPtr<ST_WAVE_PITCH> data, SBYTE fno)
    {
        ee_put[4] = data->id;
        if (data->track < 0x10)
        {
            if (data->pitch < 0x3FFF)
            {
                BYTE file = static_cast<BYTE>(fno);
                if (mWaveTrack[data->track].fno != -1)
                    file = static_cast<BYTE>(mWaveTrack[data->track].fno);
                if (file == 0xFF)
                {
                    mWaveTrack[data->track].pitch = data->pitch;
                }
                else
                {
                    const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(static_cast<int8_t>(file));
                    ST_COMMAND_ONE &request = pCL->list[pCL->PutPos];
                    request.command = 0x2022;
                    request.process = 0;
                    request.track = data->track;
                    request.pitch = data->pitch;
                    request.id = data->id;
                    if (AddRequestPutPos(pCL, file) == -3)
                        return -3;
                }
                return 1;
            }
            import::printf(str::T_T_yySound_ERROR_CmdWaveSetPitch_pitch_over_d, static_cast<uint32_t>(static_cast<int32_t>(data->pitch)));
            return -1;
        }
        import::printf(str::T_T_yySound_ERROR_CmdWaveSetPitch_track_number_over_d, data->track);
        return -1;
    }

    int32_t CmdWaveSetVolume(IopPtr<ST_WAVE_VOL> data, SBYTE fno)
    {
        ee_put[4] = data->id;
        if (data->track < 0x10)
        {
            if (data->lvolume < 0x4000 && -1 < data->lvolume)
            {
                if (data->rvolume < 0x4000 && -1 < data->rvolume)
                {
                    const BYTE track = data->track;
                    BYTE file = static_cast<BYTE>(fno);
                    if (mWaveTrack[track].fno != -1)
                        file = static_cast<BYTE>(mWaveTrack[track].fno);
                    if (file == 0xFF)
                    {
                        mWaveTrack[track].lvolume = static_cast<SWORD>(data->lvolume);
                        mWaveTrack[track].rvolume = static_cast<SWORD>(data->rvolume);
                    }
                    else
                    {
                        const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(static_cast<int8_t>(file));
                        ST_COMMAND_ONE &request = pCL->list[pCL->PutPos];
                        request.command = 0x2020;
                        request.process = 0;
                        request.track = data->track;
                        request.lvolume = static_cast<WORD>(data->lvolume);
                        request.rvolume = static_cast<WORD>(data->rvolume);
                        request.id = data->id;
                        if (AddRequestPutPos(pCL, file) == -3)
                            return -3;
                    }
                    return 1;
                }
                import::printf(str::T_T_yySound_ERROR_CmdWaveSetVolume_volume_level_over_R, static_cast<uint32_t>(data->rvolume));
                return -1;
            }
            import::printf(str::T_T_yySound_ERROR_CmdWaveSetVolume_volume_level_over_L, static_cast<uint32_t>(data->lvolume));
            return -1;
        }
        import::printf(str::T_T_yySound_ERROR_track_number_over);
        return -1;
    }

    int32_t CmdWavePitchFade(IopPtr<ST_WAVE_PITCH_FADE> data, SBYTE fno)
    {
        ee_put[4] = data->id;
        if (data->track < 0x10)
        {
            const BYTE track = data->track;
            BYTE file = static_cast<BYTE>(fno);
            if (mWaveTrack[track].fno != -1)
                file = static_cast<BYTE>(mWaveTrack[track].fno);
            if (file == 0xFF)
            {
                mWaveTrack[track].pgoal = data->pgoal;
                mWaveTrack[track].pstep = data->pstep;
            }
            else
            {
                const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(static_cast<int8_t>(file));
                ST_COMMAND_ONE &request = pCL->list[pCL->PutPos];
                request.command = 0x2024;
                request.process = 0;
                request.track = data->track;
                request.pgoal = data->pgoal;
                request.pstep = data->pstep;
                if (0x3FFF < request.pgoal)
                    request.pgoal = 0x3FFF;
                if (request.pgoal < -0x3FFF)
                    request.pgoal = -0x3FFF;
                request.id = data->id;
                if (AddRequestPutPos(pCL, file) == -3)
                    return -3;
            }
            return 1;
        }
        import::printf(str::T_T_yySound_ERROR_CmdWavePitchFade_track_number_over_d, data->track);
        return -1;
    }

    // Stops a MIDI track (a request to the file it plays); -1 when no file plays on it.
    int32_t CmdMidiStop(IopPtr<ST_MIDI_STOP> data)
    {
        ee_put[4] = data->id;
        if (data->track < 0x10)
        {
            const IopPtr<ST_MIDI_TRACK> pMT = mMidiTrack.at(data->track);
            if (pMT->fno == -1)
                return -1;
            const SBYTE file = pMT->fno;
            const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(file + 8);
            ST_COMMAND_ONE &request = pCL->list[pCL->PutPos];
            request.command = 0x1011;
            request.process = 0;
            request.track = data->track;
            request.id = data->id;
            return AddRequestPutPos(pCL, static_cast<BYTE>(pMT->fno));
        }
        import::printf(str::T_T_yySound_ERROR_CMD_MidiStop_track_number_over, data->track);
        return -1;
    }

    int32_t CmdWaveStop(IopPtr<ST_WAVE_STOP> data)
    {
        ee_put[4] = data->id;
        if (data->track < 0x10)
        {
            const IopPtr<ST_WAVE_TRACK> pWT = mWaveTrack.at(data->track);
            if (pWT->fno == -1)
                return -1;
            const SBYTE file = pWT->fno;
            const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(file);
            ST_COMMAND_ONE &request = pCL->list[pCL->PutPos];
            request.command = 0x2011;
            request.process = 0;
            request.track = data->track;
            request.id = data->id;
            return AddRequestPutPos(pCL, static_cast<BYTE>(pWT->fno));
        }
        import::printf(str::T_T_yySound_ERROR_CMD_WaveStop_track_number_over, data->track);
        return -1;
    }

    int32_t CmdMidiFade(IopPtr<ST_MIDI_FADE> data)
    {
        ee_put[4] = data->id;
        if (data->track < 0x10)
        {
            const IopPtr<ST_MIDI_TRACK> pMT = mMidiTrack.at(data->track);
            if (pMT->fno == -1)
                return -1;
            const SBYTE file = pMT->fno;
            const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(file + 8);
            ST_COMMAND_ONE &request = pCL->list[pCL->PutPos];
            request.command = 0x1021;
            request.process = 0;
            request.track = data->track;
            request.lgoal = data->goal;
            request.lstep = data->step;
            if (0x3FFF < request.lgoal)
                request.lgoal = 0x3FFF;
            request.id = data->id;
            return AddRequestPutPos(pCL, static_cast<BYTE>(pMT->fno));
        }
        import::printf(str::T_T_yySound_ERROR_CmdMidiFade_track_number_over_d, data->track);
        return -1;
    }

    int32_t CmdWaveFade(IopPtr<ST_WAVE_FADE> data)
    {
        ee_put[4] = data->id;
        if (data->track < 0x10)
        {
            const IopPtr<ST_WAVE_TRACK> pWT = mWaveTrack.at(data->track);
            if (pWT->fno == -1)
                return -1;
            const SBYTE file = pWT->fno;
            const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(file);
            ST_COMMAND_ONE &request = pCL->list[pCL->PutPos];
            request.command = 0x2021;
            request.process = 0;
            request.track = data->track;
            request.lgoal = data->lgoal;
            request.lstep = data->lstep;
            request.rgoal = data->rgoal;
            request.rstep = data->rstep;
            if (0x3FFF < request.lgoal)
                request.lgoal = 0x3FFF;
            if (0x3FFF < request.rgoal)
                request.rgoal = 0x3FFF;
            request.id = data->id;
            return AddRequestPutPos(pCL, static_cast<BYTE>(pWT->fno));
        }
        import::printf(str::T_T_yySound_ERROR_CmdWaveFade_track_number_over_d, data->track);
        return -1;
    }

    // Sets the track's volume (the whole track, not one file's play of it): the request goes to the queue of the file on the track, or the last queue.
    int32_t CmdMidiSetTrackVolume(IopPtr<ST_TRACK_VOL> data)
    {
        ee_put[4] = data->id;
        if (data->track < 0x10)
        {
            if (data->volume < 0x4000 && -1 < data->volume)
            {
                const IopPtr<ST_MIDI_TRACK> pMT = mMidiTrack.at(data->track);
                const IopPtr<ST_COMMAND_LIST> pCL = pMT->fno == -1 ? mCommandList.at(0x10) : mCommandList.at(pMT->fno + 8);
                ST_COMMAND_ONE &request = pCL->list[pCL->PutPos];
                request.command = 0x1025;
                request.process = 0;
                request.track = data->track;
                request.lvolume = static_cast<WORD>(data->volume);
                if (0x3FFF < request.lvolume)
                    request.lvolume = 0x3FFF;
                request.id = data->id;
                return AddRequestPutPos(pCL, static_cast<BYTE>(pMT->fno));
            }
            import::printf(str::T_T_yySound_ERROR_MidiSetTrackVolume_volume_level_over_d, static_cast<uint32_t>(data->volume));
            return -1;
        }
        import::printf(str::T_T_yySound_ERROR_MidiSetTrackVolume_track_number_over_d, data->track);
        return -1;
    }

    int32_t CmdWaveSetTrackVolume(IopPtr<ST_TRACK_VOL> data)
    {
        ee_put[4] = data->id;
        if (data->track < 0x10)
        {
            if (data->volume < 0x4000 && -1 < data->volume)
            {
                const IopPtr<ST_WAVE_TRACK> pWT = mWaveTrack.at(data->track);
                const IopPtr<ST_COMMAND_LIST> pCL = pWT->fno == -1 ? mCommandList.at(0x10) : mCommandList.at(pWT->fno);
                ST_COMMAND_ONE &request = pCL->list[pCL->PutPos];
                request.command = 0x2025;
                request.process = 0;
                request.track = data->track;
                request.lvolume = static_cast<WORD>(data->volume);
                request.id = data->id;
                return AddRequestPutPos(pCL, static_cast<BYTE>(pWT->fno));
            }
            import::printf(str::T_T_yySound_ERROR_WaveSetTrackVolume_volume_level_over_d, static_cast<uint32_t>(data->volume));
            return -1;
        }
        import::printf(str::T_T_yySound_ERROR_MidiSetTrackVolume_track_number_over_d, data->track);
        return -1;
    }

    // The voice file the EE names: two bytes (kept as they are) and a pitch step (a third of a note step per unit) when it is not 0.
    int32_t CmdVoiceFile(IopPtr<BYTE> data)
    {
        mVoiceFile[0] = data[0];
        mVoiceFile[1] = data[1];
        if (data[2] != 0)
            mVoicePitch = static_cast<WORD>(static_cast<WORD>(data[2]) * 3);
        return 1;
    }

    // The volume of a MIDI track, -1 when no file plays on it.
    int32_t CmdMidiGetVolume(BYTE track)
    {
        if (mMidiTrack[track].fno == -1)
            return -1;
        return mMidiTrack[track].volume;
    }

    // The volume of a wave track: the right volume in the high 16 bits, the left in the low ones (the left sign-extended); -1 when no file plays on it.
    int32_t CmdWaveGetVolume(BYTE track)
    {
        if (mWaveTrack[track].fno == -1)
            return -1;
        return static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(mWaveTrack[track].rvolume)) * 0x10000u) + mWaveTrack[track].lvolume;
    }

    int32_t CmdSetCheckID(WORD id)
    {
        m_checkID = id;
        if (id == 0)
            ee_put[5] = 0;
        return id;
    }

    // The EE gives the address it wants the 64 bytes of state sent to.
    int32_t CmdInit(IopPtr<UINT> data)
    {
        mEEaddress = *data;
        return 0;
    }

    void ReportFileRequestList()
    {
        uint16_t count = mFileList->PutPos;
        if (count < mFileList->GetPos)
            count = static_cast<uint16_t>(count + 0x20);
        import::printf(str::FileRequestCount_d, static_cast<uint32_t>(count) - static_cast<uint32_t>(static_cast<uint16_t>(mFileList->GetPos)));
    }

    // Closes a MIDI file: the file is no longer open (bit 1), and the request list gets a stop request, then one that releases the file's entry.
    int32_t CmdMidiClose(IopPtr<ST_MIDI_CLOSE> data)
    {
        ee_put[4] = data->id;
        if (data->fno < 8)
        {
            if ((mMidiFile[data->fno].open & 2) == 0)
            {
                import::printf(str::T_T_yySound_ERROR_CmdMidiClose_file_is_not_open, data->fno);
                return -1;
            }
            mMidiFile[data->fno].open = mMidiFile[data->fno].open & 1;
            const uint32_t fno = data->fno;
            const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(fno + 8);
            const BYTE put = mCommandList[fno + 8].PutPos;
            pCL->list[put].command = 0x1012;
            pCL->list[put].id = data->id;
            if (AddRequestPutPos(pCL, data->fno) == -3)
                return -3;
            ST_COMMAND_ONE &request = pCL->list[mCommandList[fno + 8].PutPos];
            request.command = 0x1003;
            request.fileNo = mFileList->PutPos;
            request.id = data->id;
            if (AddFilePutPos() == -3)
                return -3;
            return AddRequestPutPos(pCL, data->fno);
        }
        import::printf(str::T_T_yySound_ERROR_CmdMidiClose_file_number_over_d, data->fno);
        return -1;
    }

    int32_t CmdWaveClose(IopPtr<ST_WAVE_CLOSE> data)
    {
        ee_put[4] = data->id;
        if (data->fno < 8)
        {
            if ((mWaveFile[data->fno].open & 2) == 0)
            {
                import::printf(str::T_T_yySound_ERROR_CmdWaveClose_file_is_not_open, data->fno);
                return -1;
            }
            mWaveFile[data->fno].open = mWaveFile[data->fno].open & 1;
            const BYTE fno = data->fno;
            const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(fno);
            const BYTE put = mCommandList[fno].PutPos;
            pCL->list[put].command = 0x2012;
            pCL->list[put].id = data->id;
            if (AddRequestPutPos(pCL, data->fno) == -3)
                return -3;
            ST_COMMAND_ONE &request = pCL->list[mCommandList[fno].PutPos];
            request.command = 0x2003;
            request.fileNo = mFileList->PutPos;
            request.id = data->id;
            if (AddFilePutPos() == -3)
                return -3;
            return AddRequestPutPos(pCL, data->fno);
        }
        import::printf(str::T_T_yySound_ERROR_CmdWaveClose_file_number_over_d, data->fno);
        return -1;
    }

    // Opens a sequence file for a MIDI file that is already open.
    int32_t CmdSqOpen(IopPtr<ST_SQ_OPEN> data)
    {
        ee_put[4] = data->id;
        if (data->midifno < 8)
        {
            if ((mMidiFile[data->midifno].open & 2) == 0)
                return -1;
            if ((mMidiFile[data->fno].open & 2) != 0)
            {
                Frame stack(frame::CmdSqOpen);
                const IopPtr<ST_MIDI_CLOSE> mc{stack.local(local::CmdSqOpen::mc)};
                mc->fno = data->fno;
                const int32_t ret = CmdMidiClose(mc);
                if (ret != 1)
                    return ret;
            }
            const uint32_t entry = mFileList.address + offsetof(ST_FILE_LIST, list) + mFileList->PutPos * static_cast<uint32_t>(sizeof(ST_FILE_ONE));
            import::strcpy(entry + offsetof(ST_FILE_ONE, SQname), data.address + offsetof(ST_SQ_OPEN, sqname));
            mMidiFile[data->fno].open |= 2;
            const BYTE fno = data->fno;
            const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(fno + 8);
            ST_COMMAND_ONE &request = pCL->list[pCL->PutPos];
            request.command = 0x2002;
            request.process = 0;
            request.fileNo = mFileList->PutPos;
            request.id = data->id;
            if (AddFilePutPos() == -3)
                return -3;
            return AddRequestPutPos(mCommandList.at(fno + 8), data->fno);
        }
        import::printf(str::T_T_yySound_ERROR_file_number_over);
        return -1;
    }

    // Opens a wave file (header and body names, and its type): closes it first if it is open.
    int32_t CmdWaveOpen(IopPtr<ST_WAVE_OPEN> data)
    {
        ee_put[4] = data->id;
        if ((mWaveFile[data->fno].open & 2) != 0)
        {
            Frame stack(frame::CmdWaveOpen);
            const IopPtr<ST_WAVE_CLOSE> wc{stack.local(local::CmdWaveOpen::wc)};
            wc->fno = data->fno;
            const int32_t ret = CmdWaveClose(wc);
            if (ret != 1)
                return ret;
        }
        const uint32_t put = mFileList->PutPos;
        const uint32_t entry = mFileList.address + offsetof(ST_FILE_LIST, list) + put * static_cast<uint32_t>(sizeof(ST_FILE_ONE));
        import::strcpy(entry + offsetof(ST_FILE_ONE, HDname), data.address + offsetof(ST_WAVE_OPEN, hdname));
        import::strcpy(entry + offsetof(ST_FILE_ONE, BDname), data.address + offsetof(ST_WAVE_OPEN, bdname));
        mFileList->list[put].type = data->type;
        mWaveFile[data->fno].open |= 2;
        const BYTE fno = data->fno;
        const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(fno);
        ST_COMMAND_ONE &request = pCL->list[pCL->PutPos];
        request.command = 0x2001;
        request.process = 0;
        request.fileNo = mFileList->PutPos;
        request.id = data->id;
        if (AddFilePutPos() == -3)
            return -3;
        return AddRequestPutPos(mCommandList.at(fno), data->fno);
    }

    // Asks for the joint's wave data to be copied to the SPU / cleared from it (the wave file must be open).
    int32_t CmdCopyJointSPU(IopPtr<ST_JOINT_SPU> data)
    {
        ee_put[4] = data->id;
        if ((mWaveFile[data->fno].open & 2) == 0)
        {
            import::printf(str::T_T_yySound_ERROR_CmdCopyJointSPU_NotOpen);
            return -1;
        }
        const BYTE fno = data->fno;
        const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(fno);
        ST_COMMAND_ONE &request = pCL->list[pCL->PutPos];
        request.command = 0x2200;
        request.process = 0;
        request.id = data->id;
        request.jno = data->jno;
        if (AddFilePutPos() == -3)
            return -3;
        return AddRequestPutPos(mCommandList.at(fno), data->fno);
    }

    int32_t CmdClearJointSPU(IopPtr<ST_JOINT_SPU> data)
    {
        ee_put[4] = data->id;
        if ((mWaveFile[data->fno].open & 2) == 0)
        {
            import::printf(str::T_T_yySound_ERROR_CmdClearJointSPU_NotOpen);
            return -1;
        }
        const BYTE fno = data->fno;
        const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(fno);
        ST_COMMAND_ONE &request = pCL->list[pCL->PutPos];
        request.command = 0x2201;
        request.process = 0;
        request.id = data->id;
        request.jno = data->jno;
        if (AddFilePutPos() == -3)
            return -3;
        return AddRequestPutPos(mCommandList.at(fno), data->fno);
    }

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

    // Plays a MIDI file on a track: a play request, then the track's remembered volume and tempo as requests of their own.
    int32_t CmdMidiPlay(IopPtr<ST_MIDI_PLAY> data)
    {
        Frame stack(frame::CmdMidiPlay);
        ee_put[4] = data->id;
        if (data->fno < 8)
        {
            if (data->track < 0x10)
            {
                if ((mMidiFile[data->fno].open & 2) == 0)
                {
                    import::printf(str::T_T_yySound_ERROR_CmdMidiPlay_file_is_not_open, data->fno);
                    return -1;
                }
                const BYTE fno = data->fno;
                ST_COMMAND_LIST &commands = mCommandList[fno + 8];
                ST_COMMAND_ONE &request = commands.list[commands.PutPos];
                request.command = 0x1010;
                request.process = 0;
                request.track = data->track;
                request.loop = data->loop;
                request.id = data->id;
                int32_t ret = AddRequestPutPos(mCommandList.at(fno + 8), data->fno);
                if (ret == -3)
                    return -3;
                if (-1 < mMidiTrack[data->track].volume)
                {
                    const IopPtr<ST_MIDI_VOL> cmvol{stack.local(local::CmdMidiPlay::cmvol)};
                    cmvol->track = data->track;
                    cmvol->volume = mMidiTrack[data->track].volume;
                    cmvol->id = data->id;
                    ret = CmdMidiSetVolume(cmvol, static_cast<SBYTE>(data->fno));
                    if (ret != 1)
                        return ret;
                }
                if (mMidiTrack[data->track].volume == -1)
                    mMidiTrack[data->track].volume = 0x27FF;
                if (mMidiTrack[data->track].tempo == 0)
                    return 1;
                const IopPtr<ST_MIDI_TEMPO> cmtempo{stack.local(local::CmdMidiPlay::cmtempo)};
                cmtempo->track = data->track;
                cmtempo->tempo = mMidiTrack[data->track].tempo;
                cmtempo->id = data->id;
                return CmdMidiSetTempo(cmtempo, static_cast<SBYTE>(data->fno));
            }
            import::printf(str::T_T_yySound_ERROR_CmdMidiPlay_track_number_over_d, data->track);
            return -1;
        }
        import::printf(str::T_T_yySound_ERROR_CmdMidiPlay_file_number_over_d, data->fno);
        return -1;
    }

    // Plays a wave (a joint of the file) on a track, then the track's remembered volume (0x1FFF when none) and pitch as requests of their own.
    int32_t CmdWavePlay(IopPtr<ST_WAVE_PLAY> data)
    {
        Frame stack(frame::CmdWavePlay);
        ee_put[4] = data->id;
        if (data->fno < 8)
        {
            if (data->track < 0x10)
            {
                if ((mWaveFile[data->fno].open & 2) == 0)
                {
                    import::printf(str::T_T_yySound_ERROR_CmdWavePlay_file_is_not_open, data->fno);
                    return -1;
                }
                const BYTE fno = data->fno;
                ST_COMMAND_LIST &commands = mCommandList[fno];
                ST_COMMAND_ONE &request = commands.list[commands.PutPos];
                request.command = 0x2010;
                request.process = 0;
                request.track = data->track;
                request.program = data->program;
                request.jno = data->jno;
                request.loop = data->loop;
                request.note = data->note;
                request.id = data->id;
                int32_t ret = AddRequestPutPos(mCommandList.at(fno), data->fno);
                if (ret == -3)
                    return -3;
                if (-1 < mWaveTrack[data->track].lvolume || -1 < mWaveTrack[data->track].rvolume)
                {
                    const IopPtr<ST_WAVE_VOL> cwvol{stack.local(local::CmdWavePlay::cwvol)};
                    cwvol->track = data->track;
                    if (mWaveTrack[data->track].lvolume < 0)
                        cwvol->lvolume = 0x1FFF;
                    else
                        cwvol->lvolume = mWaveTrack[data->track].lvolume;
                    if (mWaveTrack[data->track].rvolume < 0)
                        cwvol->rvolume = 0x1FFF;
                    else
                        cwvol->rvolume = mWaveTrack[data->track].rvolume;
                    ret = CmdWaveSetVolume(cwvol, static_cast<SBYTE>(data->fno));
                    if (ret != 1)
                        return ret;
                }
                if (mWaveTrack[data->track].lvolume == -1)
                    mWaveTrack[data->track].lvolume = 0x1FFF;
                if (mWaveTrack[data->track].rvolume == -1)
                    mWaveTrack[data->track].rvolume = 0x1FFF;
                if (mWaveTrack[data->track].pitch == 0)
                    return 1;
                const IopPtr<ST_WAVE_PITCH> cwpitch{stack.local(local::CmdWavePlay::cwpitch)};
                cwpitch->track = data->track;
                cwpitch->pitch = mWaveTrack[data->track].pitch;
                return CmdWaveSetPitch(cwpitch, static_cast<SBYTE>(data->fno));
            }
            import::printf(str::T_T_yySound_ERROR_CmdWavePlay_track_number_over_d, data->track);
            return -1;
        }
        import::printf(str::T_T_yySound_ERROR_CmdWavePlay_file_number_over_d, data->fno);
        return -1;
    }

    // Plays a spoken text: the EE sends a string of kana codes (half-width katakana, 0xB1 to 0xDC, with the voiced marks 0xDE and 0xDF and the
    // small kana and long vowel signs after them) with a tone and a volume for each. Takes a free entry of the voice list (the first with
    // pos = -1), turns the codes into the voice numbers of the voice file (a "voice" is the sound of one mora) and queues the request that
    // starts it. -1 when the list is full.
    int32_t CmdVoicePlay(IopPtr<ST_VOICE_PLAY> data)
    {
        int32_t vno = -1;
        for (int32_t i = 0; i < 8; i++)
        {
            if (mVoiceList[i].pos == -1)
            {
                vno = i;
                break;
            }
        }
        if (vno == -1)
        {
            import::printf(str::T_T_yySound_ERROR_VOICE_LIST_BUFFER_FULL);
            return -1;
        }
        const uint32_t file = mVoiceFile[0];
        ST_COMMAND_LIST &commands = mCommandList[file];
        ST_COMMAND_ONE &request = commands.list[commands.PutPos];
        request.command = 0x2101;
        request.process = 0;
        request.program = static_cast<BYTE>(vno);
        request.note = data->note;
        request.id = data->id;
        IopPtr<BYTE> svpos{data.address + static_cast<uint32_t>(offsetof(ST_VOICE_PLAY, voice))};
        IopPtr<BYTE> stpos{data.address + static_cast<uint32_t>(offsetof(ST_VOICE_PLAY, tone))};
        IopPtr<WORD> slpos{data.address + static_cast<uint32_t>(offsetof(ST_VOICE_PLAY, volume))};
        const uint32_t entry = mVoiceList.address + static_cast<uint32_t>(vno) * static_cast<uint32_t>(sizeof(ST_VOICE_LIST));
        IopPtr<WORD> dvpos{entry + static_cast<uint32_t>(offsetof(ST_VOICE_LIST, voice))};
        IopPtr<BYTE> dtpos{entry + static_cast<uint32_t>(offsetof(ST_VOICE_LIST, tone))};
        IopPtr<WORD> dlpos{entry + static_cast<uint32_t>(offsetof(ST_VOICE_LIST, volume))};
        IopPtr<WORD> pLastCode{0}; // the code before; a long vowel sign as the first one reads address 0, as the original does
        while (*svpos != 0)
        {
            bool nflag = false;
            *dtpos = *stpos;
            *dlpos = *slpos;
            if (*svpos < 0xB1 || 0xDC < *svpos)
            {
                if (*svpos == 0xDD)
                {
                    *dvpos = 0x2C;
                }
                else if (*svpos == 0xA6)
                {
                    *dvpos = 0x2D;
                }
                else if (*svpos == 0xB0 || *svpos == 0x2D)
                {
                    // the long vowel sign: repeats the vowel of the code before (the table goes on past the 120 bytes the debug info gives it)
                    const uint16_t code = static_cast<uint16_t>(*pLastCode & 0xFFF);
                    if (code < 0x47)
                        *dvpos = TBL_CHOON[code];
                    else if (code < 0x100 || 0x1FF < code)
                        *dvpos = 0x2000;
                    else
                        *dvpos = load<uint8_t>(TBL_CHOON.address + code - 0xB9u);
                    nflag = true;
                }
                else if (*svpos == 0xAF)
                {
                    *dvpos = 0x1000;
                    nflag = true;
                }
                else
                {
                    *dvpos = 0x2000;
                    nflag = true;
                }
            }
            else
            {
                *dvpos = static_cast<WORD>(*svpos - 0xB1);
            }
            IopPtr<BYTE> nextVoice;
            IopPtr<BYTE> nextTone;
            IopPtr<WORD> nextVolume;
            if (nflag)
            {
                nextVoice = svpos + 1;
                nextTone = stpos + 1;
                nextVolume = slpos + 1;
            }
            else
            {
                if (svpos[1] == 0xDE)
                {
                    // the voiced mark
                    if (*svpos < 0xCA)
                        *dvpos = static_cast<WORD>(*dvpos + 0x29);
                    else
                        *dvpos = static_cast<WORD>(*dvpos + 0x24);
                    svpos = svpos + 1;
                    stpos = stpos + 1;
                    slpos = slpos + 1;
                }
                else if (svpos[1] == 0xDF)
                {
                    // the semi-voiced mark
                    *dvpos = static_cast<WORD>(*dvpos + 0x29);
                    svpos = svpos + 1;
                    stpos = stpos + 1;
                    slpos = slpos + 1;
                }
                nextVoice = svpos + 1;
                if (*nextVoice < 0xAC || 0xAE < *nextVoice)
                {
                    nextTone = stpos + 1;
                    nextVolume = slpos + 1;
                    if (0xA6 < *nextVoice && *nextVoice < 0xAC)
                    {
                        // a small vowel kana (0xA7 to 0xAB) after this one
                        if (*dvpos == 2)
                        {
                            if (0xA7 < *nextVoice)
                                *dvpos = 0x121;
                            if (0xA9 < *nextVoice)
                                *dvpos = 0x122;
                        }
                        else if (*dvpos == 0x1B)
                        {
                            *dvpos = static_cast<WORD>(*nextVoice + 0x7C);
                            if (0xA9 < *nextVoice)
                                *dvpos = static_cast<WORD>(*dvpos - 1);
                        }
                        else if (*dvpos == 0x12)
                        {
                            *dvpos = 0x127;
                        }
                        else if (*dvpos == 0x13)
                        {
                            *dvpos = 0x128;
                        }
                        else if (*dvpos == 0x3B)
                        {
                            *dvpos = 0x129;
                        }
                        else if (*dvpos == 0x3C)
                        {
                            *dvpos = 0x12A;
                        }
                        else if (*nextVoice == 0xAA)
                        {
                            if (*dvpos == 0xB)
                                *dvpos = 299;
                            else if (*dvpos == 0x10)
                                *dvpos = 300;
                            else if (*dvpos == 0x2F)
                                *dvpos = 0x12D;
                            else if (*dvpos == 0x34 || *dvpos == 0x39)
                                *dvpos = 0x12D;
                        }
                        nextVoice = svpos + 2;
                        nextTone = stpos + 2;
                        nextVolume = slpos + 2;
                    }
                }
                else
                {
                    // a small ya, yu or yo (0xAC to 0xAE) after this one: the contracted sound of the row the code is in
                    if (*dvpos < 0x27)
                    {
                        *dvpos = static_cast<WORD>(*dvpos - 6);
                        *dvpos = static_cast<WORD>(*dvpos / 5);
                        *dvpos = static_cast<WORD>(*dvpos * 3);
                        *dvpos = static_cast<WORD>((*dvpos - 0xAC) + *nextVoice);
                        *dvpos = static_cast<WORD>(*dvpos + 0x100);
                    }
                    else if (*dvpos == 0x27)
                    {
                        *dvpos = static_cast<WORD>(*nextVoice + 0x66);
                    }
                    else if (*dvpos < 0x35)
                    {
                        *dvpos = static_cast<WORD>(*dvpos - 0x2F);
                        *dvpos = static_cast<WORD>(*dvpos / 5);
                        *dvpos = static_cast<WORD>(*dvpos * 3);
                        *dvpos = static_cast<WORD>((*dvpos - 0xAC) + *nextVoice);
                        *dvpos = static_cast<WORD>(*dvpos + 0x115);
                    }
                    else
                    {
                        *dvpos = static_cast<WORD>(*dvpos - 0x39);
                        *dvpos = static_cast<WORD>(*dvpos / 5);
                        *dvpos = static_cast<WORD>(*dvpos * 3);
                        *dvpos = static_cast<WORD>((*dvpos - 0xAC) + *nextVoice);
                        *dvpos = static_cast<WORD>(*dvpos + 0x118);
                    }
                    nextVoice = svpos + 2;
                    nextTone = stpos + 2;
                    nextVolume = slpos + 2;
                }
            }
            slpos = nextVolume;
            stpos = nextTone;
            svpos = nextVoice;
            pLastCode = dvpos;
            dvpos = dvpos + 1;
            dtpos = dtpos + 1;
            dlpos = dlpos + 1;
        }
        *dvpos = 0xFFFF;
        int32_t i = 0;
        while (mVoiceList[vno].voice[i] != 0xFFFF)
            i++;
        mVoiceList[vno].tone[i] = 0xFF;
        mVoiceList[vno].note = data->note;
        mVoiceList[vno].counter = 0;
        mVoiceList[vno].track[0] = data->track[0];
        mVoiceList[vno].track[1] = data->track[1];
        return AddRequestPutPos(mCommandList.at(file), mVoiceFile[0]);
    }

    // The EE's request of command `command` with the data it sent: calls the handler; a command of no handler is an error (-1) and, when the EE
    // waits for it by m_checkID, lets the EE know with the error in ee_put[5].
    int32_t AddRequest(UINT command, IopPtr<void> data)
    {
        Frame stack(frame::AddRequest); // the handlers' own frames sit below this one, as the original's do (they read stale bytes of their locals)
        int32_t ret = -0xFF;
        switch (command)
        {
        case 0x1001: ret = CmdMidiOpen(IopPtr<ST_MIDI_OPEN>{data.address}); break;
        case 0x1003: ret = CmdMidiClose(IopPtr<ST_MIDI_CLOSE>{data.address}); break;
        case 0x1010: ret = CmdMidiPlay(IopPtr<ST_MIDI_PLAY>{data.address}); break;
        case 0x1011: ret = CmdMidiStop(IopPtr<ST_MIDI_STOP>{data.address}); break;
        case 0x1020: ret = CmdMidiSetVolume(IopPtr<ST_MIDI_VOL>{data.address}, -1); break;
        case 0x1021: ret = CmdMidiFade(IopPtr<ST_MIDI_FADE>{data.address}); break;
        case 0x1025: ret = CmdMidiSetTrackVolume(IopPtr<ST_TRACK_VOL>{data.address}); break;
        case 0x1026: ret = CmdMidiSetTempo(IopPtr<ST_MIDI_TEMPO>{data.address}, -1); break;
        case 0x2001: ret = CmdWaveOpen(IopPtr<ST_WAVE_OPEN>{data.address}); break;
        case 0x2002: ret = CmdSqOpen(IopPtr<ST_SQ_OPEN>{data.address}); break;
        case 0x2003: ret = CmdWaveClose(IopPtr<ST_WAVE_CLOSE>{data.address}); break;
        case 0x2010: ret = CmdWavePlay(IopPtr<ST_WAVE_PLAY>{data.address}); break;
        case 0x2011: ret = CmdWaveStop(IopPtr<ST_WAVE_STOP>{data.address}); break;
        case 0x2020: ret = CmdWaveSetVolume(IopPtr<ST_WAVE_VOL>{data.address}, -1); break;
        case 0x2021: ret = CmdWaveFade(IopPtr<ST_WAVE_FADE>{data.address}); break;
        case 0x2022: ret = CmdWaveSetPitch(IopPtr<ST_WAVE_PITCH>{data.address}, -1); break;
        case 0x2025: ret = CmdWaveSetTrackVolume(IopPtr<ST_TRACK_VOL>{data.address}); break;
        case 0x2101: ret = CmdVoicePlay(IopPtr<ST_VOICE_PLAY>{data.address}); break;
        case 0x2200: ret = CmdCopyJointSPU(IopPtr<ST_JOINT_SPU>{data.address}); break;
        case 0x2201: ret = CmdClearJointSPU(IopPtr<ST_JOINT_SPU>{data.address}); break;
        default: break;
        }
        if (ret == -3)
            import::printf(str::T_T_yySound_ERROR_COMMAND_LIST_BUFFER_OVER);
        if (ret == -0xFF)
        {
            ret = -1;
            if (m_checkID != 0 && m_checkID == load<WORD>(data.address))
            {
                ee_put[5] = -static_cast<int32_t>(static_cast<uint32_t>(m_checkID));
                m_checkID = 0;
            }
            import::printf(str::T_T_yySound_ERROR_COMMAND_EROR_0x_04X, command);
        }
        return ret;
    }

    // ---- requests run by the timer thread (what the commands from the EE queued): the file functions and the track functions

    // Takes the request of the file list that has been run off the list.
    void AddFileGetPos()
    {
        mFileList->GetPos = static_cast<BYTE>(mFileList->GetPos + 1);
        if (0x1F < mFileList->GetPos)
            mFileList->GetPos = 0;
    }

    // How many open MIDI files use the instrument file `hd_addr`.
    BYTE UseHD(UINT hd_addr)
    {
        BYTE uses = 0;
        for (int32_t i = 0; i < 8; i++)
        {
            if ((mMidiFile[i].open & 1) != 0 && static_cast<UINT>(mMidiFile[i].hd_addr) == hd_addr)
                uses = static_cast<BYTE>(uses + 1);
        }
        return uses;
    }

    // Stops the MIDI track `pCO->track`; `mode` 0 only lets go of the file, else the track is cleared (the file is closing).
    int32_t RunMidiStop(IopPtr<ST_COMMAND_ONE> pCO, BYTE mode)
    {
        Frame stack(frame::RunMidiStop);
        ee_put[4] = pCO->id;
        if (pCO->track < 0x10)
        {
            const IopPtr<ST_MIDI_TRACK> pMT = mMidiTrack.at(pCO->track);
            m_GetDefTempo = m_GetDefTempo & ~(1 << (pCO->track & 0x1F));
            if (pMT->fno < 0)
                return 0;
            if (mode == 0)
                pMT->fno = -1;
            else
                ClearMidiTrack(pMT);
            ee_put[1] = ee_put[1] & ~(1 << (pCO->track & 0x1F));
            const IopPtr<int32_t> oldstat{stack.local(local::RunMidiStop::oldstat)};
            import::CpuSuspendIntr(oldstat);
            const int32_t ret = import::sceMidi_MidiPlaySwitch(midiCtx.ptr(), pCO->track, 0);
            import::CpuResumeIntr(*oldstat);
            if (ret == 0)
                return 0;
            import::printf(str::T_T_yySound_ERROR_sceMidi_MidiPlayStop);
            return -2;
        }
        import::printf(str::T_T_yySound_ERROR_track_number_over);
        return -2;
    }

    // Stops every track that plays the MIDI file `fno`.
    int32_t RunMidiFStop(int32_t fno, IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunMidiFStop);
        ee_put[4] = pCO->id;
        for (int32_t i = 0; i < 0x10; i++)
        {
            if (static_cast<uint32_t>(static_cast<int32_t>(mMidiTrack[i].fno)) == (static_cast<uint32_t>(fno) & 0xFFu))
            {
                const IopPtr<ST_MIDI_STOP> ms{stack.local(local::RunMidiFStop::ms)};
                ms->track = static_cast<BYTE>(i);
                CmdMidiStop(ms);
            }
        }
        return 0;
    }

    // Sets the tempo of a track once its song is playing (-4: not yet, try again; at most 11 times).
    int32_t RunMidiSetTempo(IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunMidiSetTempo);
        if (pCO->process == 0)
        {
            ee_put[4] = pCO->id;
            if (0xF < pCO->track)
            {
                import::printf(str::T_T_yySound_ERROR_track_number_over);
                return -2;
            }
        }
        const BYTE track = pCO->track;
        const int32_t playing = Midi_isInPlay(pCO->track, mMidiTrack.at(track));
        if (playing < 1 && pCO->process < 0xB)
        {
            pCO->process = static_cast<BYTE>(pCO->process + 1);
            return -4;
        }
        const IopPtr<int32_t> oldstat{stack.local(local::RunMidiSetTempo::oldstat)};
        import::CpuSuspendIntr(oldstat);
        if (pCO->tempo == 0)
        {
            import::sceMidi_MidiSetAbsoluteTempo(midiCtx.ptr(), pCO->track, mMidiTrack[track].deftempo);
        }
        else
        {
            int16_t tempo = static_cast<int16_t>(mMidiTrack[track].deftempo + pCO->tempo);
            if (tempo < 0x14)
                tempo = 0x14;
            if (0xFF < tempo)
                tempo = 0xFF;
            import::sceMidi_MidiSetAbsoluteTempo(midiCtx.ptr(), pCO->track, static_cast<uint32_t>(tempo) & 0xFFu);
        }
        import::CpuResumeIntr(*oldstat);
        pCO->process = 0;
        mMidiTrack[track].tempo = pCO->tempo;
        return 0;
    }

    // Sets the volume of a track once its song is playing (-4: not yet, try again).
    int32_t RunMidiSetVolume(IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunMidiSetVolume);
        if (pCO->process == 0)
        {
            ee_put[4] = pCO->id;
            if (0xF < pCO->track)
            {
                import::printf(str::T_T_yySound_ERROR_track_number_over);
                return -2;
            }
            if (0x3FFF < pCO->lvolume)
            {
                import::printf(str::T_T_yySound_ERROR_volume_level_over);
                return -2;
            }
        }
        const BYTE track = pCO->track;
        const int32_t playing = Midi_isInPlay(pCO->track, mMidiTrack.at(track));
        if (playing < 1 && pCO->process < 0xB)
        {
            pCO->process = static_cast<BYTE>(pCO->process + 1);
            return -4;
        }
        const IopPtr<int32_t> oldstat{stack.local(local::RunMidiSetVolume::oldstat)};
        import::CpuSuspendIntr(oldstat);
        import::sceMidi_MidiVolumeChange(midiCtx.ptr(), pCO->track, 0xFF, (pCO->lvolume >> 7) & 0xFFu);
        import::CpuResumeIntr(*oldstat);
        mMidiTrack[track].volume = static_cast<SWORD>(pCO->lvolume);
        pCO->process = 0;
        return 0;
    }

    // Sets the volume of the synthesizer's track (the whole track, every file).
    int32_t RunMidiSetTVolume(IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunMidiSetTVolume);
        ee_put[4] = pCO->id;
        const IopPtr<int32_t> oldstat{stack.local(local::RunMidiSetTVolume::oldstat)};
        import::CpuSuspendIntr(oldstat);
        const int32_t ret = import::sceHSyn_SetVolume(synthCtx.ptr(), pCO->track, pCO->lvolume);
        import::CpuResumeIntr(*oldstat);
        if (ret == 0)
            return 0;
        return -2;
    }

    // Starts the fade of a track (MidiFader runs it).
    int32_t RunMidiFade(IopPtr<ST_COMMAND_ONE> pCO)
    {
        ee_put[4] = pCO->id;
        if (pCO->track < 0x10)
        {
            const BYTE track = pCO->track;
            if (mMidiTrack[track].fno == -1)
                return -2;
            mMidiTrack[track].goal = pCO->lgoal;
            mMidiTrack[track].step = pCO->lstep;
            return 0;
        }
        import::printf(str::T_T_yySound_ERROR_track_number_over);
        return -2;
    }

    // Closes a MIDI file: stops the tracks that play it, frees the sequence and, when no other open file uses the instrument file, that and
    // its sound data.
    int32_t RunMidiClose(int32_t fno, IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunMidiClose);
        const IopPtr<ST_MIDI_FILE> pMF = mMidiFile.at(fno);
        ee_put[4] = pCO->id;
        if ((pMF->open & 1) == 0)
        {
            AddFileGetPos();
        }
        else
        {
            for (int32_t i = 0; i < 0x10; i++)
            {
                if (mMidiTrack[i].fno == fno)
                {
                    pCO->track = static_cast<BYTE>(i);
                    RunMidiStop(pCO, 1);
                }
            }
            CloseFileIOP(static_cast<UINT>(mMidiFile[fno].sq_addr));
            if (UseHD(static_cast<UINT>(mMidiFile[fno].hd_addr)) == 1)
            {
                CloseFileIOP(static_cast<UINT>(mMidiFile[fno].hd_addr));
                CloseFileSPU(static_cast<UINT>(mMidiFile[fno].bd_addr));
            }
            pMF->open = pMF->open & 2;
            AddFileGetPos();
        }
        return 0;
    }

    // Plays the MIDI file `fno` on the track of the request: loads the sequence into the track, the instruments to the synthesizer, starts it.
    int32_t RunMidiPlay(int32_t fno, IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunMidiPlay);
        const BYTE track = pCO->track;
        const IopPtr<ST_MIDI_TRACK> pMT = mMidiTrack.at(track);
        ee_put[4] = pCO->id;
        if (pCO->process != 0)
            return -4;
        if ((mMidiFile[fno].open & 1) == 0)
        {
            import::printf(str::T_T_yySound_ERROR_RunMidiPlay_d_MidiFile_Not_Open, static_cast<uint32_t>(fno));
            return -2;
        }
        if (pMT->fno != -1)
            RunMidiStop(pCO, 0);
        minBfCtx[static_cast<size_t>(pCO->track) * 2].buff = IopPtr<void>{static_cast<uint32_t>(mMidiFile[fno].sq_addr)};
        if (import::sceMidi_Load(midiCtx.ptr(), pCO->track) != 0)
        {
            import::printf(str::T_T_yySound_ERROR_sceMidi_Load);
            return -2;
        }
        if (import::sceHSyn_Load(synthCtx.ptr(), pCO->track, static_cast<uint32_t>(mMidiFile[fno].bd_addr), static_cast<uint32_t>(mMidiFile[fno].hd_addr), 0) != 0)
        {
            import::printf(str::T_T_yySound_ERROR_sceHSyn_Load);
            return -2;
        }
        pMT->fno = static_cast<SBYTE>(fno);
        mMidiTrack[track].loop = pCO->loop;
        mMidiTrack[track].step = 0;
        if (MidiPlayNow(pCO->track, pMT) == -1)
            return -2;
        ee_put[1] = ee_put[1] | (1 << (pCO->track & 0x1F));
        m_GetDefTempo = m_GetDefTempo | (1 << (pCO->track & 0x1F));
        mMidiTrack[track].busy = 0;
        pCO->process = 0;
        return 0;
    }

    // Opens the sequence file of a MIDI file (the request carries the entry of the file list the names are in).
    int32_t RunSqOpen(int32_t fno, IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunSqOpen);
        ee_put[4] = pCO->id;
        if (pCO->fileNo != mFileList->GetPos)
            return -4;
        const IopPtr<ST_MIDI_FILE> pMF = mMidiFile.at(fno);
        const uint32_t entry = mFileList.address + static_cast<uint32_t>(offsetof(ST_FILE_LIST, list)) + pCO->fileNo * static_cast<uint32_t>(sizeof(ST_FILE_ONE));
        mMidiFile[fno].sq_addr = static_cast<int32_t>(OpenFileIOP(IopPtr<char>{entry + static_cast<uint32_t>(offsetof(ST_FILE_ONE, SQname))}));
        int32_t ret;
        if (mMidiFile[fno].sq_addr == 0)
        {
            ret = -2;
        }
        else
        {
            pMF->open = pMF->open | 1;
            ret = 0;
        }
        AddFileGetPos();
        if (ret == -2)
            pMF->open = 0;
        return ret;
    }

    // Frees the SPU memory of the joint `pCO->jno` of a wave file (-1: of all its joints).
    int32_t RunClearJointSPU(int32_t fno, IopPtr<ST_COMMAND_ONE> pCO)
    {
        const IopPtr<ST_WAVE_FILE> pWF = mWaveFile.at(fno);
        if ((pWF->open & 1) == 0)
        {
            import::printf(str::T_T_yySound_ERROR_RunClearJointSPU_NotOpen);
            AddFileGetPos();
            return -2;
        }
        if (!mWaveFile[fno].index)
        {
            import::printf(str::T_T_yySound_ERROR_RunClearJointSPU_ModeError);
            AddFileGetPos();
            return -2;
        }
        for (int32_t i = 0; i < 10; i++)
        {
            if (pWF->spu[i].spu_addr != 0 && (pCO->jno == -1 || pWF->spu[i].jno == pCO->jno))
            {
                DeleteSPUMemory(pWF->spu[i].spu_addr);
                pWF->spu[i].spu_addr = 0;
            }
        }
        AddFileGetPos();
        return 0;
    }

    // Stops the wave track `pCO->track`: a note off to the sound effect module; `mode` 0 only lets go of the file, else the track is cleared.
    int32_t RunWaveStop(IopPtr<ST_COMMAND_ONE> pCO, BYTE mode)
    {
        Frame stack(frame::RunWaveStop);
        ee_put[4] = pCO->id;
        if (pCO->track < 0x10)
        {
            const BYTE track = pCO->track;
            const IopPtr<ST_WAVE_TRACK> pWT = mWaveTrack.at(track);
            if (pWT->fno < 0)
                return 0;
            const int32_t id = GetWavePlayID(pCO->track);
            const IopPtr<int32_t> oldstat{stack.local(local::RunWaveStop::oldstat)};
            import::CpuSuspendIntr(oldstat);
            const uint32_t message = static_cast<uint32_t>((mWaveTrack[track].program & 0x7F) << 8 | static_cast<int32_t>(static_cast<int16_t>(mWaveTrack[track].note & 0x7F)) << 16) | 0xA0u;
            import::sceSEIn_PutMsg(seinCtx.ptr(), pCO->track, id, message, 0);
            import::CpuResumeIntr(*oldstat);
            ee_put[2] = ee_put[2] & ~(1 << (pCO->track & 0x1F));
            if (mode == 0)
                pWT->fno = -1;
            else
                ClearWaveTrack(pWT);
            return 0;
        }
        import::printf(str::T_T_yySound_ERROR_track_number_over);
        return -2;
    }

    // Stops every track that plays the wave file `fno`.
    int32_t RunWaveFStop(int32_t fno, IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunWaveFStop);
        ee_put[4] = pCO->id;
        for (int32_t i = 0; i < 0x10; i++)
        {
            if (static_cast<uint32_t>(static_cast<int32_t>(mWaveTrack[i].fno)) == (static_cast<uint32_t>(fno) & 0xFFu))
            {
                const IopPtr<ST_WAVE_STOP> ws{stack.local(local::RunWaveFStop::ws)};
                ws->track = static_cast<BYTE>(i);
                CmdWaveStop(ws);
            }
        }
        return 0;
    }

    // Closes a wave file: stops the tracks that play it, frees its header, and its sound data (or the joints' of a joint file).
    int32_t RunWaveClose(int32_t fno, IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunWaveClose);
        const IopPtr<ST_WAVE_FILE> pWF = mWaveFile.at(fno);
        ee_put[4] = pCO->id;
        if ((pWF->open & 1) == 0)
        {
            AddFileGetPos();
        }
        else
        {
            for (int32_t i = 0; i < 0x10; i++)
            {
                if (mWaveTrack[i].fno == fno)
                {
                    pCO->track = static_cast<BYTE>(i);
                    RunWaveStop(pCO, 1);
                }
            }
            CloseFileIOP(static_cast<UINT>(mWaveFile[fno].hd_addr));
            if (!mWaveFile[fno].index)
                CloseFileSPU(static_cast<UINT>(mWaveFile[fno].bd_addr));
            else
                CloseJointFile(pWF);
            pWF->open = pWF->open & 2;
            AddFileGetPos();
        }
        return 0;
    }

    // Starts the fade of a wave track (WaveVolumeFader runs it).
    int32_t RunWaveFade(IopPtr<ST_COMMAND_ONE> pCO)
    {
        ee_put[4] = pCO->id;
        if (pCO->track < 0x10)
        {
            const BYTE track = pCO->track;
            if (mWaveTrack[track].fno == -1)
                return -2;
            mWaveTrack[track].lgoal = pCO->lgoal;
            mWaveTrack[track].lstep = pCO->lstep;
            mWaveTrack[track].rgoal = pCO->rgoal;
            mWaveTrack[track].rstep = pCO->rstep;
            return 0;
        }
        import::printf(str::T_T_yySound_ERROR_track_number_over);
        return -2;
    }

    // Starts the pitch fade of a wave track (WavePitchFader runs it).
    int32_t RunWavePitchFade(IopPtr<ST_COMMAND_ONE> pCO)
    {
        ee_put[4] = pCO->id;
        if (pCO->track < 0x10)
        {
            const BYTE track = pCO->track;
            mWaveTrack[track].pgoal = pCO->pgoal;
            mWaveTrack[track].pstep = pCO->pstep;
            return 0;
        }
        import::printf(str::T_T_yySound_ERROR_track_number_over);
        return -2;
    }

    // Sets the pitch of a wave track once it plays (-4: not yet, try again): on each SPU2 voice of the track. A voice number of 24 or more is on
    // the second core and the module takes 24 off the number in its list, as the original does, so the list is changed by the call.
    int32_t RunWaveSetPitch(IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunWaveSetPitch);
        if (pCO->process == 0)
        {
            ee_put[4] = pCO->id;
            if (0xF < pCO->track)
            {
                import::printf(str::T_T_yySound_ERROR_track_number_over);
                return -2;
            }
        }
        const BYTE track = pCO->track;
        const int32_t playing = Wave_isInPlay(pCO->track, mWaveTrack.at(track));
        if (playing < 1 && pCO->process < 0xB)
        {
            pCO->process = static_cast<BYTE>(pCO->process + 1);
            return -4;
        }
        int32_t pitch = mWaveTrack[track].defpitch + pCO->pitch;
        if (pitch < 0)
            pitch = 0;
        if (0x3FFF < pitch)
            pitch = 0x3FFF;
        for (int32_t i = 0; i < mCnum; i++)
        {
            const char voice = mVnum[i];
            if (0x17 < voice)
                mVnum[i] = static_cast<char>(mVnum[i] - 0x18);
            const uint32_t core = 0x17 < voice ? 1u : 0u;
            import::sceSdSetParam(core | 0x200u | (static_cast<uint32_t>(static_cast<int32_t>(mVnum[i])) << 1), static_cast<uint32_t>(pitch) & 0xFFFFu);
        }
        pCO->process = 0;
        mWaveTrack[track].pitch = pCO->pitch;
        return 0;
    }

    // Sets the volume of the sound effect track (the whole track, every file): the tracks of the second bank are 16 up.
    int32_t RunWaveSetTVolume(IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunWaveSetTVolume);
        ee_put[4] = pCO->id;
        if (pCO->track < 0x10)
        {
            pCO->track = static_cast<BYTE>(pCO->track + 0x10);
            const IopPtr<int32_t> oldstat{stack.local(local::RunWaveSetTVolume::oldstat)};
            import::CpuSuspendIntr(oldstat);
            const int32_t ret = import::sceHSyn_SetVolume(synthCtx.ptr(), pCO->track, pCO->lvolume);
            import::CpuResumeIntr(*oldstat);
            if (ret == 0)
                return 0;
            return -2;
        }
        import::printf(str::T_T_yySound_ERROR_track_number_over);
        return -2;
    }

    // Sets the volume of a wave track once it plays (-4: not yet, try again): the left and right volume on each SPU2 voice of the track.
    int32_t RunWaveSetVolume(IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunWaveSetVolume);
        if (pCO->process == 0)
        {
            ee_put[4] = pCO->id;
            if (0xF < pCO->track)
            {
                import::printf(str::T_T_yySound_ERROR_track_number_over);
                return -2;
            }
            if (0x3FFF < pCO->lvolume)
            {
                import::printf(str::T_T_yySound_ERROR_volume_level_over);
                return -2;
            }
            if (0x3FFF < pCO->rvolume)
            {
                import::printf(str::T_T_yySound_ERROR_volume_level_over);
                return -2;
            }
        }
        const BYTE track = pCO->track;
        const int32_t playing = Wave_isInPlay(pCO->track, mWaveTrack.at(track));
        if (playing < 1 && pCO->process < 0xB)
        {
            pCO->process = static_cast<BYTE>(pCO->process + 1);
            return -4;
        }
        for (int32_t i = 0; i < mCnum; i++)
        {
            const char voice = mVnum[i];
            if (0x17 < voice)
                mVnum[i] = static_cast<char>(mVnum[i] - 0x18);
            const uint32_t core = 0x17 < voice ? 1u : 0u;
            const uint32_t entry = core | (static_cast<uint32_t>(static_cast<int32_t>(mVnum[i])) << 1);
            import::sceSdSetParam(entry, pCO->lvolume);
            import::sceSdSetParam(entry | 0x100u, pCO->rvolume);
        }
        pCO->process = 0;
        mWaveTrack[track].lvolume = static_cast<SWORD>(pCO->lvolume);
        mWaveTrack[track].rvolume = static_cast<SWORD>(pCO->rvolume);
        return 0;
    }

    // Plays the wave file `fno`'s program `pCO->program` on the track: reads the sample's rate from the bank's header (the offsets chain from the
    // program through its sample set and sample to the VAG entry) and loads the bank for the track.
    int32_t RunWavePlay(int32_t fno, IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunWavePlay);
        const BYTE track = pCO->track;
        const IopPtr<ST_WAVE_TRACK> pWT = mWaveTrack.at(track);
        const IopPtr<ST_WAVE_FILE> pWF = mWaveFile.at(fno);
        if ((pWF->open & 1) == 0)
        {
            import::printf(str::T_T_yySound_ERROR_RunWavePlay_d_WaveFile_Not_Open, static_cast<uint32_t>(fno));
            return -2;
        }
        ee_put[4] = pCO->id;
        if (pWT->fno != -1)
            RunWaveStop(pCO, 0);
        int32_t hd_addr = mWaveFile[fno].hd_addr;
        UINT bd_addr;
        if (!mWaveFile[fno].index)
        {
            bd_addr = static_cast<UINT>(mWaveFile[fno].bd_addr);
        }
        else
        {
            // a joint file: the header of the joint, and the SPU memory its data was copied to
            bd_addr = 0;
            for (int32_t i = 0; i < 10; i++)
            {
                if (pWF->spu[i].jno == pCO->jno)
                {
                    bd_addr = pWF->spu[i].spu_addr;
                    hd_addr = hd_addr + load<int32_t>(static_cast<uint32_t>(pCO->jno * 4 + hd_addr));
                    break;
                }
            }
            if (bd_addr == 0)
            {
                import::printf(str::T_T_yySound_ERROR_RunWavePlay_d_JointNo_NotCopy, static_cast<uint32_t>(static_cast<int32_t>(pCO->jno)));
                return -2;
            }
        }
        const uint32_t hd = static_cast<uint32_t>(hd_addr);
        const uint32_t pProgram = hd + load<uint32_t>(hd + 0x24);
        const uint32_t pSampSet = hd + load<uint32_t>(hd + 0x28);
        const uint32_t pSample = hd + load<uint32_t>(hd + 0x2C);
        const uint32_t pVag = hd + load<uint32_t>(hd + 0x30);
        const uint32_t pU32 = pProgram + 0xC; // the program count, then the offset of each program (-1: none)
        if (load<uint32_t>(pU32) < pCO->program)
        {
            import::printf(str::T_T_yySound_ERROR_program_number_over_max_d, load<uint32_t>(pU32));
            return -2;
        }
        const uint32_t programOffset = load<uint32_t>(pU32 + (static_cast<uint32_t>(pCO->program) + 1) * 4);
        if (programOffset == 0xFFFFFFFFu)
        {
            import::printf(str::T_T_yySound_ERROR_program_number_d_is_NoData, pCO->program);
            return -2;
        }
        const uint32_t program = pProgram + programOffset;
        const uint32_t split = program + load<uint32_t>(program);
        const uint32_t sampSetEntry = pSampSet + load<uint32_t>(pSampSet + 0x10 + static_cast<uint32_t>(load<uint16_t>(split)) * 4);
        const uint32_t sampleEntry = pSample + load<uint32_t>(pSample + 0x10 + static_cast<uint32_t>(load<uint16_t>(sampSetEntry + 4)) * 4);
        const uint32_t vagOffset = load<uint32_t>(pVag + 0x10 + static_cast<uint32_t>(load<uint16_t>(sampleEntry)) * 4);
        if (import::sceHSyn_Load(synthCtx.ptr(), pCO->track + 0x10, bd_addr, hd, 0) != 0)
        {
            import::printf(str::T_T_yySound_ERROR_sceHSyn_Load);
            return -2;
        }
        pWT->fno = static_cast<SBYTE>(fno);
        mWaveTrack[track].program = pCO->program;
        mWaveTrack[track].note = pCO->note;
        mWaveTrack[track].loop = pCO->loop;
        mWaveTrack[track].lstep = 0;
        mWaveTrack[track].rstep = 0;
        mWaveTrack[track].pstep = 0;
        mWaveTrack[track].defpitch = static_cast<WORD>((static_cast<uint32_t>(load<uint16_t>(pVag + vagOffset + 4)) << 12) / 48000);
        if (WavePlayNow(pCO->track, pWT) == -1)
            return -2;
        ee_put[2] = ee_put[2] | (1 << (pCO->track & 0x1F));
        return 0;
    }

    // Starts a voice (a spoken text): stops the two wave tracks it uses (the track of the request, as the original does) and lets the voice list
    // entry run.
    int32_t RunVoicePlay(IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunVoicePlay);
        ee_put[4] = pCO->id;
        for (int32_t i = 0; i < 2; i++)
        {
            if (mWaveTrack[mVoiceList[pCO->program].track[i]].fno != -1)
                RunWaveStop(pCO, 0);
        }
        ee_put[6] = ee_put[6] | (1 << (mVoiceList[pCO->program].track[0] & 0x1F)) | (1 << (mVoiceList[pCO->program].track[1] & 0x1F));
        mVoiceList[pCO->program].pos = 0;
        return 0;
    }

    // The request run: takes it off the list `pCL` (counting it for the EE, which gets the data sent), and when the EE waits for it by m_checkID,
    // tells the EE how it went in ee_put[5] (the id, or minus the id for a failure).
    void AddRequestGetPos(IopPtr<ST_COMMAND_LIST> pCL, IopPtr<ST_COMMAND_ONE> pCO, int32_t ret, BYTE fno)
    {
        Frame stack(frame::AddRequestGetPos);
        if (pCL->GetPos == pCL->PutPos)
        {
            import::printf(str::IOPERR_FNO_d_PUT_GET_03d_CMD_04X, fno, pCL->GetPos, pCO->command);
        }
        else if (ee_put[3] == 0)
        {
            import::printf(str::IOPERR_FNO_d_PUT_03d_GET_03d_CMD_04X, fno, pCL->PutPos, pCL->GetPos, pCO->command);
        }
        else
        {
            pCL->GetPos = static_cast<BYTE>(pCL->GetPos + 1);
            if (0x3F < pCL->GetPos)
                pCL->GetPos = 0;
            ee_put[3] = ee_put[3] - 1;
            m_SendSw = 1;
            if (m_checkID != 0 && m_checkID == pCO->id)
            {
                if (ret == 0)
                    ee_put[5] = m_checkID;
                else
                    ee_put[5] = -static_cast<int32_t>(static_cast<uint32_t>(m_checkID));
                m_checkID = 0;
            }
        }
    }

    // ---- the loops over the tracks (every tick, from the timer thread)

    // A MIDI track whose song has ended: plays it again while the loop count lasts (0: for ever), else stops it.
    void MidiLoop(int32_t track, IopPtr<ST_MIDI_TRACK> pMT)
    {
        Frame stack(frame::MidiLoop);
        if (pMT->busy == 0)
        {
            const BYTE trackNo = static_cast<BYTE>(track);
            if (pMT->loop == 0 || (pMT->loop = static_cast<WORD>(pMT->loop - 1), pMT->loop != 0))
            {
                const IopPtr<ST_MIDI_PLAY> mp{stack.local(local::MidiLoop::mp)};
                mp->track = trackNo;
                mp->fno = static_cast<BYTE>(pMT->fno);
                mp->loop = static_cast<BYTE>(pMT->loop);
                CmdMidiPlay(mp);
            }
            else
            {
                MidiStopNow(trackNo, pMT);
            }
        }
    }

    // A wave track whose sound has ended: plays it again while the loop count lasts (0: for ever), else clears the track.
    void WaveLoop(int32_t track, IopPtr<ST_WAVE_TRACK> pWT)
    {
        Frame stack(frame::WaveLoop);
        if (pWT->busy == 0)
        {
            if (pWT->loop == 0 || (pWT->loop = static_cast<WORD>(pWT->loop - 1), pWT->loop != 0))
            {
                const IopPtr<ST_WAVE_PLAY> wp{stack.local(local::WaveLoop::wp)};
                wp->id = 0;
                wp->track = static_cast<BYTE>(track);
                wp->fno = static_cast<BYTE>(pWT->fno);
                wp->loop = static_cast<BYTE>(pWT->loop);
                wp->program = pWT->program;
                wp->note = static_cast<BYTE>(pWT->note);
                CmdWavePlay(wp);
            }
            else
            {
                ClearWaveTrack(pWT);
            }
        }
    }

    // Looks after the 16 MIDI tracks (the last first): restarts the ended songs, remembers the default tempo of a song that has started, runs the
    // fades, and builds the status the EE gets (one bit per track in use). 1 when the status changed.
    BYTE MidiIdle()
    {
        Frame stack(frame::MidiIdle);
        const int32_t oldMidiStatus = ee_put[1];
        ee_put[1] = 0;
        for (int32_t i = 0xF; -1 < i; i--)
        {
            const IopPtr<ST_MIDI_TRACK> pMT = mMidiTrack.at(i);
            ee_put[1] = static_cast<int32_t>(static_cast<uint32_t>(ee_put[1]) << 1);
            if (pMT->fno != -1)
            {
                if (Midi_isInPlay(static_cast<BYTE>(i), pMT) < 1)
                {
                    MidiLoop(i, pMT);
                }
                else
                {
                    if ((static_cast<uint32_t>(m_GetDefTempo) >> (i & 0x1F) & 1u) != 0)
                    {
                        mMidiTrack[i].deftempo = static_cast<BYTE>(import::sceMidi_MidiGetAbsoluteTempo(midiCtx.ptr(), i));
                        m_GetDefTempo = m_GetDefTempo & ~(1 << (i & 0x1F));
                    }
                    MidiFader(i, pMT);
                }
                if (pMT->fno != -1)
                    ee_put[1] = ee_put[1] | 1;
            }
        }
        return oldMidiStatus != ee_put[1] ? 1 : 0;
    }

    // The same for the 16 wave tracks (the bit for a track is set when it holds a file, played or not).
    BYTE WaveIdle()
    {
        Frame stack(frame::WaveIdle);
        const int32_t oldWaveStatus = ee_put[2];
        ee_put[2] = 0;
        for (int32_t i = 0xF; -1 < i; i--)
        {
            const IopPtr<ST_WAVE_TRACK> pWT = mWaveTrack.at(i);
            ee_put[2] = static_cast<int32_t>(static_cast<uint32_t>(ee_put[2]) << 1);
            if (pWT->fno != -1)
            {
                if (Wave_isInPlay(static_cast<BYTE>(i), pWT) < 1)
                {
                    WaveLoop(i, pWT);
                }
                else
                {
                    WaveVolumeFader(static_cast<BYTE>(i), pWT);
                    WavePitchFader(static_cast<BYTE>(i), pWT);
                }
            }
            if (pWT->fno != -1)
                ee_put[2] = ee_put[2] | 1;
        }
        return oldWaveStatus != ee_put[2] ? 1 : 0;
    }

    // Steps the voices being spoken (the voice list): counts the entry's wait down; when it is over plays the next mora (a wave of the voice file,
    // its volume and, for the marks that bend the pitch, a pitch fade), or, at the end mark, frees the entry. 1 when an entry ended.
    BYTE VoiceIdle()
    {
        Frame stack(frame::VoiceIdle);
        BYTE change = 0;
        for (int32_t i = 0; i < 8; i++)
        {
            const IopPtr<ST_VOICE_LIST> pVL = mVoiceList.at(i);
            if (pVL->pos == -1)
                continue;
            if (mVoiceList[i].counter != 0)
            {
                mVoiceList[i].counter = static_cast<BYTE>(mVoiceList[i].counter + 0xFF);
                if (mVoiceList[i].counter == 0)
                    pVL->pos = static_cast<SWORD>(pVL->pos + 1);
            }
            if (mVoiceList[i].counter != 0)
                continue;
            const WORD code = pVL->voice[pVL->pos];
            const BYTE voice = static_cast<BYTE>(code);
            if (code == 0xFFFF)
            {
                ee_put[6] = ee_put[6] & ~(1 << (mVoiceList[i].track[0] & 0x1F)) & ~(1 << (mVoiceList[i].track[1] & 0x1F));
                change = 1;
                pVL->pos = -1;
                continue;
            }
            const BYTE track = pVL->track[static_cast<int16_t>(pVL->pos & 1)];
            BYTE tone = pVL->tone[pVL->pos];
            const BYTE note = mVoiceList[i].note;
            const BYTE fileIndex = static_cast<BYTE>(static_cast<BYTE>(code >> 8) & 0xF);
            WORD counter;
            if (static_cast<WORD>(note * 3) < mVoicePitch)
                counter = static_cast<WORD>(mVoicePitch + note * -3);
            else
                counter = 0;
            if (code == 0x1000)
                counter = static_cast<WORD>(counter >> 1);
            mVoiceList[i].counter = static_cast<BYTE>(counter);
            if (code != 0x2000 && code != 0x1000)
            {
                int32_t type = 0;
                if (tone < 0x61)
                {
                    if (0x40 < tone)
                    {
                        tone = static_cast<BYTE>(tone - 0x11);
                        type = 1;
                    }
                }
                else
                {
                    tone = static_cast<BYTE>(tone - 0x31);
                    type = 2;
                }
                const IopPtr<ST_WAVE_PLAY> wp{stack.local(local::VoiceIdle::wp)};
                wp->note = static_cast<BYTE>((note - 0x35) + tone);
                wp->id = 0;
                wp->loop = 1;
                wp->fno = mVoiceFile[fileIndex];
                wp->program = voice;
                wp->track = track;
                CmdWavePlay(wp);
                WORD vol = pVL->volume[pVL->pos];
                if (vol == 0)
                    vol = 0x1FFF;
                const IopPtr<ST_WAVE_VOL> wv{stack.local(local::VoiceIdle::wv)};
                wv->id = 0;
                wv->lvolume = vol;
                wv->rvolume = vol;
                wv->track = track;
                CmdWaveSetVolume(wv, static_cast<SBYTE>(mVoiceFile[fileIndex]));
                if (type != 0)
                {
                    const IopPtr<ST_WAVE_PITCH_FADE> wf{stack.local(local::VoiceIdle::wf)};
                    wf->id = 0;
                    wf->pgoal = type == 1 ? 1000 : -1000;
                    wf->pstep = 5;
                    wf->track = track;
                    CmdWavePitchFade(wf, static_cast<SBYTE>(mVoiceFile[fileIndex]));
                }
            }
        }
        return change;
    }

    // ---- start up and shut down

    // The tables to their start state: no files open, no tracks in use, the command and file lists empty, the SPU memory list with its two fixed
    // blocks (the driver's own area at the start and the end of the memory).
    void BuffInit()
    {
        for (int32_t i = 0; i < 8; i++)
            mMidiFile[i].open = 0;
        for (int32_t i = 0; i < 8; i++)
            mWaveFile[i].open = 0;
        for (int32_t i = 0; i < 0x10; i++)
        {
            mMidiTrack[i].fno = -1;
            mMidiTrack[i].volume = -1;
            mMidiTrack[i].tempo = 0;
            mMidiTrack[i].busy = 0;
        }
        for (int32_t i = 0; i < 0x10; i++)
        {
            mWaveTrack[i].fno = -1;
            mWaveTrack[i].lvolume = -1;
            mWaveTrack[i].rvolume = -1;
            mWaveTrack[i].pitch = 0;
            mWaveTrack[i].busy = 0;
        }
        for (int32_t i = 0; i < 0x10; i++)
        {
            ee_ret[i] = 0;
            ee_put[i] = 0;
        }
        for (int32_t i = 0; i < 0x11; i++)
        {
            mCommandList[i].GetPos = 0;
            mCommandList[i].PutPos = 0;
        }
        mFileList->GetPos = 0;
        mFileList->PutPos = 0;
        for (int32_t i = 0; i < 8; i++)
            mVoiceList[i].pos = -1;
        for (int32_t i = 0; i < 0x20; i++)
            mTrackFileNo[i] = 0xFF;
        for (int32_t i = 0; i < 0x14; i++)
            mSpuList[i].used = 0;
        mSpuList[0].addr = 0x5010;
        mSpuList[1].addr = 0x1C0000;
        mSpuList[1].used = 2;
        mEEaddress = 0;
        m_SendSw = 0;
        mBusy = 0;
        m_checkID = 0;
        m_GetDefTempo = 0;
        mSoundEnable = 1;
    }

    // Ends the timer, the load timer and the three threads; -1 if one of them fails.
    int32_t EndAllThread()
    {
        Frame stack(frame::EndAllThread);
        if (clear_timer() < 0)
        {
            import::printf(str::T_T_yySound_ERROR_clear_timer_something_wrong);
            return -1;
        }
        if (clear_load_timer() < 0)
        {
            import::printf(str::T_T_yySound_ERROR_clear_load_timer_something_wrong);
            return -1;
        }
        if (import::DeleteThread(timerThreadID) != 0)
            return -1;
        if (import::DeleteThread(eeThreadID) != 0)
            return -1;
        if (import::DeleteThread(LoadThreadID) != 0)
            return -1;
        return 0;
    }

    // The EE ends the driver: closes every file, ends the threads and gives back the memory of the SPU transfer packet.
    int32_t CmdTerm()
    {
        Frame stack(frame::CmdTerm);
        const IopPtr<ST_MIDI_CLOSE> mc{stack.local(local::CmdTerm::mc)};
        const IopPtr<ST_WAVE_CLOSE> wc{stack.local(local::CmdTerm::wc)};
        mc->id = 0;
        wc->id = 0;
        for (int32_t i = 0; i < 8; i++)
        {
            mc->fno = static_cast<BYTE>(i);
            CmdMidiClose(mc);
        }
        for (int32_t i = 0; i < 8; i++)
        {
            wc->fno = static_cast<BYTE>(i);
            CmdWaveClose(wc);
        }
        EndAllThread();
        const IopPtr<int32_t> oldstat{stack.local(local::CmdTerm::oldstat)};
        import::CpuSuspendIntr(oldstat);
        import::FreeSysMemory(static_cast<uint32_t>(mSpuTransPacketAddr));
        import::CpuResumeIntr(*oldstat);
        return 0;
    }

    // The RPC function the EE calls (command, data and size): answers with the address of ee_ret, whose first word is 1 once the command is
    // taken and 2 when the driver is busy with another; the result of the command is ee_put[0].
    uint32_t soundFunc(UINT command, IopPtr<void> data, int32_t size)
    {
        (void)size;
        Frame stack(frame::soundFunc);
        int32_t ret = -1;
        if ((mBusy & 1) != 0)
        {
            import::printf(str::T_T_yySound_ERROR_soundfunc_BUSY);
            ee_put[0] = -3;
            ee_put[4] = -1;
            ee_ret[0] = 2;
            return ee_ret.address;
        }
        mBusy = mBusy | 1;
        switch (command)
        {
        case 0x1024: ret = CmdMidiGetVolume(load<BYTE>(data.address)); break;
        case 1: ret = CmdInit(IopPtr<UINT>{data.address}); break;
        case 0: ret = CmdTerm(); break;
        case 2: mSoundEnable = load<BYTE>(data.address); break;
        case 0x2100: ret = CmdVoiceFile(IopPtr<BYTE>{data.address}); break;
        case 0x2024: ret = CmdWaveGetVolume(load<BYTE>(data.address)); break;
        case 0x3000: ret = CmdSetCheckID(load<WORD>(data.address)); break;
        case 0x3001: mLoadPause = load<BYTE>(data.address); break;
        default: ret = AddRequest(command, data); break;
        }
        ee_put[0] = ret;
        ee_ret[0] = 1;
        mBusy = mBusy & 6;
        return ee_ret.address;
    }

    // ---- threads

    int32_t make_Timer_thread()
    {
        Frame stack(frame::make_Timer_thread);
        const IopPtr<ThreadParam> param{stack.local(local::make_Timer_thread::param)};
        param->attr = 0x2000000;
        param->entry = IopPtr<void>{code(offset::atick)};
        param->initPriority = 0x1D;
        param->stackSize = 0x1000;
        param->option = 0;
        return import::CreateThread(param);
    }

    int32_t make_EE_thread()
    {
        Frame stack(frame::make_EE_thread);
        const IopPtr<ThreadParam> param{stack.local(local::make_EE_thread::param)};
        param->attr = 0x2000000;
        param->entry = IopPtr<void>{code(offset::sce_ee_loop)};
        param->initPriority = 0x1E;
        param->stackSize = 0x1000;
        param->option = 0;
        return import::CreateThread(param);
    }

    int32_t make_Load_thread()
    {
        Frame stack(frame::make_Load_thread);
        const IopPtr<ThreadParam> param{stack.local(local::make_Load_thread::param)};
        param->attr = 0x2000000;
        param->entry = IopPtr<void>{code(offset::Load_atick)};
        param->initPriority = 0x1F;
        param->stackSize = 0x1000;
        param->option = 0;
        return import::CreateThread(param);
    }

    // Creates and starts the timer thread, then the hard timer that wakes it.
    int32_t StartTimerThread()
    {
        Frame stack(frame::StartTimerThread);
        timerThreadID = make_Timer_thread();
        if (timerThreadID < 1)
        {
            import::printf(str::T_T_yySound_ERROR_Timer_thread_Create);
            return -1;
        }
        import::StartThread(timerThreadID, 0);
        m_timer->thread_id = timerThreadID;
        if (set_timer() < 0)
        {
            import::printf(str::T_T_yySound_ERROR_set_timer_something_wrong);
            return -1;
        }
        return 0;
    }

    int32_t StartEEThread()
    {
        Frame stack(frame::StartEEThread);
        eeThreadID = make_EE_thread();
        if (eeThreadID < 1)
        {
            import::printf(str::T_T_yySound_ERROR_EE_thread_Create);
            return -1;
        }
        import::StartThread(eeThreadID, 0);
        return 0;
    }

    // Creates and starts the load thread, then the hard timer that wakes it.
    int32_t StartLoadThread()
    {
        Frame stack(frame::StartLoadThread);
        LoadThreadID = make_Load_thread();
        if (LoadThreadID < 1)
        {
            import::printf(str::T_T_yySound_ERROR_Load_thread_Create);
            return -1;
        }
        import::StartThread(LoadThreadID, 0);
        m_load->thread_id = LoadThreadID;
        if (set_load_timer() < 0)
        {
            import::printf(str::T_T_yySound_ERROR_set_Load_timer_something_wrong);
            return -1;
        }
        return 0;
    }

    // The tables, the SPU2 and the SPU transfer packet's memory, then the three modules' contexts (modhsyn, modmidi, modsein).
    int32_t Init()
    {
        Frame stack(frame::Init);
        BuffInit();
        import::sceSdInit(0);
        set_spu2();
        const IopPtr<int32_t> oldstat{stack.local(local::Init::oldstat)};
        import::CpuSuspendIntr(oldstat);
        mSpuTransPacketAddr = static_cast<int32_t>(import::AllocSysMemory(0, 0x8000, 0));
        import::CpuResumeIntr(*oldstat);
        store<int32_t>(stack.local(local::Init::freesize), import::QueryTotalFreeMemSize());
        set_module_context();
        set_hsyn(vstat.ptr());
        set_midi();
        set_sein();
        return 0;
    }

    // The module's entry: the driver starts its three threads.
    int32_t start(int32_t argc, IopPtr<IopPtr<char>> argv)
    {
        (void)argc;
        (void)argv;
        Frame stack(frame::start);
        import::printf(str::Indi_Software_yysound_irx_2002_05_24);
        Init();
        StartEEThread();
        StartTimerThread();
        StartLoadThread();
        return 0;
    }

    // ---- the requests that load files: run by the load thread, where the reads and the transfers to the SPU can put the thread to sleep

    // Opens a MIDI file in steps, one per call (the request keeps its step in `process`): the sequence file, the instrument header, then the
    // sound data into SPU memory, which OpenFileSPU does a block at a time. -4: not done yet, call again.
    int32_t RunMidiOpen(int32_t fno, IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunMidiOpen);
        int32_t ret = -4;
        ee_put[4] = pCO->id;
        if (pCO->fileNo != mFileList->GetPos)
            return -4;
        const IopPtr<ST_MIDI_FILE> pMF = mMidiFile.at(fno);
        const BYTE fileNo = pCO->fileNo;
        const uint32_t entry = mFileList.address + static_cast<uint32_t>(offsetof(ST_FILE_LIST, list)) + fileNo * static_cast<uint32_t>(sizeof(ST_FILE_ONE));
        const BYTE process = pCO->process;
        if (process == 1)
        {
            if (mLoadPause != 1)
            {
                mMidiFile[fno].hd_addr = static_cast<int32_t>(OpenFileIOP(IopPtr<char>{entry + static_cast<uint32_t>(offsetof(ST_FILE_ONE, HDname))}));
                if (mMidiFile[fno].hd_addr == 0)
                {
                    CloseFileIOP(static_cast<UINT>(mMidiFile[fno].sq_addr));
                    ret = -2;
                }
                else
                {
                    pCO->process = static_cast<BYTE>(pCO->process + 1);
                }
            }
        }
        else if (process < 2)
        {
            if (process == 0 && mLoadPause != 1)
            {
                mMidiFile[fno].sq_addr = static_cast<int32_t>(OpenFileIOP(IopPtr<char>{entry + static_cast<uint32_t>(offsetof(ST_FILE_ONE, SQname))}));
                if (mMidiFile[fno].sq_addr == 0)
                    ret = -2;
                else
                    pCO->process = static_cast<BYTE>(pCO->process + 1);
            }
        }
        else if (process == 2)
        {
            const int32_t spuadr = OpenFileSPU(IopPtr<char>{entry + static_cast<uint32_t>(offsetof(ST_FILE_ONE, BDname))});
            if (spuadr == -1)
            {
                CloseFileIOP(static_cast<UINT>(mMidiFile[fno].hd_addr));
                CloseFileIOP(static_cast<UINT>(mMidiFile[fno].sq_addr));
                ret = -2;
            }
            else if (spuadr != 0)
            {
                mMidiFile[fno].bd_addr = spuadr;
                pMF->open = pMF->open | 1;
                ret = 0;
            }
        }
        if (ret != -4)
        {
            AddFileGetPos();
            pCO->process = 0;
        }
        if (ret == -2)
            pMF->open = 0;
        return ret;
    }

    // Opens a wave file in steps (the header, then the sound data into SPU memory or, for a joint file, the index): as RunMidiOpen.
    int32_t RunWaveOpen(int32_t fno, IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunWaveOpen);
        int32_t ret = -4;
        ee_put[4] = pCO->id;
        if (pCO->fileNo != mFileList->GetPos)
            return -4;
        const IopPtr<ST_WAVE_FILE> pWF = mWaveFile.at(fno);
        const BYTE fileNo = pCO->fileNo;
        const uint32_t entry = mFileList.address + static_cast<uint32_t>(offsetof(ST_FILE_LIST, list)) + fileNo * static_cast<uint32_t>(sizeof(ST_FILE_ONE));
        const BYTE process = pCO->process;
        if (process == 1)
        {
            const int32_t spuadr = OpenFileSPU(IopPtr<char>{entry + static_cast<uint32_t>(offsetof(ST_FILE_ONE, BDname))});
            if (spuadr == -1)
            {
                CloseFileIOP(static_cast<UINT>(mWaveFile[fno].hd_addr));
                ret = -2;
            }
            else if (spuadr != 0)
            {
                mWaveFile[fno].bd_addr = spuadr;
                mWaveFile[fno].index = IopPtr<UINT>{0};
                pWF->open = pWF->open | 1;
                ret = 0;
            }
        }
        else if (process < 2)
        {
            if (process == 0)
            {
                mWaveFile[fno].hd_addr = static_cast<int32_t>(OpenFileIOP(IopPtr<char>{entry + static_cast<uint32_t>(offsetof(ST_FILE_ONE, HDname))}));
                if (mWaveFile[fno].hd_addr == 0)
                    ret = -2;
                else if (mFileList->list[fileNo].type == 1)
                    pCO->process = 2;
                else
                    pCO->process = 1;
            }
        }
        else if (process == 2)
        {
            if (OpenJointFile(IopPtr<char>{entry + static_cast<uint32_t>(offsetof(ST_FILE_ONE, BDname))}, pWF) == -1)
            {
                CloseFileIOP(static_cast<UINT>(mWaveFile[fno].hd_addr));
                ret = -2;
            }
            else
            {
                pWF->open = pWF->open | 1;
                ret = 0;
            }
        }
        if (ret != -4)
        {
            AddFileGetPos();
            pCO->process = 0;
        }
        if (ret == -2)
            pWF->open = 0;
        return ret;
    }

    // Copies the sound data of the joint `pCO->jno` of a joint file into SPU memory (a block of 32 KB at a time through the transfer packet).
    int32_t RunCopyJointSPU(int32_t fno, IopPtr<ST_COMMAND_ONE> pCO)
    {
        Frame stack(frame::RunCopyJointSPU);
        const IopPtr<ST_WAVE_FILE> pWF = mWaveFile.at(fno);
        if ((pWF->open & 1) == 0)
        {
            import::printf(str::T_T_yySound_ERROR_RunCopyJointSPU_NotOpen);
            AddFileGetPos();
            return -2;
        }
        const IopPtr<UINT> index = mWaveFile[fno].index;
        if (!index)
        {
            import::printf(str::T_T_yySound_ERROR_RunCopyJointSPU_ModeError);
            AddFileGetPos();
            return -2;
        }
        if ((index[0] >> 2) - 2 < static_cast<uint32_t>(static_cast<int32_t>(pCO->jno)))
        {
            import::printf(str::T_T_yySound_ERROR_RunCopyJointSPU_JointNumberOver_d, static_cast<uint32_t>(static_cast<int32_t>(pCO->jno)));
            AddFileGetPos();
            return -2;
        }
        const UINT sadr = index[pCO->jno];
        for (int32_t i = 0; i < 10; i++)
        {
            if (pWF->spu[i].spu_addr != 0 && pWF->spu[i].jno == pCO->jno)
            {
                AddFileGetPos();
                return 0;
            }
        }
        int32_t lno = -1;
        for (int32_t i = 0; i < 10; i++)
        {
            if (pWF->spu[i].spu_addr == 0)
            {
                lno = i;
                break;
            }
        }
        if (lno == -1)
        {
            import::printf(str::T_T_yySound_ERROR_RunCopyJointSPU_KeepListFull);
            AddFileGetPos();
            return -2;
        }
        UINT size = (index[pCO->jno + 1] - sadr + 0x3F) & 0xFFFFFFC0u;
        const UINT mem = NewSPUMemory(size);
        if (mem == 0)
        {
            AddFileGetPos();
            return -2;
        }
        import::lseek(mWaveFile[fno].bd_addr, static_cast<int32_t>(sadr), 0);
        UINT cmem = mem;
        UINT rsize;
        while (size != 0 && (rsize = static_cast<UINT>(import::read(mWaveFile[fno].bd_addr, static_cast<uint32_t>(mSpuTransPacketAddr), 0x8000)), 0 < static_cast<int32_t>(rsize)))
        {
            if (size < rsize)
            {
                int32_t blocks = static_cast<int32_t>(size) + 0x3F;
                if (blocks < 0)
                    blocks = static_cast<int32_t>(size) + 0x7E;
                rsize = static_cast<UINT>((blocks >> 6) << 6);
            }
            if (import::sceHSyn_VoiceTrans(0, static_cast<uint32_t>(mSpuTransPacketAddr), cmem, rsize) != 0)
            {
                import::printf(str::T_T_yySound_ERROR_CopyJointSPU_sceHSyn_VoisTrans);
                DeleteSPUMemory(mem);
                AddFileGetPos();
                return -2;
            }
            cmem = cmem + rsize;
            if (size < rsize)
                size = 0;
            else
                size = size - rsize;
        }
        pWF->spu[lno].spu_addr = mem;
        pWF->spu[lno].jno = pCO->jno;
        AddFileGetPos();
        return 0;
    }

    // Runs the oldest request of each command list (the 16 files', then the last): the ones for the play and track functions in `mode` 0 (timer
    // thread), the ones for the files in `mode` 1 (load thread). A request that is not done (-4) stays where it is; one that is done is taken off the
    // list. 1 when one was.
    BYTE RunCommand(BYTE mode)
    {
        Frame stack(frame::RunCommand);
        BYTE reqCount = 0;
        for (int32_t i = 0; i <= 0x10; i++)
        {
            const IopPtr<ST_COMMAND_LIST> pCL = mCommandList.at(i);
            const IopPtr<ST_COMMAND_ONE> pCO{pCL.address + static_cast<uint32_t>(offsetof(ST_COMMAND_LIST, list)) + pCL->GetPos * static_cast<uint32_t>(sizeof(ST_COMMAND_ONE))};
            if (pCL->GetPos == mCommandList[i].PutPos)
                continue;
            int32_t ret = -4;
            if (mode == 0)
            {
                switch (pCO->command)
                {
                case 0x2010: ret = RunWavePlay(i, pCO); break;
                case 0x1020: ret = RunMidiSetVolume(pCO); break;
                case 0x1011: ret = RunMidiStop(pCO, 1); break;
                case 0x1010: ret = RunMidiPlay(i - 8, pCO); break;
                case 0x1012: ret = RunMidiFStop(i - 8, pCO); break;
                case 0x1025: ret = RunMidiSetTVolume(pCO); break;
                case 0x1021: ret = RunMidiFade(pCO); break;
                case 0x1026: ret = RunMidiSetTempo(pCO); break;
                case 0x2021: ret = RunWaveFade(pCO); break;
                case 0x2012: ret = RunWaveFStop(i, pCO); break;
                case 0x2011: ret = RunWaveStop(pCO, 1); break;
                case 0x2020: ret = RunWaveSetVolume(pCO); break;
                case 0x2024: ret = RunWavePitchFade(pCO); break;
                case 0x2022: ret = RunWaveSetPitch(pCO); break;
                case 0x2025: ret = RunWaveSetTVolume(pCO); break;
                case 0x2101: ret = RunVoicePlay(pCO); break;
                default: break;
                }
            }
            else
            {
                switch (pCO->command)
                {
                case 0x2002: ret = RunSqOpen(i - 8, pCO); break;
                case 0x1003: ret = RunMidiClose(i - 8, pCO); break;
                case 0x1001: ret = RunMidiOpen(i - 8, pCO); break;
                case 0x2001: ret = RunWaveOpen(i, pCO); break;
                case 0x2200: ret = RunCopyJointSPU(i, pCO); break;
                case 0x2003: ret = RunWaveClose(i, pCO); break;
                case 0x2201: ret = RunClearJointSPU(i, pCO); break;
                default: break;
                }
            }
            if (ret != -4)
            {
                AddRequestGetPos(pCL, pCO, ret, static_cast<BYTE>(i));
                reqCount = 1;
            }
        }
        return reqCount;
    }

    // ---- the thread bodies

    namespace offset
    {
        // The code after the `jal SleepThread` (and its delay slot) of the two timer threads: where they go on when they are woken.
        inline constexpr uint32_t atick_resume = 0x39F0;
        inline constexpr uint32_t Load_atick_resume = 0x3B74;
    }

    // Ends a thread body: the thread sleeps (a jump to the SleepThread stub, as the original's jal) and goes on at `resumeOffset` when it is woken.
    inline void sleepUntilWoken(uint32_t resumeOffset)
    {
        setReg(31, kBase + resumeOffset);
        jump(kBase + stub::SleepThread);
    }

    // The timer thread's entry: it sleeps until the hard timer's handler wakes it (every tick).
    void atick()
    {
        Frame stack(frame::atick);
        stack.keep();
        store<uint32_t>(stack.address(frame::atick - 4), reg(31));
        store<uint32_t>(stack.address(frame::atick - 8), reg(30));
        setReg(30, reg(29));
        mTCount = 0;
        sleepUntilWoken(offset::atick_resume);
    }

    // One tick of the sound: the idle loops of the voices, MIDI and wave tracks, the requests of the timer thread, the data for the EE, then the
    // modules' own ticks. Then it sleeps again.
    void atick_resume()
    {
        const Frame stack(Frame::Resumed{}, frame::atick);
        if (mSoundEnable != 0 && mBusy == 0)
        {
            mBusy = 2;
            // (each result is taken before m_SendSw is read: the functions set it too)
            BYTE changed = VoiceIdle();
            m_SendSw = m_SendSw | changed;
            changed = MidiIdle();
            m_SendSw = m_SendSw | changed;
            changed = WaveIdle();
            m_SendSw = m_SendSw | changed;
            changed = RunCommand(0);
            m_SendSw = m_SendSw | changed;
            SendDataForEE();
            mBusy = mBusy & 5;
        }
        import::sceSEIn_ATick(seinCtx.ptr());
        const IopPtr<int32_t> oldstat{stack.local(local::atick::oldstat)};
        import::CpuSuspendIntr(oldstat);
        import::sceMidi_ATick(midiCtx.ptr());
        import::sceHSyn_ATick(synthCtx.ptr());
        import::CpuResumeIntr(*oldstat);
        sleepUntilWoken(offset::atick_resume);
    }

    // The load thread's entry: it sleeps until the hard timer's handler wakes it.
    void Load_atick()
    {
        Frame stack(frame::Load_atick);
        stack.keep();
        store<uint32_t>(stack.address(frame::Load_atick - 4), reg(31));
        store<uint32_t>(stack.address(frame::Load_atick - 8), reg(30));
        setReg(30, reg(29));
        sleepUntilWoken(offset::Load_atick_resume);
    }

    // One wake of the load thread: when the timer thread is not at work, runs the file requests. Then it sleeps again.
    void Load_atick_resume()
    {
        const Frame stack(Frame::Resumed{}, frame::Load_atick);
        if (mBusy == 0)
        {
            const BYTE changed = RunCommand(1);
            m_SendSw = m_SendSw | changed;
        }
        sleepUntilWoken(offset::Load_atick_resume);
    }

    // The EE thread: registers the sound function as the RPC server and serves for ever (the thread then sleeps in sceSifRpcLoop).
    void sce_ee_loop()
    {
        Frame stack(frame::sce_ee_loop);
        stack.keep();
        store<uint32_t>(stack.address(frame::sce_ee_loop - 4), reg(31));
        store<uint32_t>(stack.address(frame::sce_ee_loop - 8), reg(30));
        setReg(30, reg(29));
        const IopPtr<_sif_queue_data> qd{stack.local(local::sce_ee_loop::qd)};
        const IopPtr<_sif_serve_data> sd{stack.local(local::sce_ee_loop::sd)};
        import::sceSifInitRpc(0);
        const int32_t thread = import::GetThreadId();
        import::sceSifSetRpcQueue(qd, thread);
        import::sceSifRegisterRpc(sd, 0x12346, code(offset::soundFunc), gRpcArg.address, 0, 0, qd);
        setReg(4, qd.address);
        setReg(31, kBase + 0xC968); // the original's return address after the call; the loop does not return
        jump(kBase + stub::sceSifRpcLoop);
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
IOP_FN(yysound, CmdMidiSetTempo)
IOP_FN(yysound, CmdMidiSetVolume)
IOP_FN(yysound, CmdWaveSetPitch)
IOP_FN(yysound, CmdWaveSetVolume)
IOP_FN(yysound, CmdWavePitchFade)
IOP_FN(yysound, CmdMidiStop)
IOP_FN(yysound, CmdWaveStop)
IOP_FN(yysound, CmdMidiFade)
IOP_FN(yysound, CmdWaveFade)
IOP_FN(yysound, CmdMidiSetTrackVolume)
IOP_FN(yysound, CmdWaveSetTrackVolume)
IOP_FN(yysound, CmdVoiceFile)
IOP_FN(yysound, CmdMidiGetVolume)
IOP_FN(yysound, CmdWaveGetVolume)
IOP_FN(yysound, CmdSetCheckID)
IOP_FN(yysound, CmdInit)
IOP_FN(yysound, ReportFileRequestList)
IOP_FN(yysound, CmdMidiClose)
IOP_FN(yysound, CmdWaveClose)
IOP_FN(yysound, CmdSqOpen)
IOP_FN(yysound, CmdWaveOpen)
IOP_FN(yysound, CmdCopyJointSPU)
IOP_FN(yysound, CmdClearJointSPU)
IOP_FN(yysound, CmdMidiPlay)
IOP_FN(yysound, CmdWavePlay)
IOP_FN(yysound, CmdVoicePlay)
IOP_FN(yysound, AddRequest)
IOP_FN(yysound, AddFileGetPos)
IOP_FN(yysound, UseHD)
IOP_FN(yysound, RunMidiStop)
IOP_FN(yysound, RunMidiFStop)
IOP_FN(yysound, RunMidiSetTempo)
IOP_FN(yysound, RunMidiSetVolume)
IOP_FN(yysound, RunMidiSetTVolume)
IOP_FN(yysound, RunMidiFade)
IOP_FN(yysound, RunMidiClose)
IOP_FN(yysound, RunMidiPlay)
IOP_FN(yysound, RunSqOpen)
IOP_FN(yysound, RunClearJointSPU)
IOP_FN(yysound, RunWaveStop)
IOP_FN(yysound, RunWaveFStop)
IOP_FN(yysound, RunWaveClose)
IOP_FN(yysound, RunWaveFade)
IOP_FN(yysound, RunWavePitchFade)
IOP_FN(yysound, RunWaveSetPitch)
IOP_FN(yysound, RunWaveSetTVolume)
IOP_FN(yysound, RunWaveSetVolume)
IOP_FN(yysound, RunWavePlay)
IOP_FN(yysound, RunVoicePlay)
IOP_FN(yysound, AddRequestGetPos)
IOP_FN(yysound, MidiLoop)
IOP_FN(yysound, WaveLoop)
IOP_FN(yysound, MidiIdle)
IOP_FN(yysound, WaveIdle)
IOP_FN(yysound, VoiceIdle)
IOP_FN(yysound, BuffInit)
IOP_FN(yysound, EndAllThread)
IOP_FN(yysound, CmdTerm)
IOP_FN(yysound, soundFunc)
IOP_FN(yysound, make_Timer_thread)
IOP_FN(yysound, make_EE_thread)
IOP_FN(yysound, make_Load_thread)
IOP_FN(yysound, StartTimerThread)
IOP_FN(yysound, StartEEThread)
IOP_FN(yysound, StartLoadThread)
IOP_FN(yysound, Init)
IOP_FN(yysound, start)
IOP_FN_BLOCKING(yysound, OpenFileSPU)
IOP_FN_BLOCKING(yysound, RunMidiOpen)
IOP_FN_BLOCKING(yysound, RunWaveOpen)
IOP_FN_BLOCKING(yysound, RunCopyJointSPU)
IOP_FN_BLOCKING(yysound, RunCommand)
IOP_FN_THREAD(yysound, atick, stub::SleepThread, false)
IOP_FN_THREAD(yysound, atick_resume, stub::SleepThread, true)
IOP_FN_THREAD(yysound, Load_atick, stub::SleepThread, false)
IOP_FN_THREAD(yysound, Load_atick_resume, stub::SleepThread, true)
IOP_FN_THREAD(yysound, sce_ee_loop, stub::sceSifRpcLoop, false)
