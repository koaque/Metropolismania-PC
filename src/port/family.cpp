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
}
