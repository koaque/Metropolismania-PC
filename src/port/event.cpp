// City events: the handler slots of the event types (table 0x1AF0D0: Check, Start, Update, End, Slot10, Slot14),
// the event manager's talk slots and the waiting queue. Every handler gets the active event (EventData) and the
// city; Check slots say whether the event may start, Start and End mark and clear the buildings taking part
// (flag 0x200) and the wants held for them, Slot10 gives the talk a text and Slot14 a value.

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
            return GuestPtr<ActorRef>{data.address + 0x0Cu + 8u * static_cast<uint32_t>(index)};
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

        // Drops the wants the event held for its building type (event table +0x20), holds 0 to count - 1.
        void releaseWantHolds(GuestPtr<EventData> data, GuestPtr<CityScene> city, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                const int type = port::load<int16_t>(globals::eventTypesCopy.get() + 0x4Cu * data->kind + 0x20u);
                port::callGuest<void>(addr::Wants_RemoveEventHold, city->wants, type, i);
            }
        }

        // Entry `index` of the family type table (gp-0x7EE4).
        uint16_t familyTypeEntry(uint32_t index) { return port::load<uint16_t>(globals::familyTypeTable.get() + 2u * index); }
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

    int EventQueue_Find(GuestPtr<EventMgr>, int16_t id)
    {
        const uint32_t end = globals::kEventQueue + 2u * static_cast<uint32_t>(globals::eventQueueLength.get());
        int position = 0;
        for (uint32_t entry = globals::kEventQueue; entry < end; entry += 2u, ++position)
            if (port::load<uint16_t>(entry) == static_cast<uint16_t>(id))
                return position;
        return position;
    }

    void EventMgr_RequeueFirst(GuestPtr<EventMgr> events)
    {
        const uint32_t slots = events.address + 0x04u;
        EventQueue_InsertByPriority(static_cast<int>(globals::eventQueueObject.get()), GuestPtr<uint8_t>{slots}, 2, 0);
        sdk::memmove(slots, slots + 0x20u, 0x80);
        sdk::memset(slots + 0x80u, 0, 0x20);
    }

    // Removes the entry from the queue object: the ids after it move up (the length rounded down to whole ids,
    // counted from the entry) and the ends of priority levels `level` + 1 to 8 move back one (the last is the length).
    void EventQueue_RemoveAt(int queue, int level, GuestPtr<uint16_t> entry)
    {
        const uint32_t base = static_cast<uint32_t>(queue);
        const uint32_t end = base + 0x24u + 2u * port::load<uint32_t>(base + 0x20u);
        const int32_t bytes = static_cast<int32_t>(end - entry.address);
        sdk::memmove(entry.address, entry.address + 2u, (bytes / 2) * 2);
        for (int i = level + 1; i < 9; ++i)
            port::store<int32_t>(base + 4u * static_cast<uint32_t>(i), port::load<int32_t>(base + 4u * static_cast<uint32_t>(i)) - 1);
    }

    // The tile list of event `list` in the claim data: a count, then (record, 8 bytes) entries of 12 bytes. Setting
    // copies each entry's 8 bytes over the record's first 8; clearing restores them from the record's second 8.
    void Event_SetTileFlags(int list)
    {
        uint32_t entry = globals::claimData.get() + 4u * port::load<uint32_t>(globals::eventTileLists.get() + 4u * static_cast<uint32_t>(list));
        uint32_t count = port::load<uint32_t>(entry);
        entry += 4u;
        for (; count != 0; --count, entry += 12u)
        {
            const uint32_t record = globals::claimData.get() + 4u * port::load<uint32_t>(entry);
            port::store<uint8_t>(record + 0u, port::load<uint8_t>(entry + 4u));
            port::store<uint8_t>(record + 1u, port::load<uint8_t>(entry + 5u));
            port::store<uint16_t>(record + 2u, port::load<uint16_t>(entry + 6u));
            port::store<uint16_t>(record + 4u, port::load<uint16_t>(entry + 8u));
            port::store<uint16_t>(record + 6u, port::load<uint16_t>(entry + 10u));
        }
    }

    void Event_ClearTileFlags(int list)
    {
        uint32_t entry = globals::claimData.get() + 4u * port::load<uint32_t>(globals::eventTileLists.get() + 4u * static_cast<uint32_t>(list));
        uint32_t count = port::load<uint32_t>(entry);
        entry += 4u;
        for (; count != 0; --count, entry += 12u)
        {
            const uint32_t record = globals::claimData.get() + 4u * port::load<uint32_t>(entry);
            port::store<uint8_t>(record + 0u, port::load<uint8_t>(record + 8u));
            port::store<uint8_t>(record + 1u, port::load<uint8_t>(record + 9u));
            port::store<uint16_t>(record + 2u, port::load<uint16_t>(record + 10u));
            port::store<uint16_t>(record + 4u, port::load<uint16_t>(record + 12u));
            port::store<uint16_t>(record + 6u, port::load<uint16_t>(record + 14u));
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

        // The person leaves the building they are in: one that is neither their workplace nor their target and
        // counts its people (type flag 0x40) loses one, and their walk tiles reset (0x4000). When they were inside,
        // v0 becomes the building record (the actor functions leave it there).
        void leaveBuilding(GuestPtr<CityScene> city, GuestPtr<MapPerson> person, int32_t &v0)
        {
            if (person->inside == kNoObject)
                return;
            const GuestPtr<MapObject> building = Map_GetRecord(city->map, person->inside);
            v0 = static_cast<int32_t>(building.address);
            if (building->id != person->work && building->id != person->target && (typeOf(building)->flags & 0x40u) != 0 &&
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

    // The buildings in the 80 tile lists whose claim (claim section B entry +2) is 0 drop it: meter 0, claim slot
    // 0x18 (none), flags 8 and 0x40 cleared. Those already at slot 0x18 are left.
    void Event_ReleaseTiles(GuestPtr<CityScene> city)
    {
        port::GuestFrame<0x70> frame;
        const GuestPtr<uint32_t> count{frame.ptr().address + 0x6Cu};
        for (int list = 0; list < 80; ++list)
        {
            const uint32_t items = Map_GetTileList(city->map, list, count).address;
            const uint32_t end = items + 2u * *count;
            for (uint32_t item = items; item < end; item += 2u)
            {
                const GuestPtr<MapObject> building = Map_GetRecord(city->map, port::load<uint16_t>(item));
                if (building->claimSlot == 0x18)
                    continue;
                const int section = Claim_GetSectionB(typeOf(building)->claimB);
                if (port::load<uint16_t>(static_cast<uint32_t>(section) + 8u * building->claimSlot + 2u) != 0)
                    continue;
                building->claimMeter = 0;
                building->claimSlot = 0x18;
                building->flags &= 0xFFF7u;
                building->flags &= 0xFFBFu;
            }
        }
    }

    // The building's footprint widened by `margin` on each side: x0, z0, x1, z1 (x1 and z1 past the last tile).
    void Building_GetFootprint(GuestPtr<MapObject> building, int margin, GuestPtr<uint32_t> rect)
    {
        const uint32_t size = typeOf(building)->size;
        rect[0] = building->tile & 0x7Fu;
        rect[1] = static_cast<uint32_t>(building->tile >> 7);
        rect[2] = static_cast<uint32_t>(margin) + (rect[0] + size);
        rect[3] = static_cast<uint32_t>(margin) + (rect[1] + size);
        rect[0] = rect[0] - static_cast<uint32_t>(margin);
        rect[1] = rect[1] - static_cast<uint32_t>(margin);
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

    // The person heads somewhere that is neither their workplace nor their target.
    int EvForgetfulGranny_Check(GuestPtr<EventData>, GuestPtr<CityScene>, GuestPtr<MapPerson> person)
    {
        const uint16_t destination = person->destination;
        if (destination == person->work)
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
        const uint32_t first = Map_GetRecord(city->map, 0).address;
        const uint32_t end = first + 0x3F80u * 0x1Cu;
        int count = 0;
        for (uint32_t object = first; object < end; object += 0x1Cu)
        {
            if (port::load<uint16_t>(object) != 10)
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

    // A cave man (type 0x113 in tile list 0x15, entrance flag 0x20, no claim met or meter maxed, claim meter level 0)
    // whose family's first member has a person record: actor 6 becomes that person at the cave, actor 5 family type
    // entry 0x10C's third member, actor 7 entry 0xB0 (the other actors none), the cave is marked.
    int EvCaveMan_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        port::GuestFrame<0x70> frame;
        const GuestPtr<uint32_t> count{frame.ptr().address + 0x6Cu};
        const uint32_t items = Map_GetTileList(city->map, 0x15, count).address;
        const uint32_t end = items + 2u * *count;
        for (uint32_t item = items; item < end; item += 2u)
        {
            const GuestPtr<MapObject> cave = Map_GetRecord(city->map, port::load<uint16_t>(item));
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
            actorAt(data, 5)->family = familyTypeEntry(0x10C);
            actorAt(data, 5)->member = 2;
            actorAt(data, 7)->family = familyTypeEntry(0xB0);
            return 1;
        }
        return 0;
    }

    // A resident (character types 7 to 29) inside a shop (category 6 to 10) that is neither their workplace nor their
    // target and has no maxed meter, standing outside the shop's footprint widened by 5: the shop becomes actor 1
    // (its family's first member).
    int EvShoppingTogether_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        port::GuestFrame<0x60> frame;
        if (person->charType < 7 || person->charType >= 30 || person->inside == kNoObject)
            return 0;
        const GuestPtr<MapObject> shop = Map_GetRecord(city->map, person->inside);
        const uint8_t category = typeOf(shop)->category;
        if (category < 6 || category >= 11 || shop->id == person->work || shop->id == person->target || (shop->flags & 0x40u) != 0)
            return 0;
        const GuestPtr<int32_t> rect{frame.ptr().address + 0x50u};
        Building_GetFootprint(shop, 5, GuestPtr<uint32_t>{rect.address});
        const int z = sdk::fptosi(ps2f(64.0f) - ps2f(person->z));
        const int x = sdk::fptosi(ps2f(64.0f) + ps2f(person->x));
        if (x >= rect[0] && x < rect[2] && z >= rect[1] && z < rect[3])
            return 0;
        actorAt(data, 1)->object = shop->id;
        actorAt(data, 1)->family = shop->family;
        actorAt(data, 1)->member = 0;
        findPerson(city, actorAt(data, 1)->family, actorAt(data, 1)->member);
        return 1;
    }

    // The thief strikes when the families of type entries 0xB8 and 0x104 live in the city with a home, and there is a
    // building to rob (mm_EvThief_FindBuilding, category 5, else 1): actor 0 the first family, actors 1 and 2 the
    // second family's first two members, actor 3 the robbed building's family; the buildings are marked.
    int EvThief_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const uint16_t familyA = familyTypeEntry(0xB8);
        if (Family_InCity(familyA) == 0)
            return 0;
        const GuestPtr<MapObject> homeA = Map_FindFamilyHome(city->map, familyA);
        if (!homeA)
            return 0;
        const uint16_t familyB = familyTypeEntry(0x104);
        if (Family_InCity(familyB) == 0)
            return 0;
        const GuestPtr<MapObject> homeB = Map_FindFamilyHome(city->map, familyB);
        if (!homeB)
            return 0;
        const GuestPtr<int32_t> rect{0x001BA8D0u};
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

    // The person's workplace is a shop of category 7 or 9 (not type 0x100), and unless the workplace has flag 0x100,
    // (family + member) % 12 is this month.
    int EvAdvertise_Check(GuestPtr<EventData>, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        const GuestPtr<MapObject> work = Map_GetRecord(city->map, person->work);
        if ((work->flags & 0x100u) == 0 && city->monthCount % 12 != (person->family + person->member) % 12)
            return 0;
        const uint8_t category = GuestPtr<BuildingType>{globals::buildingTypeAddress(work->type)}->category;
        if (category != 9 && category != 7)
            return 0;
        return work->type != 0x100 ? 1 : 0;
    }

    // ---- Start slots

    // The start slots below leave a result in v0 that the talk script action reads (Event_CallHandlerSlot passes
    // it on): the record they marked last, or the register as found.
    int64_t EvCaveMan_Start(GuestPtr<EventData>, GuestPtr<CityScene>) { return port::guestV0Wide(); }

    // The message comes from family type entry 0xB1's first member, with no person or building.
    int64_t EvMsg_Start(GuestPtr<EventData> data, GuestPtr<CityScene>)
    {
        const int64_t found = port::guestV0Wide();
        data->actor.person = kNoPerson;
        data->actor.object = kNoObject;
        data->actor.family = familyTypeEntry(0xB1);
        data->actor.member = 0;
        return found;
    }

    GuestPtr<MapObject> EvMissingChildren_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city) { return markBuilding(city, data->actor.object); }
    GuestPtr<MapObject> EvAdvertise_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city) { return markBuilding(city, data->actor.object); }

    // Marks the building and remembers the month it started in.
    GuestPtr<MapObject> EvUselessMiddleAged_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<MapObject> record = markBuilding(city, data->actor.object);
        data->unk_14 = city->monthCount;
        return record;
    }

    // Marks the building, detaches the actor and keeps their distance from the player.
    int EvEscort_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        markBuilding(city, data->actor.object);
        const GuestPtr<ActorRef> actor = GuestPtr<ActorRef>::of(data->actor);
        EvActor_Detach(city, actor);
        const int distance = Player_DistanceToBuilding(city, actor);
        data->unk_18 = distance;
        return distance;
    }

    // Marks both actors' buildings and sets the timer (+0x1C) to 180.
    GuestPtr<MapObject> EvShoppingTogether_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        markBuilding(city, actorAt(data, 0)->object);
        const GuestPtr<MapObject> second = markBuilding(city, actorAt(data, 1)->object);
        port::store<int32_t>(data.address + 0x1Cu, 180);
        return second;
    }

    // Marks both actors' buildings; the delivery state (+0x34) starts at 2.
    GuestPtr<MapObject> EvDeliveryMan_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        markBuilding(city, actorAt(data, 0)->object);
        const GuestPtr<MapObject> second = markBuilding(city, actorAt(data, 1)->object);
        port::store<int32_t>(data.address + 0x34u, 2);
        return second;
    }

    int64_t EvShopping_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city) { return markActorBuildings(city, data, 0, 5); }

    // The noodle shop event lists its six buildings from +0x2C (actors 4 to 9).
    int64_t EvNoodles_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city) { return markActorBuildings(city, data, 4, 6); }

    // Six actors without person or building: the first two from family type entries 0xAB and 0xAC, the others
    // from entry 0xAD and the three after it (members unchanged).
    int64_t EvCrazyCult_Start(GuestPtr<EventData> data, GuestPtr<CityScene>)
    {
        const int64_t found = port::guestV0Wide();
        actorAt(data, 0)->family = familyTypeEntry(0xAB);
        actorAt(data, 1)->family = familyTypeEntry(0xAC);
        for (int i = 0; i < 4; ++i)
            actorAt(data, 2 + i)->family = static_cast<uint16_t>(familyTypeEntry(0xAD) + i);
        for (int i = 0; i < 6; ++i)
        {
            actorAt(data, i)->person = kNoPerson;
            actorAt(data, i)->object = kNoObject;
        }
        return found;
    }

    // The adoption comes from family type entry 0xB1's first member; the applicants (families 0 to 9999 with an
    // application) are listed as words from +0x24, then 24 bytes from 0x1B37D0 are copied to +0x4C (through the
    // original stack, then by memcpy, whose result, the destination, is what the start slot returns).
    uint32_t EvAdoption_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        port::GuestFrame<0x70> frame;
        data->actor.person = kNoPerson;
        data->actor.object = kNoObject;
        data->actor.family = familyTypeEntry(0xB1);
        data->actor.member = 0;
        uint32_t out = data.address + 0x24u;
        for (uint32_t family = 0; family < 10000; ++family)
        {
            if (Applicants_Find(city->applicants, family) == 0)
                continue;
            port::store<uint32_t>(out, family);
            out += 4u;
        }
        const uint32_t scratch = frame.ptr().address + 0x50u;
        for (uint32_t i = 0; i < 0x18u; i += 4u)
            port::store<uint32_t>(scratch + i, port::load<uint32_t>(globals::kAdoptionStart + i));
        return sdk::memcpy(data.address + 0x4Cu, scratch, 0x18);
    }

    // ---- Update slots

    // Phase 1 (var): from 9:00 on a later month than it started, the actor is told (mm_EvActor_Notify) and phase 2
    // starts with a 60-frame timer; phase 2 goes back to 1 when the timer runs out.
    void EvUselessMiddleAged_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        if (data->var == 2)
        {
            if (--data->unk_18 <= 0)
                --data->var;
            return;
        }
        if (data->var != 1 || city->monthCount == data->unk_14 || city->clock < 32400)
            return;
        if (EvActor_Notify(city, GuestPtr<ActorRef>::of(data->actor)) == 0)
            return;
        data->unk_18 = 60;
        ++data->var;
    }

    namespace
    {
        // The message events' phases (var): 1 waits until the actor can be told (mm_EvActor_Notify), then a phone
        // call is up (+0x1C) for 60 frames, plus the minutes left until 6:00 when it comes before then, or 360 more
        // from 23:00; phase 2 counts that down and returns true when it runs out (the call taken down).
        bool messagePhase(GuestPtr<EventData> data, GuestPtr<CityScene> city)
        {
            if (data->var == 2)
            {
                if (--data->unk_14 > 0)
                    return false;
                port::store<int32_t>(data.address + 0x1Cu, 0);
                return true;
            }
            if (data->var != 1 || EvActor_Notify(city, GuestPtr<ActorRef>::of(data->actor)) == 0)
                return false;
            ++data->var;
            port::store<int32_t>(data.address + 0x1Cu, 1);
            data->unk_14 = 60;
            const int32_t clock = city->clock;
            if (clock < 21600)
                data->unk_14 += 360 - clock / 60;
            else if (clock >= 82801)
                data->unk_14 += 360;
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

    // The message is repeated: back to waiting for the first four calls, finished after the fifth (+0x18 counts).
    void EvMsg_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        if (!messagePhase(data, city))
            return;
        if (++data->unk_18 < 5)
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
            port::store<int32_t>(data.address + 0x1Cu, 0);
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
        if (++data->unk_18 < 5)
            --data->var;
        else
            ++data->var;
    }

    // The advertisement's phases (var): 1 waits for the opening minute (event table +0x2E), 2 and 4 wait until the
    // actor can be told, 3 and 5 count 60 frames back down a phase, 6 ends the event after 240 frames.
    void EvAdvertise_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const uint8_t phase = data->var;
        switch (phase)
        {
        case 1:
            if (city->clock / 60 >= port::load<uint16_t>(globals::eventTypesCopy.get() + 0x4Cu * data->kind + 0x2Eu))
                data->var = static_cast<uint8_t>(phase + 1);
            break;
        case 2:
        case 4:
            if (EvActor_Notify(city, GuestPtr<ActorRef>::of(data->actor)) == 0)
                break;
            data->unk_14 = 60;
            ++data->var;
            break;
        case 3:
        case 5:
            if (--data->unk_14 <= 0)
                --data->var;
            break;
        case 6:
        {
            const int32_t frames = port::load<int32_t>(data.address + 0x1Cu) + 1;
            port::store<int32_t>(data.address + 0x1Cu, frames);
            if (frames >= 240)
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
        const uint32_t people = city->map->people.address;
        for (uint32_t p = people; p < people + 6000u * 0x30u; p += 0x30u)
        {
            const GuestPtr<MapPerson> person{p};
            if (person->charType == 0)
                continue;
            const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
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

    // Withdraws the message's phone call if it posted one (+0x1C).
    void EvMsg_End(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        if (port::load<uint32_t>(data.address + 0x1Cu) == 0)
            return;
        PhoneCall_Remove(city->phoneCalls, city, data->actor.family, data->actor.member);
    }

    // ---- Slot10: texts for the talk

    // The claim variant text of the chosen applicant's claim (byte table from +0x4C, picked by +0x20); 0 for other
    // indexes.
    int EvAdoption_Slot10(GuestPtr<EventData> data, GuestPtr<CityScene>, int index)
    {
        if (index != 0)
            return 0;
        const uint32_t pick = port::load<uint32_t>(data.address + 0x20u);
        return Claim_PickVariant(port::load<uint8_t>(data.address + 0x4Cu + 4u * pick));
    }

    // Texts 0 to 3: the goods of the shop (+0x34); 4: the texts table itself; above: the bill (+0x4C) as money.
    GuestPtr<char> EvShopping_Slot10(GuestPtr<EventData> data, GuestPtr<CityScene>, int index)
    {
        if (index < 4)
        {
            const uint32_t shop = globals::shoppingTexts.get() + 0x28u * port::load<uint32_t>(data.address + 0x34u);
            return GuestPtr<char>{port::load<uint32_t>(0x001BDAA4u) + port::load<uint16_t>(shop + 2u * static_cast<uint32_t>(index) + 0x20u)};
        }
        if (index == 4)
            return GuestPtr<char>{globals::shoppingTexts.get()};
        Text_FormatMoney(GuestPtr<char>{globals::kEventMoneyTextB}, static_cast<int>(port::load<uint32_t>(data.address + 0x4Cu)), 7, 1);
        return GuestPtr<char>{globals::kEventMoneyTextB};
    }

    // Text 0: the catalogue item (+0x1C entry, +0x20 item); 1: its price as money; 0 for others.
    GuestPtr<char> EvDeliveryMan_Slot10(GuestPtr<EventData> data, GuestPtr<CityScene>, int index)
    {
        const uint32_t item = globals::kDeliveryCatalogue + 0x184u * port::load<uint32_t>(data.address + 0x1Cu) +
                              0x20u * port::load<uint32_t>(data.address + 0x20u);
        if (index == 0)
            return GuestPtr<char>{item + 4u};
        if (index != 1)
            return GuestPtr<char>{0};
        Text_FormatMoney(GuestPtr<char>{globals::kEventMoneyTextA}, port::load<uint16_t>(item + 0x22u), 15, 1);
        return GuestPtr<char>{globals::kEventMoneyTextA};
    }

    // ---- Slot14: values for the talk (all 0 unless the argument is 0)

    int EvShoppingTogether_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene>, GuestPtr<ActorRef>, int mode)
    {
        return mode == 0 ? port::load<int32_t>(data.address + 0x28u) : 0;
    }

    // Whether the party (+0x28) has a quarter of the population.
    int EvFarewellParty_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef>, int mode)
    {
        if (mode != 0)
            return 0;
        return port::load<int32_t>(data.address + 0x28u) >= city->map->population / 4 ? 1 : 0;
    }

    // How the escorted person keeps up: 4 within 2 tiles of the player, 3 within 6, else 1 closer than last time,
    // 2 farther, 0 the same; the distance is kept (+0x18).
    int EvBodyguard_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef>, int mode)
    {
        if (mode != 0)
            return 0;
        const int distance = Player_DistanceToBuilding(city, GuestPtr<ActorRef>::of(data->actor));
        int state;
        if (distance < 2)
            state = 4;
        else if (distance < 6)
            state = 3;
        else if (distance < data->unk_18)
            state = 1;
        else if (data->unk_18 < distance)
            state = 2;
        else
            state = 0;
        data->unk_18 = distance;
        return state;
    }

    // Argument 0: the thief struck at the actor's building (its claim meter maxed, the fourth actor cleared, the
    // count +0x3C raised) and the next month (+0x2C) noted; 0. Argument 1: by the count, 0 none, 1 up to 3, 2 at 4,
    // 3 at 5, 4 from 6.
    int EvThief_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        if (mode == 1)
        {
            const int32_t count = port::load<int32_t>(data.address + 0x3Cu);
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
            port::store<int32_t>(data.address + 0x3Cu, port::load<int32_t>(data.address + 0x3Cu) + 1);
        }
        port::store<int32_t>(data.address + 0x2Cu, city->monthCount + 1);
        return 0;
    }

    // Argument 0: +0x28. Argument 1: whether the actor's building and the event's (widened by 5) overlap.
    int EvMissingChildren_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        if (mode == 0)
            return port::load<int32_t>(data.address + 0x28u);
        if (mode != 1)
            return 0;
        port::GuestFrame<0x60> frame;
        const GuestPtr<int32_t> a{frame.ptr().address + 0x40u}, b{frame.ptr().address + 0x50u};
        Building_GetFootprint(Map_GetRecord(city->map, data->actor.object), 5, GuestPtr<uint32_t>{a.address});
        Building_GetFootprint(Map_GetRecord(city->map, actor->object), 0, GuestPtr<uint32_t>{b.address});
        return a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3] ? 1 : 0;
    }

    // ---- the party popup

    void PartyPopup_Draw(int popup, int pass)
    {
        if (pass == 2)
            Dma_AppendCall_14C200(static_cast<int>(port::load<uint32_t>(static_cast<uint32_t>(popup) + 4u)));
    }

    // Shows notice 1 and closes on Circle (0x40).
    void PartyPopup_Update(int, GuestPtr<CityScene> city)
    {
        Notice_Show(city->notice, 1);
        if ((Pad_Pressed(4) & 0x40u) != 0)
            CityScene_PopMode(city);
    }

    PORT_FN(Event_CountActive);
    PORT_FN(EventQueue_Find);
    PORT_FN(EventMgr_RequeueFirst);
    PORT_FN(EventQueue_RemoveAt);
    PORT_FN(Event_SetTileFlags);
    PORT_FN(Event_ClearTileFlags);
    PORT_FN(EvActor_StopFollowing);
    PORT_FN(EvActor_Detach);
    PORT_FN(EvActor_Follow);
    PORT_FN(EvActor_Release);
    PORT_FN(EvActor_Notify);
    PORT_FN(Event_ReleaseTiles);
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
    PORT_FN(EvAdoption_Slot10);
    PORT_FN(EvShopping_Slot10);
    PORT_FN(EvDeliveryMan_Slot10);
    PORT_FN(EvShoppingTogether_Slot14);
    PORT_FN(EvFarewellParty_Slot14);
    PORT_FN(EvBodyguard_Slot14);
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
    PORT_FN(EvThief_Slot14);
    PORT_FN(EvMissingChildren_Slot14);
    PORT_FN(PartyPopup_Draw);
    PORT_FN(PartyPopup_Update);

    // ---- building finders, more talk values and phases (batch 38)

    namespace
    {
        // Whether the building's footprint overlaps the rectangle (x0, z0, x1, z1; the ends excluded).
        bool overlapsRect(GuestPtr<MapObject> building, GuestPtr<int32_t> rect)
        {
            const int size = typeOf(building)->size;
            const int x = building->tile & 0x7F, z = building->tile >> 7;
            return rect[0] < x + size && x < rect[2] && rect[1] < z + size && z < rect[3];
        }

        // A building that can take part: its family lives in the city and it has no claim met and no maxed meter.
        bool available(GuestPtr<MapObject> building) { return Family_InCity(building->family) != 0 && (building->flags & 0x240u) == 0; }
    }

    // The first available building of `type` in tile list `list` that overlaps the rectangle.
    GuestPtr<MapObject> EvAlien_FindBuilding(GuestPtr<CityScene> city, int list, uint32_t type, GuestPtr<int32_t> rect)
    {
        port::GuestFrame<0x80> frame;
        const GuestPtr<uint32_t> count{frame.ptr().address + 0x7Cu};
        const uint32_t items = Map_GetTileList(city->map, list, count).address;
        const uint32_t end = items + 2u * *count;
        for (uint32_t item = items; item < end; item += 2u)
        {
            const GuestPtr<MapObject> building = Map_GetRecord(city->map, port::load<uint16_t>(item));
            if (building->type == type && available(building) && overlapsRect(building, rect))
                return building;
        }
        return GuestPtr<MapObject>{0};
    }

    // The first available building of `category` in any of the 80 tile lists that overlaps the rectangle.
    GuestPtr<MapObject> EvThief_FindBuilding(GuestPtr<CityScene> city, uint32_t category, GuestPtr<int32_t> rect)
    {
        port::GuestFrame<0x90> frame;
        const GuestPtr<uint32_t> count{frame.ptr().address + 0x8Cu};
        for (int list = 0; list < 80; ++list)
        {
            const uint32_t items = Map_GetTileList(city->map, list, count).address;
            const uint32_t end = items + 2u * *count;
            for (uint32_t item = items; item < end; item += 2u)
            {
                const GuestPtr<MapObject> building = Map_GetRecord(city->map, port::load<uint16_t>(item));
                if (typeOf(building)->category == category && available(building) && overlapsRect(building, rect))
                    return building;
            }
        }
        return GuestPtr<MapObject>{0};
    }

    // The building the alien lands on at (x, z): of the (tile list, type) pairs at 0x1B3760 (ended by list 0), the
    // first building whose family lives in the city, without a maxed meter, whose footprint widened by 1 holds the
    // point.
    GuestPtr<MapObject> EvAlien_PickTarget(GuestPtr<CityScene> city, int x, int z)
    {
        port::GuestFrame<0xC0> frame;
        const GuestPtr<uint32_t> count{frame.ptr().address + 0xBCu};
        const GuestPtr<int32_t> rect{frame.ptr().address + 0xA0u};
        for (uint32_t pair = 0x001B3760u; port::load<uint16_t>(pair) != 0; pair += 4u)
        {
            const uint32_t items = Map_GetTileList(city->map, port::load<uint16_t>(pair), count).address;
            const uint32_t end = items + 2u * *count;
            for (uint32_t item = items; item < end; item += 2u)
            {
                const GuestPtr<MapObject> building = Map_GetRecord(city->map, port::load<uint16_t>(item));
                if (building->type != port::load<uint16_t>(pair + 2u) || Family_InCity(building->family) == 0 || (building->flags & 0x40u) != 0)
                    continue;
                Building_GetFootprint(building, 1, GuestPtr<uint32_t>{rect.address});
                if (x >= rect[0] && x < rect[2] && z >= rect[1] && z < rect[3])
                    return building;
            }
        }
        return GuestPtr<MapObject>{0};
    }

    // Argument 0: whether the actor's home (widened by 10) and the event's home overlap, or else whether the actor's
    // person and the event's head for the same building.
    int EvRelationship_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        if (mode != 0)
            return 0;
        port::GuestFrame<0x90> frame;
        const GuestPtr<MapObject> homeA = Map_FindFamilyHome(city->map, actor->family);
        if (homeA)
        {
            const GuestPtr<MapObject> homeB = Map_FindFamilyHome(city->map, data->actor.family);
            if (homeB)
            {
                const GuestPtr<int32_t> a{frame.ptr().address + 0x70u}, b{frame.ptr().address + 0x80u};
                Building_GetFootprint(homeA, 10, GuestPtr<uint32_t>{a.address});
                Building_GetFootprint(homeB, 0, GuestPtr<uint32_t>{b.address});
                if (a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3])
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

    // Whether there is a shop (type flag 0x40) with a walkable entrance, other than the person's workplace, that the
    // person is not within 5 tiles of (the shop at the entrance tile, its footprint widened by 5).
    int EvActor_FindShop(GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        port::GuestFrame<0xC0> frame;
        const GuestPtr<uint32_t> count{frame.ptr().address + 0xBCu};
        for (int list = 0; list < 80; ++list)
        {
            const uint32_t items = Map_GetTileList(city->map, list, count).address;
            const uint32_t end = items + 2u * *count;
            for (uint32_t item = items; item < end; item += 2u)
            {
                const GuestPtr<MapObject> shop = Map_GetRecord(city->map, port::load<uint16_t>(item));
                if ((typeOf(shop)->flags & 0x40u) == 0 || Map_TileIndexFlag10(city->map, shop->entranceTile) == 0 || shop->id == person->work)
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
        // The crazy cult's waiting phases (shared by phases 1 and 3): wait for the month (+0x3C) and the time
        // (+0x40), tell the actor (then 60 frames, the time six hours later), or count those frames back down a
        // phase (the time six hours back).
        void cultWait(GuestPtr<EventData> data, GuestPtr<CityScene> city, uint32_t actor)
        {
            if (city->monthCount < port::load<int32_t>(data.address + 0x3Cu) || city->clock < port::load<int32_t>(data.address + 0x40u))
                return;
            if (EvActor_Notify(city, GuestPtr<ActorRef>{actor}) == 0)
                return;
            ++data->var;
            port::store<int32_t>(data.address + 0x44u, 60);
            port::store<int32_t>(data.address + 0x40u, port::load<int32_t>(data.address + 0x40u) + 21600);
        }

        void cultCountDown(GuestPtr<EventData> data)
        {
            const int32_t frames = port::load<int32_t>(data.address + 0x44u) - 1;
            port::store<int32_t>(data.address + 0x44u, frames);
            if (frames > 0)
                return;
            port::store<int32_t>(data.address + 0x40u, port::load<int32_t>(data.address + 0x40u) - 21600);
            --data->var;
        }
    }

    // Phases 4 to 9: actor 2 waits and is told (4 to 7), then actor 3's family home is found (8) and its person (9,
    // which sets the next wait to the next month at 7:00).
    void EvCrazyCult_Phase1(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        switch (data->var)
        {
        case 4:
        case 6:
            cultWait(data, city, data.address + 0x1Cu);
            break;
        case 5:
        case 7:
            cultCountDown(data);
            break;
        case 8:
            if (const GuestPtr<MapObject> home = Map_FindFamilyHome(city->map, port::load<uint16_t>(data.address + 0x28u)))
            {
                port::store<uint16_t>(data.address + 0x26u, home->id);
                home->flags |= 0x200u;
                data->var = 9;
            }
            break;
        case 9:
            if (const GuestPtr<MapPerson> person = findPerson(city, port::load<uint16_t>(data.address + 0x28u), port::load<uint16_t>(data.address + 0x2Au)))
            {
                port::store<uint16_t>(data.address + 0x24u, person->index);
                data->var = 10;
                port::store<int32_t>(data.address + 0x3Cu, city->monthCount + 1);
                port::store<int32_t>(data.address + 0x40u, 25200);
            }
            break;
        default:
            break;
        }
    }

    // Phases 0x12 to 0x15: actor 0 waits and is told (0x12, 0x13), then its family home is found (0x14) and its
    // person (0x15).
    void EvCrazyCult_Phase3(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        switch (data->var)
        {
        case 0x12:
            cultWait(data, city, data.address + 0x0Cu);
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

    // The delivery man's phases (jump table 0x1BA6C0): 1 walks to actor 1 for 120 frames, 2 and 5 tell actor 0
    // (60 frames), 3 and 6 count down, 4 waits 60 frames, 7 ends the event after 240.
    void EvDeliveryMan_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const auto tick = [&](uint32_t offset) {
            const int32_t value = port::load<int32_t>(data.address + offset) + 1;
            port::store<int32_t>(data.address + offset, value);
            return value;
        };
        switch (data->var)
        {
        case 1:
            EvActor_MoveTo(city, actorAt(data, 1), actorAt(data, 1), 0);
            if (tick(0x28u) < 120)
                break;
            port::store<int32_t>(data.address + 0x34u, 0);
            ++data->var;
            break;
        case 2:
        case 5:
            if (EvActor_Notify(city, GuestPtr<ActorRef>::of(data->actor)) == 0)
                break;
            port::store<int32_t>(data.address + 0x24u, 60);
            ++data->var;
            break;
        case 3:
        case 6:
        {
            const int32_t frames = port::load<int32_t>(data.address + 0x24u) - 1;
            port::store<int32_t>(data.address + 0x24u, frames);
            if (frames <= 0)
                --data->var;
            break;
        }
        case 4:
            if (tick(0x2Cu) < 60)
                break;
            port::store<int32_t>(data.address + 0x34u, 1);
            ++data->var;
            break;
        case 7:
            if (tick(0x30u) >= 240)
                Event_Finish(city->events, city, 0);
            break;
        default:
            break;
        }
    }

    PORT_FN(EvAlien_FindBuilding);
    PORT_FN(EvThief_FindBuilding);
    PORT_FN(EvAlien_PickTarget);
    PORT_FN(EvRelationship_Slot14);
    PORT_FN(EvActor_FindShop);
    PORT_FN(EvCrazyCult_Phase1);
    PORT_FN(EvCrazyCult_Phase3);
    PORT_FN(EvDeliveryMan_Update);

    // ---- batch 38

    namespace
    {
        // Whether a claim section C (four-byte entries, the id in the low half, ending at 0) lists the id.
        bool sectionCLists(uint32_t sectionC, uint16_t id)
        {
            for (uint32_t entry = sectionC; port::load<uint16_t>(entry) != 0; entry += 4u)
                if (port::load<uint16_t>(entry) == id)
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
        const auto sectionC = static_cast<uint32_t>(Claim_GetSectionC(GuestPtr<BuildingType>{globals::buildingTypeAddress(objB->type)}->claimC));
        const GuestPtr<MapObject> objA = Map_GetRecord(city->map, actorA->object);
        for (auto entry = static_cast<uint32_t>(Claim_GetSectionB(GuestPtr<BuildingType>{globals::buildingTypeAddress(objA->type)}->claimB));
             port::load<uint16_t>(entry + 2u) != 0; entry += 8u)
        {
            if (port::load<uint8_t>(entry) != 0)
                continue;
            const uint16_t id = port::load<uint16_t>(entry + 2u);
            if (id < 0x8000)
            {
                if (sectionCLists(sectionC, id))
                    return 1;
                continue;
            }
            for (auto group = static_cast<uint32_t>(Claim_GetSectionD(id)); port::load<uint16_t>(group) != 0; group += 2u)
                if (sectionCLists(sectionC, port::load<uint16_t>(group)))
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
        // arrive or there is room (capacity +0x140).
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
            mover = Map_FindPersonOfFlaggedType(city->map, static_cast<int16_t>(actor->person));
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

    // In the first minute of the day the person takes the weather flag. Outside or at work, the person goes into
    // a shop away from them: tile lists from a random one on (64 tries), the first shop (type flag 0x40) with a
    // walkable entrance, not their workplace, that they are not within 5 tiles of; none found leaves them outside.
    void EvActor_Wander(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor)
    {
        port::GuestFrame<0xD0> frame;
        const GuestPtr<uint32_t> count{frame.ptr().address + 0xCCu};
        const GuestPtr<MapPerson> person = city->map->people + actor->person;
        if (city->clock / 60 == 0)
            weatherFlag(city, person);
        if (person->inside != kNoObject && person->inside != person->work)
            return;
        int list = Random_TimerMix() & 0x3F;
        for (int tries = 0; tries < 0x40; ++tries)
        {
            const uint32_t items = Map_GetTileList(city->map, list, count).address;
            if (++list >= 0x40)
                list = 0;
            for (uint32_t i = 0, item = items; i < *count; ++i, item += 2u)
            {
                const GuestPtr<MapObject> shop = Map_GetRecord(city->map, port::load<uint16_t>(item));
                if ((typeOf(shop)->flags & 0x40u) == 0 || Map_TileIndexFlag10(city->map, shop->entranceTile) == 0 || shop->id == person->work)
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
        // A resident for a romance event near the person's workplace (its footprint widened by 5): in the tile lists
        // of the zero-ended table, a building other than the workplace whose family lives in the city, without a
        // maxed meter and with its claim meter at level 0, overlapping that area, with a resident of that family
        // who passes the test (list id, person). That resident becomes the event's actor family and member.
        template <typename Suits>
        int findSuitor(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person, uint32_t lists, Suits suits)
        {
            port::GuestFrame<0x30> frame;
            const uint32_t sp = frame.ptr().address;
            const GuestPtr<int32_t> area{sp}, rect{sp + 0x10u};
            const GuestPtr<uint32_t> count{sp + 0x20u};
            const GuestPtr<uint16_t> cursor{sp + 0x24u};
            Building_GetFootprint(Map_GetRecord(city->map, person->work), 5, GuestPtr<uint32_t>{area.address});
            for (uint32_t list = lists; port::load<int32_t>(list) != 0; list += 4u)
            {
                const int32_t id = port::load<int32_t>(list);
                const uint32_t items = Map_GetTileList(city->map, id, count).address;
                const uint32_t end = items + 2u * *count;
                for (uint32_t item = items; item < end; item += 2u)
                {
                    if (port::load<uint16_t>(item) == person->work)
                        continue;
                    const GuestPtr<MapObject> building = Map_GetRecord(city->map, port::load<uint16_t>(item));
                    if (Family_InCity(building->family) == 0 || (building->flags & 0x40u) != 0 || ClaimMeter_Level(building->claimMeter) != 0)
                        continue;
                    Building_GetFootprint(building, 0, GuestPtr<uint32_t>{rect.address});
                    if (!(area[0] < rect[2] && rect[0] < area[2] && area[1] < rect[3] && rect[1] < area[3]))
                        continue;
                    *cursor = building->firstPerson;
                    while (const GuestPtr<MapPerson> resident = People_Next(city->map, cursor))
                    {
                        const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * resident->charType)};
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

    // Character types 0x10 to 0x13 (from list 1 only members 2 and up), lists at 0x1B3810.
    int EvBoyfriend_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        return findSuitor(data, city, person, 0x001B3810u, [](int32_t list, GuestPtr<MapPerson> resident) {
            return !(list == 1 && resident->member < 2) && resident->charType >= 0x10 && resident->charType < 0x14;
        });
    }

    // Character types 0xB to 0xE (from list 1 only members 2 and up), lists at 0x1B37F0.
    int EvGirlfriend_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        return findSuitor(data, city, person, 0x001B37F0u, [](int32_t list, GuestPtr<MapPerson> resident) {
            return !(list == 1 && resident->member < 2) && resident->charType >= 0xB && resident->charType < 0xF;
        });
    }

    // Member 1 of character type 0x10 to 0x13, lists at 0x1BD9E0.
    int EvAffair_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        return findSuitor(data, city, person, 0x001BD9E0u, [](int32_t, GuestPtr<MapPerson> resident) {
            return resident->member == 1 && resident->charType >= 0x10 && resident->charType < 0x14;
        });
    }

    // The noodle stand: over every map object with its claim meter at level 0, the first idle worker of character
    // type 0x17 at a building of type 0x28, 0x18 at type 0x29 and 0xC at type 0x2C, until all three are found. With
    // any found, the event's ten actors lose their person and building, the workers become actors 4 to 6 and their
    // buildings the buildings of actors 7 to 9 (the values for the ones not found come from 0x20B8D0 and 0x1BD9D8,
    // as in the original).
    int EvNoodles_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        static constexpr uint16_t kTypes[3] = {0x28, 0x29, 0x2C};
        static constexpr uint32_t kWorkers[3] = {0x17, 0x18, 0xC};
        uint32_t workers[3];
        uint16_t buildings[3];
        for (uint32_t k = 0; k < 3; ++k)
        {
            workers[k] = port::load<uint32_t>(0x0020B8D0u + 4u * k);
            buildings[k] = port::load<uint16_t>(0x001BD9D8u + 2u * k);
        }
        int found = 0;
        const uint32_t first = Map_GetRecord(city->map, 1).address;
        const uint32_t end = first + 0x3F80u * 0x1Cu;
        for (uint32_t record = first; !(workers[0] && workers[1] && workers[2]) && record < end; record += 0x1Cu)
        {
            const GuestPtr<MapObject> obj{record};
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
        for (uint32_t actor = data.address + 0xCu; actor < data.address + 0x5Cu; actor += 8u)
        {
            port::store<uint16_t>(actor, kNoPerson);
            port::store<uint16_t>(actor + 2u, kNoObject);
        }
        for (uint32_t k = 0; k < 3; ++k)
        {
            if (workers[k] == 0)
                continue;
            const GuestPtr<MapPerson> worker{workers[k]};
            const GuestPtr<ActorRef> actor{data.address + 0x2Cu + 8u * k};
            actor->person = worker->index;
            actor->object = worker->work;
            actor->family = worker->family;
            actor->member = worker->member;
        }
        for (uint32_t k = 0; k < 3; ++k)
            port::store<uint16_t>(data.address + 0x46u + 8u * k, buildings[k]);
        return 1;
    }

    PORT_FN(EvBoyfriend_Check);
    PORT_FN(EvGirlfriend_Check);
    PORT_FN(EvAffair_Check);
    PORT_FN(EvNoodles_Check);

    // ---- event buckets (batch 39)

    namespace
    {
        // Event buckets: nine bucket starts (+0; bucket k runs up to the start of k + 1) over two-byte entries from
        // +0x24 (the event type, then its wait counter as a signed byte). Each bucket is kept in the order of the
        // types' priorities (EVENT.BIN record +0x22, signed), lowest first.
        uint32_t bucketEntry(uint32_t list, int32_t index) { return list + 0x24u + 2u * static_cast<uint32_t>(index); }
        uint32_t bucketStart(uint32_t list, int bucket) { return list + 4u * static_cast<uint32_t>(bucket); }
        uint32_t eventRecord(uint32_t types, uint8_t type) { return types + 0x4Cu * type; }
        int8_t priority(uint32_t types, uint8_t type) { return port::load<int8_t>(eventRecord(types, type) + 0x22u); }

        // Moves the entries of bucket `from` that pass the test (on the type's record and the wait counter) to
        // bucket `to`, each after the entries of lower priority there (the search goes on from the last place
        // taken), and shifts the buckets in between.
        template <typename Test>
        void moveEntries(uint32_t list, int from, int to, uint32_t types, Test test)
        {
            const int32_t first = port::load<int32_t>(bucketStart(list, from));
            const int32_t count = port::load<int32_t>(bucketStart(list, from + 1)) - first;
            uint32_t source = bucketEntry(list, first);
            uint32_t place = bucketEntry(list, port::load<int32_t>(bucketStart(list, to)));
            uint32_t end = bucketEntry(list, port::load<int32_t>(bucketStart(list, to + 1)));
            const auto findPlace = [&](int8_t prio) {
                while (place < end && priority(types, port::load<uint8_t>(place)) < prio)
                    place += 2u;
            };
            const auto entries = [](uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) / 2; };
            int32_t moved = 0;
            if (from < to)
            {
                for (int32_t i = 0; i < count; ++i)
                {
                    const uint8_t type = port::load<uint8_t>(source);
                    const int8_t wait = port::load<int8_t>(source + 1u);
                    if (!test(eventRecord(types, type), wait))
                    {
                        source += 2u;
                        continue;
                    }
                    findPlace(priority(types, type));
                    sdk::memmove(source, source + 2u, 2 * (entries(place, source) - 1));
                    port::store<uint8_t>(place - 2u, type);
                    port::store<int8_t>(place - 1u, wait);
                    ++moved;
                }
                for (int bucket = from + 1; bucket <= to; ++bucket)
                    port::store<int32_t>(bucketStart(list, bucket), port::load<int32_t>(bucketStart(list, bucket)) - moved);
                return;
            }
            for (int32_t i = 0; i < count; ++i, source += 2u)
            {
                const uint8_t type = port::load<uint8_t>(source);
                const int8_t wait = port::load<int8_t>(source + 1u);
                if (!test(eventRecord(types, type), wait))
                    continue;
                findPlace(priority(types, type));
                sdk::memmove(place + 2u, place, 2 * entries(source, place));
                port::store<uint8_t>(place, type);
                port::store<int8_t>(place + 1u, wait);
                place += 2u;
                end += 2u;
                ++moved;
            }
            for (int bucket = to + 1; bucket <= from; ++bucket)
                port::store<int32_t>(bucketStart(list, bucket), port::load<int32_t>(bucketStart(list, bucket)) + moved);
        }
    }

    // Every entry (the event manager's constructor and destructor).
    void EventList_MoveAll(int list, int from, int to)
    {
        moveEntries(static_cast<uint32_t>(list), from, to, globals::eventTypes.get(), [](uint32_t, int8_t) { return true; });
    }

    // The rest take the type records at ctx[0] and test values from ctx[1] on. Entries whose wait ran out.
    void EventList_MoveDue(int list, int from, int to, GuestPtr<int32_t> ctx)
    {
        moveEntries(static_cast<uint32_t>(list), from, to, static_cast<uint32_t>(ctx[0]), [](uint32_t, int8_t wait) { return wait <= 0; });
    }

    // Entries whose wait ran out and whose type byte +0x23 is ctx[1].
    void EventList_MoveDueTo(int list, int from, int to, GuestPtr<int32_t> ctx)
    {
        const uint32_t kind = static_cast<uint32_t>(ctx[1]);
        moveEntries(static_cast<uint32_t>(list), from, to, static_cast<uint32_t>(ctx[0]),
                    [kind](uint32_t record, int8_t wait) { return wait <= 0 && port::load<uint8_t>(record + 0x23u) == kind; });
    }

    // Entries whose rank mask (+0x27) shares a bit with ctx[1].
    void EventList_MoveOffRank(int list, int from, int to, GuestPtr<int32_t> ctx)
    {
        const uint32_t rank = static_cast<uint32_t>(ctx[1]);
        moveEntries(static_cast<uint32_t>(list), from, to, static_cast<uint32_t>(ctx[0]),
                    [rank](uint32_t record, int8_t) { return (rank & port::load<uint8_t>(record + 0x27u)) != 0; });
    }

    // The same rank test, and type byte +0x23 equal to ctx[2].
    void EventList_MoveOnRank(int list, int from, int to, GuestPtr<int32_t> ctx)
    {
        const uint32_t rank = static_cast<uint32_t>(ctx[1]), kind = static_cast<uint32_t>(ctx[2]);
        moveEntries(static_cast<uint32_t>(list), from, to, static_cast<uint32_t>(ctx[0]), [rank, kind](uint32_t record, int8_t) {
            return (rank & port::load<uint8_t>(record + 0x27u)) != 0 && port::load<uint8_t>(record + 0x23u) == kind;
        });
    }

    // Entries whose rank mask shares no bit with ctx[1].
    void EventList_MoveOffRankB(int list, int from, int to, GuestPtr<int32_t> ctx)
    {
        const uint32_t rank = static_cast<uint32_t>(ctx[1]);
        moveEntries(static_cast<uint32_t>(list), from, to, static_cast<uint32_t>(ctx[0]),
                    [rank](uint32_t record, int8_t) { return (rank & port::load<uint8_t>(record + 0x27u)) == 0; });
    }

    // Entries whose mask +0x26 shares a bit with the byte at ctx + 4.
    void EventList_MoveByMask26(int list, int from, int to, GuestPtr<int32_t> ctx)
    {
        const uint32_t mask = port::load<uint8_t>(ctx.address + 4u);
        moveEntries(static_cast<uint32_t>(list), from, to, static_cast<uint32_t>(ctx[0]),
                    [mask](uint32_t record, int8_t) { return (mask & port::load<uint8_t>(record + 0x26u)) != 0; });
    }

    PORT_FN(EventList_MoveAll);
    PORT_FN(EventList_MoveDue);
    PORT_FN(EventList_MoveDueTo);
    PORT_FN(EventList_MoveOffRank);
    PORT_FN(EventList_MoveOnRank);
    PORT_FN(EventList_MoveOffRankB);
    PORT_FN(EventList_MoveByMask26);

    // ---- alien and delivery man start checks (batch 39)

    namespace
    {
        void setActorFromPerson(GuestPtr<ActorRef> actor, GuestPtr<MapPerson> person)
        {
            actor->person = person->index;
            actor->object = person->work;
            actor->family = person->family;
            actor->member = person->member;
        }

        bool overlap(GuestPtr<int32_t> a, GuestPtr<int32_t> b) { return a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3]; }
    }

    // The alien needs four buildings anywhere on the map: of type 0xE7 (tile list 0xB), one of the types at 0x1B3750
    // (lists 0x12, then 0x13), 0x10A (list 0x18) and 0x104 (list 0x16); and a resident of character type 3 to 6
    // whose workplace is none of them, has its claim meter at level 0 and is not taken (flags 0x240). That
    // resident becomes actor 0 and the families' first members (and buildings) actors 1 to 4, the buildings
    // marked (0x200); actor 5 is the family at +0x15C of the record at 0x1BDA8C, with no person or building.
    int EvAlien_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        port::GuestFrame<0x30> frame;
        const GuestPtr<int32_t> whole{frame.ptr().address};
        whole[0] = 0;
        whole[1] = 0;
        whole[2] = 128;
        whole[3] = 128;
        uint32_t found[4];
        for (uint32_t k = 0; k < 4; ++k)
            found[k] = port::load<uint32_t>(0x0020B880u + 4u * k); // the original's initial values
        if (!(found[3] = EvAlien_FindBuilding(city, 0x16, 0x104, whole).address))
            return 0;
        if (!(found[2] = EvAlien_FindBuilding(city, 0x18, 0x10A, whole).address))
            return 0;
        if (!(found[0] = EvAlien_FindBuilding(city, 0xB, 0xE7, whole).address))
            return 0;
        for (uint32_t type = 0x001B3750u; found[1] == 0 && port::load<uint16_t>(type) != 0; type += 2u)
        {
            found[1] = EvAlien_FindBuilding(city, 0x12, port::load<uint16_t>(type), whole).address;
            if (found[1] == 0)
                found[1] = EvAlien_FindBuilding(city, 0x13, port::load<uint16_t>(type), whole).address;
        }
        if (found[1] == 0)
            return 0;
        uint16_t chosen[4] = {kNoPerson, kNoObject, 0, 0};
        const uint32_t end = city->map->people.address + 6000u * 0x30u;
        for (uint32_t p = city->map->people.address; p < end; p += 0x30u)
        {
            const GuestPtr<MapPerson> person{p};
            if (person->charType < 3 || person->charType >= 7)
                continue;
            const GuestPtr<MapObject> work = Map_GetRecord(city->map, person->work);
            if (ClaimMeter_Level(work->claimMeter) != 0 || (work->flags & 0x240u) != 0)
                continue;
            if (work.address == found[0] || work.address == found[1] || work.address == found[2] || work.address == found[3])
                continue;
            chosen[0] = person->index;
            chosen[1] = person->work;
            chosen[2] = person->family;
            chosen[3] = person->member;
            work->flags |= 0x200u;
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
        last->family = port::load<uint16_t>(port::load<uint32_t>(0x001BDA8Cu) + 0x15Cu);
        last->member = 0;
        return 1;
    }

    // The delivery man comes in rain or snow (weather 2 or 3) for a person with a target building of a type in the
    // delivery catalogue (0x1BA090, four entries of 0x184 bytes, the type first). From a random entry of the tile
    // lists (five at 0x1B3780 in holiday months, else two at 0x1BD9D0) and a random item on, a building outside the
    // target's footprint widened by 10 but inside it widened by 30, whose family's first member stands on it. That
    // building and person become actor 1; +0x1C takes the catalogue entry and +0x20 the month.
    int EvDeliveryMan_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        if (person->target == kNoObject)
            return 0;
        if (city->weatherToday != 2 && city->weatherToday != 3)
            return 0;
        port::GuestFrame<0x40> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<int32_t> near{sp}, wide{sp + 0x10u}, rect{sp + 0x20u};
        const GuestPtr<uint32_t> count{sp + 0x30u};
        const bool holiday = (city->flags & 2u) != 0;
        const uint32_t table = holiday ? 0x001B3780u : 0x001BD9D0u;
        const int lists = holiday ? 5 : 2;
        const GuestPtr<MapObject> target = Map_GetRecord(city->map, person->target);
        const uint16_t targetType = target->type;
        Building_GetFootprint(target, 10, GuestPtr<uint32_t>{near.address});
        wide[0] = near[0] - 20;
        wide[1] = near[1] - 20;
        wide[2] = near[2] + 20;
        wide[3] = near[3] + 20;
        uint32_t list = table + 4u * static_cast<uint32_t>(static_cast<int32_t>(Random_Next()) % lists);
        for (int tries = 0; tries < lists; ++tries, list += 4u)
        {
            if (list >= table + 4u * static_cast<uint32_t>(lists))
                list = table;
            const uint32_t items = Map_GetTileList(city->map, port::load<int32_t>(list), count).address;
            if (*count == 0)
                continue;
            uint32_t item = items + 2u * static_cast<uint32_t>(static_cast<int32_t>(Random_Next()) % static_cast<int32_t>(*count));
            for (int i = 0; i < static_cast<int32_t>(*count); ++i, item += 2u)
            {
                if (item >= items + 2u * *count)
                    item = items;
                const GuestPtr<MapObject> building = Map_GetRecord(city->map, port::load<uint16_t>(item));
                Building_GetFootprint(building, 0, GuestPtr<uint32_t>{rect.address});
                if (overlap(near, rect))
                    continue;
                Building_GetFootprint(building, 0, GuestPtr<uint32_t>{rect.address});
                if (!overlap(wide, rect))
                    continue;
                const GuestPtr<MapPerson> member = Map_FindPersonEntry(city->map, static_cast<int16_t>(building->family), 0);
                if (Map_FindPersonObjectOnTile(city->map, member->index, tileOf(member)).address != building.address)
                    continue;
                int entry = -1;
                for (uint32_t e = globals::kDeliveryCatalogue; e < globals::kDeliveryCatalogue + 4u * 0x184u; e += 0x184u)
                    if (port::load<uint32_t>(e) == targetType)
                    {
                        entry = static_cast<int>((e - globals::kDeliveryCatalogue) / 0x184u);
                        break;
                    }
                if (entry < 0)
                    return 0;
                port::store<int32_t>(data.address + 0x1Cu, entry);
                port::store<int32_t>(data.address + 0x20u, city->monthCount % 12);
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
            const uint32_t end = city->map->people.address + 6000u * 0x30u;
            for (uint32_t p = city->map->people.address; p < end; p += 0x30u)
            {
                const GuestPtr<MapPerson> person{p};
                if (person->charType == 0)
                    continue;
                const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
                if ((cls->flags & 1u) != 0 || (person->flags & 0x400u) != 0)
                    continue;
                person->flags |= 0x400u;
                return true;
            }
            return false;
        }

        // The actor numbers at 0x1B37A0 (and 0x1B37B0), zero-ended: 4, 5 and 1.
        template <typename Each>
        bool eachCultActor(GuestPtr<EventData> data, uint32_t table, Each each)
        {
            for (uint32_t k = table; port::load<int32_t>(k) != 0; k += 4u)
                if (!each(actorAt(data, port::load<int32_t>(k))))
                    return false;
            return true;
        }
    }

    // Phases 10 to 16: 10 waits until everyone has been reached (+0x4C), 11 and 13 tell actor 3 when its time comes
    // and 12 and 14 count those frames back, 15 finds the homes of actors 4, 5 and 1 (marked 0x200; back to 15 while
    // one is missing) and 16 their persons (back to 16).
    void EvCrazyCult_Phase2(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        switch (data->var)
        {
        case 10:
            if (port::load<int32_t>(data.address + 0x4Cu) != 0)
                data->var = 11;
            break;
        case 11:
        case 13:
            cultWait(data, city, data.address + 0x24u);
            break;
        case 12:
        case 14:
            cultCountDown(data);
            break;
        case 15:
            data->var = 0x10;
            eachCultActor(data, 0x001B37A0u, [&](GuestPtr<ActorRef> actor) {
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
            eachCultActor(data, 0x001B37B0u, [&](GuestPtr<ActorRef> actor) {
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

    // While the cult runs, every 30 frames (+0x48) one more resident takes flag 0x400; in phases 10 to 16 +0x4C then
    // tells whether everyone has it. Phases 2 and 3 find actor 2's home and person (then waiting for the next month
    // at 7:00); 4 to 9, 10 to 16 and 0x12 to 0x15 go to the phase handlers; 0x1B sets the next wait, 0x1C tells
    // actor 0 when its time comes and 0x1D counts 60 frames back.
    void EvCrazyCult_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const int var = data->var;
        const auto due = [&] {
            const int32_t frames = port::load<int32_t>(data.address + 0x48u) - 1;
            port::store<int32_t>(data.address + 0x48u, frames);
            return frames <= 0;
        };
        if (var < 10)
        {
            if (due())
            {
                flagNextResident(city);
                port::store<int32_t>(data.address + 0x48u, 30);
            }
            if (var >= 4)
            {
                EvCrazyCult_Phase1(data, city);
                return;
            }
            const GuestPtr<ActorRef> actor = actorAt(data, 2);
            if (data->var == 3)
            {
                if (const GuestPtr<MapPerson> person = Map_FindPersonEntry(city->map, static_cast<int16_t>(actor->family), actor->member))
                {
                    actor->person = person->index;
                    data->var = 4;
                    port::store<int32_t>(data.address + 0x3Cu, city->monthCount + 1);
                    port::store<int32_t>(data.address + 0x40u, 25200);
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
                port::store<int32_t>(data.address + 0x4Cu, flagNextResident(city) ? 0 : 1);
                port::store<int32_t>(data.address + 0x48u, 30);
            }
            EvCrazyCult_Phase2(data, city);
            return;
        }
        if (var < 0x16)
        {
            EvCrazyCult_Phase3(data, city);
            return;
        }
        switch (var)
        {
        case 0x1B:
            port::store<int32_t>(data.address + 0x3Cu, city->monthCount + 1);
            port::store<int32_t>(data.address + 0x40u, 25200);
            break;
        case 0x1C:
            if (city->monthCount < port::load<int32_t>(data.address + 0x3Cu) || city->clock < port::load<int32_t>(data.address + 0x40u))
                break;
            if (EvActor_Notify(city, GuestPtr<ActorRef>::of(data->actor)) == 0)
                break;
            ++data->var;
            port::store<int32_t>(data.address + 0x44u, 60);
            break;
        case 0x1D:
        {
            const int32_t frames = port::load<int32_t>(data.address + 0x44u) - 1;
            port::store<int32_t>(data.address + 0x44u, frames);
            if (frames <= 0)
                --data->var;
            break;
        }
        default:
            break;
        }
    }

    PORT_FN(EvCrazyCult_Phase2);
    PORT_FN(EvCrazyCult_Update);

    // ---- farewell party, shopping (batch 39)

    // The farewell party: the phone calls are cleared; actor 0 is the family at +0x162 of the record at 0x1BDA8C.
    // Residents (character types 1 to 0x1D) with friendship band 3 and up, working at a building other than types
    // 0xB9 and 0x105 with its claim meter at level 0, are the guests (+0x28 counts them); of those in schedule state
    // 6 and up the best friend is actor 1 (the first on a tie) and their workplace is marked. The party starts at
    // 13:00 plus an hour per 50 guests (+0x2C, seconds, 19:00 at the latest); +0x1C becomes 0xFFFF.
    GuestPtr<MapObject> EvFarewellParty_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        PhoneCall_Next(city->phoneCalls, city);
        data->actor.person = kNoPerson;
        data->actor.object = kNoObject;
        data->actor.family = port::load<uint16_t>(port::load<uint32_t>(0x001BDA8Cu) + 0x162u);
        data->actor.member = 0;
        GuestPtr<MapPerson> best{0};
        const uint32_t end = city->map->people.address + 6000u * 0x30u;
        for (uint32_t p = city->map->people.address; p < end; p += 0x30u)
        {
            const GuestPtr<MapPerson> person{p};
            if (person->charType == 0 || person->charType >= 0x1E)
                continue;
            if (Family_GetFriendshipBand(person->family, person->member) < 3)
                continue;
            const GuestPtr<MapObject> work = Map_GetRecord(city->map, person->work);
            if (work->type == 0xB9 || work->type == 0x105 || ClaimMeter_Level(work->claimMeter) != 0)
                continue;
            port::store<int32_t>(data.address + 0x28u, port::load<int32_t>(data.address + 0x28u) + 1);
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
        port::store<int32_t>(data.address + 0x2Cu, (port::load<int32_t>(data.address + 0x28u) / 50 + 13) * 3600);
        if (68400 < port::load<int32_t>(data.address + 0x2Cu))
            port::store<int32_t>(data.address + 0x2Cu, 68400);
        port::store<uint16_t>(data.address + 0x1Cu, 0xFFFF);
        return record;
    }

    // Shopping: actor 0 keeps heading for its building; by phase (jump table 0x1BA8A0) 1 waits until four hours
    // before the event type's time (+0x2E, minutes), 2 goes to 5 within two hours of it or tells the actor, 5 goes
    // to 8 once it has passed or tells the actor, 8 tells the actor, 3, 6 and 9 count 60 frames back, and 10 and 11
    // end the event after 240 frames.
    void EvShopping_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<ActorRef> actor = GuestPtr<ActorRef>::of(data->actor);
        EvActor_MoveTo(city, actor, actor, 0);
        const auto time = [&] { return static_cast<int>(port::load<uint16_t>(globals::eventTypesCopy.get() + 0x4Cu * data->kind + 0x2Eu)); };
        const auto tell = [&] {
            if (EvActor_Notify(city, actor) == 0)
                return;
            ++data->var;
            port::store<int32_t>(data.address + 0x54u, 60);
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
            const int32_t frames = port::load<int32_t>(data.address + 0x54u) - 1;
            port::store<int32_t>(data.address + 0x54u, frames);
            if (frames <= 0)
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
            const int32_t frames = port::load<int32_t>(data.address + 0x58u) + 1;
            port::store<int32_t>(data.address + 0x58u, frames);
            if (frames >= 241)
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
    int EvMsg_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef>, int mode)
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
            const uint32_t end = city->map->people.address + 6000u * 0x30u;
            for (uint32_t p = city->map->people.address; p < end; p += 0x30u)
            {
                const GuestPtr<MapPerson> person{p};
                if (person->charType == 0 || person->charType >= 0x1E || person->schedState < 6)
                    continue;
                if (Family_GetFriendshipBand(person->family, person->member) < 3)
                    continue;
                if (ClaimMeter_Level(Map_GetRecord(city->map, person->work)->claimMeter) == 0)
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

    // The advertisement keeps the families told (+0x24, fifteen (family, member) pairs, a zero family ends them) and
    // their count (+0x20). Argument 0: whether the actor was told. 1: for an actor told, where it stands (4 on the
    // event building, 3 inside it, else 5); else 0 once the list runs out, or at the first free place 2 (1 when the
    // event building is full, 15 occupants) if the actor's building claims what the event building offers; 6 with
    // the list full (the phase becomes 4). 2: adds the actor (once), moves it into the event building and counts it
    // (phase 4 at fifteen). 3: 2 from ten told, 1 from five. 4: the actor's family gains three friendship points
    // per family told.
    int EvAdvertise_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        const uint32_t first = data.address + 0x24u, end = first + 0x3Cu;
        const auto told = [&](uint32_t entry) {
            return port::load<uint16_t>(entry) == actor->family && port::load<uint16_t>(entry + 2u) == actor->member;
        };
        const auto count = [&] { return port::load<int32_t>(data.address + 0x20u); };
        switch (mode)
        {
        case 0:
            for (uint32_t entry = first; port::load<uint16_t>(entry) != 0 && entry < end; entry += 4u)
                if (told(entry))
                    return 1;
            return 0;
        case 1:
            if (count() >= 15)
            {
                data->var = 4;
                return 6;
            }
            for (uint32_t entry = first; entry < end; entry += 4u)
            {
                if (port::load<uint16_t>(entry) == 0)
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
            for (uint32_t entry = first; entry < end; entry += 4u)
            {
                if (port::load<uint16_t>(entry) == 0)
                {
                    port::store<uint16_t>(entry, actor->family);
                    port::store<uint16_t>(entry + 2u, actor->member);
                    break;
                }
                if (told(entry))
                    return 0;
            }
            EvActor_MoveTo(city, actor, GuestPtr<ActorRef>::of(data->actor), 1);
            port::store<int32_t>(data.address + 0x20u, count() + 1);
            if (count() >= 15)
                data->var = 4;
            return 0;
        case 3:
            if (count() >= 10)
                return 2;
            return count() >= 5 ? 1 : 0;
        case 4:
            Family_AddFriendship(actor->family, actor->member, GuestPtr<int32_t>{city.address + 0x8Cu}, 3 * count());
            return 0;
        default:
            return 0;
        }
    }

    PORT_FN(EvMsg_Slot14);
    PORT_FN(EvAdvertise_Slot14);

    // ---- adoption (batch 39)

    // The adoption's phases (jump table 0x1BA780). 1, 4, 7, 10, 13 and 15 wait until actor 0 can be told (then 60
    // frames, plus the minutes until 6:00 or 360 more from 23:00); 2, 5, 8, 11, 14, 16 and 19 count those down and
    // go back to telling until the sixth time (+0x18), then on; 22 likewise, ending the event after the sixth. Every
    // tenth frame (+0x14) 3 waits for an object of type 3 or 4, 6 for any residents, 9 for any friend, and 12 for 101
    // residents and twice the applicants (then phase 15) or, with fewer than 32 applicants, for a claim of the list at
    // +0x4C seen ten times with one of them pressing (claim meter level 2 and up; +0x20 takes its place). 17 and 20
    // note the month (+0x1C); 18 and 21 tell actor 0 from noon of a later month.
    void EvAdoption_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<ActorRef> actor = GuestPtr<ActorRef>::of(data->actor);
        const auto told = [&] {
            if (EvActor_Notify(city, actor) == 0)
                return false;
            ++data->var;
            data->unk_14 = 60;
            return true;
        };
        const auto advance = [&] {
            data->unk_18 = 0;
            ++data->var;
        };
        const auto tenth = [&] { return ++data->unk_14 >= 10; };
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
                    data->unk_14 += 360 - clock / 60;
                else if (clock >= 82801)
                    data->unk_14 += 360;
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
            if (--data->unk_14 > 0)
                break;
            if (++data->unk_18 < 6)
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
            const uint32_t first = Map_GetRecord(city->map, 0).address;
            for (uint32_t record = first; record < first + 0x3F80u * 0x1Cu; record += 0x1Cu)
            {
                const uint16_t type = port::load<uint16_t>(record);
                if (type == 3 || type == 4)
                {
                    advance();
                    break;
                }
            }
            data->unk_14 = 0;
            break;
        }
        case 6:
            if (!tenth())
                break;
            if (city->map->population != 0)
                advance();
            data->unk_14 = 0;
            break;
        case 9:
        {
            if (!tenth())
                break;
            const uint32_t end = city->map->people.address + 6000u * 0x30u;
            for (uint32_t p = city->map->people.address; p < end; p += 0x30u)
            {
                const GuestPtr<MapPerson> person{p};
                if (person->charType == 0)
                    continue;
                const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
                if ((cls->flags & 1u) != 0 || Family_GetFriendship(person->family, person->member) == 0)
                    continue;
                advance();
                break;
            }
            data->unk_14 = 0;
            break;
        }
        case 12:
        {
            if (!tenth())
                break;
            const int applicants = city->applicants->count;
            if (city->map->population + 2 * applicants >= 101)
            {
                data->unk_18 = 0;
                data->var = 15;
                data->unk_14 = 0;
                break;
            }
            if (applicants >= 32)
            {
                data->unk_14 = 0;
                break;
            }
            port::GuestLocal<uint32_t> count;
            const uint32_t claims = data.address + 0x4Cu;
            for (int list = 0; list < 0x50; ++list)
            {
                const uint32_t items = Map_GetTileList(city->map, list, count).address;
                const uint32_t end = items + 2u * *count;
                for (uint32_t item = items; item < end; item += 2u)
                {
                    const GuestPtr<MapObject> building = Map_GetRecord(city->map, port::load<uint16_t>(item));
                    if (building->claimSlot == 0x18 || ClaimMeter_Level(building->claimMeter) == 0)
                        continue;
                    const uint32_t section = static_cast<uint32_t>(Claim_GetSectionB(typeOf(building)->claimB)) + 8u * building->claimSlot;
                    if (port::load<uint8_t>(section) != 0)
                        continue;
                    for (uint32_t claim = claims; port::load<uint8_t>(claim) != 0; claim += 4u)
                    {
                        if (port::load<uint8_t>(claim) != port::load<uint16_t>(section + 2u))
                            continue;
                        uint8_t seen = port::load<uint8_t>(claim + 1u);
                        if ((seen & 0x80u) == 0 && ClaimMeter_Level(building->claimMeter) >= 2)
                            seen |= 0x80u;
                        seen = static_cast<uint8_t>((seen & 0x80u) | ((seen + 1u) & 0x7Fu));
                        port::store<uint8_t>(claim + 1u, seen);
                        if ((seen & 0x7Fu) >= 10 && (seen & 0x80u) != 0)
                        {
                            port::store<int32_t>(data.address + 0x20u, static_cast<int32_t>(claim - claims) / 4);
                            advance();
                            return;
                        }
                    }
                }
            }
            for (uint32_t claim = claims; port::load<uint8_t>(claim) != 0; claim += 4u)
                port::store<uint8_t>(claim + 1u, 0);
            data->unk_14 = 0;
            break;
        }
        case 17:
        case 20:
            port::store<int32_t>(data.address + 0x1Cu, city->monthCount);
            advance();
            break;
        case 18:
        case 21:
            if (port::load<int32_t>(data.address + 0x1Cu) < city->monthCount && city->clock >= 43200)
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
        // Event handler classes (table 0x1AF0D0, one vtable per handler id): Check, Start (+4), Update, End, Slot10
        // (+0x10), Slot14 (+0x14). An event type's handler id is the byte at +0x24 of its row, 0 = none.
        constexpr uint32_t kEventHandlers = 0x001AF0D0u;

        // The talk event in manager slot `index` (talkA, talkB) and its handler slot at vtable `offset`; 0 when the
        // event type has no handler or the handler lacks the slot.
        uint32_t handlerSlot(GuestPtr<EventData> data, uint32_t offset)
        {
            const uint32_t id = port::load<uint8_t>(globals::eventTypes.get() + 0x4Cu * data->kind + 0x24u);
            if (id == 0)
                return 0;
            return port::load<uint32_t>(port::load<uint32_t>(kEventHandlers + 4u * id) + offset);
        }

        GuestPtr<EventData> talkEvent(GuestPtr<EventMgr> events, int index)
        {
            return GuestPtr<EventData>{port::load<uint32_t>(events.address + 0xA4u + 4u * static_cast<uint32_t>(index))};
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

    // The talk's event runs its Slot14 handler (0 when it has none); a is the actor asked about, mode the question.
    int64_t Event_CallSlot14(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int arg, int mode)
    {
        const GuestPtr<EventData> data = talkEvent(events, mode);
        const uint32_t slot = handlerSlot(data, 0x14u);
        if (slot == 0)
            return 0;
        return port::callGuest<int64_t>(slot, data, city, actor, arg);
    }

    // The same for the Slot10 handler of the event in slot `index`.
    int64_t Event_CallSlot10(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, int index, int arg)
    {
        const GuestPtr<EventData> data = talkEvent(events, index);
        const uint32_t slot = handlerSlot(data, 0x10u);
        if (slot == 0)
            return 0;
        return port::callGuest<int64_t>(slot, data, city, arg);
    }

    // The talk starts the event's own handler (the Start slot at +4): its tile flags are set, the event's var becomes 1
    // and the handler runs. Without a handler the result register stays as it was (after the tile flags call).
    int64_t Event_CallHandlerSlot(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, int index, int arg)
    {
        const uint32_t cell = events.address + 0xA4u + 4u * static_cast<uint32_t>(index);
        const uint32_t row = globals::eventTypes.get() + 0x4Cu * GuestPtr<EventData>{port::load<uint32_t>(cell)}->kind;
        const uint32_t tileFlags = port::load<uint8_t>(row + 0x25u);
        if (tileFlags != 0)
        {
            port::setGuestV0(static_cast<int32_t>(tileFlags));
            Event_SetTileFlags(static_cast<int>(tileFlags) - 1);
        }
        port::store<uint8_t>(port::load<uint32_t>(cell) + 0xAu, 1);
        const uint32_t id = port::load<uint8_t>(row + 0x24u);
        if (id != 0)
        {
            const uint32_t slot = port::load<uint32_t>(port::load<uint32_t>(kEventHandlers + 4u * id) + 4u);
            if (slot != 0)
                return port::callGuest<int64_t>(slot, GuestPtr<EventData>{port::load<uint32_t>(cell)}, city, arg);
        }
        return port::guestV0Wide();
    }

    // Type 7 (the useless middle-aged man) answers the talk: modes 0-3 store the mode in +0x1C, 4 reads it back, 5 takes
    // 100 from the money, 6 gives 50, 7 gives 200.
    uint32_t EvUselessMiddleAged_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene>, GuestPtr<ActorRef>, int mode)
    {
        if (static_cast<uint32_t>(mode) >= 8)
            return 0;
        switch (mode)
        {
        case 4:
            return port::load<uint32_t>(data.address + 0x1Cu);
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
            port::store<int32_t>(data.address + 0x1Cu, mode);
            return 0;
        }
    }

    // Slot10 shared by types 29 and 30 (payday): formats a number into the event text buffer and returns it: the
    // families that moved in / left, the payday score, the score with the families counted (two for each moved in, four
    // for each left). Other values give an empty text.
    GuestPtr<char> EvPayday_Slot10(GuestPtr<EventData>, GuestPtr<CityScene> city, uint32_t value)
    {
        constexpr uint32_t kEventText = 0x0020B890u;
        const GuestPtr<char> out{kEventText};
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
            port::store<uint8_t>(kEventText, 0);
            break;
        }
        return out;
    }

    // Type 3 (the delivery man) answers the talk: 0 the catalogue entry (+0x1C), 1 and 2 whether the player is within 1 of
    // actor 1 / actor 0, 3 and 4 add / take the price of the delivered goods (the catalogue entry's item for the month in
    // +0x20, a halfword at 0x1BA0B2 + 0x184 * entry + 0x20 * month) to / from the money, 5 reads +0x34.
    int EvDeliveryMan_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef>, int mode)
    {
        const auto price = [&] {
            const uint32_t entry = port::load<uint32_t>(data.address + 0x1Cu), month = port::load<uint32_t>(data.address + 0x20u);
            return static_cast<int32_t>(port::load<uint16_t>(0x001BA0B2u + 0x184u * entry + 0x20u * month));
        };
        switch (static_cast<uint32_t>(mode))
        {
        case 0:
            return port::load<int32_t>(data.address + 0x1Cu);
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
            return port::load<int32_t>(data.address + 0x34u);
        default:
            return 0;
        }
    }

    // Type 18 (the crazy cult) answers the talk: 0, 1, 3 and 4 add the families stored in the event data to the
    // applicants, 2 asks whether at least three more applicants fit, 5 counts the asked actor's family among the three
    // actors (list at 0x1B37C0, as for the event's phases): the first time each one is asked bit k of +0x50 is set (the
    // answer 0); asking again answers 1, and with all three bits set (7) the event moves to its next var (0x12) and sets +0x40.
    int EvCrazyCult_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        const auto apply = [&](uint32_t offset) { CityScene_AddApplicantFamily(city, port::load<uint16_t>(data.address + offset), 1); };
        switch (static_cast<uint32_t>(mode))
        {
        case 0:
            apply(0x20u);
            return 0;
        case 1:
            apply(0x28u);
            return 0;
        case 2:
            return 0x20 - city->applicants->count >= 3 ? 1 : 0;
        case 3:
            apply(0x30u);
            apply(0x38u);
            apply(0x18u);
            return 0;
        case 4:
            apply(0x10u);
            return 0;
        case 5:
            for (uint32_t k = 0;; ++k)
            {
                const int32_t number = port::load<int32_t>(0x001B37C0u + 4u * k);
                if (number == 0)
                    return 0;
                if (actor->family != port::load<uint16_t>(data.address + 0x10u + 8u * static_cast<uint32_t>(number)))
                    continue;
                const int32_t bit = 1 << k;
                const int32_t bits = port::load<int32_t>(data.address + 0x50u);
                if ((bits & bit) != 0)
                    return 1;
                port::store<int32_t>(data.address + 0x50u, bits | bit);
                if (port::load<int32_t>(data.address + 0x50u) >= 7)
                {
                    port::store<uint8_t>(data.address + 0xAu, 0x12);
                    port::store<int32_t>(data.address + 0x40u, 0x6270);
                }
                return 0;
            }
        default:
            return 0;
        }
    }

    // Type 12 (adoption) answers the talk. The list at +0x4C has up to six entries of four bytes (a byte flag at +0, a
    // halfword at +2 that picks a family of the family type table at 0x1BDA8C); the family tried is that table entry, or
    // the next one the city lets apply.
    //   0: 1 when no orphanage (type 4) exists, 0 when there is one but no building of a type with flag 8 (type flags
    //      +0x136), else 2
    //   1: 1 when the asked actor's family is in the zero-ended family list at +0x24
    //   2: 1 while +0x60 is 0
    //   3: the family of list entry +0x20 is added to the applicants, the entry removed (list shifted down)
    //   4: 1 while the halfword +0x4E is 0
    //   5: 0 when one of the list's entries from the second on has no flag byte, checked once more than there is room
    //      for applicants, else 1
    //   6: the families of all list entries are added to the applicants
    int EvAdoption_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        // The first family at or after the entry's table family that the city lets apply.
        const auto familyFor = [&](uint32_t entry) {
            uint32_t family = port::load<uint16_t>(globals::familyTypeTable.get() + 2u * port::load<uint16_t>(entry + 2u) - 2u);
            while (Family_CanApply(family & 0xFFFFu, city) == 0)
                family = (family + 1u) & 0xFFFFu;
            return family & 0xFFFFu;
        };
        switch (static_cast<uint32_t>(mode))
        {
        case 0:
        {
            bool orphanage = false, special = false;
            const uint32_t first = Map_GetRecord(city->map, 0).address;
            for (uint32_t record = first; record < first + 0x6F200u; record += 0x1Cu)
            {
                const uint32_t type = port::load<uint16_t>(record);
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
            for (uint32_t entry = data.address + 0x24u; port::load<int32_t>(entry) != 0; entry += 4u)
                if (port::load<int32_t>(entry) == actor->family)
                    return 1;
            return 0;
        case 2:
            return port::load<uint8_t>(data.address + 0x60u) == 0 ? 1 : 0;
        case 3:
        {
            const uint32_t base = data.address + 0x4Cu;
            const int32_t index = port::load<int32_t>(data.address + 0x20u);
            const uint32_t entry = base + 4u * static_cast<uint32_t>(index);
            CityScene_AddApplicantFamily(city, familyFor(entry), 1);
            sdk::memmove(entry, entry + 4u, (6 - index) * 4);
            return 0;
        }
        case 4:
            return port::load<uint16_t>(data.address + 0x4Eu) == 0 ? 1 : 0;
        case 5:
        {
            int32_t room = 0x20 - city->applicants->count;
            for (uint32_t entry = data.address + 0x50u; room >= 0; entry += 4u, --room)
                if (port::load<uint8_t>(entry) == 0)
                    return 0;
            return 1;
        }
        case 6:
            for (uint32_t entry = data.address + 0x4Cu; port::load<uint8_t>(entry) != 0; entry += 4u)
                CityScene_AddApplicantFamily(city, familyFor(entry), 1);
            return 0;
        default:
            return 0;
        }
    }

    // Type 1x (shopping) answers the talk. The shop's record (0x28 bytes in the table at 0x1BDAB4, index +0x34) lists four
    // goods (halfwords at +0x20). Modes: 0 the count at +0x38 less one; 1 how many of the goods the bag lacks; 2 to 5 whether
    // the bag lacks good 0 to 3; 6 adds +0x4C to the money, 7 takes it, 8 takes +0x4C and +0x50; 9 moves the goods found in
    // the bag out of it into the money counter at +0x50 (their prices).
    int EvShopping_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef>, int mode)
    {
        const uint32_t goods = globals::shoppingTexts.get() + 0x28u * port::load<uint32_t>(data.address + 0x34u) + 0x20u;
        const auto bag = [&] { return GuestPtr<Bag>{port::load<uint32_t>(city.address + 0x60u)}; };
        const auto at = [&](uint32_t offset) { return port::load<int32_t>(data.address + offset); };
        switch (static_cast<uint32_t>(mode))
        {
        case 0:
            return at(0x38u) - 1;
        case 1:
        {
            int missing = 0;
            for (uint32_t entry = goods; port::load<uint16_t>(entry) != 0 && entry < goods + 8u; entry += 2u)
                if (Bag_FindItem(bag(), port::load<uint16_t>(entry)) == 0)
                    ++missing;
            return missing;
        }
        case 2:
        case 3:
        case 4:
        case 5:
            return Bag_FindItem(bag(), port::load<uint16_t>(goods + 2u * static_cast<uint32_t>(mode - 2))) != 0 ? 0 : 1;
        case 6:
            changeMoney(at(0x4Cu));
            return 0;
        case 7:
            changeMoney(static_cast<int32_t>(0u - static_cast<uint32_t>(at(0x4Cu))));
            return 0;
        case 8:
            changeMoney(static_cast<int32_t>(0u - (static_cast<uint32_t>(at(0x4Cu)) + static_cast<uint32_t>(at(0x50u)))));
            return 0;
        case 9:
            for (uint32_t entry = goods; entry < goods + 8u; entry += 2u)
            {
                const int found = Bag_FindItem(bag(), port::load<uint16_t>(entry));
                if (found == 0)
                    continue;
                const uint32_t slot = static_cast<uint32_t>(found);
                const uint32_t first = port::load<uint16_t>(port::load<uint32_t>(0x001BDA9Cu) + 4u * port::load<uint16_t>(slot));
                const uint32_t record = port::load<uint32_t>(0x001BDAA0u) + 0x88u * (port::load<uint16_t>(slot + 2u) + first);
                port::store<int32_t>(data.address + 0x50u, static_cast<int32_t>(static_cast<uint32_t>(at(0x50u)) + static_cast<uint32_t>(Goods_GetPrice(static_cast<int>(record)))));
                Bag_RemoveSlot(bag(), GuestPtr<uint16_t>{slot});
            }
            return 0;
        default:
            return 0;
        }
    }

    // The farewell party's end: the city's party object (+0x80) is destroyed (vtable +8, flag 1) and the party
    // building loses its event mark.
    void EvFarewellParty_End(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const uint32_t party = static_cast<uint32_t>(city->unk_80);
        if (party != 0)
        {
            port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(party) + 8u), party, 1);
            city->unk_80 = 0;
        }
        Map_GetRecord(city->map, port::load<uint16_t>(data.address + 0x16u))->flags &= 0xFDFFu;
    }

    PORT_FN(Event_CallSlot14);
    PORT_FN(Event_CallSlot10);
    PORT_FN(Event_CallHandlerSlot);
    PORT_FN(EvUselessMiddleAged_Slot14);
    PORT_FN(EvPayday_Slot10);
    PORT_FN(EvDeliveryMan_Slot14);
    PORT_FN(EvCrazyCult_Slot14);
    PORT_FN(EvAdoption_Slot14);
    PORT_FN(EvShopping_Slot14);
    PORT_FN(EvFarewellParty_End);

    // ---- starting, ending and saving events (batch 40)

    namespace
    {
        // A queued event's entry (type, wait counter) becomes the running event `talk`: its type, the type's first
        // var byte (+0x29, stored at +9), var 0, the Update slot of its handler at +4 and, for a person, the first actor
        // (person index, workplace, family, member).
        void fillEvent(GuestPtr<EventData> talk, uint32_t entry, GuestPtr<MapPerson> person)
        {
            const uint8_t type = port::load<uint8_t>(entry);
            const uint32_t row = globals::eventTypes.get() + 0x4Cu * type;
            talk->kind = type;
            port::store<int8_t>(talk.address + 9u, port::load<int8_t>(row + 0x29u));
            port::store<uint8_t>(talk.address + 0xAu, 0);
            const uint32_t id = port::load<uint8_t>(row + 0x24u);
            port::store<uint32_t>(talk.address + 4u, id == 0 ? 0u : port::load<uint32_t>(port::load<uint32_t>(kEventHandlers + 4u * id) + 8u));
            if (person)
            {
                port::store<uint16_t>(talk.address + 0xCu, person->index);
                port::store<uint16_t>(talk.address + 0xEu, person->work);
                port::store<uint16_t>(talk.address + 0x10u, person->family);
                port::store<uint16_t>(talk.address + 0x12u, person->member);
            }
        }

        // The running event's size (vtable +8) of its handler class, for clearing it.
        int eventSize(uint32_t talk) { return port::callGuest<int>(port::load<uint32_t>(port::load<uint32_t>(talk) + 8u), talk); }

        // The event in cell `cell` has its handler's slot at `offset` called with (event, city, third), if it has one.
        void callHandler(uint32_t row, uint32_t offset, GuestPtr<EventData> talk, GuestPtr<CityScene> city, uint32_t third)
        {
            const uint32_t id = port::load<uint8_t>(row + 0x24u);
            if (id == 0)
                return;
            const uint32_t slot = port::load<uint32_t>(port::load<uint32_t>(kEventHandlers + 4u * id) + offset);
            if (slot != 0)
                port::callGuest<void>(slot, talk, city, third);
        }

        // The event type's own ending (handler slot +0xC), its tile flags cleared and released.
        void endEventHandler(uint32_t row, GuestPtr<EventData> talk, GuestPtr<CityScene> city)
        {
            callHandler(row, 0xCu, talk, city, talk->kind); // a2 is the type byte the original had just loaded
            const uint32_t tileFlags = port::load<uint8_t>(row + 0x25u);
            if (tileFlags != 0)
            {
                Event_ClearTileFlags(static_cast<int>(tileFlags) - 1);
                Event_ReleaseTiles(city);
            }
        }

        // The finished event leaves the manager: its id goes back to the queue at `level` and its data is cleared.
        void returnEvent(uint32_t cell, uint32_t level)
        {
            EventQueue_InsertByPriority(static_cast<int>(globals::eventQueueObject.get()), GuestPtr<uint8_t>{port::load<uint32_t>(cell) + 8u}, static_cast<int>(level), 0);
            const uint32_t talk = port::load<uint32_t>(cell);
            sdk::memset(talk + 8u, 0, eventSize(talk));
        }
    }

    // The event queue, its object (0x224 bytes), the manager (0xA4 bytes) and the two running events' data go to the
    // memory card, or come back from it.
    void Event_LoadState(GuestPtr<EventMgr> events, GuestPtr<McManager> mc)
    {
        SaveFile_Read(mc, static_cast<int>(globals::kEventQueue), 0x200);
        SaveFile_Read(mc, static_cast<int>(globals::eventQueueObject.get()), 0x224);
        SaveFile_Read(mc, static_cast<int>(events.address), 0xA4);
        for (uint32_t i = 0; i < 2; ++i)
        {
            const uint32_t talk = port::load<uint32_t>(events.address + 0xA4u + 4u * i);
            const int size = eventSize(talk);
            SaveFile_Read(mc, static_cast<int>(port::load<uint32_t>(events.address + 0xA4u + 4u * i) + 8u), size);
        }
    }

    void Event_SaveState(GuestPtr<EventMgr> events, GuestPtr<McManager> mc)
    {
        SaveFile_Write(mc, static_cast<int>(globals::kEventQueue), 0x200);
        SaveFile_Write(mc, static_cast<int>(globals::eventQueueObject.get()), 0x224);
        SaveFile_Write(mc, static_cast<int>(events.address), 0xA4);
        for (uint32_t i = 0; i < 2; ++i)
        {
            const uint32_t talk = port::load<uint32_t>(events.address + 0xA4u + 4u * i);
            const int size = eventSize(talk);
            SaveFile_Write(mc, static_cast<int>(port::load<uint32_t>(events.address + 0xA4u + 4u * i) + 8u), size);
        }
    }

    // The talk event in slot `index` is cut short: its handler ends it, and it goes back to the waiting queue (level 3
    // for a type with level 4) as it was.
    void Event_Abort(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, int index)
    {
        const uint32_t cell = events.address + 0xA4u + 4u * static_cast<uint32_t>(index);
        const GuestPtr<EventData> talk{port::load<uint32_t>(cell)};
        const uint32_t row = globals::eventTypes.get() + 0x4Cu * talk->kind;
        endEventHandler(row, talk, city);
        uint32_t level = port::load<uint8_t>(row + 0x23u);
        if (level == 4)
            level = 3;
        returnEvent(cell, level);
    }

    // The talk event in slot `index` finishes. Its var byte (+0x28 of the type, signed) says where it queues next: above
    // 0 level 4, 0 its own level (3 for a type with level 4), -1 level 6, -2 level 7, any other value leaves the level
    // register as the caller had it.
    void Event_Finish(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, int index)
    {
        const uint32_t cell = events.address + 0xA4u + 4u * static_cast<uint32_t>(index);
        const GuestPtr<EventData> talk{port::load<uint32_t>(cell)};
        const uint32_t row = globals::eventTypes.get() + 0x4Cu * talk->kind;
        endEventHandler(row, talk, city);
        const int8_t next = port::load<int8_t>(row + 0x28u);
        port::store<int8_t>(port::load<uint32_t>(cell) + 9u, next);
        uint32_t level = static_cast<uint32_t>(port::guestS1());
        if (next > 0)
            level = 4;
        else if (next == 0)
        {
            level = port::load<uint8_t>(row + 0x23u);
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
    // person starts there; one that starts at level 3 also sets its tile flags and runs its Start slot. The result is
    // the running event's type.
    uint8_t Event_TryStart(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        if (GuestPtr<EventData>{port::load<uint32_t>(events.address + 0xA4u)}->kind == 0)
        {
            const uint32_t queue = globals::eventQueueObject.get();
            const uint32_t end = bucketEntry(queue, port::load<int32_t>(bucketStart(queue, 4)));
            for (uint32_t entry = bucketEntry(queue, port::load<int32_t>(bucketStart(queue, 3))); entry < end; entry += 2u)
            {
                if (Event_CheckCondition(events, city, GuestPtr<uint8_t>{entry}, person) == 0)
                    continue;
                fillEvent(GuestPtr<EventData>{port::load<uint32_t>(events.address + 0xA4u)}, entry, person);
                EventQueue_RemoveAt(static_cast<int>(queue), 3, GuestPtr<uint16_t>{entry});
                const GuestPtr<EventData> talk{port::load<uint32_t>(events.address + 0xA4u)};
                const uint32_t row = globals::eventTypes.get() + 0x4Cu * talk->kind;
                if (port::load<uint8_t>(row + 0x23u) == 3)
                {
                    const uint32_t tileFlags = port::load<uint8_t>(row + 0x25u);
                    if (tileFlags != 0)
                        Event_SetTileFlags(static_cast<int>(tileFlags) - 1);
                    port::store<uint8_t>(port::load<uint32_t>(events.address + 0xA4u) + 0xAu, 1);
                    callHandler(row, 4u, GuestPtr<EventData>{port::load<uint32_t>(events.address + 0xA4u)}, city, person.address);
                }
                break;
            }
        }
        return GuestPtr<EventData>{port::load<uint32_t>(events.address + 0xA4u)}->kind;
    }

    // A talk script starts an event for the person: with fewer than five events running and none of them waiting on the
    // person's character type (a halfword at +0x38 of the type), the first event waiting at level 2 whose condition holds
    // takes the first running slot (the others move up), with the person as actor A and the event data at +0xAC as B.
    int Event_StartFromScript(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        const uint32_t slots = events.address + 4u, slotsEnd = slots + 0xA0u;
        int running = 0;
        for (uint32_t slot = slots; port::load<uint8_t>(slot) != 0 && slot < slotsEnd; slot += 0x20u)
            ++running;
        if (running >= 5)
            return 0;
        const uint32_t types = globals::eventTypes.get();
        for (uint32_t slot = slots; port::load<uint8_t>(slot) != 0 && slot < slotsEnd; slot += 0x20u)
            if (person->charType == port::load<uint16_t>(types + 0x4Cu * port::load<uint8_t>(slot) + 0x38u))
                return 0;
        const uint32_t queue = globals::eventQueueObject.get();
        uint32_t entry = bucketEntry(queue, port::load<int32_t>(bucketStart(queue, 2)));
        const uint32_t end = bucketEntry(queue, port::load<int32_t>(bucketStart(queue, 3)));
        if (entry == end)
            return 0;
        do
        {
            if (Event_CheckCondition(events, city, GuestPtr<uint8_t>{entry}, person) != 0)
            {
                sdk::memmove(events.address + 0x24u, slots, 0x80);
                sdk::memset(slots, 0, 0x20);
                port::store<int8_t>(entry + 1u, 3);
                port::store<uint8_t>(slots, port::load<uint8_t>(entry));
                port::store<int8_t>(slots + 1u, port::load<int8_t>(entry + 1u));
                port::store<uint16_t>(events.address + 8u, person->family);
                port::store<uint16_t>(events.address + 0xAu, person->member);
                const uint32_t other = port::load<uint32_t>(events.address + 0xACu);
                port::store<uint16_t>(events.address + 0xCu, port::load<uint16_t>(other + 0x10u));
                port::store<uint16_t>(events.address + 0xEu, port::load<uint16_t>(other + 0x12u));
                const uint32_t talk = port::load<uint32_t>(events.address + 0xACu);
                sdk::memset(talk + 8u, 0, eventSize(talk));
                EventQueue_RemoveAt(static_cast<int>(queue), 2, GuestPtr<uint16_t>{entry});
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
        const uint32_t list = globals::kEventQueue, listEnd = list + 2u * static_cast<uint32_t>(globals::eventQueueLength.get());
        if (list < listEnd)
        {
            uint32_t at = list;
            do
            {
                if (port::load<uint16_t>(at) == static_cast<uint16_t>(id))
                    break;
                at += 2u;
                ++position;
            } while (at < listEnd);
        }
        const uint32_t queue = globals::eventQueueObject.get();
        uint32_t entry = bucketEntry(queue, port::load<int32_t>(bucketStart(queue, 5)));
        const uint32_t end = bucketEntry(queue, port::load<int32_t>(bucketStart(queue, 6)));
        if (!(entry < end))
            return 0;
        const uint32_t cell = events.address + 0xA4u + 4u * index;
        do
        {
            if (port::load<uint8_t>(entry) == position && Event_CheckCondition(events, city, GuestPtr<uint8_t>{entry}, person) != 0)
            {
                const uint8_t running = GuestPtr<EventData>{port::load<uint32_t>(cell)}->kind;
                bool start = true;
                if (running != 0)
                {
                    const uint32_t types = globals::eventTypes.get();
                    const int8_t newPriority = port::load<int8_t>(types + 0x4Cu * port::load<uint8_t>(entry) + 0x22u);
                    const int8_t oldPriority = port::load<int8_t>(types + 0x4Cu * running + 0x22u);
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
                    EventQueue_RemoveAt(static_cast<int>(queue), 5, GuestPtr<uint16_t>{entry});
                    const uint32_t row = globals::eventTypes.get() + 0x4Cu * GuestPtr<EventData>{port::load<uint32_t>(cell)}->kind;
                    const uint32_t tileFlags = port::load<uint8_t>(row + 0x25u);
                    if (tileFlags != 0)
                        Event_SetTileFlags(static_cast<int>(tileFlags) - 1);
                    port::store<uint8_t>(port::load<uint32_t>(cell) + 0xAu, 1);
                    callHandler(row, 4u, GuestPtr<EventData>{port::load<uint32_t>(cell)}, city, person.address);
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
    // The checks, in order, all on the type's 0x4C-byte record:
    //   the time window +0x2A..+0x2C against the minute counter (not for the levels 1 and 5), the population +0x30, the
    //   laptop modules +0x32, and for a person: the character types listed at +0x38 (one for level 2, else four), the
    //   gender +0x40, the friendship band +0x42, the claim meter of the workplace +0x41, the workplace types at +0x44,
    //   and where the person stands (+0x43: 1 in its workplace, 2 in its target, 3 away from the building it is
    //   between, 4 outside its workplace at least 8 from its middle), then the score tiers +0x35, +0x36 and the
    //   approval tier +0x34, and finally the handler's own Check slot (the handler of level 2 and of types of group 6
    //   pass the actor of the talk event 2 or 1, else the first).
    int64_t Event_CheckCondition(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, GuestPtr<uint8_t> type, GuestPtr<MapPerson> person)
    {
        port::GuestFrame<0xA0> frame; // the original's frame: the stack vectors below keep their leftover lanes
        const uint32_t row = globals::eventTypes.get() + 0x4Cu * *type;
        const auto byteAt = [&](uint32_t offset) { return static_cast<uint32_t>(port::load<uint8_t>(row + offset)); };
        const auto halfAt = [&](uint32_t offset) { return static_cast<uint32_t>(port::load<uint16_t>(row + offset)); };
        const uint32_t level = byteAt(0x23);
        if (level != 1 && level != 5)
        {
            const uint32_t start = halfAt(0x2A), end = halfAt(0x2C), now = port::load<uint16_t>(events.address);
            if (end < start)
            {
                if (end <= now && now < start)
                    return 0;
            }
            else if (now < start || end <= now)
                return 0;
        }
        if (city->map->population < static_cast<int32_t>(halfAt(0x30)))
            return 0;
        const uint32_t modules = halfAt(0x32);
        if (modules != (halfAt(0x32) & Laptop_CountActive()))
            return 0;
        if (person)
        {
            if (halfAt(0x38) != 0)
            {
                const uint32_t listed = level == 2 ? 1u : 4u;
                bool found = false;
                const uint32_t end = row + 0x38u + 2u * listed;
                for (uint32_t at = row + 0x38u; port::load<uint16_t>(at) != 0 && at < end; at += 2u)
                    if (port::load<uint16_t>(at) == person->charType)
                    {
                        found = true;
                        break;
                    }
                if (!found)
                    return 0;
            }
            if (byteAt(0x40) != 0 && static_cast<int32_t>(byteAt(0x40)) != (static_cast<int16_t>(person->flags) & 3) + 1)
                return 0;
            if (byteAt(0x42) != 0)
            {
                const int band = Family_GetFriendshipBand(person->family, person->member);
                if (byteAt(0x42) == 1 && band < 1)
                    return 0;
            }
            const GuestPtr<MapObject> work = Map_GetRecord(city->map, person->work);
            if (byteAt(0x41) != 0)
            {
                const int meter = ClaimMeter_Level(work->claimMeter);
                if (byteAt(0x41) == 1 && meter != 0)
                    return 0;
            }
            if (halfAt(0x44) != 0)
            {
                bool found = false;
                for (uint32_t at = row + 0x44u; port::load<uint16_t>(at) != 0 && at < row + 0x4Cu; at += 2u)
                    if (port::load<uint16_t>(at) == work->type)
                    {
                        found = true;
                        break;
                    }
                if (!found)
                    return 0;
            }
            if (byteAt(0x43) != 0)
            {
                const int tileX = sdk::fptosi(ps2f(64.0f) + ps2f(person->x));
                const int tileZ = sdk::fptosi(ps2f(64.0f) - ps2f(person->z));
                const GuestPtr<MapObject> here = Map_FindPersonObjectOnTile(city->map, person->index, static_cast<uint32_t>(tileX + (tileZ << 7)) & 0xFFFFu);
                if (!here)
                    return 0;
                const GuestPtr<BuildingType> hereType{globals::buildingTypeAddress(here->type)};
                const bool hasEntrance = (hereType->flags & 0x20u) != 0;
                const auto entranceAnswer = [&] { return port::callGuest<int64_t>(port::load<uint32_t>(hereType->vtable.address + 0x24u), hereType, city, here); };
                switch (byteAt(0x43))
                {
                case 1:
                    if (here->id != person->work)
                        return 0;
                    if (person->work == person->target && hasEntrance && entranceAnswer() != 0)
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
                    if (person->inside != person->work || here->id == person->inside)
                        return 0;
                    using namespace vu0;
                    const ps2f size = fromInt(GuestPtr<BuildingType>{globals::buildingTypeAddress(work->type)}->size);
                    const uint32_t base = frame.ptr().address;
                    const uint32_t tile = work->tile;
                    const GuestPtr<float> a{base + 0x80u}, b{base + 0x70u};
                    port::store<float>(a.address, static_cast<float>(person->x));
                    port::store<float>(b.address, static_cast<float>((fromInt(static_cast<int32_t>(tile & 0x7F)) - ps2f(64.0f)) + size / ps2f(2.0f)));
                    port::store<float>(a.address + 8u, static_cast<float>(person->z));
                    port::store<float>(b.address + 8u, static_cast<float>((fromInt(0x80 - static_cast<int32_t>(tile >> 7)) - ps2f(64.0f)) - size / ps2f(2.0f)));
                    vf(4) = load(GuestPtr<Vec4>{a.address});
                    vf(5) = load(GuestPtr<Vec4>{b.address});
                    set(vf(6), sub(vf(5), vf(4)), XYZW);
                    set(vf(7), mul(vf(6), vf(6)), XYZ);
                    set(vf(8), add(vf(7), lane(vf(7), 2)), X);
                    q() = sqrtQ(x(vf(8)));
                    set(vf(9), add(vf(0), _mm_set1_ps(q())), XYZW);
                    store(GuestPtr<Vec4>{base + 0x90u}, vf(9));
                    if (ps2f(port::load<float>(base + 0x90u)) < ps2f(8.0f))
                        return 0;
                    break;
                }
                default:
                    return 0;
                }
            }
        }
        if (byteAt(0x35) != 0 && static_cast<int>(byteAt(0x35)) - 1 != CityScene_ScoreA0Tier(city))
            return 0;
        if (byteAt(0x36) != 0 && static_cast<int>(byteAt(0x36)) - 1 != CityScene_ScoreA8Tier(city))
            return 0;
        if (byteAt(0x34) != 0 && static_cast<int>(byteAt(0x34)) - 1 != CityScene_ApprovalTier(city))
            return 0;
        const uint32_t handler = byteAt(0x24);
        if (handler == 0)
            return 1;
        const uint32_t which = level == 2 ? 2u : byteAt(0x20) == 6 ? 1u : 0u;
        const uint32_t check = port::load<uint32_t>(port::load<uint32_t>(kEventHandlers + 4u * handler));
        if (check == 0)
            return 1;
        return port::callGuest<int64_t>(check, GuestPtr<EventData>{port::load<uint32_t>(events.address + 0xA4u + 4u * which)}, city, person);
    }

    namespace
    {
        // The talk event `cell` takes the first waiting entry of bucket `bucket` whose condition holds (no person):
        // the entry leaves the bucket. Returns whether one did.
        bool startFromBucket(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, uint32_t cell, int bucket)
        {
            const uint32_t queue = globals::eventQueueObject.get();
            const uint32_t end = bucketEntry(queue, port::load<int32_t>(bucketStart(queue, bucket + 1)));
            for (uint32_t entry = bucketEntry(queue, port::load<int32_t>(bucketStart(queue, bucket))); entry < end; entry += 2u)
            {
                if (Event_CheckCondition(events, city, GuestPtr<uint8_t>{entry}, GuestPtr<MapPerson>{0}) == 0)
                    continue;
                fillEvent(GuestPtr<EventData>{port::load<uint32_t>(cell)}, entry, GuestPtr<MapPerson>{0});
                EventQueue_RemoveAt(static_cast<int>(queue), bucket, GuestPtr<uint16_t>{entry});
                return true;
            }
            return false;
        }

        // A running event that has not begun (var 0) begins when its start time has come: its tile flags are set and
        // its Start slot runs (with the person slot 0).
        void beginEvent(uint32_t row, GuestPtr<EventData> talk, GuestPtr<CityScene> city)
        {
            const uint32_t tileFlags = port::load<uint8_t>(row + 0x25u);
            if (tileFlags != 0)
                Event_SetTileFlags(static_cast<int>(tileFlags) - 1);
            port::store<uint8_t>(talk.address + 0xAu, 1);
            callHandler(row, 4u, talk, city, 0);
        }
    }

    // The scheduler, called every frame. The minute counter (events +0) follows the city clock. When the clock wraps
    // to a new day, the waiting lists are reorganised (the population rank, the due waits) and the running events'
    // counters tick; every minute of the day (at the hour) the first fitting waiting entry takes the second talk slot,
    // and when the first talk slot is free one from the next bucket, or a postponed event from the running list,
    // takes it. Last, each talk event begins when its start time has come, ends when its end time has, or updates.
    void Event_Update(GuestPtr<EventMgr> events, GuestPtr<CityScene> city)
    {
        const uint32_t queue = globals::eventQueueObject.get();
        const uint32_t types = globals::eventTypes.get();
        const int32_t minute = city->clock / 60;
        const uint32_t counter = port::load<uint16_t>(events.address);
        if (static_cast<uint32_t>(minute) == counter)
            return;
        const uint32_t cellA = events.address + 0xA4u, cellB = events.address + 0xA8u;
        const auto talkKind = [&](uint32_t cell) { return port::load<uint8_t>(port::load<uint32_t>(cell) + 8u); };
        port::GuestFrame<0x90> frame;
        const uint32_t ctx = frame.ptr().address;
        if (!((static_cast<uint32_t>(minute) & 0xFFFFu) < counter))
        {
            // The same day: the counter follows the clock; on the hour a second event may start.
            port::store<uint16_t>(events.address, static_cast<uint16_t>(minute));
            if (port::load<uint16_t>(events.address) % 60 == 0 && talkKind(cellB) == 0)
            {
                const uint8_t running = talkKind(cellA);
                if (running == 0 || port::load<uint8_t>(types + 0x4Cu * running + 0x20u) != 3)
                    startFromBucket(events, city, cellB, 0);
            }
        }
        else
        {
            port::store<uint16_t>(events.address, static_cast<uint16_t>(minute));
            const uint32_t rank = port::load<uint8_t>(events.address + 2u);
            if (static_cast<int32_t>(rank) < CityScene_PopulationRank(city))
            {
                port::store<uint8_t>(events.address + 2u, static_cast<uint8_t>(CityScene_PopulationRank(city)));
                const auto mask = [&] { return static_cast<int32_t>(1u << ((port::load<uint8_t>(events.address + 2u) - 1u) & 0x1Fu)); };
                port::store<int32_t>(ctx + 0x60u, static_cast<int32_t>(types));
                port::store<int32_t>(ctx + 0x64u, mask());
                for (int bucket = 0; bucket < 4; ++bucket)
                    EventList_MoveOffRankB(static_cast<int>(queue), bucket, 6, GuestPtr<int32_t>{ctx + 0x60u});
                EventList_MoveOffRankB(static_cast<int>(queue), 4, 6, GuestPtr<int32_t>{ctx + 0x60u});
                for (int to = 0; to < 3; ++to)
                {
                    port::store<int32_t>(ctx + 0x68u, static_cast<int32_t>(types));
                    port::store<int32_t>(ctx + 0x6Cu, mask());
                    port::store<int32_t>(ctx + 0x70u, to);
                    EventList_MoveOnRank(static_cast<int>(queue), 5, to, GuestPtr<int32_t>{ctx + 0x68u});
                }
                port::store<int32_t>(ctx + 0x78u, static_cast<int32_t>(types));
                port::store<int32_t>(ctx + 0x7Cu, mask());
                EventList_MoveOffRank(static_cast<int>(queue), 5, 3, GuestPtr<int32_t>{ctx + 0x78u});
            }
            // The entries waiting in bucket 4 count their wait down.
            for (uint32_t entry = bucketEntry(queue, port::load<int32_t>(bucketStart(queue, 4))); entry != bucketEntry(queue, port::load<int32_t>(bucketStart(queue, 5))); entry += 2u)
                port::store<int8_t>(entry + 1u, static_cast<int8_t>(port::load<int8_t>(entry + 1u) - 1));
            for (int to = 0; to < 3; ++to)
            {
                port::store<int32_t>(ctx + 0x80u, static_cast<int32_t>(port::load<uint32_t>(0x001BDA84u)));
                port::store<int32_t>(ctx + 0x84u, to);
                EventList_MoveDueTo(static_cast<int>(queue), 4, to, GuestPtr<int32_t>{ctx + 0x80u});
            }
            port::store<int32_t>(ctx + 0x8Cu, static_cast<int32_t>(port::load<uint32_t>(0x001BDA84u)));
            EventList_MoveDue(static_cast<int>(queue), 4, 3, GuestPtr<int32_t>{ctx + 0x8Cu});
            // The postponed events (the running list) count down and return to bucket 4 when they run out.
            const uint32_t slotsEnd = events.address + 4u + 0xA0u;
            for (uint32_t slot = events.address + 4u; port::load<uint8_t>(slot) != 0 && slot < slotsEnd;)
            {
                const int8_t left = static_cast<int8_t>(port::load<int8_t>(slot + 1u) - 1);
                port::store<int8_t>(slot + 1u, left);
                if (left > 0)
                {
                    slot += 0x20u;
                    continue;
                }
                port::store<int8_t>(slot + 1u, port::load<int8_t>(globals::eventTypes.get() + 0x4Cu * port::load<uint8_t>(slot) + 0x28u));
                EventQueue_InsertByPriority(static_cast<int>(queue), GuestPtr<uint8_t>{slot}, 4, 0);
                const uint32_t last = events.address + 0x84u;
                if (slot < last)
                    sdk::memmove(slot, slot + 0x20u, (static_cast<int32_t>(last - slot) >> 5) << 5);
                sdk::memset(last, 0, 0x20);
            }
            // The second talk event: while it runs its var +9 counts down (and finishes it at 0); when free, one may start.
            const GuestPtr<EventData> talkB{port::load<uint32_t>(cellB)};
            bool talkBDone = false;
            if (talkB->kind != 0)
            {
                const int8_t counted = port::load<int8_t>(talkB.address + 9u);
                if (counted > 0)
                {
                    port::store<int8_t>(talkB.address + 9u, static_cast<int8_t>(counted - 1));
                    if (!(static_cast<int8_t>(counted - 1) > 0))
                        Event_Finish(events, city, 1);
                }
                talkBDone = true;
            }
            if (!talkBDone)
            {
                const uint8_t running = talkKind(cellA);
                if (running == 0 || port::load<uint8_t>(types + 0x4Cu * running + 0x20u) != 3)
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
                        const uint32_t first = events.address + 4u;
                        for (uint32_t slot = events.address + 0x84u; slot >= first; slot -= 0x20u)
                        {
                            const uint8_t kind = port::load<uint8_t>(slot);
                            if (kind == 0 || port::load<uint16_t>(slot + 0x1Cu) == 0)
                                continue;
                            const uint32_t row = types + 0x4Cu * kind;
                            if (port::load<uint8_t>(row + 0x20u) != 4)
                                continue;
                            now->kind = kind;
                            port::store<int8_t>(now.address + 9u, port::load<int8_t>(row + 0x29u));
                            port::store<uint8_t>(now.address + 0xAu, 0);
                            const uint32_t handler = port::load<uint8_t>(row + 0x24u);
                            port::store<uint32_t>(now.address + 4u, handler == 0 ? 0u : port::load<uint32_t>(port::load<uint32_t>(kEventHandlers + 4u * handler) + 8u));
                            const GuestPtr<ActorRef> actors{port::load<uint32_t>(cellA) + 0xCu};
                            uint32_t actor = actors.address;
                            for (uint32_t from = slot + 4u; from < slot + 0x20u; from += 4u, actor += 8u)
                            {
                                port::store<uint16_t>(actor + 4u, port::load<uint16_t>(from));
                                port::store<uint16_t>(actor + 6u, port::load<uint16_t>(from + 2u));
                                port::store<uint16_t>(actor, 6000);
                                port::store<uint16_t>(actor + 2u, kNoObject);
                            }
                            const uint32_t last = events.address + 0x84u;
                            if (slot < last)
                                sdk::memmove(slot, slot + 0x20u, (static_cast<int32_t>(last - slot) >> 5) << 5);
                            sdk::memset(last, 0, 0x20);
                            beginEvent(types + 0x4Cu * talkKind(cellA), GuestPtr<EventData>{port::load<uint32_t>(cellA)}, city);
                            break;
                        }
                    }
                }
            }
            else
            {
                const int8_t counted = port::load<int8_t>(talkA.address + 9u);
                if (counted > 0)
                {
                    port::store<int8_t>(talkA.address + 9u, static_cast<int8_t>(counted - 1));
                    if (!(static_cast<int8_t>(counted - 1) > 0))
                        Event_Finish(events, city, 0);
                }
            }
        }
        // Each talk event: one that has not begun (var 0) begins at its start time; a running one whose var +9 says
        // 1 finishes at its end time; otherwise its Update slot runs.
        for (uint32_t i = 0; i < 2; ++i)
        {
            const GuestPtr<EventData> talk{port::load<uint32_t>(events.address + 0xA4u + 4u * i)};
            if (talk->kind == 0)
                continue;
            const uint32_t row = types + 0x4Cu * talk->kind;
            const uint32_t now = port::load<uint16_t>(events.address);
            if (port::load<uint8_t>(talk.address + 0xAu) == 0)
            {
                if (!(now < port::load<uint16_t>(row + 0x2Au)))
                    beginEvent(row, GuestPtr<EventData>{port::load<uint32_t>(events.address + 0xA4u + 4u * i)}, city);
            }
            else if (port::load<int8_t>(talk.address + 9u) == 1 && !(now < port::load<uint16_t>(row + 0x2Eu)))
                Event_Finish(events, city, static_cast<int>(i));
            else if (const uint32_t update = port::load<uint32_t>(talk.address + 4u))
                port::callGuest<void>(update, talk, city);
        }
    }

    // After a game is loaded the event lists are matched with the event table of this build: the ids saved with the game
    // (the list at 0x1C5790, one halfword per type record) against the table's. A type that was not in the saved list is
    // put in the waiting queue (by its rank and level) and every stored type number from it on goes up by one; a saved
    // type no longer in the table loses its running event or queue entry and the numbers above it go down. A changed
    // list is stored again; last, the running events get their Update slot back (and the first its tile flags).
    void Event_InitForCity(GuestPtr<EventMgr> events)
    {
        port::GuestFrame<0x90> frame;
        const uint32_t queue = globals::eventQueueObject.get();
        const uint32_t types = globals::eventTypes.get();
        const uint32_t tableEnd = types + 0x4Cu * static_cast<uint32_t>(globals::eventQueueLength.get());
        const auto talkAt = [&](uint32_t i) { return port::load<uint32_t>(events.address + 0xA4u + 4u * i); };
        const auto queueStart = [&] { return bucketEntry(queue, port::load<int32_t>(bucketStart(queue, 0))); };
        const auto queueEnd = [&] { return bucketEntry(queue, port::load<int32_t>(bucketStart(queue, 8))); };
        const uint32_t slotsEnd = events.address + 4u + 0xA0u;
        uint32_t saved = globals::kEventQueue;
        uint32_t record = types;
        int32_t index = 0;
        bool changed = false;
        while (record < tableEnd)
        {
            const uint32_t current = port::load<uint16_t>(record + 0x20u), old = port::load<uint16_t>(saved);
            if (old == current)
            {
                record += 0x4Cu;
                ++index;
                saved += 2u;
                continue;
            }
            if (old > current && current != 0)
            {
                // A type new to the saved game goes into the queue and the numbers from its index up shift.
                const uint32_t rank = port::load<uint8_t>(events.address + 2u);
                const uint32_t mask = rank == 0 ? 0u : 1u << ((rank - 1u) & 0x1Fu);
                const uint32_t rankMask = port::load<uint8_t>(record + 0x27u);
                uint32_t level = 6;
                if ((rankMask & mask & 0xFFu) == 0)
                {
                    if ((mask & 0xFFu) < rankMask)
                        level = 5;
                }
                else
                {
                    level = port::load<uint8_t>(record + 0x23u);
                    if (level == 4)
                        level = 3;
                }
                const uint32_t entry = frame.ptr().address + 0x8Cu;
                port::store<uint8_t>(entry, static_cast<uint8_t>(index));
                port::store<int8_t>(entry + 1u, port::load<int8_t>(record + 0x28u));
                EventQueue_InsertByPriority(static_cast<int>(queue), GuestPtr<uint8_t>{entry}, static_cast<int>(level), 0);
                for (uint32_t i = 0; i < 2; ++i)
                {
                    const uint32_t talk = talkAt(i);
                    const uint32_t kind = port::load<uint8_t>(talk + 8u);
                    if (kind != 0 && static_cast<int32_t>(kind) >= index)
                        port::store<uint8_t>(talk + 8u, static_cast<uint8_t>(kind + 1));
                }
                for (uint32_t slot = events.address + 4u; port::load<uint8_t>(slot) != 0 && slot < slotsEnd; slot += 0x20u)
                    if (static_cast<int32_t>(port::load<uint8_t>(slot)) >= index)
                        port::store<uint8_t>(slot, static_cast<uint8_t>(port::load<uint8_t>(slot) + 1));
                for (uint32_t at = queueStart(), end = queueEnd(); at < end; at += 2u)
                    if (static_cast<int32_t>(port::load<uint8_t>(at)) >= index)
                        port::store<uint8_t>(at, static_cast<uint8_t>(port::load<uint8_t>(at) + 1));
                record += 0x4Cu;
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
                    const uint32_t last = events.address + 0x84u;
                    if (slot < last)
                        sdk::memmove(slot, slot + 0x20u, (static_cast<int32_t>(last - slot) >> 5) << 5);
                    sdk::memset(last, 0, 0x20);
                    removed = true;
                    break;
                }
                for (int bucket = 0; !removed && bucket < 8; ++bucket)
                {
                    const uint32_t end = bucketEntry(queue, port::load<int32_t>(bucketStart(queue, bucket + 1)));
                    for (uint32_t at = bucketEntry(queue, port::load<int32_t>(bucketStart(queue, bucket))); at < end; at += 2u)
                        if (static_cast<int32_t>(port::load<uint8_t>(at)) == index)
                        {
                            EventQueue_RemoveAt(static_cast<int>(queue), bucket, GuestPtr<uint16_t>{at});
                            removed = true;
                            break;
                        }
                }
            }
            for (uint32_t i = 0; i < 2; ++i)
            {
                const uint32_t talk = talkAt(i);
                const uint32_t kind = port::load<uint8_t>(talk + 8u);
                if (kind != 0 && index < static_cast<int32_t>(kind))
                    port::store<uint8_t>(talk + 8u, static_cast<uint8_t>(kind - 1));
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
            sdk::memset(globals::kEventQueue, 0, 0x100);
            uint32_t out = globals::kEventQueue;
            for (uint32_t at = types, end = types + 0x4Cu * static_cast<uint32_t>(globals::eventQueueLength.get()); at < end; at += 0x4Cu, out += 2u)
            {
                port::store<uint8_t>(out, port::load<uint8_t>(at + 0x20u));
                port::store<uint8_t>(out + 1u, port::load<uint8_t>(at + 0x21u));
            }
        }
        for (uint32_t i = 0; i < 2; ++i)
        {
            const uint32_t talk = talkAt(i);
            const uint32_t kind = port::load<uint8_t>(talk + 8u);
            if (kind == 0)
                continue;
            const uint32_t row = globals::eventTypes.get() + 0x4Cu * kind;
            const uint32_t handler = port::load<uint8_t>(row + 0x24u);
            port::store<uint32_t>(talk + 4u, handler == 0 ? 0u : port::load<uint32_t>(port::load<uint32_t>(kEventHandlers + 4u * handler) + 8u));
            if (i == 0 && port::load<uint8_t>(talkAt(0) + 0xAu) != 0 && port::load<uint8_t>(row + 0x25u) != 0)
                Event_SetTileFlags(static_cast<int>(port::load<uint8_t>(row + 0x25u)) - 1);
        }
    }

    PORT_FN(Event_CheckCondition);
    PORT_FN(Event_Update);
    PORT_FN(Event_InitForCity);

    // ---- the shopping event's start (batch 40)

    // A person at a shop (a building of category 5) starts a shopping trip: the person is the first actor and the shop
    // record (0x28 bytes each in the table at 0x1BDAB4, taken in turn: counter 0x1BDD5C) lists up to four goods (halfwords
    // at +0x20, stored in the event's +0x34 as the record number and counted in +0x38). The buildings round the shop (a
    // square 30 tiles wider than its footprint) that are shops with an entrance and neither flag 0x240 are searched, in
    // the order of the map's 80 tile lists, for a resident family that sells the wanted goods this season: each item found
    // takes an actor slot in the event data (+0x8 * item + 0xC: person index, +0xE: shop, +0x10: family), its price goes to
    // +0x3C + 4 * item and the total (+0x4C). When every item has a seller the total is rounded up to the next thousand
    // and the event starts (1); when the whole map has been searched without that the event data is cleared (0).
    int EvShopping_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        port::GuestFrame<0x130> frame; // the wanted goods at +0x120, the tile list count at +0x128, a goods offset at +0x12C, the footprint at +0x110
        const uint32_t base = frame.ptr().address;
        const GuestPtr<MapObject> work = Map_GetRecord(city->map, person->work);
        const GuestPtr<BuildingType> shop{globals::buildingTypeAddress(work->type)};
        if (shop->category != 5)
            return 0;
        port::store<uint16_t>(data.address + 0xCu, person->index);
        port::store<uint16_t>(data.address + 0xEu, person->work);
        port::store<uint16_t>(data.address + 0x10u, person->family);
        port::store<uint16_t>(data.address + 0x12u, person->member);
        constexpr uint32_t kSerial = 0x001BDD5Cu, kShopCount = 0x001BDAB0u;
        const int32_t serial = port::load<int32_t>(kSerial);
        port::store<int32_t>(data.address + 0x34u, serial);
        const uint32_t record = globals::shoppingTexts.get() + 0x28u * static_cast<uint32_t>(serial);
        port::store<int32_t>(kSerial, serial + 1);
        if (!(serial + 1 < port::load<int32_t>(kShopCount)))
            port::store<int32_t>(kSerial, 0);
        sdk::memcpy(base + 0x120u, record + 0x20u, 8);
        int remaining = 0;
        for (uint32_t at = base + 0x120u; port::load<uint16_t>(at) != 0 && at < base + 0x128u; at += 2u)
            ++remaining;
        port::store<int32_t>(data.address + 0x38u, remaining);
        // The area the shop is searched round: its tile, its size and 30 tiles more on every side.
        const int32_t tile = work->tile;
        const int32_t areaX0 = (tile & 0x7F) - 30, areaZ0 = (tile >> 7) - 30;
        const int32_t areaX1 = (tile & 0x7F) + shop->size + 30, areaZ1 = (tile >> 7) + shop->size + 30;
        const uint32_t footprint = base + 0x110u;
        for (int list = 0; list < 0x50; ++list)
        {
            const uint32_t items = Map_GetTileList(city->map, list, GuestPtr<uint32_t>{base + 0x128u}).address;
            const uint32_t end = items + 2u * port::load<uint32_t>(base + 0x128u);
            for (uint32_t at = items; at < end; at += 2u)
            {
                const GuestPtr<MapObject> obj = Map_GetRecord(city->map, port::load<uint16_t>(at));
                if (port::load<uint8_t>(port::load<uint32_t>(0x001BDA9Cu) + 4u * obj->type + 2u) == 0)
                    continue;
                if ((obj->flags & 0x20u) == 0 || (obj->flags & 0x240u) != 0)
                    continue;
                Building_GetFootprint(obj, 0, GuestPtr<uint32_t>{footprint});
                const int32_t fx0 = port::load<int32_t>(footprint), fz0 = port::load<int32_t>(footprint + 4u);
                const int32_t fx1 = port::load<int32_t>(footprint + 8u), fz1 = port::load<int32_t>(footprint + 12u);
                if (!(areaX0 < fx1 && fx0 < areaX1 && areaZ0 < fz1 && fz0 < areaZ1))
                    continue;
                if (!Map_FindPersonEntry(city->map, static_cast<int16_t>(obj->family), 0))
                    continue;
                for (int pass = 0; pass < 4; ++pass)
                {
                    port::store<int32_t>(base + 0x12Cu, pass);
                    const int32_t month = remainder(city->monthCount, 12);
                    const int goods = Goods_GetRecordOffset(obj->type, obj->family, (month < 0 ? month + 3 : month) >> 2, GuestPtr<int32_t>{base + 0x12Cu});
                    for (uint32_t entry = base + 0x120u; entry < base + 0x128u; entry += 2u)
                    {
                        const uint32_t wanted = port::load<uint16_t>(entry);
                        if (wanted == 0 || wanted != port::load<uint16_t>(static_cast<uint32_t>(goods)))
                            continue;
                        const uint32_t number = (entry + 2u - (base + 0x120u)) >> 1;
                        port::store<uint16_t>(entry, 0);
                        const uint32_t slot = data.address + 8u * number;
                        port::store<uint16_t>(slot + 0xEu, obj->id);
                        port::store<uint16_t>(slot + 0x10u, obj->family);
                        port::store<uint16_t>(slot + 0x12u, 0);
                        const GuestPtr<MapPerson> seller = Map_FindPersonEntry(city->map, static_cast<int16_t>(port::load<uint16_t>(slot + 0x10u)), port::load<uint16_t>(slot + 0x12u));
                        port::store<uint16_t>(slot + 0xCu, seller->index);
                        port::store<int32_t>(data.address + 4u * ((entry + 0x14u - (base + 0x120u)) >> 1) + 0x14u, Goods_GetPrice(goods));
                        --remaining;
                        port::store<int32_t>(data.address + 0x4Cu, static_cast<int32_t>(static_cast<uint32_t>(port::load<int32_t>(data.address + 0x4Cu)) + static_cast<uint32_t>(Goods_GetPrice(goods))));
                        if (remaining <= 0)
                        {
                            const uint32_t thousands = static_cast<uint32_t>(port::load<int32_t>(data.address + 0x4Cu) / 1000) + 1u;
                            port::store<uint32_t>(data.address + 0x4Cu, thousands * 1000u);
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
            port::store<uint32_t>(object, 0x001BD908u);
            port::store<uint8_t>(object + 8u, 0);
            port::store<uint8_t>(object + 9u, 0);
            const uint32_t dataSize = port::callGuest<uint32_t>(port::load<uint32_t>(port::load<uint32_t>(object) + 8u), object);
            sdk::memset(object + 8u, 0, static_cast<int>(dataSize));
            if (derived)
            {
                // The talk event the script works with: the derived class (vtable 0x1BD8F8) with 0x60 more bytes cleared.
                port::store<uint32_t>(object, 0x001BD8F8u);
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
        port::store<uint32_t>(manager + 0xA4u, newEventObject(0x84u, true));
        port::store<uint32_t>(manager + 0xA8u, newEventObject(0x24u, false));
        port::store<uint32_t>(manager + 0xACu, newEventObject(0x24u, false));
        globals::eventTypesCopy.set(globals::eventTypes.get());
        const int32_t stage = port::load<int32_t>(0x001C5548u);
        if (stage >= 0)
        {
            if (stage == 0)
                EventList_MoveAll(static_cast<int>(globals::eventQueueObject.get()), 7, 6);
            // The context for the move: the event types array, then a mask with the bit of the stage (7 at most).
            port::GuestLocal<uint64_t> context;
            port::store<uint32_t>(context.ptr().address, globals::eventTypes.get());
            const int32_t shift = port::load<int32_t>(0x001C5548u) < 8 ? port::load<int32_t>(0x001C5548u) : 7;
            port::store<uint8_t>(context.ptr().address + 4u, static_cast<uint8_t>(1u << (shift & 0x1F)));
            EventList_MoveByMask26(static_cast<int>(globals::eventQueueObject.get()), 6, 5, GuestPtr<int32_t>{context.ptr().address});
        }
        return events;
    }

    // Reads DATA\BIN\EVENT.BIN (0x4C-byte records) into a new array, keeps two bytes of each record, creates the event queue
    // (0x224 bytes) and queues every record after the first at level 6 by its priority byte (+0x28).
    void Event_LoadTypes()
    {
        port::GuestLocal<GameFile> file;
        File_ctor(file, 0);
        if (File_Open(file, GuestPtr<char>{0x001B6370u}) >= 0)
        {
            const uint32_t size = static_cast<uint32_t>(File_Seek(file, 0, 2, 0));
            globals::eventQueueLength.set(static_cast<int32_t>(size / 0x4Cu));
            const uint32_t count = static_cast<uint32_t>(globals::eventQueueLength.get());
            globals::eventTypes.set(sdk::mw_NewArray(count * 0x4Cu));
            File_Seek(file, 0, 0, 0);
            File_Read(file, static_cast<int>(globals::eventTypes.get()), static_cast<int>(globals::eventQueueLength.get()), 0x4C, 0);
            File_Close(file);
            // Bytes +0x20 and +0x21 of each record, into the table at 0x1C5790.
            uint32_t out = globals::kEventQueue;
            const uint32_t end = globals::eventTypes.get() + static_cast<uint32_t>(globals::eventQueueLength.get()) * 0x4Cu;
            for (uint32_t record = globals::eventTypes.get(); record < end; record += 0x4Cu)
            {
                port::store<uint8_t>(out, port::load<uint8_t>(record + 0x20u));
                port::store<uint8_t>(out + 1u, port::load<uint8_t>(record + 0x21u));
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
            port::GuestLocal<uint16_t> id;
            port::store<uint8_t>(id.ptr().address, 0);
            port::store<uint8_t>(id.ptr().address + 1u, 0);
            const uint32_t stop = globals::eventTypes.get() + static_cast<uint32_t>(globals::eventQueueLength.get()) * 0x4Cu;
            for (uint32_t record = globals::eventTypes.get() + 0x4Cu; record < stop; record += 0x4Cu)
            {
                port::store<uint8_t>(id.ptr().address, static_cast<uint8_t>(port::load<uint8_t>(id.ptr().address) + 1));
                port::store<uint8_t>(id.ptr().address + 1u, port::load<uint8_t>(record + 0x28u));
                EventQueue_InsertByPriority(static_cast<int>(globals::eventQueueObject.get()), GuestPtr<uint8_t>{id.ptr().address}, 6, 0);
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
            port::store<uint32_t>(object, 0x001BD8E0u);
            sdk::sceGsSyncPath(0, 0);
            const uint32_t frame = port::load<uint32_t>(object + 4u);
            if (frame != 0u)
            {
                Frame_DeletingDtor(GuestPtr<Frame>{frame}, 1);
                port::store<uint32_t>(object + 4u, 0u);
            }
            port::store<uint32_t>(object, 0x001BD8B0u);
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
        const uint32_t entry = events.address + 0x20u * static_cast<uint32_t>(slot);
        const uint32_t kind = port::load<uint8_t>(entry + 4u);
        const uint32_t familyB = port::load<uint16_t>(entry + 0xCu);
        const uint32_t title = globals::eventTypes.get() + 0x4Cu * kind;
        if (familyB == 0)
            return GuestPtr<char>{title};
        constexpr uint32_t kBuffer = 0x001C5990u;
        const GuestPtr<Family> family = Family_Get(static_cast<int>(familyB), 0);
        sdk::strcpy(kBuffer, title);
        const uint32_t name = family.address + 0x4Cu + 0x48u * port::load<uint16_t>(entry + 0xEu);
        const int length = sdk::strlen(kBuffer);
        const int nameLength = sdk::strlen(name);
        if (static_cast<uint32_t>(length + nameLength) < 0x19u)
            sdk::strcpy(kBuffer + static_cast<uint32_t>(length), name);
        else
        {
            sdk::memcpy(kBuffer + static_cast<uint32_t>(length), name, 0x17 - length);
            port::store<uint8_t>(0x001C59A7u, 0xC4);
            port::store<uint8_t>(0x001C59A8u, 0);
        }
        return GuestPtr<char>{kBuffer};
    }

    PORT_FN(Event_FormatText);

    // The missing children event may start when the city has stage flag 2, the person's family has a member of character type
    // 1 or 2 (the child) and a shop is found for the person: the event then names the person's workplace, the family and the
    // child (member index) for the first actor and the family's adult (member 0) for the second.
    int EvMissingChildren_Check(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        if ((city->flags & 2u) == 0)
            return 0;
        const GuestPtr<Family> family = Family_Get(person->family, 0);
        const uint32_t members = family.address + 0x24u;
        const uint32_t count = port::load<uint16_t>(family.address + 0x22u);
        int32_t child = -1;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t charType = port::load<uint16_t>(members + 0x48u * i);
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
        const uint32_t d = data.address;
        port::store<uint16_t>(d + 0x16u, person->work);
        port::store<uint16_t>(d + 0x18u, person->family);
        port::store<uint16_t>(d + 0x1Au, static_cast<uint16_t>(child));
        port::store<uint16_t>(d + 0x14u, Map_FindPersonEntry(city->map, static_cast<int16_t>(port::load<uint16_t>(d + 0x18u)), port::load<uint16_t>(d + 0x1Au))->index);
        port::store<uint16_t>(d + 0x1Eu, person->work);
        port::store<uint16_t>(d + 0x20u, person->family);
        port::store<uint16_t>(d + 0x22u, 0);
        port::store<uint16_t>(d + 0x1Cu, Map_FindPersonEntry(city->map, static_cast<int16_t>(port::load<uint16_t>(d + 0x20u)), port::load<uint16_t>(d + 0x22u))->index);
        return 1;
    }

    PORT_FN(EvMissingChildren_Check);

    // Whether the family member `familyMember` (family, member) can be added to the event slot `choice`: 4 when the member is
    // already its second person, 0 when the event type lists the character types it takes (up to four halfwords at +0x38 of the
    // type's record) and the member's is not among them, 3 when the member is its first person, 1 when the member is already on
    // its list of five, otherwise 2 (the member goes on the list: straight away, or, for a type with a check function at +0x14
    // of its descriptor, only when the check passes; it is run on the event data kept at +0xAC, which is then cleared again,
    // and a failed check ends in 0).
    int Event_FindForFamily(GuestPtr<EventMgr> events, GuestPtr<CityScene> city, int choice, GuestPtr<int16_t> familyMember)
    {
        port::GuestFrame<0x90> frame;
        const uint32_t entry = events.address + 0x20u * static_cast<uint32_t>(choice);
        const uint32_t member = familyMember.address;
        const uint32_t family = port::load<uint16_t>(member), index = port::load<uint16_t>(member + 2u);
        if (port::load<uint16_t>(entry + 0xCu) == family && port::load<uint16_t>(entry + 0xEu) == index)
            return 4;
        const uint32_t type = globals::eventTypes.get() + 0x4Cu * port::load<uint8_t>(entry + 4u);
        if (port::load<uint16_t>(type + 0x38u) != 0)
        {
            const GuestPtr<Family> record = Family_Get(static_cast<int>(family), 0);
            const uint32_t charType = port::load<uint16_t>(record.address + 0x24u + 0x48u * index);
            bool found = false;
            for (uint32_t allowed = type + 0x38u; allowed < type + 0x40u; allowed += 2u)
            {
                const uint32_t value = port::load<uint16_t>(allowed);
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
        if (port::load<uint16_t>(entry + 8u) == family && port::load<uint16_t>(entry + 0xAu) == index)
            return 3;
        int result = 2;
        for (uint32_t list = entry + 0x10u; list < entry + 0x24u; list += 4u)
        {
            if (port::load<uint16_t>(list) == 0)
            {
                const uint32_t checkType = port::load<uint8_t>(type + 0x24u);
                const uint32_t descriptor = checkType != 0 ? port::load<uint32_t>(0x001AF0D0u + 4u * checkType) : 0u;
                const uint32_t check = descriptor != 0 ? port::load<uint32_t>(descriptor + 0x14u) : 0u;
                if (check == 0)
                {
                    port::store<uint16_t>(list, static_cast<uint16_t>(family));
                    port::store<uint16_t>(list + 2u, static_cast<uint16_t>(index));
                    return 2;
                }
                const uint32_t probe = port::load<uint32_t>(events.address + 0xACu);
                port::store<uint16_t>(probe + 0xCu, 6000);
                port::store<uint16_t>(probe + 0xEu, 0x3F80);
                port::store<uint16_t>(probe + 0x10u, port::load<uint16_t>(entry + 0xCu));
                port::store<uint16_t>(probe + 0x12u, port::load<uint16_t>(entry + 0xEu));
                const uint32_t arguments = frame.ptr().address + 0x88u;
                port::store<uint64_t>(arguments, port::load<uint64_t>(0x001BD988u));
                port::store<uint16_t>(arguments + 4u, static_cast<uint16_t>(family));
                port::store<uint16_t>(arguments + 6u, static_cast<uint16_t>(index));
                const uint32_t function = port::load<uint32_t>(port::load<uint32_t>(0x001AF0D0u + 4u * port::load<uint8_t>(type + 0x24u)) + 0x14u);
                if (port::callGuest<int64_t>(function, probe, city, arguments, 0) == 0)
                    result = 0;
                else
                {
                    port::store<uint16_t>(list, port::load<uint16_t>(member));
                    port::store<uint16_t>(list + 2u, port::load<uint16_t>(member + 2u));
                }
                const uint32_t after = port::load<uint32_t>(events.address + 0xACu);
                const int size = port::callGuest<int>(port::load<uint32_t>(port::load<uint32_t>(after) + 8u), after);
                sdk::memset(after + 8u, 0, size);
                return result;
            }
            if (port::load<uint16_t>(list) == family && port::load<uint16_t>(list + 2u) == index)
                return 1;
        }
        return result;
    }

    PORT_FN(Event_FindForFamily);

    // The alien event's slot 0x14 (called with mode 0 at its start): the family at +0x15C of the event data table (0x1BDA8C) is
    // added to the applicants, selected. The result is 0.
    int EvAlien_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        (void)data;
        (void)actor;
        if (mode == 0)
            CityScene_AddApplicantFamily(city, port::load<uint16_t>(globals::eachStrData.get() + 0x15Cu), 1);
        return 0;
    }

    PORT_FN(EvAlien_Slot14);

    // The noodles event's slot 0x14: mode 0 (start) adds the family at +0x1FE of the event data table to the applicants and sets up
    // the event's three actors (the families at +0x1FE members 0 and 1 and +0x16E member 0, no person or building) with 60 frames
    // to wait; mode 1 (end) adds the third actor's family. The result is 0.
    int EvNoodles_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        (void)actor;
        const uint32_t d = data.address;
        if (mode == 1)
        {
            CityScene_AddApplicantFamily(city, port::load<uint16_t>(d + 0x28u), 1);
            return 0;
        }
        if (mode != 0)
            return 0;
        const uint32_t table = globals::eachStrData.get();
        CityScene_AddApplicantFamily(city, port::load<uint16_t>(table + 0x1FEu), 1);
        port::store<uint16_t>(d + 0x18u, port::load<uint16_t>(table + 0x1FEu));
        port::store<uint16_t>(d + 0x1Au, 0);
        port::store<uint16_t>(d + 0x14u, 0x1770);
        port::store<uint16_t>(d + 0x16u, 0x3F80);
        port::store<uint16_t>(d + 0x20u, port::load<uint16_t>(table + 0x1FEu));
        port::store<uint16_t>(d + 0x22u, 1);
        port::store<uint16_t>(d + 0x1Cu, 0x1770);
        port::store<uint16_t>(d + 0x1Eu, 0x3F80);
        port::store<uint16_t>(d + 0x28u, port::load<uint16_t>(globals::eachStrData.get() + 0x16Eu));
        port::store<uint16_t>(d + 0x2Au, 0);
        port::store<uint16_t>(d + 0x24u, 0x1770);
        port::store<uint16_t>(d + 0x26u, 0x3F80);
        port::store<int32_t>(d + 0x64u, 0x3C);
        return 0;
    }

    PORT_FN(EvNoodles_Slot14);

    // An event actor pointed at with the cursor: with the pad focus held elsewhere nothing happens (0). An actor that is a family
    // member (person 6000) starts a family talk (argument 3). Any other actor has the building under its person found on the map
    // by the person's tile; a talk with that building and person starts (mm_CityScene_StartTalk, argument 2), and with none nothing
    // starts. The result is the talk's.
    int EvActor_PointCursor(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor)
    {
        if (globals::padFocusOwner.get() != 0)
            return 0;
        if (actor->person == 0x1770)
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

    // The cave man event's slot 0x14 (jump table 0x1BA060; modes above 8 give 0). Modes 0 to 3 add the family at +0x15E of the event
    // data table (0x1BDA8C) plus the mode to the applicants, play sound 0x17 and make it the event's actor `mode` (member 2; for the
    // fourth member 1): mode 0 also notes the family at +0x30 (+0x32 = 1) and sets the counter (+0x4C) to 1, 1 and 2 count it to 2
    // and 3, 3 moves the event on (+0xA) and clears it. 4 and 8 give the counter. 5 adds the family at +0x38 (the sound too), 7 that
    // of +0x160. 6 asks whether actors 0 and 4 stand at their buildings and neither is doing activity 5.
    int EvCaveMan_Slot14(GuestPtr<EventData> data, GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, int mode)
    {
        (void)actor;
        const uint32_t d = data.address;
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
            const uint32_t family = (static_cast<uint32_t>(mode) + port::load<int16_t>(globals::eachStrData.get() + 0x15Eu)) & 0xFFFFu;
            addFamily(family);
            const uint32_t entry = d + 0xCu + 8u * static_cast<uint32_t>(mode);
            port::store<uint16_t>(entry + 4u, static_cast<uint16_t>(family));
            if (mode < 3)
            {
                port::store<uint16_t>(entry + 6u, 2);
                port::store<int32_t>(d + 0x4Cu, mode + 1);
                if (mode == 0)
                {
                    port::store<uint16_t>(d + 0x30u, static_cast<uint16_t>(family));
                    port::store<uint16_t>(d + 0x32u, 1);
                }
            }
            else
            {
                port::store<uint16_t>(entry + 6u, 1);
                data->var = static_cast<uint8_t>(data->var + 1);
                port::store<int32_t>(d + 0x4Cu, 0);
            }
            return 0;
        }
        case 4:
        case 8:
            return port::load<int32_t>(d + 0x4Cu);
        case 5:
            addFamily(port::load<uint16_t>(d + 0x38u));
            return 0;
        case 6:
        {
            const GuestPtr<ActorRef> first{d + 0xCu}, last{d + 0x2Cu};
            if (Person_IsAtBuilding(city, first, last) == 0 || Person_IsAtBuilding(city, last, last) == 0)
                return 0;
            const GuestPtr<MapPerson> people = city->map->people;
            if ((people + first->person)->activity == 5)
                return 0;
            return (people + last->person)->activity != 5 ? 1 : 0;
        }
        case 7:
            addFamily(port::load<uint16_t>(globals::eachStrData.get() + 0x160u));
            return 0;
        default:
            return 0;
        }
    }

    PORT_FN(EvCaveMan_Slot14);

    // The escort event (phase in +0xA; the counter at +0x14 wraps at ten). Phase 1: before 22:00 the escort is met once the player is
    // within one tile of the actor's building and a tile of the person (then phase 4); failing that, on every tenth call, an actor
    // on open tile more than three tiles away is pointed at. From 22:00 (an actor off open tiles only from 22:10) pointing at the actor
    // moves it to phase 2. Phase 2: before 23:00 the same meeting gives phase 5; from 23:00 (23:10 for an actor off open tiles) a
    // point at the actor stops its following, moves it to its own spot, marks the person (flag 0x80) and moves on to phase 3. A
    // meeting also stops the following and moves the actor to its spot.
    void EvEscort_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const GuestPtr<ActorRef> actor = GuestPtr<ActorRef>::of(data->actor);
        bool met = false;
        data->unk_14 = data->unk_14 + 1;
        if (data->unk_14 >= 10)
            data->unk_14 = 0;
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
                else if (data->unk_14 == 0 && Person_IsOnOpenTile(city, actor) != 0 && !(Player_DistanceToPerson(city, actor) <= ps2f(3.0f)))
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

    // The shopping together event (phase in +0xA; actor 0 at +0xC, actor 1 at +0x14, the countdown at +0x1C, the counters at +0x20 and
    // +0x24, the outcome at +0x28, the frame count at +0x2C). Phase 1 counts the countdown down: above 60 the player must be near
    // actor 1's building and actor 0 (then actor 0 is pointed at, on an open tile or after ten tries; the outcome is 1 for a
    // countdown under 121, else 0, and phase 5), else every tenth call actor 0 is pointed at; from 60 down, pointing at actor 0 on an
    // open tile moves it to phase 2. Phase 3 does the same while the countdown runs (outcome 2, phase 5) and when it ends points at
    // actor 0, has it stop following and walk to actor 1, marks it (flag 0x80, outcome 3) and moves on to phase 4. Phase 6 ends the
    // event after 180 frames.
    void EvShoppingTogether_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const uint32_t d = data.address;
        const GuestPtr<ActorRef> first{d + 0xCu}, second{d + 0x14u};
        const auto get = [&](uint32_t offset) { return port::load<int32_t>(d + offset); };
        const auto put = [&](uint32_t offset, int32_t value) { port::store<int32_t>(d + offset, value); };
        // Too far from actor 0 (three tiles or more): every tenth call it is pointed at while on an open tile.
        const auto farOff = [&] {
            if (Player_DistanceToPerson(city, first) < ps2f(3.0f))
            {
                put(0x20, 0);
                put(0x24, 0);
                return;
            }
            put(0x24, get(0x24) + 1);
            if (get(0x24) >= 10 && Person_IsOnOpenTile(city, first) != 0 && EvActor_PointCursor(city, first) != 0)
                put(0x24, 0);
            put(0x20, 0);
        };
        const auto nearBoth = [&] {
            return Player_DistanceToBuilding(city, second) < 2 && Player_DistanceToPerson(city, first) <= ps2f(1.0f);
        };
        // Near both: pointed at once the actor is on an open tile, or after ten calls with it off one.
        const auto meet = [&](int32_t outcome) {
            if (Person_IsOnOpenTile(city, first) == 0)
            {
                put(0x20, get(0x20) + 1);
                if (get(0x20) < 10)
                {
                    put(0x24, 0);
                    return;
                }
            }
            if (EvActor_PointCursor(city, first) == 0)
            {
                put(0x24, 0);
                return;
            }
            put(0x28, outcome >= 0 ? outcome : (get(0x1C) < 0x79 ? 1 : 0));
            data->var = 5;
            put(0x24, 0);
        };
        switch (data->var)
        {
        case 1:
            put(0x1C, get(0x1C) - 1);
            if (get(0x1C) < 0x3D)
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
            put(0x1C, get(0x1C) - 1);
            if (get(0x1C) > 0)
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
                put(0x28, 3);
                data->var = static_cast<uint8_t>(data->var + 1);
            }
            break;
        case 6:
            put(0x2C, get(0x2C) + 1);
            if (get(0x2C) >= 0xB4)
                Event_Finish(city->events, city, 0);
            break;
        default:
            break;
        }
    }

    PORT_FN(EvShoppingTogether_Update);

    // The alien event's phases (phase in +0xA; actors at +0xC, +0x14, +0x1C (the place), +0x2C, +0x34; the day to wait for at +0x3C,
    // the time at +0x40 and a countdown at +0x44). 1 plays sound 0x45 and notes the next day; 2 moves on when it comes; 3 from 10:00
    // tells actor 1 and waits 60 frames; 4, 10 and 12 count the 60 down and go back a phase. 7 needs actor 0 on an open tile within a
    // tile of the player and a building picked for its tile (EvAlien_PickTarget): the place becomes that building (marked 0x200, the
    // old one's mark cleared). 9 and 11 tell the place's actor on the noted day and time (noon of the next day after 9). 22 points at
    // actor 0 within a tile of the player once it stands on the footprint of actor 3's building, 24 points at actor 5 and 26 ends the
    // event after 720 frames.
    void EvAlien_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const uint32_t d = data.address;
        const GuestPtr<ActorRef> first{d + 0xCu};
        const auto get = [&](uint32_t offset) { return port::load<int32_t>(d + offset); };
        const auto put = [&](uint32_t offset, int32_t value) { port::store<int32_t>(d + offset, value); };
        const auto advance = [&] { data->var = static_cast<uint8_t>(data->var + 1); };
        switch (data->var)
        {
        case 1:
            Sound_PlaySe(0x45, 0, 0x3C, 0.0f, 0.0f);
            advance();
            put(0x3C, city->monthCount + 1);
            break;
        case 2:
            if (get(0x3C) < city->monthCount)
                data->var = 3;
            break;
        case 3:
            if (city->clock >= 0x8CA0 && EvActor_Notify(city, GuestPtr<ActorRef>{d + 0x14u}) != 0)
            {
                advance();
                put(0x44, 0x3C);
            }
            break;
        case 4:
        case 10:
        case 12:
            put(0x44, get(0x44) - 1);
            if (get(0x44) <= 0)
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
            put(0x3C, city->monthCount + 1);
            put(0x40, 0x8CA0);
            const GuestPtr<ActorRef> place{d + 0x1Cu};
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
            if (city->monthCount < get(0x3C) || city->clock < get(0x40))
                break;
            if (EvActor_Notify(city, GuestPtr<ActorRef>{d + 0x1Cu}) != 0)
            {
                advance();
                put(0x44, 0x3C);
                put(0x3C, city->monthCount + 1);
                put(0x40, 0xA8C0);
            }
            break;
        case 11:
            if (city->monthCount < get(0x3C) || city->clock < get(0x40))
                break;
            if (EvActor_Notify(city, GuestPtr<ActorRef>{d + 0x2Cu}) != 0)
            {
                advance();
                put(0x44, 0x3C);
            }
            break;
        case 22:
        {
            if (!(Player_DistanceToPerson(city, first) <= ps2f(1.0f)))
                break;
            port::GuestFrame<0x60> frame;
            const GuestPtr<uint32_t> rect{frame.ptr().address + 0x50u};
            Building_GetFootprint(Map_GetRecord(city->map, port::load<uint16_t>(d + 0x26u)), 1, rect);
            const GuestPtr<MapPerson> person = city->map->people + first->person;
            const int z = sdk::fptosi(ps2f(64.0f) - person->z);
            const int x = sdk::fptosi(ps2f(64.0f) + person->x);
            const int32_t x0 = port::load<int32_t>(rect.address), z0 = port::load<int32_t>(rect.address + 4u);
            const int32_t x1 = port::load<int32_t>(rect.address + 8u), z1 = port::load<int32_t>(rect.address + 0xCu);
            if (x >= x0 && x < x1 && z >= z0 && z < z1 && EvActor_PointCursor(city, first) != 0)
                advance();
            break;
        }
        case 24:
            if (EvActor_PointCursor(city, GuestPtr<ActorRef>{d + 0x34u}) != 0)
            {
                advance();
                put(0x44, 0);
            }
            break;
        case 26:
            put(0x44, get(0x44) + 1);
            if (get(0x44) >= 0x2D0)
                Event_Finish(city->events, city, 0);
            break;
        default:
            break;
        }
    }

    PORT_FN(EvAlien_Update);

    // The cave man event's phases (phase in +0xA; actors at +0xC, +0x14, +0x1C, +0x24, +0x2C (the leader), +0x34, +0x3C and +0x44;
    // the counter at +0x4C, a countdown at +0x50 and a day at +0x54). 2 marks the first four buildings of type 0xB0 (flag 0x200),
    // giving their families' actors the building, and moves on once four are found (and again for each further one); 3 finds the
    // person entries of the actors one by one (the leader's from +0x30/+0x32) and notes the day; 4 and 10 wait for a day after the
    // noted one between 10:00 and 17:00 with the leader at its building and tell actor 4 or 3 (60 frames); 5 and 11 send them off and
    // count the 60 down back to the phase before; 6 to 9, 20 and 21 keep actors 0 and 4 where they are; 18 finds actor 5's building
    // or person entry; 24 sends actor 0 to actor 6 and marks it, 25 does so with the activity 10 and points at it (on an open tile
    // after ten tries, three tiles or more from the player; else once it is at actor 6's building: the talk's view lit and phase 27);
    // 26 releases it and points at it within a tile; 28 points at actor 7; 30 ends the event after 720 frames.
    void EvCaveMan_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const uint32_t d = data.address;
        const GuestPtr<ActorRef> first{d + 0xCu}, third{d + 0x24u}, fourth{d + 0x2Cu}, sixth{d + 0x3Cu};
        const auto get = [&](uint32_t offset) { return port::load<int32_t>(d + offset); };
        const auto put = [&](uint32_t offset, int32_t value) { port::store<int32_t>(d + offset, value); };
        const auto advance = [&](int step) { data->var = static_cast<uint8_t>(data->var + step); };
        // A day after the noted one, between 10:00 and 17:00.
        const auto timeToTell = [&] { return get(0x54) < city->monthCount && city->clock >= 0x8CA0 && city->clock < 0xEF10; };
        const auto person = [&](GuestPtr<ActorRef> actor) { return city->map->people + actor->person; };
        switch (data->var)
        {
        case 2:
        {
            const uint32_t begin = Map_GetRecord(city->map, 0).address;
            int found = 0;
            for (uint32_t record = begin; record < begin + 0x6F200u; record += 0x1Cu)
            {
                if (port::load<uint16_t>(record) != 0xB0)
                    continue;
                const uint32_t flags = port::load<uint16_t>(record + 6u);
                if ((flags & 0x200u) == 0)
                {
                    port::store<uint16_t>(record + 6u, static_cast<uint16_t>(flags | 0x200u));
                    const uint32_t actor = d + 0xCu + 8u * port::load<uint16_t>(record + 0xCu) - 8u * port::load<uint16_t>(d + 0x10u);
                    port::store<uint16_t>(actor + 2u, port::load<uint16_t>(record + 2u));
                }
                ++found;
                if (found >= 4)
                    advance(1);
            }
            break;
        }
        case 3:
        {
            const uint32_t index = static_cast<uint32_t>(get(0x4C));
            const uint32_t actor = d + 0xCu + 8u * index;
            const GuestPtr<MapPerson> entry = Map_FindPersonEntry(city->map, static_cast<int16_t>(port::load<uint16_t>(d + 8u * index + 0x10u)),
                                                                  port::load<uint16_t>(d + 8u * index + 0x12u));
            if (!entry)
                break;
            port::store<uint16_t>(actor, entry->index);
            if (get(0x4C) == 0)
            {
                port::store<uint16_t>(d + 0x2Eu, port::load<uint16_t>(actor + 2u));
                port::store<uint16_t>(d + 0x2Cu, Map_FindPersonEntry(city->map, static_cast<int16_t>(port::load<uint16_t>(d + 0x30u)),
                                                                     port::load<uint16_t>(d + 0x32u))->index);
            }
            put(0x4C, get(0x4C) + 1);
            if (get(0x4C) >= 4)
            {
                put(0x4C, 0);
                advance(1);
                put(0x54, city->monthCount);
            }
            break;
        }
        case 4:
            if (timeToTell() && Person_IsAtBuilding(city, fourth, fourth) != 0 && EvActor_Notify(city, fourth) != 0)
            {
                put(0x50, 0x3C);
                advance(1);
            }
            break;
        case 5:
            EvActor_MoveTo(city, fourth, fourth, 0);
            EvActor_MoveTo(city, first, fourth, 0);
            put(0x50, get(0x50) - 1);
            if (get(0x50) <= 0)
                advance(-1);
            break;
        case 6:
        case 7:
        case 8:
        case 9:
            EvActor_MoveTo(city, first, first, 0);
            EvActor_MoveTo(city, fourth, fourth, 0);
            put(0x54, city->monthCount);
            break;
        case 10:
            if (timeToTell() && Person_IsAtBuilding(city, fourth, fourth) != 0 && EvActor_Notify(city, third) != 0)
            {
                put(0x50, 0x3C);
                advance(1);
            }
            break;
        case 11:
            EvActor_MoveTo(city, third, third, 0);
            put(0x50, get(0x50) - 1);
            if (get(0x50) <= 0)
                advance(-1);
            break;
        case 18:
        {
            const GuestPtr<ActorRef> fifth{d + 0x34u};
            if (fifth->object == 0x3F80)
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
                put(0x50, get(0x50) + 1);
                if (get(0x50) < 10 || EvActor_PointCursor(city, first) == 0)
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
                put(0x50, 0);
                advance(-1);
            }
            break;
        case 28:
            if (EvActor_PointCursor(city, GuestPtr<ActorRef>{d + 0x44u}) != 0)
            {
                advance(1);
                put(0x50, 0);
            }
            break;
        case 30:
            put(0x50, get(0x50) + 1);
            if (get(0x50) >= 0x2D0)
            {
                Event_Finish(city->events, city, 0);
                break;
            }
            if (get(0x4C) != 0)
                break;
            if (Map_FindPersonEntry(city->map, static_cast<int16_t>(port::load<uint16_t>(d + 0x48u)), port::load<uint16_t>(d + 0x4Au)))
                put(0x4C, get(0x4C) + 1);
            break;
        default:
            break;
        }
    }

    PORT_FN(EvCaveMan_Update);

    // The missing children event's phases (jump table 0x1BA800; phase in +0xA; actor 0 at +0xC (the parent), actor 1 at +0x14 (the
    // child), the counter at +0x24, the outcome at +0x28 and a frame count at +0x2C). 1 keeps the parent where it is and the child
    // wandering; from 13:00 to 15:00, if a shop is found for the parent, it is told (60 frames), else the event ends. 2 and 6 count
    // the 60 down back a phase; 3 lets the child wander until 18:00; 4 waits for the child to be at its building; 5 tells the parent; 7
    // is the search: before 20:00 the parent stays and the child is pointed at on open tiles three tiles or more away every tenth
    // call, and within three tiles of the player and four of its building it is stopped and followed by the parent (outcome 2); from
    // 20:00 the parent follows the child at once (phase 12). 8 releases the child and, near it, points at the parent and has it
    // follow the child; 10 releases and points at the child; 12 does the same near both (outcome 1); 14 puts the parent inside the
    // child's building; 16 ends the event after 180 frames.
    void EvMissingChildren_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const uint32_t d = data.address;
        const GuestPtr<ActorRef> first{d + 0xCu}, second{d + 0x14u};
        const auto get = [&](uint32_t offset) { return port::load<int32_t>(d + offset); };
        const auto put = [&](uint32_t offset, int32_t value) { port::store<int32_t>(d + offset, value); };
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
                    put(0x24, 0x3C);
                }
                break;
            }
            Event_Finish(city->events, city, 0);
            break;
        case 2:
            EvActor_MoveTo(city, first, first, 0);
            EvActor_Wander(city, second);
            put(0x24, get(0x24) - 1);
            if (get(0x24) <= 0)
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
                put(0x24, 0x3C);
                advance(1);
            }
            break;
        case 6:
            EvActor_MoveTo(city, first, first, 0);
            put(0x24, get(0x24) - 1);
            if (get(0x24) <= 0)
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
                    put(0x28, 2);
                    advance(1);
                }
                put(0x24, 0);
                break;
            }
            put(0x24, get(0x24) + 1);
            if (get(0x24) < 10 || Person_IsOnOpenTile(city, second) == 0)
                break;
            EvActor_PointCursor(city, second);
            put(0x24, 0);
            break;
        case 8:
        {
            EvActor_Release(city, second);
            if (!(Person_Distance(city, first, second) <= ps2f(1.0f)))
            {
                put(0x24, 0);
                break;
            }
            if (Person_IsOnOpenTile(city, first) == 0)
            {
                put(0x24, get(0x24) + 1);
                if (get(0x24) < 5)
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
            put(0x24, get(0x24) + 1);
            if (get(0x24) < 10)
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
                put(0x28, 1);
            }
            put(0x24, 0);
            break;
        case 14:
            if (EvActor_PointCursor(city, second) != 0)
            {
                person(first)->inside = first->object;
                advance(1);
            }
            break;
        case 16:
            put(0x2C, get(0x2C) + 1);
            if (get(0x2C) >= 0xB4)
                Event_Finish(city->events, city, 0);
            break;
        default:
            break;
        }
    }

    PORT_FN(EvMissingChildren_Update);

    // The farewell party's phases (jump table 0x1BA6E0; phase in +0xA; actor 1 at +0x14, the countdown at +0x24, the time to wait to
    // at +0x2C and the step of the staging at +0x30 and +0x34). 1 tells actor 1 between 6:00 and 23:00 (60 frames, phase 2); 2 counts
    // the 60 down back to phase 1. 3 stages the party by +0x30: 0 fades out (and sets the two voice levels at 0x1BDD14/18 to 100), 1
    // (when the fade is done) moves the clock to 10:00 (a day on when it was later), brings the people home, pushes the party mode
    // (12), makes the party's overlay (an 8-byte object with a frame of 0xC0 bytes at +0x80 of the city) and starts the party sound,
    // 2 fades back in with the music on, 3 (done) closes the overlay and goes to phase 4. 4 until the time: finds the people on the
    // tile under the player when it is a type 4 building, and points at the first friend (friendship band 4 or more, class flag 1
    // clear) within a tile of the player that is not already the one chosen (actor 2); after it phase 5. 5 points at actor 0 or 1 (by
    // 0x1C5548) at night, tells them by day and waits 60 frames; 6 counts them down to phase 5. 7 stages the end by +0x34 and finishes
    // the event. The pending approval is limited so the approval stays within 100 on every call.
    void EvFarewellParty_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        port::GuestFrame<0x90> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t d = data.address;
        constexpr uint32_t kVoiceA = 0x001BDD14u, kVoiceB = 0x001BDD18u, kPartyFlag = 0x001BDD24u;
        const auto get = [&](uint32_t offset) { return port::load<int32_t>(d + offset); };
        const auto put = [&](uint32_t offset, int32_t value) { port::store<int32_t>(d + offset, value); };
        const auto setVoices = [&]() {
            port::store<uint8_t>(kVoiceA, 100);
            port::store<uint8_t>(kVoiceB, 100);
        };
        switch (data->var)
        {
        case 1:
            if (city->clock >= 0x5460 && city->clock < 0x14370 && EvActor_Notify(city, GuestPtr<ActorRef>{d + 0x14u}) != 0)
            {
                data->var = 2;
                put(0x24, 0x3C);
            }
            break;
        case 2:
            put(0x24, get(0x24) - 1);
            if (get(0x24) <= 0)
                data->var = static_cast<uint8_t>(data->var - 1);
            break;
        case 3:
            switch (get(0x30))
            {
            case 0:
                CityScene_StartFade(city, 1);
                put(0x30, get(0x30) + 1);
                setVoices();
                break;
            case 1:
            {
                if (CityScene_GetFade(city) != 0)
                    break;
                if (0x8CA0 < city->clock)
                    city->monthCount = city->monthCount + 1;
                city->clock = 0x8CA0;
                Map_ReturnPeopleToWork(city->map);
                CityScene_PushMode(city, 0xC);
                uint32_t overlay = sdk::operator_new(8u);
                if (overlay != 0u)
                {
                    port::store<uint32_t>(overlay, 0x001BD8B0u);
                    port::store<uint32_t>(overlay, 0x001BD8E0u);
                    uint32_t window = sdk::operator_new(0xC0u);
                    if (window != 0u)
                        window = Frame_ctor(GuestPtr<Frame>{window}, 0x14, 0x21C, 0xC0, 0x18, 0xC, 0xFFFFFFFFu).address;
                    port::store<uint32_t>(overlay + 4u, window);
                }
                port::store<uint32_t>(city.address + 0x80u, overlay);
                city->flags = city->flags | 8u;
                put(0x30, get(0x30) + 1);
                Se_SetVoice15Enabled(0, 1);
                Se_SetEnabled(1, 1);
                port::store<int32_t>(kPartyFlag, 0);
                break;
            }
            case 2:
                CityScene_StartFade(city, 2);
                put(0x30, get(0x30) + 1);
                Bgm_SetActive(1);
                break;
            case 3:
            {
                if (CityScene_GetFade(city) != 0)
                    break;
                Pad_ReleaseFocus(4);
                const uint32_t overlay = port::load<uint32_t>(city.address + 0x80u);
                if (overlay != 0u)
                {
                    port::callGuest<int64_t>(port::load<uint32_t>(port::load<uint32_t>(overlay) + 8u), overlay, 1);
                    port::store<uint32_t>(city.address + 0x80u, 0u);
                }
                data->var = 4;
                port::store<int32_t>(kPartyFlag, 1);
                break;
            }
            default:
                break;
            }
            break;
        case 4:
        {
            if (!(city->clock < get(0x2C)))
            {
                data->var = 5;
                break;
            }
            Player_GetMatrix(city->player, GuestPtr<Vec4>{sp + 0x40u});
            const int z = sdk::fptosi(ps2f(64.0f) - ps2f(std::bit_cast<float>(port::load<uint32_t>(sp + 0x78u))));
            const int x = sdk::fptosi(ps2f(64.0f) + ps2f(std::bit_cast<float>(port::load<uint32_t>(sp + 0x70u))));
            const GuestPtr<MapObject> under = Map_GetTileObject(city->map, x, z);
            if (under->type != 4)
                break;
            port::store<uint16_t>(sp + 0x8Eu, port::load<uint16_t>(under.address + 8u));
            const GuestPtr<ActorRef> chosen{d + 0x1Cu};
            while (const GuestPtr<MapPerson> candidate = People_Next(city->map, GuestPtr<uint16_t>{sp + 0x8Eu}))
            {
                const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * candidate->charType)};
                if ((cls->flags & 1u) != 0 || Family_GetFriendshipBand(candidate->family, candidate->member) < 4)
                    continue;
                if (!(Player_DistanceToPoint(city, GuestPtr<ps2f>{candidate.address}) <= ps2f(1.0f)) || chosen->person == candidate->index)
                    continue;
                chosen->person = candidate->index;
                chosen->object = candidate->work;
                chosen->family = candidate->family;
                chosen->member = candidate->member;
                if (EvActor_PointCursor(city, chosen) != 0)
                    break;
            }
            break;
        }
        case 5:
        {
            const uint32_t which = port::load<int32_t>(0x001C5548u) == 4 ? 0u : 1u;
            const GuestPtr<ActorRef> actor{d + 0xCu + 8u * which};
            const bool night = city->clock >= 0x13560 || city->clock < 0x5460;
            if ((night ? EvActor_PointCursor(city, actor) : EvActor_Notify(city, actor)) != 0)
            {
                data->var = 6;
                put(0x24, 0x3C);
            }
            break;
        }
        case 6:
            put(0x24, get(0x24) - 1);
            if (get(0x24) <= 0)
                data->var = 5;
            break;
        case 7:
            if (get(0x34) == 0)
            {
                CityScene_StartFade(city, 1);
                port::store<uint32_t>(city.address + 0x34u, 0u);
                put(0x34, get(0x34) + 1);
                setVoices();
            }
            else if (get(0x34) == 1)
            {
                if (CityScene_GetFade(city) != 0)
                    break;
                Pad_ReleaseFocus(4);
                CityScene_PushMode(city, 0xC);
                city->flags = city->flags | 0x10u;
                city->flags = city->flags & ~8u;
                Se_SetEnabled(1, 1);
                Se_SetVoice15Enabled(0, 1);
                port::store<int32_t>(kPartyFlag, 0);
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

    // The thief event's phases (jump table 0x1BA8E0; phase in +0xA; actor 0 at +0xC (the player's contact), 1 at +0x14 (the thief),
    // 2 at +0x1C, 3 at +0x24 (the place); the day to wait for at +0x2C, countdowns at +0x34 and +0x38, a counter at +0x3C and the
    // frames at +0x40). 1 waits for the day, then picks a building for the place (a free one of category 5, else 1, fitting the whole
    // map rectangle at 0x1BA8D0; no repeat of the first actor's building), 60 frames at a time, and marks it (0x200); from 17:00 the
    // place is given up (unmarked, claim timer 1, flag 0x40) and the day set a day on; from 10:00 actor 3 is told and phase 2 follows.
    // 2 and 11 count 60 frames down back a phase; 3 points at actor 0 (within a tile after 180 frames, else every tenth call when three
    // tiles or more away); 5 and 10 tell actor 0 or 1 in daytime or after the countdown; 6 to 8 keep actor 0 in place; 9 starts 180
    // frames; 12 to 14 and 18 to 29 move the actors around (13 brings actor 1 to actor 0 between 20:00 and 21:00, 14 starts the chase
    // with sound 0x46 and the thief's schedule 14 and activity 5, 18 gets actor 2 to the thief, 19 points at the building, 22 and 23
    // wait for the place, 28 starts 720 frames and 29 ends the event).
    void EvThief_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const uint32_t d = data.address;
        const GuestPtr<ActorRef> first{d + 0xCu}, second{d + 0x14u}, third{d + 0x1Cu}, place{d + 0x24u};
        const auto get = [&](uint32_t offset) { return port::load<int32_t>(d + offset); };
        const auto put = [&](uint32_t offset, int32_t value) { port::store<int32_t>(d + offset, value); };
        const auto advance = [&](int step) { data->var = static_cast<uint8_t>(data->var + step); };
        const auto stay = [&](GuestPtr<ActorRef> actor) { EvActor_MoveTo(city, actor, actor, 0); };
        constexpr uint32_t kWholeMap = 0x001BA8D0u;
        switch (data->var)
        {
        case 1:
        {
            stay(first);
            if (city->monthCount < get(0x2C))
                break;
            if (place->object != 0x3F80)
            {
                if (city->clock < 0xEF10)
                {
                    if (city->clock >= 0x8CA0 && EvActor_Notify(city, place) != 0)
                    {
                        advance(1);
                        put(0x34, 0x3C);
                    }
                    break;
                }
                const GuestPtr<MapObject> given = Map_GetRecord(city->map, place->object);
                given->flags = static_cast<uint16_t>(given->flags & 0xFDFFu);
                given->claimTimer = 1;
                given->flags = static_cast<uint16_t>(given->flags | 0x40u);
                place->person = 0x1770;
                place->object = 0x3F80;
                put(0x3C, get(0x3C) + 1);
                put(0x2C, city->monthCount + 1);
                break;
            }
            put(0x38, get(0x38) - 1);
            if (get(0x38) > 0)
                break;
            GuestPtr<MapObject> found = EvThief_FindBuilding(city, 5, GuestPtr<int32_t>{kWholeMap});
            if (!found)
                found = EvThief_FindBuilding(city, 1, GuestPtr<int32_t>{kWholeMap});
            if (!found || found->id == first->object)
            {
                put(0x38, 0x3C);
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
            put(0x34, get(0x34) - 1);
            if (get(0x34) <= 0)
                advance(-1);
            break;
        case 3:
            put(0x40, get(0x40) + 1);
            if (get(0x40) >= 0xB4)
            {
                if (Player_DistanceToPerson(city, first) <= ps2f(1.0f) && EvActor_PointCursor(city, first) != 0)
                {
                    advance(1);
                    put(0x2C, city->monthCount + 1);
                    put(0x40, 0);
                }
                break;
            }
            if (get(0x40) % 10 != 0 || Player_DistanceToPerson(city, first) < ps2f(3.0f))
                break;
            EvActor_PointCursor(city, first);
            break;
        case 5:
            stay(first);
            if (city->monthCount >= get(0x2C) && city->clock >= 0x8CA0 && city->clock < 0xEF10 && EvActor_Notify(city, first) != 0)
            {
                advance(1);
                put(0x34, 0x3C);
            }
            break;
        case 6:
            put(0x34, get(0x34) - 1);
            if (get(0x34) <= 0)
                advance(-1);
            stay(first);
            break;
        case 7:
        case 8:
            stay(first);
            break;
        case 9:
            put(0x38, 0xB4);
            advance(1);
            stay(first);
            break;
        case 10:
            stay(first);
            put(0x38, get(0x38) - 1);
            if (get(0x38) > 0)
                break;
            if (EvActor_Notify(city, second) != 0)
            {
                advance(1);
                put(0x34, 0x3C);
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
            port::store<uint8_t>(thief.address + 0x26u, 0xE);
            thief->activity = 5;
            EvActor_MoveTo(city, second, second, 1);
            advance(1);
            break;
        }
        case 18:
            put(0x38, get(0x38) + 1);
            if (get(0x38) >= 10)
            {
                if (Player_DistanceToBuilding(city, first) < 2 && Player_DistanceToPerson(city, third) <= ps2f(1.0f) &&
                    EvActor_PointCursor(city, third) != 0)
                {
                    EvActor_MoveTo(city, third, first, 1);
                    advance(1);
                    put(0x38, 0);
                    break;
                }
                if (!(Player_DistanceToPerson(city, third) < ps2f(3.0f)))
                    EvActor_PointCursor(city, third);
                put(0x38, 0);
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
                    put(0x34, 0x3C);
                }
                break;
            }
            stay(second);
            stay(third);
            break;
        case 24:
            put(0x34, get(0x34) - 1);
            if (get(0x34) <= 0)
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
            put(0x38, 0x2D0);
            break;
        case 29:
            put(0x38, get(0x38) - 1);
            if (get(0x38) <= 0)
                Event_Finish(city->events, city, 0);
            break;
        default:
            break;
        }
    }

    PORT_FN(EvThief_Update);

    // Event type 13: the story is a state machine on `var` (+0xA) over three to four actors: the first (+0x14), second (+0x1C) and third
    // (+0x24), and three more (+0x2C, +0x34, +0x3C) each with a partner 0x18 on (+0x44, +0x4C, +0x54) that go through the same steps one
    // after the other (phases 4 to 9 and 0xC to 0x1C in threes: the group picks its actor by the phase). The timer is at +0x64, the
    // day to wait for at +0x5C and the time of day at +0x60. State 1 finds the first actor's home and the people; 3 chooses which
    // group goes on; 4/0xD/0x16 wait for the day and 10:00 then have the actor walk to its partner and phone; 5/0xE/0x17 wait out the
    // timer; 6/0xF/0x18 send the actor home; 7/0x10/0x19 and 9/0x12/0x1B bring the player near; 0xA/0x13/0x1C walk the actor and the
    // first one to the first one's building; 0xC, 0x15 and 0x1E move on once the actor is on the building; 0x1F to 0x58 are the
    // scenes with the third actor (following, waiting at a building, the shop); 0x61 ends the event. From phase 0x20 up to 0x57 a
    // building with people inside is emptied to 20 at 2:00 (clock / 60 == 0x78).
    void EvNoodles_Update(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        const uint32_t d = data.address;
        const GuestPtr<CityMap> map = city->map;
        const auto actor = [&](uint32_t offset) { return GuestPtr<ActorRef>{d + offset}; };
        const auto partner = [&](GuestPtr<ActorRef> who) { return GuestPtr<ActorRef>{who.address + 0x18u}; };
        const auto get = [&](uint32_t offset) { return port::load<int32_t>(d + offset); };
        const auto put = [&](uint32_t offset, int32_t value) { port::store<int32_t>(d + offset, value); };
        const auto advance = [&](int step = 1) { data->var = static_cast<uint8_t>(data->var + step); };
        const auto record = [&](GuestPtr<ActorRef> who) { return map->people + who->person; };
        const auto address = [](GuestPtr<ActorRef> who) { return static_cast<int>(who.address); };
        // The actor of a group of three phases (a, middle, c): the first group member below the middle, the second at it, the third above.
        const auto byPhase = [&](int middle) { return actor(data->var < middle ? 0x2Cu : (data->var > middle ? 0x3Cu : 0x34u)); };
        const GuestPtr<ActorRef> first = actor(0x14), second = actor(0x1C), third = actor(0x24);
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
            put(0x64, get(0x64) - 1);
            if (get(0x64) >= 1)
                break;
            const GuestPtr<MapPerson> entry = Map_FindPersonEntry(map, static_cast<int16_t>(first->family), first->member);
            if (!entry)
            {
                put(0x64, 0x3C);
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
            if (actor(0x2C)->person == kNoPerson)
                data->var = actor(0x34)->person == kNoPerson ? 0x16 : 0xD;
            else
                data->var = 4;
            put(0x5C, city->monthCount);
            break;
        case 4:
        case 0xD:
        case 0x16:
            if (get(0x5C) < city->monthCount && city->clock > 0x707F)
            {
                const GuestPtr<ActorRef> who = byPhase(0xD);
                EvActor_MoveTo(city, who, partner(who), 0);
                if (city->clock >= 36000 && Person_IsAtBuilding(city, who, partner(who)) != 0 && EvActor_Notify(city, who) != 0)
                {
                    advance();
                    put(0x64, 0x3C);
                }
            }
            break;
        case 5:
        case 0xE:
        case 0x17:
        {
            const GuestPtr<ActorRef> who = byPhase(0xE);
            EvActor_MoveTo(city, who, partner(who), 0);
            put(0x64, get(0x64) - 1);
            if (get(0x64) < 1)
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
                put(0x64, get(0x64) + 1);
                if (get(0x64) >= 10 && EvActor_PointCursor(city, who) != 0)
                    put(0x64, 0);
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
            const GuestPtr<ActorRef> who = actor(0x2C);
            EvActor_MoveTo(city, who, partner(who), 0);
            if (standing(who) || Person_IsNearBuilding(city, record(who), building, 6) != 0)
            {
                if (actor(0x34)->person != kNoPerson)
                {
                    put(0x5C, city->monthCount);
                    data->var = 0xD;
                }
                else if (actor(0x3C)->person != kNoPerson)
                {
                    put(0x5C, city->monthCount);
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
            const GuestPtr<ActorRef> who = actor(0x34);
            EvActor_MoveTo(city, who, partner(who), 0);
            if (standing(who) || Person_IsNearBuilding(city, record(who), building, 6) != 0)
            {
                if (actor(0x3C)->person != kNoPerson)
                {
                    put(0x5C, city->monthCount);
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
            const GuestPtr<ActorRef> who = actor(0x3C);
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
            put(0x64, 10);
            advance();
            break;
        case 0x25:
            EvActor_Wander(city, second);
            put(0x64, get(0x64) - 1);
            if (get(0x64) < 1 && EvActor_Notify(city, third) != 0)
            {
                advance();
                put(0x64, 0x3C);
            }
            break;
        case 0x26:
        case 0x2A:
            EvActor_Wander(city, second);
            put(0x64, get(0x64) - 1);
            if (get(0x64) < 1)
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
            put(0x64, get(0x64) - 1);
            if (get(0x64) >= 1)
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
                put(0x64, 0x3C);
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
                    put(0x64, get(0x64) + 1);
                    if (get(0x64) >= 10)
                    {
                        if (!(Player_DistanceToPerson(city, third) < ps2f(3.0f)))
                            EvActor_PointCursor(city, third);
                        put(0x64, 0);
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
                put(0x64, 0);
            }
            else
            {
                put(0x64, get(0x64) + 1);
                if (get(0x64) >= 10)
                {
                    if (!(Player_DistanceToPerson(city, third) < ps2f(3.0f)) && Person_IsOnOpenTile(city, third) != 0)
                        EvActor_PointCursor(city, third);
                    put(0x64, 0);
                }
            }
            shelter();
            break;
        case 0x3A:
            EvActor_Release(city, second);
            if (Person_Distance(city, second, third) <= ps2f(1.0f))
            {
                put(0x64, get(0x64) + 1);
                if (get(0x64) >= 4 && Player_DistanceToPerson(city, second) <= ps2f(1.0f) && EvActor_PointCursor(city, second) != 0)
                    advance();
            }
            else
                put(0x64, 0);
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
                put(0x5C, city->monthCount + 1);
                put(0x60, 36000);
            }
            break;
        case 0x4E:
            EvActor_Wander(city, second);
            if (get(0x5C) <= city->monthCount && get(0x60) <= city->clock)
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
                    put(0x64, 0x3C);
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
            put(0x64, get(0x64) - 1);
            if (get(0x64) < 1)
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
                put(0x64, 0x5A0);
            }
            break;
        case 0x61:
            put(0x64, get(0x64) - 1);
            if (get(0x64) < 1)
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
