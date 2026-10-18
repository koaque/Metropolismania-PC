// City events: the handler slots of the event types (table 0x1AF0D0: Check, Start, Update, End, Slot10, Slot14),
// the event manager's talk slots and the waiting queue. Every handler gets the active event (EventData) and the
// city; Check slots say whether the event may start, Start and End mark and clear the buildings taking part
// (flag 0x200) and the wants held for them, Slot10 gives the talk a text and Slot14 a value.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

namespace mm
{
    namespace
    {
        // Actor `index` of an event that lists several (8 bytes each from EventData::actor).
        GuestPtr<ActorRef> actorAt(GuestPtr<EventData> data, int index)
        {
            return GuestPtr<ActorRef>{data.address + 0x0Cu + 8u * static_cast<uint32_t>(index)};
        }

        void markBuilding(GuestPtr<CityScene> city, uint16_t object) { Map_GetRecord(city->map, object)->flags |= 0x200u; }
        void unmarkBuilding(GuestPtr<CityScene> city, uint16_t object) { Map_GetRecord(city->map, object)->flags &= 0xFDFFu; }

        // The buildings of the event's first `count` actors, those that have one.
        void markActorBuildings(GuestPtr<CityScene> city, GuestPtr<EventData> data, int first, int count)
        {
            for (int i = first; i < first + count; ++i)
                if (actorAt(data, i)->object != kNoObject)
                    markBuilding(city, actorAt(data, i)->object);
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

    void EvCaveMan_Start(GuestPtr<EventData>, GuestPtr<CityScene>) {}

    // The message comes from family type entry 0xB1's first member, with no person or building.
    void EvMsg_Start(GuestPtr<EventData> data, GuestPtr<CityScene>)
    {
        data->actor.person = kNoPerson;
        data->actor.object = kNoObject;
        data->actor.family = familyTypeEntry(0xB1);
        data->actor.member = 0;
    }

    void EvMissingChildren_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city) { markBuilding(city, data->actor.object); }
    void EvAdvertise_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city) { markBuilding(city, data->actor.object); }

    // Marks the building and remembers the month it started in.
    void EvUselessMiddleAged_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        markBuilding(city, data->actor.object);
        data->unk_14 = city->monthCount;
    }

    // Marks the building, detaches the actor and keeps their distance from the player.
    void EvEscort_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        markBuilding(city, data->actor.object);
        const GuestPtr<ActorRef> actor = GuestPtr<ActorRef>::of(data->actor);
        EvActor_Detach(city, actor);
        data->unk_18 = Player_DistanceToBuilding(city, actor);
    }

    // Marks both actors' buildings and sets the timer (+0x1C) to 180.
    void EvShoppingTogether_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        markBuilding(city, actorAt(data, 0)->object);
        markBuilding(city, actorAt(data, 1)->object);
        port::store<int32_t>(data.address + 0x1Cu, 180);
    }

    // Marks both actors' buildings; the delivery state (+0x34) starts at 2.
    void EvDeliveryMan_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
        markBuilding(city, actorAt(data, 0)->object);
        markBuilding(city, actorAt(data, 1)->object);
        port::store<int32_t>(data.address + 0x34u, 2);
    }

    void EvShopping_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city) { markActorBuildings(city, data, 0, 5); }

    // The noodle shop event lists its six buildings from +0x2C (actors 4 to 9).
    void EvNoodles_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city) { markActorBuildings(city, data, 4, 6); }

    // Six actors without person or building: the first two from family type entries 0xAB and 0xAC, the others
    // from entry 0xAD and the three after it (members unchanged).
    void EvCrazyCult_Start(GuestPtr<EventData> data, GuestPtr<CityScene>)
    {
        actorAt(data, 0)->family = familyTypeEntry(0xAB);
        actorAt(data, 1)->family = familyTypeEntry(0xAC);
        for (int i = 0; i < 4; ++i)
            actorAt(data, 2 + i)->family = static_cast<uint16_t>(familyTypeEntry(0xAD) + i);
        for (int i = 0; i < 6; ++i)
        {
            actorAt(data, i)->person = kNoPerson;
            actorAt(data, i)->object = kNoObject;
        }
    }

    // The adoption comes from family type entry 0xB1's first member; the applicants (families 0 to 9999 with an
    // application) are listed as words from +0x24, then 24 bytes from 0x1B37D0 are copied to +0x4C.
    void EvAdoption_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
    {
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
        for (uint32_t i = 0; i < 0x18u; i += 4u)
            port::store<uint32_t>(data.address + 0x4Cu + i, port::load<uint32_t>(globals::kAdoptionStart + i));
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
    void EvFarewellParty_Start(GuestPtr<EventData> data, GuestPtr<CityScene> city)
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
        Map_GetRecord(city->map, actorAt(data, 1)->object)->flags |= 0x200u;
        port::store<int32_t>(data.address + 0x2Cu, (port::load<int32_t>(data.address + 0x28u) / 50 + 13) * 3600);
        if (68400 < port::load<int32_t>(data.address + 0x2Cu))
            port::store<int32_t>(data.address + 0x2Cu, 68400);
        port::store<uint16_t>(data.address + 0x1Cu, 0xFFFF);
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
}
