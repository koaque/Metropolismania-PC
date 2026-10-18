// City scene accessors, the claim meter and clock helpers, and the default slots of the building type
// classes (mm_BldgType_*: the base class every building type derives from).

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"
#include "vu0.h"

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

    // While Circle alone (0x20) is held in the city view (and `allowed`), HUD element 2 is hidden and +0x120 set,
    // once per press; any other input clears both again. Returns +0x120.
    int Laptop_CheckOpen(GuestPtr<Laptop> laptop, GuestPtr<CityScene> city, int allowed)
    {
        const uint32_t held = Pad_Held(0);
        if (held == 0x20u && CityScene_GetMode(city) == 1 && allowed != 0)
        {
            if (laptop->circleHeld == 0)
            {
                city->hudVisible[2] = 0;
                laptop->circleActive = 1;
                if ((Laptop_CountActive() & 0x10u) != 0)
                    laptop->unk_11C = 4;
            }
        }
        else
        {
            laptop->circleHeld = (held & 0x20u) != 0 ? 1 : 0;
            if (laptop->circleActive != 0)
            {
                city->hudVisible[2] = 1;
                laptop->circleActive = 0;
            }
        }
        return laptop->circleActive;
    }

    // The laptop's entries on a carousel, each frame. Entry i stands at angle (2 pi / count) x (count - i) plus
    // the carousel's turn, kept within -pi..pi; its matrix (from +0x50, 0x40 bytes each) is that turn about y
    // times a tilt of pi/12 about x, placed at the turned radius vector plus the centre and scaled by +0x38. In
    // state 2 the selected entry moves by kLaptopLift. The original's frame is reserved whole and its matrices
    // kept at their offsets; like the original it also builds a scale-5 matrix there that nothing reads.
    void Laptop_Animate(GuestPtr<Laptop> laptop)
    {
        using namespace vu0;
        port::GuestFrame<0x160> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<Vec4> turnY{sp + 0x40u}, tilt{sp + 0x80u}, scale{sp + 0xC0u}, unused{sp + 0x100u}, place{sp + 0x140u},
            radius{sp + 0x150u};
        const GuestPtr<Vec4> identity{globals::kIdentityMatrix};
        // The float constants of the original: pi, 2 pi and pi/12.
        const ps2f pi = 0x1.921FB6p+1f, twoPi = 0x1.921FB6p+2f, tiltAngle = 0x1.0C1524p-2f;
        sdk::sceVu0RotMatrixX(tilt.address, identity.address, tiltAngle);
        Mat4_Copy(scale, identity);
        const ps2f size = laptop->scale;
        scale[2].z = size;
        scale[1].y = size;
        scale[0].x = size;
        Mat4_Copy(unused, identity);
        unused[2].z = 5.0f;
        unused[1].y = 5.0f;
        unused[0].x = 5.0f;
        for (int i = 0; i < globals::laptopEntryCount.get(); ++i)
        {
            const int count = globals::laptopEntryCount.get();
            ps2f angle = (twoPi / fromInt(count)) * fromInt(count - i);
            if (!(angle < pi))
                angle = (angle - pi) - pi;
            angle = angle + laptop->turn;
            if (angle < -pi)
                angle = angle + twoPi;
            else if (!(angle < pi))
                angle = (angle - pi) - pi;
            sdk::sceVu0RotMatrixY(turnY.address, identity.address, angle);
            Mat4_MulVu0(turnY, turnY, tilt);
            *radius = laptop->radius;
            Math_MulMatVecVu0(place, turnY, radius);
            vf(4) = load(place);
            vf(5) = load(GuestPtr<Vec4>::of(laptop->centre));
            set(vf(4), add(vf(4), vf(5)), XYZW);
            store(place, vf(4));
            const GuestPtr<Vec4> entry{laptop.address + 0x50u + 0x40u * static_cast<uint32_t>(i)};
            Mat4_Copy(entry, identity);
            entry[3] = *place;
            Mat4_MulVu0(entry, entry, scale);
            if (laptop->state == 2 && i == laptop->selected)
            {
                vf(4) = load(entry + 3);
                vf(5) = load(GuestPtr<Vec4>{globals::kLaptopLift});
                set(vf(4), add(vf(4), vf(5)), XYZW);
                store(entry + 3, vf(4));
            }
        }
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

    // The entry under the cursor. On a contents page: the family type in row `row`, counting only types with
    // known families, 10 per page. On a family page: the family id, counting known families from the first one
    // of the type whose listing the page belongs to.
    int TownPages_SelectedEntry(GuestPtr<TownPages> pages)
    {
        const int page = pages->page;
        if (page < pages->contentsPages)
        {
            const GuestPtr<TownPagesIndex> index = pages->index;
            int type = 0;
            for (int n = 0; n < page * 10; ++n)
            {
                while (index[type].count == 0)
                    ++type;
                ++type;
            }
            for (int row = 0;; ++row, ++type)
            {
                while (index[type].count == 0)
                    ++type;
                if (row >= pages->row)
                    return type;
            }
        }
        const GuestPtr<TownPagesIndex> index = pages->index;
        int type = 0;
        while (type < globals::familyTypeCount.get() && !(index[type].count != 0 && page < index[type].startPage))
            ++type;
        do
            --type;
        while (index[type].count == 0);
        const int first = index[type].firstFamily;
        const int skip = pages->row + (page - index[type].startPage) * 10;
        int offset = 0;
        for (int n = 0; n < skip; ++n)
        {
            do
                ++offset;
            while (Family_IsKnown(static_cast<uint32_t>(first + offset)) == 0);
        }
        return first + offset;
    }

    // A contents page (the current page): the next ten family types with known families, five per column, each
    // as the building type of the same number's name (cut after 18 characters with an 0xCD byte), then a line
    // of dashes and the page its listing starts on (2 x startPage + 1, padded with '-'). A page without any
    // gets the two explanation texts and the cursor on CLOSE (row 10). The lines are built in the original's
    // frame (sp + 0x60, the page number at + 0xA0).
    void TownPages_FillContents(GuestPtr<TownPages> pages)
    {
        port::GuestFrame<0xB0> frame;
        const uint32_t line = frame.ptr().address + 0x60u, number = frame.ptr().address + 0xA0u;
        const int types = globals::familyTypeCount.get();
        TextBox_SetText(pages->text[2], GuestPtr<char>{globals::kTownPagesHeading}, 0);
        TextBox_SetText(pages->text[5], GuestPtr<char>{globals::kTownPagesClose}, 0);
        int type = 0;
        const auto skipEmpty = [&] {
            while (pages->index[type].count == 0)
                ++type;
        };
        const int skip = pages->page * 10;
        for (int n = 0; n < skip; ++n, ++type)
            skipEmpty();
        int row = 0;
        for (; row < 10; ++row, ++type)
        {
            skipEmpty();
            if (!(type < types))
                break;
            const uint32_t name = port::load<uint32_t>(globals::buildingTypeTable.get() + 4u * static_cast<uint32_t>(type)) + 0x110u;
            Text_FormatPadded(GuestPtr<char>{number}, pages->index[type].startPage * 2 + 1, 4);
            for (uint32_t c = number; port::load<int8_t>(c) == ' '; ++c)
                port::store<uint8_t>(c, '-');
            sdk::strcpy(line, name);
            if (static_cast<uint32_t>(sdk::strlen(name)) >= 19)
            {
                port::store<uint8_t>(line + 18u, 0xCD);
                port::store<uint8_t>(line + 19u, 0);
            }
            sdk::strcat(line, globals::kTownPagesDashes);
            sdk::strcat(line, number);
            sdk::strcat(line, globals::kTownPagesNewline);
            TextBox_AppendText(row < 5 ? pages->text[0] : pages->text[1], GuestPtr<char>{line}, 0);
        }
        if (row == 0)
        {
            TextBox_AppendText(pages->text[0], GuestPtr<char>{globals::kTownPagesEmptyLeft}, 0);
            TextBox_AppendText(pages->text[1], GuestPtr<char>{globals::kTownPagesEmptyRight}, 0);
            pages->unk_58 = 10;
            pages->row = 10;
            pages->cursorX = port::load<int32_t>(globals::kTownPagesCursor + 8u * static_cast<uint32_t>(pages->row));
            pages->cursorY = port::load<int32_t>(globals::kTownPagesCursor + 4u + 8u * static_cast<uint32_t>(pages->row));
        }
        pages->pageEntries = row;
    }

    // The book, its texts and, while the list has a cursor, the cursor frame over the current entry.
    void TownPages_Draw(GuestPtr<TownPages> pages)
    {
        A3DObject_Draw(pages->book);
        for (const int i : {0, 1, 3, 4, 2, 5})
            if (pages->text[i])
                TextBox_Draw(pages->text[i]);
        if (pages->text[5] && pages->mode != 6)
        {
            Frame_SetRect(pages->cursorFrame, pages->cursorX + 0x46, pages->cursorY + 0x4E, 0x18, 0xC);
            Dma_AppendCall(static_cast<int>(pages->cursorFrame.address));
        }
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

    // A building's claim icon colour: the plain colour while the claim meter is low, else the claim's icon
    // colour pulsing with the frame counter: level 1 between 0.70 and 1.00 over 60 frames, level 2 between
    // 50/60 and 60/60 over 20 frames (alpha unchanged).
    void BldgClaims_DrawIcon(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> colour)
    {
        const int level = ClaimMeter_Level(obj->claimMeter);
        if (level == 0)
        {
            *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
            return;
        }
        const uint32_t claim = static_cast<uint32_t>(Claim_GetSectionB(btype->claimB)) + 8u * obj->claimSlot;
        ClaimIcon_GetColor(port::load<uint16_t>(claim + 2u), level, colour);
        ps2f scale;
        if (level == 1)
        {
            const int phase = globals::frameCounter.get() % 60;
            scale = fromInt(phase < 31 ? phase + 70 : 130 - phase) / ps2f(100.0f);
        }
        else
        {
            const int phase = globals::frameCounter.get() % 20;
            scale = fromInt(phase < 11 ? phase + 50 : 70 - phase) / ps2f(60.0f);
        }
        colour->x = colour->x * scale;
        colour->y = colour->y * scale;
        colour->z = colour->z * scale;
        colour->w = colour->w;
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

    // After entry `index` left the list (-1: none did): pulls the two list positions back over it, clamps
    // them to the list and refreshes each entry's claim section.
    void Applicants_ScrollTo(GuestPtr<Applicants> applicants, int index)
    {
        if (index != -1)
        {
            if (index < applicants->top)
                --applicants->top;
            if (index < applicants->cursor)
                --applicants->cursor;
        }
        if (applicants->count == 0)
            return;
        const auto clamp = [&](int value)
        {
            const int last = applicants->count - 1;
            return value < 0 ? 0 : (last < value ? last : value);
        };
        applicants->cursor = clamp(applicants->cursor);
        applicants->top = clamp(applicants->top);
        if (applicants->unk_2B4 == ps2f(0.0f))
            applicants->unk_2BC = 1;
        const GuestPtr<ApplicantEntry> entries = GuestPtr<ApplicantEntry>::of(applicants->entries[0]);
        const GuestPtr<uint16_t> claims = GuestPtr<uint16_t>::of(applicants->claims[0]);
        for (int i = 0; i < applicants->count; ++i)
            claims[i] = GuestPtr<BuildingType>{globals::buildingTypeAddress(entries[i].type)}->claimC;
    }

    // Places the applicant card at `point`: both card models there (+0x30), and the card text positioned and
    // sized from the point's x, y and depth (z) through fixed factors.
    void Applicants_PlaceAt(GuestPtr<Applicants> applicants, GuestPtr<ps2f> point)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            port::store<float>(applicants->cardModelA.address + 0x30u + 4u * axis, static_cast<float>(point[axis]));
            port::store<float>(applicants->cardModelB.address + 0x30u + 4u * axis, static_cast<float>(point[axis]));
        }
        const ps2f depth = point[2];
        const auto scaled = [](float factor, ps2f value) { return sdk::fptosi(ps2f(factor) * value); };
        const int x = scaled(144.0f, point[0]) + 0x77E;
        const int left = (x + scaled(45.0f, depth)) << 4;
        const int y = scaled(72.0f, point[1]) + 0x7D6;
        const int top = (y - scaled(7.1f, depth)) << 4;
        const int charWidth = 0xC0 - scaled(16.8f, depth);
        const int charHeight = 0xB0 - scaled(16.0f, depth);
        const int pitchX = 0xB0 - scaled(14.7f, depth);
        const int pitchY = 0xC0 - scaled(16.3f, depth);
        TextBox_SetPosAndSize(applicants->cardText, left, top, charWidth, charHeight, pitchX, pitchY, -0x100000);
    }

    // Removes entry `index`, moving the later ones up, and frees the last slot.
    void Applicants_RemoveAt(GuestPtr<Applicants> applicants, int index)
    {
        const GuestPtr<ApplicantEntry> entries = GuestPtr<ApplicantEntry>::of(applicants->entries[0]);
        int i = index;
        for (; i < applicants->count - 1; ++i)
            entries[i] = entries[i + 1];
        entries[i].state = 0;
        --applicants->count;
        Applicants_ScrollTo(applicants, index);
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
        wants->count = 0;
        wants->wordsUsed = 0;
        return wants;
    }

    // How a want stands: 2 when it no longer holds (the family left, the building's claim changed, or its
    // meter is not above the low threshold), 1 when the building has flag 0x48 (the claim is met), 3 when the
    // meter is above the high threshold and the family has not phoned yet, else 0.
    int Wants_CheckEntry(GuestPtr<WantsWin>, GuestPtr<CityScene> city, GuestPtr<uint16_t> entry)
    {
        const GuestPtr<Want> want{entry.address};
        if (Family_InCity(want->family) == 0)
            return 2;
        const GuestPtr<MapObject> obj = Map_GetRecord(city->map, want->object);
        if (obj->claimSlot != want->claimSlot)
            return 2;
        if ((obj->flags & 0x48u) != 0)
            return 1;
        const int meter = obj->claimMeter;
        const GuestPtr<ClaimThresholds> thresholds = globals::claimThresholds();
        if (!(thresholds->low < meter))
            return 2;
        if (want->called != 0)
            return 0;
        return thresholds->high < meter ? 3 : 0;
    }

    // Every 121st call, checks each want not held (mm_Wants_CheckEntry): one that no longer holds is removed,
    // a met one is removed once its family gets a phone call queued, and for a pressing one the family phones
    // (once). Removing closes the gap in the data area (four words) and in the index, whose later offsets move
    // down by four.
    void Wants_Prune(GuestPtr<WantsWin> wants, GuestPtr<CityScene> city)
    {
        if (globals::wantsPruneStarted.get() == 0)
        {
            globals::wantsPruneTimer.set(0);
            globals::wantsPruneStarted.set(1);
        }
        globals::wantsPruneTimer.set(globals::wantsPruneTimer.get() + 1);
        if (globals::wantsPruneTimer.get() < 121)
            return;
        globals::wantsPruneTimer.set(0);
        const auto remove = [&](int i) {
            for (int k = wants->index[i].offset; k + 4 < 0x100; ++k)
                wants->data[k] = wants->data[k + 4];
            wants->wordsUsed = static_cast<uint16_t>(wants->wordsUsed - 4);
            for (int k = i; k + 1 < 64; ++k)
            {
                wants->index[k] = wants->index[k + 1];
                if (wants->index[k].offset != 0)
                    wants->index[k].offset = static_cast<uint16_t>(wants->index[k].offset - 4);
            }
            wants->count = static_cast<uint16_t>(wants->count - 1);
        };
        for (int i = 0; i < wants->count; ++i)
        {
            if (wants->index[i].held != 0)
                continue;
            const GuestPtr<Want> want{wants.address + 0x104u + 4u * wants->index[i].offset};
            const int state = Wants_CheckEntry(wants, city, GuestPtr<uint16_t>{want.address});
            const auto phone = [&] { return PhoneCall_Post(city->phoneCalls, want->person, want->family, want->member, 0) != 0; };
            if (state == 2)
                remove(i--);
            else if (state == 1)
            {
                if (phone())
                    remove(i--);
            }
            else if (state == 3 && phone())
                want->called = 1;
        }
    }

    // The wants window: the list, its model (from 0x1F3B04) placed at 0x1B0900, and cleared state.
    GuestPtr<WantsWin> WantsWin_ctor(GuestPtr<WantsWin> wants)
    {
        Wants_ctor(wants);
        wants->model = GuestPtr<A3DObject>{port::load<uint32_t>(0x001F3B04u)};
        wants->unk_51C = 0;
        wants->matrix = GuestPtr<Vec4>{wants->model.address + 0x10u};
        const GuestPtr<Vec4> place{0x001B0900u};
        wants->matrix[3].x = place->x;
        wants->matrix[3].y = place->y;
        wants->matrix[3].z = place->z;
        wants->unk_528 = 0;
        for (int32_t &value : wants->unk_508)
            value = 0;
        wants->unk_52C = 0;
        wants->unk_538 = 10;
        wants->unk_518 = 0;
        return wants;
    }

    // Wasteland plots get a random look, kept in the bytes other buildings use for their claim (+0x10..+0x16).
    void BldgWasteland_InitRecord(GuestPtr<BuildingType>, GuestPtr<MapObject> obj)
    {
        const auto random = [] { return static_cast<int32_t>(Random_Next()); };
        const GuestPtr<uint8_t> look{obj.address + 0x10u};
        look[0] = static_cast<uint8_t>(random());
        look[1] = static_cast<uint8_t>(random());
        look[2] = static_cast<uint8_t>(random() % 4);
        look[3] = 0;
        look[4] = 0;
        look[5] = static_cast<uint8_t>(random() % 3);
        look[6] = static_cast<uint8_t>(random() % 10);
    }

    // ---- building drawing and road links

    namespace
    {
        GuestPtr<Vec4> layerMatrix(uint32_t layer) { return GuestPtr<Vec4>{globals::kBuildingMatrices + 0x40u * layer}; }
        int blockStepX(int dir) { return port::load<int32_t>(globals::kBlockStepX + 4u * static_cast<uint32_t>(dir)); }
        int blockStepZ(int dir) { return port::load<int32_t>(globals::kBlockStepZ + 4u * static_cast<uint32_t>(dir)); }

        // Straight road pieces (variants 5 and 10) continue a road; anything else ends it.
        bool isStraightRoad(uint16_t variant) { return variant == 5 || variant == 10; }
    }

    // Leveled ground: mesh +0x160 of the type, when the object's flag 1 is set.
    void BldgLeveled_Draw(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene>, GuestPtr<MapObject> obj, int, int flags,
                          ps2f x, ps2f y, ps2f z)
    {
        if ((obj->flags & 1u) == 0)
            return;
        const GuestPtr<Vec4> matrix = layerMatrix(static_cast<uint32_t>(layer));
        matrix[3].x = x;
        matrix[3].y = y;
        matrix[3].z = z;
        Instance_DrawMesh(globals::cityPartsModel.get(), matrix, GuestPtr<Vec4>{globals::kDefaultColour},
                          port::load<int32_t>(btype.address + 0x160u), flags);
    }

    // Wasteland: one of the weed and rock meshes (row look[4] - 1, column look[5]) under one of four matrices
    // (look[2]), nudged by look[0] and look[1] / 400 (see mm_BldgWasteland_InitRecord). Nothing while look[4]
    // is 0.
    void BldgWasteland_Draw(GuestPtr<BuildingType>, int, GuestPtr<CityScene>, GuestPtr<MapObject> obj, int, int flags,
                            ps2f x, ps2f y, ps2f z)
    {
        const GuestPtr<uint8_t> look{obj.address + 0x10u};
        if (look[4] == 0)
            return;
        layerMatrix(look[2])[3].x = x + fromInt(look[0]) / ps2f(400.0f);
        layerMatrix(look[2])[3].y = y;
        layerMatrix(look[2])[3].z = z + fromInt(look[1]) / ps2f(400.0f);
        Instance_DrawMesh(globals::wastelandModel.get(), layerMatrix(look[2]), GuestPtr<Vec4>{globals::kDefaultColour},
                          look[5] + (look[4] - 1) * 3, flags);
    }

    // A straight road within three blocks of (x, z), searching outwards. Each direction is given up at the
    // first walkable block that is not straight road, and the search of a ring stops at the first direction
    // given up. Returns 1 with the direction in `spot`.
    int BldgWasteland_FindSpot(GuestPtr<BuildingType>, GuestPtr<CityScene> city, int, uint32_t x, uint32_t z, GuestPtr<uint32_t> spot)
    {
        const int bx = static_cast<int>(x & 0xFFFEu), bz = static_cast<int>(z & 0xFFFEu);
        uint32_t open = 0xF;
        for (int ring = 0; ring < 4; ++ring)
            for (int dir = 0; dir < 4; ++dir)
            {
                const uint32_t bit = 1u << dir;
                if ((open & bit) == 0)
                    break;
                const int tx = bx + ring * blockStepX(dir), tz = bz + ring * blockStepZ(dir);
                if (Map_TileFlag10(city->map, tx, tz) == 0)
                    continue;
                if (isStraightRoad(Map_GetTileObject(city->map, tx, tz)->variant))
                {
                    *spot = static_cast<uint32_t>(dir);
                    return 1;
                }
                open &= ~bit;
            }
        return 0;
    }

    // Wasteland each tick. A marked one (+0x14) loses its mark unless both triangles of its tile face straight
    // down within 0.96 (VU0 cross products of the corner edges). The tile and its four neighbours are counted
    // above the water level: all five plot colour 0 on the minimap, some 8. With none, a roll of 6..10 out of
    // 32 by a straight road (mm_BldgWasteland_FindSpot) whose block has flag 0x10 or 8 places a type 3 object
    // here, and the colour goes by the mark: 0 gives 1, 1, 3 and 7 up give 2, the others 6. Returns 0.
    int BldgWasteland_Tick(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        port::GuestFrame<0x120> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<ps2f> corners{sp + 0x70u};
        const GuestPtr<uint32_t> spot{sp + 0x11Cu};
        const int x = obj->tile & 0x7F, z = obj->tile >> 7;
        Map_GetTileVertex(city->map, obj->tile, corners);
        if (static_cast<uint8_t>(obj->entranceTile) != 0)
        {
            using namespace vu0;
            Map_GetTileVertex(city->map, obj->tile, corners);
            const ps2f half = 0.5f, centre = 64.0f;
            const ps2f cx = half + (fromInt(x) - centre);
            const ps2f cz = (fromInt(128 - z) - centre) - half;
            const ps2f left = cx - half, right = half + cx, near = half + cz, far = cz - half;
            const auto corner = [&](ps2f px, int i, ps2f pz) {
                return _mm_setr_ps(static_cast<float>(px), static_cast<float>(corners[i]), static_cast<float>(pz), 1.0f);
            };
            const __m128 a = corner(left, 0, near), b = corner(right, 1, near), c = corner(right, 2, far), d = corner(left, 3, far);
            const auto difference = [](__m128 to, __m128 from) {
                vf(4) = to;
                vf(5) = from;
                set(vf(4), sub(vf(4), vf(5)), XYZW);
                return vf(4);
            };
            const auto normal = [](__m128 u, __m128 v) {
                vf(1) = u;
                vf(2) = v;
                set(acc(), mul(yzx(vf(1)), zxy(vf(2))), XYZ);
                set(vf(3), sub(acc(), mul(yzx(vf(2)), zxy(vf(1)))), XYZ);
                set(vf(4), mul(vf(3), vf(3)), XYZW);
                set(vf(4), add(vf(4), lane(vf(4), 1)), X);
                set(vf(4), add(vf(4), lane(vf(4), 2)), X);
                q() = rsqrt(vu0::x(vf(4)));
                set(vf(4), mul(vf(3), _mm_set1_ps(q())), XYZ);
                return ps2f(vu0::x(lane(vf(4), 1)));
            };
            const __m128 ab = difference(a, b), cb = difference(c, b), cd = difference(c, d);
            const ps2f firstY = normal(ab, cb);
            const ps2f secondY = normal(cb, cd);
            const ps2f steep = -0x1.EB851Ep-1f;
            if (!(firstY <= steep) || !(secondY <= steep))
                port::store<uint8_t>(obj.address + 0x14u, 0);
        }

        Map_GetTileHeightWord(city->map, obj->tile);
        int above = 0;
        const auto count = [&](ps2f height) {
            if (!(height <= ps2f(city->waterLevel)))
                ++above;
        };
        count(Map_GetTileHeightWord(city->map, obj->tile));
        count(Map_GetHeightAt(city->map, x + 1, z));
        count(Map_GetHeightAt(city->map, x - 1, z));
        count(Map_GetHeightAt(city->map, x, z + 1));
        count(Map_GetHeightAt(city->map, x, z - 1));
        int colour = 0;
        if (above != 5)
        {
            colour = 8;
            if (above == 0)
            {
                const int roll = static_cast<int32_t>(Random_Next()) & 0x1F;
                if (roll >= 6 && roll < 11 && BldgWasteland_FindSpot(btype, city, static_cast<int>(obj.address), static_cast<uint32_t>(x),
                                                                     static_cast<uint32_t>(z), spot) != 0)
                {
                    const int sx = x + port::load<int32_t>(0x001B1770u + 4u * *spot);
                    const int sz = z + port::load<int32_t>(0x001B1780u + 4u * *spot);
                    if (Map_TileFlag10(city->map, sx, sz) != 0 || Map_TileFlag08(city->map, sx, sz) != 0)
                        Map_PlaceObject(city->map, 0, 3, x, z, static_cast<int>(city.address));
                }
                const uint8_t mark = static_cast<uint8_t>(obj->entranceTile);
                colour = mark == 1 || mark == 3 || mark >= 7 ? 2 : (mark != 0 ? 6 : 1);
            }
        }
        Minimap_Plot(city->minimap, obj->tile, btype->size, colour, 0);
        return 0;
    }

    // The walkable tile next to a building's footprint (size tiles from (x, z)), trying the four sides at
    // distance 0, then 1 and 2 blocks out. Returns its object's tile and the side, or 0x4000 and side 0.
    uint16_t BldgType_FindEntrance(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, int x, int z, GuestPtr<int32_t> side)
    {
        for (int ring = 0; ring < 3; ++ring)
            for (int s = 0; s < 4; ++s)
            {
                int tx = x + ring * blockStepX(s), tz = z + ring * blockStepZ(s);
                if (s == 0)
                    tz -= 1;
                else if (s == 1)
                    tx += btype->size;
                else if (s == 2)
                    tz += btype->size;
                else
                    tx -= 1;
                if (Map_TileFlag10(city->map, tx, tz) != 0)
                {
                    *side = s;
                    return Map_GetTileObject(city->map, tx, tz)->tile;
                }
            }
        *side = 0;
        return 0x4000;
    }

    // A road piece's links (+0x10, one tile per direction, 0x4000 = none): for each direction its shape opens
    // to (bit d of the variant), the first road block along it that is not straight road.
    void BldgRoad_FindNeighbours(GuestPtr<BuildingType>, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int x, int z)
    {
        const GuestPtr<uint16_t> links{obj.address + 0x10u};
        for (int dir = 0; dir < 4; ++dir)
        {
            links[dir] = 0x4000;
            if ((obj->variant & (1u << dir)) == 0)
                continue;
            int tx = x, tz = z;
            for (;;)
            {
                tz += blockStepZ(dir);
                tx += blockStepX(dir);
                const uint32_t tile = static_cast<uint32_t>(tx + tz * 0x80);
                if (Map_TileIndexFlag10(city->map, tile) == 0)
                    break;
                const GuestPtr<MapObject> next = Map_GetTileFirstObject(city->map, static_cast<int>(tile));
                if (next->variant == 0)
                    break;
                if (!isStraightRoad(next->variant))
                {
                    links[dir] = next->tile;
                    break;
                }
            }
        }
    }

    // Whether a building of type flag 8 stands on the blocks beyond `side` of a footprint, up to two blocks
    // out; a walkable tile in between ends the search.
    int BldgType_FindEntrance2(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, int x, int z, int side)
    {
        for (int ring = 0; ring < 3; ++ring)
        {
            int tz = z + ring * blockStepZ(side), tx = x + ring * blockStepX(side);
            if (side == 3)
                tx -= 1;
            else if (side == 2)
                tz += btype->size;
            else if (side == 1)
                tx += btype->size;
            else if (side == 0)
                tz -= 1;
            if (Map_TileFlag10(city->map, tx, tz) != 0)
                return 0;
            if ((globals::buildingTypeFlags(Map_GetTileObject(city->map, tx, tz)->type) & 8u) != 0)
                return 1;
        }
        return 0;
    }

    // A field is worked from 7:00 to 15:59 on ordinary days (weather 0 or 1, not a holiday): its meter (+0x10)
    // drops by 1 (8 when its family is not in the city) and every person on it who is headed for it adds 6,
    // or 25 once the field is established (+0x12); at 120 it becomes established with the meter at 250.
    // Marks the field on the minimap and returns 1 when the meter has fallen below -49.
    int BldgField_Tick(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        if ((city->flags & 2u) != 0)
            return 0;
        if (city->weatherToday != 0 && city->weatherToday != 1)
            return 0;
        const int hour = city->clock / 3600;
        if (hour < 7 || hour >= 16)
            return 0;
        port::GuestLocal<uint16_t> cursor;
        *cursor = obj->firstPerson;
        int16_t &meter = reinterpret_cast<int16_t &>(obj->claimMeter);
        meter = static_cast<int16_t>(meter - (Family_InCity(obj->family) != 0 ? 1 : 8));
        for (;;)
        {
            const GuestPtr<MapPerson> person = People_Next(city->map, cursor);
            if (!person)
                break;
            if (person->target != obj->id)
                continue;
            meter = static_cast<int16_t>(meter + (obj->claimSlot != 0 ? 25 : 6));
            if (meter >= 120)
            {
                obj->claimSlot = 1;
                meter = 250;
                break;
            }
        }
        Minimap_Plot(city->minimap, obj->tile, btype->size, 1, 0);
        return meter < -49 ? 1 : 0;
    }

    namespace
    {
        // Street lamps light up at 18:00 after three flickers (18:00:01-29, 18:01:21-39, 18:02:01-19, on from
        // 18:02:41) and stay on until 5:00. `time` is seconds of the day.
        bool streetLampLit(int time)
        {
            return (time > 0xFD20 && time < 0xFD3E) || (time > 0xFD70 && time < 0xFD84) || (time > 0xFD98 && time < 0xFDAC) ||
                   time > 0xFDC0 || time < 18000;
        }
    }

    // A road piece: its mesh under the layer 0 matrix. Street lamps (four meshes from type +0x160) are drawn
    // lit (colour w word 12, object flag 0x10) after dark, each block a little later than the last; traffic
    // lights (five meshes from +0x164) are drawn lit as two signal meshes chosen by the light phase (city
    // +0xC0, 0..3) from the table at 0x1B1540, the second under the layer 3 matrix.
    void BldgRoad_Draw(GuestPtr<BuildingType> btype, int, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                       ps2f x, ps2f y, ps2f z)
    {
        const GuestPtr<Vec4> matrix = layerMatrix(0);
        matrix[3].x = x;
        matrix[3].y = y;
        matrix[3].z = z;
        if (obj->mesh == 0)
            return;
        port::GuestLocal<Vec4> colour;
        *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
        const auto drawLit = [&] { port::store<int32_t>(colour.ptr().address + 0xCu, 12); };
        obj->flags &= 0xFFEFu;
        const int mesh = obj->mesh;
        const int lamps = port::load<int32_t>(btype.address + 0x160u);
        if (mesh >= lamps && mesh <= lamps + 3)
        {
            const int tile = obj->tile;
            int time = city->clock + ((tile % 128 + tile / 128) / 2) * 60;
            if (time >= 86400)
                time -= 86400;
            if (streetLampLit(time))
            {
                drawLit();
                obj->flags |= 0x10u;
            }
            Instance_DrawMesh(globals::cityPartsModel.get(), matrix, colour, obj->mesh, flags);
            return;
        }
        const auto lights = [&] { return port::load<int32_t>(btype.address + 0x164u); };
        if (mesh < lights() || mesh > lights() + 4)
        {
            Instance_DrawMesh(globals::cityPartsModel.get(), matrix, colour, mesh, flags);
            return;
        }
        drawLit();
        const uint32_t signals = 0x001B1540u + 4u * static_cast<uint32_t>(city->trafficPhase * 2 + (obj->mesh - lights()) * 8);
        Instance_DrawMesh(globals::cityPartsModel.get(), matrix, colour, lights() + port::load<int32_t>(signals), flags);
        const GuestPtr<Vec4> second = layerMatrix(3);
        second[3].x = x;
        second[3].y = y;
        second[3].z = z;
        Instance_DrawMesh(globals::cityPartsModel.get(), second, colour, lights() + port::load<int32_t>(signals + 4u), flags);
    }

    // A road block each tick. Its shape (variant) gets bit d for each direction d whose next block is road
    // (Map_TileFlag10) and its links are refreshed. Unless all four of its tiles are at or below height 0, they
    // are flattened to 0 and the ground from one tile before to three after is rebuilt. The mesh follows the
    // shape: traffic lights (type +0x164, +0 to +4) at crossings and T-junctions; on straight road the slope
    // mesh (+0x168, +1 along x) where the ground on either side rises more than 0.8 above the road, else a
    // street lamp (+0x160, +0/+1 along z, +2/+3 along x) on every eighth tile and the one two after it; none
    // on bends and dead ends. The block is then plotted on the minimap. Returns 0.
    int BldgRoad_Tick(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        const int x = obj->tile & 0x7F, z = obj->tile >> 7;
        int shape = 0;
        for (int dir = 0; dir < 4; ++dir)
            if (Map_TileFlag10(city->map, x + blockStepX(dir), z + blockStepZ(dir)) != 0)
                shape += port::load<int32_t>(globals::kBlockDirBit + 4u * static_cast<uint32_t>(dir));
        obj->variant = static_cast<uint16_t>(shape);
        BldgRoad_FindNeighbours(btype, city, obj, x, z);
        const ps2f level = 0.0f;
        if (!(Map_GetHeightAt(city->map, x, z) <= level && Map_GetHeightAt(city->map, x + 1, z) <= level &&
              Map_GetHeightAt(city->map, x, z + 1) <= level && Map_GetHeightAt(city->map, x + 1, z + 1) <= level))
        {
            Map_SetAreaHeight(city->map, x, z, 1, level);
            Map_SetAreaHeight(city->map, x + 1, z, 1, level);
            Map_SetAreaHeight(city->map, x, z + 1, 1, level);
            Map_SetAreaHeight(city->map, x + 1, z + 1, 1, level);
            port::GuestLocal<Vec4> rectSlot; // four int32 (x0, z0, x1, z1) on the guest stack
            const GuestPtr<int32_t> rect{rectSlot.ptr().address};
            rect[0] = x - 1;
            rect[1] = z - 1;
            rect[2] = x + 3;
            rect[3] = z + 3;
            Map_UpdateAreaGeometry(city->map, rect);
        }
        obj->mesh = 0;
        const int lamps = port::load<int16_t>(btype.address + 0x160u);
        const int lights = port::load<int16_t>(btype.address + 0x164u);
        const int slope = port::load<int16_t>(btype.address + 0x168u);
        const ps2f rise = 0.8f;
        switch (obj->variant)
        {
        case 0xF:
            obj->mesh = static_cast<uint16_t>(lights);
            break;
        case 0xE:
            obj->mesh = static_cast<uint16_t>(lights + 1);
            break;
        case 0xD:
            obj->mesh = static_cast<uint16_t>(lights + 2);
            break;
        case 0xB:
            obj->mesh = static_cast<uint16_t>(lights + 3);
            break;
        case 0x7:
            obj->mesh = static_cast<uint16_t>(lights + 4);
            break;
        case 0x5: // along z
        {
            const ps2f road = Map_GetTileHeightWord(city->map, obj->tile);
            const ps2f before = Map_GetHeightAt(city->map, x - 1, z);
            const ps2f after = Map_GetHeightAt(city->map, x + btype->size, z);
            if (!(before - road <= rise) || !(after - road <= rise))
                obj->mesh = static_cast<uint16_t>(slope);
            else if (z % 8 == 0)
                obj->mesh = static_cast<uint16_t>(lamps);
            else if (z % 8 == 2)
                obj->mesh = static_cast<uint16_t>(lamps + 1);
            break;
        }
        case 0xA: // along x
        {
            const ps2f road = Map_GetTileHeightWord(city->map, obj->tile);
            const ps2f before = Map_GetHeightAt(city->map, x, z - 1);
            const ps2f after = Map_GetHeightAt(city->map, x, z + btype->size);
            if (!(before - road <= rise) || !(after - road <= rise))
                obj->mesh = static_cast<uint16_t>(slope + 1);
            else if (x % 8 == 0)
                obj->mesh = static_cast<uint16_t>(lamps + 2);
            else if (x % 8 == 2)
                obj->mesh = static_cast<uint16_t>(lamps + 3);
            break;
        }
        default:
            break;
        }
        Minimap_Plot(city->minimap, obj->tile, btype->size, 3, 0);
        return 0;
    }

    // A building part each tick (type slot +0x10). The main part (its tile's first object) finds its entrance
    // tile (+0x14) and side; unless the type has flag 0x20 and the second entrance test fails, the side is then
    // set from where the footprint sits in the 4-tile grid of road blocks, and kept as the tile's kind. The
    // ground under the footprint is levelled: above 0 to 0.1, else onto a multiple of 0.5 (with fmodf), and the
    // geometry around it rebuilt when that changed it. Other parts take the main part's entrance tile. The
    // entrance part (flag 0x20) clears +0x17 from 23:00:01, between 6:00 and 7:00 drops flag 0x100 (minimap
    // colour 5), and returns mm_BldgClaims_Evaluate's result; other parts count +0x18 up from 7:00 to 18:59
    // and at 24 are flagged 0x120 and start again. Last, the minimap colour by the claim meter's level (2: 10,
    // 1: 9, else 5). The original's frame is reserved whole: the side and the rectangle are its slots.
    int Bldg_Tick(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int timeFlags)
    {
        port::GuestFrame<0xB0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<int32_t> side{sp + 0xACu};
        const int x = obj->tile % 128, z = obj->tile / 128;
        int result = 0;
        const GuestPtr<MapObject> first = Map_GetTileFirstObject(city->map, obj->tile);
        if (first->id != obj->id)
            obj->entranceTile = first->entranceTile;
        else
        {
            obj->entranceTile = BldgType_FindEntrance(btype, city, x, z, side);
            if ((btype->flags & 0x20u) == 0 || BldgType_FindEntrance2(btype, city, x, z, *side) != 0)
            {
                const int size = btype->size;
                if ((*side & 1) != 0)
                {
                    const int z4 = z % 4;
                    *side = z4 == 2 ? 0 : (z + size) % 4 == 1 ? 2 : z4 == 1 ? 0 : 2;
                }
                else
                {
                    const int x4 = x % 4;
                    *side = x4 == 2 ? 1 : (x + size) % 4 == 1 ? 3 : x4 == 1 ? 1 : 3;
                }
            }
            Map_SetTileKind(city->map, obj->tile, static_cast<uint32_t>(*side));
            const ps2f height = Map_GetTileHeightWord(city->map, obj->tile);
            ps2f level = height;
            if (!(height <= ps2f(0.0f)))
                level = 0.1f;
            else
            {
                const ps2f frac = sdk::fmodf(height, 0.5f);
                if (!(ps2f(0.0f) == frac))
                {
                    if (!(frac <= ps2f(0.1f)))
                        level = height - ps2f(0.1f);
                    else if (frac <= ps2f(-0.4f))
                        level = (height - frac) - ps2f(0.5f);
                    else
                        level = height - frac;
                }
            }
            if (!(level == height))
            {
                Map_SetAreaHeight(city->map, x, z, btype->size, level);
                const GuestPtr<int32_t> rect{sp + 0x90u}; // x0, z0, x1, z1
                rect[0] = x - 1;
                rect[1] = z + btype->size - 1;
                rect[2] = rect[0] + btype->size + 2;
                rect[3] = rect[1] + btype->size + 2;
                Map_UpdateAreaGeometry(city->map, rect);
            }
        }
        if ((obj->flags & 0x20u) != 0)
        {
            if (!(city->clock < 82801))
                port::store<uint8_t>(obj.address + 0x17u, 0);
            const uint16_t flags = obj->flags;
            if ((flags & 0x100u) != 0 && city->clock < 25200 && !(city->clock < 21600))
            {
                obj->flags = static_cast<uint16_t>(flags & 0xFEFFu);
                Minimap_Plot(city->minimap, obj->tile, btype->size, 5, 0);
            }
            result = BldgClaims_Evaluate(btype, city, obj, timeFlags);
        }
        else if (!(city->clock < 25200) && city->clock < 68400)
        {
            const uint32_t counter = obj.address + 0x18u;
            port::store<uint8_t>(counter, static_cast<uint8_t>(port::load<uint8_t>(counter) + 1));
            if (port::load<uint8_t>(counter) >= 24)
            {
                obj->flags |= 0x120u;
                port::store<uint8_t>(counter, 0);
            }
        }
        const int meter = ClaimMeter_Level(obj->claimMeter);
        if (meter == 2)
            Minimap_Plot(city->minimap, obj->tile, btype->size, 10, 0);
        else if (meter == 1)
            Minimap_Plot(city->minimap, obj->tile, btype->size, 9, 1);
        else
            Minimap_Plot(city->minimap, obj->tile, btype->size, 5, 1);
        return result;
    }

    // Offices tick like any building; a part that is no longer its tile's first object (and not the
    // entrance) is flagged 0x120 and its variant byte cleared.
    int BldgOffice_Tick(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int timeFlags)
    {
        const int result = Bldg_Tick(btype, city, obj, timeFlags);
        if ((obj->flags & 0x20u) == 0 && obj->id != Map_GetTileFirstObject(city->map, obj->tile)->id)
        {
            obj->flags |= 0x120u;
            port::store<uint8_t>(obj.address + 0x18u, 0);
        }
        return result;
    }

    // Housing ticks like any building; its entrance recounts the units, other parts that are no longer
    // their tile's first object are flagged 0x120 with the variant byte cleared.
    int BldgHousing_Tick(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int timeFlags)
    {
        const int result = Bldg_Tick(btype, city, obj, timeFlags);
        if ((obj->flags & 0x20u) != 0)
            BldgHousing_CountUnits(btype, city, obj);
        else if (obj->id != Map_GetTileFirstObject(city->map, obj->tile)->id)
        {
            obj->flags |= 0x120u;
            port::store<uint8_t>(obj.address + 0x18u, 0);
        }
        return result;
    }

    // Ports level their 3x3 block to height 2.5 and rebuild the ground around it, the rectangle reaching
    // four tiles beyond the building (offset by its size towards +z, as in the original).
    int BldgPort_Tick(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int timeFlags)
    {
        const int result = Bldg_Tick(btype, city, obj, timeFlags);
        const int x = obj->tile & 0x7F, z = obj->tile >> 7;
        Map_SetAreaHeight(city->map, x, z, 3, 2.5f);
        port::GuestLocal<Vec4> rectSlot; // four int32 (x0, z0, x1, z1) on the guest stack
        const GuestPtr<int32_t> rect{rectSlot.ptr().address};
        rect[0] = x - 4;
        rect[1] = z + btype->size - 4;
        rect[2] = rect[0] + btype->size + 8;
        rect[3] = rect[1] + btype->size + 8;
        Map_UpdateAreaGeometry(city->map, rect);
        return result;
    }

    // Shops and offices: tinted by their claim icon colour when the city shows claims (flag 1). The entrance
    // is lit (colour w word 12, object flag 0x10) outside 4:00..16:59 while open by the building's own
    // shifted clock, and drawn from the entrance model, with the closed-sign mesh after hours for type flag
    // 0x1000; other parts draw their variant from the city parts model.
    namespace
    {
        GuestPtr<BuildingType> typeOf(GuestPtr<MapObject> obj)
        {
            return GuestPtr<BuildingType>{port::load<uint32_t>(globals::buildingTypeTable.get() + 4u * obj->type)};
        }

        // The draw slots' instance colour: the claim icon tint while the city shows claims (flag 1), else untinted.
        void buildingColour(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, GuestPtr<Vec4> colour)
        {
            if ((city->flags & 1u) != 0)
                BldgClaims_DrawIcon(btype, obj, colour);
            else
                *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
        }

        // A lit instance: colour w word 12.
        void setLit(GuestPtr<Vec4> colour) { port::store<int32_t>(colour.address + 0xCu, 12); }

        // Business hours lighting: unlit 4:00..16:59 and while closed, by the object's shifted clock.
        bool litAfterDark(int time, int open, int close)
        {
            const bool daytime = time < 0xEF10 && time >= 0x3840;
            return !(daytime || time < open || time >= close);
        }

        // Places the layer's matrix at the building's position.
        GuestPtr<Vec4> placeLayer(int layer, ps2f x, ps2f y, ps2f z)
        {
            const GuestPtr<Vec4> matrix = layerMatrix(static_cast<uint32_t>(layer));
            matrix[3].x = x;
            matrix[3].y = y;
            matrix[3].z = z;
            return matrix;
        }

        // A part that is not the entrance: its variant (bits 3-7 of +0x18) from the city parts model.
        void drawPlainPart(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> matrix, GuestPtr<Vec4> colour, int flags)
        {
            Instance_DrawMesh(globals::cityPartsModel.get(), matrix, colour, btype->firstDrawMesh + ((obj->variant & 0xFFu) >> 3), flags);
        }
    }

    void BldgBusiness_Draw(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                           ps2f x, ps2f y, ps2f z)
    {
        port::GuestLocal<Vec4> colour;
        buildingColour(btype, city, obj, colour);
        const int time = Clock_AddWrap(static_cast<int>(btype.address), city->clock, obj->id);
        const int open = btype->openMinute * 60, close = btype->closeMinute * 60;
        if (litAfterDark(time, open, close) && (obj->flags & 0x20u) != 0)
        {
            setLit(colour);
            obj->flags |= 0x10u;
        }
        else
            obj->flags &= 0xFFEFu;
        const GuestPtr<Vec4> matrix = placeLayer(layer, x, y, z);
        if ((obj->flags & 0x20u) == 0)
        {
            drawPlainPart(btype, obj, matrix, colour, flags);
            return;
        }
        int mesh = btype->entranceDrawMesh;
        if ((btype->flags & 0x1000u) != 0 && (time < open || close < time))
            ++mesh;
        Instance_DrawMesh(globals::entranceModel.get(), matrix, colour, mesh, flags);
    }

    // Ports: like a business, lit after dark while open (hours of the object's own type), drawn 2.5 lower; the
    // entrance from the entrance model.
    void BldgPort_Draw(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                       ps2f x, ps2f y, ps2f z)
    {
        const GuestPtr<BuildingType> type = typeOf(obj);
        port::GuestLocal<Vec4> colour;
        buildingColour(btype, city, obj, colour);
        const int time = Clock_AddWrap(static_cast<int>(btype.address), city->clock, obj->id);
        if (litAfterDark(time, type->openMinute * 60, type->closeMinute * 60))
        {
            setLit(colour);
            obj->flags |= 0x10u;
        }
        else
            obj->flags &= 0xFFEFu;
        const GuestPtr<Vec4> matrix = placeLayer(layer, x, y - ps2f(2.5f), z);
        if ((obj->flags & 0x20u) != 0)
            Instance_DrawMesh(globals::entranceModel.get(), matrix, colour, btype->entranceDrawMesh, flags);
        else
            drawPlainPart(btype, obj, matrix, colour, flags);
    }

    // Theme parks: lit like a port; the entrance is the animated ride model, moved to the building.
    void BldgAmusementPark_Draw(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                                ps2f x, ps2f y, ps2f z)
    {
        const GuestPtr<BuildingType> type = typeOf(obj);
        port::GuestLocal<Vec4> colour;
        buildingColour(btype, city, obj, colour);
        const int time = Clock_AddWrap(static_cast<int>(btype.address), city->clock, obj->id);
        if (litAfterDark(time, type->openMinute * 60, type->closeMinute * 60))
        {
            setLit(colour);
            obj->flags |= 0x10u;
        }
        else
            obj->flags &= 0xFFEFu;
        const GuestPtr<Vec4> matrix = placeLayer(layer, x, y, z);
        if ((obj->flags & 0x20u) == 0)
        {
            drawPlainPart(btype, obj, matrix, colour, flags);
            return;
        }
        const GuestPtr<A3DObject> ride = globals::parkRideModel.get();
        for (int row = 0; row < 4; ++row)
            ride->matrix[row] = matrix[row];
        Instance_DrawModel(ride, colour, 0, flags);
    }

    namespace
    {
        // Office and housing blocks: windows lit from 17:00 to 23:00 while someone is inside.
        void litWhileOccupied(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, GuestPtr<Vec4> colour)
        {
            const int time = Clock_AddWrap(static_cast<int>(btype.address), city->clock, obj->id);
            if (time >= 0xEF10 && time < 82801 && obj->firstPerson != kNoPerson)
                setLit(colour);
        }

        // The highlight frame of a selected block (object flag 1), sized by the type.
        void drawBlockHighlight(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> matrix, GuestPtr<Vec4> colour, int flags)
        {
            if ((obj->flags & 1u) != 0)
                Instance_DrawMesh(globals::cityPartsModel.get(), matrix, colour, btype->size + 0x19, flags);
        }
    }

    // Offices: `part` is the block part being drawn (bits 0-2: 0..4; bit 3, set here for kind 0xC3, adds mesh
    // 0x67 on the parts with a second mesh). Parts choose up to two meshes from the entrance mesh on, varied by
    // the block's position along the street ((tile / size) % 3); a type extra mesh (+0x160) goes with the first.
    // Part 0 of a non-entrance object draws its variant, untinted.
    void BldgOffice_Draw(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, GuestPtr<uint32_t> part,
                         int flags, ps2f x, ps2f y, ps2f z)
    {
        if (btype->kind == 0xC3)
            *part |= 8u;
        int first = 0, second = 0;
        const int entrance = btype->entranceDrawMesh;
        switch (*part & 7u)
        {
        case 0: first = entrance; second = entrance + 3; break;
        case 1: first = entrance; break;
        case 2: first = entrance + 1; break;
        case 3: first = entrance + 1; second = entrance + 2; break;
        case 4: first = entrance + 1; second = entrance + 3; break;
        default: break;
        }
        port::GuestLocal<Vec4> colour;
        buildingColour(btype, city, obj, colour);
        litWhileOccupied(btype, city, obj, colour);
        const GuestPtr<Vec4> matrix = placeLayer(layer, x, y, z);
        if ((*part & 7u) == 0 && (obj->flags & 0x20u) == 0)
        {
            drawPlainPart(btype, obj, matrix, GuestPtr<Vec4>{globals::kDefaultColour}, flags);
            return;
        }
        const int shift = divide(obj->tile, btype->size) % 3 * 4;
        if (first != 0)
        {
            Instance_DrawMesh(globals::blockModel.get(), matrix, colour, first + shift, flags);
            const int extra = port::load<int32_t>(btype.address + 0x160u);
            if (extra != 0)
                Instance_DrawMesh(globals::blockModel.get(), matrix, colour, extra, flags);
        }
        if (second != 0)
        {
            Instance_DrawMesh(globals::blockModel.get(), matrix, colour, second + shift, flags);
            if ((*part & 8u) != 0)
                Instance_DrawMesh(globals::blockModel.get(), matrix, colour, 0x67, flags);
        }
        drawBlockHighlight(btype, obj, matrix, colour, flags);
    }

    // Housing: parts 0..4 take one mesh (parts 3 and 4 two) from the entrance mesh on, offset per unit shape
    // (the high byte of +0x18, table 0x1B14F0) and by 26 per style (the low byte of +0x1A).
    void BldgHousing_Draw(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, GuestPtr<uint32_t> part,
                          int flags, ps2f x, ps2f y, ps2f z)
    {
        const int shape = obj->variant >> 8;
        const int base = btype->entranceDrawMesh + (obj->mesh & 0xFFu) * 26;
        const auto partMesh = [&](int index) { return base + port::load<int8_t>(globals::kHousingPartMesh + 4u * shape + index); };
        int first = -1, second = -1;
        switch (*part & 7u)
        {
        case 0: first = partMesh(0); break;
        case 1: first = partMesh(1); break;
        case 2: first = partMesh(2); break;
        case 3:
        case 4: first = partMesh(2); second = partMesh(3); break;
        default: break;
        }
        port::GuestLocal<Vec4> colour;
        buildingColour(btype, city, obj, colour);
        litWhileOccupied(btype, city, obj, colour);
        const GuestPtr<Vec4> matrix = placeLayer(layer, x, y, z);
        if ((*part & 7u) == 0 && (obj->flags & 0x20u) == 0)
        {
            drawPlainPart(btype, obj, matrix, GuestPtr<Vec4>{globals::kDefaultColour}, flags);
            return;
        }
        if (first != -1)
            Instance_DrawMesh(globals::blockModel.get(), matrix, colour, first, flags);
        if (second != -1)
            Instance_DrawMesh(globals::blockModel.get(), matrix, colour, second, flags);
        drawBlockHighlight(btype, obj, matrix, colour, flags);
    }

    // Whether the tile (x, z) holds a unit of the same housing block: same rule, flag-1 tile, terrain kind and
    // height, and an entrance object (then *isUnit = 1). Returns its unit shape when more than `count` objects
    // come before it on the tile, else 6.
    uint8_t BldgHousing_UnitAt(GuestPtr<BuildingType>, int kind, int x, int z, int count, uint16_t rule, GuestPtr<CityScene> city,
                               GuestPtr<uint32_t> isUnit, ps2f height)
    {
        constexpr uint8_t kNone = 6;
        const GuestPtr<MapObject> obj = Map_GetTileObject(city->map, x, z);
        const GuestPtr<BuildingType> type = typeOf(obj);
        *isUnit = 0;
        if (rule != type->rule)
            return kNone;
        if (Map_TileFlag01(city->map, x, z) == 0)
            return kNone;
        if (kind != Map_GetTileKind(city->map, obj->tile))
            return kNone;
        if (!(height == ps2f(Map_GetTileHeightWord(city->map, obj->tile))))
            return kNone;
        if ((obj->flags & 0x20u) == 0)
            return kNone;
        *isUnit = 1;
        if (count < Map_CountObjectsBefore(city->map, x, z, kNoObject))
            return static_cast<uint8_t>(obj->variant >> 8);
        return kNone;
    }

    // Works out a housing object's unit shape (variant high byte) from the units two tiles away on either side
    // along its terrain kind's axis (table 0x1B1480, 7 x 7), merged with the shape of the tile's first object
    // when they differ (table 0x1B14C0, 6 per row). When the shape changed, the neighbouring units take or give
    // the style (mesh low byte) and are worked out again in turn, each object on their tiles. An object that
    // is not its tile's first takes that one's style.
    void BldgHousing_CountUnits(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        const auto shapeOf = [](GuestPtr<MapObject> o) { return static_cast<uint8_t>(o->variant >> 8); };
        const auto setShape = [](GuestPtr<MapObject> o, uint8_t shape) { port::store<uint8_t>(o.address + 0x19u, shape); };
        const auto styleOf = [](GuestPtr<MapObject> o) { return static_cast<uint8_t>(o->mesh); };
        const auto setStyle = [](GuestPtr<MapObject> o, uint8_t style) { port::store<uint8_t>(o.address + 0x1Au, style); };

        port::GuestFrame<0xF0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<uint32_t> isUnitA{sp + 0xE8u}, isUnitB{sp + 0xECu};
        const uint8_t oldShape = shapeOf(obj);
        const int x = obj->tile & 0x7F, z = obj->tile >> 7;
        const int count = Map_CountObjectsBefore(city->map, x, z, obj->id);
        const int kind = Map_GetTileKind(city->map, obj->tile);
        const ps2f height = Map_GetTileHeightWord(city->map, obj->tile);
        const GuestPtr<BuildingType> own = typeOf(obj);
        int xA = x, zA = z, xB = x, zB = z;
        switch (kind)
        {
        case 0: xA = x - 2, xB = x + 2; break;
        case 1: zA = z - 2, zB = z + 2; break;
        case 2: xA = x + 2, xB = x - 2; break;
        case 3: zA = z + 2, zB = z - 2; break;
        }
        const uint8_t shapeA = BldgHousing_UnitAt(btype, kind, xA, zA, count, own->rule, city, isUnitA, height);
        const uint8_t shapeB = BldgHousing_UnitAt(btype, kind, xB, zB, count, own->rule, city, isUnitB, height);
        setShape(obj, port::load<uint8_t>(0x001B1480u + 7u * shapeB + shapeA));
        const GuestPtr<MapObject> first = Map_GetTileObject(city->map, x, z);
        if (shapeOf(first) != shapeOf(obj))
            setShape(obj, port::load<uint8_t>(0x001B14C0u + 6u * shapeOf(obj) + shapeOf(first)));
        if (shapeOf(obj) != oldShape)
        {
            if (*isUnitA != 0)
            {
                GuestPtr<MapObject> unit = Map_GetTileObject(city->map, xA, zA);
                setStyle(obj, styleOf(unit));
                for (;; unit = Map_GetRecord(city->map, unit->nextOnTile))
                {
                    BldgHousing_CountUnits(own, city, unit);
                    if (unit->nextOnTile == kNoObject)
                        break;
                }
            }
            if (*isUnitB != 0)
            {
                GuestPtr<MapObject> unit = Map_GetTileObject(city->map, xB, zB);
                setStyle(unit, styleOf(obj));
                for (;; unit = Map_GetRecord(city->map, unit->nextOnTile))
                {
                    BldgHousing_CountUnits(own, city, unit);
                    if (unit->nextOnTile == kNoObject)
                        break;
                }
            }
        }
        if (first->id != obj->id)
            setStyle(obj, styleOf(first));
    }

    namespace
    {
        // Claim meter band: 0 at or below the low threshold, 1 up to the high one, 2 above it.
        int claimBand(uint16_t meter)
        {
            const GuestPtr<ClaimThresholds> limits = globals::claimThresholds();
            if (!(limits->low < meter))
                return 0;
            return meter <= limits->high ? 1 : 2;
        }
    }

    // A building's claim, once per evaluation. While the claim is met (flag 8) or its meter maxed out (0x40) the
    // claim timer runs down; when it runs out a met claim just ends, a maxed one is settled (EvalA: returns 1 and
    // lowers the city's pending approval). Otherwise section C is re-checked while the map asks for it, and with
    // `rise` set the meter climbs: by the step, or by a tenth of the timer once that is ten steps or more. Past the
    // low threshold EvalD runs; at 255 the meter maxes out (flag 0x40, the timer set to the hold time). Moving up a
    // band lowers the pending approval (from band 0 only while approval is positive). Slot 0x18 means no claim:
    // EvalB then.
    int BldgClaims_Evaluate(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int rise)
    {
        if ((obj->flags & 0x48u) != 0)
        {
            if (--obj->claimTimer != 0)
                return 0;
            if ((obj->flags & 8u) != 0)
            {
                obj->flags &= 0xFFF7u;
                obj->claimSlot = 0x18;
                return 0;
            }
            BldgClaims_EvalA(btype, city, obj);
            obj->flags &= 0xFFBFu;
            obj->claimSlot = 0x18;
            --city->pendingApproval;
            return 1;
        }
        if (city->map->claimRecheck > 0 && obj->claimSlot != 0x18)
            BldgClaims_EvalC(btype, city, obj);
        if (rise == 0)
            return 0;
        if (obj->claimSlot == 0x18)
        {
            BldgClaims_EvalB(btype, city, obj);
            return 0;
        }
        if ((obj->flags & 0x208u) != 0)
            return 0;
        const int before = claimBand(obj->claimMeter);
        const uint8_t step = globals::claimThresholds()->step;
        const uint16_t increase = obj->claimTimer < step * 10 ? step : static_cast<uint16_t>(obj->claimTimer / 10);
        obj->claimMeter = static_cast<uint16_t>(obj->claimMeter + increase);
        if (globals::claimThresholds()->low < obj->claimMeter)
            BldgClaims_EvalD(btype, city, obj);
        if (obj->claimMeter >= 0xFF)
        {
            obj->flags |= 0x40u;
            obj->claimTimer = globals::claimThresholds()->holdTime;
            obj->claimMeter = 0xFF;
        }
        const int after = claimBand(obj->claimMeter);
        if (before < after && (before != 0 || city->approval > 0))
            --city->pendingApproval;
        return 0;
    }

    // Every frame: slot 0x34 of each building type with the time of day (the ride animations; others do nothing).
    void BldgType_CallAll(int time)
    {
        for (int i = 0; i < globals::buildingTypeCount.get(); ++i)
        {
            const uint32_t btype = port::load<uint32_t>(globals::buildingTypeTable.get() + 4u * static_cast<uint32_t>(i));
            if (btype == 0)
                continue;
            port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(btype + 0x15Cu) + 0x34u), btype, time);
        }
    }

    // A building occupant's colour: set by the occupant slot of the first lit object (flag 0x10) on the person's
    // tile or on the four tiles beside the block (north, east, south, west); otherwise untinted with w 0. (The
    // original also fetches a fifth tile, from past the end of its offset table, and never looks at it.)
    void BldgType_DrawOccupant(GuestPtr<BuildingType> btype, GuestPtr<Vec4> colour, GuestPtr<MapPerson> person, GuestPtr<CityScene> city)
    {
        const int x = sdk::fptosi(ps2f(64.0f) + person->x);
        const int z = sdk::fptosi(ps2f(64.0f) - person->z);
        GuestPtr<MapObject> obj = Map_GetTileObject(city->map, x, z);
        const int tile = obj->tile;
        const int size = btype->size;
        const int stepX[4] = {0, size, 0, -1};
        const int stepZ[4] = {-1, 0, size, 0};
        for (int i = 0;; ++i)
        {
            if ((obj->flags & 0x10u) != 0)
            {
                BldgType_OccupantMatrix(typeOf(obj), colour, GuestPtr<Vec4>{person.address}, obj->tile);
                return;
            }
            if (i == 4)
                break;
            obj = Map_GetTileObject(city->map, (tile & 0x7F) + stepX[i], (tile >> 7) + stepZ[i]);
        }
        *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
        colour->w = 0.0f;
    }

    // The people inside a building: each one's colour (BldgType_DrawOccupant), then its character class's draw
    // slot (+0x10) at its position and heading, in pose 6 (flag 0x40), 1 (running, 0x80) or 0.
    void BldgType_DrawOccupants2(GuestPtr<BuildingType> btype, int, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                                 GuestPtr<MapPerson> people)
    {
        port::GuestLocal<Vec4> colour; // the original's slots: entry sp - 0x10 and - 0x20
        port::GuestLocal<Vec4> position;
        for (GuestPtr<uint16_t> link = GuestPtr<uint16_t>::of(obj->firstPerson); *link != kNoPerson;
             link = GuestPtr<uint16_t>::of(people[*link].nextOnTile))
        {
            const GuestPtr<MapPerson> person = people + *link;
            *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
            const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
            position->x = person->x;
            position->y = person->y;
            position->z = person->z;
            position->w = person->heading;
            int pose = 0;
            if ((person->flags & 0x40u) != 0)
                pose = 6;
            else if ((person->flags & 0x80u) != 0)
                pose = 1;
            BldgType_DrawOccupant(btype, colour, person, city);
            const uint32_t draw = port::load<uint32_t>(cls->vtable.address + 0x10u);
            port::callGuest<void>(draw, cls.address, city.address, person.address, colour.ptr().address, flags, position.ptr().address, pose);
        }
    }

    // Theme park and ranch rides: the animation runs while open (stopping 1000 seconds before closing),
    // finishes its cycle after hours and wraps at the length the model keeps in mesh 0 (+0xC).
    namespace
    {
        void drawRide(GuestPtr<BuildingType> btype, int time, GuestVar<GuestPtr<A3DObject>> model, GuestVar<float> frame)
        {
            const int open = btype->openMinute * 60, close = btype->closeMinute * 60;
            if (open < time && time < close)
            {
                if (time < close - 1000)
                    frame.set(static_cast<float>(ps2f(frame.get()) + ps2f(1.0f)));
            }
            else if (ps2f(frame.get()) != ps2f(0.0f))
                frame.set(static_cast<float>(ps2f(frame.get()) + ps2f(1.0f)));
            if (!model.get())
                return;
            const ps2f length = A3D_GetMeshField0C(model.get(), 0);
            if (!(ps2f(frame.get()) < length))
                frame.set(0.0f);
            Instance_DrawSwayingIfRoom(model.get(), 0, frame.get());
            A3DObject_UpdateMatrices(model.get());
        }
    }

    void BldgAmusementPark_DrawAnim(GuestPtr<BuildingType> btype, int time)
    {
        drawRide(btype, time, globals::parkRideModel, GuestVar<float>{0x001BDBE4u});
    }

    void BldgRanch_DrawAnim(GuestPtr<BuildingType> btype, int time)
    {
        drawRide(btype, time, GuestVar<GuestPtr<A3DObject>>{0x001BDBECu}, GuestVar<float>{0x001BDBF4u});
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

    // The road class adds the map object's (a1) variant to the base value.
    int BldgRoad_Slot1C(GuestPtr<BuildingType> btype, int a1)
    {
        return btype->unk_143 + GuestPtr<MapObject>{static_cast<uint32_t>(a1)}->variant;
    }

    // Collision against a building's mesh: the entrance mesh for entrance objects, else the first mesh plus
    // the object's variant (two meshes per variant).
    namespace
    {
        int collisionMesh(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj)
        {
            if ((obj->flags & 0x20u) != 0)
                return btype->entranceMesh * 2;
            return (btype->firstMesh + ((obj->variant & 0xFFu) >> 3)) * 2;
        }
    }

    int Bldg_CollideA(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                      GuestPtr<uint32_t> hit, ps2f lift)
    {
        return Collide_SegmentA(static_cast<int>(btype.address), collisionMesh(btype, obj), point, matrix, hit, lift);
    }

    int Bldg_CollideB(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                      GuestPtr<uint32_t> hit, GuestPtr<ps2f> distance, ps2f f12)
    {
        return Collide_SegmentB(static_cast<int>(btype.address), collisionMesh(btype, obj), point, matrix, hit, distance, f12);
    }

    // Open hours apply to the building as a whole (no object) and to its entrance; other parts are never open.
    int Bldg_IsOpen(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        if (!obj)
            return BldgType_IsOpenAt(btype, city, obj);
        if ((obj->flags & 0x20u) == 0)
            return 0;
        return BldgType_IsOpenAt(btype, city, obj);
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
    PORT_FN(TownPages_SelectedEntry);
    PORT_FN(TownPages_FillContents);
    PORT_FN(TownPages_Draw);
    PORT_FN(BldgType_IsOpenAt);
    PORT_FN(ClaimIcon_GetColor);
    PORT_FN(Applicants_Find);
    PORT_FN(Applicants_FindFrom);
    PORT_FN(Applicants_Clear);
    PORT_FN(Applicants_ScrollTo);
    PORT_FN(Applicants_RemoveAt);
    PORT_FN(Applicants_PlaceAt);
    PORT_FN(Laptop_CheckOpen);
    PORT_FN(Laptop_Animate);
    PORT_FN(WantsWin_ctor);
    PORT_FN(BldgWasteland_InitRecord);
    PORT_FN(BldgLeveled_Draw);
    PORT_FN(BldgWasteland_Draw);
    PORT_FN(BldgWasteland_FindSpot);
    PORT_FN(BldgWasteland_Tick);
    PORT_FN(BldgType_FindEntrance);
    PORT_FN(BldgRoad_FindNeighbours);
    PORT_FN(BldgRoad_Draw);
    PORT_FN(BldgRoad_Tick);
    PORT_FN(Bldg_Tick);
    PORT_FN(BldgType_FindEntrance2);
    PORT_FN(BldgField_Tick);
    PORT_FN(BldgOffice_Tick);
    PORT_FN(BldgHousing_Tick);
    PORT_FN(BldgPort_Tick);
    PORT_FN(BldgAmusementPark_DrawAnim);
    PORT_FN(BldgRanch_DrawAnim);
    PORT_FN(BldgBusiness_Draw);
    PORT_FN(BldgPort_Draw);
    PORT_FN(BldgAmusementPark_Draw);
    PORT_FN(BldgOffice_Draw);
    PORT_FN(BldgHousing_Draw);
    PORT_FN(BldgHousing_UnitAt);
    PORT_FN(BldgHousing_CountUnits);
    PORT_FN(BldgType_DrawOccupant);
    PORT_FN(BldgType_CallAll);
    PORT_FN(BldgClaims_Evaluate);
    PORT_FN(BldgType_DrawOccupants2);
    PORT_FN(Schedule_PickEntry);
    PORT_FN(Stamina_HungerLevel);
    PORT_FN(Clock_AddWrap);
    PORT_FN(ClaimMeter_Level);
    PORT_FN(BldgClaims_DrawIcon);
    PORT_FN(Applicants_AnyWaiting);
    PORT_FN(Wants_ctor);
    PORT_FN(Wants_CheckEntry);
    PORT_FN(Wants_Prune);
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
    PORT_FN(Bldg_CollideA);
    PORT_FN(Bldg_CollideB);
    PORT_FN(Bldg_IsOpen);

    // ---- building records and collision slots (batch 38)

    void Bldg_InitRecordBase(GuestPtr<BuildingType>, GuestPtr<MapObject> obj) { obj->claimSlot = 0x18; }

    void Bldg_InitRecord(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj)
    {
        Bldg_InitRecordBase(btype, obj);
        obj->entranceTile = 0x4000;
    }

    int Bldg_Slot30(GuestPtr<BuildingType>) { return 1; }

    // Housing blocks take one of three styles by their tile ((tile / 2) % 3).
    void BldgHousing_InitRecord(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj)
    {
        Bldg_InitRecord(btype, obj);
        port::store<uint8_t>(obj.address + 0x1Au, static_cast<uint8_t>((obj->tile / 2) % 3));
    }

    void BldgRoad_InitRecord(GuestPtr<BuildingType>, GuestPtr<MapObject> obj)
    {
        for (uint32_t offset = 0x10u; offset <= 0x16u; offset += 2u)
            port::store<uint16_t>(obj.address + offset, 0x4000);
    }

    // A field's model by its claim: the type's own (+0x143) while a claim is set, else by the claim meter: below 2,
    // 30 and 60, then the full one.
    uint8_t BldgField_Slot1C(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj)
    {
        if (obj->claimSlot != 0)
            return port::load<uint8_t>(btype.address + 0x143u);
        const int16_t meter = static_cast<int16_t>(obj->claimMeter);
        if (meter < 2)
            return 0x11;
        if (meter < 30)
            return 0x16;
        return meter < 60 ? 0x17 : 0x18;
    }

    namespace
    {
        // The variant part of a building's collision mesh (the low byte of +0x18, in steps of 8).
        uint32_t variantMesh(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj)
        {
            return btype->firstMesh + (port::load<uint8_t>(obj.address + 0x18u) >> 3);
        }

        // Housing: part 0 is the block itself (or its entrance), parts 1 and 2 and up take the unit shape's two
        // entrance meshes (signed offsets from the type's entrance mesh, table 0x1B1520 by the shape at +0x19).
        int housingMesh(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, uint32_t part)
        {
            const uint32_t which = part & 7u;
            const uint32_t shape = 0x001B1520u + 2u * port::load<uint8_t>(obj.address + 0x19u);
            if (which == 0)
            {
                if ((obj->flags & 0x20u) == 0)
                    return static_cast<int>(variantMesh(btype, obj) * 2u);
                return (btype->entranceMesh + port::load<int8_t>(shape)) * 2;
            }
            return (btype->entranceMesh + port::load<int8_t>(shape + (which == 1 ? 0u : 1u))) * 2;
        }

        // Offices: the block's mesh set (three per set) from the size's table of three by the tile, then part 0 the
        // block (its entrance one on), parts 1 and 2 the set's first, 3 its third, 4 its second.
        int officeMesh(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, uint32_t part, uint32_t table)
        {
            const int size = btype->size;
            const int column = remainder(divide(obj->tile, size), 3);
            const uint32_t set = port::load<uint32_t>(table + 12u * static_cast<uint32_t>(size) + 4u * static_cast<uint32_t>(column));
            uint32_t mesh = btype->entranceMesh + 3u * set;
            switch (part & 7u)
            {
            case 0:
                mesh = (obj->flags & 0x20u) != 0 ? mesh + 1u : variantMesh(btype, obj);
                break;
            case 3:
                mesh += 2u;
                break;
            case 4:
                mesh += 1u;
                break;
            default:
                break;
            }
            return static_cast<int>(mesh * 2u);
        }
    }

    int BldgHousing_CollideA(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                             GuestPtr<uint32_t> hit, GuestPtr<uint32_t> part, ps2f lift)
    {
        return Collide_SegmentA(static_cast<int>(btype.address), housingMesh(btype, obj, *part), point, matrix, hit, lift);
    }

    int BldgHousing_CollideB(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                             GuestPtr<uint32_t> hit, GuestPtr<ps2f> distance, GuestPtr<uint32_t> part, ps2f f12)
    {
        return Collide_SegmentB(static_cast<int>(btype.address), housingMesh(btype, obj, *part), point, matrix, hit, distance, f12);
    }

    int BldgOffice_CollideA(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                            GuestPtr<uint32_t> hit, GuestPtr<uint32_t> part, ps2f lift)
    {
        return Collide_SegmentA(static_cast<int>(btype.address), officeMesh(btype, obj, *part, 0x001B1428u), point, matrix, hit, lift);
    }

    int BldgOffice_CollideB(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                            GuestPtr<uint32_t> hit, GuestPtr<ps2f> distance, GuestPtr<uint32_t> part, ps2f f12)
    {
        return Collide_SegmentB(static_cast<int>(btype.address), officeMesh(btype, obj, *part, 0x001B1408u), point, matrix, hit, distance, f12);
    }

    // Ports collide 2.5 lower: the matrix's translation y is lowered first (and stays lowered).
    int BldgPort_CollideA(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                          GuestPtr<uint32_t> hit, ps2f lift)
    {
        const int mesh = (obj->flags & 0x20u) != 0 ? btype->entranceMesh * 2 : static_cast<int>(variantMesh(btype, obj) * 2u);
        matrix[3].y = ps2f(matrix[3].y) - ps2f(2.5f);
        return Collide_SegmentA(static_cast<int>(btype.address), mesh, point, matrix, hit, lift);
    }

    int BldgPort_CollideB(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                          GuestPtr<uint32_t> hit, GuestPtr<ps2f> distance, ps2f f12)
    {
        const int mesh = (obj->flags & 0x20u) != 0 ? btype->entranceMesh * 2 : static_cast<int>(variantMesh(btype, obj) * 2u);
        matrix[3].y = ps2f(matrix[3].y) - ps2f(2.5f);
        return Collide_SegmentB(static_cast<int>(btype.address), mesh, point, matrix, hit, distance, f12);
    }

    // Small goods stacks collide only from 0x27 goods (low byte of +0x10) on, one mesh per count.
    int BldgSmallGoods_CollideA(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                                GuestPtr<uint32_t> hit, ps2f lift)
    {
        const uint8_t count = port::load<uint8_t>(obj.address + 0x10u);
        if (count < 0x27)
            return 0;
        return Collide_SegmentA(static_cast<int>(btype.address), (btype->entranceMesh + count - 0x27) * 2, point, matrix, hit, lift);
    }

    int BldgSmallGoods_CollideB(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                                GuestPtr<uint32_t> hit, GuestPtr<ps2f> distance, ps2f f12)
    {
        const uint8_t count = port::load<uint8_t>(obj.address + 0x10u);
        if (count < 0x27)
            return 0;
        return Collide_SegmentB(static_cast<int>(btype.address), (btype->entranceMesh + count - 0x27) * 2, point, matrix, hit, distance, f12);
    }

    PORT_FN(Bldg_InitRecordBase);
    PORT_FN(Bldg_InitRecord);
    PORT_FN(Bldg_Slot30);
    PORT_FN(BldgHousing_InitRecord);
    PORT_FN(BldgRoad_InitRecord);
    PORT_FN(BldgField_Slot1C);
    PORT_FN(BldgHousing_CollideA);
    PORT_FN(BldgHousing_CollideB);
    PORT_FN(BldgOffice_CollideA);
    PORT_FN(BldgOffice_CollideB);
    PORT_FN(BldgPort_CollideA);
    PORT_FN(BldgPort_CollideB);
    PORT_FN(BldgSmallGoods_CollideA);
    PORT_FN(BldgSmallGoods_CollideB);

    // ---- city scene values (batch 38)

    void CityScene_AddApproval(GuestPtr<CityScene> city, int delta) { city->pendingApproval += delta; }

    // Approval below 0: 0, below 51: 1, below 101: 2, else 3.
    int CityScene_ApprovalTier(GuestPtr<CityScene> city)
    {
        const int approval = city->approval;
        if (approval < 0)
            return 0;
        if (approval < 51)
            return 1;
        return approval < 101 ? 2 : 3;
    }

    // Residents: below 100 rank 1, then 300, 500, 1000, 1500, 2000 and 2500 (rank 8 beyond).
    int CityScene_PopulationRank(GuestPtr<CityScene> city)
    {
        static constexpr int kLimits[] = {100, 300, 500, 1000, 1500, 2000, 2500};
        const int population = city->map->population;
        for (int rank = 0; rank < 7; ++rank)
            if (population < kLimits[rank])
                return rank + 1;
        return 8;
    }

    int CityScene_ScoreA0Tier(GuestPtr<CityScene> city)
    {
        if (city->scoreA0 < 200)
            return 0;
        return city->scoreA0 < 300 ? 1 : 2;
    }

    int CityScene_ScoreA8Tier(GuestPtr<CityScene> city)
    {
        if (city->scoreA8 < 200)
            return 0;
        return city->scoreA8 < 300 ? 1 : 2;
    }

    // The mission's progress in percent (0..100): by the stage's mission kind (stage state +0x1D0) a building count
    // of the map (kind 0 is always 100), times 100 over the target (0x1F32E0); 0 without a target. Unknown kinds use
    // v0 as it came in.
    int CityScene_MissionProgress(GuestPtr<CityScene> city)
    {
        int32_t value = port::guestV0();
        switch (port::load<uint32_t>(globals::kStageState + 0x1D0u))
        {
        case 0:
            return 100;
        case 1:
            value = city->map->categoryCount[1];
            break;
        case 2:
            value = city->map->categoryCount[7];
            break;
        case 3:
            value = city->map->categoryCount[3];
            break;
        case 4:
            value = city->map->categoryCount[4];
            break;
        case 5:
            value = city->map->bigCategory5Count;
            break;
        default:
            break;
        }
        const int32_t target = globals::familiesMovedIn.get();
        if (target == 0)
            return 0;
        int32_t percent = divide(static_cast<int32_t>(static_cast<uint32_t>(value) * 100u), target);
        if (percent >= 101)
            percent = 100;
        return percent < 0 ? 0 : percent;
    }

    // Saving the city: the pending approval is applied (kept within -99..120), then every part writes its state and
    // the save file is closed (its result returned).
    int CityScene_SaveGame(GuestPtr<CityScene> city, GuestPtr<McManager> mc)
    {
        if (city->pendingApproval != 0)
        {
            city->approval += city->pendingApproval;
            city->pendingApproval = 0;
            if (city->approval < -99)
                city->approval = -99;
            else if (city->approval >= 121)
                city->approval = 120;
        }
        Stage_SaveState(static_cast<int>(city.address), mc);
        SaveFile_Write(mc, static_cast<int>(city.address + 0x84u), 0x34);
        CityCamera_SaveState(city->cityCamera, mc);
        Player_SaveState(city->player, mc);
        Applicants_SaveState(city->applicants, mc);
        Wants_SaveState(city->wants, mc);
        Family_SaveState(mc);
        Map_SaveState(city->map, mc);
        Bag_SaveState(city->bag, mc);
        Stamina_SaveState(city->stamina, mc);
        Laptop_SaveState(city->laptop, mc);
        Event_SaveState(city->events, mc);
        return SaveFile_Close(mc);
    }

    void CityCamera_SaveState(GuestPtr<CityCamera> camera, GuestPtr<McManager> mc)
    {
        SaveFile_Write(mc, static_cast<int>(camera.address + 0x530u), 4);
        SaveFile_Write(mc, static_cast<int>(camera.address + 0x130u), 0x1D4);
    }

    // Hand on to the talk; the callees' v0 is left as they leave it.
    // The result is v0 as it came in.
    int CityScene_TalkEndChoosing(GuestPtr<CityScene> city)
    {
        Talk_EndChoosing(city->talk);
        return port::guestV0();
    }

    // The result is the slot found (what mm_Talk_SelectPerson leaves in v0).
    int CityScene_TalkSelectPerson(GuestPtr<CityScene> city, int family, int member)
    {
        Talk_SelectPerson(city->talk, static_cast<uint32_t>(family), static_cast<uint32_t>(member));
        return port::load<int32_t>(city->talk->view.address + 0xB8u);
    }

    PORT_FN(CityScene_AddApproval);
    PORT_FN(CityScene_ApprovalTier);
    PORT_FN(CityScene_PopulationRank);
    PORT_FN(CityScene_ScoreA0Tier);
    PORT_FN(CityScene_ScoreA8Tier);
    PORT_FN(CityScene_MissionProgress);
    PORT_FN(CityScene_SaveGame);
    PORT_FN(CityCamera_SaveState);
    PORT_FN(CityScene_TalkEndChoosing);
    PORT_FN(CityScene_TalkSelectPerson);

    // ---- batch 38

    // The name of a post (job) at a building type, from POSTNAME.BIN (loaded at 0x1BDBC0): an offset table by
    // post group (type byte +0x146), each group an offset table by post.
    GuestPtr<char> BldgType_GetPostName(GuestPtr<BuildingType> btype, int post)
    {
        const uint32_t names = port::load<uint32_t>(0x001BDBC0u);
        const uint32_t group = names + port::load<uint16_t>(names + 2u * btype->postGroup);
        return GuestPtr<char>{names + port::load<uint16_t>(group + 2u * static_cast<uint32_t>(post))};
    }

    int BldgType_Slot30(GuestPtr<BuildingType>) { return 0; }

    PORT_FN(BldgType_GetPostName);
    PORT_FN(BldgType_Slot30);

    // ---- the wants list (batch 38): up to 64 index entries over a data area of 256 words. A want takes four
    // words; an event hold takes one, with the building type in the index entry's held field.

    // Room for an entry of `words` words.
    int Wants_HasRoom(GuestPtr<WantsWin> wants, int words)
    {
        if (wants->count >= 64)
            return 0;
        return 256 - wants->wordsUsed < words ? 0 : 1;
    }

    // Holds wants for a building type during an event: a one-word entry with the value.
    int Wants_AddEventHold(GuestPtr<WantsWin> wants, uint16_t buildingType, int value)
    {
        if (wants->count >= 64 || 256 - wants->wordsUsed <= 0)
            return 0;
        wants->index[wants->count].offset = wants->wordsUsed;
        wants->index[wants->count].held = buildingType;
        wants->data[wants->wordsUsed] = static_cast<uint32_t>(value);
        wants->count = static_cast<uint16_t>(wants->count + 1);
        wants->wordsUsed = static_cast<uint16_t>(wants->wordsUsed + 1);
        return 1;
    }

    // Removes the event holds of a building type with the value. As in the original, the entry that moves into a
    // removed one's place is skipped, and the later entries' offsets go down by one (those at 0 stay).
    void Wants_RemoveEventHold(GuestPtr<WantsWin> wants, uint16_t buildingType, int value)
    {
        for (int i = 0; i < wants->count; ++i)
        {
            if (wants->index[i].held != buildingType)
                continue;
            const uint32_t offset = wants->index[i].offset;
            if (wants->data[offset] != static_cast<uint32_t>(value))
                continue;
            for (uint32_t j = offset; j + 1 < 256; ++j)
                wants->data[j] = wants->data[j + 1];
            wants->wordsUsed = static_cast<uint16_t>(wants->wordsUsed - 1);
            for (int k = i; k + 1 < 64; ++k)
            {
                port::store<uint32_t>(wants.address + 4u + 4u * static_cast<uint32_t>(k),
                                      port::load<uint32_t>(wants.address + 8u + 4u * static_cast<uint32_t>(k)));
                if (wants->index[k].offset != 0)
                    wants->index[k].offset = static_cast<uint16_t>(wants->index[k].offset - 1);
            }
            wants->count = static_cast<uint16_t>(wants->count - 1);
        }
    }

    // The want of a person for the claim of their workplace. It counts as phoned about already when the claim
    // meter is above the high threshold.
    int Wants_AddForPerson(GuestPtr<WantsWin> wants, GuestPtr<CityScene> city, GuestPtr<MapPerson> person, int claimSlot)
    {
        if (Wants_HasRoom(wants, 4) == 0)
            return 0;
        const GuestPtr<Want> want{wants.address + 0x104u + 4u * wants->wordsUsed};
        want->person = person->index;
        want->object = person->work;
        want->family = person->family;
        want->member = person->member;
        want->claimSlot = static_cast<uint32_t>(claimSlot);
        const GuestPtr<MapObject> obj = Map_GetRecord(city->map, want->object);
        want->called = globals::claimThresholds()->high < obj->claimMeter ? 1 : 0;
        wants->index[wants->count].offset = wants->wordsUsed;
        wants->index[wants->count].held = 0;
        wants->count = static_cast<uint16_t>(wants->count + 1);
        wants->wordsUsed = static_cast<uint16_t>(wants->wordsUsed + 4);
        return 1;
    }

    // The next want (from *iterator on, which it advances) that still holds or is pressing: its claim slot,
    // building and person. Event holds are passed over. 0 when there is none left.
    int Wants_Find(GuestPtr<WantsWin> wants, GuestPtr<CityScene> city, GuestPtr<uint32_t> claimSlot, GuestPtr<uint32_t> object,
                   GuestPtr<uint32_t> person, GuestPtr<int32_t> iterator)
    {
        for (;;)
        {
            const int32_t i = *iterator;
            if (!(i < wants->count))
                return 0;
            *iterator = i + 1;
            if (wants->index[i].held != 0)
                continue;
            const GuestPtr<Want> want{wants.address + 0x104u + 4u * wants->index[i].offset};
            const int state = Wants_CheckEntry(wants, city, GuestPtr<uint16_t>{want.address});
            if (state == 0 || state == 3)
            {
                *claimSlot = want->claimSlot;
                *object = want->object;
                *person = want->person;
                return 1;
            }
        }
    }

    void Wants_SaveState(GuestPtr<WantsWin> wants, GuestPtr<McManager> mc) { SaveFile_Write(mc, static_cast<int>(wants.address), 0x504); }

    // The window model, and while shown (+0x518) its four text boxes.
    void WantsWin_Draw(GuestPtr<WantsWin> wants)
    {
        A3DObject_Draw(wants->model);
        if (wants->unk_518 == 0)
            return;
        for (int i = 0; i < 4; ++i)
            if (const uint32_t text = static_cast<uint32_t>(wants->unk_508[i]))
                TextBox_Draw(GuestPtr<TextBox>{text});
    }

    PORT_FN(Wants_HasRoom);
    PORT_FN(Wants_AddEventHold);
    PORT_FN(Wants_RemoveEventHold);
    PORT_FN(Wants_AddForPerson);
    PORT_FN(Wants_Find);
    PORT_FN(Wants_SaveState);
    PORT_FN(WantsWin_Draw);

    // ---- more building types (batch 38)

    // Keep-out parts face the map edge they stand on (x 2, z 2, x 125, else matrix 0). On low ground
    // (tile height 0.8 or less) the entrance mesh; otherwise the other mesh, at the water level.
    void BldgKeepOut_Draw(GuestPtr<BuildingType> btype, int, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                          ps2f x, ps2f y, ps2f z)
    {
        const int tileX = obj->tile % 128, tileZ = obj->tile / 128;
        const uint32_t facing = tileX == 2 ? 1u : tileZ == 2 ? 2u : tileX == 125 ? 3u : 0u;
        const GuestPtr<Vec4> matrix = layerMatrix(facing);
        matrix[3].x = x;
        matrix[3].y = y;
        matrix[3].z = z;
        if (Map_GetTileHeightWord(city->map, obj->tile) <= ps2f(0.8f))
        {
            Instance_DrawMesh(globals::propsModel.get(), matrix, GuestPtr<Vec4>{globals::kDefaultColour}, btype->entranceDrawMesh, flags);
            return;
        }
        matrix[3].y = city->waterLevel;
        Instance_DrawMesh(globals::propsModel.get(), matrix, GuestPtr<Vec4>{globals::kDefaultColour}, btype->firstDrawMesh, flags);
    }

    // Small goods: mesh look[0] (object +0x10), raised by the height scale above ground unless the type has
    // flag 1. At night (before 4:00 or from 17:00) the colour's last word becomes the integer 12.
    void BldgSmallGoods_Draw(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                             ps2f x, ps2f y, ps2f z)
    {
        if ((btype->flags & 1u) == 0 && !(y <= ps2f(0.0f)))
            y = Map_HeightScale(y);
        const GuestPtr<Vec4> matrix = placeLayer(layer, x, y, z);
        port::GuestLocal<Vec4> colour;
        *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
        const int time = Clock_AddWrap(static_cast<int>(btype.address), city->clock, obj->id);
        if (time >= 0xEF10 || time < 0x3840)
            port::store<int32_t>(colour.ptr().address + 0xCu, 12);
        Instance_DrawMesh(globals::propsModel.get(), matrix, colour, port::load<uint8_t>(obj.address + 0x10u), flags);
    }

    // My Home: the common tick, and when it has nothing to report, the objects of type 2 along the side the home
    // faces (the row or column next to it, one tile past each corner) get their byte +0x14 cleared.
    int BldgMyHome_Tick(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        const int result = Bldg_Tick(btype, city, obj, 0);
        const int facing = Map_GetTileKind(city->map, obj->tile);
        if (result != 0)
            return result;
        const int tileX = obj->tile % 128, tileZ = obj->tile / 128, size = btype->size;
        for (int i = -1; i < size + 1; ++i)
        {
            GuestPtr<MapObject> beside{0};
            if (facing == 3)
                beside = Map_GetTileObject(city->map, tileX - 1, tileZ + i);
            else if (facing == 2)
                beside = Map_GetTileObject(city->map, tileX + i, tileZ + size);
            else if (facing == 1)
                beside = Map_GetTileObject(city->map, tileX + size, tileZ + i);
            else if (facing == 0)
                beside = Map_GetTileObject(city->map, tileX + i, tileZ - 1);
            if (beside->type == 2)
                port::store<uint8_t>(beside.address + 0x14u, 0);
        }
        return result;
    }

    PORT_FN(BldgKeepOut_Draw);
    PORT_FN(BldgSmallGoods_Draw);
    PORT_FN(BldgMyHome_Tick);

    // ---- batch 38

    // The shade of a person drawn at a building: 2.5 - min(max(d, 0), 1.5) in every lane, d being the horizontal
    // distance from the footprint's centre to `position`; the last word then becomes the integer 12. The centre
    // (height 1.5, w 2.5) goes through the stack and VU0 is left as the original leaves it (vf4 to vf11, Q).
    void BldgType_OccupantMatrix(GuestPtr<BuildingType> btype, GuestPtr<Vec4> colour, GuestPtr<Vec4> position, uint32_t tile)
    {
        using namespace vu0;
        const ps2f size = fromInt(btype->size);
        const int tileX = static_cast<int>(tile & 0xFFFFu) % 128, tileZ = static_cast<int>(tile & 0xFFFFu) / 128;
        port::GuestLocal<Vec4> centre;
        centre->x = (fromInt(tileX) - ps2f(64.0f)) + size / ps2f(2.0f);
        centre->y = 1.5f;
        centre->z = (fromInt(128 - tileZ) - ps2f(64.0f)) - size / ps2f(2.0f);
        centre->w = 2.5f;
        vf(4) = load(centre);
        vf(5) = load(position);
        set(vf(6), sub(vf(5), vf(4)), XYZW);
        set(vf(7), mul(vf(6), vf(6)), X | Z);
        set(vf(8), add(vf(7), lane(vf(7), 2)), X);
        q() = sqrtQ(x(vf(8)));
        set(vf(10), add(vf(0), lane(vf(4), 1)), XYZW);
        set(vf(11), add(vf(0), lane(vf(0), 3)), XYZW);
        set(vf(9), add(vf(0), _mm_set1_ps(q())), XYZW);
        vf(9) = _mm_max_ps(vf(9), lane(vf(0), 0)); // vmaxx and vminiy, as the runtime evaluates them
        vf(9) = _mm_min_ps(vf(9), lane(vf(4), 1));
        set(vf(9), sub(vf(10), vf(9)), XYZW);
        set(vf(9), add(vf(9), lane(vf(0), 3)), XYZW);
        store(colour, vf(9));
        port::store<int32_t>(colour.address + 0xCu, 12);
    }

    PORT_FN(BldgType_OccupantMatrix);

    // ---- the clock (batch 39)

    // One tick of the city clock: 30 seconds before 6:00, then 3. Every 110 ticks the popular flag (4) is checked: a
    // popular city loses a point of approval and the flag below 100 (or with flag 8); otherwise, without flag 8, it
    // becomes popular at 118 and event 0x206 is queued from 100. At midnight the month advances: holiday months (0,
    // 3, 7, 10) get flag 2, the weather moves on a day (tomorrow rain on every fifth month from the second, snow in
    // months 11, 0 and 1), the placement history moves, and in months 9 to 2 flag 0x20 is set with a one-in-two
    // chance. The traffic lights then step (+0xC4 counts to 1000; phases from 0, 400, 500 and 900).
    void CityScene_TickClock(GuestPtr<CityScene> city)
    {
        city->clock = city->clock + (city->clock < 21600 ? 30 : 3);
        globals::popularityTicks.set(globals::popularityTicks.get() + 1);
        if (globals::popularityTicks.get() >= 110)
        {
            const uint32_t flags = city->flags;
            if ((flags & 4u) != 0)
            {
                city->approval = city->approval - 1;
                if (city->approval < 100 || (city->flags & 8u) != 0)
                    city->flags = city->flags & ~4u;
            }
            else if ((flags & 8u) == 0)
            {
                if (city->approval >= 118)
                    city->flags = flags | 4u;
                else if (city->approval >= 100)
                    Event_Trigger(city->events, 0x206, city, GuestPtr<MapPerson>{0});
            }
            globals::popularityTicks.set(0);
        }
        if (city->clock >= 86400)
        {
            city->clock = city->clock - 86400;
            city->monthCount = city->monthCount + 1;
            const int month = city->monthCount % 12;
            if (month == 0 || month == 3 || month == 7 || month == 10)
                city->flags = city->flags | 2u;
            else
                city->flags = city->flags & ~2u;
            city->weatherYesterday = city->weatherToday;
            city->weatherToday = city->weatherTomorrow;
            if (city->monthCount % 5 == 2)
            {
                const int m = city->monthCount % 12;
                city->weatherTomorrow = m == 11 || m == 0 || m == 1 ? 3 : 2;
            }
            else
                city->weatherTomorrow = 0;
            MapObj_PushHistory(city->map);
            city->flags = city->flags & ~0x20u;
            if ((month < 3 || month >= 9) && (Random_TimerMix() & 0x100) < 0x40)
                city->flags = city->flags | 0x20u;
        }
        city->trafficTimer = city->trafficTimer + 1;
        if (city->trafficTimer >= 1000)
            city->trafficTimer = 0;
        const int timer = city->trafficTimer;
        city->trafficPhase = timer < 400 ? 0 : timer < 500 ? 1 : timer < 900 ? 2 : 3;
    }

    PORT_FN(CityScene_TickClock);

    // ---- claims (batch 39)

    // How a claim entry (section B: a flag byte, the claimed id or a section D group from 0x8000, a start value, a
    // reach) stands among the buildings of its tile lists overlapping the rectangle: each one's section C value
    // for the id is taken off the start (a claim of need, flag 0: 0 as soon as it is met) or added to it (flag set:
    // 0xFF from 0xFF on). The result is never below 0.
    uint32_t BldgClaims_CountNearby(GuestPtr<BuildingType>, GuestPtr<CityScene> city, GuestPtr<char> claimEntry, GuestPtr<int32_t> rect)
    {
        const uint32_t entry = claimEntry.address;
        const bool wish = port::load<uint8_t>(entry) != 0;
        int32_t total = wish ? -static_cast<int32_t>(port::load<uint16_t>(entry + 4u)) : static_cast<int32_t>(port::load<uint16_t>(entry + 4u));
        const uint16_t claimed = port::load<uint16_t>(entry + 2u);
        uint32_t id = entry + 2u;
        if (claimed >= 0x8000)
            id = static_cast<uint32_t>(Claim_GetSectionD(claimed));
        port::GuestLocal<uint32_t> count;
        do
        {
            uint32_t item = Map_GetTileList(city->map, port::load<uint16_t>(id), count).address;
            for (int i = 0; i < static_cast<int32_t>(*count); ++i, item += 2u)
            {
                const GuestPtr<MapObject> building = Map_GetRecord(city->map, port::load<uint16_t>(item));
                const GuestPtr<BuildingType> type{globals::buildingTypeAddress(building->type)};
                const int x = building->tile & 0x7F, z = building->tile >> 7, size = type->size;
                if (!(rect[0] < x + size && x < rect[2] && rect[1] < z + size && z < rect[3]))
                    continue;
                uint32_t value = static_cast<uint32_t>(Claim_GetSectionC(type->claimC));
                while (port::load<uint16_t>(value) != port::load<uint16_t>(id))
                    value += 4u;
                if (!wish)
                {
                    total -= port::load<uint16_t>(value + 2u);
                    if (total < 1)
                        return 0;
                }
                else
                {
                    total += port::load<uint16_t>(value + 2u);
                    if (total > 0xFE)
                        return 0xFF;
                }
            }
            if (claimed < 0x8000)
                break;
            id += 2u;
        } while (port::load<uint16_t>(id) != 0);
        return total < 0 ? 0u : static_cast<uint32_t>(total);
    }

    // Picks the building's claim: each entry of its section B weighs its value among the nearby buildings
    // (mm_BldgClaims_CountNearby over the footprint widened by the entry's reach), or for id 0x32 its start value
    // while the building has no entrance (0x4000). One is drawn by weight: it becomes the claim slot, its weight
    // the claim timer, and a claim of need not at its start value marks the building (flag 2).
    void BldgClaims_EvalB(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        port::GuestFrame<0x40> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<uint8_t> weights{sp};
        const GuestPtr<int32_t> rect{sp + 0x30u};
        const uint32_t section = static_cast<uint32_t>(Claim_GetSectionB(btype->claimB));
        const int x0 = obj->tile & 0x7F, z0 = obj->tile >> 7, x1 = x0 + btype->size, z1 = z0 + btype->size;
        int sum = 0;
        uint32_t k = 0;
        for (uint32_t entry = section; port::load<uint16_t>(entry + 2u) != 0; entry += 8u, ++k)
        {
            if (port::load<uint16_t>(entry + 2u) == 0x32)
                weights[k] = obj->entranceTile == 0x4000 ? static_cast<uint8_t>(port::load<uint16_t>(entry + 4u)) : 0;
            else
            {
                const int reach = port::load<uint16_t>(entry + 6u);
                rect[0] = x0 - reach;
                rect[1] = z0 - reach;
                rect[2] = x1 + reach;
                rect[3] = z1 + reach;
                weights[k] = static_cast<uint8_t>(BldgClaims_CountNearby(btype, city, GuestPtr<char>{entry}, rect));
            }
            sum += weights[k];
        }
        if (sum == 0)
            return;
        int draw = static_cast<int32_t>(Random_Next()) % sum;
        uint8_t slot = 0;
        while ((draw -= weights[slot]) >= 0)
            ++slot;
        obj->claimSlot = slot;
        obj->claimTimer = weights[slot];
        const uint32_t chosen = section + 8u * slot;
        if (port::load<uint8_t>(chosen) == 0 && obj->claimTimer != port::load<uint16_t>(chosen + 4u))
            obj->flags |= 2u;
    }

    // The building's claim spreads to its neighbours: over a 13 x 13 grid of every other tile around its middle
    // (weights at 0x1B8060), each building (type flag 8) met first there, and every object stacked on its tile,
    // whose own claim is the same (flag byte and id), above the low threshold, not in slot 0x18 and not maxed,
    // has its meter raised by weight x spread step; at 0xFF it is maxed (flag 0x40, hold time). A claim rising a
    // band costs a point of pending approval. Stops at the map's far rows.
    void BldgClaims_EvalA(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        port::GuestFrame<0x180> frame;
        const GuestPtr<uint16_t> seen{frame.ptr().address + 0x20u};
        int seenCount = 0;
        const GuestPtr<ClaimThresholds> limits = globals::claimThresholds();
        const uint32_t claim = static_cast<uint32_t>(Claim_GetSectionB(btype->claimB)) + 8u * obj->claimSlot;
        const uint16_t tile = obj->tile;
        const int half = btype->size >> 1;
        uint32_t weight = 0x001B8060u;
        for (int dz = -12; dz <= 12; dz += 2)
        {
            const int z = (tile >> 7) + half + dz;
            if (z < 0)
            {
                weight += 13u;
                continue;
            }
            if (z > 0x7F)
                return;
            for (int dx = -12; dx < 13; dx += 2, ++weight)
            {
                const int x = (tile & 0x7F) + half + dx;
                if (x < 0)
                    continue;
                if (x > 0x7F)
                {
                    weight += static_cast<uint32_t>((12 - dx) / 2 + 1);
                    break;
                }
                if (port::load<uint8_t>(weight) == 0)
                    continue;
                GuestPtr<MapObject> other = Map_GetTileObject(city->map, x, z);
                if ((GuestPtr<BuildingType>{globals::buildingTypeAddress(other->type)}->flags & 8u) == 0)
                    continue;
                bool already = false;
                for (int i = 0; i < seenCount; ++i)
                    if (other->id == seen[i])
                        already = true;
                if (already)
                    continue;
                seen[seenCount++] = other->id;
                for (;;)
                {
                    if (other->claimSlot != 0x18 && limits->low < other->claimMeter && (other->flags & 0x40u) == 0)
                    {
                        const GuestPtr<BuildingType> type{globals::buildingTypeAddress(other->type)};
                        const uint32_t theirs = static_cast<uint32_t>(Claim_GetSectionB(type->claimB)) + 8u * other->claimSlot;
                        if (port::load<int16_t>(theirs + 2u) == port::load<int16_t>(claim + 2u) && port::load<int8_t>(theirs) == port::load<int8_t>(claim))
                        {
                            const int before = claimBand(other->claimMeter);
                            other->claimMeter = static_cast<uint16_t>(other->claimMeter + port::load<uint8_t>(weight) * limits->spreadStep);
                            if (other->claimMeter > 0xFE)
                            {
                                other->flags |= 0x40u;
                                other->claimTimer = limits->holdTime;
                                other->claimMeter = 0xFF;
                            }
                            if (before < claimBand(other->claimMeter))
                                city->pendingApproval = city->pendingApproval - 1;
                        }
                    }
                    if (other->nextOnTile == kNoObject)
                        break;
                    other = Map_GetRecord(city->map, other->nextOnTile);
                }
            }
        }
    }

    PORT_FN(BldgClaims_CountNearby);
    PORT_FN(BldgClaims_EvalB);
    PORT_FN(BldgClaims_EvalA);

    // ---- claims, continued (batch 39)

    namespace
    {
        // A claim entry's value now: for id 0x32 its start value while the building has no entrance, else its value
        // among the nearby buildings (the footprint widened by the entry's reach; the rectangle in `rect`).
        uint32_t claimValue(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, uint32_t entry, GuestPtr<int32_t> rect)
        {
            if (port::load<uint16_t>(entry + 2u) == 0x32)
                return obj->entranceTile == 0x4000 ? port::load<uint16_t>(entry + 4u) : 0u;
            const int reach = port::load<uint16_t>(entry + 6u);
            const int x = obj->tile & 0x7F, z = obj->tile >> 7;
            rect[0] = x - reach;
            rect[1] = z - reach;
            rect[2] = x + btype->size + reach;
            rect[3] = z + btype->size + reach;
            return BldgClaims_CountNearby(btype, city, GuestPtr<char>{entry}, rect);
        }

        // A claim is met: the building is marked (8, claim timer 100), the approval gains 3 and the payday score a
        // point, the meter clears, the variant's low byte becomes 1, with a sound and a minimap mark.
        void claimMet(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
        {
            obj->flags |= 8u;
            obj->claimTimer = 100;
            CityScene_AddApproval(city, 3);
            obj->claimMeter = 0;
            city->paydayScore = city->paydayScore + 1;
            port::store<uint8_t>(obj.address + 0x18u, 1);
            Se_RequestForObject(obj->tile, 3, 0x1B, 0, 1, 0x28);
            Minimap_Plot(city->minimap, obj->tile, btype->size, 5, 0);
        }
    }

    // Re-checks the building's claim: below its timer value, a claim above the low threshold is met when nothing is
    // missing any more, or (a claim of need not yet marked) is marked as met with flag 2; otherwise the timer takes
    // the value. Below the threshold the claim is dropped (slot 0x18). Above the timer value the timer follows,
    // and a marked claim back at its start value loses flag 2.
    void BldgClaims_EvalC(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        port::GuestFrame<0x20> frame;
        const uint32_t claim = static_cast<uint32_t>(Claim_GetSectionB(btype->claimB)) + 8u * obj->claimSlot;
        const int value = static_cast<int>(claimValue(btype, city, obj, claim, GuestPtr<int32_t>{frame.ptr().address + 0x10u}));
        const int timer = obj->claimTimer;
        if (value < timer)
        {
            if (!(globals::claimThresholds()->low < obj->claimMeter))
            {
                obj->claimMeter = 0;
                obj->claimSlot = 0x18;
            }
            else if (value == 0)
            {
                claimMet(btype, city, obj);
                obj->flags &= 0xFFFDu;
            }
            else if (port::load<uint8_t>(claim) == 0 && (obj->flags & 2u) == 0)
            {
                obj->flags |= 2u;
                claimMet(btype, city, obj);
            }
            else
                obj->claimTimer = static_cast<uint8_t>(value);
        }
        else if (timer < value)
        {
            obj->claimTimer = static_cast<uint8_t>(value);
            if ((obj->flags & 2u) != 0 && value == port::load<uint16_t>(claim + 4u))
                obj->flags &= 0xFFFDu;
        }
    }

    // Weighs the building's claims against its neighbours': the table at 0x1F4B90 (0x300 bytes; per claim id, ids
    // from 0x8000 as 0x40 on, a need and a wish slot of four bytes: present, summed weights, weighted meters)
    // marks the claims the building has a value for, then the neighbours over the weighted 13 x 13 grid (as in
    // mm_BldgClaims_EvalA, stopping once a slot's weights pass 200) add their meters to the slots of their claims.
    // The heaviest slot wins: when it is the building's own claim the meter moves towards the neighbours' average
    // by half a step (the threshold step, or a tenth of a larger claim timer); otherwise, with the meter at most
    // the high threshold and its share (meter x switch factor) below the slot's weighted meters, the building
    // takes that claim.
    void BldgClaims_EvalD(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        port::GuestFrame<0x1C0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<uint8_t> weights{sp + 0x20u};
        const GuestPtr<int32_t> rect{sp + 0x50u};
        const GuestPtr<uint16_t> seen{sp + 0x60u};
        const GuestPtr<ClaimThresholds> limits = globals::claimThresholds();
        const auto slotOf = [](uint32_t entry) {
            uint32_t id = port::load<uint16_t>(entry + 2u);
            if (id >= 0x8000)
                id -= 0x7FC0;
            return 0x001F4B90u + 4u * port::load<uint8_t>(entry) + 8u * id;
        };
        sdk::memset(0x001F4B90u, 0, 0x300);
        const uint32_t section = static_cast<uint32_t>(Claim_GetSectionB(btype->claimB));
        uint32_t k = 0;
        for (uint32_t entry = section; port::load<uint16_t>(entry + 2u) != 0; entry += 8u, ++k)
        {
            weights[k] = static_cast<uint8_t>(claimValue(btype, city, obj, entry, rect));
            if (weights[k] != 0)
                port::store<uint8_t>(slotOf(entry), 1);
        }
        int seenCount = 0;
        const uint16_t tile = obj->tile;
        const int half = btype->size >> 1;
        uint32_t weight = 0x001B8060u;
        bool full = false;
        for (int dz = -12; dz <= 12 && !full; dz += 2)
        {
            const int z = (tile >> 7) + half + dz;
            if (z < 0)
            {
                weight += 13u;
                continue;
            }
            if (z > 0x7F)
                break;
            for (int dx = -12; dx < 13 && !full; dx += 2, ++weight)
            {
                const int x = (tile & 0x7F) + half + dx;
                if (x < 0)
                    continue;
                if (x > 0x7F)
                {
                    weight += static_cast<uint32_t>((12 - dx) / 2 + 1);
                    break;
                }
                if (port::load<uint8_t>(weight) == 0)
                    continue;
                GuestPtr<MapObject> other = Map_GetTileObject(city->map, x, z);
                if ((GuestPtr<BuildingType>{globals::buildingTypeAddress(other->type)}->flags & 8u) == 0)
                    continue;
                bool already = false;
                for (int i = 0; i < seenCount; ++i)
                    if (other->id == seen[i])
                        already = true;
                if (already)
                    continue;
                seen[seenCount++] = other->id;
                for (;;)
                {
                    if (other->id != obj->id && other->claimSlot != 0x18 && limits->low < other->claimMeter)
                    {
                        const GuestPtr<BuildingType> type{globals::buildingTypeAddress(other->type)};
                        const uint32_t slot = slotOf(static_cast<uint32_t>(Claim_GetSectionB(type->claimB)) + 8u * other->claimSlot);
                        if (port::load<uint8_t>(slot) != 0)
                        {
                            port::store<uint16_t>(slot + 2u, static_cast<uint16_t>(port::load<uint16_t>(slot + 2u) + other->claimMeter * port::load<uint8_t>(weight)));
                            port::store<uint8_t>(slot + 1u, static_cast<uint8_t>(port::load<uint8_t>(slot + 1u) + port::load<uint8_t>(weight)));
                            if (port::load<uint8_t>(slot + 1u) > 200)
                            {
                                full = true;
                                break;
                            }
                        }
                    }
                    if (other->nextOnTile == kNoObject)
                        break;
                    other = Map_GetRecord(city->map, other->nextOnTile);
                }
                if (full)
                    break;
            }
        }
        uint32_t bestId = 0, bestFlag = 0, bestMeters = 0, bestWeights = 0;
        for (uint32_t i = 0; i < 0xC0u; ++i)
        {
            const uint32_t slot = 0x001F4B90u + 4u * i;
            if (port::load<uint8_t>(slot) == 0 || !(bestMeters < port::load<uint16_t>(slot + 2u)))
                continue;
            bestId = i >> 1;
            bestFlag = i & 1u;
            bestWeights = port::load<uint8_t>(slot + 1u);
            bestMeters = port::load<uint16_t>(slot + 2u);
        }
        if (bestWeights == 0)
            return;
        if (static_cast<int32_t>(bestId) > 0x3F)
            bestId += 0x7FC0;
        const uint32_t own = section + 8u * obj->claimSlot;
        if (bestId == port::load<uint16_t>(own + 2u) && bestFlag == port::load<uint8_t>(own))
        {
            const uint32_t average = bestMeters / bestWeights;
            uint32_t step = limits->step;
            if (!(static_cast<uint32_t>(obj->claimTimer) < step * 10u))
                step = obj->claimTimer / 10u;
            const uint32_t meter = obj->claimMeter;
            const auto halfStep = static_cast<uint16_t>(static_cast<int32_t>(step) >> 1);
            if (!(average < meter * 2u) || !(static_cast<int32_t>(average - (step >> 1)) < static_cast<int32_t>(meter)))
                obj->claimMeter = static_cast<uint16_t>(obj->claimMeter + halfStep);
            else if (average + step < meter)
                obj->claimMeter = static_cast<uint16_t>(obj->claimMeter - halfStep);
            return;
        }
        if (!(obj->claimMeter <= limits->high) || !(static_cast<uint32_t>(obj->claimMeter) * limits->switchFactor < bestMeters))
            return;
        int index = 0;
        for (uint32_t entry = section; port::load<uint16_t>(entry + 2u) != 0; entry += 8u, ++index)
        {
            if (port::load<uint16_t>(entry + 2u) != bestId || port::load<uint8_t>(entry) != bestFlag)
                continue;
            obj->claimSlot = static_cast<uint8_t>(index);
            obj->claimTimer = weights[index];
            if (port::load<uint8_t>(entry) == 0 && obj->claimTimer != port::load<uint16_t>(entry + 4u))
                obj->flags |= 2u;
            else
                obj->flags &= 0xFFFDu;
            return;
        }
    }

    PORT_FN(BldgClaims_EvalC);
    PORT_FN(BldgClaims_EvalD);

    // ---- small goods and leveled ground (batch 39)

    namespace
    {
        // The height a flat building settles to. Above ground (a positive tile height) it is `aboveGround`.
        // Below, the height is cut to the half-unit grid: a remainder (fmod 0.5) over 0.1 takes 0.1 off, one of
        // -0.4 or less goes down to the lower half unit, any other takes the remainder off.
        ps2f settledHeight(ps2f height, ps2f aboveGround)
        {
            if (!(height <= ps2f(0.0f)))
                return aboveGround;
            const ps2f remainder = sdk::fmodf(height, ps2f(0.5f));
            if (ps2f(0.0f) == remainder)
                return height;
            if (!(remainder <= ps2f(0.1f)))
                return height - ps2f(0.1f);
            if (remainder <= ps2f(-0.4f))
                return (height - remainder) - ps2f(0.5f);
            return height - remainder;
        }
    }

    // Small goods: the tile kind from the entrance search, and for a type with flag 1 the ground under it settles
    // (0.1 above ground) and the area around it is rebuilt; then the minimap shows it (colour 1).
    int BldgSmallGoods_Tick(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        port::GuestFrame<0x90> frame;
        const uint32_t sp = frame.ptr().address;
        const int x = obj->tile & 0x7F, z = obj->tile >> 7;
        const GuestPtr<int32_t> side{sp + 0x8Cu};
        BldgType_FindEntrance(btype, city, x, z, side);
        Map_SetTileKind(city->map, obj->tile, static_cast<uint32_t>(*side));
        if ((btype->flags & 1u) != 0)
        {
            const ps2f ground = Map_GetTileHeightWord(city->map, obj->tile);
            const ps2f height = settledHeight(ground, ps2f(0.1f));
            if (height != ground)
            {
                Map_SetAreaHeight(city->map, x, z, btype->size, height);
                const GuestPtr<int32_t> rect{sp + 0x70u};
                rect[0] = x - 1;
                rect[1] = z + btype->size - 1;
                rect[2] = rect[0] + btype->size + 2;
                rect[3] = rect[1] + btype->size + 2;
                Map_UpdateAreaGeometry(city->map, rect);
            }
        }
        Minimap_Plot(city->minimap, obj->tile, btype->size, 1, 0);
        return 0;
    }

    // Whether leveled ground can go on an area of `size` at (x, z): the ring around it is counted per side, how
    // many of the `size` tiles along each side carry an object of a type with flag 2, with flag 0x40, and
    // that are not roads (type 3). Sides that are full count; which sides are needed depends on the map's placement
    // mode (+0x60): below 2 two sides of flag 2 or 0x40 (and with `needsFlag2`, one of flag 2), 2 the same
    // or one of flag 2 with another not road, above that any two sides of the three kinds with one of flag 2 or 0x40.
    int BldgLeveled_CheckArea(GuestPtr<BuildingType>, GuestPtr<CityScene> city, int x, int z, int size, int needsFlag2)
    {
        int stop[4] = {}, flag2[4] = {}, notRoad[4] = {};
        for (int i = 0; i < size; ++i)
        {
            const GuestPtr<MapObject> beside[4] = {
                Map_GetTileObject(city->map, x + i, z - 1), Map_GetTileObject(city->map, x + size, z + i),
                Map_GetTileObject(city->map, x + i, z + size), Map_GetTileObject(city->map, x - 1, z + i)};
            for (int side = 0; side < 4; ++side)
            {
                const uint16_t flags = GuestPtr<BuildingType>{globals::buildingTypeAddress(beside[side]->type)}->flags;
                if ((flags & 2u) != 0)
                    ++flag2[side];
                if ((flags & 0x40u) != 0)
                    ++stop[side];
                if (beside[side]->type != 3)
                    ++notRoad[side];
            }
        }
        int fullFlag2 = 0, fullStop = 0, fullNotRoad = 0;
        for (int side = 0; side < 4; ++side)
        {
            if (size <= flag2[side])
                ++fullFlag2;
            if (size <= stop[side])
                ++fullStop;
            if (size <= notRoad[side])
                ++fullNotRoad;
        }
        const int mode = city->map->unk_60;
        if (mode < 2)
        {
            if (needsFlag2 == 0 || fullFlag2 != 0)
                return fullFlag2 + fullStop > 1 ? 1 : 0;
            return 0;
        }
        if (mode == 2)
            return (fullFlag2 + fullStop > 1 || (fullFlag2 != 0 && fullFlag2 + fullNotRoad > 1)) ? 1 : 0;
        return (fullStop + fullFlag2 != 0 && fullFlag2 + fullNotRoad + fullStop > 1) ? 1 : 0;
    }

    // Leveled ground: the claim word at +0x10 (meter, slot and timer as one signed word) takes the smallest of the
    // neighbours' (x + 1, z), (x + 1, z + 1) and (x, z + 1) that are leveled ground at the same height (else 0), plus
    // one while the object has no flag 1. The ground settles (0.05 above ground), and every waiting applicant
    // (state 2) whose home would fit (its size within that word, CheckArea and Map_CanPlace) gets the area
    // highlighted and its state 3 with this tile. The minimap shows it (colour 4).
    int BldgLeveled_Tick(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        port::GuestFrame<0xC0> frame; // the original's frame; the applicant cursor sits at +0xBC
        const int x = obj->tile & 0x7F, z = obj->tile >> 7;
        const ps2f ground = Map_GetTileHeightWord(city->map, obj->tile);
        const uint16_t flags = obj->flags;
        const GuestPtr<MapObject> neighbour[3] = {Map_GetTileObject(city->map, x + 1, z), Map_GetTileObject(city->map, x + 1, z + 1),
                                                  Map_GetTileObject(city->map, x, z + 1)};
        const ps2f heightAt[3] = {Map_GetHeightAt(city->map, x + 1, z), Map_GetHeightAt(city->map, x + 1, z + 1), Map_GetHeightAt(city->map, x, z + 1)};
        int32_t word[3];
        for (int k = 0; k < 3; ++k)
        {
            const bool open = neighbour[k]->type == 3 && (neighbour[k]->flags & 1u) == 0;
            word[k] = open && heightAt[k] == ground ? port::load<int32_t>(neighbour[k].address + 0x10u) : 0;
        }
        int32_t least = word[0] < word[1] ? (word[2] <= word[0] ? word[2] : word[0]) : (word[2] <= word[1] ? word[2] : word[1]);
        port::store<int32_t>(obj.address + 0x10u, least + ((flags & 1u) == 0 ? 1 : 0));
        const ps2f height = settledHeight(ground, ps2f(0.05f));
        if (height != ground)
            Map_SetTileHeight(city->map, obj->tile, height);
        const GuestPtr<uint32_t> cursor{frame.ptr().address + 0xBCu};
        *cursor = 0;
        for (uint32_t applicant = static_cast<uint32_t>(Applicants_FindFrom(city->applicants, cursor, 2)); applicant != 0;
             applicant = static_cast<uint32_t>(Applicants_FindFrom(city->applicants, cursor, 2)))
        {
            const uint32_t family = port::load<uint16_t>(applicant + 4u);
            const GuestPtr<BuildingType> home{globals::buildingTypeAddress(static_cast<uint16_t>(Family_GetTypeIndex(static_cast<int>(family))))};
            const uint32_t claim = static_cast<uint32_t>(port::load<int32_t>(obj.address + 0x10u));
            if (!(home->size <= claim))
                continue;
            if (BldgLeveled_CheckArea(btype, city, x, z, home->size, home->flags & 0x20u) == 0)
                continue;
            if (Map_CanPlace(city->map, static_cast<int>(family), port::load<uint16_t>(applicant + 0xAu), x, z) == 0)
                continue;
            Map_HighlightArea(city->map, x, z, home->size);
            port::store<int16_t>(applicant + 2u, 3);
            port::store<uint16_t>(applicant + 6u, obj->tile);
        }
        Minimap_Plot(city->minimap, obj->tile, btype->size, 4, 0);
        return 0;
    }

    PORT_FN(BldgSmallGoods_Tick);
    PORT_FN(BldgLeveled_CheckArea);
    PORT_FN(BldgLeveled_Tick);

    // ---- the applicants card (batch 39)

    namespace
    {
        // The applicants card text: its place and size follow the card model's values (x at +0x30, y at +0x34, scale
        // at +0x38 of the record at Applicants +0x2A8): the pixel offsets scale with the model, the text box
        // shrinks with it.
        void placeCardText(GuestPtr<Applicants> applicants, int x, int y, ps2f scale)
        {
            const int xOffset = sdk::fptosi(ps2f(45.0f) * scale);
            const int yOffset = sdk::fptosi(ps2f(7.1f) * scale);
            const int width1 = 0xC0 - sdk::fptosi(ps2f(16.8f) * scale);
            const int width2 = 0xB0 - sdk::fptosi(ps2f(16.0f) * scale);
            const int width3 = 0xB0 - sdk::fptosi(ps2f(14.7f) * scale);
            const int width4 = 0xC0 - sdk::fptosi(ps2f(16.3f) * scale);
            TextBox_SetPosAndSize(GuestPtr<TextBox>{port::load<uint32_t>(applicants.address + 0x294u)}, (x + 0x77E + xOffset) * 0x10,
                                  ((y + 0x7D6) - yOffset) * 0x10, width1, width2, width3, width4, -0x100000);
        }
    }

    // The card slides in over the first 20 frames of its counter (+0x2C0; its x grows by 0.15 a frame), stays, and from
    // frame 60 for 6 frames drifts (its y grows by 0.4 a frame); after that the counter and +0x2B8 clear, the
    // cursor and the top entry take the saved entry (+0x2C4) and the list scrolls back to its first entry.
    void Applicants_Layout(GuestPtr<Applicants> applicants)
    {
        const uint32_t a = applicants.address;
        port::store<int32_t>(a + 0x2C0u, port::load<int32_t>(a + 0x2C0u) + 1);
        const int32_t frame = port::load<int32_t>(a + 0x2C0u);
        const uint32_t model = port::load<uint32_t>(a + 0x2A8u);
        if (frame < 0x14)
        {
            port::store<float>(model + 0x30u, static_cast<float>(ps2f(port::load<float>(model + 0x30u)) + ps2f(0.15f)));
            const ps2f x = port::load<float>(model + 0x30u), scale = port::load<float>(model + 0x38u), y = port::load<float>(model + 0x34u);
            const int pixelX = sdk::fptosi(ps2f(144.0f) * (ps2f(0.015f) + ps2f(1.1f) * x));
            const int pixelY = sdk::fptosi(ps2f(72.0f) * (y - ps2f(0.03f)));
            placeCardText(applicants, pixelX, pixelY, scale);
        }
        else if (frame >= 0x3C)
        {
            if (frame < 0x42)
            {
                port::store<float>(model + 0x34u, static_cast<float>(ps2f(port::load<float>(model + 0x34u)) + ps2f(0.4f)));
                const ps2f x = port::load<float>(model + 0x30u), scale = port::load<float>(model + 0x38u), y = port::load<float>(model + 0x34u);
                const int pixelX = sdk::fptosi(ps2f(144.0f) * x);
                const int pixelY = sdk::fptosi(ps2f(72.0f) * (ps2f(1.11f) * y - ps2f(0.1f) * y));
                placeCardText(applicants, pixelX, pixelY, scale);
            }
            else
            {
                port::store<int32_t>(a + 0x2B8u, 0);
                const int32_t saved = port::load<int32_t>(a + 0x2C4u);
                port::store<int32_t>(a + 0x2D0u, saved);
                port::store<int32_t>(a + 0x2CCu, saved);
                port::store<int32_t>(a + 0x2C0u, 0);
                Applicants_ScrollTo(applicants, -1);
                port::store<int32_t>(a + 0x29Cu, 0);
            }
        }
    }

    PORT_FN(Applicants_Layout);

    // ---- the pig farm (batch 39)

    // Pig farms are drawn like theme parks: lit after dark within the opening hours of the object's type, the
    // entrance from its own model (placed by the object's matrix), the other parts from the city parts.
    void BldgPigFarm_Draw(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                          ps2f x, ps2f y, ps2f z)
    {
        const GuestPtr<BuildingType> type = typeOf(obj);
        port::GuestLocal<Vec4> colour;
        buildingColour(btype, city, obj, colour);
        const int time = Clock_AddWrap(static_cast<int>(btype.address), city->clock, obj->id);
        if (litAfterDark(time, type->openMinute * 60, type->closeMinute * 60))
        {
            setLit(colour);
            obj->flags |= 0x10u;
        }
        else
            obj->flags &= 0xFFEFu;
        const GuestPtr<Vec4> matrix = placeLayer(layer, x, y, z);
        if ((obj->flags & 0x20u) == 0)
        {
            drawPlainPart(btype, obj, matrix, colour, flags);
            return;
        }
        const GuestPtr<A3DObject> model = globals::pigFarmModel.get();
        for (int row = 0; row < 4; ++row)
            model->matrix[row] = matrix[row];
        Instance_DrawModel(model, colour, 0, flags);
    }

    PORT_FN(BldgPigFarm_Draw);

    // ---- the laptop's item text (batch 40)

    // The text of laptop item `item` is the item's line of the table at 0x1AF6B0 (0x80 bytes each, 14 items), or the line
    // 14 further on when the item's alternative applies: item 1 while the player's mode is 0, item 2 in mode 1, item 5 when
    // the stage flag 1 is set, item 9 in mode 3, item 11 in mode 4. Items past 13 use their own line the same way.
    void Laptop_SetItemText(GuestPtr<Laptop> laptop, GuestPtr<CityScene> city, uint32_t item)
    {
        uint32_t line = item;
        if (item < 14)
        {
            const auto modeIs = [&](int mode) { return Player_GetMode(city->player) == mode; };
            switch (item)
            {
            case 1:
                if (modeIs(0))
                    line += 14;
                break;
            case 2:
                if (modeIs(1))
                    line += 14;
                break;
            case 5:
                if ((globals::stageFlags.get() & 1u) != 0)
                    line += 14;
                break;
            case 9:
                if (modeIs(3))
                    line += 14;
                break;
            case 11:
                if (modeIs(4))
                    line += 14;
                break;
            default:
                break;
            }
        }
        TextBox_SetText(GuestPtr<TextBox>{port::load<uint32_t>(laptop.address + 0x44u)}, GuestPtr<char>{0x001AF6B0u + (line << 7)}, 0);
    }

    PORT_FN(Laptop_SetItemText);

    // ---- the people of a building (batch 40)

    namespace
    {
        GuestPtr<CharClass> classOf(GuestPtr<MapPerson> person)
        {
            return GuestPtr<CharClass>{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
        }

        // The character class's draw slot (+0x10 of its vtable at +0x20) for `person`: drawn at `position` (x, y, z, heading)
        // with `colour` in the given pose.
        void drawPerson(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> person, uint32_t colour, int flags, uint32_t position, int pose)
        {
            port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(cls.address + 0x20u) + 0x10u), cls.address, city.address, person.address, colour, flags, position, pose);
        }
    }

    // One person in a building's crowd: the colour of BldgType_DrawOccupant at the person's own position and heading, in
    // pose 6 (flag 0x40), 1 (flag 0x80) or 0.
    void BldgType_DrawOccupants(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject>, GuestPtr<MapPerson> person, int flags)
    {
        port::GuestFrame<0x80> frame; // the colour at +0x60 and the position at +0x70
        const uint32_t base = frame.ptr().address;
        const GuestPtr<Vec4> colour{base + 0x60u}, position{base + 0x70u};
        *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
        const GuestPtr<CharClass> cls = classOf(person);
        position->x = person->x;
        position->y = person->y;
        position->z = person->z;
        position->w = person->heading;
        int pose = 0;
        if ((person->flags & 0x40u) != 0)
            pose = 6;
        else if ((person->flags & 0x80u) != 0)
            pose = 1;
        BldgType_DrawOccupant(btype, colour, person, city);
        drawPerson(cls, city, person, colour.address, flags, position.address, pose);
    }

    // The people of a field: residents that are not class-flag-1 and stand on it (outside, or inside it) with no activity
    // are laid out at fixed spots round the field (the spot by their member, the first two members have their own, the
    // rest share the second) and drawn in pose 5 (9 when the field has a claim, lifted by the type's float at +0x164);
    // anyone else is drawn as an occupant.
    void BldgField_DrawPeople(GuestPtr<BuildingType> btype, int, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                              GuestPtr<MapPerson> people, ps2f x, ps2f y, ps2f z)
    {
        port::GuestFrame<0xA0> frame; // the colour at +0x80 and the position at +0x90
        const uint32_t base = frame.ptr().address;
        for (uint32_t link = obj.address + 8u; port::load<uint16_t>(link) != kNoPerson;)
        {
            const GuestPtr<MapPerson> person = people + port::load<uint16_t>(link);
            const GuestPtr<CharClass> cls = classOf(person);
            const bool onField = (cls->flags & 1u) == 0 && person->activity == 0 && (person->inside == kNoObject || person->inside == obj->id);
            if (!onField)
            {
                BldgType_DrawOccupants(btype, city, obj, person, flags);
                link = person.address + 0x10u;
                continue;
            }
            const GuestPtr<Vec4> colour{base + 0x80u};
            *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
            const uint32_t spot = 0x001B1460u + (person->member < 2 ? static_cast<uint32_t>(person->member) << 4 : 0x10u);
            const ps2f px = x + ps2f(port::load<float>(spot));
            port::store<float>(person.address, static_cast<float>(px));
            port::store<float>(base + 0x90u, static_cast<float>(px));
            const ps2f py = y + ps2f(port::load<float>(spot + 4u));
            port::store<float>(person.address + 4u, static_cast<float>(py));
            port::store<float>(base + 0x94u, static_cast<float>(py));
            const ps2f pz = z + ps2f(port::load<float>(spot + 8u));
            port::store<float>(person.address + 8u, static_cast<float>(pz));
            port::store<float>(base + 0x98u, static_cast<float>(pz));
            port::store<float>(base + 0x9Cu, port::load<float>(person.address + 0xCu));
            int pose = 5;
            if (obj->claimSlot != 0)
            {
                pose = 9;
                port::store<float>(base + 0x94u, static_cast<float>(ps2f(port::load<float>(base + 0x94u)) + ps2f(port::load<float>(btype.address + 0x164u))));
            }
            drawPerson(cls, city, person, colour.address, flags, base + 0x90u, pose);
            link = person.address + 0x10u;
        }
    }

    // The ranch: its mesh from the city parts model, or for its entrance part the ranch models (the first always, then one
    // of two by the type's answer in vtable +0x24), each at the building's place.
    void BldgRanch_Draw(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                        ps2f x, ps2f y, ps2f z)
    {
        port::GuestFrame<0x80> frame; // the colour at +0x70
        const GuestPtr<Vec4> colour{frame.ptr().address + 0x70u};
        buildingColour(btype, city, obj, colour);
        Clock_AddWrap(static_cast<int>(btype.address), city->clock, obj->id);
        const GuestPtr<Vec4> matrix = placeLayer(layer, x, y, z);
        if ((obj->flags & 0x20u) == 0)
        {
            drawPlainPart(btype, obj, matrix, colour, flags);
            return;
        }
        const auto drawAt = [&](GuestPtr<A3DObject> model) {
            for (int row = 0; row < 4; ++row)
                model->matrix[row] = matrix[row];
            Instance_DrawModel(model, colour, 0, flags);
        };
        drawAt(globals::ranchModelA.get());
        const int64_t open = port::callGuest<int64_t>(port::load<uint32_t>(btype->vtable.address + 0x24u), btype, city, obj);
        drawAt(open == 0 ? globals::ranchModelC.get() : globals::ranchModelB.get());
    }

    // A road: with the city flag 8 off its people are drawn as in a building (BldgType_DrawOccupants2); with it on, the
    // residents that are neither class flag 1 nor person flag 0x100 are gathered by person index (up to four are drawn)
    // and placed on the road by its shape (the corner table at 0x1B1670, 4 slots a shape) in pose 8, each at its ground
    // height facing the table's way; the others are drawn as occupants.
    void BldgRoad_DrawPeople(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int t0, int flags,
                             GuestPtr<MapPerson> people, ps2f x, ps2f y, ps2f z)
    {
        port::GuestFrame<0x120> frame; // the gathered indexes at +0xC0, the position at +0x100, the colour at +0x110
        const uint32_t base = frame.ptr().address;
        if (CityScene_HasFlag8(city) == 0)
        {
            BldgType_DrawOccupants2(btype, layer, city, obj, t0, flags, people);
            return;
        }
        const uint32_t list = base + 0xC0u;
        int count = 0;
        for (uint32_t link = obj.address + 8u; port::load<uint16_t>(link) != kNoPerson;)
        {
            const GuestPtr<MapPerson> person = people + port::load<uint16_t>(link);
            if ((classOf(person)->flags & 1u) != 0 || (person->flags & 0x100u) != 0)
            {
                BldgType_DrawOccupants(btype, city, obj, person, flags);
                link = person.address + 0x10u;
                continue;
            }
            int place = 0;
            for (uint32_t at = list; count > 0;)
            {
                if (person->index < port::load<uint16_t>(at))
                {
                    for (int k = count; k > place; --k)
                        port::store<uint16_t>(list + 2u * static_cast<uint32_t>(k), port::load<uint16_t>(list + 2u * static_cast<uint32_t>(k - 1)));
                    break;
                }
                ++place;
                if (!(place < count))
                    break;
                at += 2u;
            }
            port::store<uint16_t>(list + 2u * static_cast<uint32_t>(place), person->index);
            ++count;
            link = person.address + 0x10u;
        }
        const uint32_t position = base + 0x100u;
        for (int i = 0; i < count && i < 4; ++i)
        {
            const GuestPtr<MapPerson> person = people + port::load<uint16_t>(list + 2u * static_cast<uint32_t>(i));
            const uint32_t slot = 0x001B1670u + (static_cast<uint32_t>(obj->variant) << 4) + 4u * static_cast<uint32_t>(i);
            const ps2f px = x + ps2f(port::load<float>(0x001B15E0u + 4u * port::load<uint32_t>(slot)));
            port::store<float>(person.address, static_cast<float>(px));
            port::store<float>(position, static_cast<float>(px));
            const ps2f pz = z + ps2f(port::load<float>(0x001B1610u + 4u * port::load<uint32_t>(slot)));
            port::store<float>(person.address + 8u, static_cast<float>(pz));
            port::store<float>(position + 8u, static_cast<float>(pz));
            const ps2f py = Map_SampleHeight(city->map, ps2f(port::load<float>(person.address)), ps2f(port::load<float>(person.address + 8u)));
            port::store<float>(person.address + 4u, static_cast<float>(py));
            port::store<float>(position + 4u, static_cast<float>(py));
            const float heading = port::load<float>(0x001B2690u + 4u * port::load<uint32_t>(0x001B1640u + 4u * port::load<uint32_t>(slot)));
            port::store<float>(person.address + 0xCu, heading);
            port::store<float>(position + 0xCu, heading);
            const GuestPtr<Vec4> colour{base + 0x110u};
            *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
            BldgType_DrawOccupant(btype, colour, person, city);
            drawPerson(classOf(person), city, person, colour.address, flags, position, 8);
        }
    }

    PORT_FN(BldgType_DrawOccupants);
    // The crops of a field (nothing while its object has no claim slot): the matrix of the layer is put at the field's
    // place (a little higher by the type's +0x160), and the month cycle (twelve steps, two to a stage of six) picks the
    // stage: its mesh from the type's table at +0x180 is drawn as it is; the growing crop of table +0x186 follows with
    // its height blended between this stage and the next by the day's progress (the clock over two days, the second
    // day of a two day cycle counting double), the layer matrix scaled by it.
    void BldgField_Draw(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                        ps2f x, ps2f y, ps2f z)
    {
        using namespace vu0;
        port::GuestFrame<0xD0> frame; // the scale matrix at +0x90, then the product with the layer matrix
        const uint32_t base = frame.ptr().address;
        if (obj->claimSlot == 0)
            return;
        int step = remainder(city->monthCount, 12) + 1;
        if (step == 12)
            step = 0;
        const GuestPtr<Vec4> matrix = layerMatrix(static_cast<uint32_t>(layer));
        matrix[3].x = x;
        matrix[3].y = y + ps2f(port::load<float>(btype.address + 0x160u));
        matrix[3].z = z;
        const int stage = step >> 1;
        const uint8_t mesh = port::load<uint8_t>(btype.address + 0x180u + static_cast<uint32_t>(stage));
        if (mesh != 0)
            Instance_DrawMesh(globals::fieldModel.get(), matrix, GuestPtr<Vec4>{globals::kDefaultColour}, mesh, flags);
        const uint32_t growing = btype.address + 0x186u + static_cast<uint32_t>(stage);
        if (port::load<uint8_t>(growing) == 0)
            return;
        const ps2f progress = fromInt(static_cast<int32_t>(static_cast<uint32_t>((step & 1) + 1) * static_cast<uint32_t>(city->clock))) / ps2f(172800.0f);
        const GuestPtr<Vec4> scale{base + 0x90u};
        Mat4_Copy(scale, GuestPtr<Vec4>{globals::kIdentityMatrix});
        const int next = stage + 1 < 6 ? stage + 1 : 0;
        const ps2f following(port::load<float>(btype.address + 0x168u + 4u * static_cast<uint32_t>(next)));
        const ps2f current(port::load<float>(btype.address + 0x168u + 4u * static_cast<uint32_t>(stage)));
        port::store<float>(base + 0xA4u, static_cast<float>(progress * following + (ps2f(1.0f) - progress) * current));
        for (int row = 0; row < 4; ++row)
            vf(1 + row) = load(GuestPtr<Vec4>{matrix.address + 0x10u * static_cast<uint32_t>(row)});
        for (int row = 0; row < 4; ++row)
            vf(5 + row) = load(GuestPtr<Vec4>{scale.address + 0x10u * static_cast<uint32_t>(row)});
        for (int row = 0; row < 3; ++row)
        {
            set(acc(), mul(vf(5), lane(vf(1 + row), 0)), XYZW);
            set(acc(), add(acc(), mul(vf(6), lane(vf(1 + row), 1))), XYZW);
            set(acc(), add(acc(), mul(vf(7), lane(vf(1 + row), 2))), XYZW);
            set(vf(9 + row), add(acc(), mul(vf(8), lane(vf(1 + row), 3))), XYZW);
        }
        for (int row = 0; row < 4; ++row)
            store(GuestPtr<Vec4>{scale.address + 0x10u * static_cast<uint32_t>(row)}, vf(9 + row));
        scale[3] = matrix[3];
        Instance_DrawMesh(globals::fieldModel.get(), scale, GuestPtr<Vec4>{globals::kDefaultColour}, port::load<uint8_t>(growing), flags);
    }

    PORT_FN(BldgField_Draw);
    PORT_FN(BldgField_DrawPeople);
    PORT_FN(BldgRanch_Draw);
    PORT_FN(BldgRoad_DrawPeople);

    // ---- the people of a building with an entrance (batch 40)

    // Buildings of type flag 0x800 with their entrance part: the people standing in it (or outside, on it) are sorted into
    // four groups, by class flag 1 (vehicles and the like) and by whether this is their workplace: 0 residents working here,
    // 1 other residents, 2 flagged workers, 3 other flagged. Each group has room for `unk_130[group]` people (up to four)
    // that are placed at fixed spots of the type (records of 16 bytes: x, y, z, a heading added to the table's at 0x1B2690),
    // drawn in pose 4. Group 0 is kept in order of the people's member numbers, the others by person index, but a person
    // inserted always lands in slot 0 (the original writes slot 0 after shifting the ones below it up). Groups with no
    // entrance answer (vtable +0x24) are skipped, except group 2. The spot record number runs on over the groups drawn.
    void Bldg_DrawPeople(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                         GuestPtr<MapPerson> people, ps2f x, ps2f y, ps2f z)
    {
        port::GuestFrame<0x250> frame; // counts +0x110, the groups' lists +0x120 (0x40 each), the position +0x220, the colour +0x230, the slots +0x248
        const uint32_t base = frame.ptr().address;
        if ((btype->flags & 0x800u) == 0 || (obj->flags & 0x20u) == 0)
            return;
        const int32_t hasEntrance = static_cast<int32_t>(port::callGuest<int64_t>(port::load<uint32_t>(btype->vtable.address + 0x24u), btype, city, obj));
        const uint32_t counts = base + 0x110u, lists = base + 0x120u, slots = base + 0x248u;
        for (uint32_t k = 0; k < 4; ++k)
            port::store<int32_t>(counts + 4u * k, 0);
        for (uint32_t link = obj.address + 8u; port::load<uint16_t>(link) != kNoPerson;)
        {
            const GuestPtr<MapPerson> person = people + port::load<uint16_t>(link);
            link = person.address + 0x10u;
            if (person->inside != obj->id && person->inside != kNoObject)
                continue;
            const bool flagged = (classOf(person)->flags & 1u) != 0;
            const uint32_t group = (flagged ? 2u : 0u) + (person->work == obj->id ? 0u : 1u);
            const uint32_t n = static_cast<uint32_t>(port::load<int32_t>(counts + 4u * group));
            port::store<uint16_t>(lists + 0x40u * group + 2u * n, person->index);
            port::store<int32_t>(counts + 4u * group, static_cast<int32_t>(n + 1));
        }
        uint32_t running = static_cast<uint32_t>(layer);
        for (uint32_t group = 0; group < 4; ++group)
        {
            if (hasEntrance == 0 && group != 2)
                continue;
            const uint32_t limit = port::load<uint8_t>(btype.address + 0x130u + group);
            if (limit == 0 || port::load<int32_t>(counts + 4u * group) == 0)
                continue;
            for (uint32_t i = 0; i < limit; ++i)
                port::store<uint16_t>(slots + 2u * i, kNoPerson);
            for (int32_t c = 0; c < port::load<int32_t>(counts + 4u * group); ++c)
            {
                const uint32_t candidate = port::load<uint16_t>(lists + 0x40u * group + 2u * static_cast<uint32_t>(c));
                for (uint32_t pos = 0; pos < limit; ++pos)
                {
                    const uint32_t held = port::load<uint16_t>(slots + 2u * pos);
                    if (held != kNoPerson)
                    {
                        const bool before = group == 0 ? (port::load<uint8_t>(people.address + 0x30u * candidate + 0x2Eu) < port::load<uint8_t>(people.address + 0x30u * held + 0x2Eu))
                                                       : candidate < held;
                        if (!before)
                            continue;
                    }
                    for (uint32_t k = limit - 1; pos < k; --k)
                        port::store<uint16_t>(slots + 2u * k, port::load<uint16_t>(slots + 2u * (k - 1)));
                    port::store<uint16_t>(slots, static_cast<uint16_t>(candidate));
                    break;
                }
            }
            for (uint32_t i = 0; i < limit; ++i, running += 4u)
            {
                const uint32_t held = port::load<uint16_t>(slots + 2u * i);
                if (held == kNoPerson)
                    continue;
                const GuestPtr<MapPerson> person = people + held;
                const uint32_t spot = btype.address + 16u * running;
                port::store<float>(base + 0x220u, static_cast<float>(x + ps2f(port::load<float>(spot))));
                port::store<float>(base + 0x224u, static_cast<float>(y + ps2f(port::load<float>(spot + 4u))));
                port::store<float>(base + 0x228u, static_cast<float>(z + ps2f(port::load<float>(spot + 8u))));
                const ps2f heading = ps2f(port::load<float>(0x001B2690u + 4u * static_cast<uint32_t>(layer))) + ps2f(port::load<float>(spot + 0xCu));
                port::store<float>(person.address + 0xCu, static_cast<float>(heading));
                const ps2f pi = ps2f(3.1415927f);
                if (heading <= -pi)
                    port::store<float>(base + 0x22Cu, static_cast<float>(heading + pi));
                else if (heading <= pi)
                    port::store<float>(base + 0x22Cu, static_cast<float>(heading));
                else
                {
                    port::store<float>(base + 0x22Cu, static_cast<float>(heading));
                    port::store<float>(base + 0x22Cu, static_cast<float>(ps2f(port::load<float>(base + 0x22Cu)) - ps2f(6.2831855f)));
                }
                const GuestPtr<Vec4> colour{base + 0x230u};
                *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
                BldgType_DrawOccupant(btype, colour, person, city);
                drawPerson(classOf(person), city, person, colour.address, flags, base + 0x220u, 4);
            }
        }
    }

    PORT_FN(Bldg_DrawPeople);
}
