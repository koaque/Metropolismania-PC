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

    PORT_FN(Sound_Update);
    PORT_FN(Se_InTimeWindow);
}
