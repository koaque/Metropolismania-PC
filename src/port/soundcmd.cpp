// The sound command queue: each function fills the next entry of the ring for the YYSOUND driver and
// returns that entry's index. Arguments go to +0x10 onwards; which fields a command uses, and their widths,
// differ per command, so each writes exactly its own.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

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

    // Removes the PlayB commands (3) not yet sent from the ring, moving the later entries up over them, and sets
    // the write index after the last one kept. It matches the voice against byte 3 of the command word, which is
    // 0 for every command, rather than the voice byte at +0x12: called with 0, it drops every queued PlayB.
    void SoundCmd_CancelPlayB(char voice)
    {
        const int32_t end = globals::soundCmdIndex.get();
        int32_t kept = globals::soundCmdSent.get();
        for (int32_t index = kept; index != end;)
        {
            const uint32_t entry = entryAt(index);
            const int32_t command = port::load<int32_t>(entry);
            const bool cancel = command == 3 && port::load<uint8_t>(entry + 3u) == static_cast<uint8_t>(voice);
            if (!cancel)
            {
                if (kept != index)
                {
                    const uint32_t to = entryAt(kept);
                    for (uint32_t offset = 0; offset < 0xD0u; offset += 4u)
                        port::store<uint32_t>(to + offset, port::load<uint32_t>(entry + offset));
                }
                if (!(++kept < 0x100))
                    kept = 0;
            }
            if (!(++index < 0x100))
                index = 0;
        }
        globals::soundCmdIndex.set(kept);
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
    PORT_FN(SoundCmd_CancelPlayB);

    // Sends the queued sound commands to the IOP driver, one per call and only while the RPC client is idle. The driver's status
    // block (0x20B7C0) names the sequence slots (+4), effect voices (+8) and stops (+0x18) it has finished with, which leave the
    // queued masks; with the queue empty the counters restart (and the masks clear, unless the driver reports itself busy at +0xC).
    // A command is a 0xD0-byte entry of the ring at 0x1FE6C0 (256 entries; command at +0, "sent" flag at +4, the arguments from
    // +0x10; the next to send is 0x1BDD48 and the next free 0x1BDD44): a sent one waits for the driver's reply word (0x20B800) and is
    // then retired; a new one is copied (or, for the commands with names, built with the path prefixes of 0x1B9D50 to 0x1B9DB0 and
    // ".." at 0x1B9D68) into the send buffer at 0x20B6C0 and called as RPC function 0x1001 to 0x3001 (command 0x17: function 2).
    // With 192 commands or more waiting, the ring is compacted to the commands that must not be lost (1, 2, 4, 5, 7, 9 to 0x10, 0x17 and
    // 0x18) with a log line, and a second line says when it is still that full.
    void SoundCmd_Flush()
    {
        constexpr uint32_t kRing = 0x001FE6C0u, kClient = 0x001FE690u, kStatus = 0x0020B7C0u, kSend = 0x0020B6C0u, kReply = 0x0020B800u;
        const auto entryOf = [&](int32_t index) { return kRing + 0xD0u * static_cast<uint32_t>(index); };
        const auto waiting = [&]() {
            int32_t count = globals::soundCmdIndex.get() - globals::soundCmdSent.get();
            if (count < 0)
                count += 0x100;
            return count;
        };
        sdk::FlushCache(0);
        const uint32_t slots = globals::soundSlotsQueued.get(), voices = globals::soundVoicesQueued.get(), stops = globals::soundStopsQueued.get();
        globals::soundSlotsQueued.set(slots & (slots ^ port::load<uint32_t>(kStatus + 4u)));
        globals::soundStopsQueued.set(stops & (stops ^ port::load<uint32_t>(kStatus + 0x18u)));
        globals::soundVoicesQueued.set(voices & (voices ^ port::load<uint32_t>(kStatus + 8u)));
        if (globals::soundCmdSent.get() == globals::soundCmdIndex.get())
        {
            const bool driverBusy = port::load<uint32_t>(kStatus + 0xCu) != 0;
            globals::soundCmdIndex.set(0);
            globals::soundCmdSent.set(0);
            if (driverBusy)
                return;
            globals::soundVoicesQueued.set(0);
            globals::soundSlotsQueued.set(0);
            globals::soundStopsQueued.set(0);
            return;
        }
        if (sdk::sceSifCheckStatRpc(kClient) == 0)
        {
            const uint32_t entry = entryOf(globals::soundCmdSent.get());
            if (port::load<int32_t>(entry + 4u) != 0)
            {
                sdk::FlushCache(0);
                if (port::load<uint32_t>(kReply) != 0)
                {
                    port::store<uint32_t>(entry, 0u);
                    port::store<uint32_t>(entry + 4u, 0u);
                    globals::soundCmdSent.set(globals::soundCmdSent.get() + 1);
                    if (globals::soundCmdSent.get() > 0xFF)
                        globals::soundCmdSent.set(0);
                }
            }
            else
            {
                const auto call = [&](int number) {
                    sdk::FlushCache(0);
                    sdk::FlushCache(0);
                    sdk::sceSifCallRpc(kClient, number, 1, kSend, 0x100, kReply, 0x40, 0u, 0u);
                };
                const auto copyArguments = [&](uint32_t bytes, int number) {
                    sdk::memcpy(kSend, entry + 0x10u, static_cast<int>(bytes));
                    call(number);
                };
                // A byte at +2 and a halfword at +0 of the send buffer, from the entry's byte and halfword at the offsets given.
                const auto byteAndHalf = [&](uint32_t byteAt, uint32_t halfAt, int number) {
                    port::store<uint8_t>(kSend + 2u, port::load<uint8_t>(entry + byteAt));
                    port::store<uint16_t>(kSend, port::load<uint16_t>(entry + halfAt));
                    call(number);
                };
                // A path: prefix, then the entry's name string, then "..".
                const auto path = [&](uint32_t at, uint32_t prefix, uint32_t nameAt) {
                    sdk::strcpy(at, prefix);
                    sdk::strcat(at, port::load<uint32_t>(entry + nameAt));
                    sdk::strcat(at, 0x001B9D68u);
                };
                switch (port::load<uint32_t>(entry))
                {
                case 1:
                    sdk::strcpy(kSend + 4u, 0x001B9DB0u);
                    sdk::strcpy(kSend + 0x44u, 0x001B9DB0u);
                    sdk::strcat(kSend + 4u, port::load<uint32_t>(entry + 0x10u));
                    sdk::strcat(kSend + 0x44u, port::load<uint32_t>(entry + 0x14u));
                    sdk::strcat(kSend + 4u, 0x001B9D68u);
                    sdk::strcat(kSend + 0x44u, 0x001B9D68u);
                    port::store<uint8_t>(kSend + 2u, port::load<uint8_t>(entry + 0xCu));
                    port::store<uint16_t>(kSend, port::load<uint16_t>(entry + 0x18u));
                    port::store<uint8_t>(kSend + 3u, port::load<uint8_t>(entry + 0x1Cu));
                    call(0x2001);
                    break;
                case 2:
                    byteAndHalf(0x10u, 0x14u, 0x2003);
                    break;
                case 3:
                    copyArguments(10, 0x2010);
                    break;
                case 4:
                    byteAndHalf(0x10u, 0x14u, 0x2011);
                    break;
                case 5:
                    copyArguments(12, 0x2021);
                    break;
                case 6:
                    copyArguments(12, 0x2020);
                    break;
                case 7:
                    copyArguments(8, 0x2025);
                    break;
                case 8:
                    copyArguments(6, 0x2022);
                    break;
                case 9:
                    sdk::strcpy(kSend + 3u, 0x001B9D50u);
                    sdk::strcpy(kSend + 0x43u, 0x001B9D70u);
                    sdk::strcpy(kSend + 0x83u, 0x001B9D90u);
                    sdk::strcat(kSend + 3u, port::load<uint32_t>(entry + 0x10u));
                    sdk::strcat(kSend + 0x43u, port::load<uint32_t>(entry + 0x14u));
                    sdk::strcat(kSend + 0x83u, port::load<uint32_t>(entry + 0x18u));
                    sdk::strcat(kSend + 3u, 0x001B9D68u);
                    sdk::strcat(kSend + 0x43u, 0x001B9D68u);
                    sdk::strcat(kSend + 0x83u, 0x001B9D68u);
                    port::store<uint8_t>(kSend + 2u, port::load<uint8_t>(entry + 0xCu));
                    port::store<uint8_t>(kSend + 0xC4u, 0);
                    port::store<uint16_t>(kSend, port::load<uint16_t>(entry + 0x1Cu));
                    call(0x1001);
                    break;
                case 10:
                    path(kSend + 3u, 0x001B9D50u, 0x10u);
                    port::store<uint8_t>(kSend + 0x43u, port::load<uint8_t>(entry + 0x14u));
                    port::store<uint16_t>(kSend, port::load<uint16_t>(entry + 0x18u));
                    call(0x2002);
                    break;
                case 11:
                    byteAndHalf(0x10u, 0x14u, 0x1003);
                    break;
                case 12:
                    copyArguments(6, 0x1010);
                    break;
                case 13:
                    byteAndHalf(0x10u, 0x14u, 0x1011);
                    break;
                case 14:
                    copyArguments(8, 0x1021);
                    break;
                case 15:
                    copyArguments(8, 0x1020);
                    break;
                case 16:
                    copyArguments(8, 0x1025);
                    break;
                case 17:
                    copyArguments(6, 0x1026);
                    break;
                case 18:
                    port::store<uint16_t>(kSend, port::load<uint16_t>(entry + 0x10u));
                    call(0x3000);
                    break;
                case 19:
                    copyArguments(3, 0x2100);
                    break;
                case 20:
                    copyArguments(0xC6, 0x2101);
                    break;
                case 21:
                    copyArguments(6, 0x2200);
                    break;
                case 22:
                    copyArguments(6, 0x2201);
                    break;
                case 23:
                    port::store<uint8_t>(kSend, port::load<uint8_t>(entry + 0x10u));
                    call(2);
                    break;
                case 24:
                    port::store<uint8_t>(kSend, port::load<uint8_t>(entry + 0x10u));
                    call(0x3001);
                    break;
                default:
                    break;
                }
                port::store<int32_t>(entryOf(globals::soundCmdSent.get()) + 4u, 1);
            }
        }
        if (waiting() >= 0xC0)
        {
            sdk::printf(0x001B9DD0u);
            const int32_t start = globals::soundCmdSent.get();
            int32_t read = start, write = start;
            while (read != globals::soundCmdIndex.get())
            {
                const uint32_t from = entryOf(read);
                const uint32_t cmd = port::load<uint32_t>(from);
                if (cmd == 1 || cmd == 2 || cmd - 4u < 2u || cmd == 0x18 || cmd == 7 || cmd - 9u < 8u || cmd == 0x17)
                {
                    if (write != read)
                    {
                        const uint32_t to = entryOf(write);
                        port::store<uint32_t>(to, cmd);
                        port::store<uint32_t>(to + 4u, port::load<uint32_t>(from + 4u));
                        port::store<uint32_t>(to + 8u, port::load<uint32_t>(from + 8u));
                        port::store<uint32_t>(to + 0xCu, port::load<uint32_t>(from + 0xCu));
                        for (uint32_t offset = 0x10u; offset < 0xD0u; offset += 8u)
                            port::store<uint64_t>(to + offset, port::load<uint64_t>(from + offset));
                    }
                    ++write;
                    if (write > 0xFF)
                        write = 0;
                }
                ++read;
                if (read > 0xFF)
                    read = 0;
            }
            int32_t kept = write - start;
            if (kept < 0)
                kept += 0x100;
            globals::soundCmdIndex.set(write);
            sdk::printf(0x001B9DF0u, kept);
        }
        if (waiting() >= 0xC0)
            sdk::printf(0x001B9E00u);
    }

    PORT_FN(SoundCmd_Flush);
}
