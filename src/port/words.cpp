// Talk script callbacks (WORDS): the conditions, texts and actions a talk script calls through the tables at
// 0x1B9630, 0x1B9570 and 0x1B9100 (mm_Words_CallCond, mm_Words_CallText, mm_Words_CallAct). The interpreter uses
// each call's v0, so every callback returns what the original leaves there: its own value, the value of the
// function it hands on to, or v0 as it came in. Callbacks that reach blocking code (mm_Family_Get,
// mm_Event_FormatText, mm_WordsFn_SuppliesWantBody) wait for the threading phase.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

namespace mm
{
    namespace
    {
        GuestPtr<EventMgr> eventsOf(GuestPtr<WordsCtx> ctx) { return ctx->city->events; }
    }

    // ---- conditions and values

    int WordsFn_Arg4(GuestPtr<WordsCtx> ctx) { return ctx->unk_04; }
    int WordsFn_Flag134(GuestPtr<WordsCtx> ctx) { return ctx->flag134; }
    int WordsFn_False(GuestPtr<WordsCtx>) { return 0; }
    int WordsFn_FamilyHasLeft(GuestPtr<WordsCtx> ctx) { return Family_HasLeft(ctx->familyA); }
    int WordsFn_FamilyInCity(GuestPtr<WordsCtx> ctx) { return Family_InCity(ctx->familyA); }
    int WordsFn_ScoreA8Tier(GuestPtr<WordsCtx> ctx) { return CityScene_ScoreA8Tier(ctx->city); }
    int WordsFn_ScoreA0Tier(GuestPtr<WordsCtx> ctx) { return CityScene_ScoreA0Tier(ctx->city); }
    int WordsFn_WeatherToday(GuestPtr<WordsCtx> ctx) { return ctx->city->weatherToday; }
    int WordsFn_WeatherTomorrow(GuestPtr<WordsCtx> ctx) { return ctx->city->weatherTomorrow; }
    int WordsFn_StaminaZero(GuestPtr<WordsCtx> ctx) { return Stamina_Zero(ctx->city->stamina); }
    int WordsFn_StaminaTier(GuestPtr<WordsCtx> ctx) { return Stamina_HungerLevel(ctx->city->stamina); }
    int WordsFn_IsApplicant(GuestPtr<WordsCtx> ctx) { return Applicants_Find(ctx->city->applicants, ctx->familyA); }
    int WordsFn_BeforeSix(GuestPtr<WordsCtx> ctx) { return ctx->city->clock < 21600 ? 1 : 0; }
    int WordsFn_CityFlag2(GuestPtr<WordsCtx> ctx) { return (ctx->city->flags & 2u) != 0 ? 1 : 0; }
    int WordsFn_Month(GuestPtr<WordsCtx> ctx) { return ctx->city->monthCount % 12; }
    int WordsFn_PopulationRank(GuestPtr<WordsCtx> ctx) { return CityScene_PopulationRank(ctx->city) - 1; }
    int WordsFn_EventsActive(GuestPtr<WordsCtx> ctx) { return Event_CountActive(eventsOf(ctx)) > 0 ? 1 : 0; }
    int WordsFn_EventA4Var(GuestPtr<WordsCtx> ctx) { return eventsOf(ctx)->talkA->var; }
    int WordsFn_EventA8Var(GuestPtr<WordsCtx> ctx) { return eventsOf(ctx)->talkB->var; }

    // The talk scripts' own variable (a halfword of the stage state at +0x282).
    int WordsFn_GetVar1C5602(GuestPtr<WordsCtx>) { return globals::talkScriptVar.get(); }

    // Family A's id modulo the argument (with the EE's rule for a zero divisor: the id itself).
    int WordsFn_FamilyModArg(GuestPtr<WordsCtx> ctx)
    {
        const int32_t family = ctx->familyA, divisor = ctx->arg;
        return divisor != 0 ? family % divisor : family;
    }

    // Person A's flag bit `arg` (0..15).
    int WordsFn_PersonFlagBit(GuestPtr<WordsCtx> ctx)
    {
        return (ctx->personA->flags & (1u << (static_cast<uint32_t>(ctx->arg) & 31u))) != 0 ? 1 : 0;
    }

    // Steps left of the event in the talk's event slot (3 - its stage).
    int WordsFn_EventCountdown(GuestPtr<WordsCtx> ctx)
    {
        const uint32_t entry = eventsOf(ctx).address + static_cast<uint32_t>(ctx->eventSlot) * 0x20u;
        return 3 - port::load<int8_t>(entry + 5u);
    }

    // Whether family B (arg non-zero) or family A is known through the Town's Pages.
    int WordsFn_FamilyKnown(GuestPtr<WordsCtx> ctx)
    {
        const uint16_t family = ctx->arg != 0 ? ctx->familyB : ctx->familyA;
        return Family_IsKnown(family);
    }

    // ---- texts

    // The name slot `arg` of the stage state (0x10 bytes each).
    GuestPtr<char> WordsFn_TextNameSlot(GuestPtr<WordsCtx> ctx)
    {
        return GuestPtr<char>{globals::kStageState + static_cast<uint32_t>(ctx->arg) * 0x10u};
    }



    // ---- actions

    int WordsFn_SetCancelAllowed(GuestPtr<WordsCtx> ctx)
    {
        ctx->cancelAllowed = ctx->arg;
        return port::guestV0();
    }

    int WordsFn_SetVar1C5602(GuestPtr<WordsCtx> ctx)
    {
        globals::talkScriptVar.set(ctx->arg);
        return port::guestV0();
    }

    int WordsFn_CitySetFlag40(GuestPtr<WordsCtx> ctx)
    {
        ctx->city->flags |= 0x40u;
        return port::guestV0();
    }

    int WordsFn_ClearPersonFlag20(GuestPtr<WordsCtx> ctx)
    {
        ctx->personA->flags &= 0xFFDFu;
        return port::guestV0();
    }

    int WordsFn_SetEventA4Var(GuestPtr<WordsCtx> ctx)
    {
        eventsOf(ctx)->talkA->var = static_cast<uint8_t>(ctx->arg);
        return port::guestV0();
    }

    int WordsFn_SetEventA8Var(GuestPtr<WordsCtx> ctx)
    {
        eventsOf(ctx)->talkB->var = static_cast<uint8_t>(ctx->arg);
        return port::guestV0();
    }

    int WordsFn_PlaySe(GuestPtr<WordsCtx> ctx) { return Sound_PlaySe(ctx->arg & 0xFF, 0, 0x3C, 0.0f, 0.0f); }

    // The functions handed on to below return nothing the port knows of, but the script uses what they leave in
    // v0, so they are called through the dispatcher and their v0 passed back.
    int WordsFn_TalkEndChoosing(GuestPtr<WordsCtx> ctx)
    {
        return port::callGuest<int>(addr::CityScene_TalkEndChoosing, ctx->city);
    }

    int WordsFn_EventQueueShift(GuestPtr<WordsCtx> ctx) { return port::callGuest<int>(addr::Event_ClearList, eventsOf(ctx)); }

    // End the talk's first (A4) or second (A8) event, finishing it or abandoning it.
    int WordsFn_EndEventA4Result(GuestPtr<WordsCtx> ctx) { return port::callGuest<int>(addr::Event_Finish, eventsOf(ctx), ctx->city, 0); }
    int WordsFn_EndEventA4(GuestPtr<WordsCtx> ctx) { return port::callGuest<int>(addr::Event_Abort, eventsOf(ctx), ctx->city, 0); }
    int WordsFn_EndEventA8Result(GuestPtr<WordsCtx> ctx) { return port::callGuest<int>(addr::Event_Finish, eventsOf(ctx), ctx->city, 1); }
    int WordsFn_EndEventA8(GuestPtr<WordsCtx> ctx) { return port::callGuest<int>(addr::Event_Abort, eventsOf(ctx), ctx->city, 1); }

    int WordsFn_StartEventA4(GuestPtr<WordsCtx> ctx)
    {
        return port::callGuest<int>(addr::Event_CallHandlerSlot, eventsOf(ctx), ctx->city, 0, ctx->personA);
    }

    int WordsFn_EventA4Slot10(GuestPtr<WordsCtx> ctx)
    {
        return port::callGuest<int>(addr::Event_CallSlot10, eventsOf(ctx), ctx->city, 0, static_cast<int>(ctx->arg));
    }

    int WordsFn_EventA8Slot10(GuestPtr<WordsCtx> ctx)
    {
        return port::callGuest<int>(addr::Event_CallSlot10, eventsOf(ctx), ctx->city, 1, static_cast<int>(ctx->arg));
    }

    // Talk_SetPose leaves v0 alone, so the result is the city pointer the original holds in v0 when it hands on.
    int WordsFn_SetPose(GuestPtr<WordsCtx> ctx)
    {
        port::setGuestV0(static_cast<int32_t>(ctx->city.address));
        return port::callGuest<int>(addr::Talk_SetPose, ctx->city->talk, static_cast<int>(ctx->familyA), static_cast<int>(ctx->memberA),
                                    static_cast<int>(ctx->arg));
    }

    // Selects actor `arg` of the talk's first event as the person talked to.
    int WordsFn_TalkSelectActor(GuestPtr<WordsCtx> ctx)
    {
        const uint32_t actor = eventsOf(ctx)->talkA.address + static_cast<uint32_t>(ctx->arg) * 8u + 0x10u;
        return port::callGuest<int>(addr::CityScene_TalkSelectPerson, ctx->city, static_cast<int>(port::load<uint16_t>(actor)),
                                    static_cast<int>(port::load<uint16_t>(actor + 2u)));
    }

    // ---- second group: conditions and values

    namespace
    {
        // Family and member A, or B when the argument is set.
        struct Who
        {
            uint16_t family, member;
        };
        Who whoByArg(GuestPtr<WordsCtx> ctx) { return ctx->arg != 0 ? Who{ctx->familyB, ctx->memberB} : Who{ctx->familyA, ctx->memberA}; }
        GuestPtr<MapObject> buildingByArg(GuestPtr<WordsCtx> ctx) { return ctx->arg != 0 ? ctx->buildingB : ctx->buildingA; }
        GuestPtr<BuildingType> typeOfObject(GuestPtr<MapObject> obj) { return GuestPtr<BuildingType>{globals::buildingTypeAddress(obj->type)}; }
        GuestPtr<BuildingType> typeAt(int index) { return GuestPtr<BuildingType>{globals::buildingTypeAddress(static_cast<uint32_t>(index))}; }
        // Row `kind` of the event table (0x4C bytes each).
        uint32_t eventType(uint8_t kind) { return globals::eventTypes.get() + 0x4Cu * kind; }
        // Bit `index` of a bitset of 32-bit words, with C's signed division for the word.
        uint32_t bitWord(uint32_t bitset, int index) { return bitset + static_cast<uint32_t>(4 * (index / 32)); }
        uint32_t bitMask(int index) { return 1u << (static_cast<uint32_t>(index) & 31u); }
        constexpr uint32_t kStoryFlagsA = 0x001C5520u, kStoryFlagsB = 0x001C5530u;
        constexpr uint32_t kTextBuffer = 0x001F4EB0u;  // shared output of the formatting texts
        int32_t lowWord(int64_t value) { return static_cast<int32_t>(value); }
        // The EE's remainder (the dividend itself for a zero divisor).
        int32_t eeRemainder(int32_t value, int32_t divisor) { return divisor == 0 ? value : (divisor == -1 ? 0 : value % divisor); }
    }

    int WordsFn_FriendshipBand(GuestPtr<WordsCtx> ctx)
    {
        const Who who = whoByArg(ctx);
        return Family_GetFriendshipBand(who.family, who.member);
    }

    int WordsFn_FriendshipTier(GuestPtr<WordsCtx> ctx)
    {
        const Who who = whoByArg(ctx);
        const int friendship = Family_GetFriendship(who.family, who.member);
        return friendship < 48 ? 0 : (friendship < 100 ? 1 : 2);
    }

    int WordsFn_MeterAgrees(GuestPtr<WordsCtx> ctx)
    {
        const Who who = whoByArg(ctx);
        return Family_MeterAgrees(who.family, who.member, ctx->city->approval);
    }

    int WordsFn_BuildingFlag100(GuestPtr<WordsCtx> ctx) { return (buildingByArg(ctx)->flags & 0x100u) != 0 ? 1 : 0; }
    int WordsFn_BuildingFlag2(GuestPtr<WordsCtx> ctx) { return (buildingByArg(ctx)->flags & 2u) != 0 ? 1 : 0; }

    int WordsFn_PersonFlag10(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<MapPerson> person = ctx->arg != 0 ? ctx->personB : ctx->personA;
        return (person->flags & 0x10u) != 0 ? 1 : 0;
    }

    int WordsFn_NameSlotSet(GuestPtr<WordsCtx> ctx)
    {
        return sdk::strlen(globals::kStageState + static_cast<uint32_t>(ctx->arg) * 0x10u) != 0 ? 1 : 0;
    }

    // Applicants: none 0, up to 8 1, up to 31 2, more 3.
    int WordsFn_ApplicantCountTier(GuestPtr<WordsCtx> ctx)
    {
        const int count = ctx->city->applicants->count;
        if (count == 0)
            return 0;
        if (count >= 32)
            return 3;
        return count < 9 ? 1 : 2;
    }

    // The city's approval tier with 2 and 3 turned into 1 and 2.
    int WordsFn_ApprovalTier(GuestPtr<WordsCtx> ctx)
    {
        const int tier = CityScene_ApprovalTier(ctx->city);
        return tier == 2 ? 1 : (tier == 3 ? 2 : tier);
    }

    int WordsFn_RandomN(GuestPtr<WordsCtx> ctx) { return eeRemainder(lowWord(Random_Next()), ctx->arg); }
    int WordsFn_Random50(GuestPtr<WordsCtx>) { return eeRemainder(lowWord(Random_Next()), 100) >= 51 ? 1 : 0; }

    int WordsFn_Random3(GuestPtr<WordsCtx>)
    {
        const int roll = eeRemainder(lowWord(Random_Next()), 120);
        return roll < 40 ? 0 : (roll < 80 ? 1 : 2);
    }

    int WordsFn_LaptopHasBit(GuestPtr<WordsCtx> ctx) { return (bitMask(ctx->arg) & Laptop_CountActive()) != 0 ? 1 : 0; }

    // Whether actor `arg` of the talk's first event is person A (family and member).
    int WordsFn_EventActorIs(GuestPtr<WordsCtx> ctx)
    {
        const uint32_t actor = eventsOf(ctx)->talkA.address + static_cast<uint32_t>(ctx->arg) * 8u + 0x10u;
        if (port::load<uint16_t>(actor) != ctx->familyA)
            return 0;
        return port::load<uint16_t>(actor + 2u) == ctx->memberA ? 1 : 0;
    }

    // Objects new in the last periods: none 0, up to 10 1, more 2.
    int WordsFn_MapHistoryTier(GuestPtr<WordsCtx> ctx)
    {
        const int sum = MapObj_SumHistory(ctx->city->map);
        if (sum == 0)
            return 0;
        return sum < 11 ? 1 : 2;
    }

    // The story flags: two bitsets (A at 0x1C5520, B at 0x1C5530). The tests return the masked word.
    int WordsFn_StoryFlagA(GuestPtr<WordsCtx> ctx) { return static_cast<int>(port::load<uint32_t>(bitWord(kStoryFlagsA, ctx->arg)) & bitMask(ctx->arg)); }
    int WordsFn_StoryFlagB(GuestPtr<WordsCtx> ctx) { return static_cast<int>(port::load<uint32_t>(bitWord(kStoryFlagsB, ctx->arg)) & bitMask(ctx->arg)); }

    int WordsFn_SetStoryFlagB(GuestPtr<WordsCtx> ctx)
    {
        const uint32_t word = bitWord(kStoryFlagsB, ctx->arg);
        port::store<uint32_t>(word, port::load<uint32_t>(word) | bitMask(ctx->arg));
        return port::guestV0();
    }

    int WordsFn_ClearStoryFlagB(GuestPtr<WordsCtx> ctx)
    {
        const uint32_t word = bitWord(kStoryFlagsB, ctx->arg);
        port::store<uint32_t>(word, port::load<uint32_t>(word) & ~bitMask(ctx->arg));
        return port::guestV0();
    }

    // Whether the talk's first event is of a talking kind (event table +0x20 is 1 or 2).
    int WordsFn_EventA4IsTalk(GuestPtr<WordsCtx> ctx)
    {
        const uint8_t talking = port::load<uint8_t>(eventType(eventsOf(ctx)->talkA->kind) + 0x20u);
        return talking == 1 || talking == 2 ? 1 : 0;
    }

    // The second event's kind (event table +0x21) plus one, 0 without one.
    int WordsFn_EventA8Kind(GuestPtr<WordsCtx> ctx)
    {
        const uint8_t kind = eventsOf(ctx)->talkB->kind;
        return kind == 0 ? 0 : port::load<uint8_t>(eventType(kind) + 0x21u) + 1;
    }

    // Night (before 3:00 or from 18:00) 2, morning 0, afternoon 1.
    int WordsFn_TimeOfDay(GuestPtr<WordsCtx> ctx)
    {
        const int clock = ctx->city->clock;
        if (clock < 10801)
            return 2;
        if (clock < 43201)
            return 0;
        return clock < 64801 ? 1 : 2;
    }

    // Families moved in: below 20 0, below 50 1, below 100 2, below 200 3, else 4.
    int WordsFn_FamilyCountTier(GuestPtr<WordsCtx>)
    {
        const int count = globals::familiesMovedIn.get();
        if (count < 20)
            return 0;
        if (count < 50)
            return 1;
        if (count < 100)
            return 2;
        return count < 200 ? 3 : 4;
    }

    int WordsFn_HomeTypeFlag100(GuestPtr<WordsCtx> ctx)
    {
        const uint16_t family = ctx->arg != 0 ? ctx->familyB : ctx->familyA;
        return (typeAt(Family_GetTypeIndex(family))->flags & 0x100u) != 0 ? 1 : 0;
    }

    // Whether family A's home type is 0x104 or 0x105 (always 0 with an argument).
    int WordsFn_HomeIsType104(GuestPtr<WordsCtx> ctx)
    {
        const int type = Family_GetTypeIndex(ctx->familyA);
        if (ctx->arg != 0)
            return 0;
        return static_cast<uint32_t>(type - 0x104) < 2 ? 1 : 0;
    }

    // Byte 0 of the building's claim entry (claim section B of its type, 8 bytes per claim slot).
    int WordsFn_BuildingClaim(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<MapObject> building = buildingByArg(ctx);
        const int section = Claim_GetSectionB(typeOfObject(building)->claimB);
        return port::load<uint8_t>(static_cast<uint32_t>(section) + 8u * building->claimSlot);
    }

    namespace
    {
        // A member's status bits through the person record when there is one, else through the family; the
        // callee's whole v0 is passed back (called through the dispatcher).
        int memberStatus(GuestPtr<WordsCtx> ctx, uint32_t fromFamily, uint32_t fromByte)
        {
            const Who who = whoByArg(ctx);
            const GuestPtr<MapPerson> person = ctx->arg != 0 ? ctx->personB : ctx->personA;
            if (!person)
                return port::callGuest<int>(fromFamily, static_cast<int>(who.family), static_cast<int>(who.member));
            return port::callGuest<int>(fromByte, person.address + 0x2Fu);
        }
    }

    int WordsFn_StatusMid2(GuestPtr<WordsCtx> ctx) { return memberStatus(ctx, addr::MemberStatus_GetMid2, addr::StatusByte_Mid2); }
    int WordsFn_StatusLow2(GuestPtr<WordsCtx> ctx) { return memberStatus(ctx, addr::MemberStatus_GetLow2, addr::StatusByte_Low2); }

    // Whether the city's wants list holds a want of person A.
    int WordsFn_HasWantForPerson(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x30> frame;
        const uint32_t sp = frame.ptr().address;
        port::store<uint32_t>(sp + 0x2Cu, 0);
        for (;;)
        {
            if (Wants_Find(ctx->city->wants, ctx->city, GuestPtr<uint32_t>{sp + 0x20u}, GuestPtr<uint32_t>{sp + 0x24u},
                           GuestPtr<uint32_t>{sp + 0x28u}, GuestPtr<int32_t>{sp + 0x2Cu}) == 0)
                return 0;
            if (port::load<uint32_t>(sp + 0x28u) == ctx->personA->index)
                return 1;
        }
    }

    int WordsFn_FindWant(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x20> frame;
        const uint32_t sp = frame.ptr().address;
        port::store<uint32_t>(sp + 0x1Cu, 0);
        return Wants_Find(ctx->city->wants, ctx->city, GuestPtr<uint32_t>{sp + 0x10u}, GuestPtr<uint32_t>{sp + 0x14u},
                          GuestPtr<uint32_t>{sp + 0x18u}, GuestPtr<int32_t>{sp + 0x1Cu});
    }

    // ---- second group: texts

    GuestPtr<char> WordsFn_TextEventA4Name(GuestPtr<WordsCtx> ctx) { return GuestPtr<char>{eventType(eventsOf(ctx)->talkA->kind)}; }

    GuestPtr<char> WordsFn_TextPopulation(GuestPtr<WordsCtx> ctx)
    {
        Text_FormatThousands(GuestPtr<char>{kTextBuffer}, port::load<uint8_t>(ctx->city->map.address + 0x29u));
        return GuestPtr<char>{kTextBuffer};
    }

    GuestPtr<char> WordsFn_TextOpenHours(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<BuildingType> type = typeAt(Family_GetTypeIndex(ctx->familyA));
        Text_FormatTimeRange(GuestPtr<char>{kTextBuffer}, type->openMinute, type->closeMinute);
        return GuestPtr<char>{kTextBuffer};
    }

    GuestPtr<char> WordsFn_TextClaimLine(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<MapObject> building = buildingByArg(ctx);
        return GuestPtr<char>{static_cast<uint32_t>(Claim_GetLine(typeOfObject(building)->claimB, building->claimSlot))};
    }

    // The name of building type B (1), of family A's home type (0) or of the result's (other).
    GuestPtr<char> WordsFn_TextBuildingName(GuestPtr<WordsCtx> ctx)
    {
        const int type = ctx->arg == 1 ? ctx->buildingTypeB : Family_GetTypeIndex(ctx->arg == 0 ? ctx->familyA : ctx->result);
        return GuestPtr<char>{typeAt(type).address + 0x110u};
    }

    // The bag item's name: its group's first record (0x1BDA9C) plus its index, 0x88-byte records (0x1BDAA0)
    // whose first halfword is the offset into the names (0x1BDAA4).
    GuestPtr<char> WordsFn_TextBagItemName(GuestPtr<WordsCtx> ctx)
    {
        const uint32_t slot = ctx->bagSlot.address;
        const uint32_t first = port::load<uint16_t>(port::load<uint32_t>(0x001BDA9Cu) + 4u * port::load<uint16_t>(slot));
        const uint32_t record = port::load<uint32_t>(0x001BDAA0u) + 0x88u * (port::load<uint16_t>(slot + 2u) + first);
        return GuestPtr<char>{port::load<uint32_t>(0x001BDAA4u) + port::load<uint16_t>(record)};
    }

    // The goods' name: the goods record of building type B for family B in the season (month / 4), with the
    // argument as the offset in and out.
    GuestPtr<char> WordsFn_TextGoodsName(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x20> frame;
        const GuestPtr<int32_t> offset{frame.ptr().address + 0x1Cu};
        *offset = ctx->arg;
        const int season = (ctx->city->monthCount % 12) / 4;
        const uint32_t record = static_cast<uint32_t>(Goods_GetRecordOffset(ctx->buildingTypeB, ctx->familyB, season, offset));
        return GuestPtr<char>{port::load<uint32_t>(0x001BDAA4u) + port::load<uint16_t>(record)};
    }

    // ---- second group: actions

    int WordsFn_MarkFamilyKnown(GuestPtr<WordsCtx> ctx)
    {
        Family_SetKnown(ctx->familyA);
        return Sound_PlaySe(0x14, 0, 0x3C, 0.0f, 0.0f);
    }

    int WordsFn_AddWant(GuestPtr<WordsCtx> ctx)
    {
        Wants_AddForPerson(ctx->city->wants, ctx->city, ctx->personA, ctx->buildingA->claimSlot);
        return Sound_PlaySe(0x15, 0, 0x3C, 0.0f, 0.0f);
    }

    int WordsFn_StartEvent(GuestPtr<WordsCtx> ctx)
    {
        const int started = Event_StartFromScript(eventsOf(ctx), ctx->city, ctx->personA);
        if (started != 0)
            ctx->eventSlot = 0;
        return started;
    }

    namespace
    {
        // The building type of the talk's first event (event table +0x20 as a halfword).
        int eventBuildingType(GuestPtr<WordsCtx> ctx) { return port::load<int16_t>(eventType(eventsOf(ctx)->talkA->kind) + 0x20u); }

        // Map_FindPersonEntry with the halfwords as the original loads them.
        GuestPtr<MapPerson> findPerson(GuestPtr<WordsCtx> ctx, uint16_t family, uint16_t member)
        {
            return port::callGuest<GuestPtr<MapPerson>>(addr::Map_FindPersonEntry, ctx->city->map, static_cast<int>(family), static_cast<int>(member));
        }
    }

    int WordsFn_EventRemoveWant(GuestPtr<WordsCtx> ctx)
    {
        return port::callGuest<int>(addr::Wants_RemoveEventHold, ctx->city->wants, eventBuildingType(ctx), static_cast<int>(ctx->arg));
    }

    int WordsFn_EventAddWant(GuestPtr<WordsCtx> ctx)
    {
        return port::callGuest<int>(addr::Wants_AddEventHold, ctx->city->wants, eventBuildingType(ctx), static_cast<int>(ctx->arg));
    }

    // Person B becomes family A's member `memberIndex`.
    int WordsFn_SelectMemberB(GuestPtr<WordsCtx> ctx)
    {
        ctx->familyB = ctx->familyA;
        ctx->memberB = ctx->memberIndex;
        ctx->personB = findPerson(ctx, ctx->familyB, ctx->memberIndex);
        return static_cast<int>(ctx->personB.address);
    }

    // Person B becomes the first (A) or second (B) person of the talk's event slot.
    int WordsFn_SelectEventPersonA(GuestPtr<WordsCtx> ctx)
    {
        const EventSlot &slot = eventsOf(ctx)->slots[ctx->eventSlot];
        const GuestPtr<MapPerson> person = findPerson(ctx, slot.familyA, slot.memberA);
        ctx->personB = person;
        ctx->familyB = slot.familyA;
        ctx->memberB = slot.memberA;
        return static_cast<int>(person.address);
    }

    int WordsFn_SelectEventPersonB(GuestPtr<WordsCtx> ctx)
    {
        const EventSlot &slot = eventsOf(ctx)->slots[ctx->eventSlot];
        const GuestPtr<MapPerson> person = findPerson(ctx, slot.familyB, slot.memberB);
        ctx->personB = person;
        ctx->familyB = slot.familyB;
        ctx->memberB = slot.memberB;
        return static_cast<int>(person.address);
    }

    PORT_FN(WordsFn_FriendshipBand);
    PORT_FN(WordsFn_FriendshipTier);
    PORT_FN(WordsFn_MeterAgrees);
    PORT_FN(WordsFn_BuildingFlag100);
    PORT_FN(WordsFn_BuildingFlag2);
    PORT_FN(WordsFn_PersonFlag10);
    PORT_FN(WordsFn_NameSlotSet);
    PORT_FN(WordsFn_ApplicantCountTier);
    PORT_FN(WordsFn_ApprovalTier);
    PORT_FN(WordsFn_RandomN);
    PORT_FN(WordsFn_Random50);
    PORT_FN(WordsFn_Random3);
    PORT_FN(WordsFn_LaptopHasBit);
    PORT_FN(WordsFn_EventActorIs);
    PORT_FN(WordsFn_MapHistoryTier);
    PORT_FN(WordsFn_StoryFlagA);
    PORT_FN(WordsFn_StoryFlagB);
    PORT_FN(WordsFn_SetStoryFlagB);
    PORT_FN(WordsFn_ClearStoryFlagB);
    PORT_FN(WordsFn_EventA4IsTalk);
    PORT_FN(WordsFn_EventA8Kind);
    PORT_FN(WordsFn_TimeOfDay);
    PORT_FN(WordsFn_FamilyCountTier);
    PORT_FN(WordsFn_HomeTypeFlag100);
    PORT_FN(WordsFn_HomeIsType104);
    PORT_FN(WordsFn_BuildingClaim);
    PORT_FN(WordsFn_StatusMid2);
    PORT_FN(WordsFn_StatusLow2);
    PORT_FN(WordsFn_HasWantForPerson);
    PORT_FN(WordsFn_FindWant);
    PORT_FN(WordsFn_TextEventA4Name);
    PORT_FN(WordsFn_TextPopulation);
    PORT_FN(WordsFn_TextOpenHours);
    PORT_FN(WordsFn_TextClaimLine);
    PORT_FN(WordsFn_TextBuildingName);
    PORT_FN(WordsFn_TextBagItemName);
    PORT_FN(WordsFn_TextGoodsName);
    PORT_FN(WordsFn_MarkFamilyKnown);
    PORT_FN(WordsFn_AddWant);
    PORT_FN(WordsFn_StartEvent);
    PORT_FN(WordsFn_EventRemoveWant);
    PORT_FN(WordsFn_EventAddWant);
    PORT_FN(WordsFn_SelectMemberB);
    PORT_FN(WordsFn_SelectEventPersonA);
    PORT_FN(WordsFn_SelectEventPersonB);
    PORT_FN(WordsFn_Arg4);
    PORT_FN(WordsFn_Flag134);
    PORT_FN(WordsFn_False);
    PORT_FN(WordsFn_FamilyHasLeft);
    PORT_FN(WordsFn_FamilyInCity);
    PORT_FN(WordsFn_ScoreA8Tier);
    PORT_FN(WordsFn_ScoreA0Tier);
    PORT_FN(WordsFn_WeatherToday);
    PORT_FN(WordsFn_WeatherTomorrow);
    PORT_FN(WordsFn_StaminaZero);
    PORT_FN(WordsFn_StaminaTier);
    PORT_FN(WordsFn_IsApplicant);
    PORT_FN(WordsFn_BeforeSix);
    PORT_FN(WordsFn_CityFlag2);
    PORT_FN(WordsFn_Month);
    PORT_FN(WordsFn_PopulationRank);
    PORT_FN(WordsFn_EventsActive);
    PORT_FN(WordsFn_EventA4Var);
    PORT_FN(WordsFn_EventA8Var);
    PORT_FN(WordsFn_GetVar1C5602);
    PORT_FN(WordsFn_FamilyModArg);
    PORT_FN(WordsFn_PersonFlagBit);
    PORT_FN(WordsFn_EventCountdown);
    PORT_FN(WordsFn_FamilyKnown);
    PORT_FN(WordsFn_TextNameSlot);
    PORT_FN(WordsFn_SetCancelAllowed);
    PORT_FN(WordsFn_SetVar1C5602);
    PORT_FN(WordsFn_CitySetFlag40);
    PORT_FN(WordsFn_ClearPersonFlag20);
    PORT_FN(WordsFn_SetEventA4Var);
    PORT_FN(WordsFn_SetEventA8Var);
    PORT_FN(WordsFn_PlaySe);
    PORT_FN(WordsFn_TalkEndChoosing);
    PORT_FN(WordsFn_EventQueueShift);
    PORT_FN(WordsFn_EndEventA4Result);
    PORT_FN(WordsFn_EndEventA4);
    PORT_FN(WordsFn_EndEventA8Result);
    PORT_FN(WordsFn_EndEventA8);
    PORT_FN(WordsFn_StartEventA4);
    PORT_FN(WordsFn_EventA4Slot10);
    PORT_FN(WordsFn_EventA8Slot10);
    PORT_FN(WordsFn_SetPose);
    PORT_FN(WordsFn_TalkSelectActor);
}
