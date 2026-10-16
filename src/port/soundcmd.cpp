// The sound command queue: each function fills the next entry of the ring for the YYSOUND driver and
// returns that entry's index. Arguments go to +0x10 onwards; which fields a command uses, and their widths,
// differ per command, so each writes exactly its own.

#include "game_functions.h"
#include "game_globals.h"

namespace mm
{
    namespace
    {
        // Address of the entry at the write index.
        uint32_t entryAt(int32_t index) { return globals::kSoundCmds + 0xD0u * static_cast<uint32_t>(index); }

        // Moves the write index on (wrapping at 256) and returns the old one, as the original does in v0.
        int advance(int32_t index)
        {
            const int32_t next = index + 1;
            globals::soundCmdIndex.set(next > 255 ? 0 : next);
            return index;
        }

        template <typename T>
        void put(uint32_t entry, uint32_t offset, T value) { port::store<T>(entry + offset, value); }

        // The common start of most commands: the command, and the state and both words at +8/+0xC cleared.
        void begin(uint32_t entry, int32_t command)
        {
            put<int32_t>(entry, 0x0, command);
            put<int32_t>(entry, 0x4, 0);
            put<int32_t>(entry, 0xC, 0);
            put<int32_t>(entry, 0x8, 0);
        }

        // Commands with a voice byte at +0x12, a halfword at +0x10 and a word (or halfword) at +0x14.
        template <typename T>
        int voiceCommand(int32_t command, uint8_t voice, T value, uint16_t extra)
        {
            const int32_t index = globals::soundCmdIndex.get();
            const uint32_t entry = entryAt(index);
            begin(entry, command);
            put<uint8_t>(entry, 0x12, voice);
            put<T>(entry, 0x14, value);
            put<uint16_t>(entry, 0x10, extra);
            return advance(index);
        }

        // Commands with two masked words at +0x10 and +0x14.
        int pairCommand(int32_t command, uint32_t first, uint32_t second)
        {
            const int32_t index = globals::soundCmdIndex.get();
            const uint32_t entry = entryAt(index);
            put<int32_t>(entry, 0x0, command);
            put<int32_t>(entry, 0x4, 0);
            put<int32_t>(entry, 0xC, 0);
            put<uint32_t>(entry, 0x10, first & 0xFFu);
            put<uint32_t>(entry, 0x14, second & 0xFFFFu);
            put<int32_t>(entry, 0x8, 0);
            return advance(index);
        }
    }

    int SoundCmd_01(uint32_t a0, uint32_t a1, int a2, int a3, uint32_t t0)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const uint32_t entry = entryAt(index);
        put<int32_t>(entry, 0x0, 1);
        put<int32_t>(entry, 0x4, 0);
        put<uint32_t>(entry, 0xC, a0 & 0xFFu);
        put<int32_t>(entry, 0x10, a2);
        put<int32_t>(entry, 0x14, a3);
        put<uint32_t>(entry, 0x18, t0 & 0xFFFFu);
        put<uint32_t>(entry, 0x1C, a1 & 0xFFu);
        put<int32_t>(entry, 0x8, 0);
        return advance(index);
    }

    int SoundCmd_02(uint32_t a0, uint32_t a1) { return pairCommand(2, a0, a1); }
    int SoundCmd_02_Zero() { return SoundCmd_02(0, 0); }

    // Plays sound `sound` of bank `bank` on an effect voice and marks the voice busy.
    int SoundCmd_PlayB(uint32_t voice, uint8_t bank, uint16_t sound, uint8_t a3, uint8_t t0, uint8_t t1, int t2, uint16_t t3)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const uint32_t entry = entryAt(index);
        put<int32_t>(entry, 0x0, 3);
        put<int32_t>(entry, 0x4, 0);
        put<int32_t>(entry, 0xC, 0);
        put<int32_t>(entry, 0x8, t2);
        put<uint8_t>(entry, 0x12, static_cast<uint8_t>(voice));
        put<uint8_t>(entry, 0x13, bank);
        put<uint8_t>(entry, 0x16, a3);
        put<uint8_t>(entry, 0x17, t0);
        put<uint8_t>(entry, 0x18, t1);
        put<uint16_t>(entry, 0x10, t3);
        put<uint16_t>(entry, 0x14, sound);
        globals::soundVoicesQueued.set(globals::soundVoicesQueued.get() | (1u << (voice & 31u)));
        return advance(index);
    }

    int SoundCmd_StopA(uint32_t voice, uint32_t a1) { return pairCommand(4, voice, a1); }

    int SoundCmd_PlayA(uint8_t voice, uint16_t a1, uint16_t a2, uint16_t a3, uint16_t t0, uint16_t t1)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const uint32_t entry = entryAt(index);
        begin(entry, 5);
        put<uint8_t>(entry, 0x12, voice);
        put<uint16_t>(entry, 0x14, a1);
        put<uint16_t>(entry, 0x16, a2);
        put<uint16_t>(entry, 0x18, a3);
        put<uint16_t>(entry, 0x1A, t0);
        put<uint16_t>(entry, 0x10, t1);
        return advance(index);
    }

    int SoundCmd_SetVolumeLRA(uint8_t voice, int left, int right, uint16_t a3)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const uint32_t entry = entryAt(index);
        begin(entry, 6);
        put<uint8_t>(entry, 0x12, voice);
        put<int32_t>(entry, 0x14, left);
        put<int32_t>(entry, 0x18, right);
        put<uint16_t>(entry, 0x10, a3);
        return advance(index);
    }

    int SoundCmd_SetVolumeA(uint8_t voice, int volume, uint16_t a2) { return voiceCommand<int32_t>(7, voice, volume, a2); }
    int SoundCmd_SetPitchA(uint8_t voice, uint16_t pitch, uint16_t a2) { return voiceCommand<uint16_t>(8, voice, pitch, a2); }

    // Loads a sound bank: SQ, HD and BD file names into slot `slot`.
    int SoundCmd_LoadBank(uint32_t slot, int sqName, int hdName, int bdName, uint32_t flags)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const uint32_t entry = entryAt(index);
        put<int32_t>(entry, 0x0, 9);
        put<int32_t>(entry, 0x4, 0);
        put<uint32_t>(entry, 0xC, slot & 0xFFu);
        put<int32_t>(entry, 0x10, sqName);
        put<int32_t>(entry, 0x14, hdName);
        put<int32_t>(entry, 0x18, bdName);
        put<uint32_t>(entry, 0x1C, flags & 0xFFFFu);
        put<int32_t>(entry, 0x8, 0);
        return advance(index);
    }

    int SoundCmd_StopSequence(uint32_t slot, uint32_t a1) { return pairCommand(0xB, slot, a1); }

    // Plays a sequence in slot `slot` and marks the slot busy.
    int SoundCmd_PlaySequence(uint32_t slot, uint8_t a1, uint8_t a2, int a3, uint16_t t0)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const uint32_t entry = entryAt(index);
        put<int32_t>(entry, 0x0, 0xC);
        put<int32_t>(entry, 0x4, 0);
        put<int32_t>(entry, 0xC, 0);
        put<int32_t>(entry, 0x8, a3);
        put<uint8_t>(entry, 0x12, static_cast<uint8_t>(slot));
        put<uint8_t>(entry, 0x13, a1);
        put<uint8_t>(entry, 0x14, a2);
        put<uint16_t>(entry, 0x10, t0);
        globals::soundSlotsQueued.set(globals::soundSlotsQueued.get() | (1u << (slot & 31u)));
        return advance(index);
    }

    int SoundCmd_StopB(uint32_t voice, uint32_t a1) { return pairCommand(0xD, voice, a1); }

    int SoundCmd_FadeB(uint8_t voice, uint16_t target, uint16_t time, uint16_t a3)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const uint32_t entry = entryAt(index);
        begin(entry, 0xE);
        put<uint8_t>(entry, 0x12, voice);
        put<uint16_t>(entry, 0x14, target);
        put<uint16_t>(entry, 0x16, time);
        put<uint16_t>(entry, 0x10, a3);
        return advance(index);
    }

    int SoundCmd_0F(uint8_t voice, int a1, uint16_t a2) { return voiceCommand<int32_t>(0xF, voice, a1, a2); }
    int SoundCmd_SetVolumeB(uint8_t voice, int volume, uint16_t a2) { return voiceCommand<int32_t>(0x10, voice, volume, a2); }
    int SoundCmd_11(uint8_t voice, uint16_t a1, uint16_t a2) { return voiceCommand<uint16_t>(0x11, voice, a1, a2); }

    // Tells the driver the disc is busy (1) or free (0); only the command and that byte are written.
    int SoundCmd_SetDiscBusy(uint32_t busy)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const uint32_t entry = entryAt(index);
        put<int32_t>(entry, 0x0, 0x18);
        put<uint32_t>(entry, 0x10, busy & 0xFFu);
        return advance(index);
    }

    PORT_FN(SoundCmd_01);
    PORT_FN(SoundCmd_02);
    PORT_FN(SoundCmd_02_Zero);
    PORT_FN(SoundCmd_PlayB);
    PORT_FN(SoundCmd_StopA);
    PORT_FN(SoundCmd_PlayA);
    PORT_FN(SoundCmd_SetVolumeLRA);
    PORT_FN(SoundCmd_SetVolumeA);
    PORT_FN(SoundCmd_SetPitchA);
    PORT_FN(SoundCmd_LoadBank);
    PORT_FN(SoundCmd_StopSequence);
    PORT_FN(SoundCmd_PlaySequence);
    PORT_FN(SoundCmd_StopB);
    PORT_FN(SoundCmd_FadeB);
    PORT_FN(SoundCmd_0F);
    PORT_FN(SoundCmd_SetVolumeB);
    PORT_FN(SoundCmd_11);
    PORT_FN(SoundCmd_SetDiscBusy);
}
