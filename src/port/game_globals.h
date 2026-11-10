// Addresses of the game's global data used by ported code (SLUS_203.77, see AGENTS.md rule 4).
// Grouped by subsystem; docs/map/<subsystem>.md describes each one.

#pragma once

#include "game_types.h"
#include "game_text.h"

#include <cstdint>

namespace mm
{
    // Index sentinels: no person (MapPerson), no object (MapObject; also "outside" and "none" in object fields).
    inline constexpr uint16_t kNoPerson = 6000;
    inline constexpr uint16_t kNoObject = 0x3F80;
    // MapObject.claimSlot of a building without a claim (past the end of every type's claim list).
    inline constexpr uint8_t kNoClaim = 0x18;
    // The object table (CityMap.objects): its records, kNoObject being one past them, and its size in bytes (saved
    // whole).
    inline constexpr int32_t kObjectSlots = 0x3F80;
    inline constexpr uint32_t kObjectTableBytes = 0x6F200u;
    static_assert(kObjectTableBytes == kObjectSlots * sizeof(MapObject));
    // The person table (CityMap.people): its records, kNoPerson being one past them.
    inline constexpr int32_t kPersonSlots = 6000;
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

    // sound: commands for the YYSOUND driver, queued in a ring of 256 entries (SoundCmd, 0xD0 bytes) and sent by
    // mm_SoundCmd_Flush.
    inline constexpr uint32_t kSoundCmds = 0x001FE6C0u;
    inline constexpr GuestVar<int32_t> soundCmdIndex{0x001BDD44u};   // next entry to write
    inline constexpr GuestVar<int32_t> soundCmdSent{0x001BDD48u};    // entries before it have been sent (mm_SoundCmd_Flush)
    inline constexpr GuestVar<uint32_t> soundVoicesQueued{0x001BDD4Cu}; // bit per voice with a queued play (cmd 3)
    inline constexpr GuestVar<uint32_t> soundSlotsQueued{0x001BDD50u};  // bit per sequence slot with a queued play (cmd 0xC)
    // Queued speech tracks (merged into SoundStatus.speechTracks); nothing sets it.
    inline constexpr GuestVar<uint32_t> soundSpeechQueued{0x001BDD54u};
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
    inline constexpr GuestVar<uint32_t> claimSupplyOffsets{0x001BDA58u};  // per supply list, its offset (words) into claimData

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

    // save: memory card request slots (McRequest, 64 of them). Requests are queued at the tail; the head moves as
    // they complete.
    inline constexpr uint32_t kMcRequests = 0x001F5CF0u;
    inline constexpr GuestVar<int32_t> mcQueueTail{0x001BDCD4u};
    inline constexpr GuestVar<int32_t> mcQueueHead{0x001BDCD8u};

    // file: the disc request queue, same scheme (FileRequest slots, 64 of them, the operation first).
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
    // The goods group of building type `group` (section A), and its record `index` (from the group's first).
    inline GuestPtr<GoodsGroup> goodsGroup(uint32_t group)
    {
        return GuestPtr<GoodsGroup>{goodsSectionA.get()} + static_cast<int32_t>(group);
    }
    inline GuestPtr<GoodsRecord> goodsAt(uint32_t group, uint32_t index)
    {
        const uint32_t first = goodsGroup(group)->first;
        return GuestPtr<GoodsRecord>{goodsSectionB.get() + 0x88u * (index + first)};
    }
    // claim: the offset tables (per list, its offset in words into claimData) of the claim lists and the item groups, and
    // the claim texts; pointers into claimData made from the file's offset words (+8 ...).
    inline constexpr GuestVar<uint32_t> claimListOffsets{0x001BDA54u};
    inline constexpr GuestVar<uint32_t> claimGroupOffsets{0x001BDA5Cu};
    inline constexpr GuestVar<uint32_t> claimTexts{0x001BDA74u};

    // pad: set by mm_Pad_SwallowCross; mm_Pad_Update keeps Cross (0x40) out of the held buttons until it is released.
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
    // The building type object for a type id.
    inline GuestPtr<BuildingType> buildingType(uint32_t type) { return GuestPtr<BuildingType>{buildingTypeAddress(type)}; }
    // Its flags, read through guest addresses like the original, whatever the table holds.
    inline uint16_t buildingTypeFlags(uint32_t type) { return buildingType(type)->flags; }

    // claim icons: colour set per icon id (bytes) and the colour table (Vec4, two levels per set).
    inline constexpr GuestVar<uint32_t> claimIconSets{0x001BDA64u};
    inline constexpr GuestVar<uint32_t> claimIconColours{0x001BDA70u};
    inline constexpr uint32_t kDefaultColour = 0x001B26C0u;              // Vec4: untinted instances
    inline constexpr uint32_t kClaimIconNoColour = kDefaultColour;       // Vec4 for level 0

    // family: DATA\BIN\EACHSTR.BIN, per building type the end (exclusive) of its family ids: type t's families run from
    // entry t - 1 up to entry t (mm_Family_GetTypeIndex); and its length in halfwords.
    inline constexpr GuestVar<uint32_t> familyTypeTable{0x001BDA8Cu};
    inline constexpr GuestVar<int32_t> familyTypeCount{0x001BDA90u};
    inline uint16_t familyTypeEnd(uint32_t type) { return port::load<uint16_t>(familyTypeTable.get() + 2u * type); }
    // The first family of a building type (the end of the one before it): the events that bring in a set family take it.
    inline uint16_t firstFamily(uint32_t type) { return familyTypeEnd(type - 1u); }
    // Building types (STRTYPE.BIN record numbers) whose families the events name.
    namespace types
    {
        inline constexpr uint32_t kZenMonksHome = 0xAC, kShadowMonksHome = 0xAD, kCrazyCult = 0xAE, kSpacemansHome = 0xAF,
                                  kStaffAccom = 0xB0, kCavemansHome = 0xB1, kBossHome = 0xB2, kStudioAppartmentB8 = 0xB8,
                                  kStudioAppartmentB9 = 0xB9, kNoodleShop = 0x100, kPoliceStation105 = 0x105,
                                  kUniversity = 0x10D;
    }

    // char: the introduction calls: the tick count the next call waits for (0 = none; one more after each try),
    // the ticks counted and the calls made (four at most).
    inline constexpr GuestVar<int32_t> introCallDue{0x001BDA44u};
    inline constexpr GuestVar<int32_t> introCallTick{0x001BDA48u};
    inline constexpr GuestVar<int32_t> introCallCount{0x001BDA4Cu};

    // char: the character class objects, one per CHARTYPE.BIN record (pointer table).
    inline constexpr GuestVar<uint32_t> charClassTable{0x001BDA3Cu};
    inline GuestPtr<CharClass> charClass(uint32_t type)
    {
        return GuestPtr<GuestPtr<CharClass>>{charClassTable.get()}[static_cast<int32_t>(type)];
    }

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

    // words: the text banks and bank 2's remaps (WordsTables), bound as the words files and script blocks load; then the
    // buffer a goods choice's name and price are written to (0x1C bytes).
    inline constexpr uint32_t kWordsTables = 0x007181C0u;
    inline GuestPtr<WordsTables> wordsTables() { return GuestPtr<WordsTables>{kWordsTables}; }
    inline GuestPtr<WordsBank> wordsBank(int32_t bank) { return GuestPtr<WordsBank>{kWordsTables} + bank; }
    inline constexpr uint32_t kWordsGoodsText = 0x00718240u;
    // The WORDS script object, the two files of script blocks (opened by the first mm_WordsBlock_ctor, which counts its
    // callers) and their block sizes (each file's first word), the queued read of the last block, the entries of the
    // -2005 menu not offered (bit n for entry n: used, or not open to this person), and the cache of eight script blocks
    // (a most-recently-used list of pointers to records: the block number, then its data).
    inline constexpr uint32_t kWords = 0x001BDDF0u;
    inline GuestPtr<Words> words() { return GuestPtr<Words>{kWords}; }
    inline constexpr GuestVar<GuestPtr<GameFile>> wordsFile{0x001BDDDCu};      // WORDS.BIN
    inline constexpr GuestVar<GuestPtr<GameFile>> wordsEventFile{0x001BDDE0u}; // WORDSEVE.BIN, a block per event kind
    inline constexpr GuestVar<int32_t> wordsFileUsers{0x001BDDE4u};
    inline constexpr GuestVar<int32_t> wordsBlockSize{0x001BDDE8u};
    inline constexpr GuestVar<int32_t> wordsEventBlockSize{0x001BDDECu};
    inline constexpr GuestVar<int32_t> wordsPendingRead{0x001BD9F4u}; // File_Read's result; -1 once the queue is waited for
    inline constexpr GuestVar<uint8_t> wordsHiddenChoices{0x001BDE18u};
    inline constexpr uint32_t kWordsBlockCache = 0x00718190u, kWordsBlockCacheEnd = 0x007181B0u;

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
    // (from icon.sys), directory entry and folder name (0x20 bytes).
    inline constexpr uint32_t kSaveSlotFound = 0x001F4780u;
    inline constexpr uint32_t kSaveSlotTitles = 0x001F4340u;
    inline constexpr uint32_t kSaveSlotEntries = 0x001F3F00u;
    inline constexpr uint32_t kSaveSlotFolders = 0x001F3CF0u;
    inline GuestPtr<int32_t> saveSlotFound(uint32_t slot) { return GuestPtr<int32_t>{kSaveSlotFound} + static_cast<int32_t>(slot); }
    inline GuestPtr<SaveTitle> saveSlotTitle(uint32_t slot) { return GuestPtr<SaveTitle>{kSaveSlotTitles} + static_cast<int32_t>(slot); }
    inline GuestPtr<CardDirEntry> saveSlotEntry(uint32_t slot) { return GuestPtr<CardDirEntry>{kSaveSlotEntries} + static_cast<int32_t>(slot); }
    inline uint32_t saveSlotFolder(uint32_t slot) { return kSaveSlotFolders + 0x20u * slot; }
    // A slot holds a save when the scan found its entry and it has a title.
    inline bool saveSlotUsed(uint32_t slot) { return *saveSlotFound(slot) >= 1 && saveSlotTitle(slot)->text[0] != 0; }
    // The loaded save's directory entry (checked before saving over it) and its description (McManager_FormatSaveInfo).
    inline GuestPtr<CardDirEntry> loadedSaveEntry() { return GuestPtr<CardDirEntry>{0x001C5640u}; }
    inline constexpr uint32_t kLoadedSaveInfo = 0x001C5680u;
    // save: the slot scan's names: the parent directory "..", "*" for any entry, "icon.sys"; the game's save
    // folder prefix (a pointer); the directory entry read (0x40 bytes) and the icon.sys buffer (0x3C4 bytes,
    // the title at +0xC0).
    inline constexpr uint32_t kParentDirName = 0x001B7EC8u;
    inline constexpr uint32_t kAnyEntryName = 0x001B7ED0u;
    inline constexpr uint32_t kIconSysName = 0x001B7E18u;
    inline constexpr GuestVar<uint32_t> saveFolderPrefix{0x001BD9A0u};
    inline GuestPtr<CardDirEntry> scanDirEntry() { return GuestPtr<CardDirEntry>{0x001F4300u}; }
    inline constexpr uint32_t kIconSysBuffer = 0x001F47C0u;
    // save: the save title's prefix and default name, and the full-width (Shift-JIS) form of ASCII 0x20..0x7D
    // (two bytes each) with the stand-in for other characters.
    inline constexpr uint32_t kSaveTitlePrefix = 0x001B7C50u;
    inline constexpr uint32_t kSaveTitleDefault = 0x001B7E00u;
    inline constexpr uint32_t kFullWidthAscii = 0x001B7C70u;
    inline constexpr uint32_t kFullWidthUnknown = 0x001B7D6Cu;

    // sound: the 31 effect voices (0x30 bytes, first byte 0xFF = free), and the ambience grid: 4096 cells of
    // (sound + 1, variant), filled from the per-sound counts at 0x1B35F0 (11 rows of 0x1C bytes, six counts
    // from +0x10).
    inline constexpr uint32_t kSeVoices = 0x001F8140u;
    inline GuestPtr<SeVoice> seVoice(uint32_t voice) { return GuestPtr<SeVoice>{kSeVoices} + static_cast<int32_t>(voice); }
    inline GuestPtr<SeSound> seSound(uint32_t sound) { return GuestPtr<SeSound>{kSoundTable} + static_cast<int32_t>(sound); }
    inline GuestPtr<BgmTrack> bgmTracks() { return GuestPtr<BgmTrack>{kBgmTracks}; }
    inline GuestPtr<SoundStatus> soundStatus() { return GuestPtr<SoundStatus>{kSoundStatus}; }
    inline GuestPtr<SoundStatus> soundStatusView() { return GuestPtr<SoundStatus>{kSoundStatusView}; }
    // Sound scripts (0x18 bytes, picked by a voice's voiceParam low byte): eight opcodes, then a uint16_t argument
    // for each (mm_Se_UpdateVoices).
    inline constexpr uint32_t kSeScripts = 0x001B3410u;
    // sound: this frame's effect requests (SeRequest, 578 and one spare) and their count.
    inline constexpr uint32_t kSeRequests = 0x001F8710u;
    inline constexpr GuestVar<uint16_t> seRequestCount{0x001BDD08u};
    inline constexpr uint32_t kAmbienceGrid = 0x001FC690u;
    inline constexpr uint32_t kAmbienceCounts = 0x001B35F0u;
    // Terrain ambience variant by an object's look[4] byte (10 entries; 0 = decide by the object's type).
    inline constexpr uint32_t kAmbienceKinds = 0x001B3728u;

    // gfx: model instance slots for this frame: the two buffers (by frame), which one is current, how many are
    // used (0x708 at most), and a second count with its own limit for mm_Instance_DrawNodeLimited.
    inline constexpr uint32_t kInstanceBuffers = 0x001BDA20u;
    inline constexpr GuestVar<int32_t> instanceCount{0x001BDA28u};
    inline constexpr GuestVar<int32_t> instanceBuffer{0x001BDA30u};
    inline constexpr GuestVar<int32_t> instanceLimitedCount{0x001BDA34u};
    inline constexpr GuestVar<int32_t> instanceLimit{0x001BD980u};
    inline constexpr GuestVar<float> frameLoad{0x001BD9B8u};          // last frame's load in percent (provisional)

    // city: building drawing. The four facings' matrices (a turn about y by kFacingHeadings), which the draw slots fill
    // in with the translation in the last row, the models holding the road and building meshes and the wasteland
    // meshes, and the steps to the four neighbouring 2x2 blocks (north, east, south, west).
    inline constexpr uint32_t kBuildingMatrices = 0x001B2590u;
    inline constexpr GuestVar<GuestPtr<A3DObject>> cityPartsModel{0x001BDBD0u};
    inline constexpr GuestVar<GuestPtr<A3DObject>> wastelandModel{0x001BDBCCu};
    inline constexpr GuestVar<GuestPtr<A3DObject>> businessModel{0x001BDBC4u};   // finished businesses and ports (BuildingType.builtMesh)
    inline constexpr GuestVar<GuestPtr<A3DObject>> blockModel{0x001BDBC8u};      // office and housing block parts
    inline constexpr GuestVar<GuestPtr<A3DObject>> parkRideModel{0x001BDBE0u};   // theme park ride (animated)
    inline constexpr GuestVar<GuestPtr<A3DObject>> propsModel{0x001BDBD4u};      // small goods stalls and keep-out parts
    inline constexpr GuestVar<GuestPtr<A3DObject>> fieldModel{0x001BDBD8u};      // the crops of a field
    inline constexpr GuestVar<GuestPtr<A3DObject>> pigFarmModel{0x001BDC00u};    // the pig farm once built
    inline constexpr GuestVar<GuestPtr<A3DObject>> ranchModelA{0x001BDBE8u};     // the ranch once built, always drawn
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
    // The heading (turn about y) for each facing 0..3 (mm_Map_GetTileFacing), float[4]: pi, -pi/2, 0, pi/2. The
    // building people, the road corners and the my-home view turn by it.
    inline constexpr uint32_t kFacingHeadings = 0x001B2690u;
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

    // event: the event types loaded from EVENT.BIN (96 EventType records), and a second copy of that pointer the
    // handlers' End slots read (gp-0x7C10); the type record of event kind `kind` through either.
    inline constexpr GuestVar<uint32_t> eventTypes{0x001BDA84u};
    inline constexpr GuestVar<uint32_t> eventTypesCopy{0x001BDD60u};
    inline GuestPtr<EventType> eventType(uint32_t kind) { return GuestPtr<EventType>{eventTypes.get()} + static_cast<int32_t>(kind); }
    inline GuestPtr<EventType> eventTypeCopy(uint32_t kind) { return GuestPtr<EventType>{eventTypesCopy.get()} + static_cast<int32_t>(kind); }
    inline constexpr GuestVar<int32_t> popularityTicks{0x001BDC50u}; // clock ticks since the last popularity check (every 110)
    // event: each event type's id (group, number; saved with the city, to match the queue with the table of a later
    // build) and the number of types; the event queue; the offsets (words) into claimData of each event claim list
    // (EventClaimList, mm_Event_AddClaims), the list `list` and the claim slot at a word offset.
    inline constexpr uint32_t kEventTypeIds = 0x001C5790u;
    inline constexpr GuestVar<int32_t> eventTypeCount{0x001BDA80u};
    inline constexpr GuestVar<uint32_t> eventQueueObject{0x001BDA88u};
    inline GuestPtr<EventQueue> eventQueue() { return GuestPtr<EventQueue>{eventQueueObject.get()}; }
    inline constexpr GuestVar<uint32_t> eventClaimLists{0x001BDA60u};
    inline GuestPtr<EventClaimList> eventClaimList(int32_t list)
    {
        return GuestPtr<EventClaimList>{claimData.get() + 4u * GuestPtr<uint32_t>{eventClaimLists.get()}[list]};
    }
    inline GuestPtr<ClaimEntry> claimSlot(uint32_t offsetWords)
    {
        return GuestPtr<ClaimEntry>{claimData.get() + 4u * offsetWords};
    }
    // event: the whole map as a block (0, 0, 128, 128), where the thief looks for a building to rob; an ActorRef with
    // no person and no building.
    inline constexpr uint32_t kWholeMapRect = 0x001BA8D0u;
    inline constexpr uint32_t kNoActor = 0x001BD988u;
    // event: the delivery man's catalogue (0x184 bytes per entry, 0x20 per item: text at +4, price at +0x22), the
    // adoption event's 24 starting bytes, and the two buffers the handlers format money into.
    inline constexpr uint32_t kDeliveryCatalogue = 0x001BA090u;
    inline GuestPtr<DeliveryCatalogue> deliveryCatalogue(int32_t entry) { return GuestPtr<DeliveryCatalogue>{kDeliveryCatalogue} + entry; }
    // The item of a catalogue for a month, by guest address (any entry and month, as the original: the sum is not masked
    // into RDRAM, so a stray one returns the same address the game would).
    inline GuestPtr<DeliveryItem> deliveryItem(int32_t entry, int32_t month)
    {
        return GuestPtr<DeliveryItem>{deliveryCatalogue(entry).address + static_cast<uint32_t>(offsetof(DeliveryCatalogue, items))} + month;
    }
    inline constexpr uint32_t kAdoptionStart = 0x001B37D0u;
    // event: zero-ended tables in the original's data, each function with its own copy: the tile lists the suitor
    // events search (Boyfriend and Girlfriend: 1, 6, 5, 0x30; Affair: 1), the cult's member actors (4, 5, 1), the
    // building types the alien's second building can be (0xF3, 0xF6, 0xFB, 0xFD, four restaurants) and the places it
    // can land (the same four types, each with its tile list).
    inline constexpr GuestPtr<int32_t> kBoyfriendLists{0x001B3810u};
    inline constexpr GuestPtr<int32_t> kGirlfriendLists{0x001B37F0u};
    inline constexpr GuestPtr<int32_t> kAffairLists{0x001BD9E0u};
    inline constexpr GuestPtr<int32_t> kCultHomesActors{0x001B37A0u};   // mm_EvCrazyCult_TellSecondMember, phase 15
    inline constexpr GuestPtr<int32_t> kCultPeopleActors{0x001B37B0u};  // the same, phase 16
    inline constexpr GuestPtr<int32_t> kCultAnswerActors{0x001B37C0u};  // mm_EvCrazyCult_Answer
    inline constexpr GuestPtr<uint16_t> kAlienSecondTypes{0x001B3750u};
    inline constexpr GuestPtr<AlienSite> kAlienSites{0x001B3760u};
    // event: the tile lists the delivery man picks a house from (not zero-ended): five in holiday months (1, 2, 5, 6,
    // 7), else two (8, 9) (mm_EvDeliveryMan_Check).
    inline constexpr GuestPtr<int32_t> kDeliveryHolidayLists{0x001B3780u};
    inline constexpr GuestPtr<int32_t> kDeliveryWorkdayLists{0x001BD9D0u};
    // event: the starting values the original copies into some handlers' locals: the noodle stand's three workers
    // (words in .bss, 0 unless written) and their buildings (kNoObject), the alien's four buildings (.bss words).
    inline constexpr GuestPtr<uint32_t> kNoodleWorkersStart{0x0020B8D0u};
    inline constexpr GuestPtr<uint16_t> kNoodleBuildingsStart{0x001BD9D8u};
    inline constexpr GuestPtr<uint32_t> kAlienBuildingsStart{0x0020B880u};
    inline constexpr uint32_t kEventMoneyTextA = 0x0020B8A0u;
    inline constexpr uint32_t kEventMoneyTextB = 0x0020B8C0u;
    // event: the shopping event's goods texts (0x28 bytes per shop, halfword offsets into the names from +0x20).
    inline constexpr GuestVar<uint32_t> shoppingTexts{0x001BDAB4u};
    inline GuestPtr<ShoppingList> shoppingList(int32_t index) { return GuestPtr<ShoppingList>{shoppingTexts.get()} + index; }

    // title: the "[SELECT]SoundTest" hint, and a function static of mm_Title_PressStart (word, then its byte flag).
    inline constexpr uint32_t kSoundTestHint = 0x001B89E0u;
    inline constexpr uint32_t kPressStartStatic = 0x001BDC0Cu;
    // boot: the memory card check (mm_TitleScene_MemoryCardCheck): each slot's status as last shown (mm_McManager_GetCardStatus:
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
    inline constexpr uint32_t kZeroVector = 0x001B26B0u;  // (0, 0, 0, 0)
    // terrain generation (mm_TerrainGen_Generate): by terrain type 0..4, the Smooth strength, radius and floor
    // (floats), and for the plants of a cell the 16 object kinds (ints) drawn for plant style 2 and 3.
    inline constexpr uint32_t kHillStrength = 0x001AF1C0u;
    inline constexpr uint32_t kHillRadius = 0x001AF1E0u;
    inline constexpr uint32_t kHillTop = 0x001AF200u;
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
    // talk: the eye's offset from a building object still being built (no flag 0x20; 1.01, -0.59, 0.01, 1), turned
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

    // models: OBJECT.A3D's models (kObjectModels, one pointer each) by what draws them.
    inline constexpr GuestVar<GuestPtr<A3DObject>> statusAlertModel{0x001F3B00u};  // model 0: the status panel's alert
    inline constexpr GuestVar<GuestPtr<A3DObject>> wantsModel{0x001F3B04u};        // model 1: the wants window
    inline constexpr GuestVar<GuestPtr<A3DObject>> statusPanelModel{0x001F3B0Cu};  // model 3
    // Model 4: the applicant cards' frame
    inline constexpr GuestVar<GuestPtr<A3DObject>> applicantFrameModel{0x001F3B10u};
    inline constexpr GuestVar<GuestPtr<A3DObject>> minimapModel{0x001F3B14u};      // model 5
    inline constexpr GuestVar<GuestPtr<A3DObject>> minimapFrameModel{0x001F3B18u}; // model 6
    // Model 7: the marker of a building being placed
    inline constexpr GuestVar<GuestPtr<A3DObject>> placementMarkerModel{0x001F3B1Cu};
    inline constexpr GuestVar<GuestPtr<A3DObject>> townPagesModel{0x001F3B20u};    // model 8: the Town's Pages book
    inline constexpr GuestVar<GuestPtr<A3DObject>> bagModel{0x001F3B24u};          // model 9
    inline constexpr GuestVar<GuestPtr<A3DObject>> markersModel{0x001F3B28u};      // model 10: the circle markers
    // Model 11: message and menu windows, talk cards, the money panel, the talk building, the briefing's office
    inline constexpr GuestVar<GuestPtr<A3DObject>> talkSceneModel{0x001F3B2Cu};
    inline constexpr GuestVar<GuestPtr<A3DObject>> statusDetailModel{0x001F3B34u}; // model 13

    // models: CHAROBJ.A3D's models (kCharObjectModels), the building types' copies of them (BldgType_BaseCtor and the
    // farm and park constructors), the talk's person model and the part drawn at a model's third node.
    // Model 0: drawn for someone away (TalkView.awayModel)
    inline constexpr GuestVar<GuestPtr<A3DObject>> awayModel{0x001F3B40u};
    inline constexpr GuestVar<GuestPtr<A3DObject>> charObjBusiness{0x001F3B44u};    // model 1, copied to businessModel
    inline constexpr GuestVar<GuestPtr<A3DObject>> charObjBlock{0x001F3B48u};       // model 2, copied to blockModel
    inline constexpr GuestVar<GuestPtr<A3DObject>> charObjParkRide{0x001F3B4Cu};    // model 3, copied to parkRideModel
    // Models 4 to 6, copied to ranchModelA to C
    inline constexpr GuestVar<GuestPtr<A3DObject>> charObjRanchA{0x001F3B50u};
    inline constexpr GuestVar<GuestPtr<A3DObject>> charObjRanchB{0x001F3B54u};
    inline constexpr GuestVar<GuestPtr<A3DObject>> charObjRanchC{0x001F3B58u};
    inline constexpr GuestVar<GuestPtr<A3DObject>> charObjPigFarm{0x001F3B5Cu};     // model 7, copied to pigFarmModel
    inline constexpr GuestVar<GuestPtr<A3DObject>> charObjCityParts{0x001F3B60u};   // model 8, copied to cityPartsModel
    inline constexpr GuestVar<GuestPtr<A3DObject>> charObjWasteland{0x001F3B64u};   // model 9, copied to wastelandModel
    inline constexpr GuestVar<GuestPtr<A3DObject>> applicantCardModel{0x001F3B68u}; // model 10: the applicant cards
    inline constexpr GuestVar<GuestPtr<A3DObject>> nameEntryModel{0x001F3B6Cu};     // model 11: the name entry window
    // Model 12: the claim-met layer's two meshes per building size
    inline constexpr GuestVar<GuestPtr<A3DObject>> claimMetModel{0x001F3B70u};
    inline constexpr GuestVar<GuestPtr<A3DObject>> charObjProps{0x001F3B74u};       // model 13, copied to propsModel
    inline constexpr GuestVar<GuestPtr<A3DObject>> charObjField{0x001F3B78u};       // model 14, copied to fieldModel
    inline constexpr GuestVar<GuestPtr<A3DObject>> talkPersonModel{0x001F3B7Cu};    // model 15 (TalkView.personModel)
    // Model 16: drawn at node 2 by mm_Instance_DrawModelEx when asked
    inline constexpr GuestVar<GuestPtr<A3DObject>> attachedPartModel{0x001F3B80u};
    // Mm_Instance_DrawModelEx stores the part it draws; nothing reads it
    inline constexpr GuestVar<GuestPtr<A3DObject>> lastAttachedPart{0x001BDA38u};

    // models: the player and the boss (entries 81 and 54 of kCharParts) and the MYHOME files' blocks after the first
    // (kMyHomeBlocks: MYHOME.A3L, COLLI.A3L, PROFILE.BIN); the building types keep the collision block
    // (mm_BldgType_BaseCtor; collide.cpp reads it).
    inline constexpr GuestVar<GuestPtr<A3DObject>> playerModel{0x001F3CD4u};
    // Entry 54 of kCharParts: the boss in the briefing
    inline constexpr GuestVar<GuestPtr<A3DObject>> bossModel{0x001F3C68u};
    inline constexpr GuestVar<uint32_t> colliBlock{0x001F3CE4u};             // COLLI.A3L
    // PROFILE.BIN: per character type three profile texts (halfword offsets), mm_TalkCard_FillText
    inline constexpr GuestVar<uint32_t> profileBlock{0x001F3CE8u};
    // The building collision meshes (COLLI.A3L): per mesh its first and last triangle, then the triangles
    inline constexpr GuestVar<uint32_t> collisionData{0x001BDBDCu};

    // city: the park ride's and the ranch's animation times (drawRide's function statics).
    inline constexpr GuestVar<float> parkRideTime{0x001BDBE4u};
    inline constexpr GuestVar<float> ranchTime{0x001BDBF4u};

    // vtables: widgets (WidgetVtable) and other classes, from the constructors that store them.
    inline constexpr GuestPtr<WidgetVtable> kWidgetVtable{0x001BD400u}; // the base widget (mm_Widget_ctor)
    inline constexpr GuestPtr<WidgetVtable> kMsgWinVtable{0x001BCE10u};
    inline constexpr GuestPtr<WidgetVtable> kMenuWinVtable{0x001BCF80u};
    inline constexpr GuestPtr<StageSceneVtable> kStageSceneVtable{0x001BCF10u};
    inline constexpr GuestPtr<WidgetVtable> kStageSceneWidgetVtable{0x001BCF18u}; // the stage scene's widget part
    inline constexpr GuestPtr<void> kApplicantsBaseVtable{0x001BCF60u};
    inline constexpr GuestPtr<void> kApplicantsVtable{0x001BCF70u};
    inline constexpr GuestPtr<void> kNameEntryVtable{0x001BCFE8u};
    inline constexpr GuestPtr<void> kMarkersVtable{0x001BCFC8u};
    inline constexpr uint32_t kTexSpriteVtable = 0x001BCFD8u; // stored as a word (mm_TexSprite_Load)
    inline constexpr GuestPtr<WidgetVtable> kTalkViewVtable{0x001BD450u};
    inline constexpr GuestPtr<WidgetVtable> kTalkPhoneFamilyVtable{0x001BD4A0u};
    inline constexpr GuestPtr<WidgetVtable> kTalkPhoneMemberVtable{0x001BD4F0u};
    inline constexpr GuestPtr<WidgetVtable> kTalkBldgFarVtable{0x001BD540u};
    inline constexpr GuestPtr<WidgetVtable> kTalkBldgNearVtable{0x001BD590u};
    inline constexpr GuestPtr<WidgetVtable> kTalkOccupantFarVtable{0x001BD5E0u};
    inline constexpr GuestPtr<WidgetVtable> kTalkOccupantNearVtable{0x001BD630u};
    inline constexpr GuestPtr<WidgetVtable> kTalkPersonFarVtable{0x001BD680u};
    inline constexpr GuestPtr<WidgetVtable> kTalkPersonNearVtable{0x001BD6D0u};
    inline constexpr GuestPtr<WidgetVtable> kTalkCardVtable{0x001BD720u};
    inline constexpr GuestPtr<WidgetVtable> kTalkVtable{0x001BD770u};
    inline constexpr GuestPtr<WidgetVtable> kTalkMoneyVtable{0x001BD7C0u};
    inline constexpr GuestPtr<WidgetVtable> kMyHomeVtable{0x001BD810u};
    inline constexpr GuestPtr<WidgetVtable> kTalkConvVtable{0x001BD860u};
    inline constexpr GuestPtr<CharClassVtable> kCharTypeVtable{0x001BCE70u}; // the base character class
    inline constexpr GuestPtr<CharClassVtable> kVehicleVtable{0x001BCE90u};
    inline constexpr GuestPtr<CharClassVtable> kPersonVtable{0x001BCEB0u};
    inline constexpr GuestPtr<CityMapVtable> kCityMapVtable{0x001BCED0u};
    inline constexpr GuestPtr<CityMapVtable> kMapPeopleVtable{0x001BCEE0u};
    inline constexpr GuestPtr<CityMapVtable> kMapObjectsVtable{0x001BCEF0u};
    inline constexpr GuestPtr<CityMapVtable> kCityRenderVtable{0x001BCF00u};
    inline constexpr GuestPtr<BuildingTypeVtable> kBldgTypeVtable{0x001BD000u}; // the base building type
    inline constexpr uint32_t kBldgVtable = 0x001BD100u; // building types (stored as words by constructBuildingType)
    inline constexpr uint32_t kBldgBusinessVtable = 0x001BD1C0u;
    inline constexpr uint32_t kBldgAmusementParkVtable = 0x001BD040u;
    inline constexpr uint32_t kBldgOfficeVtable = 0x001BD080u;
    inline constexpr uint32_t kBldgRanchVtable = 0x001BD0C0u;
    inline constexpr uint32_t kBldgFieldVtable = 0x001BD140u;
    inline constexpr uint32_t kBldgPortVtable = 0x001BD180u;
    inline constexpr uint32_t kBldgKeepOutVtable = 0x001BD200u;
    inline constexpr uint32_t kBldgLeveledVtable = 0x001BD240u;
    inline constexpr uint32_t kBldgHousingVtable = 0x001BD280u;
    inline constexpr uint32_t kBldgMyHomeVtable = 0x001BD2C0u;
    inline constexpr uint32_t kBldgPigFarmVtable = 0x001BD300u;
    inline constexpr uint32_t kBldgRoadVtable = 0x001BD340u;
    inline constexpr uint32_t kBldgSmallGoodsVtable = 0x001BD380u;
    inline constexpr uint32_t kBldgWastelandVtable = 0x001BD3C0u;
    inline constexpr uint32_t kGameVtable = 0x001BCE60u;
    inline constexpr uint32_t kAppVtable = 0x001BD8D0u;
    inline constexpr uint32_t kPartyPopupBaseVtable = 0x001BD8B0u; // the farewell party's popup, base class
    inline constexpr uint32_t kPartyPopupVtable = 0x001BD8E0u;
    // The talk event the shopping script works with, base class
    inline constexpr uint32_t kTalkEventBaseVtable = 0x001BD908u;
    inline constexpr uint32_t kTalkEventVtable = 0x001BD8F8u; // its derived class (0x60 more bytes)

    // words: the static constructor's registration (mm_StaticInit_Words), and the script function tables (member
    // pointers by function number: acts, texts, conditions).
    inline constexpr uint32_t kWordsDtor = 0x00180490u; // the destructor registered for the WORDS object
    inline constexpr uint32_t kWordsActs = 0x001B9100u;
    inline constexpr uint32_t kWordsTexts = 0x001B9570u;
    inline constexpr uint32_t kWordsConds = 0x001B9630u;
    // Int per member look: how far mm_Words_CollectNearbyHomes looks
    inline constexpr uint32_t kLookRanges = 0x001B97F0u;
    // Per argument a pointer to a zero-ended list of character types (mm_Words_PickResidentByType)
    inline constexpr uint32_t kResidentTypeLists = 0x001BE950u;
    // The starting word the want iterators copy into their locals (gp statics)
    inline constexpr GuestVar<uint32_t> claimResidentStart{0x001BDC1Cu};
    inline constexpr GuestVar<uint32_t> acquaintanceSupplyStart{0x001BDC20u};
    inline constexpr GuestVar<uint32_t> wantBodyStart{0x001BDC24u};
    inline constexpr uint32_t kWordsTextBuffer = 0x001F4EB0u; // the shared output of the formatting texts

    // ui: the message window: the blink counter of its "more" mark and the typing delay counter, each with its started
    // byte (function statics).
    inline constexpr GuestVar<int32_t> msgWinBlinkFrames{0x001BDA00u};
    inline constexpr GuestVar<int8_t> msgWinBlinkStarted{0x001BDA04u};
    inline constexpr GuestVar<int32_t> msgWinTypeFrames{0x001BDA08u};
    inline constexpr GuestVar<int8_t> msgWinTypeStarted{0x001BDA0Cu};

    // ui: the wait text (mm_WaitText_*): its box, the blink counter and whether it was made.
    inline constexpr GuestVar<uint32_t> waitTextBox{0x001BDAC4u};
    inline constexpr GuestVar<int32_t> waitTextFrames{0x001BDAC8u};
    inline constexpr GuestVar<int32_t> waitTextMade{0x001BDACCu};

    // ui: the sound test's function static (word and started byte; mm_SoundTest_Update).
    inline constexpr GuestVar<int32_t> soundTestStatic{0x001BDC14u};
    inline constexpr GuestVar<int8_t> soundTestStarted{0x001BDC18u};

    // ui: the status panel: four sprites' screen corners inside kStatusPanelSprites (the XY pairs at +0x10 and +0x18 of
    // sprites 8, 9 and 10), the digit glyphs after the sprites, and the theme words by mission kind.
    inline constexpr uint32_t kStatusPanelGlyphs = 0x001AF610u; // the digit glyphs' sprite words
    inline constexpr uint32_t kThemeNames = 0x001B6470u;        // pointers by mission kind ("", offices, ..., up-scale)

    // ui: the laptop: the selected and other entries' colours (Vec4), and each entry's description (0x80 bytes per
    // line).
    inline constexpr uint32_t kLaptopSelectedColour = 0x001AF690u;
    inline constexpr uint32_t kLaptopOtherColour = 0x001AF6A0u;
    inline constexpr uint32_t kLaptopItemTexts = 0x001AF6B0u;

    // ui: the frame packet template (0xB0 bytes, mm_Frame_ctor).
    inline constexpr uint32_t kFrameTemplate = 0x001B04B0u;

    // ui: the minimap's packet: the chain it calls (its texture upload) and the two blinking colour words in it.
    inline constexpr uint32_t kMinimapUpload = 0x001AF180u;
    inline constexpr uint32_t kMinimapBlinkA = 0x001AF1A4u;
    inline constexpr uint32_t kMinimapBlinkB = 0x001AF1A8u;

    // ui: the stage scene: the friendship level names (pointers, Stranger to Best friend) and the members counted at
    // each level 1 to 4 (mm_MemberMeters_Reset), shown on the approval rate page; the telops' texts, music volumes and
    // scroll ends by kind; the per-stage briefing words (25) and the extra briefing texts with their stage ids
    // (zero-ended); the actions per row and step (mm_StageScene_Update); the camera's eye and target.
    inline constexpr uint32_t kFriendshipNames = 0x001B0790u;
    inline constexpr uint32_t kFriendshipCounts = 0x001C5780u;    // int[4]
    inline constexpr uint32_t kTelopTexts = 0x001B0800u;
    inline constexpr uint32_t kTelopVolumes = 0x001B07A8u;
    inline constexpr uint32_t kTelopScrollEnds = 0x001B0810u;
    inline constexpr uint32_t kBriefingStageTexts = 0x001B0820u;
    inline constexpr uint32_t kBriefingExtraTexts = 0x001B0890u;
    inline constexpr uint32_t kBriefingExtraStages = 0x001B71F0u;
    inline constexpr uint32_t kStageSceneActions = 0x001B07C0u;
    inline constexpr uint32_t kStageSceneEye = 0x001B08D0u;
    inline constexpr uint32_t kStageSceneTarget = 0x001B08E0u;
    // The telop bank's three file names built at run time (0x20 bytes each)
    inline constexpr uint32_t kTelopBankSqName = 0x001F3730u;
    inline constexpr uint32_t kTelopBankHdName = 0x001F3750u;
    inline constexpr uint32_t kTelopBankBdName = 0x001F3770u;

    // stage: the stage state's names (StageState.names): 1 the mayor's name as typed, 2 printed before it where a
    // member's given name goes (the applicant card's "%s %s"; the ending telop puts no space between); empty in every
    // save at hand.
    inline constexpr uint32_t kMayorName = 0x001C5390u;
    inline constexpr uint32_t kMayorGivenName = 0x001C53A0u;

    // ui: the wants window's open and closed places (Vec4, mm_WantsWin_Update), also its starting place
    // (mm_WantsWin_ctor).
    inline constexpr uint32_t kWantsWinOpen = 0x001B08F0u;
    inline constexpr uint32_t kWantsWinClosed = 0x001B0900u;

    // ui: the name entry's key sets (a pointer per set).
    inline constexpr uint32_t kNameEntryKeySets = 0x001B13E0u;

    // title: the camera (eye, target), the new game's camera, the character preview's turn steps (float) and texts.
    inline constexpr uint32_t kTitleEye = 0x001B17C0u;
    inline constexpr uint32_t kTitleTarget = 0x001B17D0u;
    inline constexpr uint32_t kNewGameEye = 0x001B19F0u;
    inline constexpr uint32_t kNewGameTarget = 0x001B1A00u;
    inline constexpr uint32_t kPreviewTurnSteps = 0x001B1870u;
    inline constexpr uint32_t kPreviewTexts = 0x001B1930u;

    // ui: the Town's Pages book's open and closed places (Vec4, slid between in mm_TownPages_Update).
    inline constexpr uint32_t kTownPagesOpen = 0x001B1A50u;
    inline constexpr uint32_t kTownPagesClosed = 0x001B1A60u;

    // ui: the text box styles (mm_TextBox_Init: -1 the default palette, -2 the second).
    inline constexpr uint32_t kTextBoxStyleDefault = 0x001B24A0u;
    inline constexpr uint32_t kTextBoxStyleAlternate = 0x001B24E0u;

    // ui: the applicant card: the state colours (byte per entry state below 8), the frame's node per card kind and the
    // markers' x offsets, the card nodes' luminosity groups (bytes per node), its text buffer and the bytes it cuts.
    inline constexpr uint32_t kApplicantStateColours = 0x001BD998u;
    inline constexpr uint32_t kApplicantNodeGroups = 0x001B7A10u;
    inline constexpr uint32_t kApplicantFrameNodes = 0x001B7A18u;
    inline constexpr uint32_t kApplicantMarkerX = 0x001B7A20u;
    inline constexpr uint32_t kApplicantText = 0x001F3790u;

    // bag: the bag's places (Vec4): shown, and slid away; and the applicants' end place (mm_Applicants_Update).
    inline constexpr uint32_t kApplicantsEnd = 0x001AEF00u;
    inline constexpr uint32_t kBagShown = 0x001AEF10u;
    inline constexpr uint32_t kBagAway = 0x001AEF20u;
    inline constexpr uint32_t kBagNoAction = 0x001B5B00u;   // the empty action text of goods kind 0x10

    // city: housing unit shapes from neighbours (bytes, 7 per row and 6 per row), field ground textures by unit shape
    // (halfwords), the road's signal meshes by traffic phase, the road corner places and headings by shape, the
    // wasteland's spot steps, the office collision mesh tables (floor and nearest hit), the scene's camera.
    inline constexpr uint32_t kHousingShapeByPair = 0x001B1480u;
    inline constexpr uint32_t kHousingShapeByFirst = 0x001B14C0u;
    inline constexpr uint32_t kFieldGroundByShape = 0x001B1520u;
    inline constexpr uint32_t kRoadSignalMeshes = 0x001B1540u;
    inline constexpr uint32_t kRoadCornerX = 0x001B15E0u;
    inline constexpr uint32_t kRoadCornerZ = 0x001B1610u;
    inline constexpr uint32_t kRoadCornerHeadings = 0x001B1640u;
    inline constexpr uint32_t kRoadShapeCorners = 0x001B1670u;
    // Vec4: the first two members' spots, the rest share the second
    inline constexpr uint32_t kFieldPeopleSpots = 0x001B1460u;
    inline constexpr uint32_t kWastelandStepX = 0x001B1770u;
    inline constexpr uint32_t kWastelandStepZ = 0x001B1780u;
    inline constexpr uint32_t kOfficeHitMeshes = 0x001B1408u;
    inline constexpr uint32_t kOfficeFloorMeshes = 0x001B1428u;
    inline constexpr uint32_t kCityEye = 0x001B2480u;
    inline constexpr uint32_t kCityTarget = 0x001B2490u;
    // The claim spread weights by distance (mm_BldgClaims_Spread, mm_BldgClaims_WeighNeighbours)
    inline constexpr uint32_t kClaimWeights = 0x001B8060u;
    inline constexpr uint32_t kClaimTallies = 0x001F4B90u;        // ClaimTally[], cleared each pass (0x300 bytes)

    // city: function statics of the save, approval and load steps (mm_CityScene_SaveStep, mm_CityScene_UpdateApproval,
    // mm_CityScene_LoadGame): state, started byte, ticks, request, the card, the texts, sprite, fade, timer and handle.
    inline constexpr GuestVar<int32_t> saveStepState{0x001BDC5Cu};
    inline constexpr GuestVar<int8_t> saveStepStarted{0x001BDC60u};
    inline constexpr GuestVar<int32_t> saveStepRequest{0x001BDC64u};
    inline constexpr GuestVar<int32_t> approvalTicks{0x001BDC54u};
    inline constexpr GuestVar<int8_t> approvalStarted{0x001BDC58u};
    inline constexpr GuestVar<int32_t> loadGameState{0x001BDC68u};
    inline constexpr GuestVar<int8_t> loadGameStarted{0x001BDC6Cu};
    // The card port of the save chosen (mm_McManager_GetSaveEntry in the load menu)
    inline constexpr GuestVar<int32_t> loadGameCard{0x001BDA78u};
    // 1 when the title started a loaded game (mm_TitleScene_StartGame); the load's first step then shows the save's
    // description and clears it
    inline constexpr GuestVar<int32_t> loadGameFromTitle{0x001BDA7Cu};
    inline constexpr GuestVar<uint32_t> loadGameInfoText{0x001BDC7Cu};  // the box with the save's description
    inline constexpr GuestVar<uint32_t> loadGameRetryText{0x001BDC78u}; // the retry question's box
    inline constexpr GuestVar<uint32_t> loadGameSprite{0x001BDC80u};
    inline constexpr GuestVar<int32_t> loadGameFade{0x001BDC84u};
    inline constexpr GuestVar<int32_t> loadGameTimer{0x001BDC70u};
    inline constexpr GuestVar<int32_t> loadGameHandle{0x001BDC74u};

    // char: people: an animation's frame step and restart frame (by animation), the schedule's destinations (8 bytes
    // per schedule state), the farm fields' block steps and tile steps, the building list's first distance (halfword
    // per list).
    inline constexpr uint32_t kAnimFrameSteps = 0x001AF080u;
    inline constexpr uint32_t kAnimRestarts = 0x001AF050u;
    inline constexpr uint32_t kScheduleDestinations = 0x001B0BD0u;
    inline constexpr uint32_t kFieldBlockStepX = 0x001B0C50u;
    inline constexpr uint32_t kFieldBlockStepZ = 0x001B0CB0u;
    inline constexpr uint32_t kFieldTileStepX = 0x001B0D10u;
    inline constexpr uint32_t kFieldTileStepZ = 0x001B0D20u;
    inline constexpr uint32_t kBuildingListRanges = 0x001B0D30u;
    inline constexpr uint32_t kMarkerOffsets = 0x001B0DB0u;        // the circle markers' offsets (mm_Markers_ctor)
    // Pointers to the town texts by mission kind (mm_Stage_FormatTargetText)
    inline constexpr uint32_t kGoalTowns = 0x001B5C90u;
    inline constexpr uint32_t kGoalThemes = 0x001B5D30u;           // pointers by theme
    // Mm_Schedule_Evaluate's row bytes: the second member with flag 1, by (family + member) % 3
    inline constexpr uint32_t kScheduleRowsFlagged = 0x001BD9A4u;
    // The second member without the flag (by parity), and adults past the second (% 3)
    inline constexpr uint32_t kScheduleRowsOthers = 0x001BD9A8u;
    inline constexpr uint32_t kScheduleRowsYoung = 0x001BD9ACu;    // members past the second under age group 0x18 (% 3)
    // The CHARTYPE.BIN record count (mm_CharType_LoadAll)
    inline constexpr GuestVar<uint32_t> charClassCount{0x001BDA40u};

    // talk: the dimming colours (Vec4) for someone away and for a person, the asleep building colour, the friendship
    // tier words on the card (pointers), the cards' columns by slot (int) and x places (float), the name entry buffer.
    inline constexpr uint32_t kTalkDimAway = 0x001B1AD0u;
    inline constexpr uint32_t kTalkDimPerson = 0x001B1AE0u;
    inline constexpr uint32_t kTalkAsleepColour = 0x001B1B00u;
    inline constexpr uint32_t kTalkTierTexts = 0x001B1B10u;
    inline constexpr uint32_t kTalkCardColumns = 0x001B1B28u;
    inline constexpr uint32_t kTalkCardX = 0x001B1B38u;
    inline constexpr uint32_t kTalkNameBuffer = 0x001F4E90u;

    // save: the month names and number suffixes the slot text uses (pointers), the scan's clock, the save file's port
    // and name (mm_SaveFile_PrepareDir, mm_SaveFile_Open), the format result, the icon image buffer and the file
    // number.
    inline constexpr uint32_t kMonthNames = 0x001B0B8Cu;
    inline constexpr uint32_t kNumberSuffixes = 0x001B0BC0u;
    inline constexpr uint32_t kSaveClock = 0x001BDBA0u;           // BcdClock
    inline constexpr GuestVar<int32_t> saveFilePort{0x001BDB94u};
    inline constexpr GuestVar<int32_t> saveFileName{0x001BDB98u};
    inline constexpr GuestVar<int32_t> formatResult{0x001BDBA8u};
    inline constexpr uint32_t kIconSysImage = 0x001F65F0u;        // the icon.sys written with a save (0x3C4 bytes)
    inline constexpr uint32_t kIconSysFile = 0x001BDCDCu;         // its file number (McQueue_Open)

    // gfx: the frame counter whose low bit picks the double buffers (counted by mm_MainLoop).
    inline constexpr GuestVar<uint32_t> frameParity{0x001BDCD0u};

    // gfx: the GS packets queued by name (mm_DmaList_Queue*: defaults, clear Z, clear the screen, the two fades and
    // their colour words), the display setup's two GIF tag words, the asset loader's function statics, the instance
    // buffers' second half, the model count, the rain's frame counter (with its started byte), the async model header.
    inline constexpr uint32_t kGsDefaultsPacket = 0x001B2760u;
    inline constexpr uint32_t kClearZPacket = 0x001B26D0u;
    inline constexpr uint32_t kClearScreenPacket = 0x001B28E0u;
    inline constexpr uint32_t kFadePacketA = 0x001B2800u;
    inline constexpr uint32_t kFadeColourA = 0x001B2830u;
    inline constexpr uint32_t kFadePacketB = 0x001B2870u;
    inline constexpr uint32_t kFadeColourB = 0x001B28A0u;
    inline constexpr uint32_t kDisplayGifTagA = 0x001B2540u;
    inline constexpr uint32_t kDisplayGifTagB = 0x001B2548u;
    // Mm_A3DObject_LoadAsync's step (mm_InstanceBase_ctor clears it)
    inline constexpr GuestVar<int32_t> modelLoadState{0x001BDA10u};
    inline constexpr GuestVar<int32_t> modelLoadRequest{0x001BDA14u};
    // CharPartModel/InstanceModel objects made; the first sets up the instance buffers
    inline constexpr GuestVar<int32_t> modelCount{0x001BDA2Cu};
    inline constexpr GuestVar<int32_t> rainFrames{0x001BDAD0u};
    inline constexpr GuestVar<int8_t> rainStarted{0x001BDAD4u};
    inline constexpr uint32_t kAsyncModelHeader = 0x001C5100u;        // A3DModel
    inline constexpr uint32_t kSkyUpload = 0x001B0DD0u;               // the chain the sky packet calls
    // Mm_Gs_InitDisplay's blocks: the setup packet, the display settings, the draw environment
    inline constexpr uint32_t kGsSetupBuffer = 0x001F5AC0u;
    inline constexpr uint32_t kGsDisplaySetup = 0x001F5B10u;
    inline constexpr uint32_t kGsDrawEnv = 0x001F5B20u;
    inline constexpr uint32_t kDmaKickChain = 0x001F8100u;            // the two-tag chain mm_Dma_KickVif1Now sends
    // The VU1 programs' upload packets (docs/RENDER_PIPELINE.md)
    inline constexpr uint32_t kVu1PeopleProgram = 0x001A3C80u;
    inline constexpr uint32_t kVu1BuildingProgram = 0x001A4780u;
    inline constexpr uint32_t kVu1UnlitProgram = 0x001A8330u;
    inline constexpr uint32_t kVu1LitRoadProgram = 0x001A91C0u;
    inline constexpr uint32_t kVu1WaterBottomProgram = 0x001AA340u;

    // app: the double buffer settings and the two half-offset blocks per field parity (sceGsSetDefDBuffDc), and the two
    // words App_ctor sets to 1.0 (Q of the clear colours).
    inline constexpr uint32_t kDisplayBuffers = 0x001F5790u;
    inline constexpr uint32_t kHalfOffsetA0 = 0x001F57F0u;
    inline constexpr uint32_t kHalfOffsetA1 = 0x001F5960u;
    inline constexpr uint32_t kHalfOffsetB0 = 0x001F5870u;
    inline constexpr uint32_t kHalfOffsetB1 = 0x001F59E0u;
    inline constexpr uint32_t kClearQ0 = 0x001F5910u;
    inline constexpr uint32_t kClearQ1 = 0x001F5A80u;

    // sound: the RPC client, send and reply buffers (YYSOUND), and the bound client's server word.
    inline constexpr uint32_t kSoundRpcClient = 0x001FE690u;
    inline constexpr uint32_t kSoundRpcSend = 0x0020B6C0u;
    inline constexpr uint32_t kSoundRpcReply = 0x0020B800u;

    // event: the handler table (a pointer per EventType.handler), the shopping serial, the text buffers.
    inline constexpr uint32_t kEventHandlers = 0x001AF0D0u;
    inline constexpr GuestVar<int32_t> shoppingSerial{0x001BDD5Cu};
    inline constexpr uint32_t kEventTextBuffer = 0x001C5990u;
    inline constexpr uint32_t kEventText = 0x0020B890u;

    // map: the terrain quad words (mm_Terrain_AddQuad): the first word's high bits and the texture coordinate sets.
    inline constexpr uint32_t kTerrainQuadWord = 0x001BD990u;
    inline constexpr uint32_t kTerrainCoordSets = 0x001AF360u;

    // pad: the frame counters, the fault flag and the unused word (mm_Pad_TakeFocus's statics), the act align data, the
    // library's DMA buffers (0x100 bytes per port).
    inline constexpr uint32_t kPadLastFrame = 0x001BDB10u;
    inline constexpr uint32_t kPadFrame = 0x001BDB18u;
    inline constexpr uint32_t kPadFault = 0x001BDB20u;
    inline constexpr uint32_t kPadUnused = 0x001BDB30u;
    inline constexpr uint32_t kPadActAlignData = 0x001BDB28u; // the 6 bytes handed to scePadSetActAlign
    inline constexpr uint32_t kPadPortBuffers = 0x001F38C0u;  // 0x100 bytes per port for the library's DMA

    // text: the powers of ten (nine int32_t).
    inline constexpr uint32_t kPowersOfTen = 0x001B9AB0u; // nine int32_t
}
