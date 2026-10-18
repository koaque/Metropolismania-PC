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
        PhoneCall_Remove(city->phoneCalls, static_cast<int>(city.address), data->actor.family, data->actor.member);
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
}
