// City scene accessors, the claim meter and clock helpers, and the default slots of the building type
// classes (mm_BldgType_*: the base class every building type derives from).

#include "game_functions.h"
#include "game_globals.h"

namespace mm
{
    int CityScene_GetMode(GuestPtr<CityScene> city) { return city->modeStack[0]; }
    int CityScene_GetFade(GuestPtr<CityScene> city) { return city->fade; }
    int CityScene_HasFlag8(GuestPtr<CityScene> city) { return (city->flags & 8u) != 0; }

    // Starts a fade unless one runs: -0x80 when the fade level is 0, +0x80 otherwise.
    void CityScene_StartFade(GuestPtr<CityScene> city, int speed)
    {
        if (city->fade != 0)
            return;
        city->fade = city->fadeLevel == 0 ? -0x80 : 0x80;
        city->fadeSpeed = speed;
    }

    // The mode stack holds 5 modes, the current one in modeStack[0].
    void CityScene_PushMode(GuestPtr<CityScene> city, int mode)
    {
        for (int i = 4; i > 0; --i)
            city->modeStack[i] = city->modeStack[i - 1];
        city->modeStack[0] = mode;
        ++city->modeDepth;
    }

    // Pops a mode unless only the bottom one is left.
    void CityScene_PopMode(GuestPtr<CityScene> city)
    {
        if (city->modeDepth <= 1)
            return;
        for (int i = 0; i < 4; ++i)
            city->modeStack[i] = city->modeStack[i + 1];
        --city->modeDepth;
    }

    // time + (offset mod one hour), wrapped at one day (seconds). The first argument is unused.
    int Clock_AddWrap(int, int time, int offset)
    {
        int result = time + offset % 3600;
        if (result >= 86400)
            result -= 86400;
        return result;
    }

    // Two decaying averages of placement scores (x10): once ten have been seen, the old sum keeps 9/10.
    void CityScene_RecordPlacementScore(GuestPtr<CityScene> city, int scoreA8, int scoreA0)
    {
        if (++city->scoreA0Count > 9)
        {
            city->scoreA0 = city->scoreA0 * 9 / 10;
            city->scoreA0Count = 10;
        }
        city->scoreA0 += scoreA0 * 10;
        if (++city->scoreA8Count > 9)
        {
            city->scoreA8 = city->scoreA8 * 9 / 10;
            city->scoreA8Count = 10;
        }
        city->scoreA8 += scoreA8 * 10;
    }

    // Bit set of the active laptop entries (each entry is a bit number).
    uint32_t Laptop_CountActive()
    {
        uint32_t bits = 0;
        for (int i = 0; i < globals::laptopEntryCount.get(); ++i)
            bits |= 1u << (port::load<uint32_t>(globals::kLaptopEntries + 4u * static_cast<uint32_t>(i)) & 31u);
        return bits;
    }

    // Town's Pages cursor: entries 0..4 left, 5..9 right, 10 the CLOSE button. Off-page entries move to the
    // last entry, or to CLOSE when moving on (direction 2) or, on a short page, sideways (1).
    int TownPages_ClampCursor(GuestPtr<TownPages> pages, int cursor, int direction)
    {
        cursor = cursor < 0 ? 0 : cursor > 10 ? 10 : cursor;
        const int32_t entries = pages->pageEntries;
        if (entries == 0)
            return 10;
        if (cursor < 5)
        {
            if (entries <= cursor)
                cursor = direction == 2 ? 10 : entries - 1;
        }
        else if (entries <= cursor)
        {
            if (entries < 6)
                cursor = (direction == 1 || direction == 2) ? 10 : entries - 1;
            else
                cursor = direction == 2 ? 10 : entries - 1;
        }
        return cursor;
    }

    // 0 while the meter is at or below the first threshold, 1 up to the second, 2 above it. The two
    // thresholds are the first two halfwords of CLAIM.BIN.
    int ClaimMeter_Level(uint16_t meter)
    {
        const uint32_t thresholds = globals::claimData.get();
        if (meter <= port::load<uint16_t>(thresholds))
            return 0;
        return meter <= port::load<uint16_t>(thresholds + 2) ? 1 : 2;
    }

    // Colour of a claim icon at a level: level 0 has a fixed colour; otherwise the icon's colour set picks
    // two entries of the colour table (ids with bit 15 use the sets from 0x40 on).
    void ClaimIcon_GetColor(uint32_t icon, int level, GuestPtr<Vec4> colour)
    {
        if (level == 0)
        {
            *colour = port::load<Vec4>(globals::kClaimIconNoColour);
            return;
        }
        const uint32_t id = (icon & 0x8000u) ? (icon & 0x7FFFu) + 0x40u : icon & 0xFFFFu;
        const uint32_t set = port::load<uint8_t>(globals::claimIconSets.get() + id);
        *colour = port::load<Vec4>(globals::claimIconColours.get() + 16u * static_cast<uint32_t>(level - 1 + static_cast<int>(set) * 2));
    }

    // ---- applicants: 32 entries; count is the number in use

    int Applicants_AnyWaiting(GuestPtr<Applicants> applicants)
    {
        for (const ApplicantEntry &entry : applicants->entries)
            if (entry.state == 2)
                return 1;
        return 0;
    }

    int Applicants_Find(GuestPtr<Applicants> applicants, uint32_t family)
    {
        // count is not limited to 32 here, so step through guest memory like the original.
        const GuestPtr<ApplicantEntry> entries = GuestPtr<ApplicantEntry>::of(applicants->entries[0]);
        for (int i = 0; i < applicants->count; ++i)
            if (entries[i].family == family)
                return 1;
        return 0;
    }

    // The next entry from the iterator in the given state (guest address), or 0; advances the iterator.
    int Applicants_FindFrom(GuestPtr<Applicants> applicants, GuestPtr<uint32_t> iterator, uint32_t state)
    {
        while (*iterator <= 31)
        {
            const ApplicantEntry &entry = applicants->entries[(*iterator)++];
            if (static_cast<uint16_t>(entry.state) == state)
                return static_cast<int>(port::guest(&entry));
        }
        return 0;
    }

    int Applicants_Clear(GuestPtr<Applicants> applicants)
    {
        applicants->count = 0;
        applicants->unk_284 = 0;
        for (int i = 0; i < 32; ++i)
        {
            applicants->entries[i].state = 0;
            applicants->entries[i].index = static_cast<uint16_t>(i);
        }
        return static_cast<int>(applicants.address);
    }

    // ---- people's schedules and stamina

    // The schedule entry for a person: fixed entries for the t0 and t1 cases, the building type's own
    // schedule for activity 15, else entry a3.
    int Schedule_PickEntry(int, int, int a2, int a3, int t0, int t1)
    {
        const uint32_t base = globals::scheduleData.get();
        if (t0 != 0)
            return static_cast<int>(base + 0xAE0u);
        if (t1 != 0)
            return static_cast<int>(base + 0xB40u);
        if (a3 == 15)
        {
            const uint32_t own = port::load<uint8_t>(globals::buildingTypeAddress(static_cast<uint32_t>(a2)) + 0x14Bu);
            return static_cast<int>(base + (own + 15u) * 0x60u);
        }
        return static_cast<int>(base + static_cast<uint32_t>(a3) * 0x60u);
    }

    // 0 below 100000, 1 up to 1800000, 2 above.
    int Stamina_HungerLevel(GuestPtr<Stamina> stamina)
    {
        if (stamina->value < 100000)
            return 0;
        return stamina->value < 1800001 ? 1 : 2;
    }

    GuestPtr<WantsWin> Wants_ctor(GuestPtr<WantsWin> wants)
    {
        wants->counters[0] = 0;
        wants->counters[1] = 0;
        return wants;
    }

    // Building type base class: slots the subclasses override.
    void BldgType_InitRecord(GuestPtr<BuildingType>) {}
    int BldgType_Tick(GuestPtr<BuildingType>) { return 0; }
    void BldgType_Draw(GuestPtr<BuildingType>) {}
    uint8_t BldgType_Slot1C(GuestPtr<BuildingType> btype) { return btype->unk_143; }
    int BldgType_Slot20(GuestPtr<BuildingType>) { return 1; }
    int BldgType_CollideA(GuestPtr<BuildingType>) { return 0; }
    int BldgType_CollideB(GuestPtr<BuildingType>) { return 0; }
    void BldgType_Slot34(GuestPtr<BuildingType>) {}

    // Whether a building type is open now: between its opening and closing minute, the clock shifted by the
    // object's id (so neighbours do not all open at once); never on a holiday (city flag 2) for types with
    // flag 0x2000.
    int BldgType_IsOpenAt(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        int open = 1;
        int32_t time = city->clock;
        if (obj)
        {
            time = city->clock + obj->id % 3600;
            if (time >= 86400)
                time -= 86400;
        }
        if (time < static_cast<int32_t>(btype->openMinute * 60u))
            open = 0;
        else if (time > static_cast<int32_t>(btype->closeMinute * 60u))
            open = 0;
        if ((city->flags & 2u) != 0 && (btype->flags & 0x2000u) != 0)
            open = 0;
        return open;
    }

    // The My Home types are kinds 5 to 8.
    int BldgType_IsMyHome(GuestPtr<BuildingType> btype) { return btype->kind > 4 && btype->kind < 9; }

    // The road class adds the halfword at +0x18 of the map object (a1) to the base value.
    int BldgRoad_Slot1C(GuestPtr<BuildingType> btype, int a1)
    {
        return btype->unk_143 + port::load<uint16_t>(static_cast<uint32_t>(a1) + 0x18u);
    }

    PORT_FN(CityScene_GetMode);
    PORT_FN(CityScene_GetFade);
    PORT_FN(CityScene_HasFlag8);
    PORT_FN(CityScene_StartFade);
    PORT_FN(CityScene_PushMode);
    PORT_FN(CityScene_PopMode);
    PORT_FN(CityScene_RecordPlacementScore);
    PORT_FN(Laptop_CountActive);
    PORT_FN(TownPages_ClampCursor);
    PORT_FN(BldgType_IsOpenAt);
    PORT_FN(ClaimIcon_GetColor);
    PORT_FN(Applicants_Find);
    PORT_FN(Applicants_FindFrom);
    PORT_FN(Applicants_Clear);
    PORT_FN(Schedule_PickEntry);
    PORT_FN(Stamina_HungerLevel);
    PORT_FN(Clock_AddWrap);
    PORT_FN(ClaimMeter_Level);
    PORT_FN(Applicants_AnyWaiting);
    PORT_FN(Wants_ctor);
    PORT_FN(BldgType_InitRecord);
    PORT_FN(BldgType_Tick);
    PORT_FN(BldgType_Draw);
    PORT_FN(BldgType_Slot1C);
    PORT_FN(BldgType_Slot20);
    PORT_FN(BldgType_CollideA);
    PORT_FN(BldgType_CollideB);
    PORT_FN(BldgType_Slot34);
    PORT_FN(BldgType_IsMyHome);
    PORT_FN(BldgRoad_Slot1C);
}
