// Families: per-family flags kept as bitsets over all family ids.

#include "game_functions.h"
#include "game_globals.h"

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
}
