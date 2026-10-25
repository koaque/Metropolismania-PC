// Sound front end: the per-frame update of effects, music fades and city ambience.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"
#include "vu0.h"

#include <initializer_list>
#include <utility>

namespace mm
{
    // Whether an object's sound plays at the current time: a window of `length` seconds from `start`
    // within each hour (staggered by the object's id, 600 s per id mod 6). Returns 0xFF, 0x100 or 0 as
    // the sound code expects; all arithmetic is on 16-bit seconds, as in the original.
    int Se_InTimeWindow(GuestPtr<MapObject> obj, uint32_t start, int length)
    {
        const uint32_t stagger = obj ? static_cast<uint32_t>(static_cast<int16_t>(obj->id % 6) * 600) : 0u;
        const uint32_t from = (stagger + (start & 0xFFFFu)) & 0xFFFFu;
        const uint32_t to = static_cast<uint32_t>(length) + stagger;
        const uint32_t now = static_cast<uint32_t>(globals::soundClock.get() % 3600) & 0xFFFFu;
        if (from < 3600)
        {
            if (from < now || now < stagger)
                return 0xFF;
            if ((to & 0xFFFFu) <= now)
                return 0x100;
        }
        else
        {
            if (((from - 3600) & 0xFFFFu) < now && now < stagger)
                return 0xFF;
            if ((to & 0xFFFFu) < 3600)
            {
                if ((to & 0xFFFFu) <= now)
                    return 0x100;
            }
            else
            {
                if (now < (((to & 0xFFFFu) - 3600) & 0xFFFFu))
                    return 0;
                if (now < stagger)
                    return 0x100;
            }
        }
        return 0;
    }

    // Once per frame from mm_GameUpdate.
    void Sound_Update()
    {
        if (globals::seEnabled.get() != 0)
        {
            Se_AssignVoices();
            Se_UpdateVoices();
        }

        // Nothing city-related until a city is set and its start delay has run out.
        if (!globals::soundCity.get() || globals::soundDelay.get() != 0)
        {
            if (globals::soundDelay.get() != 0)
                globals::soundDelay.set(globals::soundDelay.get() - 1);
            return;
        }

        int clockUnchanged = 0;
        int8_t bgmFade = globals::bgmFadeCounter.get();
        if (bgmFade != 0)
        {
            // Music fade-out: the volume of voice 15 steps down every 10 counts, then the voice stops.
            bgmFade = static_cast<int8_t>(bgmFade - 2);
            globals::bgmFadeCounter.set(bgmFade);
            if (bgmFade <= 0)
            {
                globals::bgmPlaying.set(0);
                globals::bgmFadeCounter.set(0);
                SoundCmd_StopB(15, 0);
            }
            else if (bgmFade % 10 == 0)
            {
                SoundCmd_SetVolumeB(15, bgmFade * 3 * 64 / 100, 0);
            }
        }
        else if (globals::bgmPlaying.get() != 0)
        {
            // While the clock stands still (menus, talks, the pause screen) the music plays quieter.
            const int mode = CityScene_GetMode(globals::soundCity.get());
            const int32_t clock = globals::soundCity.get()->clock;
            clockUnchanged = globals::soundLastClock.get() == clock;
            int quiet = clockUnchanged;
            if (mode == 4 || mode == 11 || mode == 3 || mode == 10)
                quiet = 0;
            globals::soundLastClock.set(clock);
            if (globals::clockStopped.get() != quiet)
            {
                globals::clockStopped.set(quiet);
                SoundCmd_SetVolumeB(15, quiet ? 0x80 : 0xC0, 0);
            }
        }

        int8_t seFade = globals::seFadeCounter.get();
        if (seFade != 0)
        {
            seFade = static_cast<int8_t>(seFade - 2);
            globals::seFadeCounter.set(seFade);
            if (seFade <= 0)
            {
                Se_SetEnabled(0, 1);
                SoundCmd_CancelPlayB(0);
            }
        }

        if (globals::bgmState.get() == 4 || globals::seFadeCounter.get() != 0)
            return;
        Bgm_Update();
        if (globals::clockStopped.get() == 0 && globals::seEnabled.get() != 0 && globals::ambienceEnabled.get() != 0)
        {
            Se_UpdateCityAmbience(clockUnchanged);
            Se_UpdateTimeOfDay();
        }
        if (globals::soundCity.get()->clock == 18000) // 5:00
            Se_BuildAmbienceMap();
    }

    // From mm_GameInit: every effect voice free, no city, music and effects on.
    void Sound_InitState()
    {
        for (uint32_t voice = 0; voice < 31; ++voice)
            port::store<uint8_t>(globals::kSeVoices + 0x30u * voice, 0xFF);
        globals::soundLastClock.set(-1);
        globals::seRequestCount.set(0);
        globals::soundNextOwner.set(0);
        globals::soundCity.set(GuestPtr<CityScene>{0});
        globals::clockStopped.set(0);
        globals::bgmFadeCounter.set(0);
        globals::seFadeCounter.set(0);
        globals::ambiencePhase.set(0);
        globals::bgmStarted.set(0);
        globals::bgmPlaying.set(1);
        globals::seEnabled.set(1);
        globals::ambienceEnabled.set(1);
    }

    // At 5:00 each day: spreads the city ambience over the 4096-cell grid. Each sound gets as many cells as
    // its count says, evenly spaced from a start that moves with the day and the stage; taken cells are
    // skipped.
    void Se_BuildAmbienceMap()
    {
        for (uint32_t cell = 0; cell < 0x1000; ++cell)
            port::store<uint8_t>(globals::kAmbienceGrid + 2u * cell, 0);
        const GuestPtr<CityScene> city = globals::soundCity.get();
        int day = city->monthCount;
        if (city->clock < 18000)
            --day;
        int pos = (day + globals::stageState()->stage * 5 + 0x4D) % 0x1000;
        for (int sound = 0; sound < 11; ++sound)
            for (int variant = 0; variant < 6; ++variant)
            {
                const uint32_t entry = globals::kAmbienceCounts + 0x1Cu * static_cast<uint32_t>(sound) + 0x10u + 2u * static_cast<uint32_t>(variant);
                uint16_t left = port::load<uint16_t>(entry);
                if (left == 0)
                    continue;
                const int step = 0x1000 / left;
                while (left != 0)
                {
                    if (pos >= 0x1000)
                        pos -= 0x1000;
                    const uint32_t cell = globals::kAmbienceGrid + 2u * static_cast<uint32_t>(pos);
                    if (port::load<uint8_t>(cell) != 0)
                    {
                        ++pos;
                        continue;
                    }
                    port::store<uint8_t>(cell, static_cast<uint8_t>(sound + 1));
                    port::store<uint8_t>(cell + 1u, static_cast<uint8_t>(variant));
                    --left;
                    pos += step;
                }
            }
    }

    // The four sound banks of the PLAYLAND set: each an SQ, HD and BD file name of 0x10 bytes at 0x1B2E20.
    void Sound_LoadPlaylandBanks()
    {
        for (uint32_t slot = 0; slot < 4; ++slot)
        {
            const int names = static_cast<int>(0x001B2E20u + 0x30u * slot);
            SoundCmd_LoadBank(slot & 0xFFu, names, names + 0x10, names + 0x20, 0);
        }
    }

    // Starts sound bank set `bankSet` (two file names of 0x10 bytes at 0x1B2970 + 0x20 per set) and resets every
    // voice: group A volumes 0xFFF, group B 0x100 except the music voice 15 at 0xC0, all effect voices free.
    void Sound_ResetVoices(int bankSet)
    {
        const int names = 0x1B2970 + bankSet * 0x20;
        SoundCmd_01(0, 0, names, names + 0x10, 0);
        for (uint32_t voice = 0; voice < 16; ++voice)
            SoundCmd_SetVolumeA(static_cast<uint8_t>(voice), 0xFFF, 0);
        for (uint32_t voice = 0; voice < 15; ++voice)
            SoundCmd_SetVolumeB(static_cast<uint8_t>(voice), 0x100, 0);
        SoundCmd_SetVolumeB(15, 0xC0, 0);
        for (uint32_t voice = 0; voice < 31; ++voice)
            port::store<uint8_t>(globals::kSeVoices + 0x30u * voice, 0xFF);
        globals::seRequestCount.set(0);
    }

    // Sends a voice's changed parameters to the driver: volume (flag 1; left and right plus the offset,
    // kept within 1..0x3FFF) and pitch (flag 2).
    void Se_ApplyVoiceParams(uint32_t voice)
    {
        const uint32_t v = globals::kSeVoices + 0x30u * voice;
        if ((port::load<uint8_t>(v + 0x1Cu) & 1u) != 0)
        {
            const int16_t offset = port::load<int16_t>(v + 0x12u);
            const auto clamp = [](int volume) { return volume < 1 ? 1 : (volume > 0x3FFF ? 0x3FFF : volume); };
            const int left = clamp(port::load<int32_t>(v + 0x14u) + offset);
            const int right = clamp(port::load<int32_t>(v + 0x18u) + offset);
            SoundCmd_SetVolumeLRA(static_cast<uint8_t>(voice), left, right, 0);
        }
        if ((port::load<uint8_t>(v + 0x1Cu) & 2u) != 0)
            SoundCmd_SetPitchA(static_cast<uint8_t>(voice), port::load<uint16_t>(v + 0xAu), 0);
    }

    // Music table entries 3..12 at 0x1F8120: (track id, enabled), id 0xFF = free. Sets or adds track slot + 6
    // and, when asked to play it, either starts a fade-out of the current music (voice 15) with the entry as the
    // next one, or queues it (bit 7) behind a fade already running (provisional reading of 0x1BDCF0).
    void Bgm_SetTrackSlot(char slot, int play, int enable)
    {
        constexpr uint32_t kTable = globals::kBgmTracks;
        const uint8_t id = static_cast<uint8_t>(slot + 6);
        const uint8_t enabled = enable != 0 ? 1 : 0;
        int found = -1;
        for (int i = 3; i < 13 && found == -1; ++i)
            if (port::load<uint8_t>(kTable + 2u * static_cast<uint32_t>(i)) == id)
            {
                port::store<uint8_t>(kTable + 2u * static_cast<uint32_t>(i) + 1u, enabled);
                found = i;
            }
        for (int i = 3; i < 13 && found == -1; ++i)
            if (port::load<uint8_t>(kTable + 2u * static_cast<uint32_t>(i)) == 0xFF)
            {
                port::store<uint8_t>(kTable + 2u * static_cast<uint32_t>(i), id);
                port::store<uint8_t>(kTable + 2u * static_cast<uint32_t>(i) + 1u, enabled);
                found = i;
            }
        if (found == -1 || enable == 0 || play == 0)
            return;
        if (globals::bgmState.get() == 0)
        {
            port::store<uint8_t>(0x001BDCF0u, static_cast<uint8_t>(found - 1));
            SoundCmd_FadeB(15, 0, 200, 0);
        }
        else if ((port::load<uint8_t>(0x001BDCF0u) & 0x80u) == 0)
            port::store<uint8_t>(0x001BDCF0u, static_cast<uint8_t>(found + 0x7F));
    }

    // The driver's status block as the game reads it: a copy with the plays, slots and third command group
    // still waiting in the command ring merged in (words 1, 2 and 6).
    namespace
    {
        void refreshStatusView()
        {
            sdk::memcpy(globals::kSoundStatusView, globals::kSoundStatus, 0x40);
            const auto merge = [](uint32_t offset, uint32_t queued)
            { port::store<uint32_t>(globals::kSoundStatusView + offset, port::load<uint32_t>(globals::kSoundStatus + offset) | queued); };
            merge(0x4, globals::soundSlotsQueued.get());
            merge(0x8, globals::soundVoicesQueued.get());
            merge(0x18, globals::soundStopsQueued.get());
        }
    }

    int Sound_GetStatus()
    {
        refreshStatusView();
        return static_cast<int>(globals::kSoundStatusView);
    }

    // Busy while the driver reports work (status word 3) or commands are still waiting to be sent.
    int Sound_IsBusy()
    {
        refreshStatusView();
        if (port::load<int32_t>(globals::kSoundStatusView + 0xCu) != 0)
            return 1;
        return globals::soundCmdSent.get() != globals::soundCmdIndex.get();
    }

    // City music, every frame while music plays: the mood follows the city (flag 4: 1; approval below 0: 2;
    // else 0), and a change of mood fades the music out. When the driver reports the sequence over, the next
    // one starts: in the mood-7 city mode with mood 0 the current track again; otherwise mood 0 moves on to
    // the next enabled track, moods 1 to 3 have their own banks (3, 4, 5, played once).
    void Bgm_Update()
    {
        if (globals::bgmPlaying.get() == 0)
            return;
        const GuestPtr<uint32_t> status{static_cast<uint32_t>(Sound_GetStatus())};
        const GuestPtr<CityScene> city = globals::soundCity.get();
        uint8_t mood = 0;
        if ((city->flags & 4u) != 0)
            mood = 1;
        else if (city->approval < 0)
            mood = 2;
        if (globals::bgmState.get() < 3 && globals::bgmState.get() != mood)
        {
            SoundCmd_FadeB(15, 0, 200, 0);
            globals::bgmState.set(mood);
        }
        const bool sequenceOver = (status[1] & 0x8000u) != 0;
        if (globals::bgmStarted.get() != 0)
        {
            if (sequenceOver)
                globals::bgmStarted.set(0);
            return;
        }
        if (sequenceOver)
            return;
        const auto trackBank = [](uint8_t track) { return port::load<uint8_t>(globals::kBgmTracks + 2u * track); };
        const auto tempo = [](uint32_t bank) { return port::load<int32_t>(globals::kBgmTempos + 4u * bank); };
        if (CityScene_GetMode(globals::soundCity.get()) == 7 && globals::bgmState.get() == 0 && globals::bgmPlayedState.get() == 0)
        {
            SoundCmd_0F(15, tempo(trackBank(globals::bgmTrack.get())), 0);
            SoundCmd_PlaySequence(15, 7, 1, 0, 1);
            globals::bgmStarted.set(1);
            return;
        }
        if (globals::bgmBankLoaded.get() != 0)
        {
            globals::bgmBankLoaded.set(0);
            SoundCmd_StopSequence(7, 0);
        }
        uint32_t bank = 0xFF;
        uint8_t loop = 1;
        switch (globals::bgmState.get())
        {
        case 3: bank = 5; loop = 0; break;
        case 2: bank = 4; loop = 0; break;
        case 1: bank = 3; loop = 0; break;
        case 0:
        {
            globals::bgmTrack.set(globals::bgmTrack.get() & 0x7F);
            uint8_t track;
            do
            {
                globals::bgmTrack.set(static_cast<uint8_t>(globals::bgmTrack.get() + 1));
                if (globals::bgmTrack.get() >= 13)
                    globals::bgmTrack.set(0);
                track = globals::bgmTrack.get();
            } while (trackBank(track) == 0xFF || port::load<uint8_t>(globals::kBgmTracks + 2u * track + 1u) == 0);
            bank = trackBank(track);
            break;
        }
        default: break;
        }
        if (bank == 0xFF)
            return;
        const uint32_t names = globals::kBgmBanks + 0x30u * bank;
        SoundCmd_LoadBank(7, static_cast<int>(names), static_cast<int>(names + 0x10u), static_cast<int>(names + 0x20u), 0);
        SoundCmd_0F(15, tempo(bank), 0);
        SoundCmd_PlaySequence(15, 7, loop, 0, 1);
        globals::bgmStarted.set(1);
        globals::bgmBankLoaded.set(1);
        globals::bgmPlayedState.set(globals::bgmState.get());
    }

    namespace
    {
        // Two requests trade places field by field (their padding stays put).
        void swapRequests(SeRequest &a, SeRequest &b)
        {
            std::swap(a.sound, b.sound);
            std::swap(a.voice, b.voice);
            std::swap(a.owner, b.owner);
            std::swap(a.priority, b.priority);
            std::swap(a.param0A, b.param0A);
            std::swap(a.param0B, b.param0B);
            std::swap(a.param0C, b.param0C);
            std::swap(a.voiceParam, b.voiceParam);
            std::swap(a.volumeLeft, b.volumeLeft);
            std::swap(a.volumeRight, b.volumeRight);
            std::swap(a.param18, b.param18);
            std::swap(a.param19, b.param19);
        }

        // One side's volume: `level` less the attenuation, scaled to the sound's volume in percent and kept
        // within 1..0x3FFF (in 32-bit arithmetic, as the original).
        int32_t requestVolume(uint32_t level, uint32_t attenuation, int32_t percent)
        {
            const int32_t scaled = static_cast<int32_t>((level - attenuation) * static_cast<uint32_t>(percent)) / 100;
            return scaled < 1 ? 1 : scaled >= 0x4000 ? 0x3FFF : scaled;
        }
    }

    // Queues effect `sound` for this frame: a request per voice in the sound's list (up to four; 0xFF ends it),
    // with a priority (the sound's priority bits, then half the distance; lower comes first) and left and right
    // volumes from the pan (angle * 4) and the priority's low byte, scaled by the sound's volume (0 = 100%).
    // A request for the same owner and sound already queued keeps the louder volumes instead; otherwise the new
    // one goes into the list by priority and the count rises, up to 578. Sounds beyond distance 16, sound 0xFF
    // and groups from 4 on are ignored. Returns the last voice's fptoui result (the pan for a sound without
    // voices); when it queues nothing, v0 as it found it.
    int Se_Request(uint16_t owner, uint32_t group, uint8_t sound, uint8_t param0A, uint8_t param18, uint16_t param0C,
                   uint8_t param0B, uint8_t param19, ps2f angle, ps2f distance)
    {
        if (!(distance <= ps2f(16.0f)) || sound == 0xFF || (group & 0xFFu) >= 4)
            return port::guestV0();
        const uint32_t entry = globals::kSoundTable + 0x10u * sound;
        const GuestPtr<SeRequest> requests{globals::kSeRequests};
        const int pan = sdk::fptosi(ps2f(4.0f) * angle);
        int result = pan;
        uint32_t channel = (group << 4) & 0xFFu;
        for (uint32_t k = 0; k < 4; ++k, ++channel)
        {
            const uint8_t voice = port::load<uint8_t>(entry + 8u + 2u * k);
            if (voice == 0xFF)
                return result;
            const uint8_t flags = port::load<uint8_t>(entry);
            SeRequest &request = requests[globals::seRequestCount.get()];
            request.sound = sound;
            request.voice = voice;
            request.owner = ((flags & 0x20u) != 0 ? 0xFFFFu : owner) + ((channel & 0xFFu) << 16);
            request.voiceParam = static_cast<uint16_t>(port::load<uint8_t>(entry + 9u + 2u * k) + ((flags & 0xF0u) << 4));
            const uint32_t half = sdk::fptoui(distance / ps2f(2.0f));
            result = static_cast<int>(half);
            request.priority = static_cast<uint16_t>(((flags & 0xFu) << 8) + half);
            request.param0A = param0A;
            request.param0C = static_cast<int16_t>(param0C);
            request.param0B = param0B;
            request.param18 = param18;
            request.param19 = param19;

            const uint32_t low = request.priority & 0xFFu;
            const uint8_t tableVolume = port::load<uint8_t>(globals::kSoundTable + 0x10u * request.sound + 3u);
            const int32_t percent = tableVolume != 0 ? tableVolume : 100;
            const uint32_t panShare = static_cast<uint32_t>(pan) * low * 416u;
            uint32_t left = 0x3FFF, right = 0x3FFF;
            if (pan < 0)
                right = 0x3FFFu + panShare;
            if (pan > 0)
                left = 0x3FFFu - panShare;
            const uint32_t attenuation = (low * 1984u) & 0xFFFFu;
            request.volumeLeft = requestVolume(left, attenuation, percent);
            request.volumeRight = requestVolume(right, attenuation, percent);

            bool merged = false;
            const int count = globals::seRequestCount.get();
            for (int i = 0; i < count; ++i)
            {
                SeRequest &queued = requests[i];
                if (queued.sound == 0xFF || request.owner != queued.owner || request.sound != queued.sound)
                    continue;
                if (queued.volumeLeft < request.volumeLeft)
                    queued.volumeLeft = request.volumeLeft;
                if (queued.volumeRight < request.volumeRight)
                    queued.volumeRight = request.volumeRight;
                merged = true;
                break;
            }
            if (merged)
                continue;
            for (int i = 0; i < globals::seRequestCount.get(); ++i)
            {
                SeRequest &queued = requests[i];
                if (queued.sound != 0xFF && request.priority < queued.priority)
                    swapRequests(queued, request);
            }
            if (globals::seRequestCount.get() < 0x242)
                globals::seRequestCount.set(static_cast<uint16_t>(globals::seRequestCount.get() + 1));
        }
        return result;
    }

    // Plays effect `sound`; sounds flagged 0x10 get the next owner id (wrapping at 0xFF) so they can be
    // stopped later. Returns Se_Request's result, which matters: the talk script's actions that end with a
    // sound (buying goods, adding a want, inviting a family ...) pass it on, and the script adds an action's
    // result to its line (0 here: no skip). Where Se_Request queues nothing it returns v0 as it found it, so
    // v0 is first set as the original has it there: the flag test (0), or the new owner id.
    int Sound_PlaySe(int sound, int a1, int a2, ps2f angle, ps2f distance)
    {
        const uint8_t flags = port::load<uint8_t>(globals::kSoundTable + 0x10u * static_cast<uint32_t>(sound & 0xFF));
        if ((flags & 0x10u) == 0)
        {
            port::setGuestV0(0);
            return Se_Request(0, 0, static_cast<uint8_t>(sound), 0, 0, static_cast<uint16_t>(a1), static_cast<uint8_t>(a2), 0, angle, distance);
        }
        const uint16_t owner = globals::soundNextOwner.get();
        globals::soundNextOwner.set(static_cast<uint16_t>(owner + 1));
        port::setGuestV0(owner + 1);
        const int result = Se_Request(owner, 0, static_cast<uint8_t>(sound), 0, 0, static_cast<uint16_t>(a1), static_cast<uint8_t>(a2), 0, angle, distance);
        if (globals::soundNextOwner.get() == 0xFF)
            globals::soundNextOwner.set(0);
        return result;
    }

    namespace
    {
        constexpr int kGroupA = 16, kGroupB = 15;

        GuestPtr<SeVoice> voiceSlots() { return GuestPtr<SeVoice>{globals::kSeVoices}; }

        // A voice already playing the request's sound for its owner takes the new priority and parameters.
        // Held requests only gather the loudest volumes (and, in group A, that request's pitch) for ambience
        // phase 3; others set the voice's change flags and take their volumes (and pitch) at once.
        void refreshVoice(SeVoice &voice, const SeRequest &request, bool groupA)
        {
            voice.flags = 0;
            voice.priority = request.priority;
            if (groupA)
                voice.param0B = request.param0B;
            voice.voiceParam = request.voiceParam;
            voice.hold = request.param19;
            if (request.param19 != 0)
            {
                const auto sum = [](int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); };
                if (groupA && sum(voice.heldLeft, voice.heldRight) < sum(request.volumeLeft, request.volumeRight))
                    voice.heldPitch = request.param0C;
                if (voice.heldLeft < request.volumeLeft)
                    voice.heldLeft = request.volumeLeft;
                if (voice.heldRight < request.volumeRight)
                    voice.heldRight = request.volumeRight;
            }
            else
            {
                if (voice.volumeLeft != request.volumeLeft || voice.volumeRight != request.volumeRight)
                    voice.flags = 1;
                if (groupA && voice.pitch != request.param0C)
                    voice.flags |= 2;
                voice.volumeLeft = request.volumeLeft;
                voice.volumeRight = request.volumeRight;
                if (groupA)
                    voice.pitch = request.param0C;
            }
            voice.flags |= 0x80;
        }

        // The request starts in `voice`; group A voices also take the pitch and rest 20 frames once freed.
        void startVoice(SeVoice &voice, SeRequest &request, bool groupA)
        {
            voice.sound = request.sound;
            voice.voice = request.voice;
            voice.owner = request.owner;
            voice.priority = request.priority;
            if (groupA)
            {
                voice.pitch = request.param0C;
                voice.param0B = request.param0B;
            }
            voice.voiceParam = request.voiceParam;
            voice.volumeLeft = request.volumeLeft;
            voice.volumeRight = request.volumeRight;
            voice.hold = request.param19;
            voice.heldLeft = request.volumeLeft;
            voice.heldRight = request.volumeRight;
            voice.scriptStep = 0;
            voice.scriptTimer = 0;
            voice.volumeOffset = 0;
            if (groupA)
            {
                voice.heldPitch = request.param0C;
                voice.rest = 20;
                voice.unk_2B[0] = 0;
                voice.unk_2B[1] = 0;
                voice.unk_2B[2] = 0;
            }
            request.sound = 0xFF;
        }

        // One listed voice of a request, in group A (first) or B: refresh the owner's voice for the sound, else
        // (unless the request only updates) start in a free voice (group A: one done resting), else steal the
        // voice with the highest priority value (group A: any free voice first). True when the request found
        // its voice. As in the original, a request already given a voice (its sound now 0xFF) can still mark a
        // free voice as started.
        bool assignVoice(SeRequest &request, bool groupA)
        {
            const GuestPtr<SeVoice> voices = voiceSlots();
            const int first = groupA ? 0 : kGroupA, count = groupA ? kGroupA : kGroupB;
            for (int v = first; v < first + count; ++v)
            {
                SeVoice &voice = voices[v];
                if (voice.sound == 0xFF || voice.owner != request.owner || voice.sound != request.sound)
                    continue;
                refreshVoice(voice, request, groupA);
                request.sound = 0xFF;
                return true;
            }
            if (request.param0A != 0)
                return false;
            for (int v = first; v < first + count; ++v)
            {
                SeVoice &voice = voices[v];
                if (voice.sound != 0xFF || (groupA && voice.rest != 0))
                    continue;
                const bool pitched = request.param0C != 0;
                startVoice(voice, request, groupA);
                voice.flags = groupA && pitched ? 3 : 1;
                return true;
            }
            int victim = -1;
            if (groupA)
                for (int v = 0; v < kGroupA && victim == -1; ++v)
                    if (voices[v].sound == 0xFF)
                        victim = v;
            if (victim == -1)
            {
                uint16_t worst = 0;
                for (int v = first; v < first + count; ++v)
                    if (worst < voices[v].priority)
                    {
                        victim = v;
                        worst = voices[v].priority;
                    }
            }
            if (victim < 0)
                return false;
            startVoice(voices[victim], request, groupA);
            voices[victim].flags = groupA ? 3 : 1;
            return true;
        }
    }

    // Each frame: gives the frame's requests (already in priority order) effect voices, then stops the voices
    // nobody asked for. A sound's owned voices count towards its limit; a request of a sound at its limit or
    // cooling down is skipped, else it starts the cooldown and tries each voice in the sound's list (below 200
    // in group A, else B), and counts once if any took. Unrequested voices run down their hold frames and are
    // then stopped, except owned ones: group A through mm_SoundCmd_PlayA(voice, 0, 0, 0x800, 0x800, 0), group
    // B through mm_SoundCmd_StopB; free group A voices run down their rest. On ambience phase 3 every held
    // voice takes its gathered volumes (group A also the pitch, whose change flag replaces the volume's). The
    // request list is emptied.
    void Se_AssignVoices()
    {
        const GuestPtr<SeVoice> voices = voiceSlots();
        const GuestPtr<SeSound> sounds{globals::kSoundTable};
        const GuestPtr<SeRequest> requests{globals::kSeRequests};
        for (int v = 0; v < kGroupA + kGroupB; ++v)
            voices[v].flags = 0;
        for (int s = 0; s < 83; ++s)
        {
            sounds[s].playing = 0;
            if (sounds[s].cooldown != 0)
                --sounds[s].cooldown;
        }
        for (int v = 0; v < kGroupA + kGroupB; ++v)
            if (voices[v].sound != 0xFF && (voices[v].voiceParam & 0x100u) != 0)
                ++sounds[voices[v].sound].playing;

        for (int i = 0; i < globals::seRequestCount.get(); ++i)
        {
            SeRequest &request = requests[i];
            if (request.sound == 0xFF)
                continue;
            SeSound &sound = sounds[request.sound];
            if (!(sound.playing < sound.maxVoices) || sound.cooldown != 0)
                continue;
            if (request.param18 != 0)
                sound.cooldown = request.param18;
            bool started = false;
            for (int k = 0; k < 4; ++k)
            {
                const uint8_t listed = sound.voices[2 * k];
                if (listed != 0xFF && assignVoice(request, listed < 200))
                    started = true;
            }
            if (started)
                ++sound.playing;
        }

        for (int v = 0; v < kGroupA; ++v)
        {
            SeVoice &voice = voices[v];
            if (voice.sound == 0xFF)
            {
                if (voice.rest != 0)
                    --voice.rest;
                continue;
            }
            if (voice.flags != 0)
                continue;
            if (voice.hold != 0)
            {
                --voice.hold;
                continue;
            }
            if ((voice.voiceParam & 0x100u) != 0)
                continue;
            SoundCmd_PlayA(static_cast<uint8_t>(v), 0, 0, 0x800, 0x800, 0);
            voice.sound = 0xFF;
        }
        for (int v = 0; v < kGroupB; ++v)
        {
            SeVoice &voice = voices[kGroupA + v];
            if (voice.sound == 0xFF || voice.flags != 0)
                continue;
            if (voice.hold != 0)
            {
                --voice.hold;
                continue;
            }
            if ((voice.voiceParam & 0x100u) != 0)
                continue;
            SoundCmd_StopB(static_cast<uint32_t>(v), 0);
            voice.sound = 0xFF;
        }

        if (globals::ambiencePhase.get() == 3)
            for (int v = 0; v < kGroupA + kGroupB; ++v)
            {
                SeVoice &voice = voices[v];
                if (voice.sound == 0xFF || voice.hold == 0)
                    continue;
                const bool groupA = v < kGroupA;
                if (voice.volumeLeft != voice.heldLeft || voice.volumeRight != voice.heldRight)
                    voice.flags = 1;
                if (groupA && voice.pitch != voice.heldPitch)
                    voice.flags = 2;
                voice.volumeLeft = voice.heldLeft;
                voice.volumeRight = voice.heldRight;
                if (groupA)
                {
                    voice.pitch = voice.heldPitch;
                    voice.heldPitch = 0;
                }
                voice.heldLeft = 0;
                voice.heldRight = 0;
                voice.flags |= 0x80;
            }
        globals::seRequestCount.set(0);
    }

    namespace
    {
        // A script step's opcode and argument (the argument array follows the eight opcodes).
        struct ScriptStep
        {
            uint8_t op;
            uint16_t arg;
        };

        ScriptStep scriptStep(const SeVoice &voice)
        {
            const uint32_t script = globals::kSeScripts + 0x18u * (voice.voiceParam & 0xFFu);
            return {port::load<uint8_t>(script + voice.scriptStep), port::load<uint16_t>(script + 8u + 2u * voice.scriptStep)};
        }

        // The note of a start step: the argument's low byte, raised at random by up to its high byte.
        uint8_t scriptNote(uint16_t arg)
        {
            uint32_t note = arg & 0xFFu;
            const uint32_t spread = static_cast<uint32_t>(arg >> 8);
            if (spread != 0)
                note += static_cast<uint32_t>(Random_TimerMix()) % (spread + 1);
            return static_cast<uint8_t>(note);
        }

        // A random branch step: on to the next step when a random byte beats the argument's low byte, else back to
        // the step in its high byte.
        void scriptBranch(SeVoice &voice, uint16_t arg)
        {
            if ((arg & 0xFFu) < (static_cast<uint32_t>(Random_TimerMix()) & 0xFFu))
                ++voice.scriptStep;
            else
                voice.scriptStep = static_cast<uint8_t>(arg >> 8);
        }

        // A wait step: loads the timer with the argument, then counts it down to the next step.
        void scriptWait(SeVoice &voice, uint16_t arg)
        {
            if (voice.scriptTimer == 0)
                voice.scriptTimer = arg;
            else if (--voice.scriptTimer == 0)
                ++voice.scriptStep;
        }

        // A non-positional request (group 0, angle and distance 0) with param0C as given; owned sounds take the
        // next owner id, as Sound_PlaySe does.
        void requestPlain(uint8_t sound, uint16_t param0C)
        {
            if ((port::load<uint8_t>(globals::kSoundTable + 0x10u * sound) & 0x10u) == 0)
            {
                Se_Request(0, 0, sound, 0, 0, param0C, 0x3C, 0, 0.0f, 0.0f);
                return;
            }
            const uint16_t owner = globals::soundNextOwner.get();
            globals::soundNextOwner.set(static_cast<uint16_t>(owner + 1));
            Se_Request(owner, 0, sound, 0, 0, param0C, 0x3C, 0, 0.0f, 0.0f);
            if (globals::soundNextOwner.get() == 0xFF)
                globals::soundNextOwner.set(0);
        }

        // Group B has one volume: the mean of left and right plus the offset, kept within 1..0x3FFF.
        void sendVolumeB(uint32_t index, const SeVoice &voice)
        {
            const int32_t sum = static_cast<int32_t>(static_cast<uint32_t>(voice.volumeLeft) + static_cast<uint32_t>(voice.volumeRight));
            const int32_t volume = sum / 2 + voice.volumeOffset;
            SoundCmd_0F(static_cast<uint8_t>(index), volume < 1 ? 1 : (volume >= 0x4000 ? 0x3FFF : volume), 0);
        }

        // Opcodes: 0 end (a kept sound is stopped), 1 start the note, 2 wait while sounding, 3 stop, 4 wait frames,
        // 5 random branch, 6/7 pitch slide down/up, 8 stop and go on, 9/10 volume offset +/- the argument. Group A
        // sends changed volume and pitch while the voice sounds.
        void stepVoiceA(uint32_t index, SeVoice &voice, bool sounding)
        {
            const ScriptStep step = scriptStep(voice);
            const uint8_t id = static_cast<uint8_t>(index);
            switch (step.op)
            {
            case 0:
                if ((voice.voiceParam & 0x100u) != 0)
                {
                    SoundCmd_PlayA(id, 0, 0, 0x800, 0x800, 0);
                    voice.sound = 0xFF;
                }
                return;
            case 1:
                voice.flags = 3;
                Se_ApplyVoiceParams(index);
                SoundCmd_PlayB(index, 0, 0, voice.voice, voice.param0B, scriptNote(step.arg), (voice.voiceParam & 0x100u) != 0 ? 1 : 0, 0);
                ++voice.scriptStep;
                return;
            case 2:
                if (sounding)
                    Se_ApplyVoiceParams(index);
                else if ((voice.voiceParam & 0x800u) == 0)
                    ++voice.scriptStep;
                return;
            case 3:
                SoundCmd_PlayA(id, 0, 0, 0x800, 0x800, 0);
                voice.sound = 0xFF;
                return;
            case 4:
                scriptWait(voice, step.arg);
                break;
            case 5:
                scriptBranch(voice, step.arg);
                break;
            case 6:
            case 7:
                // The timer's low byte counts the slide's frames, the high byte the frames done; each frame bends
                // the pitch by frames done times the argument's high byte.
                if (voice.scriptTimer == 0)
                    voice.scriptTimer = step.arg & 0xFFu;
                else
                {
                    voice.scriptTimer += 0x100;
                    const uint32_t bend = static_cast<uint32_t>(voice.scriptTimer >> 8) * static_cast<uint32_t>(step.arg >> 8);
                    SoundCmd_SetPitchA(id, static_cast<uint16_t>(step.op == 6 ? 0u - bend : bend), 0);
                    --voice.scriptTimer;
                    if ((voice.scriptTimer & 0xFFu) == 0)
                    {
                        voice.scriptTimer = 0;
                        ++voice.scriptStep;
                    }
                }
                break;
            case 8:
                SoundCmd_PlayA(id, 0, 0, 0x800, 0x800, 0);
                ++voice.scriptStep;
                return;
            case 9:
            case 10:
                voice.volumeOffset = static_cast<int16_t>(step.op == 9 ? step.arg : 0u - step.arg);
                [[fallthrough]];
            default:
                if (sounding)
                    Se_ApplyVoiceParams(index);
                ++voice.scriptStep;
                return;
            }
            if (sounding)
                Se_ApplyVoiceParams(index);
        }

        // Group B plays sequences: the same opcodes without pitch slides (6 and 7 just go on), and a start step's
        // sequence is bank voice + 0x38. Volume goes out while the voice sounds and its volume flag is set.
        void stepVoiceB(uint32_t index, SeVoice &voice, bool sounding)
        {
            const ScriptStep step = scriptStep(voice);
            const auto sendVolume = [&] {
                if (sounding && (voice.flags & 1u) != 0)
                    sendVolumeB(index, voice);
            };
            switch (step.op)
            {
            case 0:
                if ((voice.voiceParam & 0x100u) != 0)
                {
                    SoundCmd_StopB(index, 0);
                    voice.sound = 0xFF;
                }
                return;
            case 1:
                voice.flags = 1;
                sendVolumeB(index, voice);
                SoundCmd_PlaySequence(index, static_cast<uint8_t>(voice.voice + 0x38u), scriptNote(step.arg),
                                      (voice.voiceParam & 0x100u) != 0 ? 1 : 0, 0);
                ++voice.scriptStep;
                return;
            case 2:
                if (!sounding)
                    ++voice.scriptStep;
                else
                    sendVolume();
                return;
            case 3:
                SoundCmd_StopB(index, 0);
                voice.sound = 0xFF;
                return;
            case 4:
                scriptWait(voice, step.arg);
                sendVolume();
                return;
            case 5:
                scriptBranch(voice, step.arg);
                sendVolume();
                return;
            case 8:
                SoundCmd_StopB(index, 0);
                ++voice.scriptStep;
                return;
            case 9:
            case 10:
                voice.volumeOffset = static_cast<int16_t>(step.op == 9 ? step.arg : 0u - step.arg);
                [[fallthrough]];
            default:
                sendVolume();
                ++voice.scriptStep;
                return;
            }
        }
    }

    // Runs one step of every busy voice's sound script, by priority class (bits 8-11 of the priority, 0 first).
    // The driver's status says which voices still sound: word 2 for group A, word 1 for group B, a bit per voice.
    void Se_UpdateVoices()
    {
        const GuestPtr<uint32_t> status{static_cast<uint32_t>(Sound_GetStatus())};
        const GuestPtr<SeVoice> voices = voiceSlots();
        for (uint32_t priorityClass = 0; priorityClass < 15; ++priorityClass)
        {
            int32_t sounding = static_cast<int32_t>(status[2]);
            for (uint32_t v = 0; v < kGroupA; ++v, sounding >>= 1)
            {
                SeVoice &voice = voices[static_cast<int32_t>(v)];
                if (voice.sound != 0xFF && ((voice.priority >> 8) & 0xFu) == priorityClass)
                    stepVoiceA(v, voice, (sounding & 1) != 0);
            }
            sounding = static_cast<int32_t>(status[1]);
            for (uint32_t v = 0; v < kGroupB; ++v, sounding >>= 1)
            {
                SeVoice &voice = voices[static_cast<int32_t>(kGroupA + v)];
                if (voice.sound != 0xFF && ((voice.priority >> 8) & 0xFu) == priorityClass)
                    stepVoiceB(v, voice, (sounding & 1) != 0);
            }
        }
    }

    // Plays one of seven effects (footsteps and the like) at full volume; the first four get a random pitch of
    // 0..199 from the timer noise. Owned sounds take the next owner id, as Sound_PlaySe does.
    void Se_PlayVariant(uint8_t variant)
    {
        struct Variant
        {
            uint8_t sound;
            bool randomPitch;
        };
        static constexpr Variant kVariants[7] = {{0x29, true}, {0x2A, true}, {0x2B, true}, {0x2D, true},
                                                 {0x3E, false}, {0x2C, false}, {0x2E, false}};
        const int pitch = Random_TimerMix() % 200;
        if (variant >= 7)
            return;
        const Variant &v = kVariants[variant];
        requestPlain(v.sound, v.randomPitch ? static_cast<uint16_t>(static_cast<int16_t>(pitch)) : 0);
    }

    // Turns effects on or off (off stops every voice of both groups) and clears the effects fade; with
    // `resetVolumes`, group A voices go back to volume 0xFFF and group B to 0x100.
    void Se_SetEnabled(int enabled, int resetVolumes)
    {
        globals::seEnabled.set(enabled);
        globals::seFadeCounter.set(0);
        if (enabled == 0)
        {
            for (uint32_t voice = 0; voice < 16; ++voice)
                SoundCmd_StopA(voice, 0);
            for (uint32_t voice = 0; voice < 15; ++voice)
                SoundCmd_StopB(voice, 0);
        }
        if (resetVolumes == 0)
            return;
        for (uint32_t voice = 0; voice < 16; ++voice)
            SoundCmd_SetVolumeA(static_cast<uint8_t>(voice), 0xFFF, 0);
        for (uint32_t voice = 0; voice < 15; ++voice)
            SoundCmd_SetVolumeB(static_cast<uint8_t>(voice), 0x100, 0);
    }

    // Entering a city: city sounds start after 5 frames, music voice 15 at 0xC0, the ambience map and listener
    // are rebuilt, the music table gets tracks 0-2 plus the tracks set in the bag's ten shortcut entries (track,
    // enabled), and the mood is 3 when the city has flag 8. Leaving (no city): effects are stopped and re-enabled
    // at full volume, queued plays dropped, held voices freed and the music bank in slot 7 stopped. Both end with
    // the clock, fades and enables reset.
    void Sound_SetCity(GuestPtr<CityScene> city)
    {
        globals::soundCity.set(city);
        if (city.address == 0)
        {
            globals::ambienceEnabled.set(0);
            Se_SetEnabled(0, 0);
            Se_SetEnabled(1, 1);
            SoundCmd_CancelPlayB(0);
            const GuestPtr<SeVoice> voices = voiceSlots();
            for (int v = 0; v < kGroupA + kGroupB; ++v)
                if (voices[v].sound != 0xFF && voices[v].hold != 0)
                    voices[v].sound = 0xFF;
            if (globals::bgmBankLoaded.get() != 0)
            {
                globals::bgmBankLoaded.set(0);
                SoundCmd_StopSequence(7, 0);
            }
        }
        else
        {
            globals::soundDelay.set(5);
            globals::ambienceEnabled.set(1);
            globals::bgmPlaying.set(1);
            globals::bgmBankLoaded.set(0);
            globals::ambiencePhase.set(0);
            globals::bgmFadeCounter.set(0);
            SoundCmd_SetVolumeB(15, 0xC0, 0);
            Se_BuildAmbienceMap();
            Se_UpdateListener();
            globals::bgmTrack.set(0xFF);
            globals::bgmPlayedState.set(0xFF);
            globals::bgmState.set(0);
            for (uint32_t track = 0; track < 13; ++track)
            {
                port::store<uint8_t>(globals::kBgmTracks + 2u * track, static_cast<uint8_t>(track < 3 ? track : 0xFFu));
                port::store<uint8_t>(globals::kBgmTracks + 2u * track + 1u, 1);
            }
            const GuestPtr<Bag> bag = city->bag;
            for (int i = 0; i < 10; ++i)
            {
                const BagShortcut &entry = bag->shortcuts[i];
                if (static_cast<uint16_t>(entry.slot) != 0xFFFFu)
                    Bgm_SetTrackSlot(static_cast<char>(entry.slot & 0xFF), 0, static_cast<uint16_t>(entry.unk_02));
            }
            if (CityScene_HasFlag8(city) != 0)
                globals::bgmState.set(3);
        }
        globals::soundLastClock.set(-1);
        globals::clockStopped.set(0);
        globals::bgmFadeCounter.set(0);
        globals::seFadeCounter.set(0);
        globals::bgmPlaying.set(1);
        globals::seEnabled.set(1);
        globals::ambienceEnabled.set(1);
    }

    // The vehicles on a tile (people whose class has flag 1 and whose +0x28 is set), unless the tile's object
    // already has a request this frame (owner: object id, group 1): each sounds its engine (0x18) at a pitch of
    // 200 per 40 of speed, and while `horn` is still set, a one-in-four chance of the horn (0x1A) uses it up.
    // Returns what is left of `horn`.
    int Se_RequestTileSounds(int x, int z, int horn, ps2f angle, ps2f distance)
    {
        const GuestPtr<CityScene> city = globals::soundCity.get();
        const GuestPtr<MapObject> obj = Map_GetTileObject(city->map, x, z);
        const GuestPtr<SeRequest> requests{globals::kSeRequests};
        const int count = globals::seRequestCount.get();
        for (int i = 0; i < count; ++i)
            if (obj->id + 0x100000u == (requests[i].owner & 0xF0FFFFu))
                return horn;
        port::GuestLocal<uint16_t> cursor;
        *cursor = obj->firstPerson;
        while (*cursor != kNoPerson)
        {
            const GuestPtr<MapPerson> person = People_Next(globals::soundCity.get()->map, cursor);
            if (!person)
                continue;
            const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
            if ((cls->flags & 1u) == 0 || person->target == kNoPerson)
                continue;
            const uint16_t pitch = static_cast<uint16_t>(static_cast<int16_t>(person->introduces / 40) * 200);
            Se_Request(obj->id, 1, 0x18, 0, 0, pitch, 0x3C, 5, angle, distance);
            if (horn != 0 && (Random_TimerMix() & 3) == 0)
            {
                Se_Request(obj->id, 1, 0x1A, 0, 0, 0, 0x3C, 0, angle, distance);
                horn = 0;
            }
        }
        return horn;
    }

    // A building object's ambient sound this frame, as a state word: the low byte is the sound (0xFF none), 0x100
    // marks the part of its window after the lead, 0x200 an entrance object, 0x400 the type's own sound. Only
    // types with flag 0x40 have one, and each object counts once per frame (object flag 4; again gives 0x4FF).
    // Entrances play the type's sound, other objects of variant group 2 sound 6; the sound's schedule decides
    // whether it plays now (see SeSound::schedule). "Occupied" is open (the type's vtable +0x24) with people in.
    uint32_t Se_BuildingSoundState(GuestPtr<MapObject> obj, GuestPtr<BuildingType> btype)
    {
        if ((btype->flags & 0x40u) == 0 || obj->id == 0)
            return 0xFF;
        if ((obj->flags & 4u) != 0)
            return 0x4FF;
        obj->flags = static_cast<uint16_t>(obj->flags | 4u);
        uint32_t state = btype->ambientSound;
        if (state != 0xFF)
            state = (state + 0x400) & 0xFFFFu;
        uint32_t entrance = 0;
        if ((obj->flags & 0x20u) != 0)
        {
            state = (state + 0x200) & 0xFFFFu;
            entrance = 0x200;
        }
        else
            state = (obj->variant & 0xFFu) >> 3 == 2 ? 6 : 0xFF;
        const uint32_t sound = state & 0xFFu;
        if (sound == 0xFF)
            return state;

        const GuestPtr<CityScene> city = globals::soundCity.get();
        const int clock = globals::soundClock.get();
        const auto occupied = [&] {
            const uint32_t isOpen = port::load<uint32_t>(btype->vtable.address + 0x24u);
            return port::callGuest<int64_t>(isOpen, btype, city, obj) != 0 && obj->firstPerson != kNoPerson;
        };
        const uint32_t late = (state + 0x100) & 0xFFFFu;
        const auto onTheHour = [&] { return clock % 3600 < 601 ? state : late; };
        const auto inWindow = [&](GuestPtr<MapObject> staggerBy) {
            return (state | static_cast<uint32_t>(Se_InTimeWindow(staggerBy, 0x708, 300))) & 0xFFFFu;
        };
        switch (GuestPtr<SeSound>{globals::kSoundTable}[static_cast<int32_t>(sound)].schedule)
        {
        case 12:
            if (city->monthCount % 12 == 7 && clock >= 84600)
                return clock < 85200 ? state : late;
            break;
        case 11:
            if (occupied() && city->monthCount % 12 == 11 && clock >= 64800)
                return clock < 82800 ? state : late;
            break;
        case 10:
            if (occupied())
            {
                const int hour = clock / 3600;
                if (hour == 8 || hour == 12 || hour == 15)
                    return onTheHour();
            }
            break;
        case 5:
        {
            const int hour = clock / 3600;
            if ((hour == 7 || hour == 12 || hour == 15) && (city->weatherToday == 0 || city->weatherToday == 1))
                return onTheHour();
            break;
        }
        case 4:
            if (clock >= 25200 && clock < 68400)
                return inWindow(GuestPtr<MapObject>{0});
            break;
        case 3:
            if (clock >= 21600 && clock < 64800)
                return inWindow(obj);
            break;
        case 2:
            if (occupied())
                return inWindow(obj);
            break;
        case 1:
            if (occupied())
                return state;
            break;
        }
        return (static_cast<uint32_t>(static_cast<int16_t>(entrance)) + 0xFFu) & 0xFFFFu;
    }

    namespace
    {
        __m128 soundPosition(ps2f x, ps2f y, ps2f z)
        {
            return _mm_setr_ps(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), 1.0f);
        }

        struct Bearing
        {
            ps2f cosine;
            ps2f distance;
        };

        // A sound's bearing as the ambience works it out in VU0: the cosine between the listener's facing vector and
        // the unit vector from the listener position `from` to the sound, and the distance between them. Uses vf1,
        // vf2, vf4-vf9 and Q as the original.
        Bearing bearing(__m128 position, GuestPtr<Vec4> from, GuestPtr<Vec4> facing)
        {
            using namespace vu0;
            vf(4) = position;
            vf(5) = load(from);
            set(vf(4), sub(vf(4), vf(5)), XYZW);
            vf(1) = vf(4);
            set(vf(2), mul(vf(1), vf(1)), XYZW);
            set(vf(2), add(vf(2), lane(vf(2), 1)), X);
            set(vf(2), add(vf(2), lane(vf(2), 2)), X);
            q() = rsqrt(x(vf(2)));
            set(vf(2), mul(vf(1), _mm_set1_ps(q())), XYZ);
            vf(4) = vf(2);
            vf(5) = load(facing);
            set(vf(4), mul(vf(4), vf(5)), XYZ);
            set(vf(4), add(vf(4), lane(vf(4), 1)), X);
            set(vf(4), add(vf(4), lane(vf(4), 2)), X);
            const ps2f cosine = x(vf(4));
            vf(4) = load(from);
            vf(5) = position;
            set(vf(6), sub(vf(5), vf(4)), XYZW);
            set(vf(7), mul(vf(6), vf(6)), XYZ);
            set(vf(8), add(vf(7), lane(vf(7), 1)), X);
            set(vf(8), add(vf(8), lane(vf(7), 2)), X);
            q() = sqrtQ(x(vf(8)));
            set(vf(9), add(vf(0), _mm_set1_ps(q())), XYZW);
            return {cosine, x(vf(9))};
        }
    }

    // Asks for a sound by what makes it. `group` says how `object` is read: 1 a person (the sound is at the person), 2 a
    // building object (the type's own sound when `sound` is 0xFF; at the footprint's middle), 3 a map tile, 10 a plain
    // effect without a place (owned by a running counter that wraps at 0xFF), anything else a plain request too (group 0,
    // owner `object`). Group 10 and the others skip sound 0xFF; groups 1 to 3 do nothing while the owner already has a
    // request this frame (group in bits 20 to 23 of the owner key). Placed sounds get their bearing and distance from
    // the listener (VU0), the distance cut to 15.5 from 16 up when `clampFar` is set. Without a city nothing happens.
    void Se_RequestForObject(uint32_t object, uint32_t group, uint32_t sound, int pitch, int clampFar, int param18)
    {
        using namespace vu0;
        const GuestPtr<CityScene> city = globals::soundCity.get();
        if (!city)
            return;
        port::GuestFrame<0x100> frame;
        const GuestPtr<Vec4> listener{globals::kListener};
        const GuestPtr<SeRequest> requests{globals::kSeRequests};
        group &= 0xFF;
        const uint32_t key = (object & 0xFFFFu) + group * 0x100000u;
        const uint16_t owner = static_cast<uint16_t>(object);
        const auto alreadyRequested = [&] {
            const uint32_t count = globals::seRequestCount.get();
            for (uint32_t i = 0; i < count; i = (i + 1) & 0xFFFFu)
                if (key == (requests[static_cast<int32_t>(i)].owner & 0xF0FFFFu))
                    return true;
            return false;
        };
        const auto place = [&](uint32_t placedGroup, uint32_t placedSound, __m128 position) {
            const Bearing b = bearing(position, listener + 1, listener + 3);
            ps2f distance = b.distance;
            if (!(distance < ps2f(16.0f)) && clampFar != 0)
                distance = ps2f(15.5f);
            Se_Request(owner, placedGroup, static_cast<uint8_t>(placedSound), 0, static_cast<uint8_t>(param18),
                       static_cast<uint16_t>(pitch), 0x3C, 0, b.cosine, distance);
        };
        const ps2f centre = 64.0f;
        if (group == 10)
        {
            if ((sound & 0xFFu) != 0xFF)
            {
                const uint16_t next = globals::soundNextOwner.get();
                globals::soundNextOwner.set(static_cast<uint16_t>(next + 1));
                Se_Request(next, 0, static_cast<uint8_t>(sound), 0, static_cast<uint8_t>(param18), static_cast<uint16_t>(pitch),
                           0x3C, 0, 0.0f, 0.0f);
                if (globals::soundNextOwner.get() == 0xFF)
                    globals::soundNextOwner.set(0);
            }
        }
        else if (group == 3)
        {
            if (alreadyRequested())
                return;
            const uint32_t tile = object & 0xFFFFu;
            const ps2f x = ps2f(0.5f) + (fromInt(static_cast<int32_t>(tile & 0x7Fu)) - centre);
            const ps2f z = (fromInt(128 - static_cast<int32_t>(tile >> 7)) - centre) - ps2f(0.5f);
            const ps2f y = Map_GetTileHeightWord(city->map, static_cast<int>(tile));
            place(3, sound, soundPosition(x, y, z));
        }
        else if (group == 2)
        {
            const GuestPtr<MapObject> obj = Map_GetRecord(city->map, static_cast<int>(object & 0xFFFFu));
            const GuestPtr<BuildingType> btype{port::load<uint32_t>(globals::buildingTypeTable.get() + 4u * obj->type)};
            if ((sound & 0xFFu) == 0xFF)
                sound = btype->ambientSound;
            if ((sound & 0xFFu) == 0xFF || alreadyRequested())
                return;
            const ps2f size = fromInt(btype->size);
            const ps2f x = (fromInt(obj->tile & 0x7F) - centre) + size / ps2f(2.0f);
            const ps2f z = (fromInt(128 - (obj->tile >> 7)) - centre) - size / ps2f(2.0f);
            const ps2f y = Map_GetTileHeightWord(city->map, obj->tile);
            place(2, sound, soundPosition(x, y, z));
        }
        else if (group == 1)
        {
            if (alreadyRequested())
                return;
            const GuestPtr<MapPerson> person = city->map->people + static_cast<int>(object & 0xFFFFu);
            place(1, sound, soundPosition(person->x, person->y, person->z));
        }
        else if ((sound & 0xFFu) != 0xFF)
            Se_Request(owner, 0, static_cast<uint8_t>(sound), 0, static_cast<uint8_t>(param18), static_cast<uint16_t>(pitch),
                       0x3C, 0, 0.0f, 0.0f);
    }

    // The city ambience: each frame one quarter of the 2 x 2 blocks within 16 tiles of the listener (`advance`: the
    // next quarter; 0: the quarter from the clock; phase 4 would be all of them). For each block:
    //  - the building object's ambient sound (mm_Se_BuildingSoundState) from its footprint's middle, group 2;
    //  - the terrain sound (mm_Se_PickTerrainAmbience) for what stands there: 1 outside the map, else the kind
    //    table by the object's +0x14, then 2 for types 2-4, 3 for type flag 0x80, 5 for flag 0x40; group 3;
    //  - within 8 tiles, with the listener on the ground, the vehicles on its four tiles. The horn is allowed once,
    //    when the clock is at the month's five-minute slot of the hour or half an hour after it.
    // Then the objects' once-per-frame marks are cleared, and at 17:00 a city of 100 people or more with five
    // entrances sounding in the quarter rings sound 0x3B. The sound clock is the clock in steps of 12 s (120 s
    // before 6:00).
    void Se_UpdateCityAmbience(int advance)
    {
        const GuestPtr<CityScene> city = globals::soundCity.get();
        if (!city)
            return;
        port::GuestFrame<0x1B0> frame;
        if (advance != 0)
            globals::ambiencePhase.set((globals::ambiencePhase.get() + 1) & 3);
        const int clock = city->clock;
        if (clock < 21600)
        {
            globals::soundClock.set(clock / 120 * 120);
            if (advance == 0)
                globals::ambiencePhase.set(clock % 120 / 30);
        }
        else
        {
            globals::soundClock.set(clock / 12 * 12);
            if (advance == 0)
                globals::ambiencePhase.set(clock % 12 / 3);
        }
        if (globals::ambiencePhase.get() == 0)
            Se_UpdateListener();

        // Block offsets from the listener's tile, in steps of 2.
        int x0, x1, z0, z1;
        switch (globals::ambiencePhase.get())
        {
        case 0: x0 = -16, x1 = 0, z0 = -16, z1 = 0; break;
        case 1: x0 = 2, x1 = 16, z0 = -16, z1 = 0; break;
        case 2: x0 = -16, x1 = 0, z0 = 2, z1 = 16; break;
        case 3: x0 = 2, x1 = 16, z0 = 2, z1 = 16; break;
        case 4: x0 = -16, x1 = 16, z0 = -16, z1 = 16; break;
        default: // never; the original then reads leftover words of its frame
        {
            const uint32_t sp = frame.ptr().address;
            x0 = port::load<int32_t>(sp + 0xC0u), x1 = port::load<int32_t>(sp + 0xD0u);
            z0 = port::load<int32_t>(sp + 0xE0u), z1 = port::load<int32_t>(sp + 0xF0u);
            break;
        }
        }

        const int second = city->clock % 3600;
        const int slot = city->monthCount % 12 * 300;
        int horn = 0;
        if (second == slot)
            horn = 1;
        else
        {
            int later = slot + 1800;
            if (later >= 3600)
                later -= 3600;
            if (second == later)
                horn = 1;
        }

        const GuestPtr<Vec4> listener{globals::kListener};
        const ps2f half = 0.5f, centre = 64.0f;
        uint16_t entrances = 0;
        for (int dz = z0; dz <= z1; dz += 2)
            for (int dx = x0; dx <= x1; dx += 2)
            {
                const int tileX = globals::listenerTileX.get() + dx;
                const int tileZ = globals::listenerTileZ.get() + dz;
                const int blockX = tileX / 2 * 2, blockZ = tileZ / 2 * 2;
                const GuestPtr<MapObject> obj = Map_GetTileObject(city->map, blockX, blockZ);
                const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(obj->type)};

                const uint32_t state = Se_BuildingSoundState(obj, btype) & 0xFFFFu;
                if ((state & 0x200u) != 0)
                    ++entrances;
                if ((state & 0xFFu) != 0xFF)
                {
                    const ps2f size = fromInt(btype->size);
                    const ps2f x = (fromInt(obj->tile & 0x7F) - centre) + size / ps2f(2.0f);
                    const ps2f z = (fromInt(128 - (obj->tile >> 7)) - centre) - size / ps2f(2.0f);
                    const ps2f y = Map_GetTileHeightWord(city->map, obj->tile);
                    const Bearing b = bearing(soundPosition(x, y, z), listener + 1, listener + 3);
                    Se_Request(obj->id, 2, static_cast<uint8_t>(state), (state & 0x100u) != 0 ? 1 : 0, 0, 0, 0x3C, 5,
                               b.cosine, b.distance - size / ps2f(2.0f));
                }

                int kind = 0;
                if (tileX < 0 || tileX >= 128 || tileZ < 0 || tileZ >= 128)
                    kind = 1;
                else
                {
                    const uint8_t shape = static_cast<uint8_t>(obj->entranceTile);
                    if (shape < 10)
                        kind = port::load<uint8_t>(globals::kAmbienceKinds + shape);
                    if (kind == 0)
                    {
                        if (obj->type == 2 || obj->type == 3 || obj->type == 4)
                            kind = 2;
                        if ((btype->flags & 0x80u) != 0)
                            kind = 3;
                        if ((btype->flags & 0x40u) != 0)
                            kind = 5;
                    }
                }
                if (kind != 0)
                {
                    const int cx = std::clamp(blockX, 0, 127), cz = std::clamp(blockZ, 0, 127);
                    const uint32_t terrain = Se_PickTerrainAmbience(cx, cz, kind) & 0xFFFFu;
                    if (terrain != 0xFF)
                    {
                        const ps2f x = half + (fromInt(cx) - centre);
                        const ps2f z = (fromInt(128 - cz) - centre) - half;
                        const ps2f y = Map_GetHeightAt(city->map, cx, cz);
                        const __m128 position = soundPosition(x, y, z);
                        const Bearing b = terrain == 0x1C || terrain == 0x1D ? bearing(position, listener, listener + 2)
                                                                             : bearing(position, listener + 1, listener + 3);
                        Se_Request(static_cast<uint16_t>(cx / 2 + (cz / 2 << 6)), 3, static_cast<uint8_t>(terrain), 0, 0, 0,
                                   0x3C, 5, b.cosine, b.distance);
                    }
                }

                if (dz < -8 || dz > 8 || dx < -8 || dx > 8 || globals::listenerLevel.get() != 0)
                    continue;
                const ps2f x = half + (fromInt(blockX) - centre);
                const ps2f z = (fromInt(128 - blockZ) - centre) - half;
                const ps2f y = Map_GetHeightAt(city->map, blockX, blockZ);
                const Bearing b = bearing(soundPosition(x, y, z), listener, listener + 2);
                const ps2f distance = b.distance * ps2f(2.0f);
                horn = Se_RequestTileSounds(blockX, blockZ, horn, b.cosine, distance);
                horn = Se_RequestTileSounds(blockX + 1, blockZ, horn, b.cosine, distance);
                horn = Se_RequestTileSounds(blockX, blockZ + 1, horn, b.cosine, distance);
                horn = Se_RequestTileSounds(blockX + 1, blockZ + 1, horn, b.cosine, distance);
            }

        for (int dz = z0; dz <= z1; dz += 2)
            for (int dx = x0; dx <= x1; dx += 2)
            {
                const int blockX = (globals::listenerTileX.get() + dx) / 2 * 2;
                const int blockZ = (globals::listenerTileZ.get() + dz) / 2 * 2;
                const GuestPtr<MapObject> obj = Map_GetTileObject(city->map, blockX, blockZ);
                obj->flags = static_cast<uint16_t>(obj->flags & 0xFFFBu);
            }

        if (entrances >= 5 && city->map->population >= 100 && globals::soundClock.get() == 61200)
            requestPlain(0x3B, 0);
    }

    // Rain (weather 2): sound 0x36 at a distance that falls from 16 between 2:00 and 6:00, is 0 by day,
    // rises from 0 between 18:00 and 22:00 and is out of range (100) otherwise; in months 7 and 8 also
    // sound 0x37 at 13:00 and 15:00, shifted by the month count's minutes.
    void Se_UpdateTimeOfDay()
    {
        if (globals::soundCity.get()->weatherToday != 2)
            return;
        const int clock = globals::soundClock.get();
        ps2f distance = 100.0f;
        if (clock >= 7200)
        {
            if (clock < 21600)
                distance = ps2f(16.0f) - fromInt((clock - 7200) / 650);
            else if (clock < 64800)
                distance = 0.0f;
            else if (clock < 79200)
                distance = fromInt((clock - 64800) / 650);
        }
        if (distance <= ps2f(16.0f))
            Se_Request(0, 0, 0x36, 0, 0, 0, 0x3C, 5, 0.0f, distance);
        const int months = globals::soundCity.get()->monthCount;
        const int month = months % 12 + 1;
        if (month < 7 || month >= 9)
            return;
        const int offset = (months % 60) * 60;
        if (clock == offset + 46800 || clock == offset + 54000)
            Se_Request(0, 0, 0x37, 0, 0, 0, 0x3C, 0, 0.0f, 0.0f);
    }

    // The ambience sound for a map position (0xFF: none). Ground above the water level (city +0xB0) gets
    // 0x1C, or 0x1D on tiles without flag 0x20. Lower ground looks up its 2x2 block in the ambience grid: the
    // cell's sound must exist and match `variant` (1-based, 0 = 0), and the sound's schedule must hold now
    // for today's weather (hours per weather in its table entry), with the start inside the hour staggered
    // by the cell. A sound past the lead part of its window comes back with 0x100 added.
    uint32_t Se_PickTerrainAmbience(int x, int z, int variant)
    {
        const GuestPtr<CityScene> city = globals::soundCity.get();
        if (!city)
            return 0xFF;
        const ps2f height = Map_GetHeightAt(globals::soundCity.get()->map, x, z);
        if (globals::soundCity.get()->waterLevel < height)
            return Map_TileFlag20(globals::soundCity.get()->map, x, z) != 0 ? 0x1C : 0x1D;
        const uint32_t wanted = variant != 0 ? static_cast<uint32_t>(variant - 1) & 0xFFu : 0u;
        const int cell = x / 2 + (z / 2) * 64;
        const uint32_t gridCell = globals::kAmbienceGrid + 2u * static_cast<uint32_t>(cell);
        const uint8_t sound = port::load<uint8_t>(gridCell);
        if (sound == 0 || wanted != port::load<uint8_t>(gridCell + 1u))
            return 0xFF;
        const int weather = globals::soundCity.get()->weatherToday;
        const uint32_t weatherIndex = weather == 1 || weather == 2 || weather == 3 ? static_cast<uint32_t>(weather) : 0u;
        const uint32_t entry = globals::kAmbienceCounts + 0x1Cu * ((sound - 1u) & 0xFFu);
        const uint8_t hourFrom = port::load<uint8_t>(entry + 8u + 2u * weatherIndex);
        if (hourFrom == 0xFF)
            return 0xFF;
        const int match = Se_MatchSchedule(port::load<uint8_t>(entry + 1u), port::load<uint8_t>(entry + 2u), hourFrom,
                                           port::load<uint8_t>(entry + 9u + 2u * weatherIndex), port::load<uint8_t>(entry + 3u),
                                           static_cast<uint32_t>(cell % 6) & 0xFFu, port::load<uint8_t>(entry + 4u));
        if (match == 0)
            return 0xFF;
        const uint32_t id = port::load<uint8_t>(entry);
        return match == 2 ? (id + 0x100u) & 0xFFFFu : id;
    }

    // Whether a scheduled sound plays now: within the months monthFrom..monthTo (0 = any), the hours
    // hourFrom..hourTo (wrapping past midnight) and, inside the hour, a window starting at start10 * 10
    // minutes for `length` minutes (0 = the whole hour range). Returns 0 outside, 2 inside the window but
    // past the first `lead` minutes of it, else 1 (lead 0: always 1 inside). Every value is a byte, as in
    // the original. A month range that wraps past December never excludes anything there (the test can
    // never be true), also as in the original.
    int Se_MatchSchedule(uint32_t monthFrom, uint32_t monthTo, uint32_t hourFrom, uint32_t hourTo, uint32_t length,
                         uint32_t start10, int lead)
    {
        const int month = (static_cast<int8_t>(globals::soundCity.get()->monthCount % 12) + 1) & 0xFF;
        if (monthFrom != 0)
        {
            const int from = monthFrom & 0xFF, to = monthTo & 0xFF;
            if (to < from)
            {
                if (from < month && month < to)
                    return 0;
            }
            else if (month < from || to < month)
                return 0;
        }

        const int hFrom = hourFrom & 0xFF, hTo = hourTo & 0xFF;
        const int clock = globals::soundClock.get();
        const int hour = (clock / 3600) & 0xFF;
        const int minute = ((clock / 60) % 60) & 0xFF;
        if (hTo < hFrom)
        {
            if (hTo < hour && hour < hFrom)
                return 0;
            if (static_cast<int>(monthFrom & 0xFF) == month && hour < hFrom)
                return 0;
            if (static_cast<int>(monthTo & 0xFF) == month && hTo < hour)
                return 0;
        }
        else if (hTo < hour || hour < hFrom)
            return 0;

        const int start = static_cast<int>(start10 & 0xFF) * 10 & 0xFF;
        const int len = length & 0xFF;
        const int end = (start + len) & 0xFF;
        const int leadEnd = (start + static_cast<int8_t>(lead)) & 0xFF;
        // Inside the window: 1 during the lead, 2 after it.
        const auto inWindow = [&]
        {
            if (leadEnd < 60)
            {
                if (lead == 0)
                    return 1;
                return start <= minute && minute < leadEnd ? 1 : 2;
            }
            if (lead == 0 || minute < ((leadEnd - 60) & 0xFF))
                return 1;
            return start <= minute ? 1 : 2;
        };
        if (len == 0)
        {
            if (hour == hFrom && minute < start)
                return 0;
            if (hour == hTo && end <= minute)
                return 0;
            return inWindow();
        }
        if (end < 60)
        {
            if (minute < start || end <= minute)
                return 0;
            if (lead == 0)
                return 1;
            return minute < leadEnd ? 1 : 2;
        }
        const int wrappedEnd = (end - 60) & 0xFF;
        if (minute < start && wrappedEnd <= minute)
            return 0;
        if (hour == hFrom && minute < start)
            return 0;
        if (hour == hTo && wrappedEnd <= minute)
            return 0;
        return inWindow();
    }

    // The listener for positional effects, from the city camera. [0] is the eye moved to the centre of its
    // tile and [1] the eye as it is; both get the height in steps of 6 (eye height / 12, truncated, times 6).
    // [2] and [3] are the directions to the target, the target moved like [0] and at [0]'s height for [2], as
    // it is at [1]'s height for [3]; each is normalized, turned by building matrix 3 (whose translation is
    // cleared first) and normalized again. Keeps the eye's tile (unclamped) and height step; returns the step.
    int Se_UpdateListener()
    {
        using namespace vu0;
        const GuestPtr<CityCamera> camera = globals::soundCity.get()->cityCamera;
        const GuestPtr<Vec4> listener{globals::kListener};
        port::GuestLocal<Vec4> rawTarget;
        port::GuestLocal<Vec4> target;
        listener[0] = camera->base.eye;
        *target = camera->base.target;
        listener[1] = camera->base.eye;
        *rawTarget = camera->base.target;

        const ps2f centre = 64.0f, half = 0.5f, step = 12.0f;
        const auto clampTile = [](int tile) { return tile < 0 ? 0 : tile >= 128 ? 127 : tile; };
        const auto toFloat = [](int value) { return ps2f(static_cast<float>(value)); };
        globals::listenerTileX.set(sdk::fptosi(centre + listener[0].x));
        globals::listenerTileZ.set(sdk::fptosi(centre - listener[0].z));
        const int targetX = sdk::fptosi(centre + target->x);
        const int targetZ = sdk::fptosi(centre - target->z);
        listener[0].x = half + (toFloat(clampTile(globals::listenerTileX.get())) - centre);
        listener[0].z = (toFloat(128 - clampTile(globals::listenerTileZ.get())) - centre) - half;
        target->x = half + (toFloat(clampTile(targetX)) - centre);
        target->z = (toFloat(128 - clampTile(targetZ)) - centre) - half;

        listener[0].y = listener[0].y / step;
        globals::listenerLevel.set(sdk::fptosi(listener[0].y));
        listener[1].y = listener[1].y / step;
        listener[0].y = toFloat(globals::listenerLevel.get() * 6);
        const int level = sdk::fptosi(listener[1].y);
        const GuestPtr<Vec4> turn{globals::kBuildingMatrices + 3u * 0x40u};
        turn[3].x = 0.0f;
        turn[3].y = 0.0f;
        turn[3].z = 0.0f;
        globals::listenerLevel.set(level);
        target->y = listener[0].y;
        listener[1].y = toFloat(level * 6);
        rawTarget->y = listener[1].y;

        vf(4) = load(target.ptr());
        vf(5) = load(listener);
        set(vf(4), sub(vf(4), vf(5)), XYZW);
        store(listener + 2, vf(4));
        vf(4) = load(rawTarget.ptr());
        vf(5) = load(listener + 1);
        set(vf(4), sub(vf(4), vf(5)), XYZW);
        store(listener + 3, vf(4));
        Vec3_Normalize(listener + 2, listener + 2);
        Vec3_Normalize(listener + 3, listener + 3);
        Math_MulMatVecVu0(listener + 2, turn, listener + 2);
        Math_MulMatVecVu0(listener + 3, turn, listener + 3);
        Vec3_Normalize(listener + 2, listener + 2);
        Vec3_Normalize(listener + 3, listener + 3);
        return level;
    }

    PORT_FN(Se_UpdateVoices);
    PORT_FN(Se_SetEnabled);
    PORT_FN(Sound_SetCity);
    PORT_FN(Se_RequestTileSounds);
    PORT_FN(Se_RequestForObject);
    PORT_FN(Se_BuildingSoundState);
    PORT_FN(Se_UpdateCityAmbience);
    PORT_FN(Se_PlayVariant);
    PORT_FN(Se_UpdateListener);
    PORT_FN(Se_MatchSchedule);
    PORT_FN(Se_PickTerrainAmbience);
    PORT_FN(Se_UpdateTimeOfDay);
    PORT_FN(Sound_GetStatus);
    PORT_FN(Sound_IsBusy);
    PORT_FN(Bgm_Update);
    PORT_FN(Se_Request);
    PORT_FN(Se_AssignVoices);
    PORT_FN(Sound_PlaySe);
    PORT_FN(Bgm_SetTrackSlot);
    PORT_FN(Sound_LoadPlaylandBanks);
    PORT_FN(Sound_ResetVoices);
    PORT_FN(Se_ApplyVoiceParams);
    PORT_FN(Sound_Update);
    PORT_FN(Sound_InitState);
    PORT_FN(Se_BuildAmbienceMap);
    PORT_FN(Se_InTimeWindow);

    // ---- batch 38

    // Stops the music (sequence voice 15) fading over `time`; the mood becomes "fading out".
    void Bgm_FadeOut(int time)
    {
        SoundCmd_FadeB(15, 0, static_cast<uint16_t>(time), 0);
        globals::bgmState.set(4);
    }

    // On: the loaded city bank is dropped (voice 15 and sequence slot 7 stopped), the music plays again at
    // volume 0xC0 (mood 3). Off: a fade over 8 (mood 4). The result is the last command's.
    int Bgm_SetActive(int active)
    {
        if (active == 0)
        {
            const int result = SoundCmd_FadeB(15, 0, 8, 0);
            globals::bgmState.set(4);
            return result;
        }
        if (globals::bgmBankLoaded.get() != 0)
        {
            globals::bgmBankLoaded.set(0);
            SoundCmd_StopB(15, 0);
            SoundCmd_StopSequence(7, 0);
        }
        globals::bgmPlaying.set(1);
        globals::bgmFadeCounter.set(0);
        const int result = SoundCmd_SetVolumeB(15, 0xC0, 0);
        globals::bgmState.set(3);
        return result;
    }

    // Voice 15 (the music) on or off; a volume of 0xC0 when `volume` is set. The result is v0 as the last command
    // left it (unchanged when none ran).
    int Se_SetVoice15Enabled(int enabled, int volume)
    {
        int32_t result = port::guestV0();
        globals::bgmPlaying.set(enabled);
        globals::bgmFadeCounter.set(0);
        if (enabled == 0)
            result = SoundCmd_StopB(15, 0);
        if (volume != 0)
            result = SoundCmd_SetVolumeB(15, 0xC0, 0);
        return result;
    }

    // Stops everything an owner plays: its effect voices 0..15 are silenced (play command with no sound), voices
    // 16..30 stopped, and its pending requests dropped (0xFF frees them).
    void Se_StopOwner(char owner)
    {
        const uint8_t id = static_cast<uint8_t>(owner);
        if (id == 0xFF)
            return;
        for (uint32_t voice = 0; voice < 16; ++voice)
        {
            const uint32_t slot = globals::kSeVoices + 0x30u * voice;
            if (port::load<uint8_t>(slot) != id)
                continue;
            SoundCmd_PlayA(static_cast<uint8_t>(voice), 0, 0, 0x800, 0x800, 0);
            port::store<uint8_t>(slot, 0xFF);
        }
        for (uint32_t voice = 0; voice < 15; ++voice)
        {
            const uint32_t slot = globals::kSeVoices + 0x300u + 0x30u * voice;
            if (port::load<uint8_t>(slot) != id)
                continue;
            SoundCmd_StopB(voice & 0xFFu, 0);
            port::store<uint8_t>(slot, 0xFF);
        }
        const int count = globals::seRequestCount.get();
        for (int i = 0; i < count; ++i)
        {
            const uint32_t request = globals::kSeRequests + 0x1Cu * static_cast<uint32_t>(i);
            if (port::load<uint8_t>(request) == id)
                port::store<uint8_t>(request, 0xFF);
        }
    }

    // The driver status as the game sees it (the received block with the queued commands merged in); busy while its
    // word +0xC is set.
    int Sound_IsDriverBusy()
    {
        sdk::memcpy(globals::kSoundStatusView, globals::kSoundStatus, 0x40);
        port::store<uint32_t>(globals::kSoundStatusView + 4u, port::load<uint32_t>(globals::kSoundStatus + 4u) | globals::soundSlotsQueued.get());
        port::store<uint32_t>(globals::kSoundStatusView + 8u, port::load<uint32_t>(globals::kSoundStatus + 8u) | globals::soundVoicesQueued.get());
        port::store<uint32_t>(globals::kSoundStatusView + 0x18u, port::load<uint32_t>(globals::kSoundStatus + 0x18u) | globals::soundStopsQueued.get());
        return port::load<uint32_t>(globals::kSoundStatusView + 0xCu) != 0 ? 1 : 0;
    }

    // Stops every playing voice: the B voices of status word +4, then the A voices of word +8.
    void Sound_StopAllPlaying()
    {
        const uint32_t status = static_cast<uint32_t>(Sound_GetStatus());
        for (uint32_t voice = 0; voice < 16; ++voice)
            if ((port::load<uint32_t>(status + 4u) & (1u << voice)) != 0)
                SoundCmd_StopB(voice & 0xFFu, 0);
        for (uint32_t voice = 0; voice < 16; ++voice)
            if ((port::load<uint32_t>(status + 8u) & (1u << voice)) != 0)
                SoundCmd_StopA(voice & 0xFFu, 0);
    }

    // Stops sequence slots 7 down to 0; the result is the last command's.
    int Sound_StopAllSequences()
    {
        int result = 0;
        for (int slot = 7; slot >= 0; --slot)
            result = SoundCmd_StopSequence(static_cast<uint32_t>(slot) & 0xFFu, 0);
        return result;
    }

    PORT_FN(Bgm_FadeOut);
    PORT_FN(Bgm_SetActive);
    PORT_FN(Se_SetVoice15Enabled);
    PORT_FN(Se_StopOwner);
    PORT_FN(Sound_IsDriverBusy);
    PORT_FN(Sound_StopAllPlaying);
    PORT_FN(Sound_StopAllSequences);

    // ---- message sounds (batch 39)

    // The sound for a character typed in a message: the code picks a voice (0x3F for 0x14, 0xA, 8, 6 and 1 to 4, else
    // 0x13) and its pitch (+300 for 0x14, -300 for 0xA and 9, -100 for 8 and 7, +100 for 1 to 4, else 0). A voice owned
    // by its caller (table flag 0x10) takes the next owner id (counted up, back to 0 at 0xFF); otherwise it has none.
    void Se_PlayMessageSound(char code)
    {
        uint8_t sound = 0x13;
        uint16_t pitch = 0;
        switch (code)
        {
        case 0x14:
            sound = 0x3F;
            pitch = 300;
            break;
        case 0x0A:
            sound = 0x3F;
            pitch = 0xFED4;
            break;
        case 0x09:
            pitch = 0xFED4;
            break;
        case 0x08:
            sound = 0x3F;
            pitch = 0xFF9C;
            break;
        case 0x07:
            pitch = 0xFF9C;
            break;
        case 0x06:
            sound = 0x3F;
            break;
        case 0x04:
        case 0x03:
        case 0x02:
        case 0x01:
            sound = 0x3F;
            pitch = 100;
            break;
        default:
            break;
        }
        if ((port::load<uint8_t>(globals::kSoundTable + 0x10u * sound) & 0x10u) == 0)
        {
            Se_Request(0, 0, sound, 0, 0, pitch, 0x3C, 0, 0.0f, 0.0f);
            return;
        }
        const uint16_t owner = globals::soundNextOwner.get();
        globals::soundNextOwner.set(static_cast<uint16_t>(owner + 1));
        Se_Request(owner, 0, sound, 0, 0, pitch, 0x3C, 0, 0.0f, 0.0f);
        if (globals::soundNextOwner.get() == 0xFF)
            globals::soundNextOwner.set(0);
    }

    PORT_FN(Se_PlayMessageSound);

    // Flushes the data cache and tells the IOP sound driver to shut down (an RPC the call waits on).
    void Sound_Shutdown()
    {
        sdk::FlushCache(0);
        sdk::sceSifCallRpc(0x001FE690u, 0, 0, 0x0020B6C0u, 0x100, 0x0020B800u, 0x40, 0u, 0u);
    }

    PORT_FN(Sound_Shutdown);

    // Loads the six IOP modules the sound driver needs, in order, repeating each load (with a log line) until it succeeds.
    void Sound_LoadModules()
    {
        for (const uint32_t path : {0x001B9EC0u, 0x001B9F00u, 0x001B9F40u, 0x001B9F80u, 0x001B9FC0u, 0x001BA000u})
            while (sdk::sceSifLoadModule(path, 0, 0) < 0)
                sdk::printf(path + 0x20u);
    }

    PORT_FN(Sound_LoadModules);

    // Brings the sound driver up: the IOP modules are loaded, the RPC layer started, and the client at 0x1FE690 bound to the
    // driver's server (0x12346) until the server has registered (a pause of 10000 empty turns after each bind; a refused bind prints
    // 0x1B9EA0 and hangs). The status block address 0x20B7C0 is stored at 0x20B6C0 and sent with function 1 (the answer comes back
    // in 0x20B800), then the answer word, the command queue counters and masks, and the first word of each of the 256 voice
    // records (0xD0 bytes from 0x1FE6C0) are cleared.
    void Sound_Init()
    {
        Sound_LoadModules();
        sdk::sceSifInitRpc(0);
        do
        {
            if (sdk::sceSifBindRpc(0x001FE690u, 0x12346, 0) < 0)
            {
                sdk::printf(0x001B9EA0u);
                for (volatile int spin = 0;;)
                    spin = spin + 1;
            }
            port::accountCycles(0x2711u * 32u); // the original's 10000 empty turns: the IOP runs meanwhile and registers the server
        } while (port::load<uint32_t>(0x001FE6B4u) == 0u);
        port::store<uint32_t>(0x0020B6C0u, 0x0020B7C0u);
        sdk::FlushCache(0);
        sdk::sceSifCallRpc(0x001FE690u, 1, 0, 0x0020B6C0u, 0x100, 0x0020B800u, 0x40, 0u, 0u);
        globals::soundCmdIndex.set(0);
        globals::soundCmdSent.set(0);
        globals::soundVoicesQueued.set(0);
        globals::soundSlotsQueued.set(0);
        globals::soundStopsQueued.set(0);
        port::store<uint32_t>(0x0020B800u, 0u);
        for (uint32_t voice = 0; voice < 0x100; ++voice)
            port::store<uint32_t>(0x001FE6C0u + 0xD0u * voice, 0u);
    }

    PORT_FN(Sound_Init);
}
