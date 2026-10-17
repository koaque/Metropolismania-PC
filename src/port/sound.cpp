// Sound front end: the per-frame update of effects, music fades and city ambience.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

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
        port::store<int32_t>(0x001BDD38u, 0);
        port::store<int32_t>(0x001BDD3Cu, 0);
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
        if (globals::soundCity.get()->unk_B0 < height)
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

    PORT_FN(Se_MatchSchedule);
    PORT_FN(Se_PickTerrainAmbience);
    PORT_FN(Se_UpdateTimeOfDay);
    PORT_FN(Sound_GetStatus);
    PORT_FN(Sound_IsBusy);
    PORT_FN(Bgm_Update);
    PORT_FN(Se_Request);
    PORT_FN(Sound_PlaySe);
    PORT_FN(Bgm_SetTrackSlot);
    PORT_FN(Sound_LoadPlaylandBanks);
    PORT_FN(Sound_ResetVoices);
    PORT_FN(Se_ApplyVoiceParams);
    PORT_FN(Sound_Update);
    PORT_FN(Sound_InitState);
    PORT_FN(Se_BuildAmbienceMap);
    PORT_FN(Se_InTimeWindow);
}
