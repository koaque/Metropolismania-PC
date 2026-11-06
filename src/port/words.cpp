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
            return eventsOf(ctx)->talks[0].field(&EventData::actor) + index;
        }
    }

    // ---- conditions and values

    // #56: how the talk began (0 the cursor, 1 the town pages, 2 an event actor at a person, 3 a phone call).
    int WordsFn_TalkOrigin(GuestPtr<WordsCtx> ctx) { return ctx->origin; }
    int WordsFn_FewChoicesLeft(GuestPtr<WordsCtx> ctx) { return ctx->fewChoicesLeft; }
    int WordsFn_False(GuestPtr<WordsCtx>) { return 0; }
    int WordsFn_FamilyHasLeft(GuestPtr<WordsCtx> ctx) { return Family_HasLeft(ctx->familyA); }
    int WordsFn_FamilyInCity(GuestPtr<WordsCtx> ctx) { return Family_InCity(ctx->familyA); }
    int WordsFn_ConductTier(GuestPtr<WordsCtx> ctx) { return CityScene_ConductTier(ctx->city); }
    int WordsFn_ProsperityTier(GuestPtr<WordsCtx> ctx) { return CityScene_ProsperityTier(ctx->city); }
    int WordsFn_WeatherToday(GuestPtr<WordsCtx> ctx) { return ctx->city->weatherToday; }
    int WordsFn_WeatherTomorrow(GuestPtr<WordsCtx> ctx) { return ctx->city->weatherTomorrow; }
    int WordsFn_StaminaZero(GuestPtr<WordsCtx> ctx) { return Stamina_Zero(ctx->city->stamina); }
    int WordsFn_StaminaTier(GuestPtr<WordsCtx> ctx) { return Stamina_HungerLevel(ctx->city->stamina); }
    int WordsFn_IsApplicant(GuestPtr<WordsCtx> ctx) { return Applicants_Find(ctx->city->applicants, ctx->familyA); }
    int WordsFn_BeforeSix(GuestPtr<WordsCtx> ctx) { return ctx->city->clock < 21600 ? 1 : 0; }
    int WordsFn_HolidayMonth(GuestPtr<WordsCtx> ctx) { return (ctx->city->flags & 2u) != 0 ? 1 : 0; }
    int WordsFn_Month(GuestPtr<WordsCtx> ctx) { return ctx->city->monthCount % 12; }
    int WordsFn_PopulationRank(GuestPtr<WordsCtx> ctx) { return CityScene_PopulationRank(ctx->city) - 1; }
    int WordsFn_EventsActive(GuestPtr<WordsCtx> ctx) { return Event_CountActive(eventsOf(ctx)) > 0 ? 1 : 0; }
    int WordsFn_TalkEventVar(GuestPtr<WordsCtx> ctx) { return eventsOf(ctx)->talks[0]->var; }
    int WordsFn_SystemEventVar(GuestPtr<WordsCtx> ctx) { return eventsOf(ctx)->talks[1]->var; }

    // The talk scripts' own variable (in the stage state).
    int WordsFn_ScriptVar(GuestPtr<WordsCtx>) { return globals::stageState()->scriptVar; }

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

    int WordsFn_SetScriptVar(GuestPtr<WordsCtx> ctx)
    {
        globals::stageState()->scriptVar = ctx->arg;
        return port::guestV0();
    }

    int WordsFn_FocusPadAfterTalk(GuestPtr<WordsCtx> ctx)
    {
        ctx->city->flags |= 0x40u;
        return port::guestV0();
    }

    // Person A is let go by the event: flag 0x20 (held, or following) cleared.
    int WordsFn_ReleasePerson(GuestPtr<WordsCtx> ctx)
    {
        ctx->personA->flags &= 0xFFDFu;
        return port::guestV0();
    }

    int WordsFn_SetTalkEventVar(GuestPtr<WordsCtx> ctx)
    {
        eventsOf(ctx)->talks[0]->var = static_cast<uint8_t>(ctx->arg);
        return port::guestV0();
    }

    int WordsFn_SetSystemEventVar(GuestPtr<WordsCtx> ctx)
    {
        eventsOf(ctx)->talks[1]->var = static_cast<uint8_t>(ctx->arg);
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

    // End the talk event (talks[0]) or the system event (talks[1]), finishing it or abandoning it.
    int WordsFn_FinishTalkEvent(GuestPtr<WordsCtx> ctx) { return port::callGuest<int>(addr::Event_Finish, eventsOf(ctx), ctx->city, 0); }
    int WordsFn_AbortTalkEvent(GuestPtr<WordsCtx> ctx) { return port::callGuest<int>(addr::Event_Abort, eventsOf(ctx), ctx->city, 0); }
    int WordsFn_FinishSystemEvent(GuestPtr<WordsCtx> ctx) { return port::callGuest<int>(addr::Event_Finish, eventsOf(ctx), ctx->city, 1); }
    int WordsFn_AbortSystemEvent(GuestPtr<WordsCtx> ctx) { return port::callGuest<int>(addr::Event_Abort, eventsOf(ctx), ctx->city, 1); }

    int WordsFn_StartTalkEvent(GuestPtr<WordsCtx> ctx)
    {
        return port::callGuest<int>(addr::Event_CallHandlerSlot, eventsOf(ctx), ctx->city, 0, ctx->personA);
    }

    int WordsFn_TalkEventText(GuestPtr<WordsCtx> ctx)
    {
        return port::callGuest<int>(addr::Event_CallText, eventsOf(ctx), ctx->city, 0, static_cast<int>(ctx->arg));
    }

    int WordsFn_SystemEventText(GuestPtr<WordsCtx> ctx)
    {
        return port::callGuest<int>(addr::Event_CallText, eventsOf(ctx), ctx->city, 1, static_cast<int>(ctx->arg));
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
        // Bit `index` of a bitset of 32-bit words, with C's signed division for the word.
        uint32_t bitWord(uint32_t bitset, int index) { return bitset + static_cast<uint32_t>(4 * (index / 32)); }
        uint32_t bitMask(int index) { return 1u << (static_cast<uint32_t>(index) & 31u); }
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

    // Whether building A (or B) was built since the last morning (object flag 0x100, dropped between 6:00 and 7:00).
    int WordsFn_BuildingNew(GuestPtr<WordsCtx> ctx) { return (buildingByArg(ctx)->flags & 0x100u) != 0 ? 1 : 0; }
    int WordsFn_BuildingNeedPartlyMet(GuestPtr<WordsCtx> ctx) { return (buildingByArg(ctx)->flags & 2u) != 0 ? 1 : 0; }

    // Whether person A (B with an argument) is ill today (flag 0x10, rolled each new day, mm_Person_Update).
    int WordsFn_PersonIll(GuestPtr<WordsCtx> ctx)
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

    // The new buildings for families in the last six months: none 0, up to 10 1, more 2.
    int WordsFn_NewBuildingsTier(GuestPtr<WordsCtx> ctx)
    {
        const int sum = CityMap_SumNewBuildings(ctx->city->map);
        if (sum == 0)
            return 0;
        return sum < 11 ? 1 : 2;
    }

    // The story flags: two bitsets of the stage state (any bit number, by guest address as the original). The tests
    // return the masked word.
    namespace
    {
        uint32_t storyFlagsA() { return GuestPtr<uint32_t>::of(globals::stageState()->storyFlagsA[0]).address; }
        uint32_t storyFlagsB() { return GuestPtr<uint32_t>::of(globals::stageState()->storyFlagsB[0]).address; }
    }
    int WordsFn_StoryFlagA(GuestPtr<WordsCtx> ctx) { return static_cast<int>(port::load<uint32_t>(bitWord(storyFlagsA(), ctx->arg)) & bitMask(ctx->arg)); }
    int WordsFn_StoryFlagB(GuestPtr<WordsCtx> ctx) { return static_cast<int>(port::load<uint32_t>(bitWord(storyFlagsB(), ctx->arg)) & bitMask(ctx->arg)); }

    int WordsFn_SetStoryFlagB(GuestPtr<WordsCtx> ctx)
    {
        const uint32_t word = bitWord(storyFlagsB(), ctx->arg);
        port::store<uint32_t>(word, port::load<uint32_t>(word) | bitMask(ctx->arg));
        return port::guestV0();
    }

    int WordsFn_ClearStoryFlagB(GuestPtr<WordsCtx> ctx)
    {
        const uint32_t word = bitWord(storyFlagsB(), ctx->arg);
        port::store<uint32_t>(word, port::load<uint32_t>(word) & ~bitMask(ctx->arg));
        return port::guestV0();
    }

    // Whether the talk event is a resident's request or a visitor's (its type's group is 1 or 2).
    int WordsFn_TalkEventIsRequestOrVisit(GuestPtr<WordsCtx> ctx)
    {
        const uint8_t talking = globals::eventType(eventsOf(ctx)->talks[0]->kind)->group;
        return talking == 1 || talking == 2 ? 1 : 0;
    }

    // The system event's number in its type's group plus one, 0 without one.
    int WordsFn_SystemEventNumber(GuestPtr<WordsCtx> ctx)
    {
        const uint8_t kind = eventsOf(ctx)->talks[1]->kind;
        return kind == 0 ? 0 : globals::eventType(kind)->number + 1;
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

    // Whether family A's home is a police station (type 0x104 or 0x105; always 0 with an argument).
    int WordsFn_HomeIsPoliceStation(GuestPtr<WordsCtx> ctx)
    {
        const int type = Family_GetTypeIndex(ctx->familyA);
        if (ctx->arg != 0)
            return 0;
        return static_cast<uint32_t>(type - 0x104) < 2 ? 1 : 0;
    }

    // Whether the building's claim is a wish (else a need).
    int WordsFn_BuildingClaim(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<MapObject> building = buildingByArg(ctx);
        return Claim_GetList(typeOfObject(building)->claimB)[building->claimSlot].wish;
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
            return port::callGuest<int>(fromByte, person.field(&MapPerson::status).address);
        }
    }

    int WordsFn_TalksToday(GuestPtr<WordsCtx> ctx) { return memberStatus(ctx, addr::MemberStatus_TalksToday, addr::TalkStatus_TalksToday); }
    int WordsFn_RecentTalks(GuestPtr<WordsCtx> ctx) { return memberStatus(ctx, addr::MemberStatus_RecentTalks, addr::TalkStatus_RecentTalks); }

    namespace
    {
        // mm_Wants_Find's answers and where its search goes on, kept together in the callers' frames.
        struct WantCursor
        {
            uint32_t claimSlot;        // the want's claim slot
            uint32_t object;           // its building
            uint32_t person;           // and person
            int32_t iterator;          // where the next search starts (0: the first want)
        };

        // The next want from the cursor on; 0 when none is left.
        int nextWant(GuestPtr<CityScene> city, GuestPtr<WantCursor> want)
        {
            return Wants_Find(city->wants, city, want.field(&WantCursor::claimSlot), want.field(&WantCursor::object),
                              want.field(&WantCursor::person), want.field(&WantCursor::iterator));
        }
    }

    // Whether the city's wants list holds a want of person A.
    int WordsFn_HasWantForPerson(GuestPtr<WordsCtx> ctx)
    {
        struct Locals
        {
            uint8_t gap_00[0x20];
            WantCursor want;           // +0x20
        };
        static_assert(offsetof(Locals, want) == 0x20 && sizeof(Locals) == 0x30);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<WantCursor> want = frame.at(&Locals::want);
        want->iterator = 0;
        for (;;)
        {
            if (nextWant(ctx->city, want) == 0)
                return 0;
            if (want->person == ctx->personA->index)
                return 1;
        }
    }

    int WordsFn_FindWant(GuestPtr<WordsCtx> ctx)
    {
        struct Locals
        {
            uint8_t gap_00[0x10];
            WantCursor want;           // +0x10
        };
        static_assert(offsetof(Locals, want) == 0x10 && sizeof(Locals) == 0x20);
        port::GuestFrameOf<Locals> frame;
        frame->want.iterator = 0;
        return nextWant(ctx->city, frame.at(&Locals::want));
    }

    // ---- second group: texts

    GuestPtr<char> WordsFn_TextTalkEventName(GuestPtr<WordsCtx> ctx) { return globals::eventType(eventsOf(ctx)->talks[0]->kind).as<char>(); } // the name, at +0

    // Last month's count of new buildings for families (type flag 8: offices, businesses, ports, housing; CityMap.newBuildings
    // slot 1), with thousands separators.
    GuestPtr<char> WordsFn_TextNewBuildings(GuestPtr<WordsCtx> ctx)
    {
        Text_FormatThousands(GuestPtr<char>{kTextBuffer}, ctx->city->map->newBuildings[1]);
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
        return typeAt(type).field(&BuildingType::name);
    }

    namespace
    {
        // The bag item's goods record.
        GuestPtr<GoodsRecord> bagItemRecord(GuestPtr<WordsCtx> ctx) { return globals::goodsAt(ctx->bagSlot->group, ctx->bagSlot->index); }

        // The goods record of building type B for family B in the season (month / 4), with the argument as the
        // offset in and out: a local in the caller's frame at `slot`.
        GuestPtr<GoodsRecord> goodsRecord(GuestPtr<WordsCtx> ctx, uint32_t slot)
        {
            const GuestPtr<int32_t> offset{slot};
            *offset = ctx->arg;
            const int season = (ctx->city->monthCount % 12) / 4;
            return GuestPtr<GoodsRecord>{static_cast<uint32_t>(Goods_GetRecordOffset(ctx->buildingTypeB, ctx->familyB, season, offset))};
        }
    }

    GuestPtr<char> WordsFn_TextBagItemName(GuestPtr<WordsCtx> ctx)
    {
        return GuestPtr<char>{globals::goodsSectionC.get() + bagItemRecord(ctx)->name};
    }

    GuestPtr<char> WordsFn_TextGoodsName(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x20> frame;
        const GuestPtr<GoodsRecord> record = goodsRecord(ctx, frame.ptr().address + 0x1Cu);
        return GuestPtr<char>{globals::goodsSectionC.get() + record->name};
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
        // The id of the talk's first event's type (the wants it holds carry it).
        int eventId(GuestPtr<WordsCtx> ctx) { return static_cast<int16_t>(globals::eventType(eventsOf(ctx)->talks[0]->kind)->id); }

        // Map_FindPersonEntry with the halfwords as the original loads them.
        GuestPtr<MapPerson> findPerson(GuestPtr<WordsCtx> ctx, uint16_t family, uint16_t member)
        {
            return port::callGuest<GuestPtr<MapPerson>>(addr::Map_FindPersonEntry, ctx->city->map, static_cast<int>(family), static_cast<int>(member));
        }
    }

    int WordsFn_EventRemoveWant(GuestPtr<WordsCtx> ctx)
    {
        return port::callGuest<int>(addr::Wants_RemoveEventHold, ctx->city->wants, eventId(ctx), static_cast<int>(ctx->arg));
    }

    int WordsFn_EventAddWant(GuestPtr<WordsCtx> ctx)
    {
        return port::callGuest<int>(addr::Wants_AddEventHold, ctx->city->wants, eventId(ctx), static_cast<int>(ctx->arg));
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

        int eventCallAnswer(GuestPtr<WordsCtx> ctx, int index)
        {
            port::GuestFrame<0x20> frame;
            const GuestPtr<ActorRef> actor = actorA(ctx, frame.ptr().address + 0x18u);
            port::setGuestV0(ctx->memberA);
            return port::callGuest<int>(addr::Event_CallAnswer, eventsOf(ctx), ctx->city, actor, static_cast<int>(ctx->arg), index);
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

    // The answer slot of the handler of the talk's first or second event (talks[0], talks[1]), with person A and the argument.
    // mm_Event_CallAnswer returns nothing the port knows of; its v0 is the callbacks' result.
    int WordsFn_TalkEventAnswer(GuestPtr<WordsCtx> ctx) { return eventCallAnswer(ctx, 0); }
    int WordsFn_SystemEventAnswer(GuestPtr<WordsCtx> ctx) { return eventCallAnswer(ctx, 1); }

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

    // The stage of the event in the talk's slot (its type's number in its group); for group 5 plus the queue
    // position of event 5 less that of event 4 (the queue's length for one that is not queued).
    int WordsFn_EventStage(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<EventMgr> events = eventsOf(ctx);
        const GuestPtr<EventType> type = globals::eventType(events->slots[ctx->eventSlot].kind);
        const int stage = type->number;
        if (type->group != 5)
            return stage;
        const int at5 = EventMgr_FindType(events, 5);
        return stage + at5 - EventMgr_FindType(events, 4);
    }

    // ---- third group: buildings, people and goods

    namespace
    {
        GuestPtr<MapPerson> personByArg(GuestPtr<WordsCtx> ctx) { return ctx->arg != 0 ? ctx->personB : ctx->personA; }

        // The item the building claims.
        int claimOf(GuestPtr<MapObject> building)
        {
            return Claim_GetList(typeOfObject(building)->claimB)[building->claimSlot].id;
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

        // A building's own opening test: its type's isOpen slot, called with (type, city, building) and a fourth
        // argument, `heldA3`, that the slots ignore: what the caller's a3 held. v0 holds the type's entry in the type
        // table at the call.
        int callOpenSlot(GuestPtr<WordsCtx> ctx, GuestPtr<MapObject> building, uint32_t heldA3)
        {
            const GuestPtr<BuildingType> type = typeOfObject(building);
            port::setGuestV0(static_cast<int32_t>(globals::buildingTypeTable.get() + 4u * building->type));
            return port::callGuest<int>(type->vtable->isOpen, type, ctx->city, building, heldA3);
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
        if ((globals::stageState()->flags & 2u) != 0)
            agrees = Family_CanApply(ctx->familyA, ctx->city) != 0 ? 1 : 0;
        return ((ctx->city->flags & 4u) != 0 ? 1 : 0) | agrees;
    }

    // Whether family A is the first family of a set type: the Noodle Shop (argument 0), the Studio Appartment of type 0xB8
    // (1) or the Police Station of type 0x105 (2), the families the noodles and thief events bring in.
    int WordsFn_IsSpecialFamily(GuestPtr<WordsCtx> ctx)
    {
        uint32_t type;
        switch (ctx->arg)
        {
        case 0:
            type = globals::types::kNoodleShop;
            break;
        case 1:
            type = globals::types::kStudioAppartmentB8;
            break;
        case 2:
            type = globals::types::kPoliceStation105;
            break;
        default:
            return 0;
        }
        return globals::firstFamily(type) == ctx->familyA ? 1 : 0;
    }

    // Whether the money covers the goods' price.
    int WordsFn_CanAfford(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x30> frame;
        const GuestPtr<GoodsRecord> record = goodsRecord(ctx, frame.ptr().address + 0x2Cu);
        const int32_t money = globals::stageState()->money;
        return money < Goods_GetPrice(static_cast<int>(record.address)) ? 0 : 1;
    }

    // For goods carried in the bag (kinds from 0x10 except 0x12, 0x14, 0x16 and 0x19), whether the
    // bag is full: the last slot of its size holds an item.
    int WordsFn_GoodsBagFull(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x30> frame;
        const GuestPtr<GoodsRecord> record = goodsRecord(ctx, frame.ptr().address + 0x2Cu);
        const uint8_t category = record->kind;
        if (category < 0x10 || category == 0x12 || category == 0x16 || category == 0x14 || category == 0x19)
            return 0;
        const GuestPtr<Bag> bag = ctx->city->bag;
        if (bag->size == 0)
            return 0;
        const uint32_t slots = port::load<uint8_t>(globals::kBagSlotsBySize + bag->size);
        return GuestPtr<BagSlot>{bag.address + 4u + 16u * (slots - 1u)}->group != 0 ? 1 : 0;
    }

    // Whether the goods are owned already: laptop goods by the active laptop entries (bit `category`, and
    // categories 6 and 7 by entries 7 and 8), category 0x16 when its track is among the music tracks bought, category
    // 0x19 when mm_Family_CanApply refuses its value (the family).
    int WordsFn_GoodsOwned(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x40> frame;
        const GuestPtr<GoodsRecord> record = goodsRecord(ctx, frame.ptr().address + 0x3Cu);
        const uint32_t active = Laptop_CountActive();
        const uint8_t category = record->kind;
        if ((active & (1u << (category & 31u))) != 0)
            return 1;
        if (category >= 6 && category < 8)
            return (Laptop_CountActive() & 0x180u) != 0 ? 1 : 0;
        const uint8_t which = record->value;
        if (category == 0x16)
        {
            for (const BagMusicTrack &music : ctx->city->bag->musicTracks)
                if (static_cast<uint16_t>(music.track) == which)
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
        const int price = Goods_GetPrice(static_cast<int>(bagItemRecord(ctx).address));
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
        ctx->bagSlot = GuestPtr<BagSlot>{0};
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
        return memberStatus(ctx, addr::MemberStatus_LongApart, addr::TalkStatus_LongApart);
    }

    // The target of person A (or B): 0 none, 1 a building other than their home, else whether the type of
    // building A (or B) has flag 0x20 (an entrance).
    int WordsFn_WorksAtHome(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<MapPerson> person = personByArg(ctx);
        if (person->target == kNoObject)
            return 0;
        if (person->target != person->home)
            return 1;
        return (typeOfObject(buildingByArg(ctx))->flags & 0x20u) != 0 ? 1 : 0;
    }

    // Where person A (or B) heads: 0 without a person, 3 nowhere, 0 home, 1 to their target, else 2 or 3 by the
    // destination type's flag 0x40.
    int WordsFn_PersonHeading(GuestPtr<WordsCtx> ctx)
    {
        const GuestPtr<MapPerson> person = personByArg(ctx);
        if (!person)
            return 0;
        const uint16_t destination = person->destination;
        if (destination == kNoObject)
            return 3;
        if (destination == person->home)
            return 0;
        if (destination == person->target)
            return 1;
        return (typeOfObject(Map_GetRecord(ctx->city->map, destination))->flags & 0x40u) != 0 ? 2 : 3;
    }

    // Where person A (or B) is: 1 without a person, 3 outside, 0 in their target, 1 at home, else 2 or 3 by the
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
        if (inside == person->home)
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
    // whose target and home are elsewhere. 0 none, 1 up to half the type's capacity, 2 more.
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
            if ((cls->flags & 1u) != 0 || person->target == building->id || person->home == building->id)
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
        struct Locals
        {
            uint8_t gap_00[0x40];
            WantCursor want;           // +0x40
        };
        static_assert(offsetof(Locals, want) == 0x40 && sizeof(Locals) == 0x50);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<WantCursor> want = frame.at(&Locals::want);
        const int claim = claimOf(ctx->buildingA);
        want->iterator = 0;
        while (nextWant(ctx->city, want) != 0)
        {
            if (claimOf(Map_GetRecord(ctx->city->map, static_cast<int>(want->object))) == claim)
                return 2;
        }
        return Wants_HasRoom(ctx->city->wants, 4);
    }

    // The claim line of the chosen want's building (want number wantIndex in the wants list), 0 past the end.
    GuestPtr<char> WordsFn_TextWantClaim(GuestPtr<WordsCtx> ctx)
    {
        struct Locals
        {
            uint8_t gap_00[0x30];
            WantCursor want;           // +0x30
        };
        static_assert(offsetof(Locals, want) == 0x30 && sizeof(Locals) == 0x40);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<WantCursor> want = frame.at(&Locals::want);
        want->iterator = 0;
        for (int i = 0; nextWant(ctx->city, want) != 0; ++i)
        {
            if (i != ctx->wantIndex)
                continue;
            const GuestPtr<MapObject> building = Map_GetRecord(ctx->city->map, static_cast<int>(want->object));
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
    // their home 1 (or 2 when it is their target, its type has flag 0x20 and building B is open), 2 at their
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
        if (building->id == person->home)
        {
            if (person->home != person->target || (type->flags & 0x20u) == 0)
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
    PORT_FN(WordsFn_TalkEventAnswer);
    PORT_FN(WordsFn_SystemEventAnswer);
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
    PORT_FN(WordsFn_BuildingNew);
    PORT_FN(WordsFn_BuildingNeedPartlyMet);
    PORT_FN(WordsFn_PersonIll);
    PORT_FN(WordsFn_NameSlotSet);
    PORT_FN(WordsFn_ApplicantCountTier);
    PORT_FN(WordsFn_ApprovalTier);
    PORT_FN(WordsFn_RandomN);
    PORT_FN(WordsFn_Random50);
    PORT_FN(WordsFn_Random3);
    PORT_FN(WordsFn_LaptopHasBit);
    PORT_FN(WordsFn_EventActorIs);
    PORT_FN(WordsFn_NewBuildingsTier);
    PORT_FN(WordsFn_StoryFlagA);
    PORT_FN(WordsFn_StoryFlagB);
    PORT_FN(WordsFn_SetStoryFlagB);
    PORT_FN(WordsFn_ClearStoryFlagB);
    PORT_FN(WordsFn_TalkEventIsRequestOrVisit);
    PORT_FN(WordsFn_SystemEventNumber);
    PORT_FN(WordsFn_TimeOfDay);
    PORT_FN(WordsFn_FamilyCountTier);
    PORT_FN(WordsFn_HomeTypeFlag100);
    PORT_FN(WordsFn_HomeIsPoliceStation);
    PORT_FN(WordsFn_BuildingClaim);
    PORT_FN(WordsFn_TalksToday);
    PORT_FN(WordsFn_RecentTalks);
    PORT_FN(WordsFn_HasWantForPerson);
    PORT_FN(WordsFn_FindWant);
    PORT_FN(WordsFn_TextTalkEventName);
    PORT_FN(WordsFn_TextNewBuildings);
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
    PORT_FN(WordsFn_TalkOrigin);
    PORT_FN(WordsFn_FewChoicesLeft);
    PORT_FN(WordsFn_False);
    PORT_FN(WordsFn_FamilyHasLeft);
    PORT_FN(WordsFn_FamilyInCity);
    PORT_FN(WordsFn_ConductTier);
    PORT_FN(WordsFn_ProsperityTier);
    PORT_FN(WordsFn_WeatherToday);
    PORT_FN(WordsFn_WeatherTomorrow);
    PORT_FN(WordsFn_StaminaZero);
    PORT_FN(WordsFn_StaminaTier);
    PORT_FN(WordsFn_IsApplicant);
    PORT_FN(WordsFn_BeforeSix);
    PORT_FN(WordsFn_HolidayMonth);
    PORT_FN(WordsFn_Month);
    PORT_FN(WordsFn_PopulationRank);
    PORT_FN(WordsFn_EventsActive);
    PORT_FN(WordsFn_TalkEventVar);
    PORT_FN(WordsFn_SystemEventVar);
    PORT_FN(WordsFn_ScriptVar);
    PORT_FN(WordsFn_FamilyModArg);
    PORT_FN(WordsFn_PersonFlagBit);
    PORT_FN(WordsFn_EventCountdown);
    PORT_FN(WordsFn_FamilyKnown);
    PORT_FN(WordsFn_TextNameSlot);
    PORT_FN(WordsFn_SetCancelAllowed);
    PORT_FN(WordsFn_SetScriptVar);
    PORT_FN(WordsFn_FocusPadAfterTalk);
    PORT_FN(WordsFn_ReleasePerson);
    PORT_FN(WordsFn_SetTalkEventVar);
    PORT_FN(WordsFn_SetSystemEventVar);
    PORT_FN(WordsFn_PlaySe);
    PORT_FN(WordsFn_TalkEndChoosing);
    PORT_FN(WordsFn_EventQueueShift);
    PORT_FN(WordsFn_FinishTalkEvent);
    PORT_FN(WordsFn_AbortTalkEvent);
    PORT_FN(WordsFn_FinishSystemEvent);
    PORT_FN(WordsFn_AbortSystemEvent);
    PORT_FN(WordsFn_StartTalkEvent);
    PORT_FN(WordsFn_TalkEventText);
    PORT_FN(WordsFn_SystemEventText);
    PORT_FN(WordsFn_SetPose);
    PORT_FN(WordsFn_TalkSelectActor);

    // ---- context accessors and text joining (batch 38)

    uint16_t Words_GetBuildingTypeB(GuestPtr<Words> words) { return words->ctx->buildingTypeB; }
    int Words_GetCancelAllowed(GuestPtr<Words> words) { return words->ctx->cancelAllowed; }
    int Words_GetFriendshipChanged(GuestPtr<Words> words) { return words->ctx->friendshipChanged; }
    void Words_SetArg(GuestPtr<Words> words, uint16_t arg) { words->ctx->arg = static_cast<int16_t>(arg); }
    void Words_SetFewChoicesLeft(GuestPtr<Words> words) { words->ctx->fewChoicesLeft = 1; }
    void Words_SetWantIndex(GuestPtr<Words> words, uint16_t index) { words->ctx->wantIndex = static_cast<int16_t>(index); }

    // The same, for the game's one WORDS object.
    int Words_CancelAllowed() { return Words_GetCancelAllowed(globals::words()); }
    int Words_FriendshipChanged() { return Words_GetFriendshipChanged(globals::words()); }

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
        const uint32_t first = bag.address + 4u;
        const uint32_t end = first + 16u * port::load<uint8_t>(globals::kBagSlotsBySize + bag->size);
        int count = 0;
        for (GuestPtr<BagSlot> slot{first}; slot->group != 0 && slot.address < end; ++slot)
        {
            const uint8_t category = globals::goodsAt(slot->group, slot->index)->kind;
            if (category != 0x10 && category != 0x11 && category != 0x13)
                continue;
            if (count++ == index)
            {
                ctx->bagSlot = slot;
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
    PORT_FN(Words_SetFewChoicesLeft);
    PORT_FN(Words_SetWantIndex);
    PORT_FN(Words_CancelAllowed);
    PORT_FN(Words_FriendshipChanged);
    PORT_FN(Words_GetGoods);
    PORT_FN(Words_SelectBagItem);
    PORT_FN(Words_AppendClipped);

    // ---- script banks: entries, answers, choice counts (batch 39)

    namespace
    {
        // The WORDS text banks (WordsTables at 0x7181C0): per bank, line offsets into line codes, choice ranges, answer
        // records (mode, first string), string offsets and strings. Bank 2 belongs to the building type of the talk: its
        // tables are indexed through the remaps, from the type's bases (WordsTypeBase).
        GuestPtr<WordsTypeBase> typeBase()
        {
            const int32_t type = static_cast<int32_t>(Words_GetBuildingTypeB(globals::words()));
            return globals::wordsTables()->typeBases + type;
        }

        uint32_t remapped(GuestPtr<uint16_t> remap, int index, uint16_t base) { return remap[index + base]; }

        // The code at the current word of the line of text the script is on (Words scriptWords, scriptPos and column).
        int currentCode()
        {
            const GuestPtr<Words> words = globals::words();
            const int code = words->scriptWords[words->scriptPos] - 5000;
            const uint32_t column = words->column;
            const int bank = code / 1000, index = code % 1000;
            const uint32_t line = bank == 2 ? remapped(globals::wordsTables()->lineRemap, index, typeBase()->line)
                                            : globals::wordsBank(bank)->lineOffsets[index];
            return globals::wordsBank(bank)->lineCodes[static_cast<int32_t>(column + line)];
        }

        // The answer record of an answer code (5000 and up).
        GuestPtr<WordsAnswer> answerRecord(int code)
        {
            const int bank = (code - 5000) / 1000, index = (code - 5000) % 1000;
            if (bank == 2)
                return globals::wordsBank(2)->answers +
                       static_cast<int32_t>(remapped(globals::wordsTables()->answerRemap, index, typeBase()->answer));
            return globals::wordsBank(bank)->answers + index;
        }

        uint32_t bankString(int bank, uint32_t index)
        {
            const GuestPtr<WordsBank> b = globals::wordsBank(bank);
            return b->strings.address + b->stringOffsets[static_cast<int32_t>(index)];
        }
    }

    // The answer mode of the current entry.
    int16_t Words_GetEntryMode() { return answerRecord(currentCode())->mode; }

    // The default answer of the current entry: its first string.
    GuestPtr<char> Words_GetEntryDefault()
    {
        const int code = currentCode();
        return GuestPtr<char>{bankString((code - 5000) / 1000, answerRecord(code)->first)};
    }

    // Checks a typed answer against the current entry's other strings (from its second up to the next entry's
    // first); the column moves on once, and once more on a match.
    void Words_MatchTypedAnswer(int, GuestPtr<char> typed)
    {
        const GuestPtr<Words> words = globals::words();
        const int code = currentCode();
        const int bank = (code - 5000) / 1000;
        const GuestPtr<WordsAnswer> record = answerRecord(code);
        const int strings = (record + 1)->first - record->first;
        for (int i = 1; i < strings; ++i)
        {
            if (sdk::strcmp(typed.address, bankString(bank, answerRecord(code)->first + static_cast<uint32_t>(i))) == 0)
            {
                words->column = static_cast<uint16_t>(words->column + 1);
                break;
            }
        }
        words->column = static_cast<uint16_t>(words->column + 1);
    }

    // The number of choices of a choice code: from bank choice ranges for codes below -2005; for -2005 the entries of
    // bank 1's first range still offered (the bits of globals::wordsHiddenChoices clear); for -2004 the running events
    // plus one; for -2003 the wants that still hold; for -2002 one more than the bag's food (goods categories 0x10,
    // 0x11 and 0x13); otherwise 5.
    int Words_CountChoices(int, GuestPtr<CityScene> city, int code)
    {
        if (code < -2005)
        {
            const int bank = (code + 6000) / 1000, index = (code + 6000) % 1000;
            const GuestPtr<uint16_t> range =
                bank == 2 ? globals::wordsBank(2)->choiceRanges +
                                static_cast<int32_t>(remapped(globals::wordsTables()->choiceRemap, index, typeBase()->choice))
                          : globals::wordsBank(bank)->choiceRanges + index;
            return range[1] - range[0];
        }
        if (code == -2005)
        {
            const GuestPtr<uint16_t> range = globals::wordsBank(1)->choiceRanges;
            const int entries = range[1] - range[0];
            int left = entries;
            for (int i = 0; i < entries; ++i)
                if ((globals::wordsHiddenChoices.get() & (1u << (i & 31))) != 0)
                    --left;
            return left;
        }
        if (code == -2004)
            return Event_CountActive(city->events) + 1;
        if (code == -2003)
        {
            struct Locals
            {
                WantCursor want;       // +0x00
                uint8_t gap_10[0x10];
            };
            static_assert(sizeof(Locals) == 0x20);
            port::GuestFrameOf<Locals> frame;
            frame->want.iterator = 0;
            int found = 0;
            while (nextWant(city, frame.at(&Locals::want)) != 0)
                ++found;
            return found;
        }
        if (code != -2002)
            return 5;
        const GuestPtr<Bag> bag = city->bag;
        const uint32_t end = bag.address + 4u + 16u * port::load<uint8_t>(globals::kBagSlotsBySize + bag->size);
        int count = 1;
        for (GuestPtr<BagSlot> slot{bag.address + 4u}; slot->group != 0 && slot.address < end; ++slot)
        {
            const uint8_t category = globals::goodsAt(slot->group, slot->index)->kind;
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
        // A family's member by number, by guest address arithmetic, so a wild member number wraps in RDRAM like the
        // original's indexing.
        GuestPtr<FamilyMember> memberAt(GuestPtr<Family> family, uint32_t member)
        {
            return GuestPtr<FamilyMember>::of(family->members[0]) + static_cast<int32_t>(member);
        }

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

    GuestPtr<char> WordsFn_TextMemberGivenName(GuestPtr<WordsCtx> ctx)
    {
        const MemberRef who = memberByThreeWayArg(ctx);
        return GuestPtr<char>::of(memberAt(Family_Get(static_cast<int>(who.family), 0), who.member)->givenName[0]);
    }

    GuestPtr<char> WordsFn_TextMemberSurname(GuestPtr<WordsCtx> ctx)
    {
        const MemberRef who = memberByThreeWayArg(ctx);
        return GuestPtr<char>::of(memberAt(Family_Get(static_cast<int>(who.family), 0), who.member)->surname[0]);
    }

    // The member's post at their home's building type (STRTYPE POSTNAME).
    GuestPtr<char> WordsFn_TextOccupation(GuestPtr<WordsCtx> ctx)
    {
        const MemberRef who = memberByThreeWayArg(ctx);
        const GuestPtr<Family> family = Family_Get(static_cast<int>(who.family), 0);
        const uint32_t homeType = family->homeType;
        const uint8_t role = memberAt(family, who.member)->role;
        return BldgType_GetPostName(GuestPtr<BuildingType>{globals::buildingTypeAddress(homeType)}, role);
    }

    uint8_t WordsFn_MemberLook(GuestPtr<WordsCtx> ctx)
    {
        return memberAt(Family_Get(ctx->familyA, 0), ctx->memberA)->look;
    }

    uint8_t WordsFn_MemberUnk06(GuestPtr<WordsCtx> ctx)
    {
        return memberAt(Family_Get(ctx->familyA, 0), ctx->memberA)->unk_06;
    }

    // The character type of member A, with the types above 0x1D folded: 0x26 becomes 0xD, the rest 0.
    uint16_t WordsFn_MemberCharType(GuestPtr<WordsCtx> ctx)
    {
        uint16_t charType = memberAt(Family_Get(ctx->familyA, 0), ctx->memberA)->charType;
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
        const uint8_t role = memberAt(family, member)->role;
        const bool flag = (memberAt(family, member)->flags & 1u) != 0;
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
                type = Family_Get(ctx->familyA, 0)->homeType;
            else
                type = ctx->buildingA->type;
        }
        else
            type = ctx->buildingTypeB;
        return globals::buildingType(type)->category;
    }

    namespace
    {
        // The friendship change both actions make: the meter step is 3 (down) or 5 (up) plus bits 21-23 of the character
        // type's traits; the result is what the friendship call leaves in v0.
        int changeFriendship(GuestPtr<WordsCtx> ctx, int base, int sign)
        {
            const GuestPtr<Family> family = Family_Get(ctx->familyA, 0);
            const uint32_t charType = memberAt(family, ctx->memberA)->charType;
            const uint32_t cls = port::load<uint32_t>(globals::charClassTable.get() + 4u * charType);
            const int bits = static_cast<int>((GuestPtr<CharClass>{cls}->traits & 0xE00000u) >> 0x15);
            const int points = bits + base;
            port::setGuestV0(bits);  // the friendship call returns v0 as it found it on some paths: the shifted bits here
            const int result = Family_AddFriendshipScaled(ctx->familyA, ctx->memberA, ctx->city.field(&CityScene::approval), points, sign);
            ctx->friendshipChanged = 1;
            return result;
        }
    }

    int WordsFn_FriendshipDown(GuestPtr<WordsCtx> ctx) { return changeFriendship(ctx, 3, -1); }
    int WordsFn_FriendshipUp(GuestPtr<WordsCtx> ctx) { return changeFriendship(ctx, 5, 1); }

    PORT_FN(WordsFn_TextFamilyRecord);
    PORT_FN(WordsFn_TextMemberGivenName);
    PORT_FN(WordsFn_TextMemberSurname);
    PORT_FN(WordsFn_TextOccupation);
    PORT_FN(WordsFn_MemberLook);
    PORT_FN(WordsFn_MemberUnk06);
    PORT_FN(WordsFn_MemberCharType);
    PORT_FN(WordsFn_MemberRole);
    PORT_FN(WordsFn_BuildingCategory);
    PORT_FN(WordsFn_FriendshipDown);
    PORT_FN(WordsFn_FriendshipUp);

    // ---- Script context set-up and the resident pickers ----

    namespace
    {
        // The fields both set-up calls clear: the second person and building, the results and the script's flags.
        void resetContext(GuestPtr<WordsCtx> ctx, GuestPtr<CityScene> city, int origin)
        {
            ctx->origin = origin;
            ctx->city = city;
            ctx->familyB = 0;
            ctx->memberB = 0;
            ctx->personB = GuestPtr<MapPerson>{0u};
            ctx->buildingB = GuestPtr<MapObject>{0u};
            ctx->buildingTypeB = 0;
            ctx->bagSlot = GuestPtr<BagSlot>{0u};
            ctx->result = 0;
            ctx->cancelAllowed = 1;
            ctx->fewChoicesLeft = 0;
            ctx->arg = 0;
            ctx->eventSlot = 0;
            ctx->wantIndex = 0;
            ctx->friendshipChanged = 0;
        }
    }

    // Starts a script on a family member: the family is prefetched into the cache, person and building A stay empty.
    // `origin` is how the talk began (WordsCtx.origin, read by the script's condition #56).
    void Words_SetFamily(GuestPtr<Words> words, int family, uint16_t member, int origin, GuestPtr<CityScene> city)
    {
        Family_Prefetch(family, 0);
        const GuestPtr<WordsCtx> ctx = words->ctx;
        ctx->familyA = static_cast<uint16_t>(family);
        ctx->memberA = member;
        ctx->personA = GuestPtr<MapPerson>{0u};
        ctx->buildingA = GuestPtr<MapObject>{0u};
        resetContext(ctx, city, origin);
    }

    // The same for a person on the map: building A is the person's home.
    void Words_SetPerson(GuestPtr<Words> words, GuestPtr<MapPerson> person, int origin, GuestPtr<CityScene> city)
    {
        Family_Prefetch(person->family, 0);
        const GuestPtr<WordsCtx> ctx = words->ctx;
        ctx->familyA = person->family;
        ctx->memberA = person->member;
        ctx->personA = person;
        ctx->buildingA = Map_GetRecord(city->map, person->home);
        resetContext(ctx, city, origin);
    }

    // Lists (in ctx->candidates, up to 128) the ids of the objects the family may call on: the occupied homes with
    // people inside whose entrance lies within the member's range of the building, and the objects on the same tile
    // as each, except the building itself.
    void Words_CollectNearbyHomes(GuestPtr<WordsCtx> ctx, GuestPtr<MapObject> building)
    {
        ctx->candidateCount = 0;
        sdk::memset(port::guest(ctx->candidates), 0, 0x80);
        const int32_t x0 = building->tile & 0x7F;
        const int32_t z0 = building->tile >> 7;
        const int32_t size = globals::buildingType(building->type)->size;
        const GuestPtr<Family> family = Family_Get(ctx->familyA, 0);
        const int32_t range = port::load<int32_t>(0x001B97F0u + 4u * memberAt(family, ctx->memberA)->look);
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
                    ctx->candidates[at] = record->id;
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
            ctx->buildingB = Map_GetRecord(ctx->city->map, ctx->candidates[index]);
            ctx->buildingTypeB = ctx->buildingB->type;
            const GuestPtr<Family> family = Family_Get(ctx->buildingB->family, 0);
            const uint32_t members = family->memberCount;
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
            const GuestPtr<MapObject> object = Map_GetRecord(ctx->city->map, ctx->candidates[i]);
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
        const uint32_t members = family->memberCount;
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
        const uint32_t list = GuestPtr<uint16_t>::of(memberAt(Family_Get(who.family, 0), who.member)->acquaintances[0]).address;
        const uint32_t end = list + 0x20u;
        for (uint32_t at = list; port::load<uint16_t>(at) != 0 && at < end; at += 2u)
        {
            const uint32_t acquaintance = port::load<uint16_t>(at);
            const uint32_t type = static_cast<uint32_t>(Family_GetTypeIndex(static_cast<int>(acquaintance)));
            if (globals::buildingType(type)->category != 5)
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
        const GuestPtr<BagSlot> bagSlot = ctx->bagSlot;
        if (ctx->buildingA && bagSlot->group == 0x96)
        {
            const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(ctx->buildingA->type)};
            if (btype->category != 4)
                return 0;
            return btype->rule == 0 ? 1 : 0;
        }
        const uint32_t item = globals::goodsAt(bagSlot->group, bagSlot->index)->name;
        uint32_t charType;
        if (ctx->personA)
            charType = ctx->personA->charType;
        else
            charType = memberAt(Family_Get(ctx->familyA, 0), ctx->memberA)->charType;
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

    // A new entry of the script block cache (WordsBlock): empty (block 0), with a buffer of one block. The first one
    // opens the script files (WORDS.BIN and WORDSEVE.BIN, 0x8000-byte buffers); each one is counted.
    GuestPtr<WordsBlock> WordsBlock_ctor(GuestPtr<WordsBlock> entry)
    {
        entry->number = 0;
        entry->data = GuestPtr<uint8_t>{sdk::mw_NewArray(static_cast<uint32_t>(globals::wordsBlockSize.get()))};
        if (globals::wordsFileUsers.get() == 0)
        {
            const auto open = [](GuestVar<GuestPtr<GameFile>> holder, uint32_t path) {
                const uint32_t memory = sdk::operator_new(8u);
                uint32_t file = 0u;
                if (memory != 0u)
                    file = File_ctor(GuestPtr<GameFile>{memory}, 0x8000).address;
                holder.set(GuestPtr<GameFile>{file});
                File_Open(GuestPtr<GameFile>{file}, GuestPtr<char>{path});
            };
            open(globals::wordsFile, 0x001BAD90u);
            open(globals::wordsEventFile, 0x001BADB0u);
            FileQueue_WaitAll(0);
        }
        globals::wordsFileUsers.set(globals::wordsFileUsers.get() + 1);
        return entry;
    }

    // Finds a script block in the cache of eight (a most-recently-used list; each slot points at a word that points at
    // its WordsBlock): block 0 means the first entry's block (0x16 when that is 0 too). A block not cached replaces the
    // last entry: it is read from WORDS.BIN (block number - 1 blocks past the file's first word, the block size) into
    // that entry's buffer, and the read is left queued. The entry found moves to the front of the list and the banks
    // are bound again on the next step (Words bound cleared). Returns the first slot's word.
    GuestPtr<uint32_t> Words_ReadBlock(int block)
    {
        constexpr uint32_t kList = globals::kWordsBlockCache, kListEnd = globals::kWordsBlockCacheEnd;
        const auto entryAt = [](uint32_t slot) { return GuestPtr<WordsBlock>{port::load<uint32_t>(port::load<uint32_t>(slot))}; };
        if (block == 0)
        {
            block = entryAt(kList)->number;
            if (block == 0)
                block = 0x16;
        }
        uint32_t slot = kList;
        while (slot < kListEnd && block != entryAt(slot)->number)
            slot += 4u;
        if (slot != kList)
        {
            if (slot == kListEnd)
            {
                slot -= 4u;
                const GuestPtr<WordsBlock> entry = entryAt(slot);
                entry->number = block;
                const int32_t size = globals::wordsBlockSize.get();
                File_Seek(globals::wordsFile.get(), size * (entry->number - 1) + 4, 0, 0);
                globals::wordsPendingRead.set(File_Read(globals::wordsFile.get(), static_cast<int>(entry->data.address), 1, size, 0));
            }
            const uint32_t moved = port::load<uint32_t>(slot);
            sdk::memmove(kList + 4u, kList, slot - kList);
            port::store<uint32_t>(kList, moved);
            globals::words()->bound = 0;
        }
        return GuestPtr<uint32_t>{port::load<uint32_t>(kList)};
    }

    // The static Words object (0x1BDDF0) is constructed at start-up and registered to be destroyed at exit.
    void StaticInit_Words()
    {
        const GuestPtr<Words> words = Words_ctor(globals::words());
        sdk::mw_register_global_object(words.address, 0x00180490u, 0x007181B0u);
    }

    PORT_FN(WordsBlock_ctor);
    PORT_FN(Words_ReadBlock);
    PORT_FN(StaticInit_Words);

    // ---- Script actions that forward to other calls (plan P6 batch 2) ----

    // The title text of the event in the script's slot.
    GuestPtr<char> WordsFn_TextEventTitle(GuestPtr<WordsCtx> ctx) { return Event_FormatText(ctx->city->events, ctx->eventSlot); }

    // The person the script talks to gets a new family to introduce (the 16-bit pick is kept in introduces); the result is the pick,
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

    // The family introduction of the script's first side (when the arg is 0) or second side: a person there keeps their
    // own (introduces), otherwise one is picked for the family and member. The pick (16 bits) becomes the result; 1 when
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

    // Applies the choice made in a script's menu (`code` says which menu, `choice` the entry). The script moves on by
    // the entry's place in the menu: its column while it is part-way through a line of text (Words column not 0),
    // otherwise its place in the script's words (scriptPos). Menus below -0x7D4 and -0x7D1 move on by the choice. Menu
    // -0x7D5 picks the choice-th entry still offered (globals::wordsHiddenChoices; the hidden ones before it count in
    // the move as well), hides it and, when fewer than two of the menu's entries are left, sets the script's flag
    // 0x134; -0x7D4 moves on by the result of the event chosen (mm_Words_SelectEvent); -0x7D2 selects the bag item when
    // the choice is not the last entry and moves on by one; -0x7D3 sets the want index and -0x7D1 the argument.
    void Words_ApplyChoice(int script, GuestPtr<CityScene> city, int code, int choice)
    {
        const GuestPtr<Words> words = globals::words();
        const GuestPtr<uint16_t> place = words->column != 0 ? words.field(&Words::column) : words.field(&Words::scriptPos);
        const auto add = [&](uint32_t amount) { place[0] = static_cast<uint16_t>(place[0] + amount); };
        const auto hidden = [] { return static_cast<uint32_t>(globals::wordsHiddenChoices.get()); };
        if (code < -0x7D4 || code == -0x7D1)
            add(static_cast<uint16_t>(choice));
        if (code == -0x7D5)
        {
            uint32_t bit = 0;
            int unused = 0;
            for (uint32_t i = 0;; ++i)
            {
                bit = 1u << (i & 31u);
                if ((hidden() & bit) == 0)
                {
                    ++unused;
                    if (choice < unused)
                        break;
                }
                else
                    add(1);
            }
            globals::wordsHiddenChoices.set(static_cast<uint8_t>(hidden() | (bit & 0xFFu)));
            const GuestPtr<uint16_t> table = globals::wordsBank(1)->choiceRanges;
            const int entries = static_cast<int>(table[1]) - static_cast<int>(table[0]);
            int left = entries;
            for (int j = 0; j < entries; ++j)
                if ((hidden() & (1u << (j & 31))) != 0)
                    --left;
            if (left < 2)
                Words_SetFewChoicesLeft(words);
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
    // into the number a talk script tests, by the script's argument (arg): 0 gives states 1 and 2 as 0 and 1, 3 as 2 and the
    // rest 3; 1 gives 4 and 5 as 0 and 1, the rest 2; 2 gives 6 and 7 as 0 and 1, the rest 2; 3 gives states 6 to 12 as 0 to 6, the
    // rest 7; 4 gives 0 for state 13 and 1 for the others; 5 gives the state less 1 (less 2 from 15), 15 when that is negative.
    uint32_t WordsFn_ScheduleState(GuestPtr<WordsCtx> ctx)
    {
        const uint32_t state = (ctx->personA ? ctx->personA->schedState : Schedule_Evaluate(ctx->familyA, ctx->memberA)) & 0xFFu;
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

    // The player buys the goods the script points at (by building type B and family B, the season from the city's month):
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
        int32_t month = remainder(city->monthCount, 12);
        if (month < 0)
            month += 3;
        const GuestPtr<GoodsRecord> record{static_cast<uint32_t>(Goods_GetRecordOffset(ctx->buildingTypeB, ctx->familyB, month >> 2, GuestPtr<int32_t>{offsetAt}))};
        const int32_t price = Goods_GetPrice(static_cast<int>(record.address));
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
        const GuestPtr<Laptop> laptop{city->laptop.address};
        const uint32_t bag = city->bag.address;
        const uint32_t kind = record->kind;
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
            if (port::load<uint16_t>(bag) < record->value)
                port::store<uint16_t>(bag, record->value);
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
                        city->monthCount, static_cast<int16_t>(ctx->buildingB->family), 0xFF, 0);
            break;
        case 0x12:
        case 0x14:
            Stamina_Eat(city->stamina, record);
            break;
        case 0x16:
            Bag_AddMusicTrack(GuestPtr<Bag>{bag}, ctx->buildingTypeB, port::load<int32_t>(offsetAt));
            break;
        case 0x19:
            Applicants_Add(city->applicants, record->value, city->monthCount, 8);
            break;
        default:
            break;
        }
        Sound_PlaySe(0x12, 0, 0x3C, ps2f(0.0f), ps2f(0.0f));
    }

    PORT_FN(WordsFn_BuyGoods);

    // Looks through the acquaintances (up to 16 families, the script person's member record) of the script's family
    // for one that can apply to the city and whose home type claims something the building A supplies: the claim list of the
    // building (a single value, or the list of section D when the value has bit 15) is compared with the type's claims (section
    // C of the acquaintance's home type); the first match becomes the person's introduction (introduces) and the result and
    // gives 1. 0 when there is none.
    int WordsFn_AcquaintanceSuppliesClaim(GuestPtr<WordsCtx> ctx)
    {
        port::GuestFrame<0x60> frame;
        const uint32_t local = frame.ptr().address + 0x5Cu;
        const uint32_t building = ctx->buildingA.address;
        const GuestPtr<ClaimEntry> entry = Claim_GetList(globals::buildingType(port::load<uint16_t>(building))->claimB) +
                                           GuestPtr<MapObject>{building}->claimSlot;
        port::store<uint32_t>(local, port::load<uint32_t>(0x001BDC20u));
        port::store<uint16_t>(local, entry->id);
        uint32_t claims = local;
        if (entry->id >= 0x8000)
            claims = Claim_GetGroup(port::load<uint16_t>(local)).address;
        const uint32_t member = (Family_Get(ctx->familyA, 0).field(&Family::members) + ctx->memberA).field(&FamilyMember::acquaintances).address;
        const uint32_t end = member + 0x20u;
        for (uint32_t at = member;; at += 2u)
        {
            const uint32_t family = port::load<uint16_t>(at);
            if (family == 0 || at >= end)
                return 0;
            if (Family_CanApply(family, ctx->city) == 0)
                continue;
            const uint32_t type = static_cast<uint32_t>(Family_GetTypeIndex(static_cast<int>(port::load<uint16_t>(at)))) & 0xFFFFu;
            for (uint32_t want = Claim_GetSupplies(globals::buildingType(type)->claimC).address; port::load<uint16_t>(want) != 0; want += 4u)
                for (uint32_t have = claims; port::load<uint16_t>(have) != 0; have += 2u)
                    if (port::load<uint16_t>(want) == port::load<uint16_t>(have))
                    {
                        ctx->personA->introduces = port::load<uint16_t>(at);
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
        const uint32_t activity = ctx->personA ? ctx->personA->activity : Schedule_MemberActivity(ctx->city, ctx->familyA, ctx->memberA);
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

    // The same for the family introduced to the script's person (the result): after it, the person's member flag is
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
        // The event words (WORDSEVE.BIN, a block per event kind) a script may use: kind 0 has none. A kind that is not
        // the one held is read into eventBlock (the read left queued), and the banks are bound again on the next step.
        void loadEventWords(uint32_t kind)
        {
            const GuestPtr<Words> words = globals::words();
            if (kind == 0)
            {
                words->eventLoaded = 0;
                return;
            }
            if (static_cast<int32_t>(kind) != words->eventKind)
            {
                const int32_t size = globals::wordsEventBlockSize.get();
                File_Seek(globals::wordsEventFile.get(), size * (static_cast<int32_t>(kind) - 1) + 4, 0, 0);
                globals::wordsPendingRead.set(File_Read(globals::wordsEventFile.get(), static_cast<int>(words->eventBlock.address), 1,
                                                        globals::wordsEventBlockSize.get(), 0));
                words->eventKind = static_cast<int32_t>(kind);
                words->bound = 0;
            }
            words->eventLoaded = 1;
        }

        // Once the banks are bound, the script runs its block's own words or, with event words loaded, the words the
        // event block gives for that block (the event block's first header word leads to an entry per block number, its
        // second to the words; an entry below 0 keeps the block's own). `slot` is the cache slot's word
        // (mm_Words_ReadBlock).
        void pickScriptWords(GuestPtr<uint32_t> slot)
        {
            const GuestPtr<Words> words = globals::words();
            if (words->bound == 0)
                return;
            const GuestPtr<WordsBlock> entry{port::load<uint32_t>(slot.address)};
            const uint32_t data = entry->data.address;
            words->scriptWords = GuestPtr<int16_t>{data + port::load<uint32_t>(data)};
            if (words->eventLoaded == 0)
                return;
            const uint32_t event = words->eventBlock.address;
            const int32_t first = port::load<int16_t>(event + port::load<uint32_t>(event) + 2u * static_cast<uint32_t>(entry->number));
            if (first >= 0)
                words->scriptWords = GuestPtr<int16_t>{event + port::load<uint32_t>(event + 4u) + 2u * static_cast<uint32_t>(first)};
        }
    }

    // Prepares the script of a family member: the object is set to the member (mm_Words_SetFamily), entries 1 and 2 of
    // the -2005 menu are hidden (globals::wordsHiddenChoices: bit 2 always; bit 1 for a member under 13 or with a
    // friendship under 24), the script restarts at its first word, the member's script block is read
    // (mm_Words_ReadBlock, by their character type), the event words for the city's current event are loaded and the
    // script's words picked. The result is the block's cache slot.
    int Words_LoadForFamily(int family, int member, int origin, GuestPtr<CityScene> city)
    {
        const GuestPtr<Words> words = globals::words();
        Words_SetFamily(words, family, static_cast<uint16_t>(member), origin, city);
        globals::wordsHiddenChoices.set(4);
        words->scriptPos = 0;
        words->column = 0;
        if (memberAt(Family_Get(family, 0), static_cast<uint32_t>(member))->age < 13 || Family_GetFriendship(family, member) < 24)
            globals::wordsHiddenChoices.set(static_cast<uint8_t>(globals::wordsHiddenChoices.get() | 2u));
        const GuestPtr<uint32_t> block = Words_ReadBlock(memberAt(Family_Get(family, 0), static_cast<uint32_t>(member))->charType);
        loadEventWords(city->events->talks[0]->kind);
        pickScriptWords(block);
        return static_cast<int>(block.address);
    }

    // The same for a person in the city: the script block is the one of the character type (charType), the event words
    // are for the event the person starts (mm_Event_TryStart, when `origin` is 0: the player walked up) or the city's
    // current one. The choice menus are marked as above, but with an item of category 0x10, 0x11 or 0x13 among the
    // bag's slots (its size picks how many are looked at) no entry is unavailable, and with none the age and friendship
    // rule applies to the person's family member.
    int Words_LoadForPerson(GuestPtr<MapPerson> person, int origin, GuestPtr<CityScene> city)
    {
        const GuestPtr<Words> words = globals::words();
        Words_SetPerson(words, person, origin, city);
        words->scriptPos = 0;
        words->column = 0;
        const GuestPtr<uint32_t> block = Words_ReadBlock(person->charType);
        const uint32_t kind = origin == 0 ? Event_TryStart(city->events, city, person) : city->events->talks[0]->kind;
        loadEventWords(kind);
        pickScriptWords(block);
        globals::wordsHiddenChoices.set(4);
        const GuestPtr<Bag> bag = city->bag;
        const uint32_t end = bag.address + 4u + 16u * port::load<uint8_t>(globals::kBagSlotsBySize + bag->size);
        for (GuestPtr<BagSlot> slot{bag.address + 4u}; slot->group != 0 && slot.address < end; ++slot)
        {
            const uint32_t category = globals::goodsAt(slot->group, slot->index)->kind;
            if (category == 0x10 || category == 0x11 || category == 0x13)
            {
                globals::wordsHiddenChoices.set(0);
                break;
            }
        }
        if (memberAt(Family_Get(person->family, 0), person->member)->age < 13 || Family_GetFriendship(person->family, person->member) < 24)
            globals::wordsHiddenChoices.set(static_cast<uint8_t>(globals::wordsHiddenChoices.get() | 2u));
        return static_cast<int>(block.address);
    }

    PORT_FN(Words_LoadForFamily);
    PORT_FN(Words_LoadForPerson);

    // Reads the script data at start-up with a synchronous file on the stack: WORDSCOM.BIN and WORDSTEN.BIN are read
    // whole (Words commonData and typeData), WORDSEVE.BIN gives its block size and eventBlock a buffer of one block,
    // and WORDS.BIN gives its block size and, with it, the eight entries of the block cache (each slot points at a word
    // that points at a new WordsBlock, mm_WordsBlock_ctor).
    void Words_Init()
    {
        port::GuestFrame<0x50> frame;
        const GuestPtr<GameFile> file{frame.ptr().address + 0x48u};
        const GuestPtr<Words> words = globals::words();
        File_ctor(file, 0);
        // A whole file into a new block: the block is kept in `holder` before it is read.
        const auto readWhole = [&](uint32_t path, GuestPtr<void> &holder) {
            File_Open(file, GuestPtr<char>{path});
            const int size = File_Seek(file, 0, 2, 0);
            File_Seek(file, 0, 0, 0);
            holder = GuestPtr<void>{sdk::mw_NewArray(static_cast<uint32_t>(size))};
            File_Read(file, static_cast<int>(holder.address), size, 1, 0);
            File_Close(file);
        };
        // WORDSCOM.BIN's six table offsets bind bank 1; WORDSTEN.BIN's eleven bind the remaps (typeBases to
        // stringRemap), then bank 2.
        const GuestPtr<uint32_t> bank1 = globals::wordsBank(1).as<uint32_t>(), bank2 = globals::wordsBank(2).as<uint32_t>();
        const GuestPtr<uint32_t> remaps = globals::wordsTables().field(&WordsTables::typeBases).as<uint32_t>();
        readWhole(0x001BADD0u, words->commonData);
        for (int32_t i = 0; i < 6; ++i)
        {
            const uint32_t block = words->commonData.address;
            bank1[i] = block + port::load<uint32_t>(block + 4u * static_cast<uint32_t>(i));
        }
        readWhole(0x001BADF0u, words->typeData);
        for (int32_t i = 0; i < 11; ++i)
        {
            const uint32_t block = words->typeData.address;
            (i < 5 ? remaps[i] : bank2[i - 5]) = block + port::load<uint32_t>(block + 4u * static_cast<uint32_t>(i));
        }
        File_Open(file, GuestPtr<char>{0x001BADB0u});
        File_Read(file, static_cast<int>(globals::wordsEventBlockSize.address), 1, 4, 0);
        File_Close(file);
        words->eventBlock = GuestPtr<void>{sdk::mw_NewArray(static_cast<uint32_t>(globals::wordsEventBlockSize.get()))};
        File_Open(file, GuestPtr<char>{0x001BAD90u});
        File_Read(file, static_cast<int>(globals::wordsBlockSize.address), 1, 4, 0);
        for (uint32_t i = 0; i < 8; ++i)
        {
            const uint32_t holder = sdk::operator_new(4u);
            if (holder != 0u)
            {
                uint32_t record = sdk::operator_new(8u);
                if (record != 0u)
                    record = WordsBlock_ctor(GuestPtr<WordsBlock>{record}).address;
                port::store<uint32_t>(holder, record);
            }
            port::store<uint32_t>(globals::kWordsBlockCache + 4u * i, holder);
        }
        File_dtor(file, -1);
    }

    PORT_FN(Words_Init);

    // The text of a choice in a script menu, by the menu's code. Below -2005 the code names a menu of a bank (bank
    // (code + 6000) / 1000, menu (code + 6000) % 1000): the text is the menu's index-th string, from the first its
    // choice range gives (bank 2's menus through the building type's remap). The special codes: -2005 the index-th
    // entry still offered (globals::wordsHiddenChoices); -2004 the default choice text for index 0, otherwise the text
    // of the city's event slot index - 1; -2003 the claim line of the index-th open want of the wants window; -2002 the
    // name of the index-th bag item of category 0x10, 0x11 or 0x13 (or the default when there are fewer); any other
    // code gives goods number index (under 4) with its price right-aligned to 28 characters in
    // globals::kWordsGoodsText, or a fixed text from 4 on.
    GuestPtr<char> Words_GetChoiceText(int script, GuestPtr<CityScene> city, int code, int index)
    {
        (void)script;
        struct Locals
        {
            uint8_t gap_00[0x40];
            WantCursor want;           // +0x40 the wants for code -0x7D3
        };
        static_assert(offsetof(Locals, want) == 0x40 && sizeof(Locals) == 0x50);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<WordsBank> common = globals::wordsBank(1);
        const auto defaultText = [&](int32_t string)
        {
            return GuestPtr<char>{common->strings.address + common->stringOffsets[string]};
        };
        if (code < -0x7D5)
        {
            const int group = (code + 6000) / 1000;
            const int entry = (code + 6000) % 1000;
            if (group != 2)
            {
                const GuestPtr<WordsBank> bank = globals::wordsBank(group);
                const uint32_t first = bank->choiceRanges[entry];
                const uint32_t offset = bank->stringOffsets[index + static_cast<int>(first)];
                return GuestPtr<char>{bank->strings.address + offset};
            }
            const GuestPtr<WordsBank> bank = globals::wordsBank(2);
            const uint32_t line = remapped(globals::wordsTables()->choiceRemap, entry, typeBase()->choice);
            const uint32_t block = bank->choiceRanges[static_cast<int32_t>(line)];
            const uint32_t offset = bank->stringOffsets[static_cast<int32_t>(static_cast<uint32_t>(index) + block)];
            return GuestPtr<char>{bank->strings.address + offset};
        }
        if (code == -0x7D5)
        {
            const uint32_t hidden = globals::wordsHiddenChoices.get();
            int bit = 0, seen = 0;
            for (;;)
            {
                if ((hidden & (1u << bit)) != 0)
                {
                    ++bit;
                    continue;
                }
                ++seen;
                if (index < seen)
                    break;
                ++bit;
            }
            const uint32_t base = common->choiceRanges[0];
            return defaultText(static_cast<int32_t>(static_cast<uint32_t>(bit) + base));
        }
        if (code == -0x7D4)
        {
            if (index == 0)
                return defaultText(0);
            return Event_FormatText(city->events, index - 1);
        }
        if (code == -0x7D3)
        {
            const GuestPtr<WantCursor> want = frame.at(&Locals::want);
            want->iterator = 0;
            for (int counted = 0;; ++counted)
            {
                if (nextWant(city, want) == 0)
                    return GuestPtr<char>{0};
                if (counted == index)
                    break;
            }
            const GuestPtr<MapObject> object = Map_GetRecord(city->map, static_cast<int>(want->object));
            return GuestPtr<char>{static_cast<uint32_t>(
                Claim_GetLine(globals::buildingType(object->type)->claimB, object->claimSlot))};
        }
        if (code == -0x7D2)
        {
            const GuestPtr<Bag> bag = city->bag;
            const uint32_t end = bag.address + 4u + 16u * port::load<uint8_t>(globals::kBagSlotsBySize + bag->size);
            int counted = 0;
            for (GuestPtr<BagSlot> slot{bag.address + 4u}; slot->group != 0 && slot.address < end; ++slot)
            {
                const GuestPtr<GoodsRecord> record = globals::goodsAt(slot->group, slot->index);
                const uint32_t category = record->kind;
                if (category != 0x10 && category != 0x11 && category != 0x13)
                    continue;
                if (counted == index)
                    return GuestPtr<char>{globals::goodsSectionC.get() + record->name};
                ++counted;
            }
            return defaultText(1);
        }
        if (index >= 4)
            return GuestPtr<char>{0x001BAD68u};
        const uint32_t goods = static_cast<uint32_t>(Words_GetGoods(globals::words(), index));
        sdk::strcpy(globals::kWordsGoodsText, globals::goodsSectionC.get() + GuestPtr<GoodsRecord>{goods}->name);
        const uint32_t length = static_cast<uint32_t>(sdk::strlen(globals::kWordsGoodsText));
        const int price = Goods_GetPrice(static_cast<int>(goods));
        Text_FormatMoney(GuestPtr<char>{globals::kWordsGoodsText + length}, price, 0x1C - static_cast<int>(length), 0);
        return GuestPtr<char>{globals::kWordsGoodsText};
    }

    PORT_FN(Words_GetChoiceText);

    // The text of a line of the wants window that has its own script: when the city has an event running (its kind is
    // not 0) and the event queue holds the id at the same place as the kind, the script block of the default speaker is
    // read, the Words object is reset (family 0, member 0, argument 4), the output text cleared, and the event words
    // for the kind loaded. The first time the tables are bound: any read still running is waited for, bank 0's six
    // tables (WordsTables, from the block's header offsets) and, with event words, bank 3's are set, the script's words
    // are picked as for a family (mm_Words_LoadForFamily) and the banks count as bound. The line (+3000) is then
    // expanded into `out`.
    void Words_LoadForWants(GuestPtr<CityScene> city, int16_t id, int line, GuestPtr<char> out)
    {
        const uint32_t kind = city->events->talks[0]->kind;
        if (kind == 0 || static_cast<int32_t>(kind) != EventMgr_FindType(city->events, id))
            return;
        const GuestPtr<uint32_t> block = Words_ReadBlock(0);
        const GuestPtr<Words> words = globals::words();
        Words_SetFamily(words, 0, 0, 4, city);
        words->scriptPos = 0;
        words->column = 0;
        port::store<uint8_t>(out.address, 0);
        loadEventWords(kind);
        if (words->bound == 0)
        {
            const GuestPtr<WordsBlock> entry{port::load<uint32_t>(block.address)};
            if (globals::wordsPendingRead.get() >= 0)
            {
                FileQueue_WaitAll(0);
                globals::wordsPendingRead.set(-1);
            }
            const uint32_t data = entry->data.address;
            const GuestPtr<uint32_t> bank0 = globals::wordsBank(0).as<uint32_t>(), bank3 = globals::wordsBank(3).as<uint32_t>();
            for (int32_t i = 0; i < 6; ++i)
                bank0[i] = data + port::load<uint32_t>(data + 4u + 4u * static_cast<uint32_t>(i));
            const bool eventLoaded = words->eventLoaded != 0;
            const uint32_t event = words->eventBlock.address;
            if (eventLoaded)
                for (int32_t i = 0; i < 6; ++i)
                    bank3[i] = event + port::load<uint32_t>(event + 8u + 4u * static_cast<uint32_t>(i));
            words->scriptWords = GuestPtr<int16_t>{data + port::load<uint32_t>(data)};
            if (eventLoaded)
            {
                const int32_t first = port::load<int16_t>(event + port::load<uint32_t>(event) + 2u * static_cast<uint32_t>(entry->number));
                if (first >= 0)
                    words->scriptWords = GuestPtr<int16_t>{event + port::load<uint32_t>(event + 4u) + 2u * static_cast<uint32_t>(first)};
            }
            words->bound = 1;
        }
        Words_ExpandLine(GuestPtr<int32_t>{port::load<uint32_t>(block.address)}, line + 3000, out);
    }

    PORT_FN(Words_LoadForWants);

    // The common body of the three "supplies a want" scripts. The want the wants window offers at the chosen index (the context's
    // wantIndex) gives the script's other person and building (B) and the claims the building supplies: a single value of the building
    // type's claim section B entry, or the list of section D when the value has bit 15 set. Mode 0 (a family) asks whether the home
    // type of family A claims one of them. Modes 1 (an acquaintance of member A) and 2 (of any member of the family) look through the
    // acquaintances (up to 16 families each) that can apply to the city for one whose home type claims one: it becomes the result
    // and, in mode 1, person A's introduction (introduces); mode 2 also notes the member (memberIndex). 1 on a match, else 0.
    int WordsFn_SuppliesWantBody(GuestPtr<WordsCtx> ctx, int mode)
    {
        // The want's answers sit here in another order than WantCursor's.
        struct Locals
        {
            uint8_t gap_00[0x9C];
            uint16_t oneClaim[2];      // +0x9C a list of one claim (from 0x1BDC24: the id, then the 0 that ends it)
            int32_t iterator;          // +0xA0 mm_Wants_Find's place
            uint32_t claimSlot;        // +0xA4 the want's claim slot
            uint32_t person;           // +0xA8 its person
            uint32_t object;           // +0xAC its building
        };
        static_assert(offsetof(Locals, oneClaim) == 0x9C && offsetof(Locals, iterator) == 0xA0 &&
                      offsetof(Locals, object) == 0xAC && sizeof(Locals) == 0xB0);
        port::GuestFrameOf<Locals> frame;
        const uint32_t local = frame.at(&Locals::oneClaim).address;
        port::store<uint32_t>(local, port::load<uint32_t>(0x001BDC24u));
        frame->iterator = 0;
        uint32_t claims = local;
        for (int index = 0; Wants_Find(ctx->city->wants, ctx->city, frame.at(&Locals::claimSlot), frame.at(&Locals::object),
                                       frame.at(&Locals::person), frame.at(&Locals::iterator)) != 0;
             ++index)
        {
            if (index != ctx->wantIndex)
                continue;
            const GuestPtr<CityMap> map = ctx->city->map;
            const uint32_t object = frame->object;
            const GuestPtr<ClaimEntry> list = Claim_GetList(globals::buildingType(Map_GetRecord(map, static_cast<int>(object))->type)->claimB);
            const uint32_t value = list[static_cast<int32_t>(frame->claimSlot)].id;
            if (static_cast<int32_t>(value) < 0x8000)
                frame->oneClaim[0] = static_cast<uint16_t>(value);
            else
                claims = Claim_GetGroup(value).address;
            ctx->personB = ctx->city->map->people + frame->person;
            ctx->familyB = ctx->personB->family;
            ctx->memberB = ctx->personB->member;
            ctx->buildingB = Map_GetRecord(ctx->city->map, static_cast<int>(object));
            ctx->buildingTypeB = ctx->buildingB->type;
            break;
        }
        // Whether the home type of `family` claims one of the claims supplied; the types' claim section C lists end with a 0 value.
        const auto wantsOneOf = [&](uint32_t family) {
            const uint32_t type = static_cast<uint32_t>(Family_GetTypeIndex(static_cast<int>(family))) & 0xFFFFu;
            for (uint32_t want = Claim_GetSupplies(globals::buildingType(type)->claimC).address;
                 port::load<uint16_t>(want) != 0; want += 4u)
                for (uint32_t have = claims; port::load<uint16_t>(have) != 0; have += 2u)
                    if (port::load<uint16_t>(want) == port::load<uint16_t>(have))
                        return true;
            return false;
        };
        if (mode == 0)
            return Family_CanApply(ctx->familyA, ctx->city) != 0 && wantsOneOf(ctx->familyA) ? 1 : 0;
        const GuestPtr<Family> family = Family_Get(ctx->familyA, 0);
        const uint32_t first = family.field(&Family::members).address;
        uint32_t member = mode == 2 ? first : first + 0x48u * ctx->memberA;
        const uint32_t end = mode == 2 ? member + 0x48u * family->memberCount : member + 0x48u;
        for (; member < end; member += 0x48u)
        {
            const uint32_t list = member + 8u;
            for (uint32_t at = list; port::load<uint16_t>(at) != 0 && at < list + 0x20u; at += 2u)
            {
                if (Family_CanApply(port::load<uint16_t>(at), ctx->city) == 0 || !wantsOneOf(port::load<uint16_t>(at)))
                    continue;
                if (ctx->personA && mode == 1)
                    ctx->personA->introduces = port::load<uint16_t>(at);
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

    // Picks the resident a claim script talks about. The homes around building A are collected (candidates, candidateCount),
    // and the claims the selected want asks for are worked out as in WordsFn_SuppliesWantBody. The residents of each candidate home
    // that are loaded are looked through: the acquaintances of each member (up to 16 families) that can apply to the city, whose
    // home type claims one of the claims, give person B (the home's family, the member's index); 4 when that member's friendship
    // allows it (family B, member B, building B, its type and the result are set), else the search goes on and ends with
    // 3. With no result (and arg -1000) a resident by type is tried (2); with none either a random resident of a random candidate
    // home becomes person B (1). 0 when there is no candidate or no resident.
    int WordsFn_PickClaimResident(GuestPtr<WordsCtx> ctx)
    {
        struct Locals
        {
            uint8_t gap_00[0xFC];
            uint16_t oneClaim[2];      // +0xFC a list of one claim (from 0x1BDC1C: the id, then the 0 that ends it)
            WantCursor want;           // +0x100
        };
        static_assert(offsetof(Locals, oneClaim) == 0xFC && offsetof(Locals, want) == 0x100 && sizeof(Locals) == 0x110);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<WantCursor> want = frame.at(&Locals::want);
        const uint32_t local = frame.at(&Locals::oneClaim).address;
        Words_CollectNearbyHomes(ctx, ctx->buildingA);
        if (ctx->candidateCount == 0)
            return 0;
        port::store<uint32_t>(local, port::load<uint32_t>(0x001BDC1Cu));
        want->iterator = 0;
        uint32_t claims = local;
        int result = 0;
        for (int index = 0; nextWant(ctx->city, want) != 0; ++index)
        {
            if (index != ctx->wantIndex)
                continue;
            const GuestPtr<ClaimEntry> list = Claim_GetList(
                globals::buildingType(Map_GetRecord(ctx->city->map, static_cast<int>(want->object))->type)->claimB);
            const uint32_t value = list[static_cast<int32_t>(want->claimSlot)].id;
            if (static_cast<int32_t>(value) < 0x8000)
                frame->oneClaim[0] = static_cast<uint16_t>(value);
            else
                claims = Claim_GetGroup(value).address;
            break;
        }
        if (port::load<uint16_t>(claims) != 0)
        {
            for (int k = 0; k < ctx->candidateCount; ++k)
            {
                const GuestPtr<MapObject> home = Map_GetRecord(ctx->city->map, ctx->candidates[k]);
                const uint32_t family = static_cast<uint32_t>(Family_IsCached(home->family));
                if (family == 0)
                    continue;
                const uint32_t first = memberAt(GuestPtr<Family>{family}, 0).address;
                const uint32_t end = memberAt(GuestPtr<Family>{family}, GuestPtr<Family>{family}->memberCount).address;
                for (uint32_t member = first; member < end; member += 0x48u)
                {
                    const uint32_t memberIndex = static_cast<uint32_t>(static_cast<int32_t>(member - first) / 0x48);
                    const uint32_t list = member + 8u;
                    for (uint32_t at = list; port::load<uint16_t>(at) != 0 && at < list + 0x20u; at += 2u)
                    {
                        if (Family_CanApply(port::load<uint16_t>(at), ctx->city) == 0)
                            continue;
                        const uint32_t type = static_cast<uint32_t>(Family_GetTypeIndex(static_cast<int>(port::load<uint16_t>(at)))) & 0xFFFFu;
                        for (uint32_t want = Claim_GetSupplies(globals::buildingType(type)->claimC).address;
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
        ctx->buildingB = Map_GetRecord(ctx->city->map, ctx->candidates[pick]);
        ctx->buildingTypeB = ctx->buildingB->type;
        const GuestPtr<Family> residents = Family_Get(ctx->buildingB->family, 0);
        const uint32_t count = residents->memberCount;
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

    // One step of the WORDS script interpreter on a cached script block (`script` is its WordsBlock): reads the script's
    // words from scriptPos on until one gives a result other than -6009 ("go on") and returns it. While the banks are not
    // bound it first waits for a read still queued, binds bank 0 to the script block and, with event words, bank 3 to the
    // event block (WordsTables, from the blocks' header offsets), and picks the script's words as mm_Words_LoadForWants
    // does. Words: below -9010 the context's argument is set (word + 10000); -9010 up to -8001 an action through its
    // pointer, whose result says how many words to skip (from -8900 on the action is word + 8927; from -8960 the argument
    // is the word's 26ths from -8960 and the action the remainder + 1; below that the argument is word + 9000 and the
    // action 0); 5000 and up a line of text, added to the buffer after a separator unless it adds nothing (its result is
    // returned at once when the line was left part-way, Words column not 0); 0 to 4999 jump to that word; -6010 up to -1
    // that result; -6011 starts the script over; below that a condition call (word + 7000).
    int16_t Words_StepImpl(GuestPtr<int32_t> script, GuestPtr<char> buffer)
    {
        const GuestPtr<WordsBlock> entry{script.address};
        const GuestPtr<Words> words = globals::words();
        if (words->bound == 0)
        {
            if (globals::wordsPendingRead.get() >= 0)
            {
                FileQueue_WaitAll(0);
                globals::wordsPendingRead.set(-1);
            }
            const uint32_t data = entry->data.address;
            const int32_t eventLoaded = words->eventLoaded;
            const GuestPtr<uint32_t> bank0 = globals::wordsBank(0).as<uint32_t>(), bank3 = globals::wordsBank(3).as<uint32_t>();
            for (int32_t i = 0; i < 6; ++i)
                bank0[i] = data + port::load<uint32_t>(data + 4u + 4u * static_cast<uint32_t>(i));
            if (eventLoaded != 0)
            {
                const uint32_t event = words->eventBlock.address;
                for (int32_t i = 0; i < 6; ++i)
                    bank3[i] = event + port::load<uint32_t>(event + 8u + 4u * static_cast<uint32_t>(i));
            }
            words->scriptWords = GuestPtr<int16_t>{data + port::load<uint32_t>(data)};
            if (eventLoaded != 0)
            {
                const uint32_t event = words->eventBlock.address;
                const int32_t first = port::load<int16_t>(event + port::load<uint32_t>(event) + 2u * static_cast<uint32_t>(entry->number));
                if (first >= 0)
                    words->scriptWords = GuestPtr<int16_t>{event + port::load<uint32_t>(event + 4u) + 2u * static_cast<uint32_t>(first)};
            }
            words->bound = 1;
        }
        port::store<uint8_t>(buffer.address, 0);
        int32_t result = -0x1779;
        for (;;)
        {
            const int32_t token = words->scriptWords[words->scriptPos];
            const auto act = [&](int index) { return port::callGuest<int>(addr::Words_CallAct, words, index); };
            const auto skip = [&](int count) {
                words->scriptPos = static_cast<uint16_t>(words->scriptPos + (static_cast<uint32_t>(count) & 0xFFFFu));
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
                result = Words_ExpandLine(script, token - 0x1388, buffer);
                if (separated && length + 1 == sdk::strlen(buffer.address))
                    port::store<uint8_t>(buffer.address + static_cast<uint32_t>(length), 0);
                if (words->column != 0)
                    return static_cast<int16_t>(result);
            }
            else if (token >= 0)
                words->scriptPos = static_cast<uint16_t>(token - 1);
            else if (token >= -0x177A)
                result = token;
            else if (token != -0x177B)
                port::callGuest<int>(addr::Words_CallCond, words, token + 0x1B58);
            else
            {
                const uint32_t data = entry->data.address;
                words->scriptWords = GuestPtr<int16_t>{data + port::load<uint32_t>(data)};
                words->scriptPos = 0xFFFFu;
            }
            words->scriptPos = static_cast<uint16_t>(words->scriptPos + 1u);
            if (result != -0x1779)
                return static_cast<int16_t>(result);
        }
    }

    // The same through the script's cache slot word (what mm_Words_LoadForPerson and mm_Words_LoadForFamily return),
    // which points at its WordsBlock.
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
        callMember(0x001B9630u, static_cast<uint32_t>(index), words->ctx.address);
    }

    int Words_CallText(GuestPtr<Words> words, int index)
    {
        return callMember(0x001B9570u, static_cast<uint32_t>(index), words->ctx.address);
    }

    int Words_CallAct(GuestPtr<Words> words, int index)
    {
        return callMember(0x001B9100u, static_cast<uint32_t>(index), words->ctx.address);
    }

    PORT_FN(Words_CallCond);
    PORT_FN(Words_CallText);
    PORT_FN(Words_CallAct);

    // Expands one line of script text (`code` = bank * 1000 + line) into `out`, from the column the script has reached
    // (Words column) until a word ends the step. `script` is not used. The words of a line, read from the bank's line
    // codes (bank 2 through the building type's remap):
    //   10000 and up   a string of the bank (bank = (word - 10000) / 1000), appended to the output like a spoken word
    //   5000 to 9999   an answer (bank = (word - 5000) / 1000): its selector picks which of the answer record's strings is added;
    //                  below -9000 a random one, otherwise the result of an action (the same ranges as below)
    //   below -9010    the context's argument is set to word + 10000
    //   -9010 to -8001 an action through its pointer, whose result moves the column on (from -8900 the action is word + 8927; from
    //                  -8960 the argument is (word + 8960) / 26 and the action the remainder + 1; below that the argument is
    //                  word + 9000 and the action 0)
    //   -8000 to -7001 a text getter, added to the output (the same three ranges: word + 7907; arguments in sixes from -7960;
    //                  word + 8000 and getter 0)
    //   -7000 to -6011 a condition call (word + 7000)
    //   -6010, -6000 to -1   the step ends with that word, the column left after it
    //   -6009 to -6001 the step ends with that word, the column back at 0
    //   0 to 4999      jump to that column
    // The result is the last word read.
    int16_t Words_ExpandLine(GuestPtr<int32_t>, int code, GuestPtr<char> out)
    {
        const GuestPtr<Words> words = globals::words();
        const int bank = code / 1000, index = code % 1000;
        const auto word = [&](uint32_t column) {
            const uint32_t line = bank == 2 ? remapped(globals::wordsTables()->lineRemap, index, typeBase()->line)
                                            : globals::wordsBank(bank)->lineOffsets[index];
            return static_cast<int32_t>(globals::wordsBank(bank)->lineCodes[static_cast<int32_t>(column + line)]);
        };
        const auto act = [&](int number) { return port::callGuest<int>(addr::Words_CallAct, words, number); };
        const auto getText = [&](int number) { return port::callGuest<uint32_t>(addr::Words_CallText, words, number); };
        uint32_t column = words->column;
        int32_t token = word(column);
        while (token >= -6000 || token < -6009)
        {
            if (token >= 10000)
            {
                const int stringBank = (token - 10000) / 1000, number = (token - 10000) % 1000;
                const uint32_t text =
                    stringBank == 2 ? globals::wordsBank(2)->strings.address +
                                          remapped(globals::wordsTables()->stringRemap, number, typeBase()->string)
                                    : bankString(stringBank, static_cast<uint32_t>(number));
                Words_AppendClipped(out, GuestPtr<char>{text});
            }
            else if (token >= 5000)
            {
                const int answerBank = (token - 5000) / 1000;
                const int selector = answerRecord(token)->mode;
                int choice;
                if (selector < -9000)
                {
                    const GuestPtr<WordsAnswer> record = answerRecord(token);
                    const int count = (record + 1)->first - record->first;
                    choice = count != 0 ? eeRemainder(lowWord(Random_Next()), count) : 0;
                }
                else if (selector >= -8900)
                    choice = act(selector + 8927);
                else if (selector >= -8960)
                {
                    Words_SetArg(words, static_cast<uint16_t>((selector + 8960) / 26));
                    choice = act((selector + 8960) % 26 + 1);
                }
                else
                {
                    Words_SetArg(words, static_cast<uint16_t>(selector + 9000));
                    choice = act(0);
                }
                const uint32_t first = answerRecord(token)->first;
                Words_AppendClipped(out, GuestPtr<char>{bankString(answerBank, static_cast<uint32_t>(choice) + first)});
            }
            else if (token < -9010)
                Words_SetArg(words, static_cast<uint16_t>(token + 10000));
            else if (token < -8000)
            {
                int moved;
                if (token >= -8900)
                    moved = act(token + 8927);
                else if (token >= -8960)
                {
                    Words_SetArg(words, static_cast<uint16_t>((token + 8960) / 26));
                    moved = act((token + 8960) % 26 + 1);
                }
                else
                {
                    Words_SetArg(words, static_cast<uint16_t>(token + 9000));
                    moved = act(0);
                }
                column += static_cast<uint32_t>(moved);
            }
            else if (token < -7000)
            {
                uint32_t text;
                if (token >= -7900)
                    text = getText(token + 7907);
                else if (token >= -7960)
                {
                    Words_SetArg(words, static_cast<uint16_t>((token + 7960) / 6));
                    text = getText((token + 7960) % 6 + 1);
                }
                else
                {
                    Words_SetArg(words, static_cast<uint16_t>(token + 8000));
                    text = getText(0);
                }
                Words_AppendClipped(out, GuestPtr<char>{text});
            }
            else if (token < 0)
            {
                if (token >= -6010)
                {
                    words->column = static_cast<uint16_t>(column + 1u);
                    return static_cast<int16_t>(token);
                }
                port::callGuest<int>(addr::Words_CallCond, words, token + 7000);
            }
            else
                column = static_cast<uint32_t>(token) - 1u;
            column += 1u;
            token = word(column);
        }
        words->column = 0;
        return static_cast<int16_t>(token);
    }

    PORT_FN(Words_ExpandLine);
}
