// City events: the handler slots of the event types (table 0x1AF0D0: Check, Start, Update, End, Text, Answer),
// the event manager's talk slots and the waiting queue. Every handler gets the active event (EventData) and the
// city; Check slots say whether the event may start, Start and End mark and clear the buildings taking part
// (flag 0x200) and the wants held for them, Text gives the talk a text and Answer a value.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"
#include "vu0.h"

namespace mm
{
    namespace
    {
        // Actor `index` of an event that lists several (8 bytes each from EventData::actor).
        GuestPtr<ActorRef> actorAt(GuestPtr<EventData> data, int index)
        {
            return data.field(&EventData::actor) + index;
        }

        // Marks the object record and hands it back: the start slots leave it in v0, which the talk script reads.
        GuestPtr<MapObject> markBuilding(GuestPtr<CityScene> city, uint16_t object)
        {
            const GuestPtr<MapObject> record = Map_GetRecord(city->map, object);
            record->flags |= 0x200u;
            return record;
        }
        void unmarkBuilding(GuestPtr<CityScene> city, uint16_t object) { Map_GetRecord(city->map, object)->flags &= 0xFDFFu; }

        // The buildings of `count` actors from `first`, those that have one. Returns the last record marked, or the
        // register as it was on entry when none was (so call it first in a start slot).
        int64_t markActorBuildings(GuestPtr<CityScene> city, GuestPtr<EventData> data, int first, int count)
        {
            int64_t result = port::guestV0Wide();
            for (int i = first; i < first + count; ++i)
                if (actorAt(data, i)->object != kNoObject)
                    result = markBuilding(city, actorAt(data, i)->object).address;
            return result;
        }

        void unmarkActorBuildings(GuestPtr<CityScene> city, GuestPtr<EventData> data, int count)
        {
            for (int i = 0; i < count; ++i)
                if (actorAt(data, i)->object != kNoObject)
                    unmarkBuilding(city, actorAt(data, i)->object);
        }

        // Drops the wants the event held (by its type's id), holds 0 to count - 1.
        void releaseWantHolds(GuestPtr<EventData> data, GuestPtr<CityScene> city, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                const int id = static_cast<int16_t>(globals::eventTypeCopy(data->kind)->id);
                port::callGuest<void>(addr::Wants_RemoveEventHold, city->wants, id, i);
            }
        }

        // Entry `index` of the family type table (gp-0x7EE4).
    }

    // ---- the event manager and the queue

    // Talk slots in use: the slots are filled from the front, so the count stops at the first free one.
    int Event_CountActive(GuestPtr<EventMgr> events)
    {
        int count = 0;
        for (const EventSlot &slot : events->slots)
        {
            if (slot.kind == 0)
                break;
            ++count;
        }
        return count;
    }

    int EventMgr_FindType(GuestPtr<EventMgr>, int16_t id)
    {
        const GuestPtr<uint16_t> ids{globals::kEventTypeIds}, end = ids + globals::eventTypeCount.get();
        int position = 0;
        for (GuestPtr<uint16_t> entry = ids; entry < end; ++entry, ++position)
            if (*entry == static_cast<uint16_t>(id))
                return position;
        return position;
    }

    void EventMgr_RequeueFirst(GuestPtr<EventMgr> events)
    {
        const uint32_t slots = events.field(&EventMgr::slots).address;
        EventQueue_InsertByPriority(globals::eventQueue(), GuestPtr<EventQueueEntry>{slots}, 2, 0);
        sdk::memmove(slots, slots + 0x20u, 0x80);
        sdk::memset(slots + 0x80u, 0, 0x20);
    }

    // Removes the entry from the queue: the entries after it move up (the length rounded down to whole entries,
    // counted from the entry) and the starts of buckets `bucket` + 1 to 8 move back one (the last is the count).
    void EventQueue_RemoveAt(GuestPtr<EventQueue> queue, int bucket, GuestPtr<EventQueueEntry> entry)
    {
        const uint32_t end = (GuestPtr<EventQueueEntry>::of(queue->entries[0]) + queue->starts[8]).address;
        const int32_t bytes = static_cast<int32_t>(end - entry.address);
        sdk::memmove(entry.address, entry.address + 2u, (bytes / 2) * 2);
        for (int i = bucket + 1; i < 9; ++i)
            queue->starts[i] = queue->starts[i] - 1;
    }

    // The claims an event gives building types while it runs (EventType.claimList - 1 into CLAIM.BIN's event claim
    // lists): adding copies each claim into its slot, the end of a type's claim list, so the type claims it after its
    // own; removing copies the slot's next 8 bytes (the list's end, as before) back over it. Field by field, as the
    // original copies them.
    void Event_AddClaims(int list)
    {
        const GuestPtr<EventClaimList> claims = globals::eventClaimList(list);
        uint32_t count = claims->count;
        for (GuestPtr<EventClaim> claim = claims.field(&EventClaimList::claims); count != 0; --count, ++claim)
        {
            const GuestPtr<ClaimEntry> slot = globals::claimSlot(claim->slot);
            slot->wish = claim->claim.wish;
            slot->unk_01 = claim->claim.unk_01;
            slot->id = claim->claim.id;
            slot->value = claim->claim.value;
            slot->reach = claim->claim.reach;
        }
    }

    void Event_RemoveClaims(int list)
    {
        const GuestPtr<EventClaimList> claims = globals::eventClaimList(list);
        uint32_t count = claims->count;
        for (GuestPtr<EventClaim> claim = claims.field(&EventClaimList::claims); count != 0; --count, ++claim)
        {
            const GuestPtr<ClaimEntry> slot = globals::claimSlot(claim->slot);
            const GuestPtr<ClaimEntry> next = slot + 1;
            slot->wish = next->wish;
            slot->unk_01 = next->unk_01;
            slot->id = next->id;
            slot->value = next->value;
            slot->reach = next->reach;
        }
    }

    // ---- the actors

    // The actor's person stops following anyone.
    void EvActor_StopFollowing(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor)
    {
        if (actor->person == kNoPerson)
            return;
        const GuestPtr<MapPerson> person = city->map->people + actor->person;
        if ((person->flags & 0x20u) == 0)
            return;
        person->flags &= 0xFFDFu;
        person->flags &= 0xFF7Fu;
        person->follow = kNoPerson;
    }

    namespace
    {
        GuestPtr<BuildingType> typeOf(GuestPtr<MapObject> building) { return GuestPtr<BuildingType>{globals::buildingTypeAddress(building->type)}; }

        // The person leaves the building they are in: one that is neither their home nor their target and
        // counts its people (type flag 0x40) loses one, and their walk tiles reset (0x4000). When they were inside,
        // v0 becomes the building record (the actor functions leave it there).
        void leaveBuilding(GuestPtr<CityScene> city, GuestPtr<MapPerson> person, int32_t &v0)
        {
            if (person->inside == kNoObject)
                return;
            const GuestPtr<MapObject> building = Map_GetRecord(city->map, person->inside);
            v0 = static_cast<int32_t>(building.address);
            if (building->id != person->home && building->id != person->target && (typeOf(building)->flags & 0x40u) != 0 &&
                building->occupants > 0)
                --building->occupants;
            person->walkTile = 0x4000;
            person->previousTile = 0x4000;
            person->inside = kNoObject;
        }

        // A person who starts following someone takes flag 0x40 in rain and snow (weather 2 and 3) and loses it
        // otherwise.
        void weatherFlag(GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
        {
            if (city->weatherToday == 2 || city->weatherToday == 3)
                person->flags |= 0x40u;
            else
                person->flags &= 0xFFBFu;
        }
    }

    // The actor's person leaves any building and waits (flag 0x20, following nobody). The result is v0 as the
    // original leaves it: the building record when they were inside one, else v0 as it came in.
    int EvActor_Detach(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor)
    {
        int32_t v0 = port::guestV0();
        if (actor->person == kNoPerson)
            return v0;
        const GuestPtr<MapPerson> person = city->map->people + actor->person;
        if ((person->flags & 4u) != 0)
            return v0;
        leaveBuilding(city, person, v0);
        person->flags |= 0x20u;
        person->follow = kNoPerson;
        weatherFlag(city, person);
        return v0;
    }

    // The same, following the target actor's person.
    int EvActor_Follow(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, GuestPtr<ActorRef> target)
    {
        int32_t v0 = port::guestV0();
        if (actor->person == kNoPerson || target->person == kNoPerson)
            return v0;
        const GuestPtr<MapPerson> person = city->map->people + actor->person;
        if ((person->flags & 4u) != 0)
            return v0;
        leaveBuilding(city, person, v0);
        person->flags |= 0x20u;
        person->follow = target->person;
        weatherFlag(city, person);
        return v0;
    }

    // The actor's person leaves any building and is free again (flags 0x80 and 0x100 cleared).
    void EvActor_Release(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor)
    {
        const GuestPtr<MapPerson> person = city->map->people + actor->person;
        if ((person->flags & 4u) != 0)
            return;
        int32_t v0 = 0;
        leaveBuilding(city, person, v0);
        person->flags &= 0xFE7Fu;
    }

    // Phones the player for the actor (mm_PhoneCall_Post), unless their person stands on no building of theirs
    // (then 0). Actors without a person always call.
    int EvActor_Notify(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor)
    {
        if (actor->person != kNoPerson)
        {
            const GuestPtr<CityMap> map = city->map;
            const GuestPtr<MapPerson> person = map->people + actor->person;
            const int z = sdk::fptosi(ps2f(64.0f) - ps2f(person->z));
            const int x = sdk::fptosi(ps2f(64.0f) + ps2f(person->x));
            const uint32_t tile = (static_cast<uint32_t>(x) + (static_cast<uint32_t>(z) << 7)) & 0xFFFFu;
            if (!Map_FindPersonObjectOnTile(map, person->index, tile))
                return 0;
        }
        return PhoneCall_Post(city->phoneCalls, actor->person, actor->family, actor->member, 0);
    }

    // After an event's claims are removed (mm_Event_RemoveClaims): the buildings in the 80 tile lists whose chosen claim
    // now has id 0 (the removed one) drop it: meter 0, claim slot 0x18 (none), flags 8 and 0x40 cleared. Those already
    // at slot 0x18 are left.
    void Event_DropRemovedClaims(GuestPtr<CityScene> city)
    {
        port::GuestFrame<0x70> frame;
        const GuestPtr<uint32_t> count{frame.ptr().address + 0x6Cu};
        for (int list = 0; list < 80; ++list)
        {
            const GuestPtr<uint16_t> items = Map_GetTileList(city->map, list, count);
            const GuestPtr<uint16_t> end = items + static_cast<int32_t>(*count);
            for (GuestPtr<uint16_t> item = items; item < end; ++item)
            {
                const GuestPtr<MapObject> building = Map_GetRecord(city->map, *item);
                if (building->claimSlot == kNoClaim)
                    continue;
                if (Claim_GetList(typeOf(building)->claimB)[building->claimSlot].id != 0)
                    continue;
                building->claimMeter = 0;
                building->claimSlot = kNoClaim;
                building->flags &= 0xFFF7u;
                building->flags &= 0xFFBFu;
            }
        }
    }

    namespace
    {
        // Whether two blocks overlap, and whether a block holds tile (x, z); x1 and z1 lie one past the block, as
        // mm_Building_GetFootprint gives them.
        bool overlap(const TileRect &a, const TileRect &b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.z0 < b.z1 && b.z0 < a.z1; }
        bool holds(const TileRect &r, int x, int z) { return x >= r.x0 && x < r.x1 && z >= r.z0 && z < r.z1; }
    }

    // The building's footprint widened by `margin` on each side (x1 and z1 past the last tile).
    void Building_GetFootprint(GuestPtr<MapObject> building, int margin, GuestPtr<TileRect> rect)
    {
        const int size = typeOf(building)->size;
        rect->x0 = building->tile & 0x7F;
        rect->z0 = building->tile >> 7;
        rect->x1 = margin + (rect->x0 + size);
        rect->z1 = margin + (rect->z0 + size);
        rect->x0 = rect->x0 - margin;
        rect->z0 = rect->z0 - margin;
    }

    // ---- Check slots: whether the event may start

    int EvBodyguard_Check(GuestPtr<EventData>, GuestPtr<CityScene>) { return 1; }
    int EvFirstPayday_Check(GuestPtr<EventData>, GuestPtr<CityScene> city) { return city->monthCount >= 5 ? 1 : 0; }
    int EvForciblyRetreat_Check(GuestPtr<EventData>, GuestPtr<CityScene> city) { return city->map->population >= 11 ? 1 : 0; }
    int EvNoOffer_Check(GuestPtr<EventData>, GuestPtr<CityScene> city) { return city->applicants->count == 0 ? 1 : 0; }
    int EvUselessMiddleAged_Check(GuestPtr<EventData>, GuestPtr<CityScene>) { return globals::stageState()->money >= 100 ? 1 : 0; }

    int EvPopularity_Check(GuestPtr<EventData>, GuestPtr<CityScene> city)
    {
        if (city->monthCount <= 0)
            return 0;
        return city->map->population >= 11 ? 1 : 0;
    }

    int EvWoman_Check(GuestPtr<EventData>, GuestPtr<CityScene> city)
    {
        if (city->monthCount < 3)
            return 0;
        return city->map->population >= 31 ? 1 : 0;
    }

    int EvRiddle_Check(GuestPtr<EventData>, GuestPtr<CityScene> city)
    {
        if (city->monthCount < 2)
            return 0;
        return city->map->population >= 21 ? 1 : 0;
    }

    // The person heads somewhere that is neither their home nor their target.
    int EvForgetfulGranny_Check(GuestPtr<EventData>, GuestPtr<CityScene>, GuestPtr<MapPerson> person)
    {
        const uint16_t destination = person->destination;
        if (destination == person->home)
            return 0;
        return destination != person->target ? 1 : 0;
    }

    // Some family has moved away (ids 0 to 9999).
    int EvRetreat_Check(GuestPtr<EventData>, GuestPtr<CityScene>)
    {
        for (uint32_t family = 0; family < 10000; ++family)
            if (Family_HasLeft(family) != 0)
                return 1;
        return 0;
    }

    // 150 objects of type 10 on the map.
    int EvBugDamage_Check(GuestPtr<EventData>, GuestPtr<CityScene> city)
    {
        const GuestPtr<MapObject> first = Map_GetRecord(city->map, 0);
        int count = 0;
        for (GuestPtr<MapObject> object = first; object != first + kObjectSlots; ++object)
        {
            if (object->type != 10)
                continue;
            if (++count >= 150)
                return 1;
        }
        return 0;
    }

    namespace
    {
        // mm_Map_FindPersonEntry with the halfwords as the original loads them (zero-extended).
        GuestPtr<MapPerson> findPerson(GuestPtr<CityScene> city, uint16_t family, uint16_t member)
        {
            return port::callGuest<GuestPtr<MapPerson>>(addr::Map_FindPersonEntry, city->map, static_cast<int>(family), static_cast<int>(member));
        }

        // An actor for a building's family: the building, its family and the member, then the member's person record.
        void setActorFromHome(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, GuestPtr<MapObject> home, uint16_t member)
        {
            actor->object = home->id;
            actor->family = home->family;
            actor->member = member;
            actor->person = findPerson(city, actor->family, actor->member)->index;
        }
    }

    // A cave man (type 0x113 in tile list 0x15, built (flag 0x20), no claim met or meter maxed, claim meter level 0)
    // whose family's first member has a person record: actor 6 becomes that person at the cave, actor 5 the third member of
    // the first University family, actor 7 the first Caveman's Home family (the other actors none), the cave is marked.
    int EvCaveMan_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        port::GuestFrame<0x70> frame;
        const GuestPtr<uint32_t> count{frame.ptr().address + 0x6Cu};
        const GuestPtr<uint16_t> items = Map_GetTileList(city->map, 0x15, count);
        const GuestPtr<uint16_t> end = items + static_cast<int32_t>(*count);
        for (GuestPtr<uint16_t> item = items; item < end; ++item)
        {
            const GuestPtr<MapObject> cave = Map_GetRecord(city->map, *item);
            if (cave->type != 0x113 || (cave->flags & 0x20u) == 0 || (cave->flags & 0x240u) != 0 || ClaimMeter_Level(cave->claimMeter) != 0)
                continue;
            const GuestPtr<MapPerson> person = port::callGuest<GuestPtr<MapPerson>>(addr::Map_FindPersonEntry, city->map,
                                                                                        static_cast<int>(cave->family), 0);
            if (!person)
                continue;
            for (int i = 0; i < 8; ++i)
            {
                actorAt(data, i)->person = kNoPerson;
                actorAt(data, i)->object = kNoObject;
            }
            actorAt(data, 6)->person = person->index;
            actorAt(data, 6)->object = cave->id;
            actorAt(data, 6)->family = cave->family;
            actorAt(data, 6)->member = 0;
            cave->flags |= 0x200u;
            actorAt(data, 5)->family = globals::firstFamily(globals::types::kUniversity);
            actorAt(data, 5)->member = 2;
            actorAt(data, 7)->family = globals::firstFamily(globals::types::kCavemansHome);
            return 1;
        }
        return 0;
    }

    // A resident (character types 7 to 29) inside a shop (category 6 to 10) that is neither their home nor their
    // target and has no maxed meter, standing outside the shop's footprint widened by 5: the shop becomes actor 1
    // (its family's first member).
    int EvShoppingTogether_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        struct Locals
        {
            uint8_t gap_00[0x50];
            TileRect shop;             // +0x50 the shop's footprint widened by 5
        };
        static_assert(offsetof(Locals, shop) == 0x50 && sizeof(Locals) == 0x60);
        port::GuestFrameOf<Locals> frame;
        if (person->charType < 7 || person->charType >= 30 || person->inside == kNoObject)
            return 0;
        const GuestPtr<MapObject> shop = Map_GetRecord(city->map, person->inside);
        const uint8_t category = typeOf(shop)->category;
        if (category < 6 || category >= 11 || shop->id == person->home || shop->id == person->target || (shop->flags & 0x40u) != 0)
            return 0;
        Building_GetFootprint(shop, 5, frame.at(&Locals::shop));
        const int z = sdk::fptosi(ps2f(64.0f) - ps2f(person->z));
        const int x = sdk::fptosi(ps2f(64.0f) + ps2f(person->x));
        if (holds(frame->shop, x, z))
            return 0;
        actorAt(data, 1)->object = shop->id;
        actorAt(data, 1)->family = shop->family;
        actorAt(data, 1)->member = 0;
        findPerson(city, actorAt(data, 1)->family, actorAt(data, 1)->member);
        return 1;
    }

    // The thief strikes when the first families of types 0xB9 (Studio Appartment) and 0x105 (Police Station) live in the
    // city with a home, and there is a
    // building to rob (mm_EvThief_FindBuilding, category 5, else 1): actor 0 the first family, actors 1 and 2 the
    // second family's first two members, actor 3 the robbed building's family; the buildings are marked.
    int EvThief_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const uint16_t familyA = globals::firstFamily(globals::types::kStudioAppartmentB9);
        if (Family_InCity(familyA) == 0)
            return 0;
        const GuestPtr<MapObject> homeA = Map_FindFamilyHome(city->map, familyA);
        if (!homeA)
            return 0;
        const uint16_t familyB = globals::firstFamily(globals::types::kPoliceStation105);
        if (Family_InCity(familyB) == 0)
            return 0;
        const GuestPtr<MapObject> homeB = Map_FindFamilyHome(city->map, familyB);
        if (!homeB)
            return 0;
        const GuestPtr<TileRect> rect{globals::kWholeMapRect};
        GuestPtr<MapObject> target = EvThief_FindBuilding(city, 5, rect);
        if (!target)
            target = EvThief_FindBuilding(city, 1, rect);
        if (!target)
            return 0;
        setActorFromHome(city, actorAt(data, 0), homeA, 0);
        homeA->flags |= 0x200u;
        setActorFromHome(city, actorAt(data, 1), homeB, 0);
        homeB->flags |= 0x200u;
        setActorFromHome(city, actorAt(data, 2), homeB, 1);
        setActorFromHome(city, actorAt(data, 3), target, 0);
        target->flags |= 0x200u;
        return 1;
    }

    // The person's home is their own shop, of category 7 or 9 (not type 0x100), and unless it is newly built (flag 0x100:
    // then any month), (family + member) % 12 is this month.
    int EvAdvertise_Check(GuestPtr<EventData>, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        const GuestPtr<MapObject> home = Map_GetRecord(city->map, person->home);
        if ((home->flags & 0x100u) == 0 && city->monthCount % 12 != (person->family + person->member) % 12)
            return 0;
        const uint8_t category = GuestPtr<BuildingType>{globals::buildingTypeAddress(home->type)}->category;
        if (category != 9 && category != 7)
            return 0;
        return home->type != 0x100 ? 1 : 0;
    }

    // ---- Start slots

    // The start slots below leave a result in v0 that the talk script action reads (Event_CallHandlerSlot passes
    // it on): the record they marked last, or the register as found.
    int64_t EvCaveMan_Start(GuestPtr<EventData>, GuestPtr<CityScene>) { return port::guestV0Wide(); }

    // The message comes from the boss (the first member of the Boss' Home family), with no person or building.
    int64_t EvMsg_Start(GuestPtr<EventData> data, GuestPtr<CityScene>)
    {
        const int64_t found = port::guestV0Wide();
        data->actor.person = kNoPerson;
        data->actor.object = kNoObject;
        data->actor.family = globals::firstFamily(globals::types::kBossHome);
        data->actor.member = 0;
        return found;
    }

    GuestPtr<MapObject> EvMissingChildren_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city) { return markBuilding(city, data->actor.object); }
    GuestPtr<MapObject> EvAdvertise_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city) { return markBuilding(city, data->actor.object); }

    // Marks the building and remembers the month it started in.
    GuestPtr<MapObject> EvUselessMiddleAged_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<MapObject> record = markBuilding(city, data->actor.object);
        data.as<EvMiddleAgedData>()->month = city->monthCount;
        return record;
    }

    // Marks the building, detaches the actor and keeps the player's distance from the building (distance).
    int EvEscort_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        markBuilding(city, data->actor.object);
        const GuestPtr<ActorRef> actor = GuestPtr<ActorRef>::of(data->actor);
        EvActor_Detach(city, actor);
        const int distance = Player_DistanceToBuilding(city, actor);
        data.as<EvEscortData>()->distance = distance;
        return distance;
    }

    // Marks both actors' buildings and sets the countdown to 180.
    GuestPtr<MapObject> EvShoppingTogether_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        markBuilding(city, actorAt(data, 0)->object);
        const GuestPtr<MapObject> second = markBuilding(city, actorAt(data, 1)->object);
        data.as<EvShoppingTogetherData>()->countdown = 180;
        return second;
    }

    // Marks both actors' buildings; the delivery state (state) starts at 2.
    GuestPtr<MapObject> EvDeliveryMan_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvDeliveryData> delivery = data.as<EvDeliveryData>();
        markBuilding(city, actorAt(data, 0)->object);
        const GuestPtr<MapObject> second = markBuilding(city, actorAt(data, 1)->object);
        delivery->state = 2;
        return second;
    }

    int64_t EvShopping_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city) { return markActorBuildings(city, data, 0, 5); }

    // The noodle shop event lists its six buildings in actors 4 to 9.
    int64_t EvNoodles_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city) { return markActorBuildings(city, data, 4, 6); }

    // Six actors without person or building: the Zen Monk's and the Shadow Monk's families, then the first four Crazy Cult
    // families (members unchanged).
    int64_t EvCrazyCult_Start(GuestPtr<EventData> data, GuestPtr<CityScene>)
    {
        const int64_t found = port::guestV0Wide();
        actorAt(data, 0)->family = globals::firstFamily(globals::types::kZenMonksHome);
        actorAt(data, 1)->family = globals::firstFamily(globals::types::kShadowMonksHome);
        for (int i = 0; i < 4; ++i)
            actorAt(data, 2 + i)->family = static_cast<uint16_t>(globals::firstFamily(globals::types::kCrazyCult) + i);
        for (int i = 0; i < 6; ++i)
        {
            actorAt(data, i)->person = kNoPerson;
            actorAt(data, i)->object = kNoObject;
        }
        return found;
    }

    // The adoption comes from the boss (the first member of the Boss' Home family); the applicants (families 0 to 9999 with an
    // application) are listed as words in applicants, then 24 bytes from 0x1B37D0 are copied to children (through the
    // original stack, then by memcpy, whose result, the destination, is what the start slot returns).
    uint32_t EvAdoption_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        struct Locals
        {
            uint8_t gap_00[0x50];
            AdoptionChild children[6]; // +0x50 the children, copied in from their table
            uint8_t gap_68[8];
        };
        static_assert(offsetof(Locals, children) == 0x50 && sizeof(Locals) == 0x70);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<EvAdoptionData> adoption = data.as<EvAdoptionData>();
        data->actor.person = kNoPerson;
        data->actor.object = kNoObject;
        data->actor.family = globals::firstFamily(globals::types::kBossHome);
        data->actor.member = 0;
        GuestPtr<int32_t> out = GuestPtr<int32_t>::of(adoption->applicants[0]); // runs on past the ten words like the original
        for (uint32_t family = 0; family < 10000; ++family)
        {
            if (Applicants_Find(city->applicants, family) == 0)
                continue;
            *out = static_cast<int32_t>(family);
            ++out;
        }
        for (int i = 0; i < 6; ++i)
            frame->children[i] = GuestPtr<AdoptionChild>{globals::kAdoptionStart}[i];
        return sdk::memcpy(port::guest(adoption->children), frame.at(&Locals::children).address, 0x18);
    }

    // ---- Update slots

    // Phase 1 (var): from 9:00 on a later month than it started, the actor is told (mm_EvActor_Notify) and phase 2
    // starts with a 60-minute timer; phase 2 goes back to 1 when the timer runs out.
    void EvUselessMiddleAged_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvMiddleAgedData> man = data.as<EvMiddleAgedData>();
        if (data->var == 2)
        {
            if (--man->minutes <= 0)
                --data->var;
            return;
        }
        if (data->var != 1 || city->monthCount == man->month || city->clock < 32400)
            return;
        if (EvActor_Notify(city, GuestPtr<ActorRef>::of(data->actor)) == 0)
            return;
        man->minutes = 60;
        ++data->var;
    }

    namespace
    {
        // The message events' phases (var): 1 waits until the actor can be told (mm_EvActor_Notify), then a phone
        // call is up for 60 minutes (callMinutes), plus those left until 6:00 when it comes before then, or 360 more
        // from 23:00; phase 2 counts that down and returns true when it runs out (the call taken down).
        bool messagePhase(GuestPtr<EventData> data, GuestPtr<CityScene> city)
        {
            const GuestPtr<EvMessageData> message = data.as<EvMessageData>();
            if (data->var == 2)
            {
                if (--message->callMinutes > 0)
                    return false;
                message->callPosted = 0;
                return true;
            }
            if (data->var != 1 || EvActor_Notify(city, GuestPtr<ActorRef>::of(data->actor)) == 0)
                return false;
            ++data->var;
            message->callPosted = 1;
            message->callMinutes = 60;
            const int32_t clock = city->clock;
            if (clock < 21600)
                message->callMinutes += 360 - clock / 60;
            else if (clock >= 82801)
                message->callMinutes += 360;
            return false;
        }
    }

    // The message goes back to waiting when its call runs out.
    void EvMsg_UpdateFinal(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        if (messagePhase(data, city))
            --data->var;
    }

    // The event ends (finished) when its call runs out.
    void EvNoRetreat_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        if (messagePhase(data, city))
            Event_Finish(city->events, city, 1);
    }

    // The message is repeated: back to waiting for the first four calls, finished after the fifth (callCount).
    void EvMsg_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        if (!messagePhase(data, city))
            return;
        if (++data.as<EvMessageData>()->callCount < 5)
            --data->var;
        else
            Event_Finish(city->events, city, 1);
    }

    // Payday: phases 1 and 2 as the messages (four repeats, then phase 3); phase 3 pays. After a good month (the
    // payday score plus twice the families that moved in less four times those that left, 100 or more) the salary
    // rises by 1250; the money gains the salary (both saturate); the counts are reset and the event finishes.
    void EvPayday_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        if (data->var == 3)
        {
            data.as<EvMessageData>()->callPosted = 0;
            const GuestPtr<StageState> stage = globals::stageState();
            const uint32_t score = static_cast<uint32_t>(city->paydayScore) + 2u * static_cast<uint32_t>(globals::familiesMovedInEvent.get()) -
                                   4u * static_cast<uint32_t>(globals::familiesLeftEvent.get());
            if (static_cast<int32_t>(score) >= 100)
            {
                stage->salary = static_cast<int32_t>(static_cast<uint32_t>(stage->salary) + 1250u);
                if (stage->salary < 0)
                    stage->salary = 0x7FFFFFFF;
            }
            const int32_t salary = stage->salary;
            stage->money = static_cast<int32_t>(static_cast<uint32_t>(stage->money) + static_cast<uint32_t>(salary));
            if (stage->money < 0)
                stage->money = salary > 0 ? 0x7FFFFFFF : 0;
            city->paydayScore = 0;
            globals::familiesLeftEvent.set(0);
            globals::familiesMovedInEvent.set(0);
            Event_Finish(city->events, city, 1);
            return;
        }
        if (!messagePhase(data, city))
            return;
        if (++data.as<EvMessageData>()->callCount < 5)
            --data->var;
        else
            ++data->var;
    }

    // The advertisement's phases (var): 1 waits for the event type's time (finishAt), 2 and 4 wait until the
    // actor can be told, 3 and 5 count 60 minutes back down a phase, 6 ends the event after 240 minutes.
    void EvAdvertise_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvAdvertiseData> ad = data.as<EvAdvertiseData>();
        const uint8_t phase = data->var;
        switch (phase)
        {
        case 1:
            if (city->clock / 60 >= globals::eventTypeCopy(data->kind)->finishAt)
                data->var = static_cast<uint8_t>(phase + 1);
            break;
        case 2:
        case 4:
            if (EvActor_Notify(city, GuestPtr<ActorRef>::of(data->actor)) == 0)
                break;
            ad->tellMinutes = 60;
            ++data->var;
            break;
        case 3:
        case 5:
            if (--ad->tellMinutes <= 0)
                --data->var;
            break;
        case 6:
        {
            const int32_t minutes = ad->endMinutes + 1;
            ad->endMinutes = minutes;
            if (minutes >= 240)
                Event_Finish(city->events, city, 0);
            break;
        }
        default:
            break;
        }
    }

    // ---- End slots

    // Residents lose person flag 0x400 and the six buildings are released.
    void EvCrazyCult_End(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<MapPerson> people = city->map->people;
        for (GuestPtr<MapPerson> person = people; person != people + kPersonSlots; ++person)
        {
            if (person->charType == 0)
                continue;
            const GuestPtr<CharClass> cls = globals::charClass(person->charType);
            if ((cls->flags & 1u) == 0)
                person->flags &= 0xFBFFu;
        }
        unmarkActorBuildings(city, data, 6);
    }

    void EvUselessMiddleAged_End(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        releaseWantHolds(data, city, 1);
        unmarkBuilding(city, data->actor.object);
    }

    void EvAdvertise_End(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        releaseWantHolds(data, city, 1);
        unmarkBuilding(city, data->actor.object);
    }

    void EvEscort_End(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        releaseWantHolds(data, city, 1);
        EvActor_StopFollowing(city, GuestPtr<ActorRef>::of(data->actor));
        unmarkBuilding(city, data->actor.object);
    }

    void EvShoppingTogether_End(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        releaseWantHolds(data, city, 1);
        EvActor_StopFollowing(city, actorAt(data, 0));
        unmarkBuilding(city, actorAt(data, 0)->object);
        unmarkBuilding(city, actorAt(data, 1)->object);
    }

    void EvDeliveryMan_End(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        releaseWantHolds(data, city, 2);
        unmarkBuilding(city, actorAt(data, 0)->object);
        unmarkBuilding(city, actorAt(data, 1)->object);
    }

    void EvCaveMan_End(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        releaseWantHolds(data, city, 4);
        unmarkActorBuildings(city, data, 8);
    }

    void EvShopping_End(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        releaseWantHolds(data, city, 1);
        unmarkActorBuildings(city, data, 5);
    }

    void EvAlien_End(GuestPtr<EventData> data, GuestPtr<CityScene> city) { unmarkActorBuildings(city, data, 6); }

    // The lost child (actor 0) stops following unless their person has flag 0x200; the parent (actor 1) is told to
    // stop when their person follows nobody; the parent's building is released.
    void EvMissingChildren_End(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        releaseWantHolds(data, city, 1);
        if ((city->map->people[actorAt(data, 0)->person].flags & 0x200u) == 0)
            EvActor_StopFollowing(city, actorAt(data, 0));
        if (city->map->people[actorAt(data, 1)->person].follow == kNoPerson)
            EvActor_StopFollowing(city, actorAt(data, 1));
        unmarkBuilding(city, actorAt(data, 1)->object);
    }

    // Actors 2 to 9 with a building: those with a person stop following (not while var is 0x61), and the
    // buildings of actors 4 to 9 are released.
    void EvNoodles_End(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        for (int i = 2; i < 10; ++i)
        {
            const GuestPtr<ActorRef> actor = actorAt(data, i);
            if (actor->object == kNoObject)
                continue;
            const GuestPtr<MapObject> building = Map_GetRecord(city->map, actor->object);
            if (actor->person != kNoPerson && data->var != 0x61)
                EvActor_StopFollowing(city, actor);
            if (i >= 4)
                building->flags &= 0xFDFFu;
        }
    }

    // The thief's four buildings are released; the first two are left with their claim meter maxed (flag 0x40,
    // claim timer 1).
    void EvThief_End(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        for (int i = 0; i < 4; ++i)
        {
            const uint16_t object = actorAt(data, i)->object;
            if (object == kNoObject)
                continue;
            const GuestPtr<MapObject> building = Map_GetRecord(city->map, object);
            building->flags &= 0xFDFFu;
            if (i < 2)
            {
                building->claimTimer = 1;
                building->flags |= 0x40u;
            }
        }
    }

    // Withdraws the message's phone call if it posted one.
    void EvMsg_End(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        if (data.as<EvMessageData>()->callPosted == 0)
            return;
        PhoneCall_Remove(city->phoneCalls, city, data->actor.family, data->actor.member);
    }

    // ---- Text slots: texts for the talk

    // The claim variant text of the chosen applicant's claim (the claim bytes of children, picked by pick); 0 for other
    // indexes.
    int EvAdoption_Text(GuestPtr<EventData> data, GuestPtr<CityScene>, int index)
    {
        const GuestPtr<EvAdoptionData> adoption = data.as<EvAdoptionData>();
        if (index != 0)
            return 0;
        const uint32_t pick = static_cast<uint32_t>(adoption->pick);
        return Claim_PickVariant((GuestPtr<AdoptionChild>::of(adoption->children[0]) + static_cast<int32_t>(pick))->claim);
    }

    // Texts 0 to 3: the goods of the shop (shop); 4: the texts table itself; above: the bill as money.
    GuestPtr<char> EvShopping_Text(GuestPtr<EventData> data, GuestPtr<CityScene>, int index)
    {
        const GuestPtr<EvShoppingData> shopping = data.as<EvShoppingData>();
        if (index < 4)
        {
            const GuestPtr<ShoppingList> list = globals::shoppingList(shopping->shop);
            return GuestPtr<char>{globals::goodsSectionC.get() + list->goods[index]};
        }
        if (index == 4)
            return GuestPtr<char>{globals::shoppingTexts.get()};
        Text_FormatMoney(GuestPtr<char>{globals::kEventMoneyTextB}, static_cast<int>(static_cast<uint32_t>(shopping->bill)), 7, 1);
        return GuestPtr<char>{globals::kEventMoneyTextB};
    }

    // Text 0: the catalogue item's name; 1: its price as money; 0 for others.
    GuestPtr<char> EvDeliveryMan_Text(GuestPtr<EventData> data, GuestPtr<CityScene>, int index)
    {
        const GuestPtr<EvDeliveryData> delivery = data.as<EvDeliveryData>();
        const GuestPtr<DeliveryItem> item = globals::deliveryItem(delivery->entry, delivery->month);
        if (index == 0)
            return item.as<char>(); // the name, at +0
        if (index != 1)
            return GuestPtr<char>{0};
        Text_FormatMoney(GuestPtr<char>{globals::kEventMoneyTextA}, item->price, 15, 1);
        return GuestPtr<char>{globals::kEventMoneyTextA};
    }

    // ---- Answer slots: values for the talk (all 0 unless the argument is 0)

    int EvShoppingTogether_Answer(GuestPtr<EventData> data, GuestPtr<CityScene>, GuestPtr<ActorRef>, int mode)
    {
        return mode == 0 ? data.as<EvShoppingTogetherData>()->outcome : 0;
    }

    // Whether the party (guests) has a quarter of the population.
    int EvFarewellParty_Answer(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef>, int mode)
    {
        const GuestPtr<EvPartyData> party = data.as<EvPartyData>();
        if (mode != 0)
            return 0;
        return party->guests >= city->map->population / 4 ? 1 : 0;
    }

    // How near the player is to the actor's building, where the escort ends: 4 within 2 tiles, 3 within 6, else 1
    // nearer than last time, 2 farther, 0 the same; the distance is kept (distance).
    int EvBodyguard_Answer(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef>, int mode)
    {
        if (mode != 0)
            return 0;
        const GuestPtr<EvEscortData> escort = data.as<EvEscortData>();
        const int distance = Player_DistanceToBuilding(city, GuestPtr<ActorRef>::of(data->actor));
        int state;
        if (distance < 2)
            state = 4;
        else if (distance < 6)
            state = 3;
        else if (distance < escort->distance)
            state = 1;
        else if (escort->distance < distance)
            state = 2;
        else
            state = 0;
        escort->distance = distance;
        return state;
    }

    // Argument 0: the thief struck at the actor's building (its claim meter maxed, the fourth actor cleared, the
    // count (strikes) raised) and the next month (nextMonth) noted; 0. Argument 1: by the count, 0 none, 1 up to 3, 2 at 4,
    // 3 at 5, 4 from 6.
    int EvThief_Answer(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        if (mode == 1)
        {
            const int32_t count = data.as<EvThiefData>()->strikes;
            if (count >= 6)
                return 4;
            if (count == 5)
                return 3;
            if (count == 4)
                return 2;
            return count > 0 ? 1 : 0;
        }
        if (mode != 0)
            return 0;
        if (actor->object != kNoObject)
        {
            const GuestPtr<MapObject> building = Map_GetRecord(city->map, actor->object);
            building->flags &= 0xFDFFu;
            building->claimTimer = 1;
            building->flags |= 0x40u;
            actorAt(data, 3)->person = kNoPerson;
            actorAt(data, 3)->object = kNoObject;
            data.as<EvThiefData>()->strikes = data.as<EvThiefData>()->strikes + 1;
        }
        data.as<EvThiefData>()->nextMonth = city->monthCount + 1;
        return 0;
    }

    // Argument 0: the outcome. Argument 1: whether the actor's building and the event's (widened by 5) overlap.
    int EvMissingChildren_Answer(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        if (mode == 0)
            return data.as<EvMissingChildrenData>()->outcome;
        if (mode != 1)
            return 0;
        struct Locals
        {
            uint8_t gap_00[0x40];
            TileRect event;            // +0x40 the event's building widened by 5
            TileRect actor;            // +0x50 the actor's
        };
        static_assert(offsetof(Locals, event) == 0x40 && offsetof(Locals, actor) == 0x50 && sizeof(Locals) == 0x60);
        port::GuestFrameOf<Locals> frame;
        Building_GetFootprint(Map_GetRecord(city->map, data->actor.object), 5, frame.at(&Locals::event));
        Building_GetFootprint(Map_GetRecord(city->map, actor->object), 0, frame.at(&Locals::actor));
        return overlap(frame->event, frame->actor) ? 1 : 0;
    }

    // ---- the party popup

    void PartyPopup_Draw(int popup, int pass)
    {
        if (pass == 2)
            Frame_Draw(GuestPtr<Frame>{port::load<uint32_t>(static_cast<uint32_t>(popup) + 4u)}); // the popup's frame
    }

    // Shows notice 1 and closes on Circle (0x40).
    void PartyPopup_Update(int, GuestPtr<CityScene> city)
    {
        Notice_Show(city->notice, 1);
        if ((Pad_Pressed(4) & 0x40u) != 0)
            CityScene_PopMode(city);
    }

    PORT_FN(Event_CountActive);
    PORT_FN(EventMgr_FindType);
    PORT_FN(EventMgr_RequeueFirst);
    PORT_FN(EventQueue_RemoveAt);
    PORT_FN(Event_AddClaims);
    PORT_FN(Event_RemoveClaims);
    PORT_FN(EvActor_StopFollowing);
    PORT_FN(EvActor_Detach);
    PORT_FN(EvActor_Follow);
    PORT_FN(EvActor_Release);
    PORT_FN(EvActor_Notify);
    PORT_FN(Event_DropRemovedClaims);
    PORT_FN(EvMissingChildren_End);
    PORT_FN(EvNoodles_End);
    PORT_FN(EvBodyguard_Check);
    PORT_FN(EvFirstPayday_Check);
    PORT_FN(EvForciblyRetreat_Check);
    PORT_FN(EvNoOffer_Check);
    PORT_FN(EvUselessMiddleAged_Check);
    PORT_FN(EvPopularity_Check);
    PORT_FN(EvWoman_Check);
    PORT_FN(EvRiddle_Check);
    PORT_FN(EvForgetfulGranny_Check);
    PORT_FN(EvRetreat_Check);
    PORT_FN(EvBugDamage_Check);
    PORT_FN(EvAdvertise_Check);
    PORT_FN(EvCaveMan_Start);
    PORT_FN(EvMsg_Start);
    PORT_FN(EvMissingChildren_Start);
    PORT_FN(EvAdvertise_Start);
    PORT_FN(EvUselessMiddleAged_Start);
    PORT_FN(EvEscort_Start);
    PORT_FN(EvShoppingTogether_Start);
    PORT_FN(EvDeliveryMan_Start);
    PORT_FN(EvShopping_Start);
    PORT_FN(EvNoodles_Start);
    PORT_FN(EvCrazyCult_Start);
    PORT_FN(EvAdoption_Start);
    PORT_FN(EvUselessMiddleAged_Update);
    PORT_FN(EvUselessMiddleAged_End);
    PORT_FN(EvAdvertise_End);
    PORT_FN(EvEscort_End);
    PORT_FN(EvShoppingTogether_End);
    PORT_FN(EvDeliveryMan_End);
    PORT_FN(EvCaveMan_End);
    PORT_FN(EvShopping_End);
    PORT_FN(EvAlien_End);
    PORT_FN(EvThief_End);
    PORT_FN(EvMsg_End);
    PORT_FN(EvAdoption_Text);
    PORT_FN(EvShopping_Text);
    PORT_FN(EvDeliveryMan_Text);
    PORT_FN(EvShoppingTogether_Answer);
    PORT_FN(EvFarewellParty_Answer);
    PORT_FN(EvBodyguard_Answer);
    PORT_FN(Building_GetFootprint);
    PORT_FN(EvCaveMan_Check);
    PORT_FN(EvShoppingTogether_Check);
    PORT_FN(EvThief_Check);
    PORT_FN(EvPayday_Update);
    PORT_FN(EvMsg_UpdateFinal);
    PORT_FN(EvNoRetreat_Update);
    PORT_FN(EvMsg_Update);
    PORT_FN(EvAdvertise_Update);
    PORT_FN(EvCrazyCult_End);
    PORT_FN(EvThief_Answer);
    PORT_FN(EvMissingChildren_Answer);
    PORT_FN(PartyPopup_Draw);
    PORT_FN(PartyPopup_Update);

    // ---- building finders, more talk values and phases (batch 38)

    namespace
    {
        // Whether the building's footprint overlaps the rectangle (x0, z0, x1, z1; the ends excluded).
        bool overlapsRect(GuestPtr<MapObject> building, GuestPtr<TileRect> rect)
        {
            const int size = typeOf(building)->size;
            const int x = building->tile & 0x7F, z = building->tile >> 7;
            return rect->x0 < x + size && x < rect->x1 && rect->z0 < z + size && z < rect->z1;
        }

        // A building that can take part: its family lives in the city and it has no claim met and no maxed meter.
        bool available(GuestPtr<MapObject> building) { return Family_InCity(building->family) != 0 && (building->flags & 0x240u) == 0; }
    }

    // The first available building of `type` in tile list `list` that overlaps the rectangle.
    GuestPtr<MapObject> EvAlien_FindBuilding(GuestPtr<CityScene> city, int list, uint32_t type, GuestPtr<TileRect> rect)
    {
        port::GuestFrame<0x80> frame;
        const GuestPtr<uint32_t> count{frame.ptr().address + 0x7Cu};
        const GuestPtr<uint16_t> items = Map_GetTileList(city->map, list, count);
        const GuestPtr<uint16_t> end = items + static_cast<int32_t>(*count);
        for (GuestPtr<uint16_t> item = items; item < end; ++item)
        {
            const GuestPtr<MapObject> building = Map_GetRecord(city->map, *item);
            if (building->type == type && available(building) && overlapsRect(building, rect))
                return building;
        }
        return GuestPtr<MapObject>{0};
    }

    // The first available building of `category` in any of the 80 tile lists that overlaps the rectangle.
    GuestPtr<MapObject> EvThief_FindBuilding(GuestPtr<CityScene> city, uint32_t category, GuestPtr<TileRect> rect)
    {
        port::GuestFrame<0x90> frame;
        const GuestPtr<uint32_t> count{frame.ptr().address + 0x8Cu};
        for (int list = 0; list < 80; ++list)
        {
            const GuestPtr<uint16_t> items = Map_GetTileList(city->map, list, count);
            const GuestPtr<uint16_t> end = items + static_cast<int32_t>(*count);
            for (GuestPtr<uint16_t> item = items; item < end; ++item)
            {
                const GuestPtr<MapObject> building = Map_GetRecord(city->map, *item);
                if (typeOf(building)->category == category && available(building) && overlapsRect(building, rect))
                    return building;
            }
        }
        return GuestPtr<MapObject>{0};
    }

    // The building the alien lands on at (x, z): of the places in globals::kAlienSites (ended by list 0), the
    // first building whose family lives in the city, without a maxed meter, whose footprint widened by 1 holds the
    // point.
    GuestPtr<MapObject> EvAlien_PickTarget(GuestPtr<CityScene> city, int x, int z)
    {
        struct Locals
        {
            uint8_t gap_00[0xA0];
            TileRect footprint;        // +0xA0 a building's footprint widened by 1
            uint8_t gap_B0[0xC];
            uint32_t count;            // +0xBC the tile list's length
        };
        static_assert(offsetof(Locals, footprint) == 0xA0 && offsetof(Locals, count) == 0xBC && sizeof(Locals) == 0xC0);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<uint32_t> count = frame.at(&Locals::count);
        for (GuestPtr<AlienSite> site = globals::kAlienSites; site->list != 0; ++site)
        {
            const GuestPtr<uint16_t> items = Map_GetTileList(city->map, site->list, count);
            const GuestPtr<uint16_t> end = items + static_cast<int32_t>(*count);
            for (GuestPtr<uint16_t> item = items; item < end; ++item)
            {
                const GuestPtr<MapObject> building = Map_GetRecord(city->map, *item);
                if (building->type != site->type || Family_InCity(building->family) == 0 || (building->flags & 0x40u) != 0)
                    continue;
                Building_GetFootprint(building, 1, frame.at(&Locals::footprint));
                if (holds(frame->footprint, x, z))
                    return building;
            }
        }
        return GuestPtr<MapObject>{0};
    }

    // Argument 0: whether the actor's home (widened by 10) and the event's home overlap, or else whether the actor's
    // person and the event's head for the same building.
    int EvRelationship_Answer(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        if (mode != 0)
            return 0;
        struct Locals
        {
            uint8_t gap_00[0x70];
            TileRect actorHome;        // +0x70 the actor's home widened by 10
            TileRect eventHome;        // +0x80 the event's
        };
        static_assert(offsetof(Locals, actorHome) == 0x70 && offsetof(Locals, eventHome) == 0x80 && sizeof(Locals) == 0x90);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<MapObject> homeA = Map_FindFamilyHome(city->map, actor->family);
        if (homeA)
        {
            const GuestPtr<MapObject> homeB = Map_FindFamilyHome(city->map, data->actor.family);
            if (homeB)
            {
                Building_GetFootprint(homeA, 10, frame.at(&Locals::actorHome));
                Building_GetFootprint(homeB, 0, frame.at(&Locals::eventHome));
                if (overlap(frame->actorHome, frame->eventHome))
                    return 1;
            }
        }
        const GuestPtr<MapPerson> first = findPerson(city, actor->family, actor->member);
        if (!first || first->target == kNoObject)
            return 0;
        const GuestPtr<MapPerson> second = findPerson(city, data->actor.family, data->actor.member);
        if (!second)
            return 0;
        return second->target == first->target ? 1 : 0;
    }

    // Whether there is a shop (type flag 0x40) with a walkable entrance, other than the person's home, that the
    // person is not within 5 tiles of (the shop at the entrance tile, its footprint widened by 5).
    int EvActor_FindShop(GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        port::GuestFrame<0xC0> frame;
        const GuestPtr<uint32_t> count{frame.ptr().address + 0xBCu};
        for (int list = 0; list < 80; ++list)
        {
            const GuestPtr<uint16_t> items = Map_GetTileList(city->map, list, count);
            const GuestPtr<uint16_t> end = items + static_cast<int32_t>(*count);
            for (GuestPtr<uint16_t> item = items; item < end; ++item)
            {
                const GuestPtr<MapObject> shop = Map_GetRecord(city->map, *item);
                if ((typeOf(shop)->flags & 0x40u) == 0 || Map_TileIndexWalkable(city->map, shop->entranceTile) == 0 || shop->id == person->home)
                    continue;
                const GuestPtr<MapObject> at = Map_GetTileFirstObject(city->map, shop->entranceTile);
                const int size = typeOf(at)->size;
                const int x0 = at->tile & 0x7F, z0 = at->tile >> 7;
                const int z = sdk::fptosi(ps2f(64.0f) - ps2f(person->z));
                const int x = sdk::fptosi(ps2f(64.0f) + ps2f(person->x));
                if (!(x >= x0 - 5 && x < x0 + size + 5 && z >= z0 - 5 && z < z0 + size + 5))
                    return 1;
            }
        }
        return 0;
    }

    namespace
    {
        // The crazy cult's waiting phases (mm_EvCrazyCult_TellFirstMember, TellSecondMember and TellLeader): wait for
        // the month (waitMonth) and the time (waitTime), tell the actor (then 60 minutes, and the time six hours later),
        // or count those minutes back down a phase (the time six hours back).
        void cultWait(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor)
        {
            const GuestPtr<EvCultData> cult = data.as<EvCultData>();
            if (city->monthCount < cult->waitMonth || city->clock < cult->waitTime)
                return;
            if (EvActor_Notify(city, actor) == 0)
                return;
            ++data->var;
            cult->tellMinutes = 60;
            cult->waitTime = cult->waitTime + 21600;
        }

        void cultCountDown(GuestPtr<EventData> data)
        {
            const GuestPtr<EvCultData> cult = data.as<EvCultData>();
            const int32_t minutes = cult->tellMinutes - 1;
            cult->tellMinutes = minutes;
            if (minutes > 0)
                return;
            cult->waitTime = cult->waitTime - 21600;
            --data->var;
        }
    }

    // Phases 4 to 9: actor 2 waits and is told (4 to 7), then actor 3's family home is found (8) and its person (9,
    // which sets the next wait to the next month at 7:00).
    void EvCrazyCult_TellFirstMember(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvCultData> cult = data.as<EvCultData>();
        switch (data->var)
        {
        case 4:
        case 6:
            cultWait(data, city, actorAt(data, 2));
            break;
        case 5:
        case 7:
            cultCountDown(data);
            break;
        case 8:
            if (const GuestPtr<MapObject> home = Map_FindFamilyHome(city->map, cult->actors[3].family))
            {
                cult->actors[3].object = home->id;
                home->flags |= 0x200u;
                data->var = 9;
            }
            break;
        case 9:
            if (const GuestPtr<MapPerson> person = findPerson(city, cult->actors[3].family, cult->actors[3].member))
            {
                cult->actors[3].person = person->index;
                data->var = 10;
                cult->waitMonth = city->monthCount + 1;
                cult->waitTime = 25200;
            }
            break;
        default:
            break;
        }
    }

    // Phases 0x12 to 0x15: actor 0 waits and is told (0x12, 0x13), then its family home is found (0x14) and its
    // person (0x15).
    void EvCrazyCult_TellLeader(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        switch (data->var)
        {
        case 0x12:
            cultWait(data, city, actorAt(data, 0));
            break;
        case 0x13:
            cultCountDown(data);
            break;
        case 0x14:
            if (const GuestPtr<MapObject> home = Map_FindFamilyHome(city->map, data->actor.family))
            {
                data->actor.object = home->id;
                home->flags |= 0x200u;
                data->var = 0x15;
            }
            break;
        case 0x15:
            if (const GuestPtr<MapPerson> person = findPerson(city, data->actor.family, data->actor.member))
            {
                data->actor.person = person->index;
                data->var = 0x16;
            }
            break;
        default:
            break;
        }
    }

    // The delivery man's phases (jump table 0x1BA6C0): 1 walks to actor 1 for 120 minutes, 2 and 5 tell actor 0
    // (60 minutes), 3 and 6 count down, 4 waits 60 minutes, 7 ends the event after 240.
    void EvDeliveryMan_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvDeliveryData> delivery = data.as<EvDeliveryData>();
        const auto tick = [](int32_t &minutes) { return ++minutes; };
        switch (data->var)
        {
        case 1:
            EvActor_MoveTo(city, actorAt(data, 1), actorAt(data, 1), 0);
            if (tick(delivery->walkMinutes) < 120)
                break;
            delivery->state = 0;
            ++data->var;
            break;
        case 2:
        case 5:
            if (EvActor_Notify(city, GuestPtr<ActorRef>::of(data->actor)) == 0)
                break;
            delivery->tellMinutes = 60;
            ++data->var;
            break;
        case 3:
        case 6:
        {
            const int32_t minutes = delivery->tellMinutes - 1;
            delivery->tellMinutes = minutes;
            if (minutes <= 0)
                --data->var;
            break;
        }
        case 4:
            if (tick(delivery->waitMinutes) < 60)
                break;
            delivery->state = 1;
            ++data->var;
            break;
        case 7:
            if (tick(delivery->endMinutes) >= 240)
                Event_Finish(city->events, city, 0);
            break;
        default:
            break;
        }
    }

    PORT_FN(EvAlien_FindBuilding);
    PORT_FN(EvThief_FindBuilding);
    PORT_FN(EvAlien_PickTarget);
    PORT_FN(EvRelationship_Answer);
    PORT_FN(EvActor_FindShop);
    PORT_FN(EvCrazyCult_TellFirstMember);
    PORT_FN(EvCrazyCult_TellLeader);
    PORT_FN(EvDeliveryMan_Update);

    // ---- batch 38

    namespace
    {
        // Whether a claim section C (four-byte entries, the id in the low half, ending at 0) lists the id.
        bool supplies(GuestPtr<ClaimSupply> supply, uint16_t id)
        {
            for (; supply->id != 0; ++supply)
                if (supply->id == id)
                    return true;
            return false;
        }
    }

    // Whether an active entry (first byte 0) of building A's claim section B (eight-byte entries, ending at id 0)
    // names an id, or a group of ids (0x8000 and up, listed in section D), that building B's section C lists.
    // The original lists a single id on the stack with the zero word at 0x1BDD58 as its end.
    int EvAdvertise_ClaimsMatch(GuestPtr<CityScene> city, GuestPtr<ActorRef> actorA, GuestPtr<ActorRef> actorB)
    {
        const GuestPtr<MapObject> objB = Map_GetRecord(city->map, actorB->object);
        const GuestPtr<ClaimSupply> suppliesB = Claim_GetSupplies(GuestPtr<BuildingType>{globals::buildingTypeAddress(objB->type)}->claimC);
        const GuestPtr<MapObject> objA = Map_GetRecord(city->map, actorA->object);
        for (GuestPtr<ClaimEntry> entry = Claim_GetList(GuestPtr<BuildingType>{globals::buildingTypeAddress(objA->type)}->claimB);
             entry->id != 0; ++entry)
        {
            if (entry->wish != 0)
                continue;
            const uint16_t id = entry->id;
            if (id < 0x8000)
            {
                if (supplies(suppliesB, id))
                    return 1;
                continue;
            }
            for (GuestPtr<uint16_t> group = Claim_GetGroup(id); *group != 0; ++group)
                if (supplies(suppliesB, *group))
                    return 1;
        }
        return 0;
    }

    PORT_FN(EvAdvertise_ClaimsMatch);

    // ---- actors moving into buildings (batch 39)

    namespace
    {
        // The person's tile from the position (z converted first, as the originals do).
        uint32_t tileOf(GuestPtr<MapPerson> person)
        {
            const int z = sdk::fptosi(ps2f(64.0f) - ps2f(person->z));
            const int x = sdk::fptosi(ps2f(64.0f) + ps2f(person->x));
            return (static_cast<uint32_t>(x) + (static_cast<uint32_t>(z) << 7)) & 0xFFFFu;
        }

        // Whether the actor may go into building `to`: always into the event's own building, the person's target or
        // a building that does not count its people (type flag 0x40); otherwise as one more occupant when it must
        // arrive or there is room (the type's capacity).
        bool mayEnter(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, GuestPtr<MapPerson> person, uint16_t to, int arrive)
        {
            if (to == actor->object || to == person->target)
                return true;
            const GuestPtr<MapObject> building = Map_GetRecord(city->map, to);
            const GuestPtr<BuildingType> type = typeOf(building);
            if ((type->flags & 0x40u) == 0)
                return true;
            if (arrive == 0 && !(building->occupants < type->capacity))
                return false;
            building->occupants = static_cast<uint8_t>(building->occupants + 1);
            return true;
        }
    }

    // Moves the actor (or, for a person with flag 4, the vehicle heading for them) into the target building. From
    // another building it leaves that one first; from outside it must stand on a building, which becomes its
    // destination. Returns 1 when it is in (or was already), 0 when it cannot go in yet.
    int EvActor_MoveTo(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, GuestPtr<ActorRef> target, int arrive)
    {
        const GuestPtr<MapPerson> person = city->map->people + actor->person;
        GuestPtr<MapPerson> mover = person;
        if ((person->flags & 4u) != 0)
            mover = Map_FindVehicleOfDriver(city->map, static_cast<int16_t>(actor->person));
        if (!mover)
            return 0;
        const uint16_t to = target->object;
        if (mover->inside == to)
        {
            person->flags &= 0xFF7Fu;
            return 1;
        }
        if (mover->inside != kNoObject)
        {
            if (!mayEnter(city, actor, person, to, arrive))
                return 0;
            int32_t unused = 0;
            leaveBuilding(city, mover, unused);
        }
        else
        {
            const GuestPtr<MapObject> on = Map_FindPersonObjectOnTile(city->map, mover->index, tileOf(mover));
            if (!on)
                return 0;
            if (on->id == to)
                return 1;
            if (!mayEnter(city, actor, person, to, arrive))
                return 0;
            mover->destination = on->id;
        }
        mover->inside = target->object;
        person->flags &= 0xFF7Fu;
        mover->cooldown = 12;
        return 1;
    }

    // In the first minute of the day the person takes the weather flag. Outside or at home, the person goes into
    // a shop away from them: tile lists from a random one on (64 tries), the first shop (type flag 0x40) with a
    // walkable entrance, not their home, that they are not within 5 tiles of; none found leaves them outside.
    void EvActor_Wander(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor)
    {
        port::GuestFrame<0xD0> frame;
        const GuestPtr<uint32_t> count{frame.ptr().address + 0xCCu};
        const GuestPtr<MapPerson> person = city->map->people + actor->person;
        if (city->clock / 60 == 0)
            weatherFlag(city, person);
        if (person->inside != kNoObject && person->inside != person->home)
            return;
        int list = Random_TimerMix() & 0x3F;
        for (int tries = 0; tries < 0x40; ++tries)
        {
            const GuestPtr<uint16_t> items = Map_GetTileList(city->map, list, count);
            if (++list >= 0x40)
                list = 0;
            GuestPtr<uint16_t> item = items;
            for (uint32_t i = 0; i < *count; ++i, ++item)
            {
                const GuestPtr<MapObject> shop = Map_GetRecord(city->map, *item);
                if ((typeOf(shop)->flags & 0x40u) == 0 || Map_TileIndexWalkable(city->map, shop->entranceTile) == 0 || shop->id == person->home)
                    continue;
                const GuestPtr<MapObject> at = Map_GetTileFirstObject(city->map, shop->entranceTile);
                const int size = typeOf(at)->size;
                const int x0 = at->tile % 128, z0 = at->tile / 128;
                const int z = sdk::fptosi(ps2f(64.0f) - ps2f(person->z));
                const int x = sdk::fptosi(ps2f(64.0f) + ps2f(person->x));
                if (x >= x0 - 5 && x < x0 + size + 5 && z >= z0 - 5 && z < z0 + size + 5)
                    continue;
                person->inside = at->id;
                person->cooldown = 12;
                return;
            }
        }
        person->inside = kNoObject;
    }

    PORT_FN(EvActor_MoveTo);
    PORT_FN(EvActor_Wander);

    // ---- start checks (batch 39)

    namespace
    {
        // A resident for a romance event near the person's home (its footprint widened by 5): in the tile lists
        // of the zero-ended table, a building other than that home whose family lives in the city, without a
        // maxed meter and with its claim meter at level 0, overlapping that area, with a resident of that family
        // who passes the test (list id, person). That resident becomes the event's actor family and member.
        // findSuitor's frame (that of each caller).
        struct SuitorFrame
        {
            TileRect area;             // +0x00 the person's home widened by 5
            TileRect footprint;        // +0x10 a building's
            uint32_t count;            // +0x20 the tile list's length
            uint16_t cursor;           // +0x24 the people cursor
            uint8_t gap_26[0xA];
        };
        static_assert(offsetof(SuitorFrame, footprint) == 0x10 && offsetof(SuitorFrame, count) == 0x20 &&
                      offsetof(SuitorFrame, cursor) == 0x24 && sizeof(SuitorFrame) == 0x30);

        template <typename Suits>
        int findSuitor(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person, GuestPtr<int32_t> lists,
                       Suits suits)
        {
            port::GuestFrameOf<SuitorFrame> frame;
            const GuestPtr<uint32_t> count = frame.at(&SuitorFrame::count);
            const GuestPtr<uint16_t> cursor = frame.at(&SuitorFrame::cursor);
            Building_GetFootprint(Map_GetRecord(city->map, person->home), 5, frame.at(&SuitorFrame::area));
            for (GuestPtr<int32_t> list = lists; *list != 0; ++list)
            {
                const int32_t id = *list;
                const GuestPtr<uint16_t> items = Map_GetTileList(city->map, id, count);
                const GuestPtr<uint16_t> end = items + static_cast<int32_t>(*count);
                for (GuestPtr<uint16_t> item = items; item < end; ++item)
                {
                    if (*item == person->home)
                        continue;
                    const GuestPtr<MapObject> building = Map_GetRecord(city->map, *item);
                    if (Family_InCity(building->family) == 0 || (building->flags & 0x40u) != 0 || ClaimMeter_Level(building->claimMeter) != 0)
                        continue;
                    Building_GetFootprint(building, 0, frame.at(&SuitorFrame::footprint));
                    if (!overlap(frame->area, frame->footprint))
                        continue;
                    *cursor = building->firstPerson;
                    while (const GuestPtr<MapPerson> resident = People_Next(city->map, cursor))
                    {
                        const GuestPtr<CharClass> cls = globals::charClass(resident->charType);
                        if ((cls->flags & 1u) != 0 || resident->family != building->family || !suits(id, resident))
                            continue;
                        data->actor.family = resident->family;
                        data->actor.member = resident->member;
                        return 1;
                    }
                }
            }
            return 0;
        }
    }

    // Character types 0x10 to 0x13 (from list 1 only members 2 and up), in the tile lists of globals::kBoyfriendLists.
    int EvBoyfriend_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        return findSuitor(data, city, person, globals::kBoyfriendLists, [](int32_t list, GuestPtr<MapPerson> resident) {
            return !(list == 1 && resident->member < 2) && resident->charType >= 0x10 && resident->charType < 0x14;
        });
    }

    // Character types 0xB to 0xE (from list 1 only members 2 and up), in the tile lists of globals::kGirlfriendLists.
    int EvGirlfriend_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        return findSuitor(data, city, person, globals::kGirlfriendLists, [](int32_t list, GuestPtr<MapPerson> resident) {
            return !(list == 1 && resident->member < 2) && resident->charType >= 0xB && resident->charType < 0xF;
        });
    }

    // Member 1 of character type 0x10 to 0x13, in the tile lists of globals::kAffairLists.
    int EvAffair_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        return findSuitor(data, city, person, globals::kAffairLists, [](int32_t, GuestPtr<MapPerson> resident) {
            return resident->member == 1 && resident->charType >= 0x10 && resident->charType < 0x14;
        });
    }

    // The noodle stand: over every map object with its claim meter at level 0, the first idle worker of character
    // type 0x17 at a building of type 0x28, 0x18 at type 0x29 and 0xC at type 0x2C, until all three are found. With
    // any found, the event's ten actors lose their person and building, the workers become actors 4 to 6 and their
    // buildings the buildings of actors 7 to 9 (the ones not found keep the original's starting values,
    // globals::kNoodleWorkersStart and kNoodleBuildingsStart).
    int EvNoodles_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        static constexpr uint16_t kTypes[3] = {0x28, 0x29, 0x2C};
        static constexpr uint32_t kWorkers[3] = {0x17, 0x18, 0xC};
        uint32_t workers[3];
        uint16_t buildings[3];
        for (uint32_t k = 0; k < 3; ++k)
        {
            workers[k] = globals::kNoodleWorkersStart[static_cast<int32_t>(k)];
            buildings[k] = globals::kNoodleBuildingsStart[static_cast<int32_t>(k)];
        }
        int found = 0;
        // From record 1, kObjectSlots records: the last is one past the table, as in the original.
        const GuestPtr<MapObject> first = Map_GetRecord(city->map, 1);
        for (GuestPtr<MapObject> obj = first; !(workers[0] && workers[1] && workers[2]) && obj != first + kObjectSlots; ++obj)
        {
            if (obj->type == 0 || ClaimMeter_Level(obj->claimMeter) != 0)
                continue;
            for (uint32_t k = 0; k < 3; ++k)
            {
                if (obj->type != kTypes[k])
                    continue;
                if (workers[k] == 0)
                {
                    workers[k] = Person_FindIdleOfType(city, obj, kWorkers[k]).address;
                    if (workers[k] != 0)
                    {
                        buildings[k] = obj->id;
                        found = 1;
                    }
                }
                break;
            }
        }
        if (!found)
            return 0;
        const GuestPtr<EvNoodlesData> noodles = data.as<EvNoodlesData>();
        for (GuestPtr<ActorRef> actor = GuestPtr<ActorRef>::of(noodles->actors[0]); actor.address < GuestPtr<ActorRef>::of(noodles->actors[9]).address + 8u; ++actor)
        {
            actor->person = kNoPerson;
            actor->object = kNoObject;
        }
        for (uint32_t k = 0; k < 3; ++k)
        {
            if (workers[k] == 0)
                continue;
            const GuestPtr<MapPerson> worker{workers[k]};
            const GuestPtr<ActorRef> actor = GuestPtr<ActorRef>::of(noodles->actors[4]) + static_cast<int32_t>(k);
            actor->person = worker->index;
            actor->object = worker->home;
            actor->family = worker->family;
            actor->member = worker->member;
        }
        for (uint32_t k = 0; k < 3; ++k)
            (GuestPtr<ActorRef>::of(noodles->actors[7]) + static_cast<int32_t>(k))->object = buildings[k];
        return 1;
    }

    PORT_FN(EvBoyfriend_Check);
    PORT_FN(EvGirlfriend_Check);
    PORT_FN(EvAffair_Check);
    PORT_FN(EvNoodles_Check);

    // ---- event buckets (batch 39)

    namespace
    {
        // The queue's buckets (EventQueue): bucket k runs from entry starts[k] up to the start of k + 1. Each bucket is
        // kept in the order of the types' priorities (EventType.priority), lowest first. The code walks the entries by
        // address, as the original does.
        uint32_t entryAt(GuestPtr<EventQueue> queue, int32_t index) { return (GuestPtr<EventQueueEntry>::of(queue->entries[0]) + index).address; }
        uint32_t bucketBegin(GuestPtr<EventQueue> queue, int bucket) { return entryAt(queue, queue->starts[bucket]); }
        GuestPtr<EventQueueEntry> queueEntry(uint32_t address) { return GuestPtr<EventQueueEntry>{address}; }
        GuestPtr<EventType> eventRecord(uint32_t types, uint8_t type) { return GuestPtr<EventType>{types} + type; }
        int8_t priority(uint32_t types, uint8_t type) { return eventRecord(types, type)->priority; }

        // Moves the entries of bucket `from` that pass the test (on the type's record and the wait counter) to
        // bucket `to`, each after the entries of lower priority there (the search goes on from the last place
        // taken), and shifts the buckets in between.
        template <typename Test>
        void moveEntries(GuestPtr<EventQueue> queue, int from, int to, uint32_t types, Test test)
        {
            const int32_t first = queue->starts[from];
            const int32_t count = queue->starts[from + 1] - first;
            uint32_t source = entryAt(queue, first);
            uint32_t place = bucketBegin(queue, to);
            uint32_t end = bucketBegin(queue, to + 1);
            const auto findPlace = [&](int8_t prio) {
                while (place < end && priority(types, queueEntry(place)->type) < prio)
                    place += 2u;
            };
            const auto entries = [](uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) / 2; };
            int32_t moved = 0;
            if (from < to)
            {
                for (int32_t i = 0; i < count; ++i)
                {
                    const uint8_t type = queueEntry(source)->type;
                    const int8_t wait = queueEntry(source)->wait;
                    if (!test(eventRecord(types, type), wait))
                    {
                        source += 2u;
                        continue;
                    }
                    findPlace(priority(types, type));
                    sdk::memmove(source, source + 2u, 2 * (entries(place, source) - 1));
                    queueEntry(place - 2u)->type = type;
                    queueEntry(place - 2u)->wait = wait;
                    ++moved;
                }
                for (int bucket = from + 1; bucket <= to; ++bucket)
                    queue->starts[bucket] = queue->starts[bucket] - moved;
                return;
            }
            for (int32_t i = 0; i < count; ++i, source += 2u)
            {
                const uint8_t type = queueEntry(source)->type;
                const int8_t wait = queueEntry(source)->wait;
                if (!test(eventRecord(types, type), wait))
                    continue;
                findPlace(priority(types, type));
                sdk::memmove(place + 2u, place, 2 * entries(source, place));
                queueEntry(place)->type = type;
                queueEntry(place)->wait = wait;
                place += 2u;
                end += 2u;
                ++moved;
            }
            for (int bucket = to + 1; bucket <= from; ++bucket)
                queue->starts[bucket] = queue->starts[bucket] + moved;
        }
    }

    // Every entry (the event manager's constructor and destructor).
    void EventList_MoveAll(GuestPtr<EventQueue> queue, int from, int to)
    {
        moveEntries(queue, from, to, globals::eventTypes.get(), [](GuestPtr<EventType>, int8_t) { return true; });
    }

    // The rest take the event table and what to compare with from `test`. Entries whose wait ran out.
    void EventList_MoveDue(GuestPtr<EventQueue> queue, int from, int to, GuestPtr<EventTest> test)
    {
        moveEntries(queue, from, to, test->types.address, [](GuestPtr<EventType>, int8_t wait) { return wait <= 0; });
    }

    // Entries whose wait ran out and whose type's level is the test's.
    void EventList_MoveDueLevel(GuestPtr<EventQueue> queue, int from, int to, GuestPtr<EventTest> test)
    {
        const uint32_t level = static_cast<uint32_t>(test->level);
        moveEntries(queue, from, to, test->types.address,
                    [level](GuestPtr<EventType> record, int8_t wait) { return wait <= 0 && record->level == level; });
    }

    // Entries whose type runs at the test's rank (its ranks share a bit with it).
    void EventList_MoveInRank(GuestPtr<EventQueue> queue, int from, int to, GuestPtr<EventTest> test)
    {
        const uint32_t rank = static_cast<uint32_t>(test->ranks);
        moveEntries(queue, from, to, test->types.address,
                    [rank](GuestPtr<EventType> record, int8_t) { return (rank & record->ranks) != 0; });
    }

    // The same rank test, and the type's level equal to the test's.
    void EventList_MoveInRankLevel(GuestPtr<EventQueue> queue, int from, int to, GuestPtr<EventTest> test)
    {
        const uint32_t rank = static_cast<uint32_t>(test->ranks), level = static_cast<uint32_t>(test->rankLevel);
        moveEntries(queue, from, to, test->types.address, [rank, level](GuestPtr<EventType> record, int8_t) {
            return (rank & record->ranks) != 0 && record->level == level;
        });
    }

    // Entries whose type does not run at the test's rank.
    void EventList_MoveOutOfRank(GuestPtr<EventQueue> queue, int from, int to, GuestPtr<EventTest> test)
    {
        const uint32_t rank = static_cast<uint32_t>(test->ranks);
        moveEntries(queue, from, to, test->types.address,
                    [rank](GuestPtr<EventType> record, int8_t) { return (rank & record->ranks) == 0; });
    }

    // Entries whose type's mask shares a bit with the test's.
    void EventList_MoveByMask(GuestPtr<EventQueue> queue, int from, int to, GuestPtr<EventTest> test)
    {
        const uint32_t mask = test->mask;
        moveEntries(queue, from, to, test->types.address,
                    [mask](GuestPtr<EventType> record, int8_t) { return (mask & record->mask) != 0; });
    }

    PORT_FN(EventList_MoveAll);
    PORT_FN(EventList_MoveDue);
    PORT_FN(EventList_MoveDueLevel);
    PORT_FN(EventList_MoveInRank);
    PORT_FN(EventList_MoveInRankLevel);
    PORT_FN(EventList_MoveOutOfRank);
    PORT_FN(EventList_MoveByMask);

    // ---- alien and delivery man start checks (batch 39)

    namespace
    {
        void setActorFromPerson(GuestPtr<ActorRef> actor, GuestPtr<MapPerson> person)
        {
            actor->person = person->index;
            actor->object = person->home;
            actor->family = person->family;
            actor->member = person->member;
        }

    }

    // The alien needs four buildings anywhere on the map: of type 0xE7 (tile list 0xB), one of the types of
    // globals::kAlienSecondTypes (lists 0x12, then 0x13), 0x10A (list 0x18) and 0x104 (list 0x16); and a resident of
    // character type 3 to 6 whose home is none of them, has its claim meter at level 0 and is not taken (flags 0x240).
    // That resident becomes actor 0 and the families' first members (and buildings) actors 1 to 4, the buildings marked
    // (0x200); actor 5 is the first Spaceman's Home family, with no person or building.
    int EvAlien_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        struct Locals
        {
            TileRect whole;            // +0x00 the whole map
            uint8_t gap_10[0x20];
        };
        static_assert(sizeof(Locals) == 0x30);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<TileRect> whole = frame.at(&Locals::whole);
        *whole = TileRect{0, 0, 128, 128};
        uint32_t found[4];
        for (uint32_t k = 0; k < 4; ++k)
            found[k] = globals::kAlienBuildingsStart[static_cast<int32_t>(k)];
        if (!(found[3] = EvAlien_FindBuilding(city, 0x16, 0x104, whole).address))
            return 0;
        if (!(found[2] = EvAlien_FindBuilding(city, 0x18, 0x10A, whole).address))
            return 0;
        if (!(found[0] = EvAlien_FindBuilding(city, 0xB, 0xE7, whole).address))
            return 0;
        for (GuestPtr<uint16_t> type = globals::kAlienSecondTypes; found[1] == 0 && *type != 0; ++type)
        {
            found[1] = EvAlien_FindBuilding(city, 0x12, *type, whole).address;
            if (found[1] == 0)
                found[1] = EvAlien_FindBuilding(city, 0x13, *type, whole).address;
        }
        if (found[1] == 0)
            return 0;
        uint16_t chosen[4] = {kNoPerson, kNoObject, 0, 0};
        const GuestPtr<MapPerson> people = city->map->people;
        for (GuestPtr<MapPerson> person = people; person != people + kPersonSlots; ++person)
        {
            if (person->charType < 3 || person->charType >= 7)
                continue;
            const GuestPtr<MapObject> home = Map_GetRecord(city->map, person->home);
            if (ClaimMeter_Level(home->claimMeter) != 0 || (home->flags & 0x240u) != 0)
                continue;
            if (home.address == found[0] || home.address == found[1] || home.address == found[2] || home.address == found[3])
                continue;
            chosen[0] = person->index;
            chosen[1] = person->home;
            chosen[2] = person->family;
            chosen[3] = person->member;
            home->flags |= 0x200u;
            break;
        }
        if (chosen[0] == kNoPerson)
            return 0;
        data->actor.person = chosen[0];
        data->actor.object = chosen[1];
        data->actor.family = chosen[2];
        data->actor.member = chosen[3];
        for (uint32_t k = 0; k < 4; ++k)
        {
            const GuestPtr<MapObject> building{found[k]};
            setActorFromPerson(actorAt(data, 1 + static_cast<int>(k)), Map_FindPersonEntry(city->map, static_cast<int16_t>(building->family), 0));
            building->flags |= 0x200u;
        }
        const GuestPtr<ActorRef> last = actorAt(data, 5);
        last->person = kNoPerson;
        last->object = kNoObject;
        last->family = globals::firstFamily(globals::types::kSpacemansHome);
        last->member = 0;
        return 1;
    }

    // The delivery man comes in rain or snow (weather 2 or 3) for a person with a target building of a type in the
    // delivery catalogue (0x1BA090, four entries of 0x184 bytes, the type first). From a random entry of the tile
    // lists (the five of globals::kDeliveryHolidayLists in holiday months, else the two of kDeliveryWorkdayLists) and
    // a random item on, a building outside the
    // target's footprint widened by 10 but inside it widened by 30, whose family's first member stands on it. That
    // building and person become actor 1; entry takes the catalogue entry and month the month.
    int EvDeliveryMan_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        const GuestPtr<EvDeliveryData> delivery = data.as<EvDeliveryData>();
        if (person->target == kNoObject)
            return 0;
        if (city->weatherToday != 2 && city->weatherToday != 3)
            return 0;
        struct Locals
        {
            TileRect near;             // +0x00 the target widened by 10
            TileRect wide;             // +0x10 and by 30
            TileRect footprint;        // +0x20 a building's
            uint32_t count;            // +0x30 the tile list's length
            uint8_t gap_34[0xC];
        };
        static_assert(offsetof(Locals, wide) == 0x10 && offsetof(Locals, footprint) == 0x20 && offsetof(Locals, count) == 0x30 &&
                      sizeof(Locals) == 0x40);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<uint32_t> count = frame.at(&Locals::count);
        const bool holiday = (city->flags & 2u) != 0;
        const GuestPtr<int32_t> table = holiday ? globals::kDeliveryHolidayLists : globals::kDeliveryWorkdayLists;
        const int lists = holiday ? 5 : 2;
        const GuestPtr<MapObject> target = Map_GetRecord(city->map, person->target);
        const uint16_t targetType = target->type;
        Building_GetFootprint(target, 10, frame.at(&Locals::near));
        frame->wide.x0 = frame->near.x0 - 20;
        frame->wide.z0 = frame->near.z0 - 20;
        frame->wide.x1 = frame->near.x1 + 20;
        frame->wide.z1 = frame->near.z1 + 20;
        GuestPtr<int32_t> list = table + static_cast<int32_t>(Random_Next()) % lists;
        for (int tries = 0; tries < lists; ++tries, ++list)
        {
            if (!(list < table + lists))
                list = table;
            const GuestPtr<uint16_t> items = Map_GetTileList(city->map, *list, count);
            if (*count == 0)
                continue;
            GuestPtr<uint16_t> item = items + static_cast<int32_t>(Random_Next()) % static_cast<int32_t>(*count);
            for (int i = 0; i < static_cast<int32_t>(*count); ++i, ++item)
            {
                if (!(item < items + static_cast<int32_t>(*count)))
                    item = items;
                const GuestPtr<MapObject> building = Map_GetRecord(city->map, *item);
                Building_GetFootprint(building, 0, frame.at(&Locals::footprint));
                if (overlap(frame->near, frame->footprint))
                    continue;
                Building_GetFootprint(building, 0, frame.at(&Locals::footprint));
                if (!overlap(frame->wide, frame->footprint))
                    continue;
                const GuestPtr<MapPerson> member = Map_FindPersonEntry(city->map, static_cast<int16_t>(building->family), 0);
                if (Map_FindPersonObjectOnTile(city->map, member->index, tileOf(member)).address != building.address)
                    continue;
                int entry = -1;
                for (int32_t e = 0; e < 4; ++e)
                    if (globals::deliveryCatalogue(e)->buildingType == targetType)
                    {
                        entry = e;
                        break;
                    }
                if (entry < 0)
                    return 0;
                delivery->entry = entry;
                delivery->month = city->monthCount % 12;
                const GuestPtr<ActorRef> actor = actorAt(data, 1);
                actor->object = building->id;
                actor->family = building->family;
                actor->member = 0;
                actor->person = member->index;
                return 1;
            }
        }
        return 0;
    }

    PORT_FN(EvAlien_Check);
    PORT_FN(EvDeliveryMan_Check);

    // ---- the crazy cult (batch 39)

    namespace
    {
        // The next resident (a class without flag 1) without flag 0x400 takes it; false when all have it.
        bool flagNextResident(GuestPtr<CityScene> city)
        {
            const GuestPtr<MapPerson> people = city->map->people;
            for (GuestPtr<MapPerson> person = people; person != people + kPersonSlots; ++person)
            {
                if (person->charType == 0)
                    continue;
                const GuestPtr<CharClass> cls = globals::charClass(person->charType);
                if ((cls->flags & 1u) != 0 || (person->flags & 0x400u) != 0)
                    continue;
                person->flags |= 0x400u;
                return true;
            }
            return false;
        }

        // The actors of a zero-ended table of their numbers (globals::kCultHomesActors and kCultPeopleActors: 4, 5, 1).
        template <typename Each>
        bool eachCultActor(GuestPtr<EventData> data, GuestPtr<int32_t> table, Each each)
        {
            for (GuestPtr<int32_t> k = table; *k != 0; ++k)
                if (!each(actorAt(data, *k)))
                    return false;
            return true;
        }
    }

    // Phases 10 to 16: 10 waits until everyone has been reached (allJoined), 11 and 13 tell actor 3 when its time comes
    // and 12 and 14 count those minutes back, 15 finds the homes of actors 4, 5 and 1 (marked 0x200; back to 15 while
    // one is missing) and 16 their persons (back to 16).
    void EvCrazyCult_TellSecondMember(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvCultData> cult = data.as<EvCultData>();
        switch (data->var)
        {
        case 10:
            if (cult->allJoined != 0)
                data->var = 11;
            break;
        case 11:
        case 13:
            cultWait(data, city, actorAt(data, 3));
            break;
        case 12:
        case 14:
            cultCountDown(data);
            break;
        case 15:
            data->var = 0x10;
            eachCultActor(data, globals::kCultHomesActors, [&](GuestPtr<ActorRef> actor) {
                if (actor->object != kNoObject)
                    return true;
                const GuestPtr<MapObject> home = Map_FindFamilyHome(city->map, actor->family);
                if (!home)
                {
                    data->var = 0xF;
                    return false;
                }
                actor->object = home->id;
                home->flags |= 0x200u;
                return true;
            });
            break;
        case 16:
            data->var = 0x11;
            eachCultActor(data, globals::kCultPeopleActors, [&](GuestPtr<ActorRef> actor) {
                if (actor->person != kNoPerson)
                    return true;
                const GuestPtr<MapPerson> person = Map_FindPersonEntry(city->map, static_cast<int16_t>(actor->family), actor->member);
                if (!person)
                {
                    data->var = 0x10;
                    return false;
                }
                actor->person = person->index;
                return true;
            });
            break;
        default:
            break;
        }
    }

    // While the cult runs, every 30 minutes (spreadMinutes) one more resident takes flag 0x400; in phases 10 to 16
    // allJoined then tells whether everyone has it. Phases 2 and 3 find actor 2's home and person (then waiting for the
    // next month at 7:00); 4 to 9, 10 to 16 and 0x12 to 0x15 go to the phase handlers; 0x1B sets the next wait, 0x1C
    // tells actor 0 when its time comes and 0x1D counts 60 minutes back.
    void EvCrazyCult_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvCultData> cult = data.as<EvCultData>();
        const int var = data->var;
        const auto due = [&] {
            const int32_t minutes = cult->spreadMinutes - 1;
            cult->spreadMinutes = minutes;
            return minutes <= 0;
        };
        if (var < 10)
        {
            if (due())
            {
                flagNextResident(city);
                cult->spreadMinutes = 30;
            }
            if (var >= 4)
            {
                EvCrazyCult_TellFirstMember(data, city);
                return;
            }
            const GuestPtr<ActorRef> actor = actorAt(data, 2);
            if (data->var == 3)
            {
                if (const GuestPtr<MapPerson> person = Map_FindPersonEntry(city->map, static_cast<int16_t>(actor->family), actor->member))
                {
                    actor->person = person->index;
                    data->var = 4;
                    cult->waitMonth = city->monthCount + 1;
                    cult->waitTime = 25200;
                }
            }
            else if (data->var == 2)
            {
                if (const GuestPtr<MapObject> home = Map_FindFamilyHome(city->map, actor->family))
                {
                    actor->object = home->id;
                    home->flags |= 0x200u;
                    data->var = 3;
                }
            }
            return;
        }
        if (var < 0x11)
        {
            if (due())
            {
                cult->allJoined = flagNextResident(city) ? 0 : 1;
                cult->spreadMinutes = 30;
            }
            EvCrazyCult_TellSecondMember(data, city);
            return;
        }
        if (var < 0x16)
        {
            EvCrazyCult_TellLeader(data, city);
            return;
        }
        switch (var)
        {
        case 0x1B:
            cult->waitMonth = city->monthCount + 1;
            cult->waitTime = 25200;
            break;
        case 0x1C:
            if (city->monthCount < cult->waitMonth || city->clock < cult->waitTime)
                break;
            if (EvActor_Notify(city, GuestPtr<ActorRef>::of(data->actor)) == 0)
                break;
            ++data->var;
            cult->tellMinutes = 60;
            break;
        case 0x1D:
        {
            const int32_t minutes = cult->tellMinutes - 1;
            cult->tellMinutes = minutes;
            if (minutes <= 0)
                --data->var;
            break;
        }
        default:
            break;
        }
    }

    PORT_FN(EvCrazyCult_TellSecondMember);
    PORT_FN(EvCrazyCult_Update);

    // ---- farewell party, shopping (batch 39)

    // The farewell party (the boss leaves): the phone calls are cleared; actor 0 is the Boss' Home family.
    // Residents (character types 1 to 0x1D) with friendship band 3 and up, living in a building other than types
    // 0xB9 and 0x105 with its claim meter at level 0, are the guests (guests counts them); of those in schedule state
    // 6 and up the best friend is actor 1 (the first on a tie) and their home is marked. The party starts at
    // 13:00 plus an hour per 50 guests (partyTime, seconds, 19:00 at the latest); actor 2's person becomes 0xFFFF.
    GuestPtr<MapObject> EvFarewellParty_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvPartyData> party = data.as<EvPartyData>();
        PhoneCall_Next(city->phoneCalls, city);
        data->actor.person = kNoPerson;
        data->actor.object = kNoObject;
        data->actor.family = globals::firstFamily(globals::types::kBossHome);
        data->actor.member = 0;
        GuestPtr<MapPerson> best{0};
        const GuestPtr<MapPerson> people = city->map->people;
        for (GuestPtr<MapPerson> person = people; person != people + kPersonSlots; ++person)
        {
            if (person->charType == 0 || person->charType >= 0x1E)
                continue;
            if (Family_GetFriendshipBand(person->family, person->member) < 3)
                continue;
            const GuestPtr<MapObject> home = Map_GetRecord(city->map, person->home);
            if (home->type == 0xB9 || home->type == 0x105 || ClaimMeter_Level(home->claimMeter) != 0)
                continue;
            party->guests = party->guests + 1;
            if (person->schedState < 6)
                continue;
            if (!best)
            {
                best = person;
                continue;
            }
            const int mine = Family_GetFriendship(person->family, person->member);
            if (Family_GetFriendship(best->family, best->member) < mine)
                best = person;
        }
        setActorFromPerson(actorAt(data, 1), best);
        const GuestPtr<MapObject> record = markBuilding(city, actorAt(data, 1)->object);
        party->partyTime = (party->guests / 50 + 13) * 3600;
        if (68400 < party->partyTime)
            party->partyTime = 68400;
        party->actors[2].person = 0xFFFF;
        return record;
    }

    // Shopping: actor 0 keeps heading for its building; by phase (jump table 0x1BA8A0) 1 waits until four hours
    // before the event type's time (finishAt, minutes), 2 goes to 5 within two hours of it or tells the actor, 5 goes
    // to 8 once it has passed or tells the actor, 8 tells the actor, 3, 6 and 9 count 60 minutes back, and 10 and 11
    // end the event after 240 minutes.
    void EvShopping_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvShoppingData> shopping = data.as<EvShoppingData>();
        const GuestPtr<ActorRef> actor = GuestPtr<ActorRef>::of(data->actor);
        EvActor_MoveTo(city, actor, actor, 0);
        const auto time = [&] { return static_cast<int>(globals::eventTypeCopy(data->kind)->finishAt); };
        const auto tell = [&] {
            if (EvActor_Notify(city, actor) == 0)
                return;
            ++data->var;
            shopping->tellMinutes = 60;
        };
        const int minutes = city->clock / 60;
        switch (data->var)
        {
        case 1:
            if (time() - 240 < minutes)
                ++data->var;
            break;
        case 2:
            if (time() - 120 < minutes)
                data->var = 5;
            else
                tell();
            break;
        case 3:
        case 6:
        case 9:
        {
            const int32_t minutes = shopping->tellMinutes - 1;
            shopping->tellMinutes = minutes;
            if (minutes <= 0)
                --data->var;
            break;
        }
        case 5:
            if (time() < minutes)
                data->var = 8;
            else
                tell();
            break;
        case 8:
            tell();
            break;
        case 10:
        case 11:
        {
            const int32_t minutes = shopping->endMinutes + 1;
            shopping->endMinutes = minutes;
            if (minutes >= 241)
                Event_Finish(city->events, city, 0);
            break;
        }
        default:
            break;
        }
    }

    PORT_FN(EvFarewellParty_Start);
    PORT_FN(EvShopping_Update);

    // ---- talk script values of the message and advertisement events (batch 39)

    // Argument 0 pays the first payday: after a good month (as mm_EvPayday_Update reckons it) the salary rises by 2500
    // with a happy sound (0x16, returning 1), else sound 0x11; the money gains the salary (both saturating) and the
    // counts are reset. 1 adds laptop entry 9 with sound 0x15, 2 entry 0xC. 3: whether any resident (character types
    // 1 to 0x1D) in schedule state 6 and up with friendship band 3 and up works at a building with its claim meter at
    // level 0. 4 queues event 3 and ends this one.
    int EvMsg_Answer(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef>, int mode)
    {
        switch (mode)
        {
        case 0:
        {
            int result = 0;
            const GuestPtr<StageState> stage = globals::stageState();
            const uint32_t score = static_cast<uint32_t>(city->paydayScore) + 2u * static_cast<uint32_t>(globals::familiesMovedInEvent.get()) -
                                   4u * static_cast<uint32_t>(globals::familiesLeftEvent.get());
            if (static_cast<int32_t>(score) < 100)
                Sound_PlaySe(0x11, 0, 0x3C, 0.0f, 0.0f);
            else
            {
                stage->salary = static_cast<int32_t>(static_cast<uint32_t>(stage->salary) + 2500u);
                if (stage->salary < 0)
                    stage->salary = 0x7FFFFFFF;
                Sound_PlaySe(0x16, 0, 0x3C, 0.0f, 0.0f);
                result = 1;
            }
            const int32_t salary = stage->salary;
            stage->money = static_cast<int32_t>(static_cast<uint32_t>(stage->money) + static_cast<uint32_t>(salary));
            if (stage->money < 0)
                stage->money = salary > 0 ? 0x7FFFFFFF : 0;
            city->paydayScore = 0;
            globals::familiesLeftEvent.set(0);
            globals::familiesMovedInEvent.set(0);
            return result;
        }
        case 1:
            Laptop_AddEntry(city->laptop, 9);
            Sound_PlaySe(0x15, 0, 0x3C, 0.0f, 0.0f);
            return 0;
        case 2:
            Laptop_AddEntry(city->laptop, 0xC);
            return 0;
        case 3:
        {
            const GuestPtr<MapPerson> people = city->map->people;
            for (GuestPtr<MapPerson> person = people; person != people + kPersonSlots; ++person)
            {
                if (person->charType == 0 || person->charType >= 0x1E || person->schedState < 6)
                    continue;
                if (Family_GetFriendshipBand(person->family, person->member) < 3)
                    continue;
                if (ClaimMeter_Level(Map_GetRecord(city->map, person->home)->claimMeter) == 0)
                    return 1;
            }
            return 0;
        }
        case 4:
            Event_Trigger(city->events, 3, city, GuestPtr<MapPerson>{0});
            Event_Finish(city->events, city, 1);
            return 0;
        default:
            return 0;
        }
    }

    // The advertisement keeps the families told (visitors, fifteen (family, member) pairs, a zero family ends them) and
    // their count (visitorCount). Argument 0: whether the actor was told. 1: for an actor told, where it stands (4 on the
    // event building, 3 inside it, else 5); else 0 once the list runs out, or at the first free place 2 (1 when the
    // event building is full, 15 occupants) if the actor's building claims what the event building offers; 6 with
    // the list full (the phase becomes 4). 2: adds the actor (once), moves it into the event building and counts it
    // (phase 4 at fifteen). 3: 2 from ten told, 1 from five. 4: the actor's family gains three friendship points
    // per family told.
    int EvAdvertise_Answer(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        const GuestPtr<EvAdvertiseData> ad = data.as<EvAdvertiseData>();
        const GuestPtr<MemberId> first = GuestPtr<MemberId>::of(ad->visitors[0]), end = first + 15;
        const auto told = [&](GuestPtr<MemberId> entry) { return entry->family == actor->family && entry->member == actor->member; };
        const auto count = [&] { return ad->visitorCount; };
        switch (mode)
        {
        case 0:
            for (GuestPtr<MemberId> entry = first; entry->family != 0 && entry.address < end.address; ++entry)
                if (told(entry))
                    return 1;
            return 0;
        case 1:
            if (count() >= 15)
            {
                data->var = 4;
                return 6;
            }
            for (GuestPtr<MemberId> entry = first; entry.address < end.address; ++entry)
            {
                if (entry->family == 0)
                {
                    if (EvAdvertise_ClaimsMatch(city, actor, GuestPtr<ActorRef>::of(data->actor)) == 0)
                        continue;
                    return Map_GetRecord(city->map, data->actor.object)->occupants < 15 ? 2 : 1;
                }
                if (!told(entry))
                    continue;
                const GuestPtr<MapPerson> person = city->map->people + actor->person;
                const GuestPtr<MapObject> on = Map_FindPersonObjectOnTile(city->map, person->index, tileOf(person));
                if (on && on->id == data->actor.object)
                    return 4;
                return person->inside == data->actor.object ? 3 : 5;
            }
            return 0;
        case 2:
            if (data->var == 4 || count() >= 15)
                return 0;
            for (GuestPtr<MemberId> entry = first; entry.address < end.address; ++entry)
            {
                if (entry->family == 0)
                {
                    entry->family = actor->family;
                    entry->member = actor->member;
                    break;
                }
                if (told(entry))
                    return 0;
            }
            EvActor_MoveTo(city, actor, GuestPtr<ActorRef>::of(data->actor), 1);
            ad->visitorCount = count() + 1;
            if (count() >= 15)
                data->var = 4;
            return 0;
        case 3:
            if (count() >= 10)
                return 2;
            return count() >= 5 ? 1 : 0;
        case 4:
            Family_AddFriendship(actor->family, actor->member, GuestPtr<int32_t>::of(city->approval), 3 * count());
            return 0;
        default:
            return 0;
        }
    }

    PORT_FN(EvMsg_Answer);
    PORT_FN(EvAdvertise_Answer);

    // ---- adoption (batch 39)

    // The adoption's phases (jump table 0x1BA780). 1, 4, 7, 10, 13 and 15 wait until actor 0 can be told (then 60
    // minutes, plus those until 6:00 or 360 more from 23:00); 2, 5, 8, 11, 14, 16 and 19 count those down and go
    // back to telling until the sixth time (toldCount), then on; 22 likewise, ending the event after the sixth. Every
    // tenth minute (minutes) 3 waits for an object of type 3 or 4, 6 for any residents, 9 for any friend, and 12 for
    // 101 residents and twice the applicants (then phase 15) or, with fewer than 32 applicants, for a claim of the list
    // in children seen ten times with one of them pressing (claim meter level 2 and up; pick takes its place). 17 and
    // 20 note the month (month); 18 and 21 tell actor 0 from noon of a later month.
    void EvAdoption_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvAdoptionData> adoption = data.as<EvAdoptionData>();
        const GuestPtr<ActorRef> actor = GuestPtr<ActorRef>::of(data->actor);
        const auto told = [&] {
            if (EvActor_Notify(city, actor) == 0)
                return false;
            ++data->var;
            adoption->minutes = 60;
            return true;
        };
        const auto advance = [&] {
            adoption->toldCount = 0;
            ++data->var;
        };
        const auto tenth = [&] { return ++adoption->minutes >= 10; };
        switch (data->var)
        {
        case 1:
        case 4:
        case 7:
        case 10:
        case 13:
        case 15:
            if (told())
            {
                const int32_t clock = city->clock;
                if (clock < 21600)
                    adoption->minutes += 360 - clock / 60;
                else if (clock >= 82801)
                    adoption->minutes += 360;
            }
            break;
        case 2:
        case 5:
        case 8:
        case 11:
        case 14:
        case 16:
        case 19:
        case 22:
            if (--adoption->minutes > 0)
                break;
            if (++adoption->toldCount < 6)
                --data->var;
            else if (data->var == 22)
                Event_Finish(city->events, city, 0);
            else
                ++data->var;
            break;
        case 3:
        {
            if (!tenth())
                break;
            const GuestPtr<MapObject> first = Map_GetRecord(city->map, 0);
            for (GuestPtr<MapObject> record = first; record != first + kObjectSlots; ++record)
            {
                if (record->type == 3 || record->type == 4)
                {
                    advance();
                    break;
                }
            }
            adoption->minutes = 0;
            break;
        }
        case 6:
            if (!tenth())
                break;
            if (city->map->population != 0)
                advance();
            adoption->minutes = 0;
            break;
        case 9:
        {
            if (!tenth())
                break;
            const GuestPtr<MapPerson> people = city->map->people;
            for (GuestPtr<MapPerson> person = people; person != people + kPersonSlots; ++person)
            {
                if (person->charType == 0)
                    continue;
                const GuestPtr<CharClass> cls = globals::charClass(person->charType);
                if ((cls->flags & 1u) != 0 || Family_GetFriendship(person->family, person->member) == 0)
                    continue;
                advance();
                break;
            }
            adoption->minutes = 0;
            break;
        }
        case 12:
        {
            if (!tenth())
                break;
            const int applicants = city->applicants->count;
            if (city->map->population + 2 * applicants >= 101)
            {
                adoption->toldCount = 0;
                data->var = 15;
                adoption->minutes = 0;
                break;
            }
            if (applicants >= 32)
            {
                adoption->minutes = 0;
                break;
            }
            port::GuestLocal<uint32_t> count;
            const GuestPtr<AdoptionChild> children = GuestPtr<AdoptionChild>::of(adoption->children[0]);
            for (int list = 0; list < 0x50; ++list)
            {
                const GuestPtr<uint16_t> items = Map_GetTileList(city->map, list, count);
                const GuestPtr<uint16_t> end = items + static_cast<int32_t>(*count);
                for (GuestPtr<uint16_t> item = items; item < end; ++item)
                {
                    const GuestPtr<MapObject> building = Map_GetRecord(city->map, *item);
                    if (building->claimSlot == kNoClaim || ClaimMeter_Level(building->claimMeter) == 0)
                        continue;
                    const GuestPtr<ClaimEntry> claim = Claim_GetList(typeOf(building)->claimB) + building->claimSlot;
                    if (claim->wish != 0)
                        continue;
                    for (GuestPtr<AdoptionChild> child = children; child->claim != 0; ++child)
                    {
                        if (child->claim != claim->id)
                            continue;
                        uint8_t seen = child->seen;
                        if ((seen & 0x80u) == 0 && ClaimMeter_Level(building->claimMeter) >= 2)
                            seen |= 0x80u;
                        seen = static_cast<uint8_t>((seen & 0x80u) | ((seen + 1u) & 0x7Fu));
                        child->seen = seen;
                        if ((seen & 0x7Fu) >= 10 && (seen & 0x80u) != 0)
                        {
                            adoption->pick = static_cast<int32_t>(child.address - children.address) / 4;
                            advance();
                            return;
                        }
                    }
                }
            }
            for (GuestPtr<AdoptionChild> child = children; child->claim != 0; ++child)
                child->seen = 0;
            adoption->minutes = 0;
            break;
        }
        case 17:
        case 20:
            adoption->month = city->monthCount;
            advance();
            break;
        case 18:
        case 21:
            if (adoption->month < city->monthCount && city->clock >= 43200)
                told();
            break;
        default:
            break;
        }
    }

    PORT_FN(EvAdoption_Update);

    // ---- the handler slot dispatch and the jump-table slots (batch 40)

    namespace
    {
        // Event handlers: the table 0x1AF0D0 points at one EventHandler per handler id (EventType.handler, 0 = none).
        GuestPtr<EventHandler> eventHandler(uint32_t id)
        {
            return GuestPtr<EventHandler>{port::load<uint32_t>(globals::kEventHandlers + 4u * id)};
        }

        // The handler slot `slot` of the event's type; 0 when the type has no handler or the handler lacks the slot.
        uint32_t handlerSlot(GuestPtr<EventData> data, uint32_t EventHandler::*slot)
        {
            const uint32_t id = globals::eventType(data->kind)->handler;
            if (id == 0)
                return 0;
            return (*eventHandler(id)).*slot;
        }

        GuestPtr<EventData> talkEvent(GuestPtr<EventMgr> events, int index)
        {
            return events->talks[index];
        }

        // The money changes by `delta` and, when that leaves it negative, becomes 0 (or the largest value when the
        // delta was positive: the sum wrapped).
        void changeMoney(int32_t delta)
        {
            const GuestPtr<StageState> stage = globals::stageState();
            const int32_t sum = static_cast<int32_t>(static_cast<uint32_t>(stage->money) + static_cast<uint32_t>(delta));
            stage->money = sum;
            if (sum < 0)
                stage->money = delta > 0 ? 0x7FFFFFFF : 0;
        }
    }

    // The talk's event runs its handler's Answer slot (0 when it has none); a is the actor asked about, mode the question.
    int64_t Event_CallAnswer(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int arg, int mode)
    {
        const GuestPtr<EventData> data = talkEvent(events, mode);
        const uint32_t slot = handlerSlot(data, &EventHandler::answer);
        if (slot == 0)
            return 0;
        return port::callGuest<int64_t>(slot, data, city, actor, arg);
    }

    // The same for the Text slot of the event in slot `index`.
    int64_t Event_CallText(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, int index, int arg)
    {
        const GuestPtr<EventData> data = talkEvent(events, index);
        const uint32_t slot = handlerSlot(data, &EventHandler::text);
        if (slot == 0)
            return 0;
        return port::callGuest<int64_t>(slot, data, city, arg);
    }

    // The talk starts the event's own handler (the Start slot at +4): its claims are added (mm_Event_AddClaims), the
    // event's var becomes 1 and the handler runs. Without a handler the result register stays as it was (after the
    // claims call).
    int64_t Event_CallHandlerSlot(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, int index, int arg)
    {
        const uint32_t cell = port::guest(&events->talks[index]);
        const GuestPtr<EventType> row = globals::eventType(GuestPtr<EventData>{port::load<uint32_t>(cell)}->kind);
        const uint32_t claimList = row->claimList;
        if (claimList != 0)
        {
            port::setGuestV0(static_cast<int32_t>(claimList));
            Event_AddClaims(static_cast<int>(claimList) - 1);
        }
        GuestPtr<EventData>{port::load<uint32_t>(cell)}->var = 1;
        const uint32_t id = row->handler;
        if (id != 0)
        {
            const uint32_t slot = eventHandler(id)->start;
            if (slot != 0)
                return port::callGuest<int64_t>(slot, GuestPtr<EventData>{port::load<uint32_t>(cell)}, city, arg);
        }
        return port::guestV0Wide();
    }

    // Type 7 (the useless middle-aged man) answers the talk: modes 0-3 store the mode in answer, 4 reads it back, 5 takes
    // 100 from the money, 6 gives 50, 7 gives 200.
    uint32_t EvUselessMiddleAged_Answer(GuestPtr<EventData> data, GuestPtr<CityScene>, GuestPtr<ActorRef>, int mode)
    {
        if (static_cast<uint32_t>(mode) >= 8)
            return 0;
        switch (mode)
        {
        case 4:
            return static_cast<uint32_t>(data.as<EvMiddleAgedData>()->answer);
        case 5:
            changeMoney(-100);
            return 0;
        case 6:
            changeMoney(50);
            return 0;
        case 7:
            changeMoney(200);
            return 0;
        default:
            data.as<EvMiddleAgedData>()->answer = mode;
            return 0;
        }
    }

    // The Text slot shared by types 29 and 30 (payday): formats a number into the event text buffer and returns it: the
    // families that moved in / left, the payday score, the score with the families counted (two for each moved in, four
    // for each left). Other values give an empty text.
    GuestPtr<char> EvPayday_Text(GuestPtr<EventData>, GuestPtr<CityScene> city, uint32_t value)
    {
        const GuestPtr<char> out{globals::kEventText};
        switch (value)
        {
        case 0:
            Text_FormatThousands(out, 2);
            break;
        case 1:
            Text_FormatThousands(out, 4);
            break;
        case 2:
            Text_FormatThousands(out, 1);
            break;
        case 3:
            Text_FormatThousands(out, globals::familiesMovedInEvent.get());
            break;
        case 4:
            Text_FormatThousands(out, globals::familiesLeftEvent.get());
            break;
        case 5:
            Text_FormatThousands(out, city->paydayScore);
            break;
        case 6:
            Text_FormatThousands(out, static_cast<int>(static_cast<uint32_t>(city->paydayScore) +
                                                       2u * static_cast<uint32_t>(globals::familiesMovedInEvent.get()) -
                                                       4u * static_cast<uint32_t>(globals::familiesLeftEvent.get())));
            break;
        default:
            port::store<uint8_t>(globals::kEventText, 0);
            break;
        }
        return out;
    }

    // Type 3 (the delivery man) answers the talk: 0 the catalogue entry (entry), 1 and 2 whether the player is within 1 of
    // actor 1 / actor 0, 3 and 4 add / take the price of the delivered goods (the catalogue's item for the month) to / from the
    // money, 5 reads the delivery state.
    int EvDeliveryMan_Answer(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef>, int mode)
    {
        const GuestPtr<EvDeliveryData> delivery = data.as<EvDeliveryData>();
        const auto price = [&] {
            return static_cast<int32_t>(globals::deliveryItem(delivery->entry, delivery->month)->price);
        };
        switch (static_cast<uint32_t>(mode))
        {
        case 0:
            return delivery->entry;
        case 1:
            return Player_DistanceToBuilding(city, actorAt(data, 1)) < 2 ? 1 : 0;
        case 2:
            return Player_DistanceToBuilding(city, actorAt(data, 0)) < 2 ? 1 : 0;
        case 3:
            changeMoney(price());
            return 0;
        case 4:
            changeMoney(-price());
            return 0;
        case 5:
            return delivery->state;
        default:
            return 0;
        }
    }

    // Type 18 (the crazy cult) answers the talk: 0, 1, 3 and 4 add the families stored in the event data to the
    // applicants, 2 asks whether at least three more applicants fit, 5 counts the asked actor's family among the three
    // actors (list at 0x1B37C0, as for the event's phases): the first time each one is asked bit k of metBits is set (the
    // answer 0); asking again answers 1, and with all three bits set (7) the event moves to its next var (0x12) and sets waitTime.
    int EvCrazyCult_Answer(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        const GuestPtr<EvCultData> cult = data.as<EvCultData>();
        // The family of an actor applies to move in.
        const auto apply = [&](int index) { CityScene_AddApplicantFamily(city, cult->actors[index].family, 1); };
        switch (static_cast<uint32_t>(mode))
        {
        case 0:
            apply(2);
            return 0;
        case 1:
            apply(3);
            return 0;
        case 2:
            return 0x20 - city->applicants->count >= 3 ? 1 : 0;
        case 3:
            apply(4);
            apply(5);
            apply(1);
            return 0;
        case 4:
            apply(0);
            return 0;
        case 5:
            for (uint32_t k = 0;; ++k)
            {
                const int32_t number = globals::kCultAnswerActors[static_cast<int32_t>(k)];
                if (number == 0)
                    return 0;
                if (actor->family != (GuestPtr<ActorRef>::of(cult->actors[0]) + number)->family)
                    continue;
                const int32_t bit = 1 << k;
                const int32_t bits = cult->metBits;
                if ((bits & bit) != 0)
                    return 1;
                cult->metBits = bits | bit;
                if (cult->metBits >= 7)
                {
                    data->var = 0x12;
                    cult->waitTime = 0x6270;
                }
                return 0;
            }
        default:
            return 0;
        }
    }

    // Type 12 (adoption) answers the talk. The list (children) has up to six entries of four bytes (a byte flag, claim, and
    // the building type, familyType); the family tried is that type's first family (globals::firstFamily), or the next
    // one the city lets apply.
    //   0: 1 when no orphanage (type 4) exists, 0 when there is one but no building of a type with flag 8, else 2
    //   1: 1 when the asked actor's family is in the zero-ended family list (applicants)
    //   2: 1 while the sixth entry (children[5]) is 0
    //   3: the family of list entry `pick` is added to the applicants, the entry removed (list shifted down)
    //   4: 1 while the first entry's building type (children[0].familyType) is 0
    //   5: 0 when one of the list's entries from the second on has no flag byte, checked once more than there is room
    //      for applicants, else 1
    //   6: the families of all list entries are added to the applicants
    int EvAdoption_Answer(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        const GuestPtr<EvAdoptionData> adoption = data.as<EvAdoptionData>();
        // The first family at or after the entry's table family that the city lets apply.
        const auto familyFor = [&](GuestPtr<AdoptionChild> child) {
            uint32_t family = globals::firstFamily(child->familyType);
            while (Family_CanApply(family & 0xFFFFu, city) == 0)
                family = (family + 1u) & 0xFFFFu;
            return family & 0xFFFFu;
        };
        switch (static_cast<uint32_t>(mode))
        {
        case 0:
        {
            bool orphanage = false, special = false;
            const GuestPtr<MapObject> first = Map_GetRecord(city->map, 0);
            for (GuestPtr<MapObject> object = first; object < first + kObjectSlots; ++object)
            {
                const uint32_t type = object->type;
                if (!orphanage && type == 4)
                {
                    orphanage = true;
                    if (special)
                        break;
                }
                if (special)
                    continue;
                if ((globals::buildingTypeFlags(type) & 8u) != 0)
                {
                    special = true;
                    if (orphanage)
                        break;
                }
            }
            return !orphanage ? 1 : !special ? 0 : 2;
        }
        case 1:
            for (GuestPtr<int32_t> entry = GuestPtr<int32_t>::of(adoption->applicants[0]); *entry != 0; ++entry)
                if (*entry == actor->family)
                    return 1;
            return 0;
        case 2:
            return adoption->children[5].claim == 0 ? 1 : 0;
        case 3:
        {
            const GuestPtr<AdoptionChild> children = GuestPtr<AdoptionChild>::of(adoption->children[0]);
            const int32_t index = adoption->pick;
            const GuestPtr<AdoptionChild> child = children + index;
            CityScene_AddApplicantFamily(city, familyFor(child), 1);
            sdk::memmove(child.address, (child + 1).address, (6 - index) * 4); // the later children move up
            return 0;
        }
        case 4:
            return adoption->children[0].familyType == 0 ? 1 : 0;
        case 5:
        {
            int32_t room = 0x20 - city->applicants->count;
            for (GuestPtr<AdoptionChild> child = GuestPtr<AdoptionChild>::of(adoption->children[1]); room >= 0; ++child, --room)
                if (child->claim == 0)
                    return 0;
            return 1;
        }
        case 6:
            for (GuestPtr<AdoptionChild> child = GuestPtr<AdoptionChild>::of(adoption->children[0]); child->claim != 0; ++child)
                CityScene_AddApplicantFamily(city, familyFor(child), 1);
            return 0;
        default:
            return 0;
        }
    }

    // Type 1x (shopping) answers the talk. The shop's record (ShoppingList, 0x28 bytes in the table at 0x1BDAB4, index shop)
    // lists four goods. Modes: 0 the count (goodsCount) less one; 1 how many of the goods the bag lacks; 2 to 5 whether
    // the bag lacks good 0 to 3; 6 adds the bill to the money, 7 takes it, 8 takes the bill and refund; 9 moves the goods found in
    // the bag out of it into the money counter refund (their prices).
    int EvShopping_Answer(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef>, int mode)
    {
        const GuestPtr<EvShoppingData> shopping = data.as<EvShoppingData>();
        const GuestPtr<uint16_t> goods = GuestPtr<uint16_t>::of(globals::shoppingList(shopping->shop)->goods[0]);
        const auto bag = [&] { return city->bag; };
        switch (static_cast<uint32_t>(mode))
        {
        case 0:
            return shopping->goodsCount - 1;
        case 1:
        {
            int missing = 0;
            for (GuestPtr<uint16_t> entry = goods; *entry != 0 && entry < goods + 4; ++entry)
                if (Bag_FindItem(bag(), *entry) == 0)
                    ++missing;
            return missing;
        }
        case 2:
        case 3:
        case 4:
        case 5:
            return Bag_FindItem(bag(), goods[static_cast<int32_t>(mode - 2)]) != 0 ? 0 : 1;
        case 6:
            changeMoney(shopping->bill);
            return 0;
        case 7:
            changeMoney(static_cast<int32_t>(0u - static_cast<uint32_t>(shopping->bill)));
            return 0;
        case 8:
            changeMoney(static_cast<int32_t>(0u - (static_cast<uint32_t>(shopping->bill) + static_cast<uint32_t>(shopping->refund))));
            return 0;
        case 9:
            for (GuestPtr<uint16_t> entry = goods; entry < goods + 4; ++entry)
            {
                const int found = Bag_FindItem(bag(), *entry);
                if (found == 0)
                    continue;
                const GuestPtr<BagSlot> slot{static_cast<uint32_t>(found)};
                const GuestPtr<GoodsRecord> record = globals::goodsAt(slot->group, slot->index);
                shopping->refund = static_cast<int32_t>(static_cast<uint32_t>(shopping->refund) + static_cast<uint32_t>(Goods_GetPrice(static_cast<int>(record.address))));
                Bag_RemoveSlot(bag(), slot);
            }
            return 0;
        default:
            return 0;
        }
    }

    // The farewell party's end: the city's overlay (the party object) is destroyed (vtable +8, flag 1) and the party
    // building loses its event mark.
    void EvFarewellParty_End(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvPartyData> record = data.as<EvPartyData>();
        const uint32_t party = city->overlay.address;
        if (party != 0)
        {
            port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(party) + 8u), party, 1);
            city->overlay = GuestPtr<void>{0u};
        }
        Map_GetRecord(city->map, record->actors[1].object)->flags &= 0xFDFFu;
    }

    PORT_FN(Event_CallAnswer);
    PORT_FN(Event_CallText);
    PORT_FN(Event_CallHandlerSlot);
    PORT_FN(EvUselessMiddleAged_Answer);
    PORT_FN(EvPayday_Text);
    PORT_FN(EvDeliveryMan_Answer);
    PORT_FN(EvCrazyCult_Answer);
    PORT_FN(EvAdoption_Answer);
    PORT_FN(EvShopping_Answer);
    PORT_FN(EvFarewellParty_End);

    // ---- starting, ending and saving events (batch 40)

    namespace
    {
        // A queued event's entry (type, wait counter) becomes the running event `talk`: its type, the type's days,
        // var 0, the Update slot of its handler and, for a person, the first actor (person index, home, family,
        // member).
        void fillEvent(GuestPtr<EventData> talk, uint32_t entry, GuestPtr<MapPerson> person)
        {
            const uint8_t type = port::load<uint8_t>(entry);
            const GuestPtr<EventType> row = globals::eventType(type);
            talk->kind = type;
            talk->days = row->days;
            talk->var = 0;
            const uint32_t id = row->handler;
            talk->update = id == 0 ? 0u : eventHandler(id)->update;
            if (person)
            {
                talk->actor.person = person->index;
                talk->actor.object = person->home;
                talk->actor.family = person->family;
                talk->actor.member = person->member;
            }
        }

        // The running event's size (vtable +8) of its handler class, for clearing it.
        int eventSize(uint32_t talk) { return port::callGuest<int>(port::load<uint32_t>(port::load<uint32_t>(talk) + 8u), talk); }

        // The event `talk` of type `row` has its handler's `slot` called with (event, city, third), if it has one.
        void callHandler(GuestPtr<EventType> row, uint32_t EventHandler::*slot, GuestPtr<EventData> talk, GuestPtr<CityScene> city, uint32_t third)
        {
            const uint32_t id = row->handler;
            if (id == 0)
                return;
            const uint32_t function = (*eventHandler(id)).*slot;
            if (function != 0)
                port::callGuest<void>(function, talk, city, third);
        }

        // The event type's own ending (its handler's End slot), its claims removed, and dropped by the buildings
        // that had chosen them.
        void endEventHandler(GuestPtr<EventType> row, GuestPtr<EventData> talk, GuestPtr<CityScene> city)
        {
            callHandler(row, &EventHandler::end, talk, city, talk->kind); // a2 is the type byte the original had just loaded
            const uint32_t claimList = row->claimList;
            if (claimList != 0)
            {
                Event_RemoveClaims(static_cast<int>(claimList) - 1);
                Event_DropRemovedClaims(city);
            }
        }

        // The finished event leaves the manager: its id goes back to the queue at `level` and its data is cleared.
        void returnEvent(uint32_t cell, uint32_t level)
        {
            EventQueue_InsertByPriority(globals::eventQueue(), GuestPtr<uint8_t>::of(GuestPtr<EventData>{port::load<uint32_t>(cell)}->kind).as<EventQueueEntry>(),
                                        static_cast<int>(level), 0);
            const uint32_t talk = port::load<uint32_t>(cell);
            sdk::memset(talk + 8u, 0, eventSize(talk));
        }
    }

    // The event queue, its object (0x224 bytes), the manager (0xA4 bytes) and the two running events' data go to the
    // memory card, or come back from it.
    void Event_LoadState(GuestPtr<EventMgr> events, GuestPtr<McManager> mc)
    {
        SaveFile_Read(mc, static_cast<int>(globals::kEventTypeIds), 0x200);
        SaveFile_Read(mc, static_cast<int>(globals::eventQueueObject.get()), 0x224);
        SaveFile_Read(mc, static_cast<int>(events.address), 0xA4);
        for (uint32_t i = 0; i < 2; ++i)
        {
            const uint32_t talk = events->talks[i].address;
            const int size = eventSize(talk);
            SaveFile_Read(mc, static_cast<int>(events->talks[i].address + 8u), size);
        }
    }

    void Event_SaveState(GuestPtr<EventMgr> events, GuestPtr<McManager> mc)
    {
        SaveFile_Write(mc, static_cast<int>(globals::kEventTypeIds), 0x200);
        SaveFile_Write(mc, static_cast<int>(globals::eventQueueObject.get()), 0x224);
        SaveFile_Write(mc, static_cast<int>(events.address), 0xA4);
        for (uint32_t i = 0; i < 2; ++i)
        {
            const uint32_t talk = events->talks[i].address;
            const int size = eventSize(talk);
            SaveFile_Write(mc, static_cast<int>(events->talks[i].address + 8u), size);
        }
    }

    // The talk event in slot `index` is cut short: its handler ends it, and it goes back to the waiting queue (level 3
    // for a type with level 4) as it was.
    void Event_Abort(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, int index)
    {
        const uint32_t cell = port::guest(&events->talks[index]);
        const GuestPtr<EventData> talk{port::load<uint32_t>(cell)};
        const GuestPtr<EventType> row = globals::eventType(talk->kind);
        endEventHandler(row, talk, city);
        uint32_t level = row->level;
        if (level == 4)
            level = 3;
        returnEvent(cell, level);
    }

    // The talk event in slot `index` finishes. Its type's wait goes into its days and says where it queues next: above
    // 0 level 4, 0 its own level (3 for a type with level 4), -1 level 6, -2 level 7, any other value leaves the level
    // register as the caller had it.
    void Event_Finish(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, int index)
    {
        const uint32_t cell = port::guest(&events->talks[index]);
        const GuestPtr<EventData> talk{port::load<uint32_t>(cell)};
        const GuestPtr<EventType> row = globals::eventType(talk->kind);
        endEventHandler(row, talk, city);
        const int8_t next = row->wait;
        GuestPtr<EventData>{port::load<uint32_t>(cell)}->days = next;
        uint32_t level = static_cast<uint32_t>(port::guestS1());
        if (next > 0)
            level = 4;
        else if (next == 0)
        {
            level = row->level;
            if (level == 4)
                level = 3;
        }
        else if (next == -1)
            level = 6;
        else if (next == -2)
            level = 7;
        returnEvent(cell, level);
    }

    // With no event running in the first talk slot, the first event waiting at level 3 whose condition holds for the
    // person starts there; one that starts at level 3 also adds its claims and runs its Start slot. The result is
    // the running event's type.
    uint8_t Event_TryStart(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        if (events->talks[0]->kind == 0)
        {
            const GuestPtr<EventQueue> queue = globals::eventQueue();
            const uint32_t end = bucketBegin(queue, 4);
            for (uint32_t entry = bucketBegin(queue, 3); entry < end; entry += 2u)
            {
                if (Event_CheckCondition(events, city, GuestPtr<uint8_t>{entry}, person) == 0)
                    continue;
                fillEvent(events->talks[0], entry, person);
                EventQueue_RemoveAt(queue, 3, GuestPtr<EventQueueEntry>{entry});
                const GuestPtr<EventData> talk{events->talks[0].address};
                const GuestPtr<EventType> row = globals::eventType(talk->kind);
                if (row->level == 3)
                {
                    const uint32_t claimList = row->claimList;
                    if (claimList != 0)
                        Event_AddClaims(static_cast<int>(claimList) - 1);
                    events->talks[0]->var = 1;
                    callHandler(row, &EventHandler::start, events->talks[0], city, person.address);
                }
                break;
            }
        }
        return events->talks[0]->kind;
    }

    // A talk script starts an event for the person: with fewer than five events running and none of them waiting on the
    // person's character type (the type's charTypes[0]), the first event waiting at level 2 whose condition holds
    // takes the first running slot (the others move up), with the person as actor A and the actor of the third talk event (talks[2]) as B.
    int Event_StartFromScript(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        const GuestPtr<EventSlot> slots = events.field(&EventMgr::slots), slotsEnd = slots + 5;
        int running = 0;
        for (GuestPtr<EventSlot> slot = slots; slot->kind != 0 && slot < slotsEnd; ++slot)
            ++running;
        if (running >= 5)
            return 0;
        const uint32_t types = globals::eventTypes.get();
        for (GuestPtr<EventSlot> slot = slots; slot->kind != 0 && slot < slotsEnd; ++slot)
            if (person->charType == (GuestPtr<EventType>{types} + slot->kind)->charTypes[0])
                return 0;
        const GuestPtr<EventQueue> queue = globals::eventQueue();
        uint32_t entry = bucketBegin(queue, 2);
        const uint32_t end = bucketBegin(queue, 3);
        if (entry == end)
            return 0;
        do
        {
            if (Event_CheckCondition(events, city, GuestPtr<uint8_t>{entry}, person) != 0)
            {
                sdk::memmove((slots + 1).address, slots.address, 0x80);
                sdk::memset(slots.address, 0, 0x20);
                queueEntry(entry)->wait = 3;
                events->slots[0].kind = queueEntry(entry)->type;
                events->slots[0].stage = queueEntry(entry)->wait;
                events->slots[0].familyA = person->family;
                events->slots[0].memberA = person->member;
                const GuestPtr<EventData> other = events->talks[2];
                events->slots[0].familyB = other->actor.family;
                events->slots[0].memberB = other->actor.member;
                const uint32_t talk = events->talks[2].address;
                sdk::memset(talk + 8u, 0, eventSize(talk));
                EventQueue_RemoveAt(queue, 2, GuestPtr<EventQueueEntry>{entry});
                return 1;
            }
            entry += 2u;
        } while (entry != end);
        return 0;
    }

    // An event id (the position in the event list of the loaded EVENT.BIN) is triggered from outside (a script, a talk):
    // the waiting entries of that id in bucket 5 are tried; the first whose condition holds takes the talk slot (1 for
    // id 6, else 0) when that is free or its event has a higher priority value (it is aborted for slot 1, else finished
    // first), runs its Start slot and the call returns 1.
    int Event_Trigger(GuestPtr<EventMgr> events, int16_t id, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        const uint32_t index = static_cast<uint8_t>(id) == 6 ? 1u : 0u;
        uint32_t position = 0;
        const GuestPtr<uint16_t> list{globals::kEventTypeIds}, listEnd = list + globals::eventTypeCount.get();
        if (list < listEnd)
        {
            GuestPtr<uint16_t> at = list;
            do
            {
                if (*at == static_cast<uint16_t>(id))
                    break;
                ++at;
                ++position;
            } while (at < listEnd);
        }
        const GuestPtr<EventQueue> queue = globals::eventQueue();
        uint32_t entry = bucketBegin(queue, 5);
        const uint32_t end = bucketBegin(queue, 6);
        if (!(entry < end))
            return 0;
        const uint32_t cell = port::guest(&events->talks[index]);
        do
        {
            if (port::load<uint8_t>(entry) == position && Event_CheckCondition(events, city, GuestPtr<uint8_t>{entry}, person) != 0)
            {
                const uint8_t running = GuestPtr<EventData>{port::load<uint32_t>(cell)}->kind;
                bool start = true;
                if (running != 0)
                {
                    const uint32_t types = globals::eventTypes.get();
                    const int8_t newPriority = (GuestPtr<EventType>{types} + port::load<uint8_t>(entry))->priority;
                    const int8_t oldPriority = (GuestPtr<EventType>{types} + running)->priority;
                    if (newPriority < oldPriority)
                    {
                        if (index == 1)
                            Event_Abort(events, city, 1);
                        else
                            Event_Finish(events, city, static_cast<int>(index));
                        if (port::load<uint8_t>(entry) != position)
                            entry += 2u;
                    }
                    else
                        start = false;
                }
                if (start)
                {
                    fillEvent(GuestPtr<EventData>{port::load<uint32_t>(cell)}, entry, person);
                    EventQueue_RemoveAt(queue, 5, GuestPtr<EventQueueEntry>{entry});
                    const GuestPtr<EventType> row = globals::eventType(GuestPtr<EventData>{port::load<uint32_t>(cell)}->kind);
                    const uint32_t claimList = row->claimList;
                    if (claimList != 0)
                        Event_AddClaims(static_cast<int>(claimList) - 1);
                    GuestPtr<EventData>{port::load<uint32_t>(cell)}->var = 1;
                    callHandler(row, &EventHandler::start, GuestPtr<EventData>{port::load<uint32_t>(cell)}, city, person.address);
                    return 1;
                }
            }
            entry += 2u;
        } while (entry < end);
        return 0;
    }

    PORT_FN(Event_LoadState);
    PORT_FN(Event_SaveState);
    PORT_FN(Event_Abort);
    PORT_FN(Event_Finish);
    PORT_FN(Event_TryStart);
    PORT_FN(Event_StartFromScript);
    PORT_FN(Event_Trigger);

    // ---- the event scheduler (batch 40)

    // Whether the event type at `type` (a queue entry) may start now, for the person (null for a script-less start).
    // The checks, in order, all on the type's record (EventType):
    //   the start window against the minute counter (not for the levels 1 and 5), the population, the laptop modules,
    //   and for a person: the character types (one for level 2, else four), the gender, the friendship band, the claim
    //   meter of the home, the home types, and where the person stands (place: 1 in its home, 2 in its
    //   target, 3 away from the building it is between, 4 outside its home at least 8 from its middle), then the
    //   score tiers and the approval tier, and finally the handler's own Check slot (the handler of level 2 and of
    //   types of group 6 pass the actor of the talk event 2 or 1, else the first).
    int64_t Event_CheckCondition(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, GuestPtr<uint8_t> type, GuestPtr<MapPerson> person)
    {
        // The original's frame: the vectors keep their leftover y and w lanes.
        struct Locals
        {
            uint8_t gap_00[0x70];
            Vec4 middle;               // +0x70 the home's middle (x and z written)
            Vec4 self;                 // +0x80 the person (x and z written)
            Vec4 distance;             // +0x90 the distance between them, in every lane
        };
        static_assert(offsetof(Locals, middle) == 0x70 && offsetof(Locals, self) == 0x80 && offsetof(Locals, distance) == 0x90 &&
                      sizeof(Locals) == 0xA0);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<EventType> row = globals::eventType(*type);
        const uint32_t level = row->level;
        if (level != 1 && level != 5)
        {
            const uint32_t start = row->startFrom, end = row->startUntil, now = events->minute;
            if (end < start)
            {
                if (end <= now && now < start)
                    return 0;
            }
            else if (now < start || end <= now)
                return 0;
        }
        if (city->map->population < static_cast<int32_t>(row->population))
            return 0;
        const uint32_t modules = row->modules;
        if (modules != (row->modules & Laptop_CountActive()))
            return 0;
        if (person)
        {
            if (row->charTypes[0] != 0)
            {
                const uint32_t listed = level == 2 ? 1u : 4u;
                bool found = false;
                for (uint32_t i = 0; i < listed && row->charTypes[i] != 0; ++i)
                    if (row->charTypes[i] == person->charType)
                    {
                        found = true;
                        break;
                    }
                if (!found)
                    return 0;
            }
            if (row->gender != 0 && static_cast<int32_t>(row->gender) != (static_cast<int16_t>(person->flags) & 3) + 1)
                return 0;
            if (row->friendship != 0)
            {
                const int band = Family_GetFriendshipBand(person->family, person->member);
                if (row->friendship == 1 && band < 1)
                    return 0;
            }
            const GuestPtr<MapObject> home = Map_GetRecord(city->map, person->home);
            if (row->claimMeter != 0)
            {
                const int meter = ClaimMeter_Level(home->claimMeter);
                if (row->claimMeter == 1 && meter != 0)
                    return 0;
            }
            if (row->homeTypes[0] != 0)
            {
                bool found = false;
                for (uint32_t i = 0; i < 4 && row->homeTypes[i] != 0; ++i)
                    if (row->homeTypes[i] == home->type)
                    {
                        found = true;
                        break;
                    }
                if (!found)
                    return 0;
            }
            if (row->place != 0)
            {
                const int tileX = sdk::fptosi(ps2f(64.0f) + ps2f(person->x));
                const int tileZ = sdk::fptosi(ps2f(64.0f) - ps2f(person->z));
                const GuestPtr<MapObject> here = Map_FindPersonObjectOnTile(city->map, person->index, static_cast<uint32_t>(tileX + (tileZ << 7)) & 0xFFFFu);
                if (!here)
                    return 0;
                const GuestPtr<BuildingType> hereType{globals::buildingTypeAddress(here->type)};
                const bool hasEntrance = (hereType->flags & 0x20u) != 0;
                const auto entranceAnswer = [&] { return port::callGuest<int64_t>(hereType->vtable->isOpen, hereType, city, here); };
                switch (row->place)
                {
                case 1:
                    if (here->id != person->home)
                        return 0;
                    if (person->home == person->target && hasEntrance && entranceAnswer() != 0)
                        return 0;
                    break;
                case 2:
                    if (here->id != person->target)
                        return 0;
                    if (hasEntrance && entranceAnswer() == 0)
                        return 0;
                    break;
                case 3:
                    if (person->inside == kNoObject || person->destination == kNoObject || here->id == person->inside || here->id == person->destination)
                        return 0;
                    break;
                case 4:
                {
                    if (person->inside != person->home || here->id == person->inside)
                        return 0;
                    using namespace vu0;
                    const ps2f size = fromInt(GuestPtr<BuildingType>{globals::buildingTypeAddress(home->type)}->size);
                    const uint32_t tile = home->tile;
                    frame->self.x = person->x;
                    frame->middle.x = (fromInt(static_cast<int32_t>(tile & 0x7F)) - ps2f(64.0f)) + size / ps2f(2.0f);
                    frame->self.z = person->z;
                    frame->middle.z = (fromInt(0x80 - static_cast<int32_t>(tile >> 7)) - ps2f(64.0f)) - size / ps2f(2.0f);
                    vf(4) = load(frame.at(&Locals::self));
                    vf(5) = load(frame.at(&Locals::middle));
                    set(vf(6), sub(vf(5), vf(4)), XYZW);
                    set(vf(7), mul(vf(6), vf(6)), XYZ);
                    set(vf(8), add(vf(7), lane(vf(7), 2)), X);
                    q() = sqrtQ(x(vf(8)));
                    set(vf(9), add(vf(0), _mm_set1_ps(q())), XYZW);
                    store(frame.at(&Locals::distance), vf(9));
                    if (frame->distance.x < ps2f(8.0f))
                        return 0;
                    break;
                }
                default:
                    return 0;
                }
            }
        }
        if (row->prosperityTier != 0 && static_cast<int>(row->prosperityTier) - 1 != CityScene_ProsperityTier(city))
            return 0;
        if (row->conductTier != 0 && static_cast<int>(row->conductTier) - 1 != CityScene_ConductTier(city))
            return 0;
        if (row->approvalTier != 0 && static_cast<int>(row->approvalTier) - 1 != CityScene_ApprovalTier(city))
            return 0;
        const uint32_t handler = row->handler;
        if (handler == 0)
            return 1;
        const uint32_t which = level == 2 ? 2u : row->group == 6 ? 1u : 0u;
        const uint32_t check = eventHandler(handler)->check;
        if (check == 0)
            return 1;
        return port::callGuest<int64_t>(check, events->talks[which], city, person);
    }

    namespace
    {
        // The talk event `cell` takes the first waiting entry of bucket `bucket` whose condition holds (no person):
        // the entry leaves the bucket. Returns whether one did.
        bool startFromBucket(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, uint32_t cell, int bucket)
        {
            const GuestPtr<EventQueue> queue = globals::eventQueue();
            const uint32_t end = bucketBegin(queue, bucket + 1);
            for (uint32_t entry = bucketBegin(queue, bucket); entry < end; entry += 2u)
            {
                if (Event_CheckCondition(events, city, GuestPtr<uint8_t>{entry}, GuestPtr<MapPerson>{0}) == 0)
                    continue;
                fillEvent(GuestPtr<EventData>{port::load<uint32_t>(cell)}, entry, GuestPtr<MapPerson>{0});
                EventQueue_RemoveAt(queue, bucket, GuestPtr<EventQueueEntry>{entry});
                return true;
            }
            return false;
        }

        // A running event that has not begun (var 0) begins when its start time has come: its claims are added and
        // its Start slot runs (with the person slot 0).
        void beginEvent(GuestPtr<EventType> row, GuestPtr<EventData> talk, GuestPtr<CityScene> city)
        {
            const uint32_t claimList = row->claimList;
            if (claimList != 0)
                Event_AddClaims(static_cast<int>(claimList) - 1);
            talk->var = 1;
            callHandler(row, &EventHandler::start, talk, city, 0);
        }
    }

    // The scheduler, called every frame; it does nothing until the city clock's minute changes (minute), so the
    // events' Update slots run once a game minute (every 20 frames by day, every other one before 6:00,
    // mm_CityScene_TickClock) and the counters the event records keep count minutes. When the clock wraps to a new
    // day, the waiting lists are reorganised (the population rank, the due waits) and the running events' counters
    // tick; every minute of the day (at the hour) the first fitting waiting entry takes the second talk slot, and when
    // the first talk slot is free one from the next bucket, or a postponed event from the running list, takes it.
    // Last, each talk event begins when its start time has come, ends when its end time has, or updates.
    void Event_Update(GuestPtr<EventMgr> events, GuestPtr<CityScene> city)
    {
        const GuestPtr<EventQueue> queue = globals::eventQueue();
        const uint32_t types = globals::eventTypes.get();
        const int32_t minute = city->clock / 60;
        const uint32_t counter = events->minute;
        if (static_cast<uint32_t>(minute) == counter)
            return;
        const uint32_t cellA = port::guest(&events->talks[0]), cellB = port::guest(&events->talks[1]);
        const auto talkKind = [&](uint32_t cell) { return GuestPtr<EventData>{port::load<uint32_t>(cell)}->kind; };
        port::GuestFrame<0x90> frame;
        const uint32_t ctx = frame.ptr().address;
        // The tests handed to the queue's moves, where the original kept them in its frame.
        const GuestPtr<EventTest> outOfRank{ctx + 0x60u}, inRankLevel{ctx + 0x68u}, inRank{ctx + 0x78u}, dueLevel{ctx + 0x80u},
            due{ctx + 0x8Cu};
        if (!((static_cast<uint32_t>(minute) & 0xFFFFu) < counter))
        {
            // The same day: the counter follows the clock; on the hour a second event may start.
            events->minute = static_cast<uint16_t>(minute);
            if (events->minute % 60 == 0 && talkKind(cellB) == 0)
            {
                const uint8_t running = talkKind(cellA);
                if (running == 0 || (GuestPtr<EventType>{types} + running)->group != 3)
                    startFromBucket(events, city, cellB, 0);
            }
        }
        else
        {
            events->minute = static_cast<uint16_t>(minute);
            const uint32_t rank = events->rank;
            if (static_cast<int32_t>(rank) < CityScene_PopulationRank(city))
            {
                events->rank = static_cast<uint8_t>(CityScene_PopulationRank(city));
                const auto mask = [&] { return static_cast<int32_t>(1u << ((events->rank - 1u) & 0x1Fu)); };
                outOfRank->types = GuestPtr<EventType>{types};
                outOfRank->ranks = mask();
                for (int bucket = 0; bucket < 4; ++bucket)
                    EventList_MoveOutOfRank(queue, bucket, 6, outOfRank);
                EventList_MoveOutOfRank(queue, 4, 6, outOfRank);
                for (int to = 0; to < 3; ++to)
                {
                    inRankLevel->types = GuestPtr<EventType>{types};
                    inRankLevel->ranks = mask();
                    inRankLevel->rankLevel = to;
                    EventList_MoveInRankLevel(queue, 5, to, inRankLevel);
                }
                inRank->types = GuestPtr<EventType>{types};
                inRank->ranks = mask();
                EventList_MoveInRank(queue, 5, 3, inRank);
            }
            // The entries waiting in bucket 4 count their wait down.
            for (uint32_t entry = bucketBegin(queue, 4); entry != bucketBegin(queue, 5); entry += 2u)
                queueEntry(entry)->wait = static_cast<int8_t>(queueEntry(entry)->wait - 1);
            for (int to = 0; to < 3; ++to)
            {
                dueLevel->types = GuestPtr<EventType>{globals::eventTypes.get()};
                dueLevel->level = to;
                EventList_MoveDueLevel(queue, 4, to, dueLevel);
            }
            due->types = GuestPtr<EventType>{globals::eventTypes.get()};
            EventList_MoveDue(queue, 4, 3, due);
            // The postponed events (the running list) count down and return to bucket 4 when they run out.
            const uint32_t slotsEnd = events.address + 4u + 0xA0u;
            for (uint32_t slot = events.address + 4u; port::load<uint8_t>(slot) != 0 && slot < slotsEnd;)
            {
                const GuestPtr<EventSlot> postponed{slot};
                const int8_t left = static_cast<int8_t>(postponed->stage - 1);
                postponed->stage = left;
                if (left > 0)
                {
                    slot += 0x20u;
                    continue;
                }
                postponed->stage = globals::eventType(postponed->kind)->wait;
                EventQueue_InsertByPriority(queue, GuestPtr<EventQueueEntry>{slot}, 4, 0);
                const uint32_t last = (events.field(&EventMgr::slots) + 4).address;
                if (slot < last)
                    sdk::memmove(slot, slot + 0x20u, (static_cast<int32_t>(last - slot) >> 5) << 5);
                sdk::memset(last, 0, 0x20);
            }
            // The second talk event: while it runs its days count down (and finish it at 0); when free, one may start.
            const GuestPtr<EventData> talkB{port::load<uint32_t>(cellB)};
            bool talkBDone = false;
            if (talkB->kind != 0)
            {
                const int8_t counted = talkB->days;
                if (counted > 0)
                {
                    talkB->days = static_cast<int8_t>(counted - 1);
                    if (!(static_cast<int8_t>(counted - 1) > 0))
                        Event_Finish(events, city, 1);
                }
                talkBDone = true;
            }
            if (!talkBDone)
            {
                const uint8_t running = talkKind(cellA);
                if (running == 0 || (GuestPtr<EventType>{types} + running)->group != 3)
                    startFromBucket(events, city, cellB, 0);
            }
            // The first talk event: while it runs the same countdown; when free, one from the next bucket, else a
            // postponed event with a timer of type level 4 takes it (the last in the list first).
            const GuestPtr<EventData> talkA{port::load<uint32_t>(cellA)};
            if (talkA->kind == 0)
            {
                if (!startFromBucket(events, city, cellA, 1))
                {
                    const GuestPtr<EventData> now{port::load<uint32_t>(cellA)};
                    if (now->kind == 0)
                    {
                        const uint32_t first = events.field(&EventMgr::slots).address;
                        for (uint32_t slot = (events.field(&EventMgr::slots) + 4).address; slot >= first; slot -= 0x20u)
                        {
                            const uint8_t kind = port::load<uint8_t>(slot);
                            if (kind == 0 || GuestPtr<EventSlot>{slot}->joined[4].family == 0)
                                continue;
                            const GuestPtr<EventType> row = GuestPtr<EventType>{types} + kind;
                            if (row->group != 4)
                                continue;
                            now->kind = kind;
                            now->days = row->days;
                            now->var = 0;
                            const uint32_t handler = row->handler;
                            now->update = handler == 0 ? 0u : eventHandler(handler)->update;
                            // The slot's persons and joined residents become the actors.
                            GuestPtr<ActorRef> actor{port::load<uint32_t>(cellA) + 0xCu};
                            for (GuestPtr<MemberId> from{slot + 4u}; from.address < slot + 0x20u; ++from, ++actor)
                            {
                                actor->family = from->family;
                                actor->member = from->member;
                                actor->person = kNoPerson;
                                actor->object = kNoObject;
                            }
                            const uint32_t last = (events.field(&EventMgr::slots) + 4).address;
                            if (slot < last)
                                sdk::memmove(slot, slot + 0x20u, (static_cast<int32_t>(last - slot) >> 5) << 5);
                            sdk::memset(last, 0, 0x20);
                            beginEvent(GuestPtr<EventType>{types} + talkKind(cellA), GuestPtr<EventData>{port::load<uint32_t>(cellA)}, city);
                            break;
                        }
                    }
                }
            }
            else
            {
                const int8_t counted = talkA->days;
                if (counted > 0)
                {
                    talkA->days = static_cast<int8_t>(counted - 1);
                    if (!(static_cast<int8_t>(counted - 1) > 0))
                        Event_Finish(events, city, 0);
                }
            }
        }
        // Each talk event: one that has not begun (var 0) begins at its start time; a running one on its last day
        // finishes at its finish time; otherwise its Update slot runs.
        for (uint32_t i = 0; i < 2; ++i)
        {
            const GuestPtr<EventData> talk{events->talks[i].address};
            if (talk->kind == 0)
                continue;
            const GuestPtr<EventType> row = GuestPtr<EventType>{types} + talk->kind;
            const uint32_t now = events->minute;
            if (talk->var == 0)
            {
                if (!(now < row->startFrom))
                    beginEvent(row, events->talks[i], city);
            }
            else if (talk->days == 1 && !(now < row->finishAt))
                Event_Finish(events, city, static_cast<int>(i));
            else if (const uint32_t update = talk->update)
                port::callGuest<void>(update, talk, city);
        }
    }

    // After a game is loaded the event lists are matched with the event table of this build: the ids saved with the game
    // (the list at 0x1C5790, one halfword per type record) against the table's. A type that was not in the saved list is
    // put in the waiting queue (by its rank and level) and every stored type number from it on goes up by one; a saved
    // type no longer in the table loses its running event or queue entry and the numbers above it go down. A changed
    // list is stored again; last, the running events get their Update slot back (and the first its claims).
    void Event_InitForCity(GuestPtr<EventMgr> events)
    {
        port::GuestFrame<0x90> frame;
        const GuestPtr<EventQueue> queue = globals::eventQueue();
        const uint32_t types = globals::eventTypes.get();
        const uint32_t tableEnd = types + 0x4Cu * static_cast<uint32_t>(globals::eventTypeCount.get());
        const auto talkAt = [&](uint32_t i) { return events->talks[i].address; };
        const auto queueStart = [&] { return bucketBegin(queue, 0); };
        const auto queueEnd = [&] { return bucketBegin(queue, 8); };
        const uint32_t slotsEnd = events.address + 4u + 0xA0u;
        uint32_t saved = globals::kEventTypeIds;
        GuestPtr<EventType> record{types};
        int32_t index = 0;
        bool changed = false;
        while (record.address < tableEnd)
        {
            const uint32_t current = record->id, old = port::load<uint16_t>(saved);
            if (old == current)
            {
                ++record;
                ++index;
                saved += 2u;
                continue;
            }
            if (old > current && current != 0)
            {
                // A type new to the saved game goes into the queue and the numbers from its index up shift.
                const uint32_t rank = events->rank;
                const uint32_t mask = rank == 0 ? 0u : 1u << ((rank - 1u) & 0x1Fu);
                const uint32_t rankMask = record->ranks;
                uint32_t level = 6;
                if ((rankMask & mask & 0xFFu) == 0)
                {
                    if ((mask & 0xFFu) < rankMask)
                        level = 5;
                }
                else
                {
                    level = record->level;
                    if (level == 4)
                        level = 3;
                }
                const GuestPtr<EventQueueEntry> entry{frame.ptr().address + 0x8Cu};
                entry->type = static_cast<uint8_t>(index);
                entry->wait = record->wait;
                EventQueue_InsertByPriority(queue, entry, static_cast<int>(level), 0);
                for (uint32_t i = 0; i < 2; ++i)
                {
                    const uint32_t talk = talkAt(i);
                    const uint32_t kind = GuestPtr<EventData>{talk}->kind;
                    if (kind != 0 && static_cast<int32_t>(kind) >= index)
                        GuestPtr<EventData>{talk}->kind = static_cast<uint8_t>(kind + 1);
                }
                for (uint32_t slot = events.address + 4u; port::load<uint8_t>(slot) != 0 && slot < slotsEnd; slot += 0x20u)
                    if (static_cast<int32_t>(port::load<uint8_t>(slot)) >= index)
                        port::store<uint8_t>(slot, static_cast<uint8_t>(port::load<uint8_t>(slot) + 1));
                for (uint32_t at = queueStart(), end = queueEnd(); at < end; at += 2u)
                    if (static_cast<int32_t>(port::load<uint8_t>(at)) >= index)
                        port::store<uint8_t>(at, static_cast<uint8_t>(port::load<uint8_t>(at) + 1));
                ++record;
                ++index;
                changed = true;
                continue;
            }
            // A saved type that is no longer in the table: its running event, postponed event or queue entry goes.
            const GuestPtr<EventData> talkA{talkAt(0)}, talkB{talkAt(1)};
            if (index == talkA->kind)
                sdk::memset(talkA.address + 8u, 0, eventSize(talkA.address));
            else if (index == talkB->kind)
                sdk::memset(talkB.address + 8u, 0, eventSize(talkB.address));
            else
            {
                bool removed = false;
                for (uint32_t slot = events.address + 4u; port::load<uint8_t>(slot) != 0 && slot < slotsEnd; slot += 0x20u)
                {
                    if (static_cast<int32_t>(port::load<uint8_t>(slot)) != index)
                        continue;
                    const uint32_t last = (events.field(&EventMgr::slots) + 4).address;
                    if (slot < last)
                        sdk::memmove(slot, slot + 0x20u, (static_cast<int32_t>(last - slot) >> 5) << 5);
                    sdk::memset(last, 0, 0x20);
                    removed = true;
                    break;
                }
                for (int bucket = 0; !removed && bucket < 8; ++bucket)
                {
                    const uint32_t end = bucketBegin(queue, bucket + 1);
                    for (uint32_t at = bucketBegin(queue, bucket); at < end; at += 2u)
                        if (static_cast<int32_t>(port::load<uint8_t>(at)) == index)
                        {
                            EventQueue_RemoveAt(queue, bucket, GuestPtr<EventQueueEntry>{at});
                            removed = true;
                            break;
                        }
                }
            }
            for (uint32_t i = 0; i < 2; ++i)
            {
                const uint32_t talk = talkAt(i);
                const uint32_t kind = GuestPtr<EventData>{talk}->kind;
                if (kind != 0 && index < static_cast<int32_t>(kind))
                    GuestPtr<EventData>{talk}->kind = static_cast<uint8_t>(kind - 1);
            }
            for (uint32_t slot = events.address + 4u; port::load<uint8_t>(slot) != 0 && slot < slotsEnd; slot += 0x20u)
                if (index < static_cast<int32_t>(port::load<uint8_t>(slot)))
                    port::store<uint8_t>(slot, static_cast<uint8_t>(port::load<uint8_t>(slot) - 1));
            for (uint32_t at = queueStart(), end = queueEnd(); at < end; at += 2u)
                if (index < static_cast<int32_t>(port::load<uint8_t>(at)))
                    port::store<uint8_t>(at, static_cast<uint8_t>(port::load<uint8_t>(at) - 1));
            saved += 2u;
            changed = true;
        }
        if (changed)
        {
            sdk::memset(globals::kEventTypeIds, 0, 0x100);
            uint32_t out = globals::kEventTypeIds;
            for (uint32_t at = types, end = types + 0x4Cu * static_cast<uint32_t>(globals::eventTypeCount.get()); at < end; at += 0x4Cu, out += 2u)
            {
                port::store<uint8_t>(out, GuestPtr<EventType>{at}->group);
                port::store<uint8_t>(out + 1u, GuestPtr<EventType>{at}->number);
            }
        }
        for (uint32_t i = 0; i < 2; ++i)
        {
            const uint32_t talk = talkAt(i);
            const uint32_t kind = GuestPtr<EventData>{talk}->kind;
            if (kind == 0)
                continue;
            const GuestPtr<EventType> row = globals::eventType(kind);
            const uint32_t handler = row->handler;
            GuestPtr<EventData>{talk}->update = handler == 0 ? 0u : eventHandler(handler)->update;
            if (i == 0 && GuestPtr<EventData>{talkAt(0)}->var != 0 && row->claimList != 0)
                Event_AddClaims(static_cast<int>(row->claimList) - 1);
        }
    }

    PORT_FN(Event_CheckCondition);
    PORT_FN(Event_Update);
    PORT_FN(Event_InitForCity);

    // ---- the shopping event's start (batch 40)

    // A person at a shop (a building of category 5) starts a shopping trip: the person is the first actor and the shop
    // record (0x28 bytes each in the table at 0x1BDAB4, taken in turn: counter 0x1BDD5C) lists up to four goods (ShoppingList
    // goods; the record number is stored in shop and the goods counted in goodsCount). The buildings round the shop (a
    // square 30 tiles wider than its footprint) that are shops with an entrance and neither flag 0x240 are searched, in
    // the order of the map's 80 tile lists, for a resident family that sells the wanted goods this season: each item found
    // takes an actor slot in the event data (actors[item]: the person index, the shop as object, the family), its price
    // goes to prices[item] and the total to bill. When every item has a seller the total is rounded up to the next thousand
    // and the event starts (1); when the whole map has been searched without that the event data is cleared (0).
    int EvShopping_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        const GuestPtr<EvShoppingData> shopping = data.as<EvShoppingData>();
        struct Locals
        {
            uint8_t gap_000[0x110];
            TileRect footprint;        // +0x110 a shop's footprint
            uint16_t wanted[4];        // +0x120 the goods still without a seller (0 once found)
            uint32_t count;            // +0x128 the tile list's length
            int32_t offset;            // +0x12C the goods record offset: the pass, advanced by mm_Goods_GetRecordOffset
        };
        static_assert(offsetof(Locals, footprint) == 0x110 && offsetof(Locals, wanted) == 0x120 &&
                      offsetof(Locals, count) == 0x128 && offsetof(Locals, offset) == 0x12C && sizeof(Locals) == 0x130);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<MapObject> home = Map_GetRecord(city->map, person->home);
        const GuestPtr<BuildingType> homeType{globals::buildingTypeAddress(home->type)};
        if (homeType->category != 5) // homes
            return 0;
        data->actor.person = person->index;
        data->actor.object = person->home;
        data->actor.family = person->family;
        data->actor.member = person->member;
        const int32_t serial = globals::shoppingSerial.get();
        shopping->shop = serial;
        const GuestPtr<ShoppingList> list = globals::shoppingList(serial);
        globals::shoppingSerial.set(serial + 1);
        if (!(serial + 1 < globals::goodsTextCount.get()))
            globals::shoppingSerial.set(0);
        sdk::memcpy(frame.at(&Locals::wanted).address, GuestPtr<uint16_t>::of(list->goods[0]).address, 8);
        int remaining = 0;
        while (remaining < 4 && frame->wanted[remaining] != 0)
            ++remaining;
        shopping->goodsCount = remaining;
        // The area shops are searched in: the home's tile, its size and 30 tiles more on every side.
        const int32_t tile = home->tile, homeSize = homeType->size;
        const TileRect area{(tile & 0x7F) - 30, (tile >> 7) - 30, (tile & 0x7F) + homeSize + 30, (tile >> 7) + homeSize + 30};
        for (int list = 0; list < 0x50; ++list)
        {
            const GuestPtr<uint16_t> items = Map_GetTileList(city->map, list, frame.at(&Locals::count));
            const GuestPtr<uint16_t> end = items + static_cast<int32_t>(frame->count);
            for (GuestPtr<uint16_t> at = items; at < end; ++at)
            {
                const GuestPtr<MapObject> obj = Map_GetRecord(city->map, *at);
                if (globals::goodsGroup(obj->type)->variants == 0) // sells nothing
                    continue;
                if ((obj->flags & 0x20u) == 0 || (obj->flags & 0x240u) != 0)
                    continue;
                Building_GetFootprint(obj, 0, frame.at(&Locals::footprint));
                if (!overlap(area, frame->footprint))
                    continue;
                if (!Map_FindPersonEntry(city->map, static_cast<int16_t>(obj->family), 0))
                    continue;
                for (int pass = 0; pass < 4; ++pass)
                {
                    frame->offset = pass;
                    const int32_t month = remainder(city->monthCount, 12);
                    const int goods = Goods_GetRecordOffset(obj->type, obj->family, (month < 0 ? month + 3 : month) >> 2,
                                                            frame.at(&Locals::offset));
                    for (int k = 0; k < 4; ++k)
                    {
                        const uint32_t wanted = frame->wanted[k];
                        if (wanted == 0 || wanted != port::load<uint16_t>(static_cast<uint32_t>(goods)))
                            continue;
                        frame->wanted[k] = 0;
                        const GuestPtr<ActorRef> slot = GuestPtr<ActorRef>::of(shopping->actors[0]) + (k + 1); // the seller
                        slot->object = obj->id;
                        slot->family = obj->family;
                        slot->member = 0;
                        const GuestPtr<MapPerson> seller = Map_FindPersonEntry(city->map, static_cast<int16_t>(slot->family), slot->member);
                        slot->person = seller->index;
                        shopping->prices[k] = Goods_GetPrice(goods);
                        --remaining;
                        shopping->bill = static_cast<int32_t>(static_cast<uint32_t>(shopping->bill) + static_cast<uint32_t>(Goods_GetPrice(goods)));
                        if (remaining <= 0)
                        {
                            const uint32_t thousands = static_cast<uint32_t>(shopping->bill / 1000) + 1u;
                            shopping->bill = static_cast<int32_t>(thousands * 1000u);
                            return 1;
                        }
                    }
                }
            }
        }
        sdk::memset(data.address + 8u, 0, eventSize(data.address));
        return 0;
    }

    PORT_FN(EvShopping_Check);

    // ---- Event manager and event types (plan P6 batch 1) ----

    namespace
    {
        // A new event object of the base class (vtable 0x1BD908), its data from +8 cleared to the size its class reports.
        // Returns the object (0 when the allocation failed, nothing is done then).
        uint32_t newEventObject(uint32_t size, bool derived)
        {
            const uint32_t object = sdk::operator_new(size);
            if (object == 0u)
                return 0u;
            port::store<uint32_t>(object, globals::kTalkEventBaseVtable);
            port::store<uint8_t>(object + 8u, 0);
            port::store<uint8_t>(object + 9u, 0);
            const uint32_t dataSize = port::callGuest<uint32_t>(port::load<uint32_t>(port::load<uint32_t>(object) + 8u), object);
            sdk::memset(object + 8u, 0, static_cast<int>(dataSize));
            if (derived)
            {
                // The talk event the script works with: the derived class (vtable 0x1BD8F8) with 0x60 more bytes cleared.
                port::store<uint32_t>(object, globals::kTalkEventVtable);
                sdk::memset(object + 0x24u, 0, 0x60);
            }
            return object;
        }
    }

    // Clears the five event slots, creates the two talk events and the extra event object, and (from stage 0 on) moves the
    // events the stage allows from the queue's level 6 to level 5.
    GuestPtr<EventMgr> EventMgr_ctor(GuestPtr<EventMgr> events)
    {
        const uint32_t manager = events.address;
        port::store<uint16_t>(manager, 0x5A0);
        port::store<uint8_t>(manager + 2u, 0);
        for (uint32_t slot = manager + 4u; slot != manager + 0xA4u; slot += 0x20u)
        {
            port::store<uint8_t>(slot, 0);
            port::store<uint8_t>(slot + 1u, 0);
            for (const uint32_t half : {4u, 6u, 8u, 0xAu})
                port::store<uint16_t>(slot + half, 0);
            for (uint32_t at = slot + 0xCu; at != slot + 0x20u; at += 4u)
            {
                port::store<uint16_t>(at, 0);
                port::store<uint16_t>(at + 2u, 0);
            }
        }
        events->talks[0] = GuestPtr<EventData>{newEventObject(0x84u, true)};
        events->talks[1] = GuestPtr<EventData>{newEventObject(0x24u, false)};
        events->talks[2] = GuestPtr<EventData>{newEventObject(0x24u, false)};
        globals::eventTypesCopy.set(globals::eventTypes.get());
        const int32_t stage = globals::stageState()->stage;
        if (stage >= 0)
        {
            if (stage == 0)
                EventList_MoveAll(globals::eventQueue(), 7, 6);
            // The test for the move: the event table, then a mask with the bit of the stage (7 at most). The original
            // keeps it in eight bytes of its frame.
            port::GuestLocal<uint64_t> context;
            const GuestPtr<EventTest> test{context.ptr().address};
            test->types = GuestPtr<EventType>{globals::eventTypes.get()};
            const int32_t shift = globals::stageState()->stage < 8 ? globals::stageState()->stage : 7;
            test->mask = static_cast<uint8_t>(1u << (shift & 0x1F));
            EventList_MoveByMask(globals::eventQueue(), 6, 5, test);
        }
        return events;
    }

    // Reads DATA\BIN\EVENT.BIN (0x4C-byte records) into a new array, keeps each type's id, creates the event queue and
    // queues every type after the first in bucket 6 by its priority, with its wait.
    void Event_LoadTypes()
    {
        port::GuestLocal<GameFile> file;
        File_ctor(file, 0);
        if (File_Open(file, GuestPtr<char>{globals::kPathEventBin}) >= 0)
        {
            const uint32_t size = static_cast<uint32_t>(File_Seek(file, 0, 2, 0));
            globals::eventTypeCount.set(static_cast<int32_t>(size / 0x4Cu));
            const uint32_t count = static_cast<uint32_t>(globals::eventTypeCount.get());
            globals::eventTypes.set(sdk::mw_NewArray(count * 0x4Cu));
            File_Seek(file, 0, 0, 0);
            File_Read(file, static_cast<int>(globals::eventTypes.get()), static_cast<int>(globals::eventTypeCount.get()), 0x4C, 0);
            File_Close(file);
            // Each type's id (group, number), into the id table.
            uint32_t out = globals::kEventTypeIds;
            const uint32_t end = globals::eventTypes.get() + static_cast<uint32_t>(globals::eventTypeCount.get()) * 0x4Cu;
            for (uint32_t record = globals::eventTypes.get(); record < end; record += 0x4Cu)
            {
                port::store<uint8_t>(out, GuestPtr<EventType>{record}->group);
                port::store<uint8_t>(out + 1u, GuestPtr<EventType>{record}->number);
                out += 2u;
            }
            uint32_t queue = sdk::operator_new(0x224u);
            if (queue != 0u)
            {
                for (uint32_t at = queue + 0x24u; at != queue + 0x224u; at += 2u)
                {
                    port::store<uint8_t>(at, 0);
                    port::store<uint8_t>(at + 1u, 0);
                }
                sdk::memset(queue, 0, 0x24);
            }
            globals::eventQueueObject.set(queue);
            port::GuestLocal<EventQueueEntry> entry;
            entry->type = 0;
            entry->wait = 0;
            const uint32_t stop = globals::eventTypes.get() + static_cast<uint32_t>(globals::eventTypeCount.get()) * 0x4Cu;
            for (uint32_t record = globals::eventTypes.get() + 0x4Cu; record < stop; record += 0x4Cu)
            {
                entry->type = static_cast<uint8_t>(entry->type + 1);
                entry->wait = GuestPtr<EventType>{record}->wait;
                EventQueue_InsertByPriority(globals::eventQueue(), entry, 6, 0);
            }
        }
        File_dtor(file, -1);
    }

    PORT_FN(EventMgr_ctor);
    PORT_FN(Event_LoadTypes);

    // ---- Party popup (plan P6 batch 2) ----

    // The vtable steps down to the derived class's destructor state and back to the base class's; the GS is waited for
    // before the frame it may still draw is deleted.
    int PartyPopup_dtor(int popup, int16_t deleteFlag)
    {
        if (popup != 0)
        {
            const uint32_t object = static_cast<uint32_t>(popup);
            port::store<uint32_t>(object, globals::kPartyPopupVtable);
            sdk::sceGsSyncPath(0, 0);
            const uint32_t frame = port::load<uint32_t>(object + 4u);
            if (frame != 0u)
            {
                Frame_DeletingDtor(GuestPtr<Frame>{frame}, 1);
                port::store<uint32_t>(object + 4u, 0u);
            }
            port::store<uint32_t>(object, globals::kPartyPopupBaseVtable);
            if (0 < deleteFlag)
                sdk::operator_delete(object);
        }
        return popup;
    }

    PORT_FN(PartyPopup_dtor);

    // The title text of an event slot: the event type's title, or with a second family in the slot (familyB) the title followed
    // by that member's name, cut to fit 24 characters and ended with a 0xC4 byte when it is cut. Built at 0x1C5990.
    GuestPtr<char> Event_FormatText(GuestPtr<EventMgr> events, int slot)
    {
        const GuestPtr<EventSlot> entry = GuestPtr<EventSlot>::of(events->slots[slot]);
        const uint32_t kind = entry->kind;
        const uint32_t familyB = entry->familyB;
        const uint32_t title = GuestPtr<char>::of(globals::eventType(kind)->name[0]).address;
        if (familyB == 0)
            return GuestPtr<char>{title};
        const GuestPtr<Family> family = Family_Get(static_cast<int>(familyB), 0);
        sdk::strcpy(globals::kEventTextBuffer, title);
        const uint32_t name = GuestPtr<char>::of(family->members[entry->memberB].surname[0]).address;
        const int length = sdk::strlen(globals::kEventTextBuffer);
        const int nameLength = sdk::strlen(name);
        if (static_cast<uint32_t>(length + nameLength) < 0x19u)
            sdk::strcpy(globals::kEventTextBuffer + static_cast<uint32_t>(length), name);
        else
        {
            sdk::memcpy(globals::kEventTextBuffer + static_cast<uint32_t>(length), name, 0x17 - length);
            port::store<uint8_t>(globals::kEventTextBuffer + 0x17u, 0xC4);
            port::store<uint8_t>(globals::kEventTextBuffer + 0x18u, 0);
        }
        return GuestPtr<char>{globals::kEventTextBuffer};
    }

    PORT_FN(Event_FormatText);

    // The missing children event may start when the city has stage flag 2, the person's family has a member of character type
    // 1 or 2 (the child) and a shop is found for the person: the event then names the person's home, the family and the
    // child (member index) for actor 1 and the family's adult (member 0) for actor 2.
    int EvMissingChildren_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        if ((city->flags & 2u) == 0)
            return 0;
        const GuestPtr<Family> family = Family_Get(person->family, 0);
        const uint32_t count = family->memberCount;
        int32_t child = -1;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t charType = family->members[i].charType;
            if (charType == 1 || charType == 2)
            {
                child = static_cast<int32_t>(i);
                break;
            }
        }
        if (child < 0)
            return 0;
        if (EvActor_FindShop(city, person) == 0)
            return 0;
        const GuestPtr<ActorRef> lost = actorAt(data, 1), adult = actorAt(data, 2);
        lost->object = person->home;
        lost->family = person->family;
        lost->member = static_cast<uint16_t>(child);
        lost->person = Map_FindPersonEntry(city->map, static_cast<int16_t>(lost->family), lost->member)->index;
        adult->object = person->home;
        adult->family = person->family;
        adult->member = 0;
        adult->person = Map_FindPersonEntry(city->map, static_cast<int16_t>(adult->family), adult->member)->index;
        return 1;
    }

    PORT_FN(EvMissingChildren_Check);

    // Whether the family member `familyMember` (family, member) can be added to the event slot `choice`: 4 when the member is
    // already its second person, 0 when the event type lists the character types it takes (charTypes, up to four) and the
    // member's is not among them, 3 when the member is its first person, 1 when the member is already on its list of five,
    // otherwise 2 (the member goes on the list: straight away, or, for a type whose handler has an answer slot, only when that
    // answer says yes; it is asked with the third talk event's data (talks[2]) set to the slot's second person, which is
    // cleared again, and a no ends in 0).
    int Event_FindForFamily(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, int choice, GuestPtr<int16_t> familyMember)
    {
        struct Locals
        {
            uint8_t gap_00[0x88];
            ActorRef asked;            // +0x88 the member asked about, for the handler's answer slot
        };
        static_assert(offsetof(Locals, asked) == 0x88 && sizeof(Locals) == 0x90);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<EventSlot> entry = GuestPtr<EventSlot>::of(events->slots[choice]);
        const uint32_t member = familyMember.address;
        const uint32_t family = port::load<uint16_t>(member), index = port::load<uint16_t>(member + 2u);
        if (entry->familyB == family && entry->memberB == index)
            return 4;
        const GuestPtr<EventType> type = globals::eventType(entry->kind);
        if (type->charTypes[0] != 0)
        {
            const GuestPtr<Family> record = Family_Get(static_cast<int>(family), 0);
            const uint32_t charType = record->members[index].charType;
            bool found = false;
            for (uint32_t i = 0; i < 4; ++i)
            {
                const uint32_t value = type->charTypes[i];
                if (value == 0)
                    break;
                if (value == charType)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                return 0;
        }
        if (entry->familyA == family && entry->memberA == index)
            return 3;
        int result = 2;
        for (uint32_t k = 0; k < 5; ++k)
        {
            const GuestPtr<MemberId> list = GuestPtr<MemberId>::of(entry->joined[k]);
            if (list->family == 0)
            {
                const uint32_t checkType = type->handler;
                const uint32_t descriptor = checkType != 0 ? eventHandler(checkType).address : 0u;
                const uint32_t check = descriptor != 0 ? GuestPtr<EventHandler>{descriptor}->answer : 0u;
                if (check == 0)
                {
                    list->family = static_cast<uint16_t>(family);
                    list->member = static_cast<uint16_t>(index);
                    return 2;
                }
                const GuestPtr<EventData> probe = events->talks[2];
                probe->actor.person = kNoPerson;
                probe->actor.object = kNoObject;
                probe->actor.family = entry->familyB;
                probe->actor.member = entry->memberB;
                frame->asked = *GuestPtr<ActorRef>{globals::kNoActor};
                frame->asked.family = static_cast<uint16_t>(family);
                frame->asked.member = static_cast<uint16_t>(index);
                const uint32_t function = eventHandler(type->handler)->answer;
                if (port::callGuest<int64_t>(function, probe, city, frame.at(&Locals::asked), 0) == 0)
                    result = 0;
                else
                {
                    list->family = port::load<uint16_t>(member);
                    list->member = port::load<uint16_t>(member + 2u);
                }
                const uint32_t after = events->talks[2].address;
                const int size = eventSize(after);
                sdk::memset(after + 8u, 0, size);
                return result;
            }
            if (list->family == family && list->member == index)
                return 1;
        }
        return result;
    }

    PORT_FN(Event_FindForFamily);

    // The alien event's Answer slot (called with mode 0 at its start): the first Spaceman's Home family is added to the
    // applicants, selected. The result is 0.
    int EvAlien_Answer(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        (void)data;
        (void)actor;
        if (mode == 0)
            CityScene_AddApplicantFamily(city, globals::firstFamily(globals::types::kSpacemansHome), 1);
        return 0;
    }

    PORT_FN(EvAlien_Answer);

    // The noodles event's Answer slot: mode 0 (start) adds the first Noodle Shop family to the applicants and sets up
    // the event's three actors (that family's members 0 and 1, and member 0 of the first family of type 0xB8, a Studio
    // Appartment; no person or building) with 60 minutes to wait; mode 1 (end) adds the third actor's family. The
    // result is 0.
    int EvNoodles_Answer(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        (void)actor;
        const GuestPtr<EvNoodlesData> noodles = data.as<EvNoodlesData>();
        if (mode == 1)
        {
            CityScene_AddApplicantFamily(city, noodles->actors[3].family, 1);
            return 0;
        }
        if (mode != 0)
            return 0;
        const uint16_t shop = globals::firstFamily(globals::types::kNoodleShop);
        CityScene_AddApplicantFamily(city, shop, 1);
        noodles->actors[1].family = shop;
        noodles->actors[1].member = 0;
        noodles->actors[1].person = kNoPerson;
        noodles->actors[1].object = kNoObject;
        noodles->actors[2].family = shop;
        noodles->actors[2].member = 1;
        noodles->actors[2].person = kNoPerson;
        noodles->actors[2].object = kNoObject;
        noodles->actors[3].family = globals::firstFamily(globals::types::kStudioAppartmentB8);
        noodles->actors[3].member = 0;
        noodles->actors[3].person = kNoPerson;
        noodles->actors[3].object = kNoObject;
        noodles->timer = 0x3C;
        return 0;
    }

    PORT_FN(EvNoodles_Answer);

    // An event actor pointed at with the cursor: with the pad focus held elsewhere nothing happens (0). An actor that is a family
    // member (person 6000) starts a family talk (argument 3). Any other actor has the building under its person found on the map
    // by the person's tile; a talk with that building and person starts (mm_CityScene_StartTalk, argument 2), and with none nothing
    // starts. The result is the talk's.
    int EvActor_PointCursor(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor)
    {
        if (globals::padFocusOwner.get() != 0)
            return 0;
        if (actor->person == kNoPerson)
            return CityScene_StartTalk(city, 1, actor->family, actor->member, 3);
        const GuestPtr<MapPerson> person = city->map->people + actor->person;
        const int z = sdk::fptosi(ps2f(64.0f) - person->z);
        const int x = sdk::fptosi(ps2f(64.0f) + person->x);
        const GuestPtr<MapObject> object = Map_FindPersonObjectOnTile(city->map, person->index, static_cast<uint32_t>(x + (z << 7)) & 0xFFFFu);
        if (!object)
            return 0;
        return CityScene_StartTalk(city, 0, object->id, person->index, 2);
    }

    PORT_FN(EvActor_PointCursor);

    // The cave man event's Answer slot (jump table 0x1BA060; modes above 8 give 0). Modes 0 to 3 add Staff Accom family `mode` (the
    // type's first family plus the mode) to the applicants, play sound 0x17 and make it the event's actor `mode` (member 2; for the
    // fourth member 1): mode 0 also makes the family actor 4's (member 1) and sets the counter to 1, 1 and 2 count it to 2
    // and 3, 3 moves the event on (var) and clears it. 4 and 8 give the counter. 5 adds actor 5's family (the sound too), 7 the
    // first Caveman's Home family. 6 asks whether actors 0 and 4 stand at their buildings and neither is asleep (activity 5).
    int EvCaveMan_Answer(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        (void)actor;
        const GuestPtr<EvCaveManData> ev = data.as<EvCaveManData>();
        const auto addFamily = [&](uint32_t family) {
            CityScene_AddApplicantFamily(city, family, 1);
            Sound_PlaySe(0x17, 0, 0x3C, 0.0f, 0.0f);
        };
        switch (static_cast<uint32_t>(mode))
        {
        case 0:
        case 1:
        case 2:
        case 3:
        {
            const uint32_t family = (static_cast<uint32_t>(mode) + globals::firstFamily(globals::types::kStaffAccom)) & 0xFFFFu;
            addFamily(family);
            const GuestPtr<ActorRef> entry = actorAt(data, mode);
            entry->family = static_cast<uint16_t>(family);
            if (mode < 3)
            {
                entry->member = 2;
                ev->counter = mode + 1;
                if (mode == 0)
                {
                    ev->actors[4].family = static_cast<uint16_t>(family);
                    ev->actors[4].member = 1;
                }
            }
            else
            {
                entry->member = 1;
                data->var = static_cast<uint8_t>(data->var + 1);
                ev->counter = 0;
            }
            return 0;
        }
        case 4:
        case 8:
            return ev->counter;
        case 5:
            addFamily(ev->actors[5].family);
            return 0;
        case 6:
        {
            const GuestPtr<ActorRef> first = actorAt(data, 0), last = actorAt(data, 4);
            if (Person_IsAtBuilding(city, first, last) == 0 || Person_IsAtBuilding(city, last, last) == 0)
                return 0;
            const GuestPtr<MapPerson> people = city->map->people;
            if ((people + first->person)->activity == 5)
                return 0;
            return (people + last->person)->activity != 5 ? 1 : 0;
        }
        case 7:
            addFamily(globals::firstFamily(globals::types::kCavemansHome));
            return 0;
        default:
            return 0;
        }
    }

    PORT_FN(EvCaveMan_Answer);

    // The escort event (EvEscortData; phase in var; minutes wraps at ten). Phase 1: before 22:00 the escort is met once
    // the player is within one tile of the actor's building and a tile of the person (then phase 4); failing that, on
    // every tenth call, an actor on open tile more than three tiles away is pointed at. From 22:00 (an actor off open
    // tiles only from 22:10) pointing at the actor moves it to phase 2. Phase 2: before 23:00 the same meeting gives
    // phase 5; from 23:00 (23:10 for an actor off open tiles) a point at the actor stops its following, moves it to its
    // own spot, marks the person (flag 0x80) and moves on to phase 3. A meeting also stops the following and moves the
    // actor to its spot.
    void EvEscort_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvEscortData> escort = data.as<EvEscortData>();
        const GuestPtr<ActorRef> actor = GuestPtr<ActorRef>::of(data->actor);
        bool met = false;
        escort->minutes = escort->minutes + 1;
        if (escort->minutes >= 10)
            escort->minutes = 0;
        const int32_t minutes = city->clock / 60;
        const auto closeEnough = [&] {
            return Player_DistanceToBuilding(city, actor) < 2 && Player_DistanceToPerson(city, actor) <= ps2f(1.0f) && EvActor_PointCursor(city, actor) != 0;
        };
        if (data->var == 1)
        {
            if (minutes < 0x528)
            {
                if (closeEnough())
                {
                    met = true;
                    data->var = 4;
                }
                else if (escort->minutes == 0 && Person_IsOnOpenTile(city, actor) != 0 &&
                         !(Player_DistanceToPerson(city, actor) <= ps2f(3.0f)))
                    EvActor_PointCursor(city, actor);
            }
            else if (Person_IsOnOpenTile(city, actor) != 0 || minutes >= 0x532)
            {
                if (EvActor_PointCursor(city, actor) != 0)
                    data->var = 2;
            }
        }
        else if (data->var == 2)
        {
            if (minutes < 0x564)
            {
                if (closeEnough())
                {
                    met = true;
                    data->var = 5;
                }
            }
            else if ((Person_IsOnOpenTile(city, actor) != 0 || minutes >= 0x56E) && EvActor_PointCursor(city, actor) != 0)
            {
                EvActor_StopFollowing(city, actor);
                EvActor_MoveTo(city, actor, actor, 0);
                const GuestPtr<MapPerson> person = city->map->people + actor->person;
                person->flags = static_cast<uint16_t>(person->flags | 0x80u);
                data->var = 3;
            }
        }
        if (met)
        {
            EvActor_StopFollowing(city, actor);
            EvActor_MoveTo(city, actor, actor, 0);
        }
    }

    PORT_FN(EvEscort_Update);

    // The shopping together event (EvShoppingTogetherData; the phase in var). Phase 1 counts the countdown down: above
    // 60 the player must be near actor 1's building and actor 0 (then actor 0 is pointed at, on an open tile or after
    // ten tries; the outcome is 1 for a countdown under 121, else 0, and phase 5), else every tenth call actor 0 is
    // pointed at; from 60 down, pointing at actor 0 on an open tile moves it to phase 2. Phase 3 does the same while
    // the countdown runs (outcome 2, phase 5) and when it ends points at actor 0, has it stop following and walk to
    // actor 1, marks it (flag 0x80, outcome 3) and moves on to phase 4. Phase 6 ends the event after 180 minutes.
    void EvShoppingTogether_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvShoppingTogetherData> ev = data.as<EvShoppingTogetherData>();
        const GuestPtr<ActorRef> first = GuestPtr<ActorRef>::of(ev->actors[0]), second = GuestPtr<ActorRef>::of(ev->actors[1]);
        // Too far from actor 0 (three tiles or more): every tenth call it is pointed at while on an open tile.
        const auto farOff = [&] {
            if (Player_DistanceToPerson(city, first) < ps2f(3.0f))
            {
                ev->offTileCalls = 0;
                ev->farCalls = 0;
                return;
            }
            ev->farCalls = ev->farCalls + 1;
            if (ev->farCalls >= 10 && Person_IsOnOpenTile(city, first) != 0 && EvActor_PointCursor(city, first) != 0)
                ev->farCalls = 0;
            ev->offTileCalls = 0;
        };
        const auto nearBoth = [&] {
            return Player_DistanceToBuilding(city, second) < 2 && Player_DistanceToPerson(city, first) <= ps2f(1.0f);
        };
        // Near both: pointed at once the actor is on an open tile, or after ten calls with it off one.
        const auto meet = [&](int32_t outcome) {
            if (Person_IsOnOpenTile(city, first) == 0)
            {
                ev->offTileCalls = ev->offTileCalls + 1;
                if (ev->offTileCalls < 10)
                {
                    ev->farCalls = 0;
                    return;
                }
            }
            if (EvActor_PointCursor(city, first) == 0)
            {
                ev->farCalls = 0;
                return;
            }
            ev->outcome = outcome >= 0 ? outcome : (ev->countdown < 0x79 ? 1 : 0);
            data->var = 5;
            ev->farCalls = 0;
        };
        switch (data->var)
        {
        case 1:
            ev->countdown = ev->countdown - 1;
            if (ev->countdown < 0x3D)
            {
                if (Person_IsOnOpenTile(city, first) != 0 && EvActor_PointCursor(city, first) != 0)
                    data->var = static_cast<uint8_t>(data->var + 1);
            }
            else if (nearBoth())
                meet(-1);
            else
                farOff();
            break;
        case 3:
            ev->countdown = ev->countdown - 1;
            if (ev->countdown > 0)
            {
                if (nearBoth())
                    meet(2);
                else
                    farOff();
            }
            else if (Person_IsOnOpenTile(city, first) != 0 && EvActor_PointCursor(city, first) != 0)
            {
                EvActor_StopFollowing(city, first);
                EvActor_MoveTo(city, first, second, 1);
                const GuestPtr<MapPerson> person = city->map->people + first->person;
                person->flags = static_cast<uint16_t>(person->flags | 0x80u);
                ev->outcome = 3;
                data->var = static_cast<uint8_t>(data->var + 1);
            }
            break;
        case 6:
            ev->minutes = ev->minutes + 1;
            if (ev->minutes >= 0xB4)
                Event_Finish(city->events, city, 0);
            break;
        default:
            break;
        }
    }

    PORT_FN(EvShoppingTogether_Update);

    // The alien event's phases (EvAlienData; the phase in var). 1 plays sound 0x45 and notes the next day; 2 moves on
    // when it comes; 3 from 10:00 tells actor 1 and waits 60 minutes; 4, 10 and 12 count the 60 down and go back a
    // phase. 7 needs actor 0 on an open tile within a tile of the player and a building picked for its tile
    // (EvAlien_PickTarget): the place becomes that building (marked 0x200, the old one's mark cleared). 9 and 11 tell
    // the place's actor on the noted day and time (noon of the next day after 9). 22 points at actor 0 within a tile of
    // the player once it stands on the footprint of actor 3's building, 24 points at actor 5 and 26 ends the event
    // after 720 minutes.
    void EvAlien_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvAlienData> ev = data.as<EvAlienData>();
        const auto actor = [&](int i) { return GuestPtr<ActorRef>::of(ev->actors[i]); };
        const GuestPtr<ActorRef> first = actor(0);
        const auto advance = [&] { data->var = static_cast<uint8_t>(data->var + 1); };
        switch (data->var)
        {
        case 1:
            Sound_PlaySe(0x45, 0, 0x3C, 0.0f, 0.0f);
            advance();
            ev->month = city->monthCount + 1;
            break;
        case 2:
            if (ev->month < city->monthCount)
                data->var = 3;
            break;
        case 3:
            if (city->clock >= 0x8CA0 && EvActor_Notify(city, actor(1)) != 0)
            {
                advance();
                ev->minutes = 0x3C;
            }
            break;
        case 4:
        case 10:
        case 12:
            ev->minutes = ev->minutes - 1;
            if (ev->minutes <= 0)
                data->var = static_cast<uint8_t>(data->var - 1);
            break;
        case 7:
        {
            if (Person_IsOnOpenTile(city, first) == 0 || !(Player_DistanceToPerson(city, first) <= ps2f(1.0f)))
                break;
            const GuestPtr<MapPerson> person = city->map->people + first->person;
            const int z = sdk::fptosi(ps2f(64.0f) - person->z);
            const int x = sdk::fptosi(ps2f(64.0f) + person->x);
            const GuestPtr<MapObject> target = EvAlien_PickTarget(city, x, z);
            if (!target || EvActor_PointCursor(city, first) == 0)
                break;
            advance();
            ev->month = city->monthCount + 1;
            ev->time = 0x8CA0;
            const GuestPtr<ActorRef> place = actor(2);
            if (place->object == target->id)
                break;
            const GuestPtr<MapObject> old = Map_GetRecord(city->map, place->object);
            old->flags = static_cast<uint16_t>(old->flags & 0xFDFFu);
            place->object = target->id;
            place->family = target->family;
            place->member = 0;
            place->person = Map_FindPersonEntry(city->map, static_cast<int16_t>(place->family), place->member)->index;
            target->flags = static_cast<uint16_t>(target->flags | 0x200u);
            break;
        }
        case 9:
            if (city->monthCount < ev->month || city->clock < ev->time)
                break;
            if (EvActor_Notify(city, actor(2)) != 0)
            {
                advance();
                ev->minutes = 0x3C;
                ev->month = city->monthCount + 1;
                ev->time = 0xA8C0;
            }
            break;
        case 11:
            if (city->monthCount < ev->month || city->clock < ev->time)
                break;
            if (EvActor_Notify(city, actor(4)) != 0)
            {
                advance();
                ev->minutes = 0x3C;
            }
            break;
        case 22:
        {
            if (!(Player_DistanceToPerson(city, first) <= ps2f(1.0f)))
                break;
            struct Locals
            {
                uint8_t gap_00[0x50];
                TileRect goal;         // +0x50 actor 3's building widened by 1
            };
            static_assert(offsetof(Locals, goal) == 0x50 && sizeof(Locals) == 0x60);
            port::GuestFrameOf<Locals> frame;
            Building_GetFootprint(Map_GetRecord(city->map, ev->actors[3].object), 1, frame.at(&Locals::goal));
            const GuestPtr<MapPerson> person = city->map->people + first->person;
            const int z = sdk::fptosi(ps2f(64.0f) - person->z);
            const int x = sdk::fptosi(ps2f(64.0f) + person->x);
            if (holds(frame->goal, x, z) && EvActor_PointCursor(city, first) != 0)
                advance();
            break;
        }
        case 24:
            if (EvActor_PointCursor(city, actor(5)) != 0)
            {
                advance();
                ev->minutes = 0;
            }
            break;
        case 26:
            ev->minutes = ev->minutes + 1;
            if (ev->minutes >= 0x2D0)
                Event_Finish(city->events, city, 0);
            break;
        default:
            break;
        }
    }

    PORT_FN(EvAlien_Update);

    // The cave man event's phases (phase in var; EvCaveManData: eight actors, 4 the leader; the counter, a countdown
    // and a month). 2 marks the first four buildings of type 0xB0 (flag 0x200), giving their families' actors the
    // building, and moves on once four are found (and again for each further one); 3 finds the person entries of the
    // actors one by one (the leader's from its family and member) and notes the month; 4 and 10 wait for a day after
    // the noted one between 10:00 and 17:00 with the leader at its building and tell actor 4 or 3 (60 minutes); 5 and
    // 11 send them off and count the 60 down back to the phase before; 6 to 9, 20 and 21 keep actors 0 and 4 where they
    // are; 18 finds actor 5's building or person entry; 24 sends actor 0 to actor 6 and marks it, 25 does so with the
    // activity 10 and points at it (on an open tile after ten tries, three tiles or more from the player; else once it
    // is at actor 6's building: the talk's view lit and phase 27); 26 releases it and points at it within a tile; 28
    // points at actor 7; 30 ends the event after 720 minutes.
    void EvCaveMan_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvCaveManData> ev = data.as<EvCaveManData>();
        const GuestPtr<ActorRef> first = actorAt(data, 0), third = actorAt(data, 3), fourth = actorAt(data, 4), sixth = actorAt(data, 6);
        const auto advance = [&](int step) { data->var = static_cast<uint8_t>(data->var + step); };
        // A day after the noted one, between 10:00 and 17:00.
        const auto timeToTell = [&] { return ev->month < city->monthCount && city->clock >= 0x8CA0 && city->clock < 0xEF10; };
        const auto person = [&](GuestPtr<ActorRef> actor) { return city->map->people + actor->person; };
        switch (data->var)
        {
        case 2:
        {
            const GuestPtr<MapObject> begin = Map_GetRecord(city->map, 0);
            int found = 0;
            for (GuestPtr<MapObject> record = begin; record.address < begin.address + kObjectTableBytes; ++record)
            {
                if (record->type != 0xB0)
                    continue;
                const uint32_t flags = record->flags;
                if ((flags & 0x200u) == 0)
                {
                    record->flags = static_cast<uint16_t>(flags | 0x200u);
                    // The actor of the building's family (the families are numbered on from the first actor's).
                    actorAt(data, record->family - data->actor.family)->object = record->id;
                }
                ++found;
                if (found >= 4)
                    advance(1);
            }
            break;
        }
        case 3:
        {
            const uint32_t index = static_cast<uint32_t>(ev->counter);
            const GuestPtr<ActorRef> actor = actorAt(data, static_cast<int>(index));
            const GuestPtr<MapPerson> entry = Map_FindPersonEntry(city->map, static_cast<int16_t>(actor->family), actor->member);
            if (!entry)
                break;
            actor->person = entry->index;
            if (ev->counter == 0)
            {
                ev->actors[4].object = actor->object;
                ev->actors[4].person = Map_FindPersonEntry(city->map, static_cast<int16_t>(ev->actors[4].family), ev->actors[4].member)->index;
            }
            ev->counter = ev->counter + 1;
            if (ev->counter >= 4)
            {
                ev->counter = 0;
                advance(1);
                ev->month = city->monthCount;
            }
            break;
        }
        case 4:
            if (timeToTell() && Person_IsAtBuilding(city, fourth, fourth) != 0 && EvActor_Notify(city, fourth) != 0)
            {
                ev->minutes = 0x3C;
                advance(1);
            }
            break;
        case 5:
            EvActor_MoveTo(city, fourth, fourth, 0);
            EvActor_MoveTo(city, first, fourth, 0);
            ev->minutes = ev->minutes - 1;
            if (ev->minutes <= 0)
                advance(-1);
            break;
        case 6:
        case 7:
        case 8:
        case 9:
            EvActor_MoveTo(city, first, first, 0);
            EvActor_MoveTo(city, fourth, fourth, 0);
            ev->month = city->monthCount;
            break;
        case 10:
            if (timeToTell() && Person_IsAtBuilding(city, fourth, fourth) != 0 && EvActor_Notify(city, third) != 0)
            {
                ev->minutes = 0x3C;
                advance(1);
            }
            break;
        case 11:
            EvActor_MoveTo(city, third, third, 0);
            ev->minutes = ev->minutes - 1;
            if (ev->minutes <= 0)
                advance(-1);
            break;
        case 18:
        {
            const GuestPtr<ActorRef> fifth = actorAt(data, 5);
            if (fifth->object == kNoObject)
            {
                const GuestPtr<MapObject> home = Map_FindFamilyHome(city->map, fifth->family);
                if (!home)
                    break;
                fifth->object = home->id;
                home->flags = static_cast<uint16_t>(home->flags | 0x200u);
                break;
            }
            const GuestPtr<MapPerson> entry = Map_FindPersonEntry(city->map, static_cast<int16_t>(fifth->family), fifth->member);
            if (!entry)
                break;
            fifth->person = entry->index;
            advance(1);
            break;
        }
        case 20:
        case 21:
            EvActor_MoveTo(city, first, first, 0);
            EvActor_MoveTo(city, fourth, fourth, 0);
            break;
        case 24:
        {
            EvActor_MoveTo(city, first, sixth, 1);
            const GuestPtr<MapPerson> p = person(first);
            p->flags = static_cast<uint16_t>(p->flags | 0x80u);
            advance(1);
            break;
        }
        case 25:
        {
            EvActor_MoveTo(city, first, sixth, 1);
            const GuestPtr<MapPerson> p = person(first);
            p->activity = 0xA;
            p->flags = static_cast<uint16_t>(p->flags | 0x80u);
            if (Person_IsOnOpenTile(city, first) != 0)
            {
                if (Player_DistanceToPerson(city, first) < ps2f(3.0f))
                    break;
                ev->minutes = ev->minutes + 1;
                if (ev->minutes < 10 || EvActor_PointCursor(city, first) == 0)
                    break;
                EvActor_Release(city, first);
                advance(1);
            }
            else if (Person_IsAtBuilding(city, first, sixth) != 0 && EvActor_PointCursor(city, first) != 0)
            {
                Talk_SetViewLit(city->talk);
                data->var = 0x1B;
            }
            break;
        }
        case 26:
            EvActor_Release(city, first);
            if (Player_DistanceToPerson(city, first) <= ps2f(1.0f) && EvActor_PointCursor(city, first) != 0)
            {
                ev->minutes = 0;
                advance(-1);
            }
            break;
        case 28:
            if (EvActor_PointCursor(city, actorAt(data, 7)) != 0)
            {
                advance(1);
                ev->minutes = 0;
            }
            break;
        case 30:
            ev->minutes = ev->minutes + 1;
            if (ev->minutes >= 0x2D0)
            {
                Event_Finish(city->events, city, 0);
                break;
            }
            if (ev->counter != 0)
                break;
            if (Map_FindPersonEntry(city->map, static_cast<int16_t>(actorAt(data, 7)->family), actorAt(data, 7)->member))
                ev->counter = ev->counter + 1;
            break;
        default:
            break;
        }
    }

    PORT_FN(EvCaveMan_Update);

    // The missing children event's phases (jump table 0x1BA800; phase in var; EvMissingChildrenData: actor 0 the
    // parent, actor 1 the child, the counter, the outcome and a minute count). 1 keeps the parent where it is and the
    // child wandering; from 13:00 to 15:00, if a shop is found for the parent, it is told (60 minutes), else the event
    // ends. 2 and 6 count the 60 down back a phase; 3 lets the child wander until 18:00; 4 waits for the child to be at
    // its building; 5 tells the parent; 7 is the search: before 20:00 the parent stays and the child is pointed at on
    // open tiles three tiles or more away every tenth call, and within three tiles of the player and four of its
    // building it is stopped and followed by the parent (outcome 2); from 20:00 the parent follows the child at once
    // (phase 12). 8 releases the child and, near it, points at the parent and has it follow the child; 10 releases and
    // points at the child; 12 does the same near both (outcome 1); 14 puts the parent inside the child's building; 16
    // ends the event after 180 minutes.
    void EvMissingChildren_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvMissingChildrenData> ev = data.as<EvMissingChildrenData>();
        const GuestPtr<ActorRef> first = actorAt(data, 0), second = actorAt(data, 1);
        const auto advance = [&](int step) { data->var = static_cast<uint8_t>(data->var + step); };
        const auto person = [&](GuestPtr<ActorRef> actor) { return city->map->people + actor->person; };
        switch (data->var)
        {
        case 1:
            EvActor_MoveTo(city, first, first, 0);
            EvActor_Wander(city, second);
            if (city->clock < 46800)
                break;
            if (city->clock < 54000 && EvActor_FindShop(city, person(first)) != 0)
            {
                if (EvActor_Notify(city, first) != 0)
                {
                    advance(1);
                    ev->counter = 0x3C;
                }
                break;
            }
            Event_Finish(city->events, city, 0);
            break;
        case 2:
            EvActor_MoveTo(city, first, first, 0);
            EvActor_Wander(city, second);
            ev->counter = ev->counter - 1;
            if (ev->counter <= 0)
                advance(-1);
            break;
        case 3:
            EvActor_MoveTo(city, first, first, 0);
            if (city->clock >= 64800)
                advance(1);
            else
                EvActor_Wander(city, second);
            break;
        case 4:
            EvActor_MoveTo(city, first, first, 0);
            if (Person_IsAtBuilding(city, second, second) != 0)
                advance(1);
            break;
        case 5:
            EvActor_MoveTo(city, first, first, 0);
            if (EvActor_Notify(city, first) != 0)
            {
                ev->counter = 0x3C;
                advance(1);
            }
            break;
        case 6:
            EvActor_MoveTo(city, first, first, 0);
            ev->counter = ev->counter - 1;
            if (ev->counter <= 0)
                advance(-1);
            break;
        case 7:
            if (city->clock >= 72000)
            {
                EvActor_Follow(city, first, second);
                data->var = 0xC;
                break;
            }
            EvActor_MoveTo(city, first, first, 0);
            if (Player_DistanceToPerson(city, second) <= ps2f(3.0f))
            {
                if (Player_DistanceToBuilding(city, second) < 4)
                {
                    EvActor_StopFollowing(city, second);
                    EvActor_Follow(city, first, second);
                    ev->outcome = 2;
                    advance(1);
                }
                ev->counter = 0;
                break;
            }
            ev->counter = ev->counter + 1;
            if (ev->counter < 10 || Person_IsOnOpenTile(city, second) == 0)
                break;
            EvActor_PointCursor(city, second);
            ev->counter = 0;
            break;
        case 8:
        {
            EvActor_Release(city, second);
            if (!(Person_Distance(city, first, second) <= ps2f(1.0f)))
            {
                ev->counter = 0;
                break;
            }
            if (Person_IsOnOpenTile(city, first) == 0)
            {
                ev->counter = ev->counter + 1;
                if (ev->counter < 5)
                    break;
            }
            if (EvActor_PointCursor(city, first) == 0)
                break;
            EvActor_StopFollowing(city, first);
            const GuestPtr<MapPerson> parent = person(first);
            parent->flags = static_cast<uint16_t>(parent->flags | 0x200u);
            parent->follow = second->person;
            EvActor_MoveTo(city, first, first, 0);
            EvActor_Follow(city, second, first);
            advance(1);
            break;
        }
        case 10:
            EvActor_Release(city, second);
            if (EvActor_PointCursor(city, second) != 0)
                advance(1);
            break;
        case 12:
            ev->counter = ev->counter + 1;
            if (ev->counter < 10)
                break;
            if (Player_DistanceToPerson(city, second) <= ps2f(1.0f) && Player_DistanceToPerson(city, first) <= ps2f(1.0f) &&
                EvActor_PointCursor(city, first) != 0)
            {
                EvActor_StopFollowing(city, first);
                const GuestPtr<MapPerson> parent = person(first);
                parent->flags = static_cast<uint16_t>(parent->flags | 0x200u);
                parent->follow = second->person;
                EvActor_Follow(city, second, first);
                advance(1);
                ev->outcome = 1;
            }
            ev->counter = 0;
            break;
        case 14:
            if (EvActor_PointCursor(city, second) != 0)
            {
                person(first)->inside = first->object;
                advance(1);
            }
            break;
        case 16:
            ev->minutes = ev->minutes + 1;
            if (ev->minutes >= 0xB4)
                Event_Finish(city->events, city, 0);
            break;
        default:
            break;
        }
    }

    PORT_FN(EvMissingChildren_Update);

    // The farewell party's phases (jump table 0x1BA6E0; phase in var; actor 1, the countdown tellMinutes, the time to
    // wait to partyTime and the steps of the staging stageStep and endStep). 1 tells actor 1 between 6:00 and 23:00 (60
    // minutes, phase 2); 2 counts the 60 down back to phase 1. 3 stages the party by stageStep: 0 fades out (and sets
    // the two voice levels at 0x1BDD14/18 to 100), 1 (when the fade is done) moves the clock to 10:00 (a day on when it
    // was later), brings the people home, pushes the party mode (12), makes the party's overlay (an 8-byte object with
    // a frame of 0xC0 bytes, the city's overlay) and starts the party sound, 2 fades back in with the music on, 3
    // (done) closes the overlay and goes to phase 4. 4 until the time: finds the people on the tile under the player
    // when it is a type 4 building, and points at the first friend (friendship band 4 or more, class flag 1 clear)
    // within a tile of the player that is not already the one chosen (actor 2); after it phase 5. 5 points at actor 0
    // or 1 (by 0x1C5548) at night, tells them by day and waits 60 minutes; 6 counts them down to phase 5. 7 stages the
    // end by endStep and finishes the event. The pending approval is limited so the approval stays within 100 on every
    // call.
    void EvFarewellParty_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        struct Locals
        {
            uint8_t gap_00[0x40];
            Vec4 playerMatrix[4];      // +0x40
            uint8_t gap_80[0xE];
            uint16_t cursor;           // +0x8E the people cursor
        };
        static_assert(offsetof(Locals, playerMatrix) == 0x40 && offsetof(Locals, cursor) == 0x8E && sizeof(Locals) == 0x90);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<EvPartyData> party = data.as<EvPartyData>();
        const auto setVoices = [&]() {
            globals::bgmFadeCounter.set(static_cast<int8_t>(100));
            globals::seFadeCounter.set(static_cast<int8_t>(100));
        };
        switch (data->var)
        {
        case 1:
            if (city->clock >= 0x5460 && city->clock < 0x14370 && EvActor_Notify(city, actorAt(data, 1)) != 0)
            {
                data->var = 2;
                party->tellMinutes = 0x3C;
            }
            break;
        case 2:
            party->tellMinutes = party->tellMinutes - 1;
            if (party->tellMinutes <= 0)
                data->var = static_cast<uint8_t>(data->var - 1);
            break;
        case 3:
            switch (party->stageStep)
            {
            case 0:
                CityScene_StartFade(city, 1);
                party->stageStep = party->stageStep + 1;
                setVoices();
                break;
            case 1:
            {
                if (CityScene_GetFade(city) != 0)
                    break;
                if (0x8CA0 < city->clock)
                    city->monthCount = city->monthCount + 1;
                city->clock = 0x8CA0;
                Map_ReturnPeopleHome(city->map);
                CityScene_PushMode(city, 0xC);
                uint32_t overlay = sdk::operator_new(8u);
                if (overlay != 0u)
                {
                    port::store<uint32_t>(overlay, globals::kPartyPopupBaseVtable);
                    port::store<uint32_t>(overlay, globals::kPartyPopupVtable);
                    uint32_t window = sdk::operator_new(0xC0u);
                    if (window != 0u)
                        window = Frame_ctor(GuestPtr<Frame>{window}, 0x14, 0x21C, 0xC0, 0x18, 0xC, 0xFFFFFFFFu).address;
                    port::store<uint32_t>(overlay + 4u, window);
                }
                city->overlay = GuestPtr<void>{overlay};
                city->flags = city->flags | 8u;
                party->stageStep = party->stageStep + 1;
                Bgm_SetPlaying(0, 1);
                Se_SetEnabled(1, 1);
                globals::ambienceEnabled.set(0);
                break;
            }
            case 2:
                CityScene_StartFade(city, 2);
                party->stageStep = party->stageStep + 1;
                Bgm_SetActive(1);
                break;
            case 3:
            {
                if (CityScene_GetFade(city) != 0)
                    break;
                Pad_ReleaseFocus(4);
                const uint32_t overlay = city->overlay.address;
                if (overlay != 0u)
                {
                    port::callGuest<int64_t>(port::load<uint32_t>(port::load<uint32_t>(overlay) + 8u), overlay, 1);
                    city->overlay = GuestPtr<void>{0u};
                }
                data->var = 4;
                globals::ambienceEnabled.set(1);
                break;
            }
            default:
                break;
            }
            break;
        case 4:
        {
            if (!(city->clock < party->partyTime))
            {
                data->var = 5;
                break;
            }
            Player_GetMatrix(city->player, frame.at(&Locals::playerMatrix));
            const int z = sdk::fptosi(ps2f(64.0f) - frame->playerMatrix[3].z);
            const int x = sdk::fptosi(ps2f(64.0f) + frame->playerMatrix[3].x);
            const GuestPtr<MapObject> under = Map_GetTileObject(city->map, x, z);
            if (under->type != 4)
                break;
            frame->cursor = under->firstPerson;
            const GuestPtr<ActorRef> chosen = actorAt(data, 2);
            while (const GuestPtr<MapPerson> candidate = People_Next(city->map, frame.at(&Locals::cursor)))
            {
                const GuestPtr<CharClass> cls = globals::charClass(candidate->charType);
                if ((cls->flags & 1u) != 0 || Family_GetFriendshipBand(candidate->family, candidate->member) < 4)
                    continue;
                if (!(Player_DistanceToPoint(city, GuestPtr<ps2f>{candidate.address}) <= ps2f(1.0f)) || chosen->person == candidate->index)
                    continue;
                chosen->person = candidate->index;
                chosen->object = candidate->home;
                chosen->family = candidate->family;
                chosen->member = candidate->member;
                if (EvActor_PointCursor(city, chosen) != 0)
                    break;
            }
            break;
        }
        case 5:
        {
            const uint32_t which = globals::stageState()->stage == 4 ? 0u : 1u;
            const GuestPtr<ActorRef> actor = actorAt(data, static_cast<int>(which));
            const bool night = city->clock >= 0x13560 || city->clock < 0x5460;
            if ((night ? EvActor_PointCursor(city, actor) : EvActor_Notify(city, actor)) != 0)
            {
                data->var = 6;
                party->tellMinutes = 0x3C;
            }
            break;
        }
        case 6:
            party->tellMinutes = party->tellMinutes - 1;
            if (party->tellMinutes <= 0)
                data->var = 5;
            break;
        case 7:
            if (party->endStep == 0)
            {
                CityScene_StartFade(city, 1);
                city->hudVisible[2] = 0;
                party->endStep = party->endStep + 1;
                setVoices();
            }
            else if (party->endStep == 1)
            {
                if (CityScene_GetFade(city) != 0)
                    break;
                Pad_ReleaseFocus(4);
                CityScene_PushMode(city, 0xC);
                city->flags = city->flags | 0x10u;
                city->flags = city->flags & ~8u;
                Se_SetEnabled(1, 1);
                Bgm_SetPlaying(0, 1);
                globals::ambienceEnabled.set(0);
                Event_Finish(city->events, city, 0);
            }
            break;
        default:
            break;
        }
        if (city->approval + city->pendingApproval >= 0x65)
            city->pendingApproval = 0x64 - city->approval;
    }

    PORT_FN(EvFarewellParty_Update);

    // The thief event's phases (jump table 0x1BA8E0; EvThiefData, the phase in var; actor 0 the player's contact, 1 the
    // thief, 2, 3 the place). 1 waits for the day, then picks a building for the place (a free one of category 5, else
    // 1, fitting the whole map rectangle at 0x1BA8D0; no repeat of the first actor's building), 60 minutes at a time,
    // and marks it (0x200); from 17:00 the place is given up (unmarked, claim timer 1, flag 0x40) and the day set a day
    // on; from 10:00 actor 3 is told and phase 2 follows. 2 and 11 count 60 minutes down back a phase; 3 points at
    // actor 0 (within a tile after 180 minutes, else every tenth call when three tiles or more away); 5 and 10 tell
    // actor 0 or 1 in daytime or after the countdown; 6 to 8 keep actor 0 in place; 9 starts 180 minutes; 12 to 14 and
    // 18 to 29 move the actors around (13 brings actor 1 to actor 0 between 20:00 and 21:00, 14 starts the chase with
    // sound 0x46 and the thief's schedule 14 and activity 5, 18 gets actor 2 to the thief, 19 points at the building,
    // 22 and 23 wait for the place, 28 starts 720 minutes and 29 ends the event).
    void EvThief_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvThiefData> ev = data.as<EvThiefData>();
        const auto actor = [&](int i) { return GuestPtr<ActorRef>::of(ev->actors[i]); };
        const GuestPtr<ActorRef> first = actor(0), second = actor(1), third = actor(2), place = actor(3);
        const auto advance = [&](int step) { data->var = static_cast<uint8_t>(data->var + step); };
        const auto stay = [&](GuestPtr<ActorRef> actor) { EvActor_MoveTo(city, actor, actor, 0); };
        switch (data->var)
        {
        case 1:
        {
            stay(first);
            if (city->monthCount < ev->nextMonth)
                break;
            if (place->object != kNoObject)
            {
                if (city->clock < 0xEF10)
                {
                    if (city->clock >= 0x8CA0 && EvActor_Notify(city, place) != 0)
                    {
                        advance(1);
                        ev->countdown = 0x3C;
                    }
                    break;
                }
                const GuestPtr<MapObject> given = Map_GetRecord(city->map, place->object);
                given->flags = static_cast<uint16_t>(given->flags & 0xFDFFu);
                given->claimTimer = 1;
                given->flags = static_cast<uint16_t>(given->flags | 0x40u);
                place->person = kNoPerson;
                place->object = kNoObject;
                ev->strikes = ev->strikes + 1;
                ev->nextMonth = city->monthCount + 1;
                break;
            }
            ev->timer = ev->timer - 1;
            if (ev->timer > 0)
                break;
            GuestPtr<MapObject> found = EvThief_FindBuilding(city, 5, GuestPtr<TileRect>{globals::kWholeMapRect});
            if (!found)
                found = EvThief_FindBuilding(city, 1, GuestPtr<TileRect>{globals::kWholeMapRect});
            if (!found || found->id == first->object)
            {
                ev->timer = 0x3C;
                break;
            }
            place->person = Map_FindPersonEntry(city->map, static_cast<int16_t>(found->family), 0)->index;
            place->object = found->id;
            place->family = found->family;
            place->member = 0;
            found->flags = static_cast<uint16_t>(found->flags | 0x200u);
            break;
        }
        case 2:
        case 11:
            stay(first);
            ev->countdown = ev->countdown - 1;
            if (ev->countdown <= 0)
                advance(-1);
            break;
        case 3:
            ev->minutes = ev->minutes + 1;
            if (ev->minutes >= 0xB4)
            {
                if (Player_DistanceToPerson(city, first) <= ps2f(1.0f) && EvActor_PointCursor(city, first) != 0)
                {
                    advance(1);
                    ev->nextMonth = city->monthCount + 1;
                    ev->minutes = 0;
                }
                break;
            }
            if (ev->minutes % 10 != 0 || Player_DistanceToPerson(city, first) < ps2f(3.0f))
                break;
            EvActor_PointCursor(city, first);
            break;
        case 5:
            stay(first);
            if (city->monthCount >= ev->nextMonth && city->clock >= 0x8CA0 && city->clock < 0xEF10 && EvActor_Notify(city, first) != 0)
            {
                advance(1);
                ev->countdown = 0x3C;
            }
            break;
        case 6:
            ev->countdown = ev->countdown - 1;
            if (ev->countdown <= 0)
                advance(-1);
            stay(first);
            break;
        case 7:
        case 8:
            stay(first);
            break;
        case 9:
            ev->timer = 0xB4;
            advance(1);
            stay(first);
            break;
        case 10:
            stay(first);
            ev->timer = ev->timer - 1;
            if (ev->timer > 0)
                break;
            if (EvActor_Notify(city, second) != 0)
            {
                advance(1);
                ev->countdown = 0x3C;
            }
            break;
        case 12:
            stay(first);
            stay(second);
            break;
        case 13:
            stay(first);
            if (city->clock < 0x11940 || city->clock >= 0x12750)
                break;
            if (EvActor_MoveTo(city, second, first, 1) != 0)
                advance(1);
            break;
        case 14:
        {
            EvActor_MoveTo(city, second, first, 1);
            stay(first);
            if (Person_IsAtBuilding(city, second, first) == 0)
                break;
            Sound_PlaySe(0x46, 0, 0x3C, 0.0f, 0.0f);
            const GuestPtr<MapPerson> thief = city->map->people + first->person;
            thief->schedState = 0xE;
            thief->activity = 5;
            EvActor_MoveTo(city, second, second, 1);
            advance(1);
            break;
        }
        case 18:
            ev->timer = ev->timer + 1;
            if (ev->timer >= 10)
            {
                if (Player_DistanceToBuilding(city, first) < 2 && Player_DistanceToPerson(city, third) <= ps2f(1.0f) &&
                    EvActor_PointCursor(city, third) != 0)
                {
                    EvActor_MoveTo(city, third, first, 1);
                    advance(1);
                    ev->timer = 0;
                    break;
                }
                if (!(Player_DistanceToPerson(city, third) < ps2f(3.0f)))
                    EvActor_PointCursor(city, third);
                ev->timer = 0;
            }
            break;
        case 19:
            if (Person_IsAtBuilding(city, third, first) != 0)
            {
                if (EvActor_PointAtBuilding(city, static_cast<int>(third.address), static_cast<int>(first.address)) != 0)
                {
                    advance(1);
                    break;
                }
            }
            EvActor_MoveTo(city, third, first, 1);
            break;
        case 22:
            if (EvActor_MoveTo(city, third, third, 0) != 0)
                advance(1);
            break;
        case 23:
            if (Person_IsAtBuilding(city, second, second) != 0 && Person_IsAtBuilding(city, third, second) != 0)
            {
                if (EvActor_Notify(city, third) != 0)
                {
                    advance(1);
                    ev->countdown = 0x3C;
                }
                break;
            }
            stay(second);
            stay(third);
            break;
        case 24:
            ev->countdown = ev->countdown - 1;
            if (ev->countdown <= 0)
                advance(-1);
            stay(second);
            stay(third);
            break;
        case 25:
        case 26:
        case 27:
            stay(second);
            stay(third);
            break;
        case 28:
            advance(1);
            ev->timer = 0x2D0;
            break;
        case 29:
            ev->timer = ev->timer - 1;
            if (ev->timer <= 0)
                Event_Finish(city->events, city, 0);
            break;
        default:
            break;
        }
    }

    PORT_FN(EvThief_Update);

    // Event type 13: the story is a state machine on `var` over three to four actors: actors 1, 2 and 3, and three more (4, 5
    // and 6) each with a partner three on (7, 8 and 9) that go through the same steps one after the other (phases 4 to 9 and
    // 0xC to 0x1C in threes: the group picks its actor by the phase). The timer is `timer`, the day to wait for `month` and
    // the time of day `time`. State 1 finds the first actor's home and the people; 3 chooses which
    // group goes on; 4/0xD/0x16 wait for the day and 10:00 then have the actor walk to its partner and phone; 5/0xE/0x17 wait out the
    // timer; 6/0xF/0x18 send the actor home; 7/0x10/0x19 and 9/0x12/0x1B bring the player near; 0xA/0x13/0x1C walk the actor and the
    // first one to the first one's building; 0xC, 0x15 and 0x1E move on once the actor is on the building; 0x1F to 0x58 are the
    // scenes with the third actor (following, waiting at a building, the shop); 0x61 ends the event. From phase 0x20 up to 0x57 a
    // building with people inside is emptied to 20 at 2:00 (clock / 60 == 0x78).
    void EvNoodles_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<EvNoodlesData> noodles = data.as<EvNoodlesData>();
        const GuestPtr<CityMap> map = city->map;
        const auto actor = [&](int index) { return actorAt(data, index); };
        const auto partner = [&](GuestPtr<ActorRef> who) { return who + 3; };
        const auto advance = [&](int step = 1) { data->var = static_cast<uint8_t>(data->var + step); };
        const auto record = [&](GuestPtr<ActorRef> who) { return map->people + who->person; };
        const auto address = [](GuestPtr<ActorRef> who) { return static_cast<int>(who.address); };
        // The actor of a group of three phases (a, middle, c): the first group member below the middle, the second at it, the third above.
        const auto byPhase = [&](int middle) { return actor(data->var < middle ? 4 : (data->var > middle ? 6 : 5)); };
        const GuestPtr<ActorRef> first = actor(1), second = actor(2), third = actor(3);
        // The actor reached its partner's place and is stopped there: it walks to the partner, and once at the building (from 10:00)
        // is told.
        const auto standing = [&](GuestPtr<ActorRef> who) {
            return Person_IsOnOpenTile(city, who) != 0 && EvActor_PointCursor(city, who) != 0;
        };
        // The third actor's person, at the turn of each in-game minute, takes shelter (flag 0x40) in rain and snow and leaves it otherwise.
        const auto shelter = [&] {
            if (city->clock / 60 != 0)
                return;
            const GuestPtr<MapPerson> who = record(third);
            const bool wet = city->weatherToday == 2 || city->weatherToday == 3;
            if (wet && (who->flags & 0x40u) == 0)
                who->flags = static_cast<uint16_t>(who->flags | 0x40u);
            else if (!wet && (who->flags & 0x40u) != 0)
                who->flags = static_cast<uint16_t>(who->flags & 0xFFBFu);
        };
        // A person on the map keeps one building's heading count right.
        const auto leaveBuilding = [&](uint32_t building) {
            const GuestPtr<MapObject> old = Map_GetRecord(map, static_cast<int>(building));
            if (old->headingHere != 0)
                --old->headingHere;
        };

        switch (data->var)
        {
        case 1:
        {
            if (first->object == kNoObject)
            {
                const GuestPtr<MapObject> home = Map_FindFamilyHome(map, first->family);
                if (!home)
                    break;
                first->object = home->id;
                second->object = home->id;
                home->flags = static_cast<uint16_t>(home->flags | 0x200u);
                break;
            }
            noodles->timer = noodles->timer - 1;
            if (noodles->timer >= 1)
                break;
            const GuestPtr<MapPerson> entry = Map_FindPersonEntry(map, static_cast<int16_t>(first->family), first->member);
            if (!entry)
            {
                noodles->timer = 0x3C;
                break;
            }
            first->person = entry->index;
            const GuestPtr<MapPerson> other = Map_FindPersonEntry(map, static_cast<int16_t>(second->family), second->member);
            second->person = other->index;
            if (other->target != kNoObject)
                leaveBuilding(other->target);
            other->target = second->object;
            ++Map_GetRecord(map, second->object)->headingHere;
            data->var = 2;
            break;
        }
        case 3:
            if (actor(4)->person == kNoPerson)
                data->var = actor(5)->person == kNoPerson ? 0x16 : 0xD;
            else
                data->var = 4;
            noodles->month = city->monthCount;
            break;
        case 4:
        case 0xD:
        case 0x16:
            if (noodles->month < city->monthCount && city->clock > 0x707F)
            {
                const GuestPtr<ActorRef> who = byPhase(0xD);
                EvActor_MoveTo(city, who, partner(who), 0);
                if (city->clock >= 36000 && Person_IsAtBuilding(city, who, partner(who)) != 0 && EvActor_Notify(city, who) != 0)
                {
                    advance();
                    noodles->timer = 0x3C;
                }
            }
            break;
        case 5:
        case 0xE:
        case 0x17:
        {
            const GuestPtr<ActorRef> who = byPhase(0xE);
            EvActor_MoveTo(city, who, partner(who), 0);
            noodles->timer = noodles->timer - 1;
            if (noodles->timer < 1)
                advance(-1);
            break;
        }
        case 6:
        case 0xF:
        case 0x18:
            EvActor_Detach(city, byPhase(0xF));
            advance();
            break;
        case 7:
        case 0x10:
        case 0x19:
        {
            const GuestPtr<ActorRef> who = byPhase(0x10);
            if (Person_IsOnOpenTile(city, who) != 0 && Player_DistanceToPerson(city, who) <= ps2f(1.0f) && EvActor_PointCursor(city, who) != 0)
                advance();
            break;
        }
        case 9:
        case 0x12:
        case 0x1B:
        {
            const GuestPtr<ActorRef> who = byPhase(0x12);
            if (Person_IsOnOpenTile(city, who) == 0)
                break;
            const ps2f away = Player_DistanceToPerson(city, who);
            if (!(away < ps2f(3.0f)))
            {
                noodles->timer = noodles->timer + 1;
                if (noodles->timer >= 10 && EvActor_PointCursor(city, who) != 0)
                    noodles->timer = 0;
            }
            else if (away <= ps2f(1.0f) && Player_DistanceToBuilding(city, first) < 2 && EvActor_PointCursor(city, who) != 0)
            {
                EvActor_StopFollowing(city, who);
                EvActor_MoveTo(city, who, first, 1);
                advance();
            }
            break;
        }
        case 0xA:
        case 0x13:
        case 0x1C:
        {
            const GuestPtr<ActorRef> who = byPhase(0x13);
            EvActor_MoveTo(city, who, first, 1);
            EvActor_MoveTo(city, first, first, 0);
            if (Person_IsAtBuilding(city, who, first) != 0 && Person_IsAtBuilding(city, first, first) != 0 &&
                EvActor_PointAtBuilding(city, address(who), address(first)) != 0)
            {
                record(who)->activity = 2;
                record(first)->activity = 2;
                advance();
            }
            break;
        }
        case 0xC:
        {
            const GuestPtr<MapObject> building = Map_GetRecord(map, first->object);
            const GuestPtr<ActorRef> who = actor(4);
            EvActor_MoveTo(city, who, partner(who), 0);
            if (standing(who) || Person_IsNearBuilding(city, record(who), building, 6) != 0)
            {
                if (actor(5)->person != kNoPerson)
                {
                    noodles->month = city->monthCount;
                    data->var = 0xD;
                }
                else if (actor(6)->person != kNoPerson)
                {
                    noodles->month = city->monthCount;
                    data->var = 0x16;
                }
                else
                    data->var = 0x1F;
            }
            break;
        }
        case 0x15:
        {
            const GuestPtr<MapObject> building = Map_GetRecord(map, first->object);
            const GuestPtr<ActorRef> who = actor(5);
            EvActor_MoveTo(city, who, partner(who), 0);
            if (standing(who) || Person_IsNearBuilding(city, record(who), building, 6) != 0)
            {
                if (actor(6)->person != kNoPerson)
                {
                    noodles->month = city->monthCount;
                    data->var = 0x16;
                }
                else
                    data->var = 0x1F;
            }
            break;
        }
        case 0x1E:
        {
            EvActor_MoveTo(city, second, second, 0);
            const GuestPtr<MapObject> building = Map_GetRecord(map, first->object);
            building->occupants = 0x14;
            const GuestPtr<ActorRef> who = actor(6);
            EvActor_MoveTo(city, who, partner(who), 0);
            if (standing(who) || Person_IsNearBuilding(city, record(who), building, 6) != 0)
                advance();
            break;
        }
        case 0x1F:
            EvActor_MoveTo(city, second, second, 0);
            EvActor_MoveTo(city, second, second, 0);
            EvActor_MoveTo(city, first, first, 0);
            if (Person_IsAtBuilding(city, second, second) != 0 && Person_IsAtBuilding(city, first, first) != 0 &&
                EvActor_PointAtBuilding(city, address(second), address(second)) != 0)
            {
                record(second)->activity = 2;
                record(first)->activity = 2;
                advance();
            }
            break;
        case 0x24:
            EvActor_Wander(city, second);
            noodles->timer = 10;
            advance();
            break;
        case 0x25:
            EvActor_Wander(city, second);
            noodles->timer = noodles->timer - 1;
            if (noodles->timer < 1 && EvActor_Notify(city, third) != 0)
            {
                advance();
                noodles->timer = 0x3C;
            }
            break;
        case 0x26:
        case 0x2A:
            EvActor_Wander(city, second);
            noodles->timer = noodles->timer - 1;
            if (noodles->timer < 1)
                advance(-1);
            break;
        case 0x27:
        {
            EvActor_Wander(city, second);
            const GuestPtr<MapObject> home = Map_FindFamilyHome(map, third->family);
            if (home)
            {
                third->object = home->id;
                home->flags = static_cast<uint16_t>(home->flags | 0x200u);
                advance();
            }
            else if (city->clock / 60 == 0x1A4)
                EvActor_Notify(city, third);
            break;
        }
        case 0x28:
        {
            EvActor_Wander(city, second);
            noodles->timer = noodles->timer - 1;
            if (noodles->timer >= 1)
                break;
            const GuestPtr<MapPerson> entry = Map_FindPersonEntry(map, static_cast<int16_t>(third->family), third->member);
            if (entry)
            {
                third->person = entry->index;
                advance();
            }
            break;
        }
        case 0x29:
            EvActor_Wander(city, second);
            EvActor_MoveTo(city, third, third, 0);
            if (city->clock >= 36000 && city->clock < 0xB6D0 && EvActor_Notify(city, third) != 0)
            {
                advance();
                noodles->timer = 0x3C;
            }
            break;
        case 0x2B:
            EvActor_Wander(city, second);
            EvActor_MoveTo(city, third, third, 0);
            if (Player_DistanceToBuilding(city, third) < 2)
                advance();
            break;
        case 0x2C:
            EvActor_Wander(city, second);
            if (EvActor_PointCursor(city, third) != 0)
                advance();
            break;
        case 0x2E:
            EvActor_Wander(city, second);
            if (Person_IsOnOpenTile(city, third) != 0)
            {
                if (Player_DistanceToBuilding(city, second) < 2 && Player_DistanceToPerson(city, third) <= ps2f(1.0f))
                {
                    if (EvActor_PointCursor(city, third) != 0)
                        advance();
                }
                else
                {
                    noodles->timer = noodles->timer + 1;
                    if (noodles->timer >= 10)
                    {
                        if (!(Player_DistanceToPerson(city, third) < ps2f(3.0f)))
                            EvActor_PointCursor(city, third);
                        noodles->timer = 0;
                    }
                }
            }
            shelter();
            break;
        case 0x2F:
            EvActor_Wander(city, second);
            EvActor_MoveTo(city, third, first, 1);
            EvActor_MoveTo(city, first, first, 0);
            if (Person_IsAtBuilding(city, third, first) != 0 && Person_IsAtBuilding(city, first, first) != 0 &&
                EvActor_PointAtBuilding(city, address(first), address(first)) != 0)
            {
                record(third)->activity = 2;
                record(first)->activity = 2;
                advance();
            }
            break;
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
        case 0x38:
            EvActor_Wander(city, second);
            break;
        case 0x39:
            EvActor_Wander(city, second);
            if (Person_Distance(city, second, third) <= ps2f(3.0f) && Person_IsOnOpenTile(city, second) != 0 && Person_IsOnOpenTile(city, third) != 0 &&
                EvActor_PointCursor(city, third) != 0)
            {
                EvActor_Follow(city, third, second);
                const GuestPtr<MapPerson> who = record(third);
                who->flags = static_cast<uint16_t>(who->flags | 0x80u);
                EvActor_Release(city, second);
                advance();
                noodles->timer = 0;
            }
            else
            {
                noodles->timer = noodles->timer + 1;
                if (noodles->timer >= 10)
                {
                    if (!(Player_DistanceToPerson(city, third) < ps2f(3.0f)) && Person_IsOnOpenTile(city, third) != 0)
                        EvActor_PointCursor(city, third);
                    noodles->timer = 0;
                }
            }
            shelter();
            break;
        case 0x3A:
            EvActor_Release(city, second);
            if (Person_Distance(city, second, third) <= ps2f(1.0f))
            {
                noodles->timer = noodles->timer + 1;
                if (noodles->timer >= 4 && Player_DistanceToPerson(city, second) <= ps2f(1.0f) && EvActor_PointCursor(city, second) != 0)
                    advance();
            }
            else
                noodles->timer = 0;
            break;
        case 0x3B:
        case 0x3D:
        case 0x3F:
        case 0x41:
            EvActor_Release(city, second);
            if (EvActor_PointCursor(city, third) != 0)
                advance();
            break;
        case 0x3C:
        case 0x3E:
        case 0x40:
            EvActor_Release(city, second);
            if (EvActor_PointCursor(city, second) != 0)
                advance();
            break;
        case 0x42:
            EvActor_Release(city, second);
            if (EvActor_PointCursor(city, second) != 0)
            {
                EvActor_MoveTo(city, second, second, 0);
                advance();
            }
            break;
        case 0x43:
            EvActor_MoveTo(city, second, first, 0);
            EvActor_MoveTo(city, first, first, 0);
            if (Person_IsAtBuilding(city, third, first) != 0 && Person_IsAtBuilding(city, second, first) != 0 && Person_IsAtBuilding(city, first, first) != 0 &&
                EvActor_PointAtBuilding(city, address(first), address(first)) != 0)
            {
                EvActor_Follow(city, third, first);
                const GuestPtr<MapPerson> who = record(third);
                who->activity = 2;
                if (who->target != kNoObject)
                    leaveBuilding(who->target);
                who->target = first->object;
                who->schedState = 0xC;
                record(second)->activity = 2;
                record(first)->activity = 2;
                advance();
                noodles->month = city->monthCount + 1;
                noodles->time = 36000;
            }
            break;
        case 0x4E:
            EvActor_Wander(city, second);
            if (noodles->month <= city->monthCount && noodles->time <= city->clock)
            {
                EvActor_StopFollowing(city, third);
                EvActor_MoveTo(city, third, third, 0);
                EvActor_MoveTo(city, second, third, 1);
                advance();
            }
            break;
        case 0x4F:
            if (Person_IsAtBuilding(city, third, third) != 0 && Person_IsAtBuilding(city, second, third) != 0)
            {
                if (EvActor_Notify(city, second) != 0)
                {
                    advance();
                    noodles->timer = 0x3C;
                }
            }
            else
            {
                EvActor_MoveTo(city, third, third, 0);
                EvActor_MoveTo(city, second, third, 1);
            }
            break;
        case 0x50:
            EvActor_MoveTo(city, third, third, 0);
            EvActor_MoveTo(city, second, third, 1);
            noodles->timer = noodles->timer - 1;
            if (noodles->timer < 1)
                advance(-1);
            break;
        case 0x51:
            EvActor_MoveTo(city, third, third, 0);
            EvActor_MoveTo(city, second, third, 1);
            if (Player_DistanceToBuilding(city, third) < 2 && Person_IsAtBuilding(city, third, third) != 0 && Person_IsAtBuilding(city, second, third) != 0 &&
                EvActor_PointAtBuilding(city, address(second), address(third)) != 0)
            {
                record(third)->activity = 6;
                record(second)->activity = 10;
                advance();
            }
            break;
        case 0x57:
            EvActor_Follow(city, third, second);
            EvActor_MoveTo(city, second, second, 0);
            advance();
            break;
        case 0x58:
            EvActor_MoveTo(city, second, second, 0);
            EvActor_MoveTo(city, first, first, 0);
            if (Person_IsAtBuilding(city, third, first) != 0 && Person_IsAtBuilding(city, second, first) != 0 && Person_IsAtBuilding(city, first, first) != 0 &&
                EvActor_PointAtBuilding(city, address(first), address(first)) != 0)
            {
                const GuestPtr<MapPerson> host = record(first);
                const uint32_t px = port::load<uint32_t>(host.address), pz = port::load<uint32_t>(host.address + 8u);
                host->activity = 2;
                const GuestPtr<MapPerson> guest = record(second);
                guest->activity = 2;
                EvActor_Follow(city, second, first);
                port::store<uint32_t>(guest.address, px);
                port::store<uint32_t>(guest.address + 8u, pz);
                const GuestPtr<MapPerson> last = record(third);
                last->activity = 2;
                port::store<uint32_t>(last.address, px);
                port::store<uint32_t>(last.address + 8u, pz);
                const GuestPtr<MapObject> building = Map_GetRecord(map, first->object);
                ++building->headingHere;
                building->occupants = 0;
                advance();
                noodles->timer = 0x5A0;
            }
            break;
        case 0x61:
            noodles->timer = noodles->timer - 1;
            if (noodles->timer < 1)
                Event_Finish(city->events, city, 0);
            break;
        default:
            break;
        }
        if (data->var >= 0x20 && data->var < 0x58 && city->clock / 60 == 0x78)
            Map_GetRecord(map, first->object)->occupants = 0x14;
    }

    PORT_FN(EvNoodles_Update);
}
