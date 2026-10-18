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
        // Actor `index` of the talk's first event (EventData::actors, by guest address for any index).
        GuestPtr<ActorRef> talkActor(GuestPtr<WordsCtx> ctx, int index)
        {
            return GuestPtr<ActorRef>{eventsOf(ctx)->talkA.address + 0x0Cu + 8u * static_cast<uint32_t>(index)};
        }
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
        return 3 - eventsOf(ctx)->slots[ctx->eventSlot].stage;
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

    int WordsFn_EventQueueShift(GuestPtr<WordsCtx> ctx) { return port::callGuest<int>(addr::EventMgr_RequeueFirst, eventsOf(ctx)); }

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
        const GuestPtr<ActorRef> actor = talkActor(ctx, ctx->arg);
        return port::callGuest<int>(addr::CityScene_TalkSelectPerson, ctx->city, static_cast<int>(actor->family),
                                    static_cast<int>(actor->member));
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
        const GuestPtr<ActorRef> actor = talkActor(ctx, ctx->arg);
        if (actor->family != ctx->familyA)
            return 0;
        return actor->member == ctx->memberA ? 1 : 0;
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

    namespace
    {
        // The bag item's goods record: its group's first record (0x1BDA9C) plus its index, 0x88-byte records
        // (0x1BDAA0) whose first halfword is the offset into the names (0x1BDAA4).
        uint32_t bagItemRecord(GuestPtr<WordsCtx> ctx)
        {
            const uint32_t slot = ctx->bagSlot.address;
            const uint32_t first = port::load<uint16_t>(port::load<uint32_t>(0x001BDA9Cu) + 4u * port::load<uint16_t>(slot));
            return port::load<uint32_t>(0x001BDAA0u) + 0x88u * (port::load<uint16_t>(slot + 2u) + first);
        }

        // The goods record of building type B for family B in the season (month / 4), with the argument as the
        // offset in and out: a local in the caller's frame at `slot`.
        uint32_t goodsRecord(GuestPtr<WordsCtx> ctx, uint32_t slot)
        {
            const GuestPtr<int32_t> offset{slot};
            *offset = ctx->arg;
            const int season = (ctx->city->monthCount % 12) / 4;
            return static_cast<uint32_t>(Goods_GetRecordOffset(ctx->buildingTypeB, ctx->familyB, season, offset));
        }
    }

    GuestPtr<char> WordsFn_TextBagItemName(GuestPtr<WordsCtx> ctx)
    {
        return GuestPtr<char>{port::load<uint32_t>(0x001BDAA4u) + port::load<uint16_t>(bagItemRecord(ctx))};
    }

    GuestPtr<char> WordsFn_TextGoodsName(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x20> frame;
        const uint32_t record = goodsRecord(ctx, frame.ptr().address + 0x1Cu);
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

    // ---- third group: event actors
    //
    // The functions these hand on to leave v0 alone on some paths, so v0 is set first to what the original holds
    // there at the call: the last halfword of person A's actor (the member), or the target actor's entry less 0xC.

    namespace
    {
        // Person A as an event actor, written at `slot` (the original builds it in its own stack frame): the person
        // record and the building (6000 and 0x3F80 without them), the family and the member.
        GuestPtr<ActorRef> actorA(GuestPtr<WordsCtx> ctx, uint32_t slot)
        {
            const GuestPtr<ActorRef> actor{slot};
            actor->person = ctx->personA ? ctx->personA->index : kNoPerson;
            actor->object = ctx->buildingA ? ctx->buildingA->id : kNoObject;
            actor->family = ctx->familyA;
            actor->member = ctx->memberA;
            return actor;
        }

        int eventCallSlot14(GuestPtr<WordsCtx> ctx, int index)
        {
            port::GuestFrame<0x20> frame;
            const GuestPtr<ActorRef> actor = actorA(ctx, frame.ptr().address + 0x18u);
            port::setGuestV0(ctx->memberA);
            return port::callGuest<int>(addr::Event_CallSlot14, eventsOf(ctx), ctx->city, actor, static_cast<int>(ctx->arg), index);
        }
    }

    int WordsFn_ActorDetach(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x20> frame;
        const GuestPtr<ActorRef> actor = actorA(ctx, frame.ptr().address + 0x18u);
        port::setGuestV0(ctx->memberA);
        return EvActor_Detach(ctx->city, actor);
    }

    // Person A walks to actor `arg` of the talk's first event.
    int WordsFn_ActorMoveTo(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x20> frame;
        const GuestPtr<ActorRef> actor = actorA(ctx, frame.ptr().address + 0x18u);
        const GuestPtr<ActorRef> target = talkActor(ctx, ctx->arg);
        port::setGuestV0(static_cast<int32_t>(target.address - 0x0Cu));
        return EvActor_MoveTo(ctx->city, actor, target, 1);
    }

    // Person A leaves the building it is in and follows actor `arg` of the talk's first event.
    int WordsFn_ActorFollow(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x30> frame;
        Person_ExitBuilding(ctx->city, ctx->personA);
        const GuestPtr<ActorRef> actor = actorA(ctx, frame.ptr().address + 0x28u);
        const GuestPtr<ActorRef> target = talkActor(ctx, ctx->arg);
        port::setGuestV0(static_cast<int32_t>(target.address - 0x0Cu));
        return EvActor_Follow(ctx->city, actor, target);
    }

    // Handler slot +0x14 of the talk's first (A4) or second (A8) event, with person A and the argument.
    // mm_Event_CallSlot14 returns nothing the port knows of; its v0 is the callbacks' result.
    int WordsFn_EventA4Slot14(GuestPtr<WordsCtx> ctx) { return eventCallSlot14(ctx, 0); }
    int WordsFn_EventA8Slot14(GuestPtr<WordsCtx> ctx) { return eventCallSlot14(ctx, 1); }

    // Person A becomes actor `arg` of the talk's first event and building A takes part (flag 0x200). Not for a
    // building with flag 0x40, and not over an actor at object 0 when object 0 has flag 0x200.
    int WordsFn_EventSetActor(GuestPtr<WordsCtx> ctx)
    {
        if (ctx->buildingA && (ctx->buildingA->flags & 0x40u) != 0)
            return 0;
        const GuestPtr<ActorRef> actor = talkActor(ctx, ctx->arg);
        const uint16_t object = actor->object;
        if (object == 0 && (Map_GetRecord(ctx->city->map, object)->flags & 0x200u) != 0)
            return 0;
        actorA(ctx, actor.address);
        if (ctx->buildingA)
            ctx->buildingA->flags |= 0x200u;
        return 1;
    }

    // Person B, family and member B, building B and type B become actor `arg` of the talk's first event.
    int WordsFn_SelectEventActor(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<ActorRef> actor = talkActor(ctx, ctx->arg);
        ctx->personB = actor->person < kNoPerson ? ctx->city->map->people + actor->person : GuestPtr<MapPerson>{0};
        ctx->familyB = actor->family;
        ctx->memberB = actor->member;
        ctx->buildingB = actor->object < kNoObject ? Map_GetRecord(ctx->city->map, actor->object) : GuestPtr<MapObject>{0};
        const int type = Family_GetTypeIndex(ctx->familyB);
        ctx->buildingTypeB = static_cast<uint16_t>(type);
        return type;
    }

    // The stage of the event in the talk's slot (event table +0x21); for events of kind 5 (+0x20) plus the queue
    // position of event 5 less that of event 4 (the queue's length for one that is not queued).
    int WordsFn_EventStage(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<EventMgr> events = eventsOf(ctx);
        const uint32_t type = eventType(events->slots[ctx->eventSlot].kind);
        const int stage = port::load<uint8_t>(type + 0x21u);
        if (port::load<uint8_t>(type + 0x20u) != 5)
            return stage;
        const int at5 = EventQueue_Find(events, 5);
        return stage + at5 - EventQueue_Find(events, 4);
    }

    // ---- third group: buildings, people and goods

    namespace
    {
        GuestPtr<MapPerson> personByArg(GuestPtr<WordsCtx> ctx) { return ctx->arg != 0 ? ctx->personB : ctx->personA; }

        // The building's claim: the halfword at +2 of its entry in claim section B of its type.
        int claimOf(GuestPtr<MapObject> building)
        {
            const int section = Claim_GetSectionB(typeOfObject(building)->claimB);
            return port::load<uint16_t>(static_cast<uint32_t>(section) + 8u * building->claimSlot + 2u);
        }

        // Whether the clock is within type B's opening hours, which may run past midnight.
        bool withinOpeningHours(GuestPtr<WordsCtx> ctx)
        {
            const GuestPtr<BuildingType> type = typeAt(ctx->buildingTypeB);
            const int minute = ctx->city->clock / 60;
            const int open = type->openMinute, close = type->closeMinute;
            if (close < open)
                return minute >= open || minute < close;
            return minute >= open && minute < close;
        }

        // A building's own opening test: slot +0x24 of its type, called with (type, city, building, a3). v0 holds the
        // type's entry in the type table at the call.
        int callOpenSlot(GuestPtr<WordsCtx> ctx, GuestPtr<MapObject> building, uint32_t a3)
        {
            const GuestPtr<BuildingType> type = typeOfObject(building);
            port::setGuestV0(static_cast<int32_t>(globals::buildingTypeTable.get() + 4u * building->type));
            return port::callGuest<int>(port::load<uint32_t>(type->vtable.address + 0x24u), type, ctx->city, building, a3);
        }

        // The person's building on their tile (mm_Map_FindPersonObjectOnTile), the tile from the position as
        // (x + 64, 64 - z).
        GuestPtr<MapObject> buildingUnderPerson(GuestPtr<WordsCtx> ctx, GuestPtr<MapPerson> person)
        {
            const int x = sdk::fptosi(ps2f(64.0f) + ps2f(person->x));
            const int z = sdk::fptosi(ps2f(64.0f) - ps2f(person->z));
            const uint32_t tile = (static_cast<uint32_t>(x) + (static_cast<uint32_t>(z) << 7)) & 0xFFFFu;
            return Map_FindPersonObjectOnTile(ctx->city->map, person->index, tile);
        }

        // Building B, family B and type B become the building's.
        int setBuildingB(GuestPtr<WordsCtx> ctx, GuestPtr<MapObject> building)
        {
            ctx->buildingB = building;
            ctx->familyB = ctx->buildingB->family;
            ctx->buildingTypeB = ctx->buildingB->type;
            return static_cast<int>(building.address);
        }
    }

    int WordsFn_ClaimIs50(GuestPtr<WordsCtx> ctx) { return claimOf(buildingByArg(ctx)) == 0x32 ? 1 : 0; }

    // Whether family A may apply: always while the city is popular (flag 4), and with stage flag 2 when
    // mm_Family_CanApply agrees.
    int WordsFn_CanApply(GuestPtr<WordsCtx> ctx)
    {
        int agrees = 0;
        if ((globals::stageFlags.get() & 2u) != 0)
            agrees = Family_CanApply(ctx->familyA, ctx->city) != 0 ? 1 : 0;
        return ((ctx->city->flags & 4u) != 0 ? 1 : 0) | agrees;
    }

    // Whether family A is entry 0xFF (argument 0), 0xB7 (1) or 0x104 (2) of the family type table.
    int WordsFn_IsSpecialFamily(GuestPtr<WordsCtx> ctx)
    {
        uint32_t type;
        switch (ctx->arg)
        {
        case 0:
            type = 0xFF;
            break;
        case 1:
            type = 0xB7;
            break;
        case 2:
            type = 0x104;
            break;
        default:
            return 0;
        }
        return port::load<uint16_t>(globals::familyTypeTable.get() + 2u * type) == ctx->familyA ? 1 : 0;
    }

    // Whether the money covers the goods' price.
    int WordsFn_CanAfford(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x30> frame;
        const uint32_t record = goodsRecord(ctx, frame.ptr().address + 0x2Cu);
        const int32_t money = globals::stageState()->money;
        return money < Goods_GetPrice(static_cast<int>(record)) ? 0 : 1;
    }

    // For goods carried in the bag (category, record +4, from 0x10 except 0x12, 0x14, 0x16 and 0x19), whether the
    // bag is full: the last slot of its size holds an item.
    int WordsFn_GoodsBagFull(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x30> frame;
        const uint32_t record = goodsRecord(ctx, frame.ptr().address + 0x2Cu);
        const uint8_t category = port::load<uint8_t>(record + 4u);
        if (category < 0x10 || category == 0x12 || category == 0x16 || category == 0x14 || category == 0x19)
            return 0;
        const GuestPtr<Bag> bag = ctx->city->bag;
        if (bag->size == 0)
            return 0;
        const uint32_t slots = port::load<uint8_t>(globals::kBagSlotsBySize + bag->size);
        return port::load<uint16_t>(bag.address + 4u + 16u * (slots - 1u)) != 0 ? 1 : 0; // slots[slots - 1].item
    }

    // Whether the goods are owned already: laptop goods by the active laptop entries (bit `category`, and
    // categories 6 and 7 by entries 7 and 8), category 0x16 when a bag shortcut holds record +5, category 0x19 when
    // mm_Family_CanApply refuses record +5.
    int WordsFn_GoodsOwned(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x40> frame;
        const uint32_t record = goodsRecord(ctx, frame.ptr().address + 0x3Cu);
        const uint32_t active = Laptop_CountActive();
        const uint8_t category = port::load<uint8_t>(record + 4u);
        if ((active & (1u << (category & 31u))) != 0)
            return 1;
        if (category >= 6 && category < 8)
            return (Laptop_CountActive() & 0x180u) != 0 ? 1 : 0;
        const uint8_t which = port::load<uint8_t>(record + 5u);
        if (category == 0x16)
        {
            for (const BagShortcut &shortcut : ctx->city->bag->shortcuts)
                if (static_cast<uint16_t>(shortcut.slot) == which)
                    return 1;
            return 0;
        }
        if (category == 0x19)
            return Family_CanApply(which, ctx->city) == 0 ? 1 : 0;
        return 0;
    }

    // Person A is given the bag item: friendship points by its price (three per step: 1 below 1001, 2 below 5001,
    // 3 below 10001, 4 below 50001, 5 below 100001, 10 below 500001, 20 below 1000001, 30 below 5000001, else 50),
    // then the item leaves the bag. mm_Bag_RemoveSlot returns nothing the port knows of and its v0 is the result: the
    // city pointer the original holds in v0 at the call, unless it changes it.
    int WordsFn_GiveBagItem(GuestPtr<WordsCtx> ctx)
    {
        const int price = Goods_GetPrice(static_cast<int>(bagItemRecord(ctx)));
        int steps;
        if (price < 1001)
            steps = 1;
        else if (price < 5001)
            steps = 2;
        else if (price < 10001)
            steps = 3;
        else if (price < 50001)
            steps = 4;
        else if (price < 100001)
            steps = 5;
        else if (price < 500001)
            steps = 10;
        else if (price < 1000001)
            steps = 20;
        else if (price < 5000001)
            steps = 30;
        else
            steps = 50;
        Family_AddFriendship(ctx->familyA, ctx->memberA, GuestPtr<int32_t>::of(ctx->city->approval), steps * 3);
        port::setGuestV0(static_cast<int32_t>(ctx->city.address));
        const int removed = port::callGuest<int>(addr::Bag_RemoveSlot, ctx->city->bag, ctx->bagSlot);
        ctx->bagSlot = GuestPtr<void>{0};
        return removed;
    }

    // Whether (family + member) % 12 of person A (or B) is this month.
    int WordsFn_IsBirthdayMonth(GuestPtr<WordsCtx> ctx)
    {
        const Who who = whoByArg(ctx);
        return (who.family + who.member) % 12 == ctx->city->monthCount % 12 ? 1 : 0;
    }

    // The building's state (B with an argument, else A): 0 without one, 3 with its claim met (flag 8), 4 with its
    // meter maxed (flag 0x40), else its claim meter level.
    int WordsFn_BuildingState(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<MapObject> building = buildingByArg(ctx);
        if (!building)
            return 0;
        if ((building->flags & 8u) != 0)
            return 3;
        if ((building->flags & 0x40u) != 0)
            return 4;
        return ClaimMeter_Level(building->claimMeter);
    }

    // 2 for a member without friendship yet, else whether their friendship level is above 13.
    int WordsFn_FriendLevelHigh(GuestPtr<WordsCtx> ctx)
    {
        const Who who = whoByArg(ctx);
        if (Family_GetFriendship(who.family, who.member) == 0)
            return 2;
        return memberStatus(ctx, addr::MemberStatus_IsLevelAbove13, addr::StatusByte_LevelAbove13);
    }

    // The target of person A (or B): 0 none, 1 a building other than their workplace, else whether the type of
    // building A (or B) has flag 0x20.
    int WordsFn_WorksAtHome(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<MapPerson> person = personByArg(ctx);
        if (person->target == kNoObject)
            return 0;
        if (person->target != person->work)
            return 1;
        return (typeOfObject(buildingByArg(ctx))->flags & 0x20u) != 0 ? 1 : 0;
    }

    // Where person A (or B) heads: 0 without a person, 3 nowhere, 0 to work, 1 to their target, else 2 or 3 by the
    // destination type's flag 0x40.
    int WordsFn_PersonHeading(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<MapPerson> person = personByArg(ctx);
        if (!person)
            return 0;
        const uint16_t destination = person->destination;
        if (destination == kNoObject)
            return 3;
        if (destination == person->work)
            return 0;
        if (destination == person->target)
            return 1;
        return (typeOfObject(Map_GetRecord(ctx->city->map, destination))->flags & 0x40u) != 0 ? 2 : 3;
    }

    // Where person A (or B) is: 1 without a person, 3 outside, 0 in their target, 1 at work, else 2 or 3 by the
    // type flag 0x40 of the building they are in.
    int WordsFn_PersonWhere(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<MapPerson> person = personByArg(ctx);
        if (!person)
            return 1;
        const uint16_t inside = person->inside;
        if (inside == kNoObject)
            return 3;
        if (inside == person->target)
            return 0;
        if (inside == person->work)
            return 1;
        return (typeOfObject(Map_GetRecord(ctx->city->map, inside))->flags & 0x40u) != 0 ? 2 : 3;
    }

    // Whether building B is open (its type decides), or without one whether type B's hours hold the clock.
    int WordsFn_BuildingOpen(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<MapObject> building = ctx->buildingB;
        if (building)
            return callOpenSlot(ctx, building, port::guestA3());
        return withinOpeningHours(ctx) ? 1 : 0;
    }

    // Whether building A's footprint overlaps a type B footprint at building B's tile.
    int WordsFn_BuildingsOverlap(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<MapObject> a = ctx->buildingA, b = ctx->buildingB;
        const int ax = a->tile & 0x7F, az = a->tile >> 7, sizeA = typeOfObject(a)->size;
        const int bx = b->tile & 0x7F, bz = b->tile >> 7, sizeB = typeAt(ctx->buildingTypeB)->size;
        return ax < bx + sizeB && bx < ax + sizeA && az < bz + sizeB && bz < az + sizeA ? 1 : 0;
    }

    // Whether family A's (or B's) member agrees to come as a friend: never with 32 applicants, nor when the person
    // records in use plus seven for each active applicant and seven more would reach 6000.
    int WordsFn_TryFriendAgrees(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<Applicants> applicants = ctx->city->applicants;
        if (applicants->count >= 32)
            return 0;
        const uint32_t people = static_cast<uint32_t>(ctx->city->map->peopleInUse);
        const uint32_t needed = 7u * static_cast<uint32_t>(Applicants_CountActive(applicants)) + people + 7u;
        if (static_cast<int32_t>(needed) >= kNoPerson)
            return 0;
        const Who who = whoByArg(ctx);
        return Family_FriendAgrees(who.family, who.member, ctx->city->approval);
    }

    // How crowded the building (B with an argument, else A) is with visitors: residents (classes without flag 1)
    // whose target and workplace are elsewhere. 0 none, 1 up to half the type's capacity, 2 more.
    int WordsFn_BuildingCrowd(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<MapObject> building = buildingByArg(ctx);
        const int capacity = typeOfObject(building)->capacity;
        port::GuestLocal<uint16_t> cursor;
        *cursor = building->firstPerson;
        int visitors = 0;
        while (const GuestPtr<MapPerson> person = People_Next(ctx->city->map, cursor))
        {
            const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
            if ((cls->flags & 1u) != 0 || person->target == building->id || person->work == building->id)
                continue;
            ++visitors;
        }
        if (visitors == 0)
            return 0;
        return visitors <= capacity / 2 ? 1 : 2;
    }

    // 2 when a want's building has building A's claim, else whether the wants list has room for 4 more words.
    int WordsFn_WantClaimMatch(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x50> frame;
        const uint32_t sp = frame.ptr().address;
        const int claim = claimOf(ctx->buildingA);
        port::store<uint32_t>(sp + 0x4Cu, 0);
        while (Wants_Find(ctx->city->wants, ctx->city, GuestPtr<uint32_t>{sp + 0x40u}, GuestPtr<uint32_t>{sp + 0x44u},
                          GuestPtr<uint32_t>{sp + 0x48u}, GuestPtr<int32_t>{sp + 0x4Cu}) != 0)
        {
            if (claimOf(Map_GetRecord(ctx->city->map, static_cast<int>(port::load<uint32_t>(sp + 0x44u)))) == claim)
                return 2;
        }
        return Wants_HasRoom(ctx->city->wants, 4);
    }

    // The claim line of the chosen want's building (want number wantIndex in the wants list), 0 past the end.
    GuestPtr<char> WordsFn_TextWantClaim(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x40> frame;
        const uint32_t sp = frame.ptr().address;
        port::store<uint32_t>(sp + 0x3Cu, 0);
        for (int i = 0; Wants_Find(ctx->city->wants, ctx->city, GuestPtr<uint32_t>{sp + 0x30u}, GuestPtr<uint32_t>{sp + 0x34u},
                                   GuestPtr<uint32_t>{sp + 0x38u}, GuestPtr<int32_t>{sp + 0x3Cu}) != 0;
             ++i)
        {
            if (i != ctx->wantIndex)
                continue;
            const GuestPtr<MapObject> building = Map_GetRecord(ctx->city->map, static_cast<int>(port::load<uint32_t>(sp + 0x34u)));
            return GuestPtr<char>{static_cast<uint32_t>(Claim_GetLine(typeOfObject(building)->claimB, building->claimSlot))};
        }
        return GuestPtr<char>{0};
    }

    // Building B (with family B and type B) becomes, by the argument: 0 building A, 1 person A's target, 2 the
    // building person A stands at, 3 the one they are in, 4 their destination, 5 none, with family B the result's
    // family. Otherwise nothing changes.
    int WordsFn_SelectBuildingB(GuestPtr<WordsCtx> ctx)
    {
        switch (ctx->arg)
        {
        case 0:
            ctx->familyB = ctx->familyA;
            ctx->buildingB = ctx->buildingA;
            ctx->buildingTypeB = ctx->buildingB->type;
            return port::guestV0();
        case 1:
            return setBuildingB(ctx, Map_GetRecord(ctx->city->map, ctx->personA->target));
        case 2:
        {
            const GuestPtr<MapObject> building = buildingUnderPerson(ctx, ctx->personA);
            ctx->buildingB = building;
            ctx->familyB = ctx->buildingB ? ctx->buildingB->family : 0;
            ctx->buildingTypeB = ctx->buildingB ? ctx->buildingB->type : 0;
            return static_cast<int>(building.address);
        }
        case 3:
            return setBuildingB(ctx, Map_GetRecord(ctx->city->map, ctx->personA->inside));
        case 4:
            return setBuildingB(ctx, Map_GetRecord(ctx->city->map, ctx->personA->destination));
        case 5:
        {
            ctx->familyB = ctx->result;
            ctx->buildingB = GuestPtr<MapObject>{0};
            const int type = Family_GetTypeIndex(ctx->result);
            ctx->buildingTypeB = static_cast<uint16_t>(type);
            return type;
        }
        default:
            return port::guestV0();
        }
    }

    // Where person A (or B) stands: 1 without a person, 0 off any building of theirs or inside another one, at
    // their workplace 1 (or 2 when it is their target, its type has flag 0x20 and building B is open), 2 at their
    // target, 3 at a building whose type has flag 0x40, else 0. Without an argument building B and type B become
    // the building stood at.
    int WordsFn_PersonAtBuilding(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<MapPerson> person = personByArg(ctx);
        if (!person)
            return 1;
        const GuestPtr<MapObject> building = buildingUnderPerson(ctx, person);
        if (!building)
            return 0;
        if (ctx->arg == 0)
        {
            ctx->buildingB = Map_GetRecord(ctx->city->map, building->id);
            ctx->buildingTypeB = ctx->buildingB->type;
        }
        if (person->inside != kNoObject && person->inside != building->id)
            return 0;
        const GuestPtr<BuildingType> type = typeOfObject(building);
        if (building->id == person->work)
        {
            if (person->work != person->target || (type->flags & 0x20u) == 0)
                return 1;
            int open;
            if (const GuestPtr<MapObject> b = ctx->buildingB)
                open = callOpenSlot(ctx, b, globals::buildingTypeTable.get()); // a3 holds the type table here
            else
                open = withinOpeningHours(ctx) ? 1 : 0;
            return open != 0 ? 2 : 1;
        }
        if (building->id == person->target)
            return 2;
        return (type->flags & 0x40u) != 0 ? 3 : 0;
    }

    PORT_FN(WordsFn_ActorDetach);
    PORT_FN(WordsFn_ActorMoveTo);
    PORT_FN(WordsFn_ActorFollow);
    PORT_FN(WordsFn_EventA4Slot14);
    PORT_FN(WordsFn_EventA8Slot14);
    PORT_FN(WordsFn_EventSetActor);
    PORT_FN(WordsFn_SelectEventActor);
    PORT_FN(WordsFn_EventStage);
    PORT_FN(WordsFn_ClaimIs50);
    PORT_FN(WordsFn_CanApply);
    PORT_FN(WordsFn_IsSpecialFamily);
    PORT_FN(WordsFn_CanAfford);
    PORT_FN(WordsFn_GoodsBagFull);
    PORT_FN(WordsFn_GoodsOwned);
    PORT_FN(WordsFn_GiveBagItem);
    PORT_FN(WordsFn_IsBirthdayMonth);
    PORT_FN(WordsFn_BuildingState);
    PORT_FN(WordsFn_FriendLevelHigh);
    PORT_FN(WordsFn_WorksAtHome);
    PORT_FN(WordsFn_PersonHeading);
    PORT_FN(WordsFn_PersonWhere);
    PORT_FN(WordsFn_BuildingOpen);
    PORT_FN(WordsFn_BuildingsOverlap);
    PORT_FN(WordsFn_TryFriendAgrees);
    PORT_FN(WordsFn_BuildingCrowd);
    PORT_FN(WordsFn_WantClaimMatch);
    PORT_FN(WordsFn_TextWantClaim);
    PORT_FN(WordsFn_SelectBuildingB);
    PORT_FN(WordsFn_PersonAtBuilding);
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

    // ---- context accessors and text joining (batch 38)

    uint16_t Words_GetBuildingTypeB(GuestPtr<Words> words) { return words->ctx->buildingTypeB; }
    int Words_GetCancelAllowed(GuestPtr<Words> words) { return words->ctx->cancelAllowed; }
    int Words_GetFriendshipChanged(GuestPtr<Words> words) { return words->ctx->friendshipChanged; }
    void Words_SetArg(GuestPtr<Words> words, uint16_t arg) { words->ctx->arg = static_cast<int16_t>(arg); }
    void Words_SetFlag134(GuestPtr<Words> words) { words->ctx->flag134 = 1; }
    void Words_SetWantIndex(GuestPtr<Words> words, uint16_t index) { words->ctx->wantIndex = static_cast<int16_t>(index); }

    // The same, for the game's one WORDS object.
    int Words_CancelAllowed() { return Words_GetCancelAllowed(GuestPtr<Words>{0x001BDDF0u}); }
    int Words_FriendshipChanged() { return Words_GetFriendshipChanged(GuestPtr<Words>{0x001BDDF0u}); }

    // The goods record of building type B for family B and the time of year ((month mod 12) / 4), entry `index`.
    int Words_GetGoods(GuestPtr<Words> words, int index)
    {
        port::GuestLocal<int32_t> offset;
        *offset = index;
        const GuestPtr<WordsCtx> ctx = words->ctx;
        const int quarter = ctx->city->monthCount % 12 / 4;
        return Goods_GetRecordOffset(ctx->buildingTypeB, ctx->familyB, quarter, offset);
    }

    // Points the context's bag slot at the index-th item of goods category 0x10, 0x11 or 0x13 in the bag, looking
    // up to the first empty slot.
    void Words_SelectBagItem(GuestPtr<Words> words, int index)
    {
        const GuestPtr<WordsCtx> ctx = words->ctx;
        const GuestPtr<Bag> bag = ctx->city->bag;
        const uint32_t groups = port::load<uint32_t>(0x001BDA9Cu), records = port::load<uint32_t>(0x001BDAA0u);
        const uint32_t first = bag.address + 4u;
        const uint32_t end = first + 16u * port::load<uint8_t>(globals::kBagSlotsBySize + bag->size);
        int count = 0;
        for (uint32_t slot = first; port::load<uint16_t>(slot) != 0 && slot < end; slot += 16u)
        {
            const uint32_t record = port::load<uint16_t>(groups + 4u * port::load<uint16_t>(slot)) + port::load<uint16_t>(slot + 2u);
            const uint8_t category = port::load<uint8_t>(records + 0x88u * record + 4u);
            if (category != 0x10 && category != 0x11 && category != 0x13)
                continue;
            if (count++ == index)
            {
                ctx->bagSlot = GuestPtr<void>{slot};
                return;
            }
        }
    }

    // Appends a piece of text to a line being built, joined the way English wants it: a space between words, none
    // after a line break, '(' or '<', or before a line break or punctuation ('.', '?', ',', ')', '>', an
    // apostrophe). Two full stops make one, and a word ending in s takes "'s" as a bare "'".
    void Words_AppendClipped(GuestPtr<char> buffer, GuestPtr<char> text)
    {
        if (!text)
            return;
        uint32_t from = text.address;
        const int length = sdk::strlen(buffer.address);
        if (length != 0)
        {
            const uint32_t lastAt = buffer.address + static_cast<uint32_t>(length) - 1u;
            const int8_t last = port::load<int8_t>(lastAt), first = port::load<int8_t>(from);
            const bool joined = last == '\n' || last == '\r' || last == '(' || last == '<' || first == '\n' || first == '\r' ||
                                first == '.' || first == '?' || first == ',' || first == ')' || first == '>' || first == '\'';
            if (!joined)
                sdk::strcat(buffer.address, 0x001BAD78u); // " "
            else if (last == '.' && first == '.')
                port::store<int8_t>(lastAt, 0);
            else if (first == '\'' && last == 's' && port::load<int8_t>(from + 1u) == 's')
            {
                sdk::strcat(buffer.address, 0x001BAD80u); // "'"
                from += 2u;
            }
        }
        sdk::strcat(buffer.address, from);
    }

    PORT_FN(Words_GetBuildingTypeB);
    PORT_FN(Words_GetCancelAllowed);
    PORT_FN(Words_GetFriendshipChanged);
    PORT_FN(Words_SetArg);
    PORT_FN(Words_SetFlag134);
    PORT_FN(Words_SetWantIndex);
    PORT_FN(Words_CancelAllowed);
    PORT_FN(Words_FriendshipChanged);
    PORT_FN(Words_GetGoods);
    PORT_FN(Words_SelectBagItem);
    PORT_FN(Words_AppendClipped);
}
