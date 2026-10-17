// Addresses of the game's global data used by ported code (SLUS_203.77, see AGENTS.md rule 4).
// Grouped by subsystem; docs/map/<subsystem>.md describes each one.

#pragma once

#include "game_types.h"

#include <cstdint>

namespace mm
{
    // Index sentinels: no person (MapPerson), no object (MapObject; also "outside" and "none" in object fields).
    inline constexpr uint16_t kNoPerson = 6000;
    inline constexpr uint16_t kNoObject = 0x3F80;
}

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
    inline constexpr GuestVar<int32_t> soundCmdSent{0x001BDD48u};    // entries before it have been sent (mm_SoundCmd_Flush)
    inline constexpr GuestVar<uint32_t> soundVoicesQueued{0x001BDD4Cu}; // bit per voice with a queued play (cmd 3)
    inline constexpr GuestVar<uint32_t> soundSlotsQueued{0x001BDD50u};  // bit per sequence slot with a queued play (cmd 0xC)
    inline constexpr GuestVar<uint32_t> soundStopsQueued{0x001BDD54u};  // a third mask of queued commands (mm_Sound_GetStatus)
    inline constexpr GuestVar<uint16_t> soundNextOwner{0x001BDCFCu};    // owner id for the next owned effect, wraps at 0xFF
    inline constexpr uint32_t kSoundTable = 0x001B2EE0u;                // 0x10 bytes per sound id; flag 0x10 = owned
    inline constexpr uint32_t kSoundStatus = 0x0020B7C0u;               // driver status block (0x40 bytes) as received
    inline constexpr uint32_t kSoundStatusView = 0x0020B840u;           // the same with the queued commands merged in

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

    // core: frames since start (counted by mm_MainLoop); blinking effects use it.
    inline constexpr GuestVar<int32_t> frameCounter{0x001BDCACu};

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

    // file: the disc request queue, same scheme (0x1C-byte slots, 64 of them, state word first).
    inline constexpr uint32_t kFileRequests = 0x001F4ED0u;
    inline constexpr GuestVar<int32_t> fileQueueTail{0x001BDC88u};
    inline constexpr GuestVar<int32_t> fileQueueHead{0x001BDC8Cu};
    inline constexpr GuestVar<int32_t> fileQueueCount{0x001BDC90u}; // requests queued so far

    // pad: set by mm_Pad_ClearBit40; mm_Pad_Update keeps Cross (0x40) out of the held buttons until it is released.
    inline constexpr GuestVar<int32_t> padSwallowCross{0x001BDCC0u};

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
    inline constexpr uint32_t kDefaultColour = 0x001B26C0u;              // Vec4: untinted instances
    inline constexpr uint32_t kClaimIconNoColour = kDefaultColour;       // Vec4 for level 0

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
    inline GuestPtr<StageState> stageState() { return GuestPtr<StageState>{kStageState}; }

    // family: counters bumped with the bitsets (provisional names): families moved in (read by
    // mm_WordsFn_FamilyCountTier), the same count for the event code, and families that left.
    inline constexpr GuestVar<int32_t> familiesMovedIn{0x001F32E0u};
    inline constexpr GuestVar<int32_t> familiesMovedInEvent{0x001F32E4u};
    inline constexpr GuestVar<int32_t> familiesLeftEvent{0x001F32E8u};
    inline constexpr uint32_t kFamilyTableA = 0x001F26E0u; // 256 words, cleared by mm_Family_ResetState
    inline constexpr uint32_t kFamilyTableB = 0x001F2AE0u; // 256 words

    // save: per card port (two words each): card type (2 = PS2 memory card), free space, formatted, the
    // type at the last check, and the last result (-1, -2: no card).
    inline constexpr uint32_t kCardType = 0x001BDB60u;
    inline constexpr uint32_t kCardFree = 0x001BDB68u;
    inline constexpr uint32_t kCardFormatted = 0x001BDB70u;
    inline constexpr uint32_t kCardTypeChecked = 0x001BDB78u;
    inline constexpr uint32_t kCardResult = 0x001BDB88u;
    // save: the 16 save slots (8 per card): size (-4 = unknown), name (0x44 bytes) and header (0x40 bytes).
    inline constexpr uint32_t kSaveSlotSizes = 0x001F4780u;
    inline constexpr uint32_t kSaveSlotNames = 0x001F4340u;
    inline constexpr uint32_t kSaveSlotHeaders = 0x001F3F00u;

    // sound: the 31 effect voices (0x30 bytes, first byte 0xFF = free), and the ambience grid: 4096 cells of
    // (sound + 1, variant), filled from the per-sound counts at 0x1B35F0 (11 rows of 0x1C bytes, six counts
    // from +0x10).
    inline constexpr uint32_t kSeVoices = 0x001F8140u;
    inline constexpr uint32_t kAmbienceGrid = 0x001FC690u;
    inline constexpr uint32_t kAmbienceCounts = 0x001B35F0u;

    // gfx: model instance slots for this frame: the two buffers (by frame), which one is current, how many are
    // used (0x708 at most), and a second count with its own limit for mm_Instance_DrawNodeLimited.
    inline constexpr uint32_t kInstanceBuffers = 0x001BDA20u;
    inline constexpr GuestVar<int32_t> instanceCount{0x001BDA28u};
    inline constexpr GuestVar<int32_t> instanceBuffer{0x001BDA30u};
    inline constexpr GuestVar<int32_t> instanceLimitedCount{0x001BDA34u};
    inline constexpr GuestVar<int32_t> instanceLimit{0x001BD980u};
    inline constexpr GuestVar<float> frameLoad{0x001BD9B8u};          // last frame's load in percent (provisional)

    // city: building drawing. Matrices the draw slots fill in (one 4x4 per layer, translation in the last
    // row), the models holding the road and building meshes and the wasteland meshes, and the steps to the
    // four neighbouring 2x2 blocks (north, east, south, west).
    inline constexpr uint32_t kBuildingMatrices = 0x001B2590u;
    inline constexpr GuestVar<GuestPtr<A3DObject>> cityPartsModel{0x001BDBD0u};
    inline constexpr GuestVar<GuestPtr<A3DObject>> wastelandModel{0x001BDBCCu};
    inline constexpr GuestVar<GuestPtr<A3DObject>> entranceModel{0x001BDBC4u};   // business entrances
    inline constexpr GuestVar<GuestPtr<A3DObject>> blockModel{0x001BDBC8u};      // office and housing block parts
    inline constexpr GuestVar<GuestPtr<A3DObject>> parkRideModel{0x001BDBE0u};   // theme park ride (animated)
    inline constexpr uint32_t kBlockStepX = 0x001B13F0u; // int[4]: 0, 2, 0, -2
    inline constexpr uint32_t kBlockStepZ = 0x001B1400u; // int[4]: -2, 0, 2, 0
    // Housing blocks: mesh offset of each of the four block parts, per unit shape (int8[11][4]).
    inline constexpr uint32_t kHousingPartMesh = 0x001B14F0u;
    // The eight tiles around a tile and the tile itself, as x and z steps (int[9] each).
    inline constexpr uint32_t kNeighbourStepX = 0x001AEFF0u;
    inline constexpr uint32_t kNeighbourStepZ = 0x001AF020u;

    // gfx: start of the packet buffer (mm_DmaList_BeginScene rewinds to it).
    inline constexpr GuestVar<uint32_t> packetBufBase{0x001BDCE4u};

    // ui: the window model shared by the pause menu and phone calls, and where the pause window starts its
    // slide (closed) and ends it (open), as Vec4 positions.
    inline constexpr GuestVar<GuestPtr<A3DObject>> menuWindowModel{0x001F3B08u};
    inline constexpr uint32_t kPauseWindowOpen = 0x001B0910u;
    inline constexpr uint32_t kPauseWindowClosed = 0x001B0920u;
    // ui: the status panel alert's 15 colours (a pulse), moved along one entry per frame.
    inline constexpr uint32_t kAlertColours = 0x001AF370u;

    // title: the logo letters' size steps (uint16[3][8]: 0 hidden, 1 small, 2 medium, 3 full) and their five
    // places (uint16 x, y; 0x14 bytes per letter).
    inline constexpr uint32_t kLogoLetterSteps = 0x001B1820u;
    inline constexpr uint32_t kLogoLetterPlaces = 0x001B17E0u;

    // char: a person's walking step in its own frame, (0, 0, -speed, 1); mm_Person_UpdatePose sets z.
    inline constexpr uint32_t kWalkStep = 0x001AF0B0u;

    // gfx: light colours at the start of each 2-hour segment of the day (12 RGB float triples per table; a
    // second set 0x90 bytes on for bad weather).
    inline constexpr uint32_t kLightColoursA = 0x001B0550u;
    inline constexpr uint32_t kLightColoursB = 0x001B0670u;

    // gfx: the water wave animation's function statics, each a float and its "initialised" byte 4 bytes on:
    // the phase (-pi/2 .. pi/2), the sway direction (+1 / -1) and the two scroll offsets.
    inline constexpr uint32_t kWavePhase = 0x001BDAD8u;
    inline constexpr uint32_t kWaveDirection = 0x001BDAE0u;
    inline constexpr uint32_t kWaveScrollA = 0x001BDAE8u;
    inline constexpr uint32_t kWaveScrollB = 0x001BDAF0u;

    // event: the event types loaded from EVENT.BIN (96 records of 0x4C bytes; priority is the signed byte at +0x22).
    inline constexpr GuestVar<uint32_t> eventTypes{0x001BDA84u};

    // gfx: an identity matrix (four Vec4).
    inline constexpr uint32_t kIdentityMatrix = 0x001B2550u;

    // char: model parts loaded from CHAR.A3D (pointer table, indexed by CHARTYPE.BIN record bytes).
    inline constexpr uint32_t kCharParts = 0x001F3B90u;

    // laptop: the default entry count (4 the first time) and its "set" flag; the entry template.
    inline constexpr GuestVar<int32_t> laptopDefaultCount{0x001BDABCu};
    inline constexpr GuestVar<int8_t> laptopDefaultSet{0x001BDAC0u};
    inline constexpr uint32_t kLaptopTemplate = 0x001AF640u;
}
