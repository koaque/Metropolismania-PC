// YYSOUND.IRX (Indi Software, yysound.irx 2002.05.24), the game's sound driver, as readable C++ (plan P5). Every function keeps the name,
// parameters and structures of the module's own debug info (config/iop/yysound_functions.csv, yysound_types.h, yysound_globals.h) and replaces
// the original at its address; PS2X_IOP_VERIFY=1 runs the original beside each one and compares the result.

#include "iop_port.h"

#include "yysound_functions.h"
#include "yysound_globals.h"

#include <cstddef>

namespace iop::yysound
{
    namespace
    {
        // The module's imports: each is its stub in the module's import table, run as guest code.
        int32_t iWakeupThread(int32_t thread) { return static_cast<int32_t>(callImport(kBase + stub::iWakeupThread, 1, static_cast<uint32_t>(thread))); }
        void strcpy(uint32_t destination, uint32_t source) { callImport(kBase + stub::strcpy, 2, destination, source); }

        // Functions of the module that are not ported yet run as the original.
        int32_t CmdMidiClose(IopPtr<ST_MIDI_CLOSE> data) { return static_cast<int32_t>(call(kBase + offset::CmdMidiClose, data.address)); }
    }

    // ---- hard timer

    // The handler of the hard timers, in interrupt context: wakes the thread that waits for the tick and returns the timer's count.
    UINT timer_handler(IopPtr<void> common)
    {
        const IopPtr<TimerCtx> timer{common.address};
        iWakeupThread(timer->thread_id);
        return timer->count;
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
            const IopPtr<ST_MIDI_CLOSE> close{stack.address(0x10)};
            close->fno = data->fno;
            const int32_t closed = CmdMidiClose(close);
            if (closed != 1)
                return closed;
        }
        const uint32_t entry = mFileList.address + offsetof(ST_FILE_LIST, list) + mFileList->PutPos * static_cast<uint32_t>(sizeof(ST_FILE_ONE));
        strcpy(entry + offsetof(ST_FILE_ONE, SQname), data.address + offsetof(ST_MIDI_OPEN, sqname));
        strcpy(entry + offsetof(ST_FILE_ONE, HDname), data.address + offsetof(ST_MIDI_OPEN, hdname));
        strcpy(entry + offsetof(ST_FILE_ONE, BDname), data.address + offsetof(ST_MIDI_OPEN, bdname));
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
IOP_FN(yysound, ClearMidiTrack)
IOP_FN(yysound, ClearWaveTrack)
IOP_FN(yysound, AddFilePutPos)
IOP_FN(yysound, AddRequestPutPos)
IOP_FN(yysound, CmdMidiOpen)
