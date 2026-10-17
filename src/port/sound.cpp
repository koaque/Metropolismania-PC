// Sound front end: the per-frame update of effects, music fades and city ambience.

#include "game_functions.h"
#include "game_globals.h"

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
        port::store<uint16_t>(0x001BDD08u, 0);
        port::store<uint16_t>(0x001BDCFCu, 0);
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
        port::store<uint16_t>(0x001BDD08u, 0);
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
        constexpr uint32_t kTable = 0x001F8120u;
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

    PORT_FN(Bgm_SetTrackSlot);
    PORT_FN(Sound_LoadPlaylandBanks);
    PORT_FN(Sound_ResetVoices);
    PORT_FN(Se_ApplyVoiceParams);
    PORT_FN(Sound_Update);
    PORT_FN(Sound_InitState);
    PORT_FN(Se_BuildAmbienceMap);
    PORT_FN(Se_InTimeWindow);
}
