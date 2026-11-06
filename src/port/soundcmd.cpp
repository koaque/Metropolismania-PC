// The sound command queue: each function fills the next entry of the ring for the YYSOUND driver (SoundCmd) and
// returns that entry's index. Which fields a command uses, and their widths, differ per command, so each writes exactly
// its own. From +0x10 the commands on one track hold the driver's own argument block (YYSOUND.IRX's ST_* structs,
// config/iop/yysound_types.h), sent as it is: the request id the driver reports back, then the track (an effect voice
// or a sequence slot) and the file (a bank) where the command has them; the others are rebuilt when sent.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

#include <cstddef>

namespace mm
{
    namespace
    {
        // The ring's entry `index`.
        GuestPtr<SoundCmd> entryAt(int32_t index) { return GuestPtr<SoundCmd>{globals::kSoundCmds} + index; }

        // Moves the write index on (wrapping at 256) and returns the old one, as the original does in v0.
        int advance(int32_t index)
        {
            const int32_t next = index + 1;
            globals::soundCmdIndex.set(next > 255 ? 0 : next);
            return index;
        }

        // The common start of most commands: the command, and the state and both words at +8/+0xC cleared.
        void begin(GuestPtr<SoundCmd> cmd, int32_t command)
        {
            cmd->command = command;
            cmd->sent = 0;
            cmd->openFile = 0;
            cmd->kept = 0;
        }

        // Commands with one value for a track (SoundCmdTrackValue): the track, the value `set` writes, the request id.
        template <typename Set>
        int trackCommand(int32_t command, uint8_t track, uint16_t id, Set set)
        {
            const int32_t index = globals::soundCmdIndex.get();
            const GuestPtr<SoundCmd> cmd = entryAt(index);
            begin(cmd, command);
            cmd->trackValue.track = track;
            set(cmd->trackValue);
            cmd->trackValue.id = id;
            return advance(index);
        }

        // Commands rebuilt when sent (SoundCmdPair): the target's low byte and the id's low half, each kept as a word.
        int pairCommand(int32_t command, uint32_t target, uint32_t id)
        {
            const int32_t index = globals::soundCmdIndex.get();
            const GuestPtr<SoundCmd> cmd = entryAt(index);
            cmd->command = command;
            cmd->sent = 0;
            cmd->openFile = 0;
            cmd->pair.target = target & 0xFFu;
            cmd->pair.id = id & 0xFFFFu;
            cmd->kept = 0;
            return advance(index);
        }
    }

    // Loads a wave bank: its HD and BD file names into bank `bank`; `id` is the request id.
    int SoundCmd_WaveOpen(uint32_t bank, uint32_t type, int hdName, int bdName, uint32_t id)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const GuestPtr<SoundCmd> cmd = entryAt(index);
        cmd->command = 1;
        cmd->sent = 0;
        cmd->openFile = bank & 0xFFu;
        cmd->waveOpen.hdName = static_cast<uint32_t>(hdName);
        cmd->waveOpen.bdName = static_cast<uint32_t>(bdName);
        cmd->waveOpen.id = id & 0xFFFFu;
        cmd->waveOpen.type = type & 0xFFu;
        cmd->kept = 0;
        return advance(index);
    }

    int SoundCmd_WaveClose(uint32_t bank, uint32_t id) { return pairCommand(2, bank, id); }
    // Closes the sound bank set mm_Sound_ResetVoices opens (wave bank 0, file 0).
    int SoundCmd_WaveCloseBankSet() { return SoundCmd_WaveClose(0, 0); }

    // Plays sound `sound` of bank `bank` on an effect voice and marks the voice busy: with `program`, at `note` (60
    // plays it as recorded), `loop` times (0 until stopped). `kept` (+8, not sent; nothing reads it) is 1 for a voice
    // kept until stopped.
    int SoundCmd_WavePlay(uint32_t voice, uint8_t bank, uint16_t sound, uint8_t program, uint8_t note, uint8_t loop, int kept, uint16_t id)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const GuestPtr<SoundCmd> cmd = entryAt(index);
        cmd->command = 3;
        cmd->sent = 0;
        cmd->openFile = 0;
        cmd->kept = kept;
        SoundCmdWavePlay &play = cmd->wavePlay;
        play.track = static_cast<uint8_t>(voice);
        play.file = bank;
        play.program = program;
        play.note = note;
        play.loop = loop;
        play.id = id;
        play.sound = sound;
        globals::soundVoicesQueued.set(globals::soundVoicesQueued.get() | (1u << (voice & 31u)));
        return advance(index);
    }

    int SoundCmd_WaveStop(uint32_t voice, uint32_t id) { return pairCommand(4, voice, id); }

    int SoundCmd_WaveFade(uint8_t voice, uint16_t leftGoal, uint16_t rightGoal, uint16_t leftStep, uint16_t rightStep, uint16_t id)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const GuestPtr<SoundCmd> cmd = entryAt(index);
        begin(cmd, 5);
        SoundCmdWaveFade &fade = cmd->waveFade;
        fade.track = voice;
        fade.leftGoal = leftGoal;
        fade.rightGoal = rightGoal;
        fade.leftStep = leftStep;
        fade.rightStep = rightStep;
        fade.id = id;
        return advance(index);
    }

    int SoundCmd_WaveSetVolume(uint8_t voice, int left, int right, uint16_t id)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const GuestPtr<SoundCmd> cmd = entryAt(index);
        begin(cmd, 6);
        cmd->waveVolume.track = voice;
        cmd->waveVolume.left = left;
        cmd->waveVolume.right = right;
        cmd->waveVolume.id = id;
        return advance(index);
    }

    int SoundCmd_WaveSetTrackVolume(uint8_t voice, int volume, uint16_t id)
    {
        return trackCommand(7, voice, id, [&](SoundCmdTrackValue &value) { value.volume = volume; });
    }

    int SoundCmd_WaveSetPitch(uint8_t voice, uint16_t pitch, uint16_t id)
    {
        return trackCommand(8, voice, id, [&](SoundCmdTrackValue &value) { value.pitch = pitch; });
    }

    // Loads a sound bank: SQ, HD and BD file names into slot `slot`; `id` is the request id.
    int SoundCmd_MidiOpen(uint32_t slot, int sqName, int hdName, int bdName, uint32_t id)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const GuestPtr<SoundCmd> cmd = entryAt(index);
        cmd->command = 9;
        cmd->sent = 0;
        cmd->openFile = slot & 0xFFu;
        cmd->midiOpen.sqName = static_cast<uint32_t>(sqName);
        cmd->midiOpen.hdName = static_cast<uint32_t>(hdName);
        cmd->midiOpen.bdName = static_cast<uint32_t>(bdName);
        cmd->midiOpen.id = id & 0xFFFFu;
        cmd->kept = 0;
        return advance(index);
    }

    int SoundCmd_MidiClose(uint32_t slot, uint32_t id) { return pairCommand(0xB, slot, id); }

    // Plays the sequence of bank `file` (opened by SoundCmd_MidiOpen) in slot `slot`, `loop` times (0 until stopped),
    // and marks the slot busy; `kept` as for SoundCmd_WavePlay.
    int SoundCmd_MidiPlay(uint32_t slot, uint8_t file, uint8_t loop, int kept, uint16_t id)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const GuestPtr<SoundCmd> cmd = entryAt(index);
        cmd->command = 0xC;
        cmd->sent = 0;
        cmd->openFile = 0;
        cmd->kept = kept;
        cmd->midiPlay.track = static_cast<uint8_t>(slot);
        cmd->midiPlay.file = file;
        cmd->midiPlay.loop = loop;
        cmd->midiPlay.id = id;
        globals::soundSlotsQueued.set(globals::soundSlotsQueued.get() | (1u << (slot & 31u)));
        return advance(index);
    }

    int SoundCmd_MidiStop(uint32_t voice, uint32_t id) { return pairCommand(0xD, voice, id); }

    int SoundCmd_MidiFade(uint8_t voice, uint16_t target, uint16_t time, uint16_t id)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const GuestPtr<SoundCmd> cmd = entryAt(index);
        begin(cmd, 0xE);
        cmd->midiFade.track = voice;
        cmd->midiFade.goal = target;
        cmd->midiFade.step = time;
        cmd->midiFade.id = id;
        return advance(index);
    }

    int SoundCmd_MidiSetVolume(uint8_t voice, int volume, uint16_t id)
    {
        return trackCommand(0xF, voice, id, [&](SoundCmdTrackValue &value) { value.volume = volume; });
    }

    int SoundCmd_MidiSetTrackVolume(uint8_t voice, int volume, uint16_t id)
    {
        return trackCommand(0x10, voice, id, [&](SoundCmdTrackValue &value) { value.volume = volume; });
    }

    int SoundCmd_MidiSetTempo(uint8_t voice, uint16_t tempo, uint16_t id)
    {
        return trackCommand(0x11, voice, id, [&](SoundCmdTrackValue &value) { value.tempo = tempo; });
    }

    // Tells the driver the disc is busy (1) or free (0); only the command and that word are written.
    int SoundCmd_SetLoadPause(uint32_t busy)
    {
        const int32_t index = globals::soundCmdIndex.get();
        const GuestPtr<SoundCmd> cmd = entryAt(index);
        cmd->command = 0x18;
        cmd->loadPause = busy & 0xFFu;
        return advance(index);
    }

    // Removes the WavePlay commands (3) not yet sent from the ring, moving the later entries up over them, and sets
    // the write index after the last one kept. It matches the voice against byte 3 of the command word, which is
    // 0 for every command, rather than the voice byte (wavePlay.track): called with 0, it drops every queued WavePlay.
    void SoundCmd_CancelWavePlay(char voice)
    {
        const int32_t end = globals::soundCmdIndex.get();
        int32_t kept = globals::soundCmdSent.get();
        for (int32_t index = kept; index != end;)
        {
            const GuestPtr<SoundCmd> cmd = entryAt(index);
            const uint8_t commandByte3 = static_cast<uint8_t>(static_cast<uint32_t>(cmd->command) >> 24);
            const bool cancel = cmd->command == 3 && commandByte3 == static_cast<uint8_t>(voice);
            if (!cancel)
            {
                if (kept != index)
                    *entryAt(kept) = *cmd;
                if (!(++kept < 0x100))
                    kept = 0;
            }
            if (!(++index < 0x100))
                index = 0;
        }
        globals::soundCmdIndex.set(kept);
    }

    PORT_FN(SoundCmd_WaveOpen);
    PORT_FN(SoundCmd_WaveClose);
    PORT_FN(SoundCmd_WaveCloseBankSet);
    PORT_FN(SoundCmd_WavePlay);
    PORT_FN(SoundCmd_WaveStop);
    PORT_FN(SoundCmd_WaveFade);
    PORT_FN(SoundCmd_WaveSetVolume);
    PORT_FN(SoundCmd_WaveSetTrackVolume);
    PORT_FN(SoundCmd_WaveSetPitch);
    PORT_FN(SoundCmd_MidiOpen);
    PORT_FN(SoundCmd_MidiClose);
    PORT_FN(SoundCmd_MidiPlay);
    PORT_FN(SoundCmd_MidiStop);
    PORT_FN(SoundCmd_MidiFade);
    PORT_FN(SoundCmd_MidiSetVolume);
    PORT_FN(SoundCmd_MidiSetTrackVolume);
    PORT_FN(SoundCmd_MidiSetTempo);
    PORT_FN(SoundCmd_SetLoadPause);
    PORT_FN(SoundCmd_CancelWavePlay);

    // Sends the queued sound commands to the IOP driver, one per call and only while the RPC client is idle. The bits
    // the driver's status block (SoundStatus at 0x20B7C0) reports in use, sequence slots (midiPlaying), wave voices
    // (wavePlaying) and the tracks spoken voices use (speechTracks), leave the queued masks (the driver has taken those
    // commands); with the queue empty the counters restart (and the masks clear, unless the driver reports itself
    // busy). A command is a SoundCmd entry of the ring (the next to send is 0x1BDD48 and the next free 0x1BDD44): a sent
    // one waits for the driver's reply word (0x20B800) and is then retired; a new one is copied (or, for the commands
    // with names, built with the path prefixes of 0x1B9D50 to 0x1B9DB0 and ".." at 0x1B9D68) into the send buffer at
    // 0x20B6C0 and called as RPC function 0x1001 to 0x3001 (command 0x17: function 2). None of the queue functions
    // above writes commands 10 or 0x12 to 0x17; their arguments are read as words from +0x10. With 192 commands or more
    // waiting, the ring is compacted to the commands that must not be lost (1, 2, 4, 5, 7, 9 to 0x10, 0x17 and 0x18) with
    // a log line, and a second line says when it is still that full.
    void SoundCmd_Flush()
    {
        constexpr uint32_t kClient = 0x001FE690u, kStatus = 0x0020B7C0u, kSend = 0x0020B6C0u, kReply = 0x0020B800u;
        const auto waiting = [&]() {
            int32_t count = globals::soundCmdIndex.get() - globals::soundCmdSent.get();
            if (count < 0)
                count += 0x100;
            return count;
        };
        sdk::FlushCache(0);
        const uint32_t slots = globals::soundSlotsQueued.get(), voices = globals::soundVoicesQueued.get(), speech = globals::soundSpeechQueued.get();
        const GuestPtr<SoundStatus> status{kStatus};
        globals::soundSlotsQueued.set(slots & (slots ^ status->midiPlaying));
        globals::soundSpeechQueued.set(speech & (speech ^ status->speechTracks));
        globals::soundVoicesQueued.set(voices & (voices ^ status->wavePlaying));
        if (globals::soundCmdSent.get() == globals::soundCmdIndex.get())
        {
            const bool driverBusy = status->busy != 0;
            globals::soundCmdIndex.set(0);
            globals::soundCmdSent.set(0);
            if (driverBusy)
                return;
            globals::soundVoicesQueued.set(0);
            globals::soundSlotsQueued.set(0);
            globals::soundSpeechQueued.set(0);
            return;
        }
        if (sdk::sceSifCheckStatRpc(kClient) == 0)
        {
            const GuestPtr<SoundCmd> cmd = entryAt(globals::soundCmdSent.get());
            if (cmd->sent != 0)
            {
                sdk::FlushCache(0);
                if (port::load<uint32_t>(kReply) != 0)
                {
                    cmd->command = 0;
                    cmd->sent = 0;
                    globals::soundCmdSent.set(globals::soundCmdSent.get() + 1);
                    if (globals::soundCmdSent.get() > 0xFF)
                        globals::soundCmdSent.set(0);
                }
            }
            else
            {
                const uint32_t args = cmd.address + static_cast<uint32_t>(offsetof(SoundCmd, args));
                const auto call = [&](int number) {
                    sdk::FlushCache(0);
                    sdk::FlushCache(0);
                    sdk::sceSifCallRpc(kClient, number, 1, kSend, 0x100, kReply, 0x40, 0u, 0u);
                };
                const auto copyArguments = [&](uint32_t bytes, int number) {
                    sdk::memcpy(kSend, args, static_cast<int>(bytes));
                    call(number);
                };
                // The track or file byte at +2 and the request id at +0 of the send buffer, from a SoundCmdPair.
                const auto sendPair = [&](int number) {
                    port::store<uint8_t>(kSend + 2u, static_cast<uint8_t>(cmd->pair.target));
                    port::store<uint16_t>(kSend, static_cast<uint16_t>(cmd->pair.id));
                    call(number);
                };
                // A path: prefix, then the name string, then "..".
                const auto path = [&](uint32_t at, uint32_t prefix, uint32_t name) {
                    sdk::strcpy(at, prefix);
                    sdk::strcat(at, name);
                    sdk::strcat(at, 0x001B9D68u);
                };
                switch (static_cast<uint32_t>(cmd->command))
                {
                case 1:
                    sdk::strcpy(kSend + 4u, 0x001B9DB0u);
                    sdk::strcpy(kSend + 0x44u, 0x001B9DB0u);
                    sdk::strcat(kSend + 4u, cmd->waveOpen.hdName);
                    sdk::strcat(kSend + 0x44u, cmd->waveOpen.bdName);
                    sdk::strcat(kSend + 4u, 0x001B9D68u);
                    sdk::strcat(kSend + 0x44u, 0x001B9D68u);
                    port::store<uint8_t>(kSend + 2u, static_cast<uint8_t>(cmd->openFile));
                    port::store<uint16_t>(kSend, static_cast<uint16_t>(cmd->waveOpen.id));
                    port::store<uint8_t>(kSend + 3u, static_cast<uint8_t>(cmd->waveOpen.type));
                    call(0x2001);
                    break;
                case 2:
                    sendPair(0x2003);
                    break;
                case 3:
                    copyArguments(10, 0x2010);
                    break;
                case 4:
                    sendPair(0x2011);
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
                    sdk::strcat(kSend + 3u, cmd->midiOpen.sqName);
                    sdk::strcat(kSend + 0x43u, cmd->midiOpen.hdName);
                    sdk::strcat(kSend + 0x83u, cmd->midiOpen.bdName);
                    sdk::strcat(kSend + 3u, 0x001B9D68u);
                    sdk::strcat(kSend + 0x43u, 0x001B9D68u);
                    sdk::strcat(kSend + 0x83u, 0x001B9D68u);
                    port::store<uint8_t>(kSend + 2u, static_cast<uint8_t>(cmd->openFile));
                    port::store<uint8_t>(kSend + 0xC4u, 0);
                    port::store<uint16_t>(kSend, static_cast<uint16_t>(cmd->midiOpen.id));
                    call(0x1001);
                    break;
                case 10:
                    path(kSend + 3u, 0x001B9D50u, port::load<uint32_t>(args));
                    port::store<uint8_t>(kSend + 0x43u, port::load<uint8_t>(args + 4u));
                    port::store<uint16_t>(kSend, port::load<uint16_t>(args + 8u));
                    call(0x2002);
                    break;
                case 11:
                    sendPair(0x1003);
                    break;
                case 12:
                    copyArguments(6, 0x1010);
                    break;
                case 13:
                    sendPair(0x1011);
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
                    port::store<uint16_t>(kSend, port::load<uint16_t>(args));
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
                    port::store<uint8_t>(kSend, port::load<uint8_t>(args));
                    call(2);
                    break;
                case 24:
                    port::store<uint8_t>(kSend, static_cast<uint8_t>(cmd->loadPause));
                    call(0x3001);
                    break;
                default:
                    break;
                }
                entryAt(globals::soundCmdSent.get())->sent = 1;
            }
        }
        if (waiting() >= 0xC0)
        {
            sdk::printf(0x001B9DD0u);
            const int32_t start = globals::soundCmdSent.get();
            int32_t read = start, write = start;
            while (read != globals::soundCmdIndex.get())
            {
                const GuestPtr<SoundCmd> from = entryAt(read);
                const uint32_t command = static_cast<uint32_t>(from->command);
                if (command == 1 || command == 2 || command - 4u < 2u || command == 0x18 || command == 7 || command - 9u < 8u ||
                    command == 0x17)
                {
                    if (write != read)
                        *entryAt(write) = *from;
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
