// Families: per-family flags kept as bitsets over all family ids.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

namespace mm
{
    namespace
    {
        // Bit `family` of a bitset. The original divides with C's signed rules, so a negative id would
        // reach the word before the bitset; ids are never negative in play, but the port keeps that.
        int familyBit(uint32_t bitset, uint32_t family)
        {
            const int32_t id = static_cast<int32_t>(family);
            const uint32_t word = port::load<uint32_t>(bitset + static_cast<uint32_t>(4 * (id / 32)));
            return (word >> ((id % 32) & 31)) & 1u;
        }
    }

    // Friendship meter of a member (bits 0-6 of the member byte), capped at 120.
    uint8_t Family_GetFriendship(int family, int member)
    {
        const uint8_t meter = port::load<uint8_t>(globals::kMemberBytes + static_cast<uint32_t>(family * 6 + member)) & 0x7Fu;
        return meter > 120 ? 120 : meter;
    }

    // Band 0..4 of the friendship meter: 0, below 24, below 48, below 100, 100 and up.
    int Family_GetFriendshipBand(int family, int member)
    {
        const uint8_t meter = Family_GetFriendship(family, member);
        if (meter == 0)
            return 0;
        if (meter < 24)
            return 1;
        if (meter < 48)
            return 2;
        return meter < 100 ? 3 : 4;
    }

    // Building type index of a family id: the first entry of the sorted threshold table above it, 0 if none.
    int Family_GetTypeIndex(int family)
    {
        const uint32_t table = globals::familyTypeTable.get();
        const int32_t count = globals::familyTypeCount.get();
        for (int i = 0; i < count; ++i)
            if ((static_cast<uint32_t>(family) & 0xFFFFu) < port::load<uint16_t>(table + 2u * static_cast<uint32_t>(i)))
                return i;
        return 0;
    }

    // A family living in the city moves away: its bit goes from bitset A to bitset C, the moved-in count drops and
    // the left count rises.
    void Family_MoveAway(uint32_t family)
    {
        const int32_t id = static_cast<int32_t>(family);
        const uint32_t offset = static_cast<uint32_t>(4 * (id / 32));
        const uint32_t bit = 1u << (id & 31);
        const uint32_t inCity = port::load<uint32_t>(globals::kFamiliesInCity + offset);
        if ((inCity & bit) == 0)
            return;
        port::store<uint32_t>(globals::kFamiliesInCity + offset, inCity & ~bit);
        port::store<uint32_t>(globals::kFamiliesLeft + offset, port::load<uint32_t>(globals::kFamiliesLeft + offset) | bit);
        globals::familiesMovedIn.set(globals::familiesMovedIn.get() - 1);
        globals::familiesLeftEvent.set(globals::familiesLeftEvent.get() + 1);
    }

    // A family moves in: its bit in bitset A and both moved-in counters.
    void Family_SetFlagA(uint32_t family)
    {
        const int32_t id = static_cast<int32_t>(family);
        const uint32_t word = globals::kFamiliesInCity + static_cast<uint32_t>(4 * (id / 32));
        port::store<uint32_t>(word, port::load<uint32_t>(word) | (1u << (id & 31)));
        globals::familiesMovedIn.set(globals::familiesMovedIn.get() + 1);
        globals::familiesMovedInEvent.set(globals::familiesMovedInEvent.get() + 1);
    }

    // The family's number in the Town's Pages: 19 followed by the low six digits of 97 * family + 6920, with
    // digits 1 to 5 in reverse order (digit k of the source lands at kPlace[k]).
    int Family_MakePhoneNumber(int family)
    {
        static constexpr int kPlace[6] = {1, 100000, 10000, 1000, 100, 10};
        int source = family * 0x61 + 0x1B08;
        int number = 0;
        for (int digit = 0; digit < 6; ++digit, source /= 10)
            number += (source % 10) * kPlace[digit];
        return number + 19000000;
    }

    // Forgets every family: the three bitsets, the two family tables, the member bytes and the counters.
    void Family_ResetState()
    {
        for (uint32_t i = 0; i < 0x139; ++i)
        {
            port::store<uint32_t>(globals::kFamiliesKnown + 4u * i, 0);
            port::store<uint32_t>(globals::kFamiliesInCity + 4u * i, 0);
            port::store<uint32_t>(globals::kFamiliesLeft + 4u * i, 0);
        }
        for (uint32_t i = 0; i < 0x100; ++i)
        {
            port::store<uint32_t>(globals::kFamilyTableB + 4u * i, 0);
            port::store<uint32_t>(globals::kFamilyTableA + 4u * i, 0);
        }
        for (uint32_t i = 0; i < 60000; ++i)
            port::store<uint8_t>(globals::kMemberBytes + i, 0);
        globals::familiesMovedIn.set(0);
        globals::familiesMovedInEvent.set(0);
        globals::familiesLeftEvent.set(0);
    }

    int Family_HasLeft(uint32_t family) { return familyBit(globals::kFamiliesLeft, family); }
    int Family_IsKnown(uint32_t family) { return familyBit(globals::kFamiliesKnown, family); }
    int Family_InCity(uint32_t family) { return familyBit(globals::kFamiliesInCity, family); }

    PORT_FN(Family_GetFriendship);
    PORT_FN(Family_GetFriendshipBand);
    PORT_FN(Family_GetTypeIndex);
    PORT_FN(Family_HasLeft);
    PORT_FN(Family_IsKnown);
    PORT_FN(Family_InCity);
    PORT_FN(Family_SetFlagA);
    PORT_FN(Family_MoveAway);
    PORT_FN(Family_MakePhoneNumber);
    PORT_FN(Family_ResetState);

    // ---- friendship tests, applications, the cache (batch 38)

    namespace
    {
        uint32_t memberByte(int family, int member) { return globals::kMemberBytes + static_cast<uint32_t>(family * 6 + member); }

        // A member agrees with a meter of 100 (capped at 120), or 48 with approval not below 0, or 24 with approval
        // 50 and up.
        int meterAgrees(uint32_t meter, int approval)
        {
            if (meter > 120)
                meter = 120;
            const bool warm = meter >= 100 || (meter >= 48 && approval >= 0);
            return warm || (meter >= 24 && approval >= 50) ? 1 : 0;
        }
    }

    int Family_AddFriendshipScaled(int family, int member, GuestPtr<int32_t> approval, int points, int sign)
    {
        return Family_AddFriendship(family, member, approval, static_cast<int>(static_cast<uint32_t>(points) * static_cast<uint32_t>(sign)));
    }

    void Family_ClearMemberFlag(int family, int member)
    {
        const uint32_t byte = memberByte(family, member);
        port::store<uint8_t>(byte, port::load<uint8_t>(byte) & 0x7Fu);
    }

    int Family_MeterAgrees(int family, int member, int approval) { return meterAgrees(port::load<uint8_t>(memberByte(family, member)) & 0x7Fu, approval); }

    // The same for friends only (bit 7 of the member byte).
    int Family_FriendAgrees(int family, int member, int approval)
    {
        const uint8_t byte = port::load<uint8_t>(memberByte(family, member));
        if ((byte & 0x80u) == 0)
            return 0;
        return meterAgrees(byte & 0x7Fu, approval);
    }

    // A family may apply when its home type's call level (+0x154, low 7 bits) is reached by the stage, it is not in
    // the city, has not left and has no application, and the person records have room: 231 more, or seven for each
    // active applicant and seven more.
    int Family_CanApply(uint32_t family, GuestPtr<CityScene> city)
    {
        if (family == 0)
            return 0;
        uint32_t type = 0;
        const uint32_t thresholds = globals::familyTypeTable.get();
        for (int i = 0; i < globals::familyTypeCount.get(); ++i)
        {
            if ((family & 0xFFFFu) < port::load<uint16_t>(thresholds + 2u * static_cast<uint32_t>(i)))
            {
                type = static_cast<uint32_t>(i);
                break;
            }
        }
        const int callLevel = GuestPtr<BuildingType>{globals::buildingTypeAddress(type)}->callFlags & 0x7F;
        if (globals::stageState()->stage < callLevel)
            return 0;
        if (Family_InCity(family) != 0 || Family_HasLeft(family) != 0)
            return 0;
        if (Applicants_Find(city->applicants, family) != 0)
            return 0;
        const int32_t people = city->map->peopleInUse;
        if (people + 231 < 6000)
            return 1;
        return static_cast<int32_t>(7u * static_cast<uint32_t>(Applicants_CountActive(city->applicants)) + static_cast<uint32_t>(people) + 7u) < 6000 ? 1 : 0;
    }

    // The family's cached record (0x1D4 bytes from 0x1D52E0) when its load has finished: the slot of the family in
    // table A, then its pending file request (table at 0x1F2EE0, -1 none) is waited for once; 0 when not cached or
    // still loading.
    int Family_IsCached(int family)
    {
        int slot = -1;
        for (int i = 0; i < 256; ++i)
        {
            if (port::load<int32_t>(globals::kFamilyTableA + 4u * static_cast<uint32_t>(i)) == family)
            {
                slot = i;
                break;
            }
        }
        if (slot == -1)
            return 0;
        const uint32_t request = 0x001F2EE0u + 4u * static_cast<uint32_t>(slot);
        if (port::load<int32_t>(request) != -1)
        {
            if (FileQueue_IsDone(static_cast<int>(port::load<uint32_t>(0x001BDA94u)), port::load<int32_t>(request)) == 0)
                return 0;
            port::store<int32_t>(request, -1);
        }
        return static_cast<int>(0x001D52E0u + 0x1D4u * static_cast<uint32_t>(slot));
    }

    // A new stage: no family lives in the city or has left, and the three counters are cleared.
    void Family_ResetForStage()
    {
        for (uint32_t i = 0; i < 0x139u; ++i)
        {
            port::store<uint32_t>(globals::kFamiliesLeft + 4u * i, 0);
            port::store<uint32_t>(globals::kFamiliesInCity + 4u * i, 0);
        }
        globals::familiesMovedIn.set(0);
        globals::familiesMovedInEvent.set(0);
        globals::familiesLeftEvent.set(0);
    }

    void Family_SaveState(GuestPtr<McManager> mc)
    {
        SaveFile_Write(mc, static_cast<int>(globals::kMemberBytes), 0xEA60);
        SaveFile_Write(mc, static_cast<int>(globals::kFamiliesInCity), 0x4E4);
        SaveFile_Write(mc, static_cast<int>(globals::kFamiliesKnown), 0x4E4);
        SaveFile_Write(mc, static_cast<int>(globals::kFamiliesLeft), 0x4E4);
        SaveFile_Write(mc, static_cast<int>(globals::kMemberStatus), 0x400);
        SaveFile_Write(mc, static_cast<int>(globals::familiesMovedIn.address), 0xC);
    }

    PORT_FN(Family_AddFriendshipScaled);
    PORT_FN(Family_ClearMemberFlag);
    PORT_FN(Family_MeterAgrees);
    PORT_FN(Family_FriendAgrees);
    PORT_FN(Family_CanApply);
    PORT_FN(Family_IsCached);
    PORT_FN(Family_ResetForStage);
    PORT_FN(Family_SaveState);

    // ---- batch 38

    // Marks the family known (its number is in the Town's Pages).
    void Family_SetKnown(uint32_t family)
    {
        const int32_t id = static_cast<int32_t>(family);
        const uint32_t word = globals::kFamiliesKnown + static_cast<uint32_t>(4 * (id / 32));
        port::store<uint32_t>(word, port::load<uint32_t>(word) | (1u << (id & 31)));
    }

    PORT_FN(Family_SetKnown);

    // ---- friendship (batch 39)

    // Adds friendship points to a member's meter byte: the level (bits 3-6, steps of 8) carries a fraction in its low
    // three bits, so points move it in eighths (a negative sum takes one step more than it divides into). A member
    // who is not a friend yet and gains a step becomes one with a chance of the larger of the level and the approval
    // in 120. The level stays within 0..120 (the byte 1..0x7F) and the approval moves by the change of the
    // friendship band (a first band gained counts one less). The result is v0 as it came in, or the random entry
    // when one was drawn.
    int64_t Family_AddFriendship(int family, int member, GuestPtr<int32_t> approval, int points)
    {
        int64_t result = static_cast<int64_t>(port::gpr64(port::g_call.ctx, 2));
        const uint32_t meter = globals::kMemberBytes + static_cast<uint32_t>(member + family * 6);
        const uint8_t byte = port::load<uint8_t>(meter);
        const int sum = (byte & 7) + points;
        uint8_t isFriend = byte & 0x80u;
        int level = byte & 0x78;
        const int steps = sum < 0 ? sum / 8 - 1 : sum / 8;
        const int fraction = sum & 7;
        if (isFriend == 0 && steps > 0)
        {
            const int chance = level < *approval ? static_cast<int>(*approval) : level;
            result = Random_Next();
            if (static_cast<int32_t>(result) % 120 < chance)
                isFriend = 0x80u;
        }
        level += steps * 8;
        if (level < 0)
            level = 0;
        else if (level >= 121)
            level = 120;
        const int before = Family_GetFriendshipBand(family, member);
        int value = level | fraction;
        if (value <= 0)
            value = 1;
        else if (value >= 0x80)
            value = 0x7F;
        port::store<uint8_t>(meter, static_cast<uint8_t>(isFriend | value));
        const int after = Family_GetFriendshipBand(family, member);
        int change = after - before;
        if (change > 0 && before == 0)
            --change;
        *approval = *approval + change;
        return result;
    }

    // Every family (ids below 10000) becomes known, with member 0 a friend at 64 points (0xC0).
    void Family_BefriendAll()
    {
        for (int family = 0; family < 10000; ++family)
        {
            const uint32_t word = globals::kFamiliesKnown + 4u * static_cast<uint32_t>(family / 32);
            port::store<uint32_t>(word, port::load<uint32_t>(word) | (1u << (family & 31)));
            port::store<uint8_t>(globals::kMemberBytes + 6u * static_cast<uint32_t>(family), 0xC0);
        }
    }

    PORT_FN(Family_AddFriendship);
    PORT_FN(Family_BefriendAll);

    // ---- The family record cache (plan P6 batch 1) ----

    namespace
    {
        // The cache slot holding `family`, or -1.
        int32_t findFamilySlot(int family)
        {
            for (int32_t slot = 0; slot < 0x100; ++slot)
                if (port::load<int32_t>(globals::kFamilyTableA + 4u * static_cast<uint32_t>(slot)) == family)
                    return slot;
            return -1;
        }

        // The slot to reuse: the one used longest ago (the first, at a tie), starting from the current frame.
        int32_t oldestFamilySlot()
        {
            uint32_t oldest = static_cast<uint32_t>(globals::frameCounter.get());
            int32_t best = 0;
            for (int32_t slot = 0; slot < 0x100; ++slot)
            {
                const uint32_t usedAt = port::load<uint32_t>(globals::kFamilyTableB + 4u * static_cast<uint32_t>(slot));
                if (!(oldest <= usedAt))
                {
                    oldest = usedAt;
                    best = slot;
                }
            }
            return best;
        }

        uint32_t familyRecord(int32_t slot) { return globals::kFamilyRecords + 0x1D4u * static_cast<uint32_t>(slot); }
        uint32_t familyLoadedFlag(int32_t slot) { return familyRecord(slot) + 0x20u; }
        uint32_t slotWord(uint32_t table, int32_t slot) { return table + 4u * static_cast<uint32_t>(slot); }

        // A slot that was claimed but whose record never loaded is emptied.
        void emptyFamilySlot(int32_t slot)
        {
            port::store<uint32_t>(slotWord(globals::kFamilyTableB, slot), 0u);
            port::store<uint32_t>(slotWord(globals::kFamilyTableA, slot), 0u);
            port::store<int32_t>(slotWord(globals::kFamilyTableC, slot), -1);
        }
    }

    // Opens FAMILY.BIN (asynchronous reads, 0x8000 buffer) and loads DATA\BIN\EACHSTR.BIN whole; its size is kept in halfwords.
    void Family_Init()
    {
        Family_ResetState();
        const uint32_t memory = sdk::operator_new(8u);
        uint32_t file = memory;
        if (memory != 0u)
            file = File_ctor(GuestPtr<GameFile>{memory}, 0x8000).address;
        globals::familyFile.set(file);
        File_Open(GuestPtr<GameFile>{globals::familyFile.get()}, GuestPtr<char>{0x001B6390u});
        FileQueue_WaitAll(0);
        port::GuestLocal<GameFile> local;
        port::GuestLocal<int32_t> size;
        File_ctor(local, 0);
        globals::eachStrData.set(static_cast<uint32_t>(File_LoadWhole(local, GuestPtr<char>{0x001B63B0u}, size)));
        int32_t length = *size;
        if (length < 0)
            length += 1;
        globals::eachStrCount.set(length >> 1);
        File_dtor(local, -1);
    }

    // The family record in the cache, waiting for it if it is still being read; otherwise reads it into the least recently
    // used slot and waits (flags is the wait functions' quiet argument).
    GuestPtr<Family> Family_Get(int family, int flags)
    {
        int32_t slot = findFamilySlot(family);
        if (slot != -1)
        {
            const uint32_t request = slotWord(globals::kFamilyTableC, slot);
            if (port::load<int32_t>(request) != -1)
            {
                FileQueue_WaitFor(static_cast<int>(globals::familyFile.get()), port::load<int32_t>(request), flags);
                port::store<int32_t>(request, -1);
            }
            if (port::load<int16_t>(familyLoadedFlag(slot)) != 0)
                return GuestPtr<Family>{familyRecord(slot)};
            emptyFamilySlot(slot);
        }
        slot = oldestFamilySlot();
        port::store<int16_t>(familyLoadedFlag(slot), 0);
        const GuestPtr<GameFile> file{globals::familyFile.get()};
        File_Seek(file, family * 0x1D4, 0, 0);
        const uint32_t record = familyRecord(slot);
        File_Read(file, static_cast<int>(record), 1, 0x1D4, 0);
        port::store<int32_t>(slotWord(globals::kFamilyTableA, slot), family);
        port::store<int32_t>(slotWord(globals::kFamilyTableB, slot), globals::frameCounter.get());
        port::store<int32_t>(slotWord(globals::kFamilyTableC, slot), -1);
        FileQueue_WaitAll(flags);
        return GuestPtr<Family>{record};
    }

    // Starts reading a family record without waiting: returns the record's address once it is there, else 0 (the read is
    // started, or skipped when flags is 1 and the file queue already holds more than 0x20 requests).
    int Family_Prefetch(int family, int flags)
    {
        int32_t slot = findFamilySlot(family);
        if (slot != -1)
        {
            const uint32_t request = slotWord(globals::kFamilyTableC, slot);
            if (port::load<int32_t>(request) != -1)
            {
                if (FileQueue_IsDone(static_cast<int>(globals::familyFile.get()), port::load<int32_t>(request)) == 0)
                    return 0;
                port::store<int32_t>(request, -1);
                return static_cast<int>(familyRecord(slot));
            }
            if (port::load<int16_t>(familyLoadedFlag(slot)) != 0)
                return static_cast<int>(familyRecord(slot));
            emptyFamilySlot(slot);
        }
        if (flags != 1 || globals::fileQueueCount.get() < 0x21)
        {
            slot = oldestFamilySlot();
            port::store<int16_t>(familyLoadedFlag(slot), 0);
            const GuestPtr<GameFile> file{globals::familyFile.get()};
            File_Seek(file, family * 0x1D4, 0, static_cast<uint16_t>(flags));
            const int requestId = File_Read(file, static_cast<int>(familyRecord(slot)), 1, 0x1D4, static_cast<uint16_t>(flags));
            port::store<int32_t>(slotWord(globals::kFamilyTableC, slot), requestId);
            port::store<int32_t>(slotWord(globals::kFamilyTableA, slot), family);
            port::store<int32_t>(slotWord(globals::kFamilyTableB, slot), globals::frameCounter.get());
        }
        return 0;
    }

    PORT_FN(Family_Init);
    PORT_FN(Family_Get);
    PORT_FN(Family_Prefetch);

    // The first of the member's acquaintances (of up to 16, ended by 0) that may apply to the city (Family_CanApply); 0 when
    // there is none. Returns the family id.
    uint32_t Family_PickIntroduction(int family, int member, GuestPtr<CityScene> city)
    {
        const GuestPtr<Family> record = Family_Get(family, 0);
        const uint32_t acquaintances = record.address + 0x24u + 0x48u * static_cast<uint32_t>(member) + 8u;
        for (uint32_t i = 0; i < 16u; ++i)
        {
            const uint32_t id = port::load<uint16_t>(acquaintances + 2u * i);
            if (id == 0)
                return 0;
            if (Family_CanApply(id, city) != 0)
                return id;
        }
        return 0;
    }

    PORT_FN(Family_PickIntroduction);

    // ---- Moving a family in (plan P6 batch 2) ----

    namespace
    {
        // The next free person record (one whose character type, +0x24, is 0) from the cursor at +0x6C of the map, which
        // moves on past the used ones and wraps at 6000. After 6001 used records in a row the answer is record 0, in use or not.
        uint32_t nextFreePerson(uint32_t map)
        {
            int steps = 0;
            for (;;)
            {
                const uint32_t cursor = port::load<uint32_t>(map + 0x6Cu);
                if (port::load<uint8_t>(port::load<uint32_t>(map + 0x68u) + cursor * 0x30u + 0x24u) == 0)
                    return cursor;
                ++steps;
                port::store<uint32_t>(map + 0x6Cu, cursor + 1u);
                if (steps >= 0x1771)
                    return 0;
                if (port::load<int32_t>(map + 0x6Cu) >= 0x1770)
                    port::store<uint32_t>(map + 0x6Cu, 0u);
            }
        }
    }

    // Puts the members of `family` on the map at the tile of building `object`: a person record for each, at the middle of the
    // tile (x from the low 7 bits of the tile, z from the high bits counted from the far edge), walking nowhere, with the
    // family and member numbers, a schedule state, the member's flags (+0x2B of the member record, 8 added for members with
    // bit 4 of +0x27), activity 0x13 and an introduction; each counts in the placement score of its character class. When some
    // member had bit 4 of +0x27 and the building is not of category 9 or 0xB, one more record (character type 0x34) is made at
    // the same spot. The family is then flagged (mm_Family_SetFlagA).
    void Family_AssignHome(GuestPtr<CityMap> map, GuestPtr<CityScene> city, int family, int object)
    {
        const GuestPtr<Family> record = Family_Get(family, 0);
        const GuestPtr<MapObject> house = map->objects + object;
        const uint32_t tile = house->tile;
        const ps2f x = ps2f(0.5f) + (fromInt(static_cast<int32_t>(tile & 0x7Fu)) - ps2f(64.0f));
        const ps2f z = (fromInt(0x80 - static_cast<int32_t>(tile >> 7)) - ps2f(64.0f)) - ps2f(0.5f);
        bool extra = false;
        const auto place = [&](uint32_t index) {
            const GuestPtr<MapPerson> person{port::load<uint32_t>(map.address + 0x68u) + 0x30u * index};
            person->x = x;
            port::store<uint32_t>(person.address + 4u, 0u);
            person->z = z;
            port::store<uint32_t>(person.address + 0xCu, 0u);
            return person;
        };
        for (int i = 0; i < record->memberCount; ++i)
        {
            const uint32_t member = record.address + 0x48u * static_cast<uint32_t>(i);
            const uint32_t index = nextFreePerson(map.address);
            const GuestPtr<MapPerson> person = place(index);
            person->charType = port::load<uint8_t>(member + 0x24u);
            person->nextOnTile = 6000;
            person->index = static_cast<uint16_t>(index);
            person->work = static_cast<uint16_t>(object);
            person->inside = 0x3F80;
            person->destination = 0x3F80;
            person->walkTile = 0x4000;
            person->previousTile = 0x4000;
            port::store<uint16_t>(person.address + 0x1Cu, 0);
            person->cooldown = 0;
            person->target = 0x3F80;
            person->follow = 0;
            person->family = static_cast<uint16_t>(family);
            person->member = static_cast<uint8_t>(i);
            person->schedState = Schedule_Evaluate(person->family, person->member);
            person->flags = port::load<uint8_t>(member + 0x2Bu);
            person->activity = 0x13;
            person->introduces = static_cast<uint16_t>(Family_PickIntroduction(person->family, person->member, city));
            if ((port::load<uint8_t>(member + 0x27u) & 4u) != 0)
            {
                extra = true;
                person->flags = static_cast<uint16_t>(person->flags | 8);
            }
            person->flags = static_cast<uint16_t>(person->flags | 0x800);
            const uint32_t classWord = port::load<uint32_t>(port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType) + 0x1Cu);
            CityScene_RecordPlacementScore(city, static_cast<int>((classWord & 0x7000u) >> 12), static_cast<int>((classWord & 0xE00u) >> 9));
            port::store<int32_t>(map.address + 0x74u, port::load<int32_t>(map.address + 0x74u) + 1);
            port::store<int32_t>(map.address + 0x70u, port::load<int32_t>(map.address + 0x70u) + 1);
        }
        const uint32_t category = port::load<uint8_t>(globals::buildingTypeAddress(house->type) + 0x14Bu);
        if (category == 9 || category == 0xB)
            extra = false;
        if (extra)
        {
            const uint32_t index = nextFreePerson(map.address);
            const GuestPtr<MapPerson> person = place(index);
            person->charType = 0x34;
            person->nextOnTile = 6000;
            person->index = static_cast<uint16_t>(index);
            person->flags = 0;
            person->work = static_cast<uint16_t>(object);
            person->inside = 0x3F80;
            person->walkTile = 0x4000;
            person->previousTile = 0x4000;
            port::store<uint16_t>(person.address + 0x1Cu, 0);
            person->target = 6000;
            person->family = 6000;
            person->introduces = 0;
            person->flags = static_cast<uint16_t>(person->flags | 0x800);
            port::store<int32_t>(map.address + 0x70u, port::load<int32_t>(map.address + 0x70u) + 1);
        }
        Family_SetFlagA(static_cast<uint32_t>(family));
    }

    PORT_FN(Family_AssignHome);
}
