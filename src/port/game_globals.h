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

    // sound: commands for the YYSOUND driver, queued in a ring of 256 entries (0xD0 bytes) and sent by
    // mm_SoundCmd_Flush. Each entry: +0 command, +4 state, +8 and +0xC words, arguments from +0x10.
    inline constexpr uint32_t kSoundCmds = 0x001FE6C0u;
    inline constexpr GuestVar<int32_t> soundCmdIndex{0x001BDD44u};   // next entry to write
    inline constexpr GuestVar<uint32_t> soundVoicesQueued{0x001BDD4Cu}; // bit per voice with a queued play (cmd 3)
    inline constexpr GuestVar<uint32_t> soundSlotsQueued{0x001BDD50u};  // bit per sequence slot with a queued play (cmd 0xC)

    // pad: button words as the game uses them (the raw pad word byte-swapped, analog sticks folded in).
    inline constexpr GuestVar<uint32_t> padHeld{0x001BDCA0u};
    inline constexpr GuestVar<uint32_t> padPressed{0x001BDCA4u};     // held now, not last frame
    inline constexpr GuestVar<int32_t> padFocusOwner{0x001BDCB0u};   // 0 = nobody (mm_Pad_TakeFocus)
    inline constexpr GuestVar<uint32_t> padFocusMask{0x001BDCB4u};   // buttons other owners do not see
    inline constexpr uint32_t kPadPortStates = 0x001BDB08u;          // int32_t per port, 100 = ready (gp-0x7E68)
    inline constexpr uint32_t kPadBuffers = 0x001F3AC0u;             // 0x20 bytes per port

    // family: one member byte per (family * 6 + member): friendship meter in bits 0-6, friend flag in bit 7.
    inline constexpr uint32_t kMemberBytes = 0x001C59B0u;

    // font, claims: data loaded from DATA\FONT1222.GF and DATA\BIN\CLAIM.BIN.
    inline constexpr GuestVar<uint32_t> fontData{0x001BDC98u};       // 0x90 bytes per glyph
    inline constexpr GuestVar<uint32_t> claimData{0x001BDA50u};      // starts with the two claim meter thresholds (u16)
    inline constexpr GuestVar<uint32_t> claimSectionC{0x001BDA58u};  // offset table (words) into claimData

    // gfx: the two 64 KB DMA lists, which one is current and where it is written, and the packet buffer.
    inline constexpr uint32_t kDmaLists = 0x001BD9C0u;               // two list addresses
    inline constexpr GuestVar<uint32_t> dmaListIndex{0x001BDCECu};
    inline constexpr GuestVar<uint32_t> dmaListPtr{0x001BDCE0u};     // write position in the current list
    inline constexpr GuestVar<int32_t> packetBufPos{0x001BDCE8u};

    // save: memory card request slots (0x24 bytes each, 64 of them); the result word is at +0xC. Requests
    // are queued at the tail; the head moves as they complete.
    inline constexpr uint32_t kMcRequests = 0x001F5CF0u;
    inline constexpr GuestVar<int32_t> mcQueueTail{0x001BDCD4u};
    inline constexpr GuestVar<int32_t> mcQueueHead{0x001BDCD8u};

    // file: the disc request queue, same scheme.
    inline constexpr GuestVar<int32_t> fileQueueTail{0x001BDC88u};
    inline constexpr GuestVar<int32_t> fileQueueHead{0x001BDC8Cu};

    // pad: auto-repeat (mm_Pad_GetRepeat).
    inline constexpr GuestVar<int32_t> padRepeatTimer{0x001BDCC4u};
    inline constexpr GuestVar<uint32_t> padRepeat{0x001BDCC8u};

    // city: the building type objects, one per type id (STRTYPE.BIN), and the schedule data.
    inline constexpr GuestVar<uint32_t> buildingTypeTable{0x001BDBF8u}; // array of BuildingType pointers
    inline constexpr GuestVar<uint32_t> scheduleData{0x001BDBACu};      // 0x60-byte entries

    // Guest address of the building type object for a type id.
    inline uint32_t buildingTypeAddress(uint32_t type) { return port::load<uint32_t>(buildingTypeTable.get() + 4u * type); }
    // Its flags (+0x136), read through guest addresses like the original, whatever the table holds.
    inline uint16_t buildingTypeFlags(uint32_t type) { return port::load<uint16_t>(buildingTypeAddress(type) + 0x136u); }

    // claim icons: colour set per icon id (bytes) and the colour table (Vec4, two levels per set).
    inline constexpr GuestVar<uint32_t> claimIconSets{0x001BDA64u};
    inline constexpr GuestVar<uint32_t> claimIconColours{0x001BDA70u};
    inline constexpr uint32_t kClaimIconNoColour = 0x001B26C0u;          // Vec4 for level 0

    // family: thresholds that map family ids to building type indexes (sorted u16 table and its length).
    inline constexpr GuestVar<uint32_t> familyTypeTable{0x001BDA8Cu};
    inline constexpr GuestVar<int32_t> familyTypeCount{0x001BDA90u};

    // char: the character class objects, one per CHARTYPE.BIN record (pointer table).
    inline constexpr GuestVar<uint32_t> charClassTable{0x001BDA3Cu};

    // family: the 256 member status entries (MemberStatus, 4 bytes each).
    inline constexpr uint32_t kMemberStatus = 0x001F32F0u;

    // laptop: the active entries (one word each) and their count.
    inline constexpr uint32_t kLaptopEntries = 0x001F36F0u;
    inline constexpr GuestVar<int32_t> laptopEntryCount{0x001BDAB8u};

    // sound: the city clock as the sound code keeps it (seconds of the day).
    inline constexpr GuestVar<int32_t> soundClock{0x001BDD34u};

    // char: 25 recently used character type slots (6000 = free) and their timers.
    inline constexpr uint32_t kCharTypeSlots = 0x001C5110u;
    inline constexpr uint32_t kCharTypeTimers = 0x001C5310u;

    // stage: the stage state saved with the city (0x284 bytes); it starts with the city's name (saves show
    // "Forest Hills", "Rhinecliff" ... there), then the mayor's name at +0x10.
    inline constexpr uint32_t kStageState = 0x001C5380u;
}
