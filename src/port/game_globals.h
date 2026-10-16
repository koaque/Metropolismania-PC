// Addresses of the game's global data used by ported code (SLUS_203.77, see AGENTS.md rule 4).
// Grouped by subsystem; docs/map/<subsystem>.md describes each one.

#pragma once

#include "game_types.h"

#include <cstdint>

namespace mm::globals
{
    // family: three bitsets with one bit per family id (0x4E4 bytes each).
    inline constexpr uint32_t kFamiliesInCity = 0x001D4410u; // A: the family lives in the city
    inline constexpr uint32_t kFamiliesKnown = 0x001D4900u;  // B: known through the Town's Pages
    inline constexpr uint32_t kFamiliesLeft = 0x001D4DF0u;   // C: the family moved away

    // random: Knuth's subtractive generator. Entries 1..55 of an int64_t table (entry 0 unused) and the
    // index of the last value handed out (gp-0x7CD4).
    inline constexpr uint32_t kRandomTable = 0x001F55D0u;
    inline constexpr uint32_t kRandomNext = 0x001BDC9Cu;

    // sound: front-end state (gp-relative, gp = 0x1C5970).
    inline constexpr GuestVar<uint8_t> bgmState{0x001BDCF4u};        // 4 = fading out (mm_Bgm_FadeOut)
    inline constexpr GuestVar<GuestPtr<CityScene>> soundCity{0x001BDD00u}; // set by mm_Sound_SetCity
    inline constexpr GuestVar<int32_t> soundDelay{0x001BDD04u};      // frames before city sounds start
    inline constexpr GuestVar<int32_t> soundLastClock{0x001BDD0Cu};  // city clock seen last frame
    inline constexpr GuestVar<int32_t> clockStopped{0x001BDD10u};    // 1 while the clock stands still: BGM at 0x80
    inline constexpr GuestVar<int8_t> bgmFadeCounter{0x001BDD14u};   // counts down by 2 per frame while fading
    inline constexpr GuestVar<int8_t> seFadeCounter{0x001BDD18u};    // effects fade, same pace
    inline constexpr GuestVar<int32_t> bgmPlaying{0x001BDD1Cu};      // cleared when the fade-out stops voice 15
    inline constexpr GuestVar<int32_t> seEnabled{0x001BDD20u};       // mm_Se_SetEnabled
    inline constexpr GuestVar<int32_t> ambienceEnabled{0x001BDD24u};
}
