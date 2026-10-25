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
    // Five words that mm_Random_TimerMix runs as a cascade (each adds the one before it), with timer 0 mixed
    // into the first.
    inline constexpr uint32_t kTimerMix = 0x001B2520u;

    // sound: front-end state (gp-relative, gp = 0x1C5970).
    inline constexpr GuestVar<uint8_t> bgmTrack{0x001BDCF0u};        // last city track played (index into kBgmTracks)
    inline constexpr GuestVar<uint8_t> bgmState{0x001BDCF4u};        // mood: 0 normal, 1 city flag 4, 2 approval below 0, 3; 4 = fading out (mm_Bgm_FadeOut)
    inline constexpr GuestVar<uint8_t> bgmPlayedState{0x001BDCF8u};  // mood the playing music was chosen for
    inline constexpr GuestVar<int32_t> ambiencePhase{0x001BDD38u};   // 0..3: the city ambience's four-frame cycle; on 3 held voices take their loudest volumes
    inline constexpr GuestVar<int32_t> bgmStarted{0x001BDD3Cu};      // a sequence was started; cleared when the driver reports it over
    inline constexpr GuestVar<int32_t> bgmBankLoaded{0x001BDD40u};   // the music bank in slot 7 is loaded
    inline constexpr GuestVar<GuestPtr<CityScene>> soundCity{0x001BDD00u}; // set by mm_Sound_SetCity
    inline constexpr GuestVar<int32_t> soundDelay{0x001BDD04u};      // frames before city sounds start
    inline constexpr GuestVar<int32_t> soundLastClock{0x001BDD0Cu};  // city clock seen last frame
    inline constexpr GuestVar<int32_t> clockStopped{0x001BDD10u};    // 1 while the clock stands still: BGM at 0x80
    inline constexpr GuestVar<int8_t> bgmFadeCounter{0x001BDD14u};   // counts down by 2 per frame while fading
    inline constexpr GuestVar<int8_t> seFadeCounter{0x001BDD18u};    // effects fade, same pace
    inline constexpr GuestVar<int32_t> bgmPlaying{0x001BDD1Cu};      // cleared when the fade-out stops voice 15
    inline constexpr GuestVar<int32_t> seEnabled{0x001BDD20u};       // mm_Se_SetEnabled
    inline constexpr GuestVar<int32_t> ambienceEnabled{0x001BDD24u};
    // sound: the listener (mm_Se_UpdateListener): the eye's tile (unclamped) and height step, and four vectors:
    // the eye at its tile centre, the eye, the direction to the target from each (turned by building matrix 3).
    inline constexpr GuestVar<int32_t> listenerTileX{0x001BDD28u};
    inline constexpr GuestVar<int32_t> listenerLevel{0x001BDD2Cu};
    inline constexpr GuestVar<int32_t> listenerTileZ{0x001BDD30u};
    inline constexpr uint32_t kListener = 0x001FC650u;

    // sound: commands for the YYSOUND driver, queued in a ring of 256 entries (0xD0 bytes) and sent by
    // mm_SoundCmd_Flush. Each entry: +0 command, +4 state, +8 and +0xC words, arguments from +0x10.
    inline constexpr uint32_t kSoundCmds = 0x001FE6C0u;
    inline constexpr GuestVar<int32_t> soundCmdIndex{0x001BDD44u};   // next entry to write
    inline constexpr GuestVar<int32_t> soundCmdSent{0x001BDD48u};    // entries before it have been sent (mm_SoundCmd_Flush)
    inline constexpr GuestVar<uint32_t> soundVoicesQueued{0x001BDD4Cu}; // bit per voice with a queued play (cmd 3)
    inline constexpr GuestVar<uint32_t> soundSlotsQueued{0x001BDD50u};  // bit per sequence slot with a queued play (cmd 0xC)
    inline constexpr GuestVar<uint32_t> soundStopsQueued{0x001BDD54u};  // a third mask of queued commands (mm_Sound_GetStatus)
    inline constexpr GuestVar<uint16_t> soundNextOwner{0x001BDCFCu};    // owner id for the next owned effect, wraps at 0xFF
    // sound: the city music: 13 tracks of (bank, enabled), the bank file names (SQ, HD, BD, 0x10 bytes each,
    // 0x30 per bank) and each bank's tempo word.
    inline constexpr uint32_t kBgmTracks = 0x001F8120u;
    inline constexpr uint32_t kBgmBanks = 0x001B2A10u;
    inline constexpr uint32_t kBgmTempos = 0x001B2DD0u;
    inline constexpr uint32_t kSoundTable = 0x001B2EE0u;                // 0x10 bytes per sound id; flag 0x10 = owned
    inline constexpr uint32_t kSoundStatus = 0x0020B7C0u;               // driver status block (0x40 bytes) as received
    inline constexpr uint32_t kSoundStatusView = 0x0020B840u;           // the same with the queued commands merged in

    // pad: button words as the game uses them (the raw pad word byte-swapped, analog sticks folded in).
    inline constexpr GuestVar<uint32_t> padHeld{0x001BDCA0u};
    inline constexpr GuestVar<uint32_t> padPressed{0x001BDCA4u};     // held now, not last frame
    inline constexpr GuestVar<uint32_t> padHeldBefore{0x001BDCA8u};   // the held buttons of the previous frame
    inline constexpr GuestVar<uint32_t> padData{0x001BDCCCu};        // pad buffer of port 0 read this frame (0 = none)
    inline constexpr GuestVar<int32_t> padFocusOwner{0x001BDCB0u};   // 0 = nobody (mm_Pad_TakeFocus)
    inline constexpr GuestVar<uint32_t> padFocusMask{0x001BDCB4u};   // buttons other owners do not see
    inline constexpr uint32_t kPadPortStates = 0x001BDB08u;          // int32_t per port, 100 = ready (gp-0x7E68)
    inline constexpr uint32_t kPadBuffers = 0x001F3AC0u;             // 0x20 bytes per port

    // family: one member byte per (family * 6 + member): friendship meter in bits 0-6, friend flag in bit 7.
    inline constexpr uint32_t kMemberBytes = 0x001C59B0u;

    // font, claims: data loaded from DATA\FONT1222.GF and DATA\BIN\CLAIM.BIN.
    inline constexpr GuestVar<uint32_t> fontData{0x001BDC98u};       // 0x90 bytes per glyph; the first is the blank
    inline constexpr uint32_t kWordDelimiters = 0x001B9AE0u;         // characters that end a word when text wraps
    inline constexpr GuestVar<uint32_t> claimData{0x001BDA50u};      // starts with the two claim meter thresholds (u16)
    // mm_Wants_Prune: frames since its last pass (one every 121) and whether that count was started.
    inline constexpr GuestVar<int32_t> wantsPruneTimer{0x001BDAF8u};
    inline constexpr GuestVar<int8_t> wantsPruneStarted{0x001BDAFCu};
    inline GuestPtr<ClaimThresholds> claimThresholds() { return GuestPtr<ClaimThresholds>{claimData.get()}; }
    inline constexpr GuestVar<uint32_t> claimSectionC{0x001BDA58u};  // offset table (words) into claimData

    // core: frames since start (counted by mm_MainLoop); blinking effects use it.
    inline constexpr GuestVar<int32_t> frameCounter{0x001BDCACu};
    // Timer 0's count as mm_MainLoop reads it after the game update and after the GS sync that follows it (the load, frameLoad, is
    // the second count times 100 over 9765.625).
    inline constexpr GuestVar<uint32_t> timerAfterUpdate{0x001BDCB8u};
    inline constexpr GuestVar<uint32_t> timerAfterSync{0x001BDCBCu};

    // gfx: the two 64 KB DMA lists, which one is current and where it is written, and the packet buffer.
    inline constexpr uint32_t kDmaLists = 0x001BD9C0u;               // two list addresses
    inline constexpr uint32_t kFramePictures = 0x001F69C0u;          // two packets drawing a 640 x 224 picture (mm_DmaList_InitPackets)
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
    inline constexpr GuestVar<int32_t> fileQueueIoctl{0x001BDC94u}; // where sceIoctl puts the pending-request count

    // Objects created at start-up and the data files loaded whole (the Create and Load functions).
    inline constexpr GuestVar<uint32_t> bagObject{0x001BDA18u};         // the player's bag
    inline constexpr GuestVar<uint32_t> staminaObject{0x001BDBB8u};     // 16 bytes: 0, the limit (399999), 0
    inline constexpr GuestVar<int32_t> buildingTypeInstances{0x001BDBBCu}; // building type objects alive; the shared block goes with the last
    inline constexpr GuestVar<uint32_t> buildingTypeShared{0x001BDBC0u};
    inline constexpr GuestVar<uint32_t> goodsData{0x001BDA98u};         // DATA\BIN\GOODS.BIN, then these pointers into it
    inline constexpr GuestVar<uint32_t> goodsSectionA{0x001BDA9Cu};
    inline constexpr GuestVar<uint32_t> goodsSectionB{0x001BDAA0u};
    inline constexpr GuestVar<uint32_t> goodsSectionC{0x001BDAA4u};
    inline constexpr GuestVar<uint32_t> goodsSectionD{0x001BDAA8u};
    inline constexpr GuestVar<uint32_t> goodsSectionE{0x001BDAACu};
    inline constexpr GuestVar<int32_t> goodsTextCount{0x001BDAB0u};
    inline constexpr GuestVar<uint32_t> claimSection54{0x001BDA54u};     // pointers into claimData from its offset words (+8 ...)
    inline constexpr GuestVar<uint32_t> claimSection5C{0x001BDA5Cu};
    inline constexpr GuestVar<uint32_t> claimSection74{0x001BDA74u};

    // pad: set by mm_Pad_ClearBit40; mm_Pad_Update keeps Cross (0x40) out of the held buttons until it is released.
    inline constexpr GuestVar<int32_t> padSwallowCross{0x001BDCC0u};

    // pad: auto-repeat (mm_Pad_GetRepeat).
    inline constexpr GuestVar<int32_t> padRepeatTimer{0x001BDCC4u};
    inline constexpr GuestVar<uint32_t> padRepeat{0x001BDCC8u};

    // city: the building type objects, one per type id (STRTYPE.BIN), and the schedule data.
    inline constexpr GuestVar<uint32_t> buildingTypeTable{0x001BDBF8u}; // array of BuildingType pointers
    inline constexpr GuestVar<int32_t> buildingTypeCount{0x001BDBFCu};
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

    // char: the introduction calls: the tick count the next call waits for (0 = none; one more after each try),
    // the ticks counted and the calls made (four at most).
    inline constexpr GuestVar<int32_t> introCallDue{0x001BDA44u};
    inline constexpr GuestVar<int32_t> introCallTick{0x001BDA48u};
    inline constexpr GuestVar<int32_t> introCallCount{0x001BDA4Cu};

    // char: the character class objects, one per CHARTYPE.BIN record (pointer table).
    inline constexpr GuestVar<uint32_t> charClassTable{0x001BDA3Cu};

    // family: the 256 member status entries (MemberStatus, 4 bytes each).
    inline constexpr uint32_t kMemberStatus = 0x001F32F0u;

    // laptop: the active entries (one word each) and their count.
    inline constexpr uint32_t kLaptopEntries = 0x001F36F0u;
    inline constexpr GuestVar<int32_t> laptopEntryCount{0x001BDAB8u};
    inline constexpr uint32_t kLaptopLift = 0x001AF680u; // (1.86, 0.45, -7.8, 0): the selected entry's offset (mm_Laptop_Animate)

    // sound: the city clock as the sound code keeps it (seconds of the day).
    inline constexpr GuestVar<int32_t> soundClock{0x001BDD34u};

    // char: 25 slots of people recently turned to face the player (person index, 6000 = free), their timers and
    // the direction each faces (Vec4; mm_Person_TurnToPlayer).
    inline constexpr uint32_t kCharTypeSlots = 0x001C5110u;
    inline constexpr uint32_t kCharTypeTimers = 0x001C5310u;
    inline constexpr uint32_t kCharTypeFacings = 0x001C5180u;

    // stage: the stage state saved with the city (0x284 bytes); it starts with the city's name (saves show
    // "Forest Hills", "Rhinecliff" ... there), then the mayor's name at +0x10.
    inline constexpr uint32_t kStageState = 0x001C5380u;
    inline GuestPtr<StageState> stageState() { return GuestPtr<StageState>{kStageState}; }
    inline constexpr GuestVar<int16_t> talkScriptVar{0x001C5602u};  // stage state +0x282: set and tested by talk scripts
    inline constexpr GuestVar<uint16_t> stageFlags{0x001C5600u};    // stage state +0x280: bit 2 lets families apply (mm_WordsFn_CanApply)

    // bag: the slots of each bag size (bytes 0, 4, 10, 20 for sizes 0 to 3).
    inline constexpr uint32_t kBagSlotsBySize = 0x001B5B08u;

    // family: counters bumped with the bitsets (provisional names): families moved in (read by
    // mm_WordsFn_FamilyCountTier), the same count for the event code, and families that left.
    inline constexpr GuestVar<int32_t> familiesMovedIn{0x001F32E0u};
    inline constexpr GuestVar<int32_t> familiesMovedInEvent{0x001F32E4u};
    inline constexpr GuestVar<int32_t> familiesLeftEvent{0x001F32E8u};
    inline constexpr uint32_t kFamilyTableA = 0x001F26E0u; // 256 words, cleared by mm_Family_ResetState
    inline constexpr uint32_t kFamilyTableB = 0x001F2AE0u; // 256 words
    // The family record cache (mm_Family_Get, mm_Family_Prefetch): 256 slots; for each slot the family number
    // (kFamilyTableA), the frame it was last used (kFamilyTableB) and the file queue request still reading it or -1 (kFamilyTableC).
    inline constexpr uint32_t kFamilyTableC = 0x001F2EE0u;
    inline constexpr uint32_t kFamilyRecords = 0x001D52E0u;  // 0x1D4 bytes each; the halfword at +0x20 is 0 while it loads
    inline constexpr GuestVar<uint32_t> familyFile{0x001BDA94u};        // the GameFile of FAMILY.BIN (asynchronous)
    // The MYHOME model files loaded a step per call (mm_CityAssets_LoadMyHome): step, file index, loads finished, the file,
    // the request that closes it, reads started; the blocks, their sizes and the three paths (0x20 bytes each).
    inline constexpr GuestVar<int32_t> myHomeLoadsDone{0x001BDB40u};
    inline constexpr GuestVar<int32_t> myHomeLoadState{0x001BDB44u};
    inline constexpr GuestVar<int32_t> myHomeLoadIndex{0x001BDB48u};
    inline constexpr GuestVar<uint32_t> myHomeFile{0x001BDB4Cu};
    inline constexpr GuestVar<int32_t> myHomeRequest{0x001BDB50u};
    inline constexpr GuestVar<int32_t> myHomeReads{0x001BDB54u};
    inline constexpr uint32_t kMyHomeBlocks = 0x001F3CE0u;
    inline constexpr uint32_t kMyHomeSizes = 0x001B0B80u;
    inline constexpr uint32_t kMyHomePaths = 0x001B0B20u;
    // The whole asset load (mm_CityAssets_LoadStep) shares those words: which load is running (myHomeLoadsDone: 0 file object, 1
    // characters, 2 objects, 3 character objects, 4 the MYHOME files, 5 close), then for the model list loads the step, the model
    // number, the shared file, its request and the models made so far, and the model being loaded by the character load.
    inline constexpr GuestVar<int32_t> assetLoadsDone{0x001BDB40u};
    inline constexpr GuestVar<int32_t> assetLoadStep{0x001BDB44u};
    inline constexpr GuestVar<int32_t> assetLoadIndex{0x001BDB48u};
    inline constexpr GuestVar<uint32_t> assetLoadFile{0x001BDB4Cu};
    inline constexpr GuestVar<int32_t> assetLoadRequest{0x001BDB50u};
    inline constexpr GuestVar<int32_t> assetLoadCount{0x001BDB54u};
    inline constexpr GuestVar<uint32_t> assetLoadModel{0x001BDB58u};
    inline constexpr uint32_t kObjectModels = 0x001F3B00u;      // OBJECT.A3D models, one word each
    inline constexpr uint32_t kCharObjectModels = 0x001F3B40u;  // CHAROBJ.A3D models
    inline constexpr GuestVar<uint32_t> eachStrData{0x001BDA8Cu};       // DATA\BIN\EACHSTR.BIN and its size in halfwords
    inline constexpr GuestVar<int32_t> eachStrCount{0x001BDA90u};

    // save: per card port (two words each): card type (2 = PS2 memory card), free space, formatted, the
    // type at the last check, the type when the save slot scan ended, and the last result (-1, -2: a new
    // card); then the entries returned by the scan's last directory read.
    inline constexpr uint32_t kCardType = 0x001BDB60u;
    inline constexpr uint32_t kCardFree = 0x001BDB68u;
    inline constexpr uint32_t kCardFormatted = 0x001BDB70u;
    inline constexpr uint32_t kCardTypeChecked = 0x001BDB78u;
    inline constexpr uint32_t kCardTypeScanned = 0x001BDB80u;
    inline constexpr uint32_t kCardResult = 0x001BDB88u;
    inline constexpr GuestVar<int32_t> cardDirEntries{0x001BDB90u};
    // save: the 16 save slots (8 per card): entries found when scanned (1 for a save, -4 = unknown), title
    // (0x44 bytes, from icon.sys), directory entry (0x40 bytes, its name from +0x20) and folder name (0x20).
    inline constexpr uint32_t kSaveSlotSizes = 0x001F4780u;
    inline constexpr uint32_t kSaveSlotFolders = 0x001F3CF0u;
    // save: the slot scan's names: the parent directory "..", "*" for any entry, "icon.sys"; the game's save
    // folder prefix (a pointer); the directory entry read (0x40 bytes) and the icon.sys buffer (0x3C4 bytes,
    // the title at +0xC0).
    inline constexpr uint32_t kParentDirName = 0x001B7EC8u;
    inline constexpr uint32_t kAnyEntryName = 0x001B7ED0u;
    inline constexpr uint32_t kIconSysName = 0x001B7E18u;
    inline constexpr GuestVar<uint32_t> saveFolderPrefix{0x001BD9A0u};
    inline constexpr uint32_t kScanDirEntry = 0x001F4300u;
    inline constexpr uint32_t kIconSysBuffer = 0x001F47C0u;
    // save: the save title's prefix and default name, and the full-width (Shift-JIS) form of ASCII 0x20..0x7D
    // (two bytes each) with the stand-in for other characters.
    inline constexpr uint32_t kSaveTitlePrefix = 0x001B7C50u;
    inline constexpr uint32_t kSaveTitleDefault = 0x001B7E00u;
    inline constexpr uint32_t kFullWidthAscii = 0x001B7C70u;
    inline constexpr uint32_t kFullWidthUnknown = 0x001B7D6Cu;
    inline constexpr uint32_t kSaveSlotNames = 0x001F4340u;
    inline constexpr uint32_t kSaveSlotHeaders = 0x001F3F00u;

    // sound: the 31 effect voices (0x30 bytes, first byte 0xFF = free), and the ambience grid: 4096 cells of
    // (sound + 1, variant), filled from the per-sound counts at 0x1B35F0 (11 rows of 0x1C bytes, six counts
    // from +0x10).
    inline constexpr uint32_t kSeVoices = 0x001F8140u;
    // Sound scripts (0x18 bytes, picked by a voice's voiceParam low byte): eight opcodes, then a uint16_t argument
    // for each (mm_Se_UpdateVoices).
    inline constexpr uint32_t kSeScripts = 0x001B3410u;
    // sound: this frame's effect requests (SeRequest, 578 and one spare) and their count.
    inline constexpr uint32_t kSeRequests = 0x001F8710u;
    inline constexpr GuestVar<uint16_t> seRequestCount{0x001BDD08u};
    inline constexpr uint32_t kAmbienceGrid = 0x001FC690u;
    inline constexpr uint32_t kAmbienceCounts = 0x001B35F0u;
    // Terrain ambience variant by an object's +0x14 byte (10 entries; 0 = decide by the object's type).
    inline constexpr uint32_t kAmbienceKinds = 0x001B3728u;

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
    inline constexpr GuestVar<GuestPtr<A3DObject>> propsModel{0x001BDBD4u};      // small goods stalls and keep-out parts
    inline constexpr GuestVar<GuestPtr<A3DObject>> fieldModel{0x001BDBD8u};      // the crops of a field
    inline constexpr GuestVar<GuestPtr<A3DObject>> pigFarmModel{0x001BDC00u};    // the pig farm entrance
    inline constexpr GuestVar<GuestPtr<A3DObject>> ranchModelA{0x001BDBE8u};     // the ranch entrance, always drawn
    inline constexpr GuestVar<GuestPtr<A3DObject>> ranchModelB{0x001BDBECu};     // drawn on top when the type's slot +0x24 answers yes
    inline constexpr GuestVar<GuestPtr<A3DObject>> ranchModelC{0x001BDBF0u};     // drawn on top when it answers no
    inline constexpr uint32_t kBlockStepX = 0x001B13F0u; // int[4]: 0, 2, 0, -2
    inline constexpr uint32_t kBlockStepZ = 0x001B1400u; // int[4]: -2, 0, 2, 0
    inline constexpr uint32_t kBlockDirBit = 0x001B1410u; // int[4]: 1, 2, 4, 8 (road shape bit per direction)
    // city camera (mm_CityCamera_Update): per zoom level the eye's offset in the player's frame, for clear
    // weather and for the rest (four Vec4 each); the near levels' target, 2 ahead of the player (0, 0, -2, 0);
    // the fog's far distance by level height, 29.5, 29.5, 32.5 and 40.5.
    inline constexpr uint32_t kCameraLevelsClear = 0x001AEF30u;
    inline constexpr uint32_t kCameraLevelsWeather = 0x001AEF70u;
    inline constexpr uint32_t kCameraLookAhead = 0x001AEFB0u;
    inline constexpr uint32_t kFogFarByLevel = 0x001AEFC0u;
    inline constexpr uint32_t kPathStepX = 0x001AEFD0u; // int[4]: 0, 2, 0, -2 (the same steps, mm_Path_NextWaypoint's copy)
    inline constexpr uint32_t kPathStepZ = 0x001AEFE0u; // int[4]: -2, 0, 2, 0
    // Housing blocks: mesh offset of each of the four block parts, per unit shape (int8[11][4]).
    inline constexpr uint32_t kHousingPartMesh = 0x001B14F0u;
    // The eight tiles around a tile and the tile itself, as x and z steps (int[9] each).
    inline constexpr uint32_t kNeighbourStepX = 0x001AEFF0u;
    // The eight tiles around a tile, without itself (int[8] each), clockwise from the south.
    inline constexpr uint32_t kRingStepX = 0x001AF2A0u;
    inline constexpr uint32_t kRingStepZ = 0x001AF2C0u;
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
    // ui: the status panel's 19 sprites (0x20 bytes each: two ST float pairs, then two XY word pairs in 1/16
    // pixel units), drawn from its packet (mm_StatusPanel_BuildPacket).
    inline constexpr uint32_t kStatusPanelSprites = 0x001AF3B0u;

    // title: the logo letters' size steps (uint16[3][8]: 0 hidden, 1 small, 2 medium, 3 full) and their five
    // places (uint16 x, y; 0x14 bytes per letter).
    inline constexpr uint32_t kLogoLetterSteps = 0x001B1820u;
    inline constexpr uint32_t kLogoLetterPlaces = 0x001B17E0u;
    // title: the logo intro's timed effects: the animation frame of each (float, 84 to 664, then 9000 as an
    // end mark) and its sound (uint8).
    inline constexpr uint32_t kIntroSoundFrames = 0x001B1A20u;
    inline constexpr uint32_t kIntroSounds = 0x001B1A10u;
    // title: the idle wobble of the selected choice, (0, -5, 0, 1) and (0, -10, 0, 1) (mm_Title_DrawChoicesIdle).
    inline constexpr uint32_t kChoiceWobbleSmall = 0x001B1850u;
    inline constexpr uint32_t kChoiceWobbleLarge = 0x001B1860u;
    // title: the sound test code, pad words ending in 0: Up, Up, Down, Down, Left, Right, Left, Right, L1, R1.
    inline constexpr uint32_t kSoundTestCode = 0x001B1790u;
    // the loading screen sprite's circling offset, (0, -10, 0, 1) (mm_LoadingScreen_Update).
    inline constexpr uint32_t kLoadingCircle = 0x001B13D0u;
    // Town's Pages contents (mm_TownPages_FillContents): the heading, CLOSE, the dashes line before a page
    // number, a newline, the two explanation texts of an empty book, and the cursor place per row (x, y).
    inline constexpr uint32_t kTownPagesHeading = 0x001B8E90u;
    inline constexpr uint32_t kTownPagesClose = 0x001B8EA0u;
    inline constexpr uint32_t kTownPagesDashes = 0x001B8EB0u;
    inline constexpr uint32_t kTownPagesNewline = 0x001B8EC0u;
    inline constexpr uint32_t kTownPagesEmptyLeft = 0x001B8ED0u;
    inline constexpr uint32_t kTownPagesEmptyRight = 0x001B8F30u;
    inline constexpr uint32_t kTownPagesCursor = 0x001B1A70u;

    // char: a person's walking step in its own frame, (0, 0, -speed, 1); mm_Person_UpdatePose sets z.
    inline constexpr uint32_t kWalkStep = 0x001AF0B0u;
    // The same for mm_Person_Walk, which keeps its own copy and sets z to the stride.
    inline constexpr uint32_t kWalkStride = 0x001AF0C0u;
    // mm_Player_PickTarget: the ray's step in the player's own frame, and the frames a kept target has been off
    // the heading (with the flag that the count was started).
    inline constexpr uint32_t kPickStep = 0x001B0930u;
    inline constexpr GuestVar<int32_t> pickOffFrames{0x001BDB38u};
    inline constexpr GuestVar<int8_t> pickTimerStarted{0x001BDB3Cu};

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

    // event: the event types loaded from EVENT.BIN (96 records of 0x4C bytes; priority is the signed byte at +0x22),
    // and a second copy of that pointer the handlers' End slots read (gp-0x7C10).
    inline constexpr GuestVar<uint32_t> eventTypes{0x001BDA84u};
    inline constexpr GuestVar<uint32_t> eventTypesCopy{0x001BDD60u};
    inline constexpr GuestVar<int32_t> popularityTicks{0x001BDC50u}; // clock ticks since the last popularity check (every 110)
    // event: the waiting queue (event ids, uint16_t) and its length; the queue object handed to
    // mm_EventQueue_InsertByPriority; the offsets (words) into claimData of each event's tile list
    // (mm_Event_SetTileFlags).
    inline constexpr uint32_t kEventQueue = 0x001C5790u;
    inline constexpr GuestVar<int32_t> eventQueueLength{0x001BDA80u};
    inline constexpr GuestVar<uint32_t> eventQueueObject{0x001BDA88u};
    inline constexpr GuestVar<uint32_t> eventTileLists{0x001BDA60u};
    // event: the delivery man's catalogue (0x184 bytes per entry, 0x20 per item: text at +4, price at +0x22), the
    // adoption event's 24 starting bytes, and the two buffers the handlers format money into.
    inline constexpr uint32_t kDeliveryCatalogue = 0x001BA090u;
    inline constexpr uint32_t kAdoptionStart = 0x001B37D0u;
    inline constexpr uint32_t kEventMoneyTextA = 0x0020B8A0u;
    inline constexpr uint32_t kEventMoneyTextB = 0x0020B8C0u;
    // event: the shopping event's goods texts (0x28 bytes per shop, halfword offsets into the names from +0x20).
    inline constexpr GuestVar<uint32_t> shoppingTexts{0x001BDAB4u};

    // title: the "[SELECT]SoundTest" hint, and a function static of mm_Title_PressStart (word, then its byte flag).
    inline constexpr uint32_t kSoundTestHint = 0x001B89E0u;
    inline constexpr uint32_t kPressStartStatic = 0x001BDC0Cu;
    // boot: the memory card check (mm_SceneA_MemoryCardCheck): each slot's status as last shown (mm_McManager_GetCardStatus:
    // 0 and 3 fine, 1 not enough space, 2 no card, 4 still being read) and the warning's pieces: heading, no card
    // in either slot, "the card in slot", then "s" (both), " 1" or " 2", "has not enough space", the closing line
    // (press Cross), and an empty text.
    inline constexpr GuestVar<int32_t> cardStatus0{0x001BDC04u};
    inline constexpr GuestVar<int32_t> cardStatus1{0x001BDC08u};
    inline constexpr uint32_t kCardTextHeading = 0x001B8A70u;
    inline constexpr uint32_t kCardTextNoCard = 0x001B8A90u;
    inline constexpr uint32_t kCardTextSlot = 0x001B8B30u;
    inline constexpr uint32_t kCardTextBoth = 0x001B8B78u;
    inline constexpr uint32_t kCardTextSlot1 = 0x001B8B80u;
    inline constexpr uint32_t kCardTextSlot2 = 0x001B8B88u;
    inline constexpr uint32_t kCardTextNoSpace = 0x001B8B90u;
    inline constexpr uint32_t kCardTextEnd = 0x001B8C20u;
    inline constexpr uint32_t kEmptyText = 0x001B8330u;

    // gfx: an identity matrix (four Vec4).
    inline constexpr uint32_t kIdentityMatrix = 0x001B2550u;
    inline constexpr uint32_t kOriginPoint = 0x001B26A0u; // (0, 0, 0, 1)
    // terrain generation (mm_TerrainGen_Generate): by terrain type 0..4, the Smooth strength, radius and floor
    // (floats), and for the plants of a cell the 16 object kinds (ints) drawn for plant style 2 and 3.
    inline constexpr uint32_t kSmoothStrength = 0x001AF1C0u;
    inline constexpr uint32_t kSmoothRadius = 0x001AF1E0u;
    inline constexpr uint32_t kSmoothFloor = 0x001AF200u;
    inline constexpr uint32_t kScatterKindsStyle2 = 0x001AF220u;
    inline constexpr uint32_t kScatterKindsStyle3 = 0x001AF260u;
    // terrain drawing (mm_Terrain_Draw), by camera zoom step: the projection's x and y extent, the camera's z offset (floats),
    // the reach in tiles (ints) and the far distance in two sets (picked by the caller), plus the camera heights where
    // the zoom steps change, in two sets.
    inline constexpr uint32_t kTerrainScreenX = 0x001AF300u;
    inline constexpr uint32_t kTerrainScreenY = 0x001AF310u;
    inline constexpr uint32_t kTerrainCameraZ = 0x001AF320u;
    inline constexpr uint32_t kTerrainReach = 0x001AF330u;
    inline constexpr uint32_t kTerrainFarA = 0x001AF2E0u;
    inline constexpr uint32_t kTerrainFarB = 0x001AF2F0u;
    inline constexpr uint32_t kTerrainZoomHeightsA = 0x001AF340u;
    inline constexpr uint32_t kTerrainZoomHeightsB = 0x001AF350u;
    // The frame counter whose low bit picks the double buffers (0x1BDCD0).
    inline constexpr uint32_t kFrameParity = 0x001BDCD0u;
    // talk: the eye's offset from a building object without the entrance flag (1.01, -0.59, 0.01, 1), turned
    // by the tile's facing (mm_TalkView_BuildingCamera).
    inline constexpr uint32_t kTalkEyeOffset = 0x001B1AF0u;
    // city lights and sky by the time of day (mm_CityCamera_UpdateTimeOfDay): the two sun directions (0, 1, 0, 1)
    // and (0, -1, 0, 1), and per two-hour slot (12 for clear weather, then 12 for the rest) three floats each for
    // the ambient level, lights 0 and 1, and the sky's two colours; a frame counter (started on first use) drives
    // the sky scroll and shimmer.
    inline constexpr uint32_t kSunDirections = 0x001B0E10u;
    inline constexpr uint32_t kAmbientBySlot = 0x001B0E30u;
    inline constexpr uint32_t kLightColourBySlot0 = 0x001B0F50u;
    inline constexpr uint32_t kLightColourBySlot1 = 0x001B1070u;
    inline constexpr uint32_t kSkyColourBySlotA = 0x001B1190u;
    inline constexpr uint32_t kSkyColourBySlotB = 0x001B12B0u;
    inline constexpr GuestVar<int32_t> timeOfDayFrames{0x001BDBB0u};
    inline constexpr GuestVar<int8_t> timeOfDayStarted{0x001BDBB4u};

    // char: model parts loaded from CHAR.A3D (pointer table, indexed by CHARTYPE.BIN record bytes).
    inline constexpr uint32_t kCharParts = 0x001F3B90u;

    // laptop: the default entry count (4 the first time) and its "set" flag; the entry template.
    inline constexpr GuestVar<int32_t> laptopDefaultCount{0x001BDABCu};
    inline constexpr GuestVar<int8_t> laptopDefaultSet{0x001BDAC0u};
    inline constexpr uint32_t kLaptopTemplate = 0x001AF640u;
}
