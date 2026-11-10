// The game's strings and file paths that ported code uses, by address (SLUS_203.77, see AGENTS.md rule 4).
// Each function has its own copy of a string in the original's data, so a text used in two places can have two
// names. The comments quote only short words and file names.

#pragma once

#include <cstdint>

namespace mm::globals
{
    // Data files the game opens by name (DATA\..., through the file queue).
    inline constexpr uint32_t kPathCharTypeBin = 0x001B5B90u; // DATA\BIN\CHARTYPE.BIN
    inline constexpr uint32_t kPathClaimBin = 0x001B5BB0u;    // DATA\BIN\CLAIM.BIN
    inline constexpr uint32_t kPathStageBin = 0x001B5D50u;    // DATA\BIN\STAGE.BIN
    inline constexpr uint32_t kPathEventBin = 0x001B6370u;    // DATA\BIN\EVENT.BIN
    inline constexpr uint32_t kPathFamilyBin = 0x001B6390u;   // DATA\BIN\FAMILY.BIN
    inline constexpr uint32_t kPathEachStrBin = 0x001B63B0u;  // DATA\BIN\EACHSTR.BIN
    inline constexpr uint32_t kPathGoodsBin = 0x001B63D0u;    // DATA\BIN\GOODS.BIN
    inline constexpr uint32_t kPathScheduleBin = 0x001B7F30u; // DATA\BIN\SCHEDULE.BIN
    inline constexpr uint32_t kPathPostNameBin = 0x001B8040u; // DATA\BIN\POSTNAME.BIN
    inline constexpr uint32_t kPathStrTypeBin = 0x001B8150u;  // DATA\BIN\STRTYPE.BIN
    inline constexpr uint32_t kPathWordsBin = 0x001BAD90u;    // DATA\BIN\WORDS.BIN
    inline constexpr uint32_t kPathWordsEveBin = 0x001BADB0u; // DATA\BIN\WORDSEVE.BIN
    inline constexpr uint32_t kPathWordsComBin = 0x001BADD0u; // DATA\BIN\WORDSCOM.BIN
    inline constexpr uint32_t kPathWordsTenBin = 0x001BADF0u; // DATA\BIN\WORDSTEN.BIN
    inline constexpr uint32_t kPathDestructA3d = 0x001B5EF0u; // DATA\A3D\DESTRUCT.A3D
    inline constexpr uint32_t kPathCharA3d = 0x001B7B50u;     // DATA\A3D\CHAR.A3D
    inline constexpr uint32_t kPathCharObjA3d = 0x001B7B70u;  // DATA\A3D\CHAROBJ.A3D
    inline constexpr uint32_t kPathObjectA3d = 0x001B7B90u;   // DATA\A3D\OBJECT.A3D
    inline constexpr uint32_t kPathSkyA3d = 0x001B7F50u;      // DATA\A3D\SKY.A3D
    inline constexpr uint32_t kPathJunkA3d = 0x001B8D00u;     // DATA\A3D\JUNK.A3D
    inline constexpr uint32_t kPathGroundTex = 0x001B8D20u;   // DATA\GROUND.TEX
    inline constexpr uint32_t kPathWaterTex = 0x001B8D30u;    // DATA\WATER.TEX
    inline constexpr uint32_t kPathSpotTex = 0x001B8D40u;     // DATA\SPOT.TEX
    inline constexpr uint32_t kPathSplashTex = 0x001B8D50u;   // DATA\SPLASH.TEX
    inline constexpr uint32_t kPathStaticIco = 0x001B7E30u;   // DATA\STATIC.ICO (copied to the card as the save icon)

    // The title and stage scenes' pictures and models (DATA\A3D\INTRO\...).
    inline constexpr uint32_t kPathEndRollTex = 0x001B7350u;  // END_ROLL.TEX
    inline constexpr uint32_t kPathLoadingTTex = 0x001B7F70u; // LOADINGT.TEX
    inline constexpr uint32_t kPathNatsumeTex = 0x001B8C80u;  // NATSUME.TEX
    inline constexpr uint32_t kPathMfUsTex = 0x001B8CA0u;     // MF_US.TEX
    inline constexpr uint32_t kPathLogoTex = 0x001B8CC0u;     // LOGO.TEX
    inline constexpr uint32_t kPathLoadA3d = 0x001B8D60u;     // LOAD.A3D
    inline constexpr uint32_t kPathTitleA3d = 0x001B8D80u;    // TITLE.A3D
    inline constexpr uint32_t kPathTitleTex = 0x001B8DA0u;    // TITLE.TEX
    inline constexpr uint32_t kPathTwinkleTex = 0x001B8DC0u;  // TWINKLE.TEX
    inline constexpr uint32_t kPathHajimeTex = 0x001B8DE0u;   // HAJIME.TEX
    inline constexpr uint32_t kPathTuzukiTex = 0x001B8E00u;   // TUZUKI.TEX
    inline constexpr uint32_t kPathLoadingTex = 0x001B8E20u;  // LOADING.TEX
    inline constexpr uint32_t kPathGaugeOffTex = 0x001B8E40u; // GAUGEOFF.TEX
    inline constexpr uint32_t kPathGaugeOnTex = 0x001B8E60u;  // GAUGEON.TEX
    inline constexpr uint32_t kPathReportTex = 0x001B9940u;   // REPORT.TEX

    // Disc paths: the device prefix and version suffix File_Open adds, the font file's parts (DATA\FONT, the size,
    // .GF).
    inline constexpr uint32_t kDiscPrefix = 0x001B9AE8u;     // "cdrom0:\"
    inline constexpr uint32_t kDiscVersion = 0x001B9AF8u;    // ;1
    inline constexpr uint32_t kFontPathStart = 0x001B9B08u;  // DATA\FONT
    inline constexpr uint32_t kFontPathNumber = 0x001B9B00u; // %d
    inline constexpr uint32_t kFontPathEnd = 0x001B9B18u;    // .GF

    // IOP modules loaded at start-up (App_ctor, Sound_LoadModules) and the messages printed when one fails.
    inline constexpr uint32_t kPathSio2manIrx = 0x001B9B20u;    // SIO2MAN.IRX
    inline constexpr uint32_t kTextNoSio2man = 0x001B9B40u;
    inline constexpr uint32_t kPathMcmanIrx = 0x001B9B60u;      // MCMAN.IRX
    inline constexpr uint32_t kTextNoMcman = 0x001B9B80u;
    inline constexpr uint32_t kPathMcservIrx = 0x001B9BA0u;     // MCSERV.IRX
    inline constexpr uint32_t kTextNoMcserv = 0x001B9BC0u;
    inline constexpr uint32_t kPathPadmanIrx = 0x001B9BE0u;     // PADMAN.IRX
    inline constexpr uint32_t kTextNoPadman = 0x001B9C00u;
    inline constexpr uint32_t kPathIoprpImg = 0x001B9C20u;      // IOPRP243.IMG, the IOP replacement image
    inline constexpr uint32_t kPathLibsdIrx = 0x001B9EC0u;      // LIBSD.IRX
    inline constexpr uint32_t kPathSdrdrvIrx = 0x001B9F00u;     // SDRDRV.IRX
    inline constexpr uint32_t kPathModmidiIrx = 0x001B9F40u;    // MODMIDI.IRX
    inline constexpr uint32_t kPathModhsynIrx = 0x001B9F80u;    // MODHSYN.IRX
    inline constexpr uint32_t kPathModseinIrx = 0x001B9FC0u;    // MODSEIN.IRX
    inline constexpr uint32_t kPathYysoundIrx = 0x001BA000u;    // YYSOUND.IRX
    inline constexpr uint32_t kTextSifBindFailed = 0x001B9EA0u; // the sound RPC bind failed (Sound_Init)

    // sound: the bank folders on the disc (SQ, HD, BD, the root) and the version suffix SoundCmd_Flush builds paths
    // with, its two debug prints when the ring overflows, and the bank files the sound test and the stage scene open.
    inline constexpr uint32_t kSoundPathSq = 0x001B9D50u; // "cdrom0:\DATA\SOUND\SQ\"
    inline constexpr uint32_t kSoundPathHd = 0x001B9D70u;
    inline constexpr uint32_t kSoundPathBd = 0x001B9D90u;
    inline constexpr uint32_t kSoundPathRoot = 0x001B9DB0u;
    inline constexpr uint32_t kSoundPathVersion = 0x001B9D68u; // ;1
    inline constexpr uint32_t kSoundTextUrgentCut = 0x001B9DD0u;
    inline constexpr uint32_t kSoundTextNumber = 0x001B9DF0u; // %d and a newline
    inline constexpr uint32_t kSoundTextManyRequests = 0x001B9E00u;
    inline constexpr uint32_t kPlaylandBankSq = 0x001B2E20u; // PLAYLAND.SQ (then .HD, .BD 0x10 apart)
    inline constexpr uint32_t kTitleBankSq = 0x001B8728u; // TITLE.SQ
    inline constexpr uint32_t kTitleBankHd = 0x001B8738u; // TITLE.HD
    inline constexpr uint32_t kTitleBankBd = 0x001B8748u; // TITLE.BD
    inline constexpr uint32_t kTelopBankName = 0x001B72F0u; // TELOP%d. (the telop's music bank; SQ, HD or BD added)
    inline constexpr uint32_t kTelopBankSq = 0x001B7300u; // SQ
    inline constexpr uint32_t kTelopBankHd = 0x001B7308u; // HD
    inline constexpr uint32_t kTelopBankBd = 0x001B7310u; // BD

    // bag menu (mm_BagMenu_*): the selected item's actions and the list line's count.
    inline constexpr uint32_t kBagTextEmpty = 0x001B5B10u;   // Empty bag
    inline constexpr uint32_t kBagTextEat = 0x001B5B20u;     // Eat
    inline constexpr uint32_t kBagTextDrink = 0x001B5B28u;   // Drink
    inline constexpr uint32_t kBagTextClothes = 0x001B5B30u; // New clothes
    inline constexpr uint32_t kBagTextSetUp = 0x001B5B40u;   // Set up
    inline constexpr uint32_t kBagTextCount = 0x001B5B48u;   // " * %d"
    inline constexpr uint32_t kBagTextClose = 0x001B5B50u;   // Close
    inline constexpr uint32_t kBagTextDiscard = 0x001B5B58u; // Discard
    inline constexpr uint32_t kBagTextKeep = 0x001B5B60u;    // Keep it

    // stage goals (mm_Stage_FormatGoalText, mm_Stage_FormatTargetText): the town each mission theme asks for, the parts
    // of the goal and target lines.
    inline constexpr uint32_t kGoalTextRich = 0x001B5C40u;         // high-class town with rich people
    inline constexpr uint32_t kGoalTextBusiness = 0x001B5DB0u;
    inline constexpr uint32_t kGoalTextLeisure = 0x001B5DD0u;
    inline constexpr uint32_t kGoalTextFishing = 0x001B5E00u;
    inline constexpr uint32_t kGoalTextFarming = 0x001B5E20u;
    inline constexpr uint32_t kGoalTextIndustry = 0x001B5E40u;
    inline constexpr uint32_t kGoalTextPeople = 0x001B5D68u;       // %d people
    inline constexpr uint32_t kGoalTextThemeShare = 0x001B5D80u;
    inline constexpr uint32_t kGoalTextEnd = 0x001B5E60u;          // .
    inline constexpr uint32_t kTargetTextPeople = 0x001B5E70u;
    inline constexpr uint32_t kTargetTextThemeShare = 0x001B5EB0u;

    // ui: the status panel's texts (mm_StatusPanel_*).
    inline constexpr uint32_t kStatusTextHeading = 0x001B6490u;      // STATUS
    inline constexpr uint32_t kStatusTextReached = 0x001B64B0u;      // the population has reached the target
    inline constexpr uint32_t kStatusTextPeopleNeeded = 0x001B64E0u;
    inline constexpr uint32_t kStatusTextThemeShare = 0x001B6510u;
    inline constexpr uint32_t kStatusTextSalary = 0x001B6538u;       // Salary%s
    inline constexpr uint32_t kStatusTextMoney = 0x001B6548u;        // Money%s
    inline constexpr uint32_t kStatusTextStrength = 0x001B6550u;     // Strength

    // ui: the "please wait" text (mm_WaitText_Create), the empty texts some windows clear their boxes with (each
    // function its own copy), the message window's word breaks (space, newline, return).
    inline constexpr uint32_t kWaitText = 0x001B65F0u;           // Please wait....
    inline constexpr uint32_t kListMenuEmptyText = 0x001B6600u;
    inline constexpr uint32_t kMsgWinEmptyText = 0x001B5A80u;
    inline constexpr uint32_t kCursorEmptyText = 0x001B5ED8u;
    inline constexpr uint32_t kLaptopEmptyText = 0x001B6560u;
    inline constexpr uint32_t kWantsEmptyText = 0x001B73A8u;
    inline constexpr uint32_t kPauseEmptyText = 0x001B73C0u;
    inline constexpr uint32_t kNameEntryEmptyText = 0x001B7F90u;
    inline constexpr uint32_t kTownPagesEmptyText = 0x001B8E80u;
    inline constexpr uint32_t kMsgWinWordBreaks = 0x001B5A78u;

    // ui: the message window's bracket marks (mm_MsgWin_TypeNext): "<>()", read by character from each start.
    inline constexpr uint32_t kMsgWinOpenAngle = 0x001B5A70u;  // "<" (with ">()" after it)
    inline constexpr uint32_t kMsgWinCloseAngle = 0x001B5A71u; // ">"
    inline constexpr uint32_t kMsgWinOpenParen = 0x001B5A72u;  // "("
    inline constexpr uint32_t kMsgWinCloseParen = 0x001B5A73u; // ")"

    // ui: the stage scene: the stage title, the approval rate page, the mission briefing and the ending telop's thanks
    // ("Thanks to Mr. " and the mayor's names, then "'" and "s" unless the name ends in s).
    inline constexpr uint32_t kStageTextStage = 0x001B7210u;       // STAGE
    inline constexpr uint32_t kStageTextApproval = 0x001B7220u;
    inline constexpr uint32_t kStageTextIndent = 0x001B7238u;      // a newline and two spaces
    inline constexpr uint32_t kBriefingTextMission = 0x001B7270u;
    inline constexpr uint32_t kBriefingTextTarget = 0x001B72A0u;
    inline constexpr uint32_t kBriefingTextGoodLuck = 0x001B72D0u;
    inline constexpr uint32_t kTelopTextThanks = 0x001B7318u;      // Thanks to
    inline constexpr uint32_t kTelopTextYour = 0x001B7328u;        // your (no name entered)
    inline constexpr uint32_t kTelopTextMister = 0x001B7330u;      // Mr.
    inline constexpr uint32_t kTelopTextApostrophe = 0x001B7338u;  // '
    inline constexpr uint32_t kTelopTextS = 0x001B7340u;           // s

    // ui: the city cursor's name line (mm_CityCursor_UpdateInfo).
    inline constexpr uint32_t kCursorTextName = 0x001B5EE0u; // %s %s

    // ui: the wants window's line (mm_WantsWin_FormatLine).
    inline constexpr uint32_t kWantsTextLine = 0x001B73B0u; // %s %s wants %s.

    // ui: the pause menu: the default save title (full-width), the menu words, the memory card messages.
    inline constexpr uint32_t kPauseSaveTitle = 0x001B73D0u;        // Metropolismania in full-width letters
    inline constexpr uint32_t kPauseTextResume = 0x001B73F0u;       // Resume
    inline constexpr uint32_t kPauseTextSave = 0x001B73F8u;         // Save
    inline constexpr uint32_t kPauseTextQuit = 0x001B7400u;         // Quit
    inline constexpr uint32_t kPauseTextQuitSure = 0x001B7410u;
    inline constexpr uint32_t kPauseTextYes = 0x001B7438u;          // Yes
    inline constexpr uint32_t kPauseTextNo = 0x001B7440u;           // No
    inline constexpr uint32_t kPauseTextNoCard = 0x001B7450u;
    inline constexpr uint32_t kPauseTextFormatted = 0x001B74A0u;
    inline constexpr uint32_t kPauseTextSaveFailed = 0x001B74F0u;
    inline constexpr uint32_t kPauseTextFormatFailed = 0x001B7510u;
    inline constexpr uint32_t kPauseTextNoCardAgain = 0x001B7530u;
    inline constexpr uint32_t kPauseTextDigit2 = 0x001B7580u;       // 2
    inline constexpr uint32_t kPauseTextDigit1 = 0x001B7588u;       // 1
    inline constexpr uint32_t kPauseTextPeriod = 0x001B7590u;       // .
    inline constexpr uint32_t kPauseTextCardInSlot = 0x001B75A0u;
    inline constexpr uint32_t kPauseTextSlot1 = 0x001B75E0u;        // " 1"
    inline constexpr uint32_t kPauseTextSlot2 = 0x001B75E8u;        // " 2"
    inline constexpr uint32_t kPauseTextPlural = 0x001B75F0u;       // s
    inline constexpr uint32_t kPauseTextNoSpace = 0x001B7600u;
    inline constexpr uint32_t kPauseTextDots = 0x001B7688u;         // ..
    inline constexpr uint32_t kPauseTextSaving = 0x001B7690u;
    inline constexpr uint32_t kPauseTextDoNotRemove = 0x001B76E0u;
    inline constexpr uint32_t kPauseTextChecking = 0x001B7750u;
    inline constexpr uint32_t kPauseTextFormatting = 0x001B77A0u;
    inline constexpr uint32_t kPauseTextSlotNeeds = 0x001B7810u;
    inline constexpr uint32_t kPauseTextNeedsFormat = 0x001B7850u;
    inline constexpr uint32_t kPauseTextOverwrite = 0x001B78B0u;
    inline constexpr uint32_t kPauseTextNewSave = 0x001B78E0u;
    inline constexpr uint32_t kPauseTextNewSaveAgain = 0x001B7960u;

    // ui: the applicant card (mm_Applicants_BuildInfo).
    inline constexpr uint32_t kApplicantTextPlayer = 0x001B7A30u;   // Player
    inline constexpr uint32_t kApplicantTextName = 0x001B7A38u;     // %s %s
    inline constexpr uint32_t kApplicantTextUnknown = 0x001B7A40u;
    inline constexpr uint32_t kApplicantTextLines = 0x001B7A60u;
    inline constexpr uint32_t kApplicantTextDate = 0x001B7A80u;
    inline constexpr uint32_t kApplicantTextBuilding = 0x001B7A98u; // " (Bldg.)"
    inline constexpr uint32_t kApplicantTextUnit = 0x001B7AA8u;     // " (Unit)"
    inline constexpr uint32_t kApplicantTextNewline = 0x001B7AB0u;
    inline constexpr uint32_t kApplicantTextMetres = 0x001B7AB8u;   // %sm

    // save: the loaded save's description (mm_McManager_FormatSaveInfo), a slot's line (mm_McManager_FormatSlotLine,
    // mm_McManager_FormatSlotText), the card files (icon, icon.sys, its magic and title).
    inline constexpr uint32_t kSaveInfoStage = 0x001B7D40u;      // Stage
    inline constexpr uint32_t kSaveInfoIndent = 0x001B7D48u;
    inline constexpr uint32_t kSaveInfoMonth = 0x001B7D50u;
    inline constexpr uint32_t kSaveInfoYear = 0x001B7D60u;
    inline constexpr uint32_t kSaveInfoPopulation = 0x001B7D70u;
    inline constexpr uint32_t kSaveInfoApproval = 0x001B7D80u;
    inline constexpr uint32_t kSaveInfoPercent = 0x001B7D98u;    // %
    inline constexpr uint32_t kSlotLineStage = 0x001B7DA0u;
    inline constexpr uint32_t kSlotLineName = 0x001B7DA8u;
    inline constexpr uint32_t kSlotLinePopulation = 0x001B7DB0u;
    inline constexpr uint32_t kSlotLineApproval = 0x001B7DC0u;
    inline constexpr uint32_t kSlotLinePercent = 0x001B7DD0u;
    inline constexpr uint32_t kSlotLineNewest = 0x001B7DE0u;     // *Newest*
    inline constexpr uint32_t kSlotTextFormat = 0x001B7EA0u;
    inline constexpr uint32_t kStaticIcoName = 0x001B7E40u;      // STATIC.ICO (mm_SaveFile_PrepareDir)
    // Metropolismania in full-width letters (mm_McSave_WriteIconSys)
    inline constexpr uint32_t kIconSysTitle = 0x001B9C40u;
    inline constexpr uint32_t kIconSysMagic = 0x001B9C60u;       // PS2D
    inline constexpr uint32_t kIconSysIconName = 0x001B9C68u;    // STATIC.ICO
    inline constexpr uint32_t kIconSysFileName = 0x001B9C78u;    // icon.sys

    // ui: the name entry's cursor mark (mm_NameEntry_Setup, mm_NameEntry_Draw).
    inline constexpr uint32_t kNameEntryMark = 0x001B8038u; // *

    // char: the phone call window (mm_PhoneCall_Update).
    inline constexpr uint32_t kPhoneTextButtons = 0x001B8170u;
    inline constexpr uint32_t kPhoneTextBoss = 0x001B8198u;     // Call from boss
    inline constexpr uint32_t kPhoneTextStranger = 0x001B81B0u; // Call from stranger
    inline constexpr uint32_t kPhoneTextCaller = 0x001B81C8u;   // Call from %s

    // ui: the notices (mm_Notice_Show): 48 bytes per notice id, the first "A bit hungry.".
    inline constexpr uint32_t kNoticeTexts = 0x001B81E0u;

    // ui: the sound test's pages (mm_SoundTest_*).
    inline constexpr uint32_t kSoundTestHeading = 0x001B86F0u;
    inline constexpr uint32_t kSoundTestWait = 0x001B8710u;
    inline constexpr uint32_t kSoundTestBgmText = 0x001B8760u;
    inline constexpr uint32_t kSoundTestTempo = 0x001B87E0u;
    inline constexpr uint32_t kSoundTestPlus = 0x001B87F8u;      // " " (before a value that is not negative)
    inline constexpr uint32_t kSoundTestMinus = 0x001B8800u;     // -
    inline constexpr uint32_t kSoundTestBgmNumber = 0x001B8808u;
    inline constexpr uint32_t kSoundTestSeText = 0x001B8820u;
    inline constexpr uint32_t kSoundTestRight = 0x001B88A8u;     // R
    inline constexpr uint32_t kSoundTestLeft = 0x001B88B0u;      // L
    inline constexpr uint32_t kSoundTestCentre = 0x001B88B8u;    // Center
    inline constexpr uint32_t kSoundTestDistance = 0x001B88C0u;
    inline constexpr uint32_t kSoundTestNote = 0x001B88E0u;
    inline constexpr uint32_t kSoundTestPitch = 0x001B8900u;
    inline constexpr uint32_t kSoundTestSeNumber = 0x001B8918u;

    // title: the load menu's card messages (mm_Title_LoadMenu) and the loading text (mm_TitleScene_Load).
    inline constexpr uint32_t kLoadMenuTextChecking = 0x001B8930u;
    inline constexpr uint32_t kLoadMenuTextNoSave = 0x001B8980u;
    inline constexpr uint32_t kTitleTextLoading = 0x001B8CE0u;

    // ui: the Town's Pages entries (mm_TownPages_FillPage): two marks, the index heading and a family's lines.
    inline constexpr uint32_t kTownPagesMarkName = 0x001B8F98u;
    inline constexpr uint32_t kTownPagesMark = 0x001B8FA0u;
    inline constexpr uint32_t kTownPagesIndex = 0x001B8FA8u;
    inline constexpr uint32_t kTownPagesEntry = 0x001B8FB0u;

    // talk: the separator between people's names (mm_TalkView_DrawPeople), the talk card's lines (mm_TalkCard_FillText:
    // the name and age, a line, when the player last met the person) and the blank a name entry starts from.
    inline constexpr uint32_t kTalkTextSeparator = 0x001B8FF8u;      // " "
    inline constexpr uint32_t kTalkCardName = 0x001B9048u;
    inline constexpr uint32_t kTalkCardLine = 0x001B9058u;           // %s and a newline
    inline constexpr uint32_t kTalkCardNextTown = 0x001B9060u;
    inline constexpr uint32_t kTalkCardJustIntroduced = 0x001B9080u;
    inline constexpr uint32_t kTalkCardMetMomentAgo = 0x001B90A0u;
    inline constexpr uint32_t kTalkCardMetToday = 0x001B90B8u;
    inline constexpr uint32_t kTalkCardFirstToday = 0x001B90D0u;
    inline constexpr uint32_t kTalkCardBeenAwhile = 0x001B90E8u;
    inline constexpr uint32_t kTalkTextBlank = 0x001B90F8u;          // *

    // city: loading a save (mm_CityScene_LoadGame): its messages and the slot digits.
    inline constexpr uint32_t kLoadTextLoading = 0x001B98E0u;
    inline constexpr uint32_t kLoadTextNoCard = 0x001B9960u;
    inline constexpr uint32_t kLoadTextSlot2 = 0x001B99B8u;    // 2.
    inline constexpr uint32_t kLoadTextSlot1 = 0x001B99C0u;    // 1.
    inline constexpr uint32_t kLoadTextDots = 0x001B99C8u;     // ..
    inline constexpr uint32_t kLoadTextRetry = 0x001B99D0u;
    inline constexpr uint32_t kLoadTextFailed = 0x001B9A10u;
    inline constexpr uint32_t kLoadTextChecking = 0x001B9A30u;

    // text: the dash between two times (mm_Text_FormatTimeRange; a glyph of the game's font).
    inline constexpr uint32_t kTimeRangeDash = 0x001B9AD8u;

    // words: the choice "Don't buy" (mm_Words_GetChoiceText), a space and an apostrophe (mm_Words_AppendClipped), a
    // newline (mm_Words_StepImpl).
    inline constexpr uint32_t kWordsTextDontBuy = 0x001BAD68u;    // Don't buy
    inline constexpr uint32_t kWordsTextSpace = 0x001BAD78u;
    inline constexpr uint32_t kWordsTextApostrophe = 0x001BAD80u;
    inline constexpr uint32_t kWordsTextNewline = 0x001BAD88u;

    // gfx: the error mm_A3DObject_ChangeLuminosity prints for a node it cannot find.
    inline constexpr uint32_t kChangeLuminosityError = 0x001B5A90u;
}
