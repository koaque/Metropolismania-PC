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

    int Family_HasLeft(uint32_t family) { return familyBit(globals::kFamiliesLeft, family); }
    int Family_IsKnown(uint32_t family) { return familyBit(globals::kFamiliesKnown, family); }
    int Family_InCity(uint32_t family) { return familyBit(globals::kFamiliesInCity, family); }

    PORT_FN(Family_HasLeft);
    PORT_FN(Family_IsKnown);
    PORT_FN(Family_InCity);
}
