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

    int Family_HasLeft(uint32_t family) { return familyBit(globals::kFamiliesLeft, family); }
    int Family_IsKnown(uint32_t family) { return familyBit(globals::kFamiliesKnown, family); }
    int Family_InCity(uint32_t family) { return familyBit(globals::kFamiliesInCity, family); }

    PORT_FN(Family_GetFriendship);
    PORT_FN(Family_GetFriendshipBand);
    PORT_FN(Family_GetTypeIndex);
    PORT_FN(Family_HasLeft);
    PORT_FN(Family_IsKnown);
    PORT_FN(Family_InCity);
}
