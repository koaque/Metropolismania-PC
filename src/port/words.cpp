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

    // ---- script banks: entries, answers, choice counts (batch 39)

    namespace
    {
        // The WORDS text banks: records of 0x18 bytes at 0x7181C0, each with tables of line offsets (+0) into line
        // codes (+4), choice ranges (+8), answer records (+0xC, four bytes: mode, first string), string offsets
        // (+0x10) and strings (+0x14). Bank 2 belongs to the building type of the talk: its tables are indexed
        // through per-type remap tables (type records of 8 bytes at *0x718220; remaps for lines *0x718224, choices
        // *0x718228, answers *0x71822C).
        uint32_t bankTable(int bank, uint32_t field) { return port::load<uint32_t>(0x007181C0u + 0x18u * static_cast<uint32_t>(bank) + field); }

        uint32_t remapped(uint32_t remap, int index, uint32_t part)
        {
            const uint32_t type = port::load<uint32_t>(0x00718220u) + 8u * Words_GetBuildingTypeB(GuestPtr<Words>{0x001BDDF0u});
            return port::load<uint16_t>(port::load<uint32_t>(remap) + 2u * static_cast<uint32_t>(index + port::load<uint16_t>(type + part)));
        }

        // The code at the script's current line (Words +0x10) and column (+0x14) of its line table (+0x24).
        int currentCode()
        {
            const int code = port::load<int16_t>(port::load<uint32_t>(0x001BDE14u) + 2u * port::load<uint16_t>(0x001BDE00u)) - 5000;
            const uint32_t column = port::load<uint16_t>(0x001BDE04u);
            const int bank = code / 1000, index = code % 1000;
            const uint32_t line = bank == 2 ? remapped(0x00718224u, index, 0) : port::load<uint16_t>(bankTable(bank, 0) + 2u * static_cast<uint32_t>(index));
            return port::load<int16_t>(bankTable(bank, 4) + 2u * (column + line));
        }

        // The answer record of an answer code (5000 and up).
        uint32_t answerRecord(int code)
        {
            const int bank = (code - 5000) / 1000, index = (code - 5000) % 1000;
            if (bank == 2)
                return bankTable(2, 0xC) + 4u * remapped(0x0071822Cu, index, 4);
            return bankTable(bank, 0xC) + 4u * static_cast<uint32_t>(index);
        }

        uint32_t bankString(int bank, uint32_t index) { return bankTable(bank, 0x14) + port::load<uint16_t>(bankTable(bank, 0x10) + 2u * index); }
    }

    // The answer mode of the current entry.
    int16_t Words_GetEntryMode() { return port::load<int16_t>(answerRecord(currentCode())); }

    // The default answer of the current entry: its first string.
    GuestPtr<char> Words_GetEntryDefault()
    {
        const int code = currentCode();
        return GuestPtr<char>{bankString((code - 5000) / 1000, port::load<uint16_t>(answerRecord(code) + 2u))};
    }

    // Checks a typed answer against the current entry's other strings (from its second up to the next entry's
    // first); the column moves on once, and once more on a match.
    void Words_MatchTypedAnswer(int, GuestPtr<char> typed)
    {
        const int code = currentCode();
        const int bank = (code - 5000) / 1000;
        const uint32_t record = answerRecord(code);
        const int strings = port::load<uint16_t>(record + 6u) - port::load<uint16_t>(record + 2u);
        for (int i = 1; i < strings; ++i)
        {
            if (sdk::strcmp(typed.address, bankString(bank, port::load<uint16_t>(answerRecord(code) + 2u) + static_cast<uint32_t>(i))) == 0)
            {
                port::store<uint16_t>(0x001BDE04u, static_cast<uint16_t>(port::load<uint16_t>(0x001BDE04u) + 1));
                break;
            }
        }
        port::store<uint16_t>(0x001BDE04u, static_cast<uint16_t>(port::load<uint16_t>(0x001BDE04u) + 1));
    }

    // The number of choices of a choice code: from bank choice ranges for codes below -2005; for -2005 the entries
    // of bank 1's first range not yet answered (the mask byte at Words +0x28); for -2004 the running events plus
    // one; for -2003 the wants that still hold; for -2002 one more than the bag's food (goods categories 0x10, 0x11
    // and 0x13); otherwise 5.
    int Words_CountChoices(int, GuestPtr<CityScene> city, int code)
    {
        if (code < -2005)
        {
            const int bank = (code + 6000) / 1000, index = (code + 6000) % 1000;
            const uint32_t range = bank == 2 ? bankTable(2, 8) + 2u * remapped(0x00718228u, index, 2)
                                             : bankTable(bank, 8) + 2u * static_cast<uint32_t>(index);
            return port::load<uint16_t>(range + 2u) - port::load<uint16_t>(range);
        }
        if (code == -2005)
        {
            const uint32_t range = bankTable(1, 8);
            const int entries = port::load<uint16_t>(range + 2u) - port::load<uint16_t>(range);
            int left = entries;
            for (int i = 0; i < entries; ++i)
                if ((port::load<uint8_t>(0x001BDE18u) & (1u << (i & 31))) != 0)
                    --left;
            return left;
        }
        if (code == -2004)
            return Event_CountActive(city->events) + 1;
        if (code == -2003)
        {
            port::GuestFrame<0x20> frame;
            const uint32_t sp = frame.ptr().address;
            const GuestPtr<int32_t> iterator{sp + 0xCu};
            *iterator = 0;
            int found = 0;
            while (Wants_Find(city->wants, city, GuestPtr<uint32_t>{sp}, GuestPtr<uint32_t>{sp + 4u}, GuestPtr<uint32_t>{sp + 8u}, iterator) != 0)
                ++found;
            return found;
        }
        if (code != -2002)
            return 5;
        const GuestPtr<Bag> bag = city->bag;
        const uint32_t groups = port::load<uint32_t>(0x001BDA9Cu), records = port::load<uint32_t>(0x001BDAA0u);
        const uint32_t end = bag.address + 4u + 16u * port::load<uint8_t>(globals::kBagSlotsBySize + bag->size);
        int count = 1;
        for (uint32_t slot = bag.address + 4u; port::load<uint16_t>(slot) != 0 && slot < end; slot += 16u)
        {
            const uint32_t record = port::load<uint16_t>(groups + 4u * port::load<uint16_t>(slot)) + port::load<uint16_t>(slot + 2u);
            const uint8_t category = port::load<uint8_t>(records + 0x88u * record + 4u);
            if (category == 0x10 || category == 0x11 || category == 0x13)
                ++count;
        }
        return count;
    }

    // Person B: from a random one of the candidate buildings on (each once), a resident of the building's family
    // whose character type fits the argument: a negative argument names a zero-ended type list (pointer table
    // 0x1BE950, indexed back from it), up to 1000 is the type itself, above that type arg / 3 with person flags & 3
    // equal to arg % 3. Person B, its building and their family, member and type are set; 0 when none fits.
    int Words_PickResidentByType(GuestPtr<WordsCtx> ctx)
    {
        const int32_t random = static_cast<int32_t>(Random_Next());
        const int count = ctx->candidateCount;
        int pick = count != 0 ? random % count : 0;
        port::GuestLocal<uint16_t> cursor;
        for (int tries = 0; tries < ctx->candidateCount; ++tries, ++pick)
        {
            if (pick >= ctx->candidateCount)
                pick = 0;
            const GuestPtr<MapObject> building = Map_GetRecord(ctx->city->map, ctx->candidates[pick]);
            *cursor = building->firstPerson;
            while (const GuestPtr<MapPerson> person = People_Next(ctx->city->map, cursor))
            {
                const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
                if ((cls->flags & 1u) != 0 || person->family != building->family)
                    continue;
                const int arg = ctx->arg;
                bool fits = false;
                if (arg < 0)
                {
                    for (uint32_t type = port::load<uint32_t>(0x001BE950u + 4u * static_cast<uint32_t>(arg)); port::load<uint8_t>(type) != 0; ++type)
                        if (person->charType == port::load<uint8_t>(type))
                        {
                            fits = true;
                            break;
                        }
                }
                else if (arg < 1001)
                    fits = person->charType == arg;
                else
                    fits = person->charType == arg / 3 && arg % 3 == static_cast<int>(person->flags & 3u);
                if (!fits)
                    continue;
                ctx->personB = person;
                ctx->familyB = person->family;
                ctx->memberB = person->member;
                ctx->buildingB = building;
                ctx->buildingTypeB = building->type;
                return 1;
            }
        }
        return 0;
    }

    PORT_FN(Words_GetEntryMode);
    PORT_FN(Words_GetEntryDefault);
    PORT_FN(Words_MatchTypedAnswer);
    PORT_FN(Words_CountChoices);
    PORT_FN(Words_PickResidentByType);

    // ---- Lifetime (plan P6 batch 1) ----

    // The script interpreter's 0x140-byte context is allocated with the object.
    GuestPtr<Words> Words_ctor(GuestPtr<Words> words)
    {
        words->ctx = GuestPtr<WordsCtx>{sdk::operator_new(0x140u)};
        return words;
    }

    GuestPtr<Words> Words_dtor(GuestPtr<Words> words, int16_t deleteFlag)
    {
        if (words)
        {
            sdk::operator_delete(words->ctx.address);
            if (0 < deleteFlag)
                sdk::operator_delete(words.address);
        }
        return words;
    }

    PORT_FN(Words_ctor);
    PORT_FN(Words_dtor);

    // ---- Family records (plan P6 batch 2): these read the family cache through Family_Get, which can wait on the file queue ----

    namespace
    {
        // A family record holds six members of 0x48 bytes from +0x24; fields are read by guest address, so a wild member
        // number wraps in RDRAM like the original's indexing.
        constexpr uint32_t kMembersAt = 0x24u, kMemberSize = 0x48u;
        uint32_t memberAt(GuestPtr<Family> family, uint32_t member) { return family.address + kMembersAt + kMemberSize * member; }
        constexpr uint32_t kMemberCharType = 0x00u, kMemberFlags = 0x03u, kMemberRole = 0x04u, kMemberUnk06 = 0x06u, kMemberRange = 0x07u,
                           kMemberName = 0x28u, kMemberName2 = 0x38u;

        // The member named by the argument: B when it is 1, A when it is 0, otherwise the context's result family, member 0.
        struct MemberRef
        {
            uint32_t family, member;
        };
        MemberRef memberByThreeWayArg(GuestPtr<WordsCtx> ctx)
        {
            if (ctx->arg == 1)
                return {ctx->familyB, ctx->memberB};
            if (ctx->arg == 0)
                return {ctx->familyA, ctx->memberA};
            return {ctx->result, 0};
        }
    }

    // The family record of the building's family (arg 1), of family A (arg 0), or of the result family.
    GuestPtr<char> WordsFn_TextFamilyRecord(GuestPtr<WordsCtx> ctx)
    {
        uint32_t family;
        if (ctx->arg == 1)
            family = ctx->buildingB->family;
        else if (ctx->arg == 0)
            family = ctx->familyA;
        else
            family = ctx->result;
        return GuestPtr<char>{Family_Get(static_cast<int>(family), 0).address};
    }

    GuestPtr<char> WordsFn_TextMemberName2(GuestPtr<WordsCtx> ctx)
    {
        const MemberRef who = memberByThreeWayArg(ctx);
        return GuestPtr<char>{memberAt(Family_Get(static_cast<int>(who.family), 0), who.member) + kMemberName2};
    }

    GuestPtr<char> WordsFn_TextMemberName(GuestPtr<WordsCtx> ctx)
    {
        const MemberRef who = memberByThreeWayArg(ctx);
        return GuestPtr<char>{memberAt(Family_Get(static_cast<int>(who.family), 0), who.member) + kMemberName};
    }

    // The member's post at their home's building type (STRTYPE POSTNAME).
    GuestPtr<char> WordsFn_TextOccupation(GuestPtr<WordsCtx> ctx)
    {
        const MemberRef who = memberByThreeWayArg(ctx);
        const GuestPtr<Family> family = Family_Get(static_cast<int>(who.family), 0);
        const uint32_t homeType = port::load<uint16_t>(family.address + 0x20u);
        const uint8_t role = port::load<uint8_t>(memberAt(family, who.member) + kMemberRole);
        return BldgType_GetPostName(GuestPtr<BuildingType>{globals::buildingTypeAddress(homeType)}, role);
    }

    uint8_t WordsFn_MemberByte2B(GuestPtr<WordsCtx> ctx)
    {
        return port::load<uint8_t>(memberAt(Family_Get(ctx->familyA, 0), ctx->memberA) + kMemberRange);
    }

    uint8_t WordsFn_MemberByte2A(GuestPtr<WordsCtx> ctx)
    {
        return port::load<uint8_t>(memberAt(Family_Get(ctx->familyA, 0), ctx->memberA) + kMemberUnk06);
    }

    // The character type of member A, with the types above 0x1D folded: 0x26 becomes 0xD, the rest 0.
    uint16_t WordsFn_MemberCharType(GuestPtr<WordsCtx> ctx)
    {
        uint16_t charType = port::load<uint16_t>(memberAt(Family_Get(ctx->familyA, 0), ctx->memberA) + kMemberCharType);
        if (0x1D < charType)
            charType = charType == 0x26 ? 0xD : 0;
        return charType;
    }

    // 0..5 from the member's role and the flag bit 0: roles 2 gives 4/5, role 1 gives 3, role 0 gives 2, the others 0/1.
    int WordsFn_MemberRole(GuestPtr<WordsCtx> ctx)
    {
        GuestPtr<Family> family;
        uint32_t member;
        if (ctx->arg == 0)
        {
            family = Family_Get(ctx->familyA, 0);
            member = ctx->memberA;
        }
        else
        {
            family = Family_Get(ctx->familyB, 0);
            member = ctx->memberB;
        }
        const uint8_t role = port::load<uint8_t>(memberAt(family, member) + kMemberRole);
        const bool flag = (port::load<uint8_t>(memberAt(family, member) + kMemberFlags) & 1u) != 0;
        if (role == 2)
            return flag ? 5 : 4;
        if (role == 1)
            return 3;
        if (role == 0)
            return 2;
        return flag ? 1 : 0;
    }

    // The building type's STRTYPE category: of the building (arg 0; the family's home type when there is no building)
    // or of building type B.
    uint8_t WordsFn_BuildingCategory(GuestPtr<WordsCtx> ctx)
    {
        uint32_t type;
        if (ctx->arg == 0)
        {
            if (!ctx->buildingA)
                type = port::load<uint16_t>(Family_Get(ctx->familyA, 0).address + 0x20u);
            else
                type = ctx->buildingA->type;
        }
        else
            type = ctx->buildingTypeB;
        return port::load<uint8_t>(globals::buildingTypeAddress(type) + 0x145u);
    }

    namespace
    {
        // The friendship change both actions make: the meter step is 3 (down) or 5 (up) plus bits 21-23 of the character
        // type's class word; the result is what the friendship call leaves in v0.
        int changeFriendship(GuestPtr<WordsCtx> ctx, int base, int sign)
        {
            const GuestPtr<Family> family = Family_Get(ctx->familyA, 0);
            const uint32_t charType = port::load<uint16_t>(memberAt(family, ctx->memberA) + kMemberCharType);
            const uint32_t cls = port::load<uint32_t>(globals::charClassTable.get() + 4u * charType);
            const int bits = static_cast<int>((port::load<uint32_t>(cls + 0x1Cu) & 0xE00000u) >> 0x15);
            const int points = bits + base;
            port::setGuestV0(bits);  // the friendship call returns v0 as it found it on some paths: the shifted bits here
            const int result = Family_AddFriendshipScaled(ctx->familyA, ctx->memberA, GuestPtr<int32_t>{ctx->city.address + 0x8Cu}, points, sign);
            ctx->friendshipChanged = 1;
            return result;
        }
    }

    int WordsFn_FriendshipDown(GuestPtr<WordsCtx> ctx) { return changeFriendship(ctx, 3, -1); }
    int WordsFn_FriendshipUp(GuestPtr<WordsCtx> ctx) { return changeFriendship(ctx, 5, 1); }

    PORT_FN(WordsFn_TextFamilyRecord);
    PORT_FN(WordsFn_TextMemberName2);
    PORT_FN(WordsFn_TextMemberName);
    PORT_FN(WordsFn_TextOccupation);
    PORT_FN(WordsFn_MemberByte2B);
    PORT_FN(WordsFn_MemberByte2A);
    PORT_FN(WordsFn_MemberCharType);
    PORT_FN(WordsFn_MemberRole);
    PORT_FN(WordsFn_BuildingCategory);
    PORT_FN(WordsFn_FriendshipDown);
    PORT_FN(WordsFn_FriendshipUp);

    // ---- Script context set-up and the resident pickers ----

    namespace
    {
        // The fields both set-up calls clear: the second person and building, the results and the script's flags.
        void resetContext(GuestPtr<WordsCtx> ctx, GuestPtr<CityScene> city, int arg4)
        {
            ctx->unk_04 = arg4;
            ctx->city = city;
            ctx->familyB = 0;
            ctx->memberB = 0;
            ctx->personB = GuestPtr<MapPerson>{0u};
            ctx->buildingB = GuestPtr<MapObject>{0u};
            ctx->buildingTypeB = 0;
            ctx->bagSlot = GuestPtr<void>{0u};
            ctx->result = 0;
            ctx->cancelAllowed = 1;
            ctx->flag134 = 0;
            ctx->arg = 0;
            ctx->eventSlot = 0;
            ctx->wantIndex = 0;
            ctx->friendshipChanged = 0;
        }
    }

    // Starts a script on a family member: the family is prefetched into the cache, person and building A stay empty.
    void Words_SetFamily(GuestPtr<Words> words, int family, uint16_t member, int arg4, GuestPtr<CityScene> city)
    {
        Family_Prefetch(family, 0);
        const GuestPtr<WordsCtx> ctx = words->ctx;
        ctx->familyA = static_cast<uint16_t>(family);
        ctx->memberA = member;
        ctx->personA = GuestPtr<MapPerson>{0u};
        ctx->buildingA = GuestPtr<MapObject>{0u};
        resetContext(ctx, city, arg4);
    }

    // The same for a person on the map: building A is the person's workplace.
    void Words_SetPerson(GuestPtr<Words> words, GuestPtr<MapPerson> person, int arg4, GuestPtr<CityScene> city)
    {
        Family_Prefetch(person->family, 0);
        const GuestPtr<WordsCtx> ctx = words->ctx;
        ctx->familyA = person->family;
        ctx->memberA = person->member;
        ctx->personA = person;
        ctx->buildingA = Map_GetRecord(city->map, person->work);
        resetContext(ctx, city, arg4);
    }

    // Lists (in ctx->candidates, up to 128) the ids of the objects the family may call on: the occupied homes with
    // people inside whose entrance lies within the member's range of the building, and the objects on the same tile
    // as each, except the building itself.
    void Words_CollectNearbyHomes(GuestPtr<WordsCtx> ctx, GuestPtr<MapObject> building)
    {
        ctx->candidateCount = 0;
        sdk::memset(ctx.address + 0x30u, 0, 0x80);
        const int32_t x0 = building->tile & 0x7F;
        const int32_t z0 = building->tile >> 7;
        const int32_t size = port::load<uint8_t>(globals::buildingTypeAddress(building->type) + 0x142u);
        const GuestPtr<Family> family = Family_Get(ctx->familyA, 0);
        const int32_t range = port::load<int32_t>(0x001B97F0u + 4u * port::load<uint8_t>(memberAt(family, ctx->memberA) + kMemberRange));
        const int32_t xStart = x0 - range, xEnd = x0 + size + range, zEnd = z0 + size + range;
        uint16_t homes[80];
        int32_t found = 0;
        for (int32_t z = z0 - range; z < zEnd && found < 0x50; ++z)
        {
            for (int32_t x = xStart; x < xEnd && found < 0x50; ++x)
            {
                const GuestPtr<MapObject> object = Map_GetTileObject(ctx->city->map, x, z);
                if (object->id == 0 || (object->flags & 0x20) == 0 || (globals::buildingTypeFlags(object->type) & 0x40) == 0)
                    continue;
                if (Family_InCity(object->family) == 0)
                    continue;
                bool known = false;
                for (int32_t i = 0; i < found && !known; ++i)
                    known = object->id == homes[i];
                if (!known)
                    homes[found++] = object->id;
            }
        }
        for (int32_t i = 0; i < found; ++i)
        {
            GuestPtr<MapObject> record = Map_GetRecord(ctx->city->map, homes[i]);
            for (;;)
            {
                if (record.address != building.address)
                {
                    const int32_t at = ctx->candidateCount;
                    port::store<uint16_t>(ctx.address + 0x30u + 2u * static_cast<uint32_t>(at), record->id);
                    ctx->candidateCount = at + 1;
                    if (!(at + 1 < 0x80))
                        return;
                }
                if (record->nextOnTile == 0x3F80)
                    break;
                record = Map_GetRecord(ctx->city->map, record->nextOnTile);
                if ((record->flags & 0x20) == 0 || Family_InCity(record->family) == 0)
                    break;
            }
        }
    }

    int WordsFn_PickByCandidates(GuestPtr<WordsCtx> ctx)
    {
        Words_CollectNearbyHomes(ctx, ctx->buildingA);
        return Words_PickResidentByType(ctx);
    }

    namespace
    {
        // Building B is a random candidate and person B a random member of its family: 1 when there is one.
        int pickRandomCandidate(GuestPtr<WordsCtx> ctx)
        {
            const int32_t roll = static_cast<int32_t>(Random_Next());
            const uint32_t index = static_cast<uint32_t>(eeRemainder(roll, ctx->candidateCount));
            ctx->buildingB = Map_GetRecord(ctx->city->map, port::load<uint16_t>(ctx.address + 0x30u + 2u * index));
            ctx->buildingTypeB = ctx->buildingB->type;
            const GuestPtr<Family> family = Family_Get(ctx->buildingB->family, 0);
            const uint32_t members = port::load<uint16_t>(family.address + 0x22u);
            if (members != 0)
            {
                const int32_t memberRoll = static_cast<int32_t>(Random_Next());
                ctx->personB = Map_FindPersonEntry(ctx->city->map, static_cast<int16_t>(ctx->buildingB->family),
                                                   static_cast<uint16_t>(eeRemainder(memberRoll, static_cast<int32_t>(members))));
                if (ctx->personB)
                {
                    ctx->familyB = ctx->personB->family;
                    ctx->memberB = ctx->personB->member;
                    return 1;
                }
            }
            return 0;
        }
    }

    int WordsFn_PickRandomResident(GuestPtr<WordsCtx> ctx)
    {
        Words_CollectNearbyHomes(ctx, ctx->buildingA);
        if (ctx->candidateCount == 0)
            return 0;
        return pickRandomCandidate(ctx);
    }

    // The first resident of a candidate home who can introduce someone and whose friendship agrees (3); else a resident
    // who can introduce someone but does not agree (2); else a random one as in PickRandomResident (1 or 0).
    int WordsFn_PickTestableResident(GuestPtr<WordsCtx> ctx)
    {
        int result = 0;
        Words_CollectNearbyHomes(ctx, ctx->buildingA);
        for (int32_t i = 0; i < ctx->candidateCount; ++i)
        {
            const GuestPtr<MapObject> object = Map_GetRecord(ctx->city->map, port::load<uint16_t>(ctx.address + 0x30u + 2u * static_cast<uint32_t>(i)));
            port::GuestLocal<uint16_t> cursor;
            *cursor = object->firstPerson;
            while (const GuestPtr<MapPerson> person = People_Next(ctx->city->map, cursor))
            {
                const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
                if ((cls->flags & 1u) != 0 || person->family != object->family)
                    continue;
                ctx->personB = person;  // stored before the test of introduces: the original does it in a branch delay slot
                if (person->introduces == 0)
                    continue;
                ctx->familyB = ctx->personB->family;
                ctx->memberB = ctx->personB->member;
                ctx->buildingB = object;
                ctx->buildingTypeB = ctx->buildingB->type;
                const int agrees = Family_FriendAgrees(ctx->familyB, ctx->memberB, ctx->city->approval);
                result = 2;
                if (agrees != 0)
                    return 3;
            }
        }
        if (result != 0)
            return result;
        if (ctx->candidateCount == 0)
            return 0;
        return pickRandomCandidate(ctx);
    }

    // Person B is a random other member of building B's family (not person A itself): 1 when found.
    int WordsFn_PickOtherResident(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<Family> family = Family_Get(ctx->buildingB->family, 0);
        const uint32_t members = port::load<uint16_t>(family.address + 0x22u);
        if (members == 0)
            return 0;
        if (members < 2 && ctx->buildingB->family == ctx->familyA)
            return 0;
        int32_t member;
        do
        {
            const int32_t roll = static_cast<int32_t>(Random_Next());
            member = remainder(roll, static_cast<int32_t>(members));
            if (ctx->buildingB->family == ctx->familyA && static_cast<uint32_t>(member) == ctx->personA->member)
                member = -1;
        } while (member < 0);
        ctx->personB = Map_FindPersonEntry(ctx->city->map, static_cast<int16_t>(ctx->buildingB->family), static_cast<uint16_t>(member));
        if (!ctx->personB)
            return 0;
        ctx->familyB = ctx->personB->family;
        ctx->memberB = ctx->personB->member;
        return 1;
    }

    // The first of the member's acquaintances that lives in a business of category 5 and may apply (becomes the result).
    int WordsFn_FindOutsideAcquaintance(GuestPtr<WordsCtx> ctx)
    {
        const MemberRef who = ctx->arg == 0 ? MemberRef{ctx->familyA, ctx->memberA} : MemberRef{ctx->familyB, ctx->memberB};
        const uint32_t member = memberAt(Family_Get(who.family, 0), who.member);
        const uint32_t end = member + 0x08u + 0x20u;
        for (uint32_t at = member + 0x08u; port::load<uint16_t>(at) != 0 && at < end; at += 2u)
        {
            const uint32_t acquaintance = port::load<uint16_t>(at);
            const uint32_t type = static_cast<uint32_t>(Family_GetTypeIndex(static_cast<int>(acquaintance)));
            if (port::load<uint8_t>(globals::buildingTypeAddress(type) + 0x145u) != 5)
                continue;
            if (Family_CanApply(acquaintance, ctx->city) != 0)
            {
                ctx->result = static_cast<uint16_t>(acquaintance);
                break;
            }
        }
        return ctx->result != 0 ? 1 : 0;
    }

    // Whether the character likes the bag item: for a business of category 4 it is the item 0x96 and the rule 0; else the
    // item's id is in the character type's list of liked items.
    int WordsFn_LikesBagItem(GuestPtr<WordsCtx> ctx)
    {
        const uint32_t bagSlot = ctx->bagSlot.address;
        if (ctx->buildingA && port::load<uint16_t>(bagSlot) == 0x96)
        {
            const uint32_t btype = globals::buildingTypeAddress(ctx->buildingA->type);
            if (port::load<uint8_t>(btype + 0x145u) != 4)
                return 0;
            return port::load<uint8_t>(btype + 0x144u) == 0 ? 1 : 0;
        }
        const uint32_t group = port::load<uint16_t>(bagSlot);
        const uint32_t index = port::load<uint16_t>(bagSlot + 2u);
        const uint32_t first = port::load<uint16_t>(globals::goodsSectionA.get() + 4u * group);
        const uint32_t item = port::load<uint16_t>(globals::goodsSectionB.get() + 0x88u * (index + first));
        uint32_t charType;
        if (ctx->personA)
            charType = ctx->personA->charType;
        else
            charType = port::load<uint16_t>(memberAt(Family_Get(ctx->familyA, 0), ctx->memberA) + kMemberCharType);
        const uint32_t likes = globals::goodsSectionD.get() + 2u * charType;
        const uint32_t list = globals::goodsSectionE.get();
        for (uint32_t at = list + 2u * port::load<uint16_t>(likes); at < list + 2u * port::load<uint16_t>(likes + 2u); at += 2u)
            if (port::load<uint16_t>(at) == item)
                return 1;
        return 0;
    }

    PORT_FN(Words_SetFamily);
    PORT_FN(Words_SetPerson);
    PORT_FN(Words_CollectNearbyHomes);
    PORT_FN(WordsFn_PickByCandidates);
    PORT_FN(WordsFn_PickRandomResident);
    PORT_FN(WordsFn_PickTestableResident);
    PORT_FN(WordsFn_PickOtherResident);
    PORT_FN(WordsFn_FindOutsideAcquaintance);
    PORT_FN(WordsFn_LikesBagItem);

    // ---- The WORDS script files (plan P6 batch 2) ----

    // Opens the script files on the first call (WORDS.BIN and WORDSEVE.BIN, 0x8000-byte buffers) and counts the users; the
    // object gets a state of 0 and a block buffer of one script block.
    int Words_OpenFiles(int words)
    {
        constexpr uint32_t kBlockSize = 0x001BDDE8u, kUsers = 0x001BDDE4u, kWordsFile = 0x001BDDDCu, kEventFile = 0x001BDDE0u;
        port::store<int32_t>(static_cast<uint32_t>(words), 0);
        port::store<uint32_t>(static_cast<uint32_t>(words) + 4u, sdk::mw_NewArray(port::load<uint32_t>(kBlockSize)));
        if (port::load<int32_t>(kUsers) == 0)
        {
            const auto open = [](uint32_t holder, uint32_t path) {
                const uint32_t memory = sdk::operator_new(8u);
                uint32_t file = 0u;
                if (memory != 0u)
                    file = File_ctor(GuestPtr<GameFile>{memory}, 0x8000).address;
                port::store<uint32_t>(holder, file);
                File_Open(GuestPtr<GameFile>{file}, GuestPtr<char>{path});
            };
            open(kWordsFile, 0x001BAD90u);
            open(kEventFile, 0x001BADB0u);
            FileQueue_WaitAll(0);
        }
        port::store<int32_t>(kUsers, port::load<int32_t>(kUsers) + 1);
        return words;
    }

    // Finds a script block in the cache of 8 (a most-recently-used list at 0x718190, each entry reached through two
    // pointers): block 0 means the first entry's block (0x16 when that is 0 too). A block not cached replaces the last entry:
    // it is read from WORDS.BIN (block number - 1 blocks past the first word) into that entry. The entry found moves to the
    // front of the list. Returns the first entry.
    GuestPtr<uint32_t> Words_ReadBlock(int block)
    {
        constexpr uint32_t kList = 0x00718190u, kListEnd = 0x007181B0u;
        constexpr uint32_t kBlockSize = 0x001BDDE8u, kWordsFile = 0x001BDDDCu, kReadResult = 0x001BD9F4u, kCleared = 0x001BDDFCu;
        const auto idAt = [](uint32_t slot) { return port::load<int32_t>(port::load<uint32_t>(port::load<uint32_t>(slot))); };
        if (block == 0)
        {
            block = idAt(kList);
            if (block == 0)
                block = 0x16;
        }
        uint32_t slot = kList;
        while (slot < kListEnd && block != idAt(slot))
            slot += 4u;
        if (slot != kList)
        {
            if (slot == kListEnd)
            {
                slot -= 4u;
                const uint32_t record = port::load<uint32_t>(port::load<uint32_t>(slot));
                port::store<int32_t>(record, block);
                const int32_t size = port::load<int32_t>(kBlockSize);
                File_Seek(GuestPtr<GameFile>{port::load<uint32_t>(kWordsFile)}, size * (port::load<int32_t>(record) - 1) + 4, 0, 0);
                port::store<int32_t>(kReadResult, File_Read(GuestPtr<GameFile>{port::load<uint32_t>(kWordsFile)}, static_cast<int>(port::load<uint32_t>(record + 4u)), 1, size, 0));
            }
            const uint32_t entry = port::load<uint32_t>(slot);
            sdk::memmove(kList + 4u, kList, slot - kList);
            port::store<uint32_t>(kList, entry);
            port::store<uint32_t>(kCleared, 0u);
        }
        return GuestPtr<uint32_t>{port::load<uint32_t>(kList)};
    }

    // The static Words object (0x1BDDF0) is constructed at start-up and registered to be destroyed at exit.
    void StaticInit_Words()
    {
        const GuestPtr<Words> words = Words_ctor(GuestPtr<Words>{0x001BDDF0u});
        sdk::mw_register_global_object(words.address, 0x00180490u, 0x007181B0u);
    }

    PORT_FN(Words_OpenFiles);
    PORT_FN(Words_ReadBlock);
    PORT_FN(StaticInit_Words);

    // ---- Script actions that forward to other calls (plan P6 batch 2) ----

    // The title text of the event in the script's slot.
    GuestPtr<char> WordsFn_TextEventTitle(GuestPtr<WordsCtx> ctx) { return Event_FormatText(ctx->city->events, ctx->eventSlot); }

    // The person the script talks to gets a new family to introduce (the 16-bit pick is kept at +0x2C); the result is the pick,
    // or what the register held when there is no such person.
    int WordsFn_NewIntroduction(GuestPtr<WordsCtx> ctx)
    {
        if (!ctx->personA)
            return port::guestV0();
        const GuestPtr<MapPerson> person = ctx->personA;
        const uint32_t pick = Family_PickIntroduction(person->family, person->member, ctx->city);
        ctx->personA->introduces = static_cast<uint16_t>(pick);
        return static_cast<int>(pick);
    }

    PORT_FN(WordsFn_TextEventTitle);
    PORT_FN(WordsFn_NewIntroduction);

    // The family introduction of the script's first side (when the arg at +0x2A is 0) or second side: a person there keeps their
    // own (+0x2C), otherwise one is picked for the family and member. The pick (16 bits) becomes the result (+0x28); 1 when
    // it is not 0.
    int WordsFn_GetIntroduction(GuestPtr<WordsCtx> ctx)
    {
        uint32_t pick;
        if (ctx->arg != 0)
            pick = ctx->personB ? static_cast<uint32_t>(ctx->personB->introduces) : Family_PickIntroduction(ctx->familyB, ctx->memberB, ctx->city);
        else
            pick = ctx->personA ? static_cast<uint32_t>(ctx->personA->introduces) : Family_PickIntroduction(ctx->familyA, ctx->memberA, ctx->city);
        ctx->result = static_cast<uint16_t>(pick);
        return ctx->result != 0 ? 1 : 0;
    }

    PORT_FN(WordsFn_GetIntroduction);

    // Chooses event `choice` - 1 of the city's list for the script (0 is no choice and gives 0): asks whether the first person
    // of the script can take part in it (mm_Event_FindForFamily); the result is its code plus one.
    int Words_SelectEvent(GuestPtr<Words> words, int choice)
    {
        port::GuestFrame<0x20> frame;
        if (choice == 0)
            return 0;
        words->ctx->eventSlot = static_cast<int16_t>(choice - 1);
        const uint32_t pair = frame.ptr().address + 0x1Cu;
        port::store<uint16_t>(pair, words->ctx->familyA);
        port::store<uint16_t>(pair + 2u, words->ctx->memberA);
        const GuestPtr<CityScene> city = words->ctx->city;
        return Event_FindForFamily(city->events, city, words->ctx->eventSlot, GuestPtr<int16_t>{pair}) + 1;
    }

    PORT_FN(Words_SelectEvent);

    // Applies the choice made in a script's menu (`code` says which menu, `choice` the entry). The running total of the answers
    // (a halfword at 0x1BDE04, or at 0x1BDE00 while that is 0) grows by the choice for the menus below -0x7D4 and for -0x7D1.
    // Menu -0x7D5 picks the choice-th entry not yet used (a bit mask of used ones at 0x1BDE18; the used ones before it count into
    // the total), marks it used and, when fewer than two of the table's entries are left, sets the script's flag 0x134; -0x7D4
    // adds the event chosen (mm_Words_SelectEvent); -0x7D2 selects the bag item when the choice is not the last entry and counts
    // it; -0x7D3 sets the want index and -0x7D1 the argument.
    void Words_ApplyChoice(int script, GuestPtr<CityScene> city, int code, int choice)
    {
        constexpr uint32_t kWords = 0x001BDDF0u, kUsed = 0x001BDE18u;
        const GuestPtr<Words> words{kWords};
        const uint32_t total = port::load<uint16_t>(0x001BDE04u) != 0 ? 0x001BDE04u : 0x001BDE00u;
        const auto add = [&](uint32_t amount) { port::store<uint16_t>(total, static_cast<uint16_t>(port::load<uint16_t>(total) + amount)); };
        if (code < -0x7D4 || code == -0x7D1)
            add(static_cast<uint16_t>(choice));
        if (code == -0x7D5)
        {
            uint32_t bit = 0;
            int unused = 0;
            for (uint32_t i = 0;; ++i)
            {
                bit = 1u << (i & 31u);
                if ((port::load<uint8_t>(kUsed) & bit) == 0)
                {
                    ++unused;
                    if (choice < unused)
                        break;
                }
                else
                    add(1);
            }
            port::store<uint8_t>(kUsed, static_cast<uint8_t>(port::load<uint8_t>(kUsed) | (bit & 0xFFu)));
            const uint32_t table = port::load<uint32_t>(0x007181E0u);
            const int entries = static_cast<int>(port::load<uint16_t>(table + 2u)) - static_cast<int>(port::load<uint16_t>(table));
            int left = entries;
            for (int j = 0; j < entries; ++j)
                if ((port::load<uint8_t>(kUsed) & (1u << (j & 31))) != 0)
                    --left;
            if (left < 2)
                Words_SetFlag134(words);
        }
        if (code == -0x7D4)
            add(static_cast<uint16_t>(Words_SelectEvent(words, choice)));
        if (code == -0x7D2)
        {
            if (choice < Words_CountChoices(script, city, code) - 1)
            {
                Words_SelectBagItem(words, choice);
                add(1);
            }
        }
        else if (code == -0x7D3)
            Words_SetWantIndex(words, static_cast<uint16_t>(choice));
        else if (code == -0x7D1)
            Words_SetArg(words, static_cast<uint16_t>(choice));
    }

    PORT_FN(Words_ApplyChoice);

    // The script person's schedule state (kept in their record, or worked out for a family member who is not on the map) turned
    // into the number a talk script tests, by the script's argument (+0x2A): 0 gives states 1 and 2 as 0 and 1, 3 as 2 and the
    // rest 3; 1 gives 4 and 5 as 0 and 1, the rest 2; 2 gives 6 and 7 as 0 and 1, the rest 2; 3 gives states 6 to 12 as 0 to 6, the
    // rest 7; 4 gives 0 for state 13 and 1 for the others; 5 gives the state less 1 (less 2 from 15), 15 when that is negative.
    uint32_t WordsFn_ScheduleState(GuestPtr<WordsCtx> ctx)
    {
        const uint32_t state = (ctx->personA ? port::load<uint8_t>(ctx->personA.address + 0x26u) : Schedule_Evaluate(ctx->familyA, ctx->memberA)) & 0xFFu;
        switch (ctx->arg)
        {
        case 0:
            if (state - 1u < 2u)
                return state - 1u;
            return state == 3 ? 2u : 3u;
        case 1:
            return state == 4 ? 0u : (state == 5 ? 1u : 2u);
        case 2:
            return state == 6 ? 0u : (state == 7 ? 1u : 2u);
        case 3:
            return state >= 6 && state < 0xD ? state - 6u : 7u;
        case 4:
            return state != 0xD ? 1u : 0u;
        case 5:
        {
            const int32_t line = static_cast<int32_t>(state < 0xF ? state - 1u : state - 2u);
            return line < 0 ? 0xFu : static_cast<uint32_t>(line);
        }
        default:
            return 0;
        }
    }

    PORT_FN(WordsFn_ScheduleState);

    // The player buys the goods the script points at (group +0x14, variant +0x16, the season from the month at +0x88 of the city):
    // the price comes off the money at 0x1C5540 (saturating: a purchase that overflows leaves INT_MAX, any other shortfall 0) and
    // the seller's family gains friendship by price band (2 up to 100, 3 up to 1000, 4 up to 5000, 5 up to 10000, 20 up to
    // 50000, 30 above). What happens next depends on the goods' kind: kinds 4, 5, 10 and 11 add a laptop entry; 6 to 8 add one and
    // make the bag at least the size in byte 5; the goods kinds 0x10 to 0x1C that go into the bag are added to it (the laptop
    // entry 6 when the bag is new); 0x12 and 0x14 are eaten; 0x16 adds a music track; 0x19 adds an applicant family. Sound 0x12
    // plays at the end.
    void WordsFn_BuyGoods(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x40> frame;
        const uint32_t offsetAt = frame.ptr().address + 0x3Cu;
        port::store<int32_t>(offsetAt, ctx->arg);
        const GuestPtr<CityScene> city = ctx->city;
        const uint32_t c = city.address;
        int32_t month = remainder(port::load<int32_t>(c + 0x88u), 12);
        if (month < 0)
            month += 3;
        const uint32_t record = static_cast<uint32_t>(Goods_GetRecordOffset(ctx->buildingTypeB, ctx->familyB, month >> 2, GuestPtr<int32_t>{offsetAt}));
        const int32_t price = Goods_GetPrice(static_cast<int>(record));
        constexpr uint32_t kMoney = 0x001C5540u;
        const int32_t money = static_cast<int32_t>(port::load<uint32_t>(kMoney) - static_cast<uint32_t>(price));
        port::store<int32_t>(kMoney, money);
        if (money < 0)
            port::store<int32_t>(kMoney, static_cast<int32_t>(0u - static_cast<uint32_t>(price)) > 0 ? 0x7FFFFFFF : 0);
        int points;
        if (price < 0xB)
            points = 2;
        else if (price < 0x65)
            points = 2;
        else if (price < 0x3E9)
            points = 3;
        else if (price < 0x1389)
            points = 4;
        else if (price < 0x2711)
            points = 5;
        else
            points = price < 0xC351 ? 0x14 : 0x1E;
        Family_AddFriendship(ctx->familyA, ctx->memberA, GuestPtr<int32_t>{c + 0x8Cu}, points);
        const GuestPtr<Laptop> laptop{port::load<uint32_t>(c + 0x48u)};
        const uint32_t bag = port::load<uint32_t>(c + 0x60u);
        const uint32_t kind = port::load<uint8_t>(record + 4u);
        switch (kind)
        {
        case 4:
        case 5:
        case 10:
        case 11:
            Laptop_AddEntry(laptop, static_cast<int>(kind));
            break;
        case 6:
        case 7:
        case 8:
            Laptop_AddEntry(laptop, static_cast<int>(kind));
            if (port::load<uint16_t>(bag) < port::load<uint8_t>(record + 5u))
                port::store<uint16_t>(bag, port::load<uint8_t>(record + 5u));
            break;
        case 0x10:
        case 0x11:
        case 0x13:
        case 0x15:
        case 0x17:
        case 0x18:
        case 0x1A:
        case 0x1B:
        case 0x1C:
            if (port::load<uint16_t>(bag) == 0)
                Laptop_AddEntry(laptop, 6);
            Bag_AddItem(GuestPtr<Bag>{bag}, ctx->buildingTypeB, port::load<int32_t>(offsetAt), static_cast<int16_t>(port::load<int32_t>(0x001C5548u)),
                        port::load<int32_t>(c + 0x88u), static_cast<int16_t>(port::load<uint16_t>(ctx->buildingB.address + 0xCu)), 0xFF, 0);
            break;
        case 0x12:
        case 0x14:
            Stamina_Eat(GuestPtr<Stamina>{port::load<uint32_t>(c + 0x64u)}, GuestPtr<uint8_t>{record});
            break;
        case 0x16:
            Bag_AddMusicTrack(GuestPtr<Bag>{bag}, ctx->buildingTypeB, port::load<int32_t>(offsetAt));
            break;
        case 0x19:
            Applicants_Add(GuestPtr<Applicants>{port::load<uint32_t>(c + 0x54u)}, port::load<uint8_t>(record + 5u), port::load<int32_t>(c + 0x88u), 8);
            break;
        default:
            break;
        }
        Sound_PlaySe(0x12, 0, 0x3C, ps2f(0.0f), ps2f(0.0f));
    }

    PORT_FN(WordsFn_BuyGoods);

    // Looks through the acquaintances (up to 16 families at +0x2C of the script person's member record) of the script's family
    // for one that can apply to the city and whose home type claims something the building A supplies: the claim list of the
    // building (a single value, or the list of section D when the value has bit 15) is compared with the type's claims (section
    // C of the acquaintance's home type); the first match becomes the person's introduction (+0x2C) and the result (+0x28) and
    // gives 1. 0 when there is none.
    int WordsFn_AcquaintanceSuppliesClaim(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x60> frame;
        const uint32_t local = frame.ptr().address + 0x5Cu;
        const uint32_t building = ctx->buildingA.address;
        const uint32_t sections = static_cast<uint32_t>(Claim_GetSectionB(port::load<uint8_t>(globals::buildingTypeAddress(port::load<uint16_t>(building)) + 0x147u)));
        const uint32_t entry = static_cast<uint32_t>(sections) + 8u * port::load<uint8_t>(building + 0x12u);
        port::store<uint32_t>(local, port::load<uint32_t>(0x001BDC20u));
        port::store<uint16_t>(local, port::load<uint16_t>(entry + 2u));
        uint32_t claims = local;
        if (port::load<uint16_t>(entry + 2u) >= 0x8000)
            claims = static_cast<uint32_t>(Claim_GetSectionD(port::load<uint16_t>(local)));
        const uint32_t member = Family_Get(ctx->familyA, 0).address + 0x48u * ctx->memberA + 0x2Cu;
        const uint32_t end = member + 0x20u;
        for (uint32_t at = member;; at += 2u)
        {
            const uint32_t family = port::load<uint16_t>(at);
            if (family == 0 || at >= end)
                return 0;
            if (Family_CanApply(family, ctx->city) == 0)
                continue;
            const uint32_t type = static_cast<uint32_t>(Family_GetTypeIndex(static_cast<int>(port::load<uint16_t>(at)))) & 0xFFFFu;
            for (uint32_t want = static_cast<uint32_t>(Claim_GetSectionC(port::load<uint8_t>(globals::buildingTypeAddress(type) + 0x148u))); port::load<uint16_t>(want) != 0; want += 4u)
                for (uint32_t have = claims; port::load<uint16_t>(have) != 0; have += 2u)
                    if (port::load<uint16_t>(want) == port::load<uint16_t>(have))
                    {
                        port::store<uint16_t>(ctx->personA.address + 0x2Cu, port::load<uint16_t>(at));
                        ctx->result = port::load<uint16_t>(at);
                        return 1;
                    }
        }
    }

    PORT_FN(WordsFn_AcquaintanceSuppliesClaim);

    // ---- Small script actions (plan P6 batch 2) ----

    // 1 when the script person's schedule activity (their record's, or the schedule's own for a family member who is not on the
    // map) is the script's argument.
    int WordsFn_ScheduleIs(GuestPtr<WordsCtx> ctx)
    {
        const uint32_t activity = ctx->personA ? port::load<uint8_t>(ctx->personA.address + 0x27u) : Schedule_QueryB(ctx->city, ctx->familyA, ctx->memberA);
        return static_cast<int64_t>(activity) == static_cast<int64_t>(ctx->arg) ? 1 : 0;
    }

    // Invites the script's family (+8) as an applicant when it can apply to the city, with sound 0x17.
    void WordsFn_InviteFamily(GuestPtr<WordsCtx> ctx)
    {
        if (Family_CanApply(ctx->familyA, ctx->city) != 0)
        {
            CityScene_AddApplicantFamily(ctx->city, ctx->familyA, 1);
            Sound_PlaySe(0x17, 0, 0x3C, ps2f(0.0f), ps2f(0.0f));
        }
    }

    // The same for the family introduced to the script's person (the result at +0x28): after it, the person's member flag is
    // cleared and the introduction forgotten.
    void WordsFn_InviteIntroduced(GuestPtr<WordsCtx> ctx)
    {
        if (Family_CanApply(ctx->result, ctx->city) != 0)
        {
            CityScene_AddApplicantFamily(ctx->city, ctx->result, 1);
            Family_ClearMemberFlag(ctx->familyA, ctx->memberA);
            ctx->result = 0;
            Sound_PlaySe(0x17, 0, 0x3C, ps2f(0.0f), ps2f(0.0f));
        }
    }

    PORT_FN(WordsFn_ScheduleIs);
    PORT_FN(WordsFn_InviteFamily);
    PORT_FN(WordsFn_InviteIntroduced);

    namespace
    {
        constexpr uint32_t kWordsObject = 0x001BDDF0u, kWordsUsedMask = 0x001BDE18u;

        // The event words (WORDSEVE.BIN, a block per event kind) a script may use: kind 0 has none. A kind that is not the one held
        // is read into the object's extra table (the block size is in 0x1BDDEC) and forgets the line tables bound so far.
        void loadEventWords(uint32_t kind)
        {
            constexpr uint32_t kEventFile = 0x001BDDE0u, kEventBlockSize = 0x001BDDECu, kReadResult = 0x001BD9F4u;
            const GuestPtr<Words> words{kWordsObject};
            if (kind == 0)
            {
                words->extraLoaded = 0;
                return;
            }
            if (static_cast<int32_t>(kind) != words->unk_04)
            {
                const int32_t size = port::load<int32_t>(kEventBlockSize);
                File_Seek(GuestPtr<GameFile>{port::load<uint32_t>(kEventFile)}, size * (static_cast<int32_t>(kind) - 1) + 4, 0, 0);
                port::store<int32_t>(kReadResult, File_Read(GuestPtr<GameFile>{port::load<uint32_t>(kEventFile)}, static_cast<int>(words->extraTable.address), 1,
                                                            port::load<int32_t>(kEventBlockSize), 0));
                words->unk_04 = static_cast<int32_t>(kind);
                words->bound = 0;
            }
            words->extraLoaded = 1;
        }

        // Once the tables are bound the line table points at the block's own lines, or, with event words loaded, at the lines the
        // event block names for this block (an entry below 0 keeps the block's own).
        void pickLineTable(GuestPtr<uint32_t> blockPtr)
        {
            const GuestPtr<Words> words{kWordsObject};
            if (words->bound == 0)
                return;
            const uint32_t record = port::load<uint32_t>(blockPtr.address);
            const uint32_t data = port::load<uint32_t>(record + 4u);
            words->lineTable = GuestPtr<void>{data + port::load<uint32_t>(data)};
            if (words->extraLoaded == 0)
                return;
            const uint32_t extra = words->extraTable.address;
            const int32_t entry = port::load<int16_t>(extra + port::load<uint32_t>(extra) + 2u * port::load<uint32_t>(record));
            if (entry >= 0)
                words->lineTable = GuestPtr<void>{extra + port::load<uint32_t>(extra + 4u) + 2u * static_cast<uint32_t>(entry)};
        }
    }

    // Prepares the script of a family member: the object is set to the member (mm_Words_SetFamily), the entries 1 and 2 of the
    // choice menus are marked unavailable (bit 2 always; bit 1 for a member under 13 or with a friendship under 24, the used
    // mask at 0x1BDE18), the line and column restart, the member's script block is read (mm_Words_ReadBlock, by the first
    // halfword of its record), the event words for the city's current event are loaded and the line table picked. The result is the
    // block.
    int Words_LoadForFamily(int family, int member, int arg5, GuestPtr<CityScene> city)
    {
        const GuestPtr<Words> words{kWordsObject};
        Words_SetFamily(words, family, static_cast<uint16_t>(member), arg5, city);
        port::store<uint8_t>(kWordsUsedMask, 4);
        words->line = 0;
        words->column = 0;
        const uint32_t self = Family_Get(family, 0).address + 0x48u * static_cast<uint32_t>(member);
        if (port::load<uint8_t>(self + 0x26u) < 13 || Family_GetFriendship(family, member) < 24)
            port::store<uint8_t>(kWordsUsedMask, static_cast<uint8_t>(port::load<uint8_t>(kWordsUsedMask) | 2u));
        const GuestPtr<uint32_t> block = Words_ReadBlock(port::load<uint16_t>(Family_Get(family, 0).address + 0x48u * static_cast<uint32_t>(member) + 0x24u));
        loadEventWords(city->events->talkA->kind);
        pickLineTable(block);
        return static_cast<int>(block.address);
    }

    // The same for a person in the city: the script block is the one of the character type (+0x24), the event words are for the
    // event the person starts (mm_Event_TryStart, when arg5 is 0) or the city's current one. The choice menus are marked as
    // above, but with an item of category 0x10, 0x11 or 0x13 among the bag's slots (its size picks how many are looked at) no
    // entry is unavailable, and with none the age and friendship rule applies to the person's family member.
    int Words_LoadForPerson(GuestPtr<MapPerson> person, int arg5, GuestPtr<CityScene> city)
    {
        const GuestPtr<Words> words{kWordsObject};
        Words_SetPerson(words, person, arg5, city);
        words->line = 0;
        words->column = 0;
        const GuestPtr<uint32_t> block = Words_ReadBlock(person->charType);
        const uint32_t kind = arg5 == 0 ? Event_TryStart(city->events, city, person) : city->events->talkA->kind;
        loadEventWords(kind);
        pickLineTable(block);
        port::store<uint8_t>(kWordsUsedMask, 4);
        const GuestPtr<Bag> bag = city->bag;
        const uint32_t end = bag.address + 4u + 16u * port::load<uint8_t>(globals::kBagSlotsBySize + bag->size);
        for (uint32_t slot = bag.address + 4u; port::load<uint16_t>(slot) != 0 && slot < end; slot += 16u)
        {
            const uint32_t first = port::load<uint16_t>(globals::goodsSectionA.get() + 4u * port::load<uint16_t>(slot));
            const uint32_t category = port::load<uint8_t>(globals::goodsSectionB.get() + 0x88u * (port::load<uint16_t>(slot + 2u) + first) + 4u);
            if (category == 0x10 || category == 0x11 || category == 0x13)
            {
                port::store<uint8_t>(kWordsUsedMask, 0);
                break;
            }
        }
        const uint32_t self = Family_Get(person->family, 0).address + 0x48u * person->member;
        if (port::load<uint8_t>(self + 0x26u) < 13 || Family_GetFriendship(person->family, person->member) < 24)
            port::store<uint8_t>(kWordsUsedMask, static_cast<uint8_t>(port::load<uint8_t>(kWordsUsedMask) | 2u));
        return static_cast<int>(block.address);
    }

    PORT_FN(Words_LoadForFamily);
    PORT_FN(Words_LoadForPerson);

    // Reads the script data at start-up with a synchronous file on the stack: two table files are read whole (0x1BADD0 into a block
    // at 0x1BDE08 with six table pointers made from its first six offsets at 0x7181D8, and 0x1BADF0 into a block at 0x1BDE0C with
    // eleven pointers, five at 0x718220 and six at 0x7181F0), the event words file (0x1BADB0) gives its block size (0x1BDDEC) and
    // the buffer for one block (0x1BDE10), and the words file (0x1BAD90) gives its block size (0x1BDDE8) and, with it, the eight
    // cache entries of the block list at 0x718190 (a pointer to a record that opens the files, mm_Words_OpenFiles).
    void Words_Init()
    {
        port::GuestFrame<0x50> frame;
        const GuestPtr<GameFile> file{frame.ptr().address + 0x48u};
        File_ctor(file, 0);
        // A whole file into a new block: the block is kept in `holder` before it is read.
        const auto readWhole = [&](uint32_t path, uint32_t holder) {
            File_Open(file, GuestPtr<char>{path});
            const int size = File_Seek(file, 0, 2, 0);
            File_Seek(file, 0, 0, 0);
            port::store<uint32_t>(holder, sdk::mw_NewArray(static_cast<uint32_t>(size)));
            File_Read(file, static_cast<int>(port::load<uint32_t>(holder)), size, 1, 0);
            File_Close(file);
        };
        readWhole(0x001BADD0u, 0x001BDE08u);
        for (uint32_t i = 0; i < 6; ++i)
        {
            const uint32_t block = port::load<uint32_t>(0x001BDE08u);
            port::store<uint32_t>(0x007181D8u + 4u * i, block + port::load<uint32_t>(block + 4u * i));
        }
        readWhole(0x001BADF0u, 0x001BDE0Cu);
        for (uint32_t i = 0; i < 11; ++i)
        {
            const uint32_t block = port::load<uint32_t>(0x001BDE0Cu);
            port::store<uint32_t>((i < 5 ? 0x00718220u + 4u * i : 0x007181F0u + 4u * (i - 5u)), block + port::load<uint32_t>(block + 4u * i));
        }
        File_Open(file, GuestPtr<char>{0x001BADB0u});
        File_Read(file, 0x001BDDECu, 1, 4, 0);
        File_Close(file);
        port::store<uint32_t>(0x001BDE10u, sdk::mw_NewArray(port::load<uint32_t>(0x001BDDECu)));
        File_Open(file, GuestPtr<char>{0x001BAD90u});
        File_Read(file, 0x001BDDE8u, 1, 4, 0);
        for (uint32_t i = 0; i < 8; ++i)
        {
            const uint32_t holder = sdk::operator_new(4u);
            if (holder != 0u)
            {
                uint32_t record = sdk::operator_new(8u);
                if (record != 0u)
                    record = static_cast<uint32_t>(Words_OpenFiles(static_cast<int>(record)));
                port::store<uint32_t>(holder, record);
            }
            port::store<uint32_t>(0x00718190u + 4u * i, holder);
        }
        File_dtor(file, -1);
    }

    PORT_FN(Words_Init);

    // The text of a choice in a script menu, by the menu's code. Below -2005 the code names a table: group g = (code + 6000) /
    // 1000 and entry h = (code + 6000) % 1000 look the text up through the offset tables the script files were bound to at start-up
    // (0x7181C8, 24 bytes a group: the entry offsets at +0, the choice offsets at +8, the text at +0xC; group 2 goes through the
    // building type of the script's context first). The special codes: -2005 the index-th choice not marked in the used mask
    // (0x1BDE18); -2004 the default choice text for index 0, otherwise the text of the city's event slot index - 1; -2003 the claim
    // line of the index-th open want of the wants window; -2002 the name of the index-th bag item of category 0x10, 0x11 or 0x13
    // (or the default when there are fewer); any other code gives goods number index (under 4) with its price right-aligned to 28
    // characters in a buffer at 0x718240, or a fixed text from 4 on.
    GuestPtr<char> Words_GetChoiceText(int script, GuestPtr<CityScene> city, int code, int index)
    {
        (void)script;
        constexpr uint32_t kGroups = 0x007181C8u, kDefaults = 0x007181E8u, kWordsObject = 0x001BDDF0u;
        port::GuestFrame<0x50> frame;
        const uint32_t sp = frame.ptr().address;
        const auto defaultText = [&](uint32_t second) {
            return GuestPtr<char>{port::load<uint32_t>(kDefaults + 4u) + port::load<uint16_t>(port::load<uint32_t>(kDefaults) + second)};
        };
        if (code < -0x7D5)
        {
            const int group = (code + 6000) / 1000;
            const int entry = (code + 6000) % 1000;
            if (group != 2)
            {
                const uint32_t table = kGroups + 24u * static_cast<uint32_t>(group);
                const uint32_t first = port::load<uint16_t>(port::load<uint32_t>(table) + 2u * static_cast<uint32_t>(entry));
                const uint32_t offset = port::load<uint16_t>(port::load<uint32_t>(table + 8u) + 2u * static_cast<uint32_t>(index + static_cast<int>(first)));
                return GuestPtr<char>{port::load<uint32_t>(table + 0xCu) + offset};
            }
            const uint32_t type = port::load<uint32_t>(0x00718220u) + 8u * Words_GetBuildingTypeB(GuestPtr<Words>{kWordsObject});
            const uint32_t table = kGroups + 48u;
            const uint32_t kind = port::load<uint16_t>(type + 2u);
            const uint32_t line = port::load<uint16_t>(port::load<uint32_t>(0x00718228u) + 2u * (static_cast<uint32_t>(entry) + kind));
            const uint32_t block = port::load<uint16_t>(port::load<uint32_t>(table) + 2u * line);
            const uint32_t offset = port::load<uint16_t>(port::load<uint32_t>(table + 8u) + 2u * (static_cast<uint32_t>(index) + block));
            return GuestPtr<char>{port::load<uint32_t>(table + 0xCu) + offset};
        }
        if (code == -0x7D5)
        {
            const uint32_t used = port::load<uint8_t>(0x001BDE18u);
            int bit = 0, seen = 0;
            for (;;)
            {
                if ((used & (1u << bit)) != 0)
                {
                    ++bit;
                    continue;
                }
                ++seen;
                if (index < seen)
                    break;
                ++bit;
            }
            const uint32_t base = port::load<uint16_t>(port::load<uint32_t>(0x007181E0u));
            return GuestPtr<char>{port::load<uint32_t>(0x007181ECu) +
                                  port::load<uint16_t>(port::load<uint32_t>(0x007181E8u) + 2u * (static_cast<uint32_t>(bit) + base))};
        }
        if (code == -0x7D4)
        {
            if (index == 0)
                return GuestPtr<char>{port::load<uint32_t>(0x007181ECu) + port::load<uint16_t>(port::load<uint32_t>(0x007181E8u))};
            return Event_FormatText(city->events, index - 1);
        }
        if (code == -0x7D3)
        {
            port::store<int32_t>(sp + 0x4Cu, 0);
            for (int counted = 0;; ++counted)
            {
                if (Wants_Find(city->wants, city, GuestPtr<uint32_t>{sp + 0x40u}, GuestPtr<uint32_t>{sp + 0x44u}, GuestPtr<uint32_t>{sp + 0x48u},
                               GuestPtr<int32_t>{sp + 0x4Cu}) == 0)
                    return GuestPtr<char>{0};
                if (counted == index)
                    break;
            }
            const GuestPtr<MapObject> object = Map_GetRecord(city->map, static_cast<int>(port::load<uint32_t>(sp + 0x44u)));
            return GuestPtr<char>{static_cast<uint32_t>(
                Claim_GetLine(port::load<uint8_t>(globals::buildingTypeAddress(object->type) + 0x147u), object->claimSlot))};
        }
        if (code == -0x7D2)
        {
            const GuestPtr<Bag> bag = city->bag;
            const uint32_t end = bag.address + 4u + 16u * port::load<uint8_t>(globals::kBagSlotsBySize + bag->size);
            int counted = 0;
            for (uint32_t slot = bag.address + 4u; port::load<uint16_t>(slot) != 0 && slot < end; slot += 16u)
            {
                const uint32_t first = port::load<uint16_t>(globals::goodsSectionA.get() + 4u * port::load<uint16_t>(slot));
                const uint32_t record = globals::goodsSectionB.get() + 0x88u * (port::load<uint16_t>(slot + 2u) + first);
                const uint32_t category = port::load<uint8_t>(record + 4u);
                if (category != 0x10 && category != 0x11 && category != 0x13)
                    continue;
                if (counted == index)
                    return GuestPtr<char>{globals::goodsSectionC.get() + port::load<uint16_t>(record)};
                ++counted;
            }
            return defaultText(2u);
        }
        if (index >= 4)
            return GuestPtr<char>{0x001BAD68u};
        const uint32_t goods = static_cast<uint32_t>(Words_GetGoods(GuestPtr<Words>{kWordsObject}, index));
        sdk::strcpy(0x00718240u, globals::goodsSectionC.get() + port::load<uint16_t>(goods));
        const uint32_t length = static_cast<uint32_t>(sdk::strlen(0x00718240u));
        const int price = Goods_GetPrice(static_cast<int>(goods));
        Text_FormatMoney(GuestPtr<char>{0x00718240u + length}, price, 0x1C - static_cast<int>(length), 0);
        return GuestPtr<char>{0x00718240u};
    }

    PORT_FN(Words_GetChoiceText);

    // The text of a line of the wants window that has its own script: when the city has an event running (its kind is not 0) and
    // the event queue holds the id at the same place as the kind, the script block of the default speaker is read, the Words object
    // is reset (family 0, member 0, argument 4), the output text cleared, and the event words for the kind loaded. The first time
    // the tables are bound: any read still running is waited for, the six table pointers (0x7181C0, from the block's header
    // offsets) and, with event words, the six extra ones (0x718208) are set, the line table is picked as for a family
    // (mm_Words_LoadForFamily) and the tables count as bound. The line (+3000) is then expanded into `out`.
    void Words_LoadForWants(GuestPtr<CityScene> city, int16_t id, int line, GuestPtr<char> out)
    {
        constexpr uint32_t kReadResult = 0x001BD9F4u;
        const uint32_t kind = city->events->talkA->kind;
        if (kind == 0 || static_cast<int32_t>(kind) != EventQueue_Find(city->events, id))
            return;
        const GuestPtr<uint32_t> block = Words_ReadBlock(0);
        const GuestPtr<Words> words{kWordsObject};
        Words_SetFamily(words, 0, 0, 4, city);
        words->line = 0;
        words->column = 0;
        port::store<uint8_t>(out.address, 0);
        loadEventWords(kind);
        if (words->bound == 0)
        {
            const uint32_t record = port::load<uint32_t>(block.address);
            if (port::load<int32_t>(kReadResult) >= 0)
            {
                FileQueue_WaitAll(0);
                port::store<int32_t>(kReadResult, -1);
            }
            const uint32_t data = port::load<uint32_t>(record + 4u);
            for (uint32_t i = 0; i < 6; ++i)
                port::store<uint32_t>(0x007181C0u + 4u * i, data + port::load<uint32_t>(data + 4u + 4u * i));
            const bool extraLoaded = words->extraLoaded != 0;
            const uint32_t extra = words->extraTable.address;
            if (extraLoaded)
                for (uint32_t i = 0; i < 6; ++i)
                    port::store<uint32_t>(0x00718208u + 4u * i, extra + port::load<uint32_t>(extra + 8u + 4u * i));
            words->lineTable = GuestPtr<void>{data + port::load<uint32_t>(data)};
            if (extraLoaded)
            {
                const int32_t entry = port::load<int16_t>(extra + port::load<uint32_t>(extra) + 2u * port::load<uint32_t>(record));
                if (entry >= 0)
                    words->lineTable = GuestPtr<void>{extra + port::load<uint32_t>(extra + 4u) + 2u * static_cast<uint32_t>(entry)};
            }
            words->bound = 1;
        }
        Words_ExpandLine(static_cast<int>(port::load<uint32_t>(block.address)), line + 3000, static_cast<int>(out.address));
    }

    PORT_FN(Words_LoadForWants);

    // The common body of the three "supplies a want" scripts. The want the wants window offers at the chosen index (+0x13A of the
    // context) gives the script's other person and building (B) and the claims the building supplies: a single value of the building
    // type's claim section B entry, or the list of section D when the value has bit 15 set. Mode 0 (a family) asks whether the home
    // type of family A claims one of them. Modes 1 (an acquaintance of member A) and 2 (of any member of the family) look through the
    // acquaintances (up to 16 families each) that can apply to the city for one whose home type claims one: it becomes the result
    // (+0x28) and, in mode 1, person A's introduction (+0x2C); mode 2 also notes the member (+0x1A). 1 on a match, else 0.
    int WordsFn_SuppliesWantBody(GuestPtr<WordsCtx> ctx, int mode)
    {
        port::GuestFrame<0xB0> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t local = sp + 0x9Cu;
        port::store<uint32_t>(local, port::load<uint32_t>(0x001BDC24u));
        port::store<int32_t>(sp + 0xA0u, 0);
        uint32_t claims = local;
        for (int index = 0; Wants_Find(ctx->city->wants, ctx->city, GuestPtr<uint32_t>{sp + 0xA4u}, GuestPtr<uint32_t>{sp + 0xACu},
                                       GuestPtr<uint32_t>{sp + 0xA8u}, GuestPtr<int32_t>{sp + 0xA0u}) != 0;
             ++index)
        {
            if (index != ctx->wantIndex)
                continue;
            const GuestPtr<CityMap> map = ctx->city->map;
            const uint32_t object = port::load<uint32_t>(sp + 0xACu);
            const uint32_t section = static_cast<uint32_t>(
                Claim_GetSectionB(port::load<uint8_t>(globals::buildingTypeAddress(Map_GetRecord(map, static_cast<int>(object))->type) + 0x147u)));
            const uint32_t value = port::load<uint16_t>(section + 8u * port::load<uint32_t>(sp + 0xA4u) + 2u);
            if (static_cast<int32_t>(value) < 0x8000)
                port::store<uint16_t>(local, static_cast<uint16_t>(value));
            else
                claims = static_cast<uint32_t>(Claim_GetSectionD(value));
            ctx->personB = ctx->city->map->people + port::load<uint32_t>(sp + 0xA8u);
            ctx->familyB = ctx->personB->family;
            ctx->memberB = ctx->personB->member;
            ctx->buildingB = Map_GetRecord(ctx->city->map, static_cast<int>(object));
            ctx->buildingTypeB = ctx->buildingB->type;
            break;
        }
        // Whether the home type of `family` claims one of the claims supplied; the types' claim section C lists end with a 0 value.
        const auto wantsOneOf = [&](uint32_t family) {
            const uint32_t type = static_cast<uint32_t>(Family_GetTypeIndex(static_cast<int>(family))) & 0xFFFFu;
            for (uint32_t want = static_cast<uint32_t>(Claim_GetSectionC(port::load<uint8_t>(globals::buildingTypeAddress(type) + 0x148u)));
                 port::load<uint16_t>(want) != 0; want += 4u)
                for (uint32_t have = claims; port::load<uint16_t>(have) != 0; have += 2u)
                    if (port::load<uint16_t>(want) == port::load<uint16_t>(have))
                        return true;
            return false;
        };
        if (mode == 0)
            return Family_CanApply(ctx->familyA, ctx->city) != 0 && wantsOneOf(ctx->familyA) ? 1 : 0;
        const GuestPtr<Family> family = Family_Get(ctx->familyA, 0);
        const uint32_t first = family.address + 0x24u;
        uint32_t member = mode == 2 ? first : first + 0x48u * ctx->memberA;
        const uint32_t end = mode == 2 ? member + 0x48u * port::load<uint16_t>(family.address + 0x22u) : member + 0x48u;
        for (; member < end; member += 0x48u)
        {
            const uint32_t list = member + 8u;
            for (uint32_t at = list; port::load<uint16_t>(at) != 0 && at < list + 0x20u; at += 2u)
            {
                if (Family_CanApply(port::load<uint16_t>(at), ctx->city) == 0 || !wantsOneOf(port::load<uint16_t>(at)))
                    continue;
                if (ctx->personA && mode == 1)
                    port::store<uint16_t>(ctx->personA.address + 0x2Cu, port::load<uint16_t>(at));
                ctx->result = port::load<uint16_t>(at);
                if (mode == 2)
                    ctx->memberIndex = static_cast<uint16_t>(static_cast<int32_t>(member - first) / 0x48);
                return 1;
            }
        }
        return 0;
    }

    PORT_FN(WordsFn_SuppliesWantBody);

    // The scripts that share the body: any acquaintance of the family, an acquaintance of the member, the family itself.
    int WordsFn_AnyAcquaintanceSuppliesWant(GuestPtr<WordsCtx> ctx) { return WordsFn_SuppliesWantBody(ctx, 2); }
    int WordsFn_AcquaintanceSuppliesWant(GuestPtr<WordsCtx> ctx) { return WordsFn_SuppliesWantBody(ctx, 1); }
    int WordsFn_FamilySuppliesWant(GuestPtr<WordsCtx> ctx) { return WordsFn_SuppliesWantBody(ctx, 0); }

    PORT_FN(WordsFn_AnyAcquaintanceSuppliesWant);
    PORT_FN(WordsFn_AcquaintanceSuppliesWant);
    PORT_FN(WordsFn_FamilySuppliesWant);

    // Picks the resident a claim script talks about. The homes around building A are collected (candidates, +0x2C of the context),
    // and the claims the selected want asks for are worked out as in WordsFn_SuppliesWantBody. The residents of each candidate home
    // that are loaded are looked through: the acquaintances of each member (up to 16 families) that can apply to the city, whose
    // home type claims one of the claims, give person B (the home's family, the member's index); 4 when that member's friendship
    // allows it (family B, member B, building B, its type and the result at +0x28 are set), else the search goes on and ends with
    // 3. With no result (and arg -1000) a resident by type is tried (2); with none either a random resident of a random candidate
    // home becomes person B (1). 0 when there is no candidate or no resident.
    int WordsFn_PickClaimResident(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x110> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t local = sp + 0xFCu;
        Words_CollectNearbyHomes(ctx, ctx->buildingA);
        if (ctx->candidateCount == 0)
            return 0;
        port::store<uint32_t>(local, port::load<uint32_t>(0x001BDC1Cu));
        port::store<int32_t>(sp + 0x10Cu, 0);
        uint32_t claims = local;
        int result = 0;
        for (int index = 0; Wants_Find(ctx->city->wants, ctx->city, GuestPtr<uint32_t>{sp + 0x100u}, GuestPtr<uint32_t>{sp + 0x104u},
                                       GuestPtr<uint32_t>{sp + 0x108u}, GuestPtr<int32_t>{sp + 0x10Cu}) != 0;
             ++index)
        {
            if (index != ctx->wantIndex)
                continue;
            const uint32_t section = static_cast<uint32_t>(Claim_GetSectionB(
                port::load<uint8_t>(globals::buildingTypeAddress(Map_GetRecord(ctx->city->map, static_cast<int>(port::load<uint32_t>(sp + 0x104u)))->type) + 0x147u)));
            const uint32_t value = port::load<uint16_t>(section + 8u * port::load<uint32_t>(sp + 0x100u) + 2u);
            if (static_cast<int32_t>(value) < 0x8000)
                port::store<uint16_t>(local, static_cast<uint16_t>(value));
            else
                claims = static_cast<uint32_t>(Claim_GetSectionD(value));
            break;
        }
        if (port::load<uint16_t>(claims) != 0)
        {
            for (int k = 0; k < ctx->candidateCount; ++k)
            {
                const GuestPtr<MapObject> home = Map_GetRecord(ctx->city->map, port::load<uint16_t>(ctx.address + 0x30u + 2u * static_cast<uint32_t>(k)));
                const uint32_t family = static_cast<uint32_t>(Family_IsCached(home->family));
                if (family == 0)
                    continue;
                const uint32_t first = family + 0x24u, end = first + 0x48u * port::load<uint16_t>(family + 0x22u);
                for (uint32_t member = first; member < end; member += 0x48u)
                {
                    const uint32_t memberIndex = static_cast<uint32_t>(static_cast<int32_t>(member - first) / 0x48);
                    const uint32_t list = member + 8u;
                    for (uint32_t at = list; port::load<uint16_t>(at) != 0 && at < list + 0x20u; at += 2u)
                    {
                        if (Family_CanApply(port::load<uint16_t>(at), ctx->city) == 0)
                            continue;
                        const uint32_t type = static_cast<uint32_t>(Family_GetTypeIndex(static_cast<int>(port::load<uint16_t>(at)))) & 0xFFFFu;
                        for (uint32_t want = static_cast<uint32_t>(Claim_GetSectionC(port::load<uint8_t>(globals::buildingTypeAddress(type) + 0x148u)));
                             port::load<uint16_t>(want) != 0; want += 4u)
                            for (uint32_t have = claims; port::load<uint16_t>(have) != 0; have += 2u)
                            {
                                if (port::load<uint16_t>(want) != port::load<uint16_t>(have))
                                    continue;
                                ctx->familyB = home->family;
                                ctx->memberB = static_cast<uint16_t>(memberIndex);
                                ctx->personB = Map_FindPersonEntry(ctx->city->map, static_cast<int16_t>(ctx->familyB), ctx->memberB);
                                if (!ctx->personB)
                                    continue;
                                ctx->buildingB = home;
                                ctx->buildingTypeB = home->type;
                                ctx->result = port::load<uint16_t>(at);
                                const int agrees = Family_FriendAgrees(ctx->familyB, ctx->memberB, ctx->city->approval);
                                result = 3;
                                if (agrees != 0)
                                    return 4;
                            }
                    }
                }
            }
        }
        if (result == 0)
        {
            ctx->arg = -0x3E8;
            if (Words_PickResidentByType(ctx) != 0)
                result = 2;
        }
        if (result != 0)
            return result;
        if (ctx->candidateCount == 0)
            return 0;
        const int32_t pick = static_cast<int32_t>(Random_Next()) % ctx->candidateCount;
        ctx->buildingB = Map_GetRecord(ctx->city->map, port::load<uint16_t>(ctx.address + 0x30u + 2u * static_cast<uint32_t>(pick)));
        ctx->buildingTypeB = ctx->buildingB->type;
        const GuestPtr<Family> residents = Family_Get(ctx->buildingB->family, 0);
        const uint32_t count = port::load<uint16_t>(residents.address + 0x22u);
        if (count == 0)
            return 0;
        const int32_t which = static_cast<int32_t>(Random_Next()) % static_cast<int32_t>(count);
        ctx->personB = Map_FindPersonEntry(ctx->city->map, static_cast<int16_t>(ctx->buildingB->family), static_cast<uint16_t>(which));
        if (!ctx->personB)
            return 0;
        ctx->familyB = ctx->personB->family;
        ctx->memberB = ctx->personB->member;
        return 1;
    }

    PORT_FN(WordsFn_PickClaimResident);

    // One step of the WORDS script interpreter: reads the script's words from the current position (0x1BDE00) in the current block
    // (0x1BDE14) until one gives a result other than -6009 ("go on") and returns it. The first call also waits for the file queue,
    // points the tables of the two script banks (0x7181C0 and 0x718208) at their data and picks the block of the script in the
    // second bank. Words: below -9010 the context's argument is set (word + 10000); -9010 up to -8001 an action through its pointer,
    // whose result says how many words to skip (from -8900 on the action is word + 8927; from -8960 the argument is the word's
    // 26ths from -8960 and the action the remainder + 1; below that the argument is word + 9000 and the action 0); 5000 and up a
    // line of text, added to the buffer after a separator unless it adds nothing (its result is returned at once when the stop flag
    // at 0x1BDE04 is set); 0 to 4999 jump to that word; -6010 up to -1 that result; -6011 starts the script over; below that a
    // condition call (word + 7000).
    int16_t Words_StepImpl(GuestPtr<int32_t> script, GuestPtr<char> buffer)
    {
        constexpr uint32_t kInitialised = 0x001BDDFCu, kWaited = 0x001BD9F4u, kBankFlag = 0x001BDDF8u, kBank2 = 0x001BDE10u,
                           kCurrent = 0x001BDE14u, kPosition = 0x001BDE00u, kStopFlag = 0x001BDE04u, kWords = 0x001BDDF0u;
        const uint32_t s = script.address;
        const GuestPtr<Words> words{kWords};
        if (port::load<int32_t>(kInitialised) == 0)
        {
            if (port::load<int32_t>(kWaited) >= 0)
            {
                FileQueue_WaitAll(0);
                port::store<int32_t>(kWaited, -1);
            }
            const uint32_t base = port::load<uint32_t>(s + 4u);
            const int32_t bank2 = port::load<int32_t>(kBankFlag);
            for (uint32_t i = 0; i < 6; ++i)
                port::store<uint32_t>(0x007181C0u + 4u * i, base + port::load<uint32_t>(base + 4u + 4u * i));
            if (bank2 != 0)
            {
                const uint32_t second = port::load<uint32_t>(kBank2);
                for (uint32_t i = 0; i < 6; ++i)
                    port::store<uint32_t>(0x00718208u + 4u * i, second + port::load<uint32_t>(second + 8u + 4u * i));
            }
            const uint32_t again = port::load<uint32_t>(s + 4u);
            port::store<uint32_t>(kCurrent, again + port::load<uint32_t>(again));
            if (bank2 != 0)
            {
                const uint32_t second = port::load<uint32_t>(kBank2);
                const int32_t entry = port::load<int16_t>(second + port::load<uint32_t>(second) + 2u * port::load<uint32_t>(s));
                if (entry >= 0)
                    port::store<uint32_t>(kCurrent, second + port::load<uint32_t>(second + 4u) + 2u * static_cast<uint32_t>(entry));
            }
            port::store<int32_t>(kInitialised, 1);
        }
        port::store<uint8_t>(buffer.address, 0);
        int32_t result = -0x1779;
        for (;;)
        {
            const int32_t token = port::load<int16_t>(port::load<uint32_t>(kCurrent) + 2u * port::load<uint16_t>(kPosition));
            const auto act = [&](int index) { return port::callGuest<int>(addr::Words_CallAct, words, index); };
            const auto skip = [&](int count) {
                port::store<uint16_t>(kPosition, static_cast<uint16_t>(port::load<uint16_t>(kPosition) + (static_cast<uint32_t>(count) & 0xFFFFu)));
            };
            if (token < -0x2332)
                Words_SetArg(words, static_cast<uint16_t>(token + 0x2710));
            else if (token < -0x1F40)
            {
                if (token >= -0x22C4)
                    skip(act(token + 0x22DF));
                else if (token >= -0x2300)
                {
                    const int32_t relative = token + 0x2300;
                    Words_SetArg(words, static_cast<uint16_t>(relative / 26));
                    skip(act(relative % 26 + 1));
                }
                else
                {
                    Words_SetArg(words, static_cast<uint16_t>(token + 0x2328));
                    skip(act(0));
                }
            }
            else if (token >= 0x1388)
            {
                const int length = sdk::strlen(buffer.address);
                bool separated = false;
                if (length != 0)
                {
                    sdk::strcat(buffer.address, 0x001BAD88u);
                    separated = true;
                }
                result = static_cast<int32_t>(Words_ExpandLine(static_cast<int>(s), token - 0x1388, static_cast<int>(buffer.address)));
                if (separated && length + 1 == sdk::strlen(buffer.address))
                    port::store<uint8_t>(buffer.address + static_cast<uint32_t>(length), 0);
                if (port::load<uint16_t>(kStopFlag) != 0)
                    return static_cast<int16_t>(result);
            }
            else if (token >= 0)
                port::store<uint16_t>(kPosition, static_cast<uint16_t>(token - 1));
            else if (token >= -0x177A)
                result = token;
            else if (token != -0x177B)
                port::callGuest<int>(addr::Words_CallCond, words, token + 0x1B58);
            else
            {
                const uint32_t base = port::load<uint32_t>(s + 4u);
                port::store<uint32_t>(kCurrent, base + port::load<uint32_t>(base));
                port::store<uint16_t>(kPosition, 0xFFFFu);
            }
            port::store<uint16_t>(kPosition, static_cast<uint16_t>(port::load<uint16_t>(kPosition) + 1u));
            if (result != -0x1779)
                return static_cast<int16_t>(result);
        }
    }

    // The same through the script's pointer (the object holds it in its first word).
    int16_t Words_Step(GuestPtr<int32_t> script, GuestPtr<char> buffer)
    {
        return Words_StepImpl(GuestPtr<int32_t>{port::load<uint32_t>(script.address)}, buffer);
    }

    PORT_FN(Words_StepImpl);
    PORT_FN(Words_Step);

    namespace
    {
        // __ptmf_scall: the pointer to member function at `entry` (the delta added to `this`, then a vtable offset or -1, then the
        // function or, for a virtual one, where the vtable pointer sits in the object) called on `object` with the index. The callee
        // finds the table's address in v0 (the callbacks that never set v0 hand it back).
        int callMember(uint32_t table, uint32_t index, uint32_t object)
        {
            const uint32_t entry = table + 12u * index;
            const uint32_t self = object + port::load<uint32_t>(entry);
            const int32_t slot = port::load<int32_t>(entry + 4u);
            uint32_t target = port::load<uint32_t>(entry + 8u);
            if (slot >= 0)
                target = port::load<uint32_t>(port::load<uint32_t>(self + target) + static_cast<uint32_t>(slot));
            port::setGuestV0(static_cast<int32_t>(table));
            return port::callGuest<int>(target, self, static_cast<int>(index));
        }
    }

    // A script's condition, text getter or action number `index`: the member function in the table (12 bytes an entry, at 0x1B9630,
    // 0x1B9570 and 0x1B9100) called on the script's context (the first word of the object).
    void Words_CallCond(GuestPtr<Words> words, int index)
    {
        callMember(0x001B9630u, static_cast<uint32_t>(index), port::load<uint32_t>(words.address));
    }

    int Words_CallText(GuestPtr<Words> words, int index)
    {
        return callMember(0x001B9570u, static_cast<uint32_t>(index), port::load<uint32_t>(words.address));
    }

    int Words_CallAct(GuestPtr<Words> words, int index)
    {
        return callMember(0x001B9100u, static_cast<uint32_t>(index), port::load<uint32_t>(words.address));
    }

    PORT_FN(Words_CallCond);
    PORT_FN(Words_CallText);
    PORT_FN(Words_CallAct);
}
