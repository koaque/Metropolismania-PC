// City scene accessors, the claim meter and clock helpers, and the default slots of the building type
// classes (mm_BldgType_*: the base class every building type derives from).

#include "game_functions.h"
#include "game_globals.h"
#include "widgets.h"
#include "sdk.h"
#include "vu0.h"

#include <bit>
#include <initializer_list>

namespace mm
{
    int CityScene_GetMode(GuestPtr<CityScene> city) { return city->modeStack[0]; }
    int CityScene_GetFade(GuestPtr<CityScene> city) { return city->fade; }
    int CityScene_InFarewellParty(GuestPtr<CityScene> city) { return (city->flags & 8u) != 0; }

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

    // A resident moves in with the prosperity and conduct of its class (traits bits 9-11 and 12-14): two decaying averages
    // (x10); once ten have been seen, the old sum keeps 9/10.
    void CityScene_RecordPlacementScore(GuestPtr<CityScene> city, int conduct, int prosperity)
    {
        if (++city->prosperityCount > 9)
        {
            city->prosperity = city->prosperity * 9 / 10;
            city->prosperityCount = 10;
        }
        city->prosperity += prosperity * 10;
        if (++city->conductCount > 9)
        {
            city->conduct = city->conduct * 9 / 10;
            city->conductCount = 10;
        }
        city->conduct += conduct * 10;
    }

    // Bit set of the active laptop entries (each entry is a bit number).
    uint32_t Laptop_CountActive()
    {
        uint32_t bits = 0;
        for (int i = 0; i < globals::laptopEntryCount.get(); ++i)
            bits |= 1u << (port::load<uint32_t>(globals::kLaptopEntries + 4u * static_cast<uint32_t>(i)) & 31u);
        return bits;
    }

    // The building markers' Circle check (a markers method, named as a laptop one): while Circle alone (0x20) is held in the
    // city view (and `allowed`), the cursor is hidden and the markers shown, once per press, with the fourth marker on node 4
    // when laptop entry 4 is active; any other input hides them again. Returns whether they are shown.
    int Markers_CheckCircle(GuestPtr<Markers> markers, GuestPtr<CityScene> city, int allowed)
    {
        const uint32_t held = Pad_Held(0);
        if (held == 0x20u && CityScene_GetMode(city) == 1 && allowed != 0)
        {
            if (markers->circleHeld == 0)
            {
                city->hudVisible[2] = 0;
                markers->visible = 1;
                if ((Laptop_CountActive() & 0x10u) != 0)
                    markers->nodes[3] = 4;
            }
        }
        else
        {
            markers->circleHeld = (held & 0x20u) != 0 ? 1 : 0;
            if (markers->visible != 0)
            {
                city->hudVisible[2] = 1;
                markers->visible = 0;
            }
        }
        return markers->visible;
    }

    // The laptop's entries on a carousel, each frame. Entry i stands at angle (2 pi / count) x (count - i) plus
    // the carousel's turn, kept within -pi..pi; its matrix (from +0x50, 0x40 bytes each) is that turn about y
    // times a tilt of pi/12 about x, placed at the turned radius vector plus the centre and scaled by scale. In
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
            const uint32_t name = port::guest(globals::buildingType(static_cast<uint32_t>(type))->name);
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
            pages->targetRow = 10;
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
        const GuestPtr<ClaimThresholds> thresholds = globals::claimThresholds();
        if (meter <= thresholds->low)
            return 0;
        return meter <= thresholds->high ? 1 : 2;
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
        ClaimIcon_GetColor(Claim_GetList(btype->claimB)[obj->claimSlot].id, level, colour);
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
        if (applicants->rollDir == ps2f(0.0f))
            applicants->relight = 1;
        const GuestPtr<ApplicantEntry> entries = GuestPtr<ApplicantEntry>::of(applicants->entries[0]);
        const GuestPtr<uint16_t> claims = GuestPtr<uint16_t>::of(applicants->claims[0]);
        for (int i = 0; i < applicants->count; ++i)
            claims[i] = GuestPtr<BuildingType>{globals::buildingTypeAddress(entries[i].type)}->claimC;
    }

    // Places the applicant card at `point`: both card models there (their matrices' last rows), and the card text
    // positioned and sized from the point's x, y and depth (z) through fixed factors.
    void Applicants_PlaceAt(GuestPtr<Applicants> applicants, GuestPtr<ps2f> point)
    {
        for (int32_t axis = 0; axis < 3; ++axis)
        {
            (GuestPtr<ps2f>::of(applicants->frameMatrix[3].x) + axis)[0] = point[axis];
            (GuestPtr<ps2f>::of(applicants->cardMatrix[3].x) + axis)[0] = point[axis];
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
        applicants->shownCount = 0;
        for (int i = 0; i < 32; ++i)
        {
            applicants->entries[i].state = 0;
            applicants->entries[i].index = static_cast<uint16_t>(i);
        }
        return static_cast<int>(applicants.address);
    }

    // ---- people's schedules and stamina

    // A person's schedule row (SCHEDULE.BIN, 0x60 bytes each): 29 during a farewell party, 30 when ill (person flag 0x10),
    // for schedule state 15 the home type's own (15 + its schedule), else the state's row.
    int Schedule_PickEntry(int, int, int homeType, int state, int party, int ill)
    {
        const uint32_t base = globals::scheduleData.get();
        if (party != 0)
            return static_cast<int>(base + 29u * 0x60u);
        if (ill != 0)
            return static_cast<int>(base + 30u * 0x60u);
        if (state == 15)
        {
            const uint32_t own = globals::buildingType(static_cast<uint32_t>(homeType))->schedule;
            return static_cast<int>(base + (own + 15u) * 0x60u);
        }
        return static_cast<int>(base + static_cast<uint32_t>(state) * 0x60u);
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
            const GuestPtr<Want> want = (wants.field(&WantsWin::data) + wants->index[i].offset).as<Want>();
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
        wants->state = 0;
        wants->matrix = GuestPtr<Vec4>::of(wants->model->matrix[0]);
        const GuestPtr<Vec4> place{0x001B0900u};
        wants->matrix[3].x = place->x;
        wants->matrix[3].y = place->y;
        wants->matrix[3].z = place->z;
        wants->animTime = 0.0f;
        for (GuestPtr<TextBox> &text : wants->texts)
            text = GuestPtr<TextBox>{0u};
        wants->page = 0;
        wants->pageCount = 10;
        wants->textsShown = 0;
        return wants;
    }

    // Wasteland plots get a random look, kept in the bytes other buildings use for their claim.
    void BldgWasteland_InitRecord(GuestPtr<BuildingType>, GuestPtr<MapObject> obj)
    {
        const auto random = [] { return static_cast<int32_t>(Random_Next()); };
        obj->look[0] = static_cast<uint8_t>(random());
        obj->look[1] = static_cast<uint8_t>(random());
        obj->look[2] = static_cast<uint8_t>(random() % 4);
        obj->look[3] = 0;
        obj->look[4] = 0;
        obj->look[5] = static_cast<uint8_t>(random() % 3);
        obj->look[6] = static_cast<uint8_t>(random() % 10);
    }

    // ---- building drawing and road links

    namespace
    {
        GuestPtr<Vec4> layerMatrix(uint32_t layer) { return GuestPtr<Vec4>{globals::kBuildingMatrices + 0x40u * layer}; }
        int blockStepX(int dir) { return port::load<int32_t>(globals::kBlockStepX + 4u * static_cast<uint32_t>(dir)); }
        int blockStepZ(int dir) { return port::load<int32_t>(globals::kBlockStepZ + 4u * static_cast<uint32_t>(dir)); }

        // Straight road pieces (shapes 5 and 10) continue a road; anything else ends it.
        bool isStraightRoad(uint16_t shape) { return shape == 5 || shape == 10; }
    }

    // Leveled ground: the type's mesh, when the object's flag 1 is set.
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
                          btype.as<LeveledType>()->mesh, flags);
    }

    // Wasteland: one of the weed and rock meshes (row look[4] - 1, column look[5]) under one of four matrices
    // (look[2]), nudged by look[0] and look[1] / 400 (see mm_BldgWasteland_InitRecord). Nothing while look[4]
    // is 0.
    void BldgWasteland_Draw(GuestPtr<BuildingType>, int, GuestPtr<CityScene>, GuestPtr<MapObject> obj, int, int flags,
                            ps2f x, ps2f y, ps2f z)
    {
        if (obj->look[4] == 0)
            return;
        layerMatrix(obj->look[2])[3].x = x + fromInt(obj->look[0]) / ps2f(400.0f);
        layerMatrix(obj->look[2])[3].y = y;
        layerMatrix(obj->look[2])[3].z = z + fromInt(obj->look[1]) / ps2f(400.0f);
        Instance_DrawMesh(globals::wastelandModel.get(), layerMatrix(obj->look[2]), GuestPtr<Vec4>{globals::kDefaultColour},
                          obj->look[5] + (obj->look[4] - 1) * 3, flags);
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
                if (Map_TileWalkable(city->map, tx, tz) == 0)
                    continue;
                if (isStraightRoad(Map_GetTileObject(city->map, tx, tz)->roadShape))
                {
                    *spot = static_cast<uint32_t>(dir);
                    return 1;
                }
                open &= ~bit;
            }
        return 0;
    }

    // Wasteland each tick. A marked one (weeds, look[4]) loses its mark unless both triangles of its tile face straight
    // up within 0.96 (normals towards -y, as y points down; VU0 cross products of the corner edges). The tile and its
    // four neighbours under the water (deeper than waterLevel) are counted: all five plot colour 0 on the minimap, some
    // 8. With none, dry land, a roll of 6..10 out of 32 by a straight road (mm_BldgWasteland_FindSpot) whose block is
    // walkable or leveled ground (tile flags 0x10, 8) levels this tile (a type 3 object, leveled land), and the colour
    // goes by the mark: 0 gives 1, 1, 3 and 7 up give 2, the others 6. Returns 0.
    int BldgWasteland_Tick(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        port::GuestFrame<0x120> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<ps2f> corners{sp + 0x70u};
        const GuestPtr<uint32_t> spot{sp + 0x11Cu};
        const int x = obj->tile & 0x7F, z = obj->tile >> 7;
        Map_GetTileVertex(city->map, obj->tile, corners);
        if (obj->look[4] != 0)
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
                obj->look[4] = 0;
        }

        Map_GetTileHeightWord(city->map, obj->tile);
        int wet = 0;
        const auto count = [&](ps2f height) {
            if (!(height <= ps2f(city->waterLevel)))
                ++wet;
        };
        count(Map_GetTileHeightWord(city->map, obj->tile));
        count(Map_GetHeightAt(city->map, x + 1, z));
        count(Map_GetHeightAt(city->map, x - 1, z));
        count(Map_GetHeightAt(city->map, x, z + 1));
        count(Map_GetHeightAt(city->map, x, z - 1));
        int colour = 0;
        if (wet != 5)
        {
            colour = 8;
            if (wet == 0)
            {
                const int roll = static_cast<int32_t>(Random_Next()) & 0x1F;
                if (roll >= 6 && roll < 11 && BldgWasteland_FindSpot(btype, city, static_cast<int>(obj.address), static_cast<uint32_t>(x),
                                                                     static_cast<uint32_t>(z), spot) != 0)
                {
                    const int sx = x + port::load<int32_t>(0x001B1770u + 4u * *spot);
                    const int sz = z + port::load<int32_t>(0x001B1780u + 4u * *spot);
                    if (Map_TileWalkable(city->map, sx, sz) != 0 || Map_TileIsLeveled(city->map, sx, sz) != 0)
                        Map_PlaceObject(city->map, 0, 3, x, z, static_cast<int>(city.address));
                }
                const uint8_t mark = obj->look[4];
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
                if (Map_TileWalkable(city->map, tx, tz) != 0)
                {
                    *side = s;
                    return Map_GetTileObject(city->map, tx, tz)->tile;
                }
            }
        *side = 0;
        return 0x4000;
    }

    // A road piece's links (one tile per direction, 0x4000 = none): for each direction its shape opens to
    // (bit d), the first road block along it that is not straight road.
    void BldgRoad_FindNeighbours(GuestPtr<BuildingType>, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int x, int z)
    {
        for (int dir = 0; dir < 4; ++dir)
        {
            obj->roadLinks[dir] = 0x4000;
            if ((obj->roadShape & (1u << dir)) == 0)
                continue;
            int tx = x, tz = z;
            for (;;)
            {
                tz += blockStepZ(dir);
                tx += blockStepX(dir);
                const uint32_t tile = static_cast<uint32_t>(tx + tz * 0x80);
                if (Map_TileIndexWalkable(city->map, tile) == 0)
                    break;
                const GuestPtr<MapObject> next = Map_GetTileFirstObject(city->map, static_cast<int>(tile));
                if (next->roadShape == 0)
                    break;
                if (!isStraightRoad(next->roadShape))
                {
                    obj->roadLinks[dir] = next->tile;
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
            if (Map_TileWalkable(city->map, tx, tz) != 0)
                return 0;
            if ((globals::buildingTypeFlags(Map_GetTileObject(city->map, tx, tz)->type) & 8u) != 0)
                return 1;
        }
        return 0;
    }

    // A field is worked from 7:00 to 15:59 on ordinary days (weather 0 or 1, not a holiday): its meter (claimMeter, signed)
    // drops by 1 (8 when its family is not in the city) and every person on it who is headed for it adds 6,
    // or 25 once the field is established (claimSlot); at 120 it becomes established with the meter at 250.
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

    // A road piece: its mesh under the layer 0 matrix. Street lamps (four meshes from the type's lampMesh) are drawn
    // lit (colour w word 12, object flag 0x10) after dark, each block a little later than the last; traffic
    // lights (five meshes from lightMesh) are drawn lit as two signal meshes chosen by the light phase (the city's
    // trafficPhase, 0..3) from the table at 0x1B1540, the second under the layer 3 matrix.
    void BldgRoad_Draw(GuestPtr<BuildingType> btype, int, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                       ps2f x, ps2f y, ps2f z)
    {
        const GuestPtr<Vec4> matrix = layerMatrix(0);
        matrix[3].x = x;
        matrix[3].y = y;
        matrix[3].z = z;
        if (obj->roadMesh == 0)
            return;
        port::GuestLocal<Vec4> colour;
        *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
        const auto drawLit = [&] { port::store<int32_t>(colour.ptr().address + 0xCu, 12); };
        obj->flags &= 0xFFEFu;
        const int mesh = obj->roadMesh;
        const int lamps = btype.as<RoadType>()->lampMesh;
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
            Instance_DrawMesh(globals::cityPartsModel.get(), matrix, colour, obj->roadMesh, flags);
            return;
        }
        const auto lights = [&] { return btype.as<RoadType>()->lightMesh; };
        if (mesh < lights() || mesh > lights() + 4)
        {
            Instance_DrawMesh(globals::cityPartsModel.get(), matrix, colour, mesh, flags);
            return;
        }
        drawLit();
        const uint32_t signals = 0x001B1540u + 4u * static_cast<uint32_t>(city->trafficPhase * 2 + (obj->roadMesh - lights()) * 8);
        Instance_DrawMesh(globals::cityPartsModel.get(), matrix, colour, lights() + port::load<int32_t>(signals), flags);
        const GuestPtr<Vec4> second = layerMatrix(3);
        second[3].x = x;
        second[3].y = y;
        second[3].z = z;
        Instance_DrawMesh(globals::cityPartsModel.get(), second, colour, lights() + port::load<int32_t>(signals + 4u), flags);
    }

    // A road block each tick. Its shape (variant) gets bit d for each direction d whose next block is road
    // (Map_TileWalkable) and its links are refreshed. When any of its four tiles lies below ground level (height above
    // 0: heights grow downwards), all four are set to 0 and the ground from one tile before to three after is rebuilt.
    // The mesh follows the shape: traffic lights (+0 to +4) at crossings and T-junctions; on straight road the slope
    // mesh (+1 along x) where the ground on either side lies more than 0.8 below the road (an embankment), else a
    // street lamp (+0/+1 along z, +2/+3 along x) on every eighth tile and the one two after it; none on bends and dead
    // ends. The block is then plotted on the minimap. Returns 0.
    int BldgRoad_Tick(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        const int x = obj->tile & 0x7F, z = obj->tile >> 7;
        int shape = 0;
        for (int dir = 0; dir < 4; ++dir)
            if (Map_TileWalkable(city->map, x + blockStepX(dir), z + blockStepZ(dir)) != 0)
                shape += port::load<int32_t>(globals::kBlockDirBit + 4u * static_cast<uint32_t>(dir));
        obj->roadShape = static_cast<uint16_t>(shape);
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
        obj->roadMesh = 0;
        const GuestPtr<RoadType> road = btype.as<RoadType>();
        const int lamps = static_cast<int16_t>(road->lampMesh); // the low halves, as the game reads them here
        const int lights = static_cast<int16_t>(road->lightMesh);
        const int slope = static_cast<int16_t>(road->slopeMesh);
        const ps2f rise = 0.8f;
        switch (obj->roadShape)
        {
        case 0xF:
            obj->roadMesh = static_cast<uint16_t>(lights);
            break;
        case 0xE:
            obj->roadMesh = static_cast<uint16_t>(lights + 1);
            break;
        case 0xD:
            obj->roadMesh = static_cast<uint16_t>(lights + 2);
            break;
        case 0xB:
            obj->roadMesh = static_cast<uint16_t>(lights + 3);
            break;
        case 0x7:
            obj->roadMesh = static_cast<uint16_t>(lights + 4);
            break;
        case 0x5: // along z
        {
            const ps2f road = Map_GetTileHeightWord(city->map, obj->tile);
            const ps2f before = Map_GetHeightAt(city->map, x - 1, z);
            const ps2f after = Map_GetHeightAt(city->map, x + btype->size, z);
            if (!(before - road <= rise) || !(after - road <= rise))
                obj->roadMesh = static_cast<uint16_t>(slope);
            else if (z % 8 == 0)
                obj->roadMesh = static_cast<uint16_t>(lamps);
            else if (z % 8 == 2)
                obj->roadMesh = static_cast<uint16_t>(lamps + 1);
            break;
        }
        case 0xA: // along x
        {
            const ps2f road = Map_GetTileHeightWord(city->map, obj->tile);
            const ps2f before = Map_GetHeightAt(city->map, x, z - 1);
            const ps2f after = Map_GetHeightAt(city->map, x, z + btype->size);
            if (!(before - road <= rise) || !(after - road <= rise))
                obj->roadMesh = static_cast<uint16_t>(slope + 1);
            else if (x % 8 == 0)
                obj->roadMesh = static_cast<uint16_t>(lamps + 2);
            else if (x % 8 == 2)
                obj->roadMesh = static_cast<uint16_t>(lamps + 3);
            break;
        }
        default:
            break;
        }
        Minimap_Plot(city->minimap, obj->tile, btype->size, 3, 0);
        return 0;
    }

    // A building part each tick (the type's tick slot). The main part (its tile's first object) finds its entrance tile
    // (entranceTile) and side; unless the type has flag 0x20 and the second entrance test fails, the side is then set
    // from where the footprint sits in the 4-tile grid of road blocks, and kept as the tile's kind. The ground under
    // the footprint is levelled: lowered ground (above 0) to 0.1, raised ground onto a multiple of 0.5 (with fmodf),
    // and the geometry around it rebuilt when that changed it. Other parts take the main part's entrance tile. A built
    // part (flag 0x20) clears occupants from 23:00:01, between 6:00 and 7:00 drops flag 0x100 (newly built; minimap
    // colour 5), and returns mm_BldgClaims_Evaluate's result; a part being built counts its work (variant) up from 7:00
    // to 18:59, and at 24 it is built and new (flags 0x120) and the count starts again. Last, the minimap colour by the
    // claim meter's level (2: 10, 1: 9, else 5). The original's frame is reserved whole: the side and the rectangle are
    // its slots.
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
                obj->occupants = 0;
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
            obj->variant = static_cast<uint8_t>(obj->variant + 1); // the construction work
            if (obj->variant >= 24)
            {
                obj->flags |= 0x120u;
                obj->variant = 0;
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

    // Offices tick like any building; a part still being built that is no longer its tile's first object (another
    // was stacked on it) is finished at once: built and new (0x120), its work count cleared.
    int BldgOffice_Tick(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int timeFlags)
    {
        const int result = Bldg_Tick(btype, city, obj, timeFlags);
        if ((obj->flags & 0x20u) == 0 && obj->id != Map_GetTileFirstObject(city->map, obj->tile)->id)
        {
            obj->flags |= 0x120u;
            obj->variant = 0;
        }
        return result;
    }

    // Housing ticks like any building; once built it recounts the units, and a part still being built that is no
    // longer its tile's first object is finished at once (0x120) with its work count cleared.
    int BldgHousing_Tick(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int timeFlags)
    {
        const int result = Bldg_Tick(btype, city, obj, timeFlags);
        if ((obj->flags & 0x20u) != 0)
            BldgHousing_CountUnits(btype, city, obj);
        else if (obj->id != Map_GetTileFirstObject(city->map, obj->tile)->id)
        {
            obj->flags |= 0x120u;
            obj->variant = 0;
        }
        return result;
    }

    // Ports dig their 3x3 block down to 2.5 (heights grow downwards) and rebuild the ground around it, the rectangle
    // reaching four tiles beyond the building (offset by its size towards +z, as in the original).
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

    // Shops and offices: tinted by their claim icon colour when the city shows claims (flag 1). A built one
    // is lit (colour w word 12, object flag 0x10) outside 4:00..16:59 while open by the building's own
    // shifted clock, and drawn from the business model, with the closed-sign mesh after hours for type flag
    // 0x1000; one being built draws its construction stage from the city parts model.
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

        // An object being built: its construction stage (variant >> 3) from the city parts model.
        void drawPlainPart(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> matrix, GuestPtr<Vec4> colour, int flags)
        {
            Instance_DrawMesh(globals::cityPartsModel.get(), matrix, colour, btype->stageMesh + (obj->variant >> 3), flags);
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
        int mesh = btype->builtMesh;
        if ((btype->flags & 0x1000u) != 0 && (time < open || close < time))
            ++mesh;
        Instance_DrawMesh(globals::businessModel.get(), matrix, colour, mesh, flags);
    }

    // Ports: like a business, lit after dark while open (hours of the object's own type), drawn 2.5 higher (at ground
    // level over their dug block); once built from the business model.
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
            Instance_DrawMesh(globals::businessModel.get(), matrix, colour, btype->builtMesh, flags);
        else
            drawPlainPart(btype, obj, matrix, colour, flags);
    }

    // Theme parks: lit like a port; once built, the animated ride model, moved to the building.
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
    // 0x67 on the parts with a second mesh). Parts choose up to two meshes from the built mesh on, varied by
    // the block's position along the street ((tile / size) % 3); the type's extra mesh goes with the first.
    // Part 0 of an object being built draws its construction stage, untinted.
    void BldgOffice_Draw(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, GuestPtr<uint32_t> part,
                         int flags, ps2f x, ps2f y, ps2f z)
    {
        if (btype->kind == 0xC3)
            *part |= 8u;
        int first = 0, second = 0;
        const int base = btype->builtMesh;
        switch (*part & 7u)
        {
        case 0: first = base; second = base + 3; break;
        case 1: first = base; break;
        case 2: first = base + 1; break;
        case 3: first = base + 1; second = base + 2; break;
        case 4: first = base + 1; second = base + 3; break;
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
            const int extra = btype.as<OfficeType>()->extraMesh;
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

    // Housing: parts 0..4 take one mesh (parts 3 and 4 two) from the built mesh on, offset per unit shape
    // (unitShape, table 0x1B14F0) and by 26 per style (style).
    void BldgHousing_Draw(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, GuestPtr<uint32_t> part,
                          int flags, ps2f x, ps2f y, ps2f z)
    {
        const int shape = obj->unitShape;
        const int base = btype->builtMesh + obj->style * 26;
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
    // height, and a built object (then *isUnit = 1). Returns its unit shape when more than `count` objects
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
        if (Map_TileIsCorner(city->map, x, z) == 0)
            return kNone;
        if (kind != Map_GetTileKind(city->map, obj->tile))
            return kNone;
        if (!(height == ps2f(Map_GetTileHeightWord(city->map, obj->tile))))
            return kNone;
        if ((obj->flags & 0x20u) == 0)
            return kNone;
        *isUnit = 1;
        if (count < Map_CountObjectsBefore(city->map, x, z, kNoObject))
            return obj->unitShape;
        return kNone;
    }

    // Works out a housing object's unit shape (variant high byte) from the units two tiles away on either side
    // along its terrain kind's axis (table 0x1B1480, 7 x 7), merged with the shape of the tile's first object
    // when they differ (table 0x1B14C0, 6 per row). When the shape changed, the neighbouring units take or give
    // the style (mesh low byte) and are worked out again in turn, each object on their tiles. An object that
    // is not its tile's first takes that one's style.
    void BldgHousing_CountUnits(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        const auto shapeOf = [](GuestPtr<MapObject> o) { return o->unitShape; };
        const auto setShape = [](GuestPtr<MapObject> o, uint8_t shape) { o->unitShape = shape; };
        const auto styleOf = [](GuestPtr<MapObject> o) { return o->style; };
        const auto setStyle = [](GuestPtr<MapObject> o, uint8_t style) { o->style = style; };

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
    // claim timer runs down; when it runs out a met claim just ends, a maxed one is settled (BldgClaims_Spread: returns 1 and
    // lowers the city's pending approval). Otherwise section C is re-checked while the map asks for it, and with
    // `rise` set the meter climbs: by the step, or by a tenth of the timer once that is ten steps or more. Past the
    // low threshold BldgClaims_WeighNeighbours runs; at 255 the meter maxes out (flag 0x40, the timer set to the hold time). Moving up a
    // band lowers the pending approval (from band 0 only while approval is positive). Slot 0x18 means no claim:
    // BldgClaims_Pick then.
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
            BldgClaims_Spread(btype, city, obj);
            obj->flags &= 0xFFBFu;
            obj->claimSlot = 0x18;
            --city->pendingApproval;
            return 1;
        }
        if (city->map->claimRecheck > 0 && obj->claimSlot != 0x18)
            BldgClaims_CheckMet(btype, city, obj);
        if (rise == 0)
            return 0;
        if (obj->claimSlot == 0x18)
        {
            BldgClaims_Pick(btype, city, obj);
            return 0;
        }
        if ((obj->flags & 0x208u) != 0)
            return 0;
        const int before = claimBand(obj->claimMeter);
        const uint8_t step = globals::claimThresholds()->step;
        const uint16_t increase = obj->claimTimer < step * 10 ? step : static_cast<uint16_t>(obj->claimTimer / 10);
        obj->claimMeter = static_cast<uint16_t>(obj->claimMeter + increase);
        if (globals::claimThresholds()->low < obj->claimMeter)
            BldgClaims_WeighNeighbours(btype, city, obj);
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

    // Every frame: each building type's animate method with the time of day (the ride animations; others do nothing).
    void BldgType_CallAll(int time)
    {
        for (int i = 0; i < globals::buildingTypeCount.get(); ++i)
        {
            const GuestPtr<BuildingType> btype{port::load<uint32_t>(globals::buildingTypeTable.get() + 4u * static_cast<uint32_t>(i))};
            if (!btype)
                continue;
            port::callGuest<void>(btype->vtable->animate, btype, time);
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
    // slot at its position and heading, in pose 6 (flag 0x40), 1 (running, 0x80) or 0.
    void BldgType_DrawOccupants2(GuestPtr<BuildingType> btype, int, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                                 GuestPtr<MapPerson> people)
    {
        port::GuestLocal<Vec4> colour; // the original's slots: entry sp - 0x10 and - 0x20
        port::GuestLocal<Vec4> position;
        for (GuestPtr<uint16_t> link = GuestPtr<uint16_t>::of(obj->firstPerson); *link != kNoPerson;
             link = GuestPtr<uint16_t>::of(people[*link].nextHere))
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
            const uint32_t draw = cls->vtable->draw;
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
            const ps2f length = A3D_GetAnimLength(model.get(), 0);
            if (!(ps2f(frame.get()) < length))
                frame.set(0.0f);
            A3D_AnimateIfRoom(model.get(), 0, frame.get());
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
    uint8_t BldgType_GroundTexture(GuestPtr<BuildingType> btype) { return btype->groundTexture; }
    int BldgType_AcceptsOn(GuestPtr<BuildingType>) { return 1; }
    int BldgType_FloorBelow(GuestPtr<BuildingType>) { return 0; }
    int BldgType_NearestHit(GuestPtr<BuildingType>) { return 0; }
    void BldgType_Animate(GuestPtr<BuildingType>) {}

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
    int BldgRoad_GroundTexture(GuestPtr<BuildingType> btype, int a1)
    {
        return btype->groundTexture + GuestPtr<MapObject>{static_cast<uint32_t>(a1)}->roadShape;
    }

    // Collision against a building's mesh: the built collision mesh once built (flag 0x20), else the one of its
    // construction stage (variant >> 3); the mesh index is doubled.
    namespace
    {
        int collisionMesh(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj)
        {
            if ((obj->flags & 0x20u) != 0)
                return btype->builtCollision * 2;
            return (btype->stageCollision + (obj->variant >> 3)) * 2;
        }
    }

    int Bldg_FloorBelow(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                      GuestPtr<uint32_t> hit, ps2f lift)
    {
        return Collide_MeshFloorBelow(static_cast<int>(btype.address), collisionMesh(btype, obj), point, matrix, hit, lift);
    }

    int Bldg_NearestHit(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                      GuestPtr<uint32_t> hit, GuestPtr<ps2f> distance, ps2f f12)
    {
        return Collide_MeshNearestHit(static_cast<int>(btype.address), collisionMesh(btype, obj), point, matrix, hit, distance, f12);
    }

    // Open hours apply to the building as a whole (no object) and to a built object; one being built is never open.
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
    PORT_FN(CityScene_InFarewellParty);
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
    PORT_FN(Markers_CheckCircle);
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
    PORT_FN(BldgType_GroundTexture);
    PORT_FN(BldgType_AcceptsOn);
    PORT_FN(BldgType_FloorBelow);
    PORT_FN(BldgType_NearestHit);
    PORT_FN(BldgType_Animate);
    PORT_FN(BldgType_IsMyHome);
    PORT_FN(BldgRoad_GroundTexture);
    PORT_FN(Bldg_FloorBelow);
    PORT_FN(Bldg_NearestHit);
    PORT_FN(Bldg_IsOpen);

    // ---- building records and collision slots (batch 38)

    void Bldg_InitRecordBase(GuestPtr<BuildingType>, GuestPtr<MapObject> obj) { obj->claimSlot = 0x18; }

    void Bldg_InitRecord(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj)
    {
        Bldg_InitRecordBase(btype, obj);
        obj->entranceTile = 0x4000;
    }

    int Bldg_RayHit(GuestPtr<BuildingType>) { return 1; }

    // Housing blocks take one of three styles by their tile ((tile / 2) % 3).
    void BldgHousing_InitRecord(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj)
    {
        Bldg_InitRecord(btype, obj);
        obj->style = static_cast<uint8_t>((obj->tile / 2) % 3);
    }

    void BldgRoad_InitRecord(GuestPtr<BuildingType>, GuestPtr<MapObject> obj)
    {
        for (uint32_t offset = 0x10u; offset <= 0x16u; offset += 2u)
            port::store<uint16_t>(obj.address + offset, 0x4000);
    }

    // A field's ground texture by its claim: the type's own while a claim is set, else by the claim meter: below 2,
    // 30 and 60, then the full one.
    uint8_t BldgField_GroundTexture(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj)
    {
        if (obj->claimSlot != 0)
            return btype->groundTexture;
        const int16_t meter = static_cast<int16_t>(obj->claimMeter);
        if (meter < 2)
            return 0x11;
        if (meter < 30)
            return 0x16;
        return meter < 60 ? 0x17 : 0x18;
    }

    namespace
    {
        // The variant part of a building's collision mesh (the variant byte in steps of 8).
        uint32_t variantMesh(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj)
        {
            return btype->stageCollision + (obj->variant >> 3);
        }

        // Housing: part 0 is its construction stage while being built; once built, parts 0 and 1 take the unit
        // shape's first mesh and the others its second (signed offsets from the type's built collision mesh, table
        // 0x1B1520 by the unit shape).
        int housingMesh(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, uint32_t part)
        {
            const uint32_t which = part & 7u;
            const uint32_t shape = 0x001B1520u + 2u * obj->unitShape;
            if (which == 0)
            {
                if ((obj->flags & 0x20u) == 0)
                    return static_cast<int>(variantMesh(btype, obj) * 2u);
                return (btype->builtCollision + port::load<int8_t>(shape)) * 2;
            }
            return (btype->builtCollision + port::load<int8_t>(shape + (which == 1 ? 0u : 1u))) * 2;
        }

        // Offices: the block's mesh set (three per set) from the size's table of three by the tile, then part 0 the
        // block (the set's second once built, else its construction mesh), parts 1 and 2 the set's first, 3 its third,
        // 4 its second.
        int officeMesh(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, uint32_t part, uint32_t table)
        {
            const int size = btype->size;
            const int column = remainder(divide(obj->tile, size), 3);
            const uint32_t set = port::load<uint32_t>(table + 12u * static_cast<uint32_t>(size) + 4u * static_cast<uint32_t>(column));
            uint32_t mesh = btype->builtCollision + 3u * set;
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

    int BldgHousing_FloorBelow(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                             GuestPtr<uint32_t> hit, GuestPtr<uint32_t> part, ps2f lift)
    {
        return Collide_MeshFloorBelow(static_cast<int>(btype.address), housingMesh(btype, obj, *part), point, matrix, hit, lift);
    }

    int BldgHousing_NearestHit(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                             GuestPtr<uint32_t> hit, GuestPtr<ps2f> distance, GuestPtr<uint32_t> part, ps2f f12)
    {
        return Collide_MeshNearestHit(static_cast<int>(btype.address), housingMesh(btype, obj, *part), point, matrix, hit, distance, f12);
    }

    int BldgOffice_FloorBelow(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                            GuestPtr<uint32_t> hit, GuestPtr<uint32_t> part, ps2f lift)
    {
        return Collide_MeshFloorBelow(static_cast<int>(btype.address), officeMesh(btype, obj, *part, 0x001B1428u), point, matrix, hit, lift);
    }

    int BldgOffice_NearestHit(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                            GuestPtr<uint32_t> hit, GuestPtr<ps2f> distance, GuestPtr<uint32_t> part, ps2f f12)
    {
        return Collide_MeshNearestHit(static_cast<int>(btype.address), officeMesh(btype, obj, *part, 0x001B1408u), point, matrix, hit, distance, f12);
    }

    // Ports collide 2.5 higher (at ground level over their dug block): 2.5 is taken off the matrix's translation y
    // first (heights grow downwards), and it stays so.
    int BldgPort_FloorBelow(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                          GuestPtr<uint32_t> hit, ps2f lift)
    {
        const int mesh = (obj->flags & 0x20u) != 0 ? btype->builtCollision * 2 : static_cast<int>(variantMesh(btype, obj) * 2u);
        matrix[3].y = ps2f(matrix[3].y) - ps2f(2.5f);
        return Collide_MeshFloorBelow(static_cast<int>(btype.address), mesh, point, matrix, hit, lift);
    }

    int BldgPort_NearestHit(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                          GuestPtr<uint32_t> hit, GuestPtr<ps2f> distance, ps2f f12)
    {
        const int mesh = (obj->flags & 0x20u) != 0 ? btype->builtCollision * 2 : static_cast<int>(variantMesh(btype, obj) * 2u);
        matrix[3].y = ps2f(matrix[3].y) - ps2f(2.5f);
        return Collide_MeshNearestHit(static_cast<int>(btype.address), mesh, point, matrix, hit, distance, f12);
    }

    // Small goods collide only from props mesh 0x27 on, one collision mesh each.
    int BldgSmallGoods_FloorBelow(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                                GuestPtr<uint32_t> hit, ps2f lift)
    {
        const uint8_t count = obj->goodsMesh;
        if (count < 0x27)
            return 0;
        return Collide_MeshFloorBelow(static_cast<int>(btype.address), (btype->builtCollision + count - 0x27) * 2, point, matrix, hit, lift);
    }

    int BldgSmallGoods_NearestHit(GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix,
                                GuestPtr<uint32_t> hit, GuestPtr<ps2f> distance, ps2f f12)
    {
        const uint8_t count = obj->goodsMesh;
        if (count < 0x27)
            return 0;
        return Collide_MeshNearestHit(static_cast<int>(btype.address), (btype->builtCollision + count - 0x27) * 2, point, matrix, hit, distance, f12);
    }

    PORT_FN(Bldg_InitRecordBase);
    PORT_FN(Bldg_InitRecord);
    PORT_FN(Bldg_RayHit);
    PORT_FN(BldgHousing_InitRecord);
    PORT_FN(BldgRoad_InitRecord);
    PORT_FN(BldgField_GroundTexture);
    PORT_FN(BldgHousing_FloorBelow);
    PORT_FN(BldgHousing_NearestHit);
    PORT_FN(BldgOffice_FloorBelow);
    PORT_FN(BldgOffice_NearestHit);
    PORT_FN(BldgPort_FloorBelow);
    PORT_FN(BldgPort_NearestHit);
    PORT_FN(BldgSmallGoods_FloorBelow);
    PORT_FN(BldgSmallGoods_NearestHit);

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

    int CityScene_ProsperityTier(GuestPtr<CityScene> city)
    {
        if (city->prosperity < 200)
            return 0;
        return city->prosperity < 300 ? 1 : 2;
    }

    int CityScene_ConductTier(GuestPtr<CityScene> city)
    {
        if (city->conduct < 200)
            return 0;
        return city->conduct < 300 ? 1 : 2;
    }

    // The mission's progress in percent (0..100): by the stage's mission kind (the stage state's missionKind) a building count
    // of the map (kind 0 is always 100), times 100 over the target (0x1F32E0); 0 without a target. Unknown kinds use
    // v0 as it came in.
    int CityScene_MissionProgress(GuestPtr<CityScene> city)
    {
        int32_t value = port::guestV0();
        switch (static_cast<uint32_t>(globals::stageState()->missionKind))
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
        SaveFile_Write(mc, static_cast<int>(city.field(&CityScene::clock).address), 0x34);
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
        SaveFile_Write(mc, static_cast<int>(camera.field(&CityCamera::zoomLevel).address), 4);
        SaveFile_Write(mc, static_cast<int>(camera.field(&CityCamera::base).field(&Camera::eye).address), 0x1D4);
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
        return city->talk->view->forcedChoice;
    }

    PORT_FN(CityScene_AddApproval);
    PORT_FN(CityScene_ApprovalTier);
    PORT_FN(CityScene_PopulationRank);
    PORT_FN(CityScene_ProsperityTier);
    PORT_FN(CityScene_ConductTier);
    PORT_FN(CityScene_MissionProgress);
    PORT_FN(CityScene_SaveGame);
    PORT_FN(CityCamera_SaveState);
    PORT_FN(CityScene_TalkEndChoosing);
    PORT_FN(CityScene_TalkSelectPerson);

    // ---- batch 38

    // The name of a post (job) at a building type, from POSTNAME.BIN (loaded at 0x1BDBC0): an offset table by
    // post group (the type's postGroup), each group an offset table by post.
    GuestPtr<char> BldgType_GetPostName(GuestPtr<BuildingType> btype, int post)
    {
        const uint32_t names = port::load<uint32_t>(0x001BDBC0u);
        const uint32_t group = names + port::load<uint16_t>(names + 2u * btype->postGroup);
        return GuestPtr<char>{names + port::load<uint16_t>(group + 2u * static_cast<uint32_t>(post))};
    }

    int BldgType_RayHit(GuestPtr<BuildingType>) { return 0; }

    PORT_FN(BldgType_GetPostName);
    PORT_FN(BldgType_RayHit);

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
    int Wants_AddEventHold(GuestPtr<WantsWin> wants, uint16_t eventId, int value)
    {
        if (wants->count >= 64 || 256 - wants->wordsUsed <= 0)
            return 0;
        wants->index[wants->count].offset = wants->wordsUsed;
        wants->index[wants->count].held = eventId;
        wants->data[wants->wordsUsed] = static_cast<uint32_t>(value);
        wants->count = static_cast<uint16_t>(wants->count + 1);
        wants->wordsUsed = static_cast<uint16_t>(wants->wordsUsed + 1);
        return 1;
    }

    // Removes the holds of the event `eventId` with the value. As in the original, the entry that moves into a
    // removed one's place is skipped, and the later entries' offsets go down by one (those at 0 stay).
    void Wants_RemoveEventHold(GuestPtr<WantsWin> wants, uint16_t eventId, int value)
    {
        for (int i = 0; i < wants->count; ++i)
        {
            if (wants->index[i].held != eventId)
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

    // The want of a person for the claim of their home. It counts as phoned about already when the claim
    // meter is above the high threshold.
    int Wants_AddForPerson(GuestPtr<WantsWin> wants, GuestPtr<CityScene> city, GuestPtr<MapPerson> person, int claimSlot)
    {
        if (Wants_HasRoom(wants, 4) == 0)
            return 0;
        const GuestPtr<Want> want = (wants.field(&WantsWin::data) + wants->wordsUsed).as<Want>();
        want->person = person->index;
        want->object = person->home;
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
            const GuestPtr<Want> want = (wants.field(&WantsWin::data) + wants->index[i].offset).as<Want>();
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

    // The window model, and while shown (textsShown) its four text boxes.
    void WantsWin_Draw(GuestPtr<WantsWin> wants)
    {
        A3DObject_Draw(wants->model);
        if (wants->textsShown == 0)
            return;
        for (int i = 0; i < 4; ++i)
            if (const GuestPtr<TextBox> text = wants->texts[i])
                TextBox_Draw(text);
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
    // (tile height 0.8 or less) the type's built mesh; otherwise its stage mesh, at the water level.
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
            Instance_DrawMesh(globals::propsModel.get(), matrix, GuestPtr<Vec4>{globals::kDefaultColour}, btype->builtMesh, flags);
            return;
        }
        matrix[3].y = city->waterLevel;
        Instance_DrawMesh(globals::propsModel.get(), matrix, GuestPtr<Vec4>{globals::kDefaultColour}, btype->stageMesh, flags);
    }

    // Small goods: their props mesh, on lowered ground at the drawn bottom's height (Map_BottomHeight) unless the type
    // has flag 1. At night (before 4:00 or from 17:00) the colour's last word becomes the integer 12.
    void BldgSmallGoods_Draw(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                             ps2f x, ps2f y, ps2f z)
    {
        if ((btype->flags & 1u) == 0 && !(y <= ps2f(0.0f)))
            y = Map_BottomHeight(y);
        const GuestPtr<Vec4> matrix = placeLayer(layer, x, y, z);
        port::GuestLocal<Vec4> colour;
        *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
        const int time = Clock_AddWrap(static_cast<int>(btype.address), city->clock, obj->id);
        if (time >= 0xEF10 || time < 0x3840)
            port::store<int32_t>(colour.ptr().address + 0xCu, 12);
        Instance_DrawMesh(globals::propsModel.get(), matrix, colour, obj->goodsMesh, flags);
    }

    // My Home: the common tick, and when it has nothing to report, the objects of type 2 along the side the home
    // faces (the row or column next to it, one tile past each corner) lose their weeds (look[4] cleared).
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
                beside->look[4] = 0;
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
    // chance. The traffic lights then step (trafficTimer counts to 1000; phases from 0, 400, 500 and 900).
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
            CityMap_PushNewBuildings(city->map);
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

    // How a claim stands among the buildings of its items' tile lists (each item of its group, from 0x8000) that
    // overlap the rectangle: what each one supplies of the item is taken off the value (a need, wish 0: 0 as soon as
    // it is met) or added to minus the value (a wish: 0xFF from 0xFF on). The result is never below 0.
    uint32_t BldgClaims_CountNearby(GuestPtr<BuildingType>, GuestPtr<CityScene> city, GuestPtr<ClaimEntry> claim, GuestPtr<int32_t> rect)
    {
        const bool wish = claim->wish != 0;
        int32_t total = wish ? -static_cast<int32_t>(claim->value) : static_cast<int32_t>(claim->value);
        const uint16_t claimed = claim->id;
        GuestPtr<uint16_t> id = GuestPtr<uint16_t>::of(claim->id);
        if (claimed >= 0x8000)
            id = Claim_GetGroup(claimed);
        port::GuestLocal<uint32_t> count;
        do
        {
            uint32_t item = Map_GetTileList(city->map, *id, count).address;
            for (int i = 0; i < static_cast<int32_t>(*count); ++i, item += 2u)
            {
                const GuestPtr<MapObject> building = Map_GetRecord(city->map, port::load<uint16_t>(item));
                const GuestPtr<BuildingType> type{globals::buildingTypeAddress(building->type)};
                const int x = building->tile & 0x7F, z = building->tile >> 7, size = type->size;
                if (!(rect[0] < x + size && x < rect[2] && rect[1] < z + size && z < rect[3]))
                    continue;
                GuestPtr<ClaimSupply> supply = Claim_GetSupplies(type->claimC);
                while (supply->id != *id)
                    ++supply;
                if (!wish)
                {
                    total -= supply->value;
                    if (total < 1)
                        return 0;
                }
                else
                {
                    total += supply->value;
                    if (total > 0xFE)
                        return 0xFF;
                }
            }
            if (claimed < 0x8000)
                break;
            ++id;
        } while (*id != 0);
        return total < 0 ? 0u : static_cast<uint32_t>(total);
    }

    // Picks the building's claim: each claim of its type's list weighs its value among the nearby buildings
    // (mm_BldgClaims_CountNearby over the footprint widened by the claim's reach), or for id 0x32 its value while
    // the building has no entrance (0x4000). One is drawn by weight: it becomes the claim slot, its weight the
    // claim timer, and a need not at its starting value marks the building (flag 2).
    void BldgClaims_Pick(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        port::GuestFrame<0x40> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<uint8_t> weights{sp};
        const GuestPtr<int32_t> rect{sp + 0x30u};
        const GuestPtr<ClaimEntry> claims = Claim_GetList(btype->claimB);
        const int x0 = obj->tile & 0x7F, z0 = obj->tile >> 7, x1 = x0 + btype->size, z1 = z0 + btype->size;
        int sum = 0;
        uint32_t k = 0;
        for (GuestPtr<ClaimEntry> entry = claims; entry->id != 0; ++entry, ++k)
        {
            if (entry->id == 0x32)
                weights[k] = obj->entranceTile == 0x4000 ? static_cast<uint8_t>(entry->value) : 0;
            else
            {
                const int reach = entry->reach;
                rect[0] = x0 - reach;
                rect[1] = z0 - reach;
                rect[2] = x1 + reach;
                rect[3] = z1 + reach;
                weights[k] = static_cast<uint8_t>(BldgClaims_CountNearby(btype, city, entry, rect));
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
        const GuestPtr<ClaimEntry> chosen = claims + slot;
        if (chosen->wish == 0 && obj->claimTimer != chosen->value)
            obj->flags |= 2u;
    }

    // The building's claim spreads to its neighbours: over a 13 x 13 grid of every other tile around its middle
    // (weights at 0x1B8060), each building (type flag 8) met first there, and every object stacked on its tile,
    // whose own claim is the same (flag byte and id), above the low threshold, not in slot 0x18 and not maxed,
    // has its meter raised by weight x spread step; at 0xFF it is maxed (flag 0x40, hold time). A claim rising a
    // band costs a point of pending approval. Stops at the map's far rows.
    void BldgClaims_Spread(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        port::GuestFrame<0x180> frame;
        const GuestPtr<uint16_t> seen{frame.ptr().address + 0x20u};
        int seenCount = 0;
        const GuestPtr<ClaimThresholds> limits = globals::claimThresholds();
        const GuestPtr<ClaimEntry> claim = Claim_GetList(btype->claimB) + obj->claimSlot;
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
                        const GuestPtr<ClaimEntry> theirs = Claim_GetList(type->claimB) + other->claimSlot;
                        if (theirs->id == claim->id && theirs->wish == claim->wish)
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
    PORT_FN(BldgClaims_Pick);
    PORT_FN(BldgClaims_Spread);

    // ---- claims, continued (batch 39)

    namespace
    {
        // A claim's value now: for id 0x32 its value while the building has no entrance, else its value among the
        // nearby buildings (the footprint widened by the claim's reach; the rectangle in `rect`).
        uint32_t claimValue(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, GuestPtr<ClaimEntry> entry, GuestPtr<int32_t> rect)
        {
            if (entry->id == 0x32)
                return obj->entranceTile == 0x4000 ? entry->value : 0u;
            const int reach = entry->reach;
            const int x = obj->tile & 0x7F, z = obj->tile >> 7;
            rect[0] = x - reach;
            rect[1] = z - reach;
            rect[2] = x + btype->size + reach;
            rect[3] = z + btype->size + reach;
            return BldgClaims_CountNearby(btype, city, entry, rect);
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
            obj->variant = 1; // starts the count shown with the claim met
            Se_RequestForObject(obj->tile, 3, 0x1B, 0, 1, 0x28);
            Minimap_Plot(city->minimap, obj->tile, btype->size, 5, 0);
        }
    }

    // Re-checks the building's claim: below its timer value, a claim above the low threshold is met when nothing is
    // missing any more, or (a claim of need not yet marked) is marked as met with flag 2; otherwise the timer takes
    // the value. Below the threshold the claim is dropped (slot 0x18). Above the timer value the timer follows,
    // and a marked claim back at its start value loses flag 2.
    void BldgClaims_CheckMet(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        port::GuestFrame<0x20> frame;
        const GuestPtr<ClaimEntry> claim = Claim_GetList(btype->claimB) + obj->claimSlot;
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
            else if (claim->wish == 0 && (obj->flags & 2u) == 0)
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
            if ((obj->flags & 2u) != 0 && value == claim->value)
                obj->flags &= 0xFFFDu;
        }
    }

    // Weighs the building's claims against its neighbours': the table at 0x1F4B90 (0x300 bytes; per claim id, ids
    // from 0x8000 as 0x40 on, a need and a wish slot of four bytes: present, summed weights, weighted meters)
    // marks the claims the building has a value for, then the neighbours over the weighted 13 x 13 grid (as in
    // mm_BldgClaims_Spread, stopping once a slot's weights pass 200) add their meters to the slots of their claims.
    // The heaviest slot wins: when it is the building's own claim the meter moves towards the neighbours' average
    // by half a step (the threshold step, or a tenth of a larger claim timer); otherwise, with the meter at most
    // the high threshold and its share (meter x switch factor) below the slot's weighted meters, the building
    // takes that claim.
    void BldgClaims_WeighNeighbours(GuestPtr<BuildingType> btype, GuestPtr<CityScene> city, GuestPtr<MapObject> obj)
    {
        port::GuestFrame<0x1C0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<uint8_t> weights{sp + 0x20u};
        const GuestPtr<int32_t> rect{sp + 0x50u};
        const GuestPtr<uint16_t> seen{sp + 0x60u};
        const GuestPtr<ClaimThresholds> limits = globals::claimThresholds();
        // The tally of a claim: two per item (need, wish), the groups from 0x40.
        const auto slotOf = [](GuestPtr<ClaimEntry> entry) {
            uint32_t id = entry->id;
            if (id >= 0x8000)
                id -= 0x7FC0;
            return GuestPtr<ClaimTally>{0x001F4B90u + 4u * entry->wish + 8u * id};
        };
        sdk::memset(0x001F4B90u, 0, 0x300);
        const GuestPtr<ClaimEntry> claims = Claim_GetList(btype->claimB);
        uint32_t k = 0;
        for (GuestPtr<ClaimEntry> entry = claims; entry->id != 0; ++entry, ++k)
        {
            weights[k] = static_cast<uint8_t>(claimValue(btype, city, obj, entry, rect));
            if (weights[k] != 0)
                slotOf(entry)->own = 1;
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
                        const GuestPtr<ClaimTally> slot = slotOf(Claim_GetList(type->claimB) + other->claimSlot);
                        if (slot->own != 0)
                        {
                            slot->meters = static_cast<uint16_t>(slot->meters + other->claimMeter * port::load<uint8_t>(weight));
                            slot->weight = static_cast<uint8_t>(slot->weight + port::load<uint8_t>(weight));
                            if (slot->weight > 200)
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
            const GuestPtr<ClaimTally> slot = GuestPtr<ClaimTally>{0x001F4B90u} + static_cast<int32_t>(i);
            if (slot->own == 0 || !(bestMeters < slot->meters))
                continue;
            bestId = i >> 1;
            bestFlag = i & 1u;
            bestWeights = slot->weight;
            bestMeters = slot->meters;
        }
        if (bestWeights == 0)
            return;
        if (static_cast<int32_t>(bestId) > 0x3F)
            bestId += 0x7FC0;
        const GuestPtr<ClaimEntry> own = claims + obj->claimSlot;
        if (bestId == own->id && bestFlag == own->wish)
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
        for (GuestPtr<ClaimEntry> entry = claims; entry->id != 0; ++entry, ++index)
        {
            if (entry->id != bestId || entry->wish != bestFlag)
                continue;
            obj->claimSlot = static_cast<uint8_t>(index);
            obj->claimTimer = weights[index];
            if (entry->wish == 0 && obj->claimTimer != entry->value)
                obj->flags |= 2u;
            else
                obj->flags &= 0xFFFDu;
            return;
        }
    }

    PORT_FN(BldgClaims_CheckMet);
    PORT_FN(BldgClaims_WeighNeighbours);

    // ---- small goods and leveled ground (batch 39)

    namespace
    {
        // The height a flat building settles to (heights grow downwards). On lowered ground (a positive tile height) it
        // is `lowered`, just below ground level. Raised ground is cut to the half-unit grid: a remainder (fmod 0.5) over
        // 0.1 takes 0.1 off, one of -0.4 or less goes on to the next half unit up, any other takes the remainder off.
        ps2f settledHeight(ps2f height, ps2f lowered)
        {
            if (!(height <= ps2f(0.0f)))
                return lowered;
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
    // (lowered ground to 0.1) and the area around it is rebuilt; then the minimap shows it (colour 1).
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
    // that are not roads (type 3). Sides that are full count; which sides are needed depends on how long applicants
    // have waited (CityMap.applicantWait): below 2 two sides of flag 2 or 0x40 (and with `needsFlag2`, one of flag 2), 2 the same
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
        const int mode = city->map->applicantWait;
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

    // Leveled ground: openSize (the side of the open square of leveled land from here) takes the smallest of the
    // neighbours' (x + 1, z), (x + 1, z + 1) and (x, z + 1) that are leveled ground at the same height (else 0), plus
    // one while the object has no flag 1. The ground settles (lowered ground to 0.05), and every waiting applicant
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
            word[k] = open && heightAt[k] == ground ? neighbour[k]->openSize : 0;
        }
        int32_t least = word[0] < word[1] ? (word[2] <= word[0] ? word[2] : word[0]) : (word[2] <= word[1] ? word[2] : word[1]);
        obj->openSize = least + ((flags & 1u) == 0 ? 1 : 0);
        const ps2f height = settledHeight(ground, ps2f(0.05f));
        if (height != ground)
            Map_SetTileHeight(city->map, obj->tile, height);
        const GuestPtr<uint32_t> cursor{frame.ptr().address + 0xBCu};
        *cursor = 0;
        for (uint32_t applicant = static_cast<uint32_t>(Applicants_FindFrom(city->applicants, cursor, 2)); applicant != 0;
             applicant = static_cast<uint32_t>(Applicants_FindFrom(city->applicants, cursor, 2)))
        {
            const uint32_t family = GuestPtr<ApplicantEntry>{applicant}->family;
            const GuestPtr<BuildingType> home{globals::buildingTypeAddress(static_cast<uint16_t>(Family_GetTypeIndex(static_cast<int>(family))))};
            const uint32_t claim = static_cast<uint32_t>(obj->openSize);
            if (!(home->size <= claim))
                continue;
            if (BldgLeveled_CheckArea(btype, city, x, z, home->size, home->flags & 0x20u) == 0)
                continue;
            if (Map_CanPlace(city->map, static_cast<int>(family), GuestPtr<ApplicantEntry>{applicant}->type, x, z) == 0)
                continue;
            Map_HighlightArea(city->map, x, z, home->size);
            GuestPtr<ApplicantEntry>{applicant}->state = 3;
            GuestPtr<ApplicantEntry>{applicant}->tile = obj->tile;
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
        // The applicants card text: its place and size follow the card model's values (the x, y and, as the scale,
        // z of the translation row of cardNodeA): the pixel offsets scale with the model, the text box
        // shrinks with it.
        void placeCardText(GuestPtr<Applicants> applicants, int x, int y, ps2f scale)
        {
            const int xOffset = sdk::fptosi(ps2f(45.0f) * scale);
            const int yOffset = sdk::fptosi(ps2f(7.1f) * scale);
            const int width1 = 0xC0 - sdk::fptosi(ps2f(16.8f) * scale);
            const int width2 = 0xB0 - sdk::fptosi(ps2f(16.0f) * scale);
            const int width3 = 0xB0 - sdk::fptosi(ps2f(14.7f) * scale);
            const int width4 = 0xC0 - sdk::fptosi(ps2f(16.3f) * scale);
            TextBox_SetPosAndSize(applicants->cardText, (x + 0x77E + xOffset) * 0x10,
                                  ((y + 0x7D6) - yOffset) * 0x10, width1, width2, width3, width4, -0x100000);
        }
    }

    // The card slides in over the first 20 frames of its counter (slideFrame; its x grows by 0.15 a frame), stays, and
    // from frame 60 for 6 frames drifts (its y grows by 0.4 a frame); after that the counter, sliding and cardShown clear,
    // the cursor and the top entry take the saved entry (savedTop) and the list scrolls back to its first entry.
    void Applicants_Layout(GuestPtr<Applicants> applicants)
    {
        applicants->slideFrame = applicants->slideFrame + 1;
        const int32_t frame = applicants->slideFrame;
        Vec4 &place = applicants->cardNodeA[3];
        if (frame < 0x14)
        {
            place.x = ps2f(place.x) + ps2f(0.15f);
            const ps2f x = place.x, scale = place.z, y = place.y;
            const int pixelX = sdk::fptosi(ps2f(144.0f) * (ps2f(0.015f) + ps2f(1.1f) * x));
            const int pixelY = sdk::fptosi(ps2f(72.0f) * (y - ps2f(0.03f)));
            placeCardText(applicants, pixelX, pixelY, scale);
        }
        else if (frame >= 0x3C)
        {
            if (frame < 0x42)
            {
                place.y = ps2f(place.y) + ps2f(0.4f);
                const ps2f x = place.x, scale = place.z, y = place.y;
                const int pixelX = sdk::fptosi(ps2f(144.0f) * x);
                const int pixelY = sdk::fptosi(ps2f(72.0f) * (ps2f(1.11f) * y - ps2f(0.1f) * y));
                placeCardText(applicants, pixelX, pixelY, scale);
            }
            else
            {
                applicants->sliding = 0;
                const int32_t saved = applicants->savedTop;
                applicants->top = saved;
                applicants->cursor = saved;
                applicants->slideFrame = 0;
                Applicants_ScrollTo(applicants, -1);
                applicants->cardShown = 0;
            }
        }
    }

    PORT_FN(Applicants_Layout);

    // ---- the pig farm (batch 39)

    // Pig farms are drawn like theme parks: lit after dark within the opening hours of the object's type, the
    // built one from its own model (placed by the object's matrix), one being built from the city parts.
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
                if ((globals::stageState()->flags & 1u) != 0)
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
        TextBox_SetText(laptop->text, GuestPtr<char>{0x001AF6B0u + (line << 7)}, 0);
    }

    PORT_FN(Laptop_SetItemText);

    // ---- the people of a building (batch 40)

    namespace
    {
        GuestPtr<CharClass> classOf(GuestPtr<MapPerson> person)
        {
            return GuestPtr<CharClass>{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
        }

        // The character class's draw method for `person`: drawn at `position` (x, y, z, heading)
        // with `colour` in the given pose.
        void drawPerson(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> person, uint32_t colour, int flags, uint32_t position, int pose)
        {
            port::callGuest<void>(cls->vtable->draw, cls.address, city.address, person.address, colour, flags, position, pose);
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
    // rest share the second) and drawn in pose 5 (9 when the field has a claim, lifted by the type's people lift);
    // anyone else is drawn as an occupant.
    void BldgField_DrawPeople(GuestPtr<BuildingType> btype, int, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                              GuestPtr<MapPerson> people, ps2f x, ps2f y, ps2f z)
    {
        port::GuestFrame<0xA0> frame;
        const GuestPtr<Vec4> colour{frame.ptr().address + 0x80u}, position{frame.ptr().address + 0x90u};
        const GuestPtr<Vec4> spots{0x001B1460u}; // the first two members' spots, the rest share the second
        for (GuestPtr<uint16_t> link = GuestPtr<uint16_t>::of(obj->firstPerson); *link != kNoPerson;)
        {
            const GuestPtr<MapPerson> person = people + *link;
            const GuestPtr<CharClass> cls = classOf(person);
            const bool onField = (cls->flags & 1u) == 0 && person->activity == 0 && (person->inside == kNoObject || person->inside == obj->id);
            if (!onField)
            {
                BldgType_DrawOccupants(btype, city, obj, person, flags);
                link = GuestPtr<uint16_t>::of(person->nextHere);
                continue;
            }
            *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
            const GuestPtr<Vec4> spot = spots + (person->member < 2 ? static_cast<int32_t>(person->member) : 1);
            const ps2f px = x + ps2f(spot->x);
            person->x = px;
            position->x = static_cast<float>(px);
            const ps2f py = y + ps2f(spot->y);
            person->y = py;
            position->y = static_cast<float>(py);
            const ps2f pz = z + ps2f(spot->z);
            person->z = pz;
            position->z = static_cast<float>(pz);
            position->w = static_cast<float>(person->heading);
            int pose = 5;
            if (obj->claimSlot != 0)
            {
                pose = 9;
                position->y = static_cast<float>(ps2f(position->y) + btype.as<FieldType>()->peopleLift);
            }
            drawPerson(cls, city, person, colour.address, flags, position.address, pose);
            link = GuestPtr<uint16_t>::of(person->nextHere);
        }
    }

    // The ranch: while being built its plain part from the city parts model, once built (flag 0x20) the ranch models (the first always, then one
    // of two by the type's isOpen answer), each at the building's place.
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
        const int64_t open = port::callGuest<int64_t>(btype->vtable->isOpen, btype, city, obj);
        drawAt(open == 0 ? globals::ranchModelC.get() : globals::ranchModelB.get());
    }

    // A road: with the city flag 8 off its people are drawn as in a building (BldgType_DrawOccupants2); with it on, the
    // residents that are neither class flag 1 nor person flag 0x100 are gathered by person index (up to four are drawn)
    // and placed on the road by its shape (the corner table at 0x1B1670, 4 slots a shape) in pose 8, each at its ground
    // height facing the table's way; the others are drawn as occupants.
    void BldgRoad_DrawPeople(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int t0, int flags,
                             GuestPtr<MapPerson> people, ps2f x, ps2f y, ps2f z)
    {
        port::GuestFrame<0x120> frame;
        const uint32_t base = frame.ptr().address;
        const GuestPtr<uint16_t> list{base + 0xC0u}; // the people gathered, by person index
        const GuestPtr<Vec4> position{base + 0x100u}, colour{base + 0x110u};
        if (CityScene_InFarewellParty(city) == 0)
        {
            BldgType_DrawOccupants2(btype, layer, city, obj, t0, flags, people);
            return;
        }
        int count = 0;
        for (GuestPtr<uint16_t> link = GuestPtr<uint16_t>::of(obj->firstPerson); *link != kNoPerson;)
        {
            const GuestPtr<MapPerson> person = people + *link;
            if ((classOf(person)->flags & 1u) != 0 || (person->flags & 0x100u) != 0)
            {
                BldgType_DrawOccupants(btype, city, obj, person, flags);
                link = GuestPtr<uint16_t>::of(person->nextHere);
                continue;
            }
            int place = 0;
            while (count > 0)
            {
                if (person->index < list[place])
                {
                    for (int k = count; k > place; --k)
                        list[k] = list[k - 1];
                    break;
                }
                ++place;
                if (!(place < count))
                    break;
            }
            list[place] = person->index;
            ++count;
            link = GuestPtr<uint16_t>::of(person->nextHere);
        }
        // Each road shape has four corner numbers; by corner, its x and z offsets and its heading number (the headings
        // are the table the building people use by layer).
        const GuestPtr<uint32_t> shapeCorners{0x001B1670u}, cornerHeading{0x001B1640u};
        const GuestPtr<float> cornerX{0x001B15E0u}, cornerZ{0x001B1610u}, headings{0x001B2690u};
        for (int i = 0; i < count && i < 4; ++i)
        {
            const GuestPtr<MapPerson> person = people + list[i];
            const uint32_t corner = shapeCorners[4 * static_cast<int32_t>(obj->roadShape) + i];
            const ps2f px = x + ps2f(cornerX[static_cast<int32_t>(corner)]);
            person->x = px;
            position->x = static_cast<float>(px);
            const ps2f pz = z + ps2f(cornerZ[static_cast<int32_t>(corner)]);
            person->z = pz;
            position->z = static_cast<float>(pz);
            const ps2f py = Map_SampleHeight(city->map, person->x, person->z);
            person->y = py;
            position->y = static_cast<float>(py);
            const float heading = headings[static_cast<int32_t>(cornerHeading[static_cast<int32_t>(corner)])];
            person->heading = heading;
            position->w = heading;
            *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
            BldgType_DrawOccupant(btype, colour, person, city);
            drawPerson(classOf(person), city, person, colour.address, flags, position.address, 8);
        }
    }

    PORT_FN(BldgType_DrawOccupants);
    // The crops of a field (nothing while its object has no claim slot): the matrix of the layer is put at the field's
    // place (a little higher by the type's crop lift), and the month cycle (twelve steps, two to a stage of six) picks the
    // stage: its crop mesh is drawn as it is; its growing mesh follows with
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
        const GuestPtr<FieldType> field = btype.as<FieldType>();
        const GuestPtr<Vec4> matrix = layerMatrix(static_cast<uint32_t>(layer));
        matrix[3].x = x;
        matrix[3].y = y + field->cropLift;
        matrix[3].z = z;
        const int stage = step >> 1;
        const uint8_t mesh = field->cropMesh[stage];
        if (mesh != 0)
            Instance_DrawMesh(globals::fieldModel.get(), matrix, GuestPtr<Vec4>{globals::kDefaultColour}, mesh, flags);
        if (field->growingMesh[stage] == 0)
            return;
        const ps2f progress = fromInt(static_cast<int32_t>(static_cast<uint32_t>((step & 1) + 1) * static_cast<uint32_t>(city->clock))) / ps2f(172800.0f);
        const GuestPtr<Vec4> scale{base + 0x90u};
        Mat4_Copy(scale, GuestPtr<Vec4>{globals::kIdentityMatrix});
        const int next = stage + 1 < 6 ? stage + 1 : 0;
        const ps2f following = field->growingHeight[next];
        const ps2f current = field->growingHeight[stage];
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
        Instance_DrawMesh(globals::fieldModel.get(), scale, GuestPtr<Vec4>{globals::kDefaultColour}, field->growingMesh[stage], flags);
    }

    PORT_FN(BldgField_Draw);
    PORT_FN(BldgField_DrawPeople);
    PORT_FN(BldgRanch_Draw);
    PORT_FN(BldgRoad_DrawPeople);

    // ---- the people of a built building (batch 40)

    // Built buildings (flag 0x20) of type flag 0x800: the people standing in it (or outside, on it) are sorted into
    // four groups, by class flag 1 (vehicles) and by whether this is their home: 0 residents living here,
    // 1 other residents, 2 vehicles of this building, 3 other vehicles. Each group has room for `groupSpots[group]` people (up to four)
    // that are placed at the type's people spots (x, y, z, a heading added to the table's at 0x1B2690),
    // drawn in pose 4. Group 0 is kept in order of the people's member numbers, the others by person index, but a person
    // inserted always lands in slot 0 (the original writes slot 0 after shifting the ones below it up). While the
    // building is not open (isOpen slot) the groups are skipped, except group 2. The spot record number runs on over the groups drawn.
    void Bldg_DrawPeople(GuestPtr<BuildingType> btype, int layer, GuestPtr<CityScene> city, GuestPtr<MapObject> obj, int, int flags,
                         GuestPtr<MapPerson> people, ps2f x, ps2f y, ps2f z)
    {
        port::GuestFrame<0x250> frame;
        const uint32_t base = frame.ptr().address;
        // The groups' counts and lists (0x20 people each), the slots of the group being placed, the position and colour
        // handed to the draw.
        const GuestPtr<int32_t> counts{base + 0x110u};
        const GuestPtr<uint16_t> lists{base + 0x120u}, slots{base + 0x248u};
        const GuestPtr<Vec4> position{base + 0x220u}, colour{base + 0x230u};
        const auto list = [&](uint32_t group) { return lists + static_cast<int32_t>(0x20u * group); };
        const GuestPtr<float> headings{0x001B2690u}; // by layer
        if ((btype->flags & 0x800u) == 0 || (obj->flags & 0x20u) == 0)
            return;
        const int32_t open = static_cast<int32_t>(port::callGuest<int64_t>(btype->vtable->isOpen, btype, city, obj));
        for (int32_t k = 0; k < 4; ++k)
            counts[k] = 0;
        for (GuestPtr<uint16_t> link = GuestPtr<uint16_t>::of(obj->firstPerson); *link != kNoPerson;)
        {
            const GuestPtr<MapPerson> person = people + *link;
            link = GuestPtr<uint16_t>::of(person->nextHere);
            if (person->inside != obj->id && person->inside != kNoObject)
                continue;
            const bool flagged = (classOf(person)->flags & 1u) != 0;
            const uint32_t group = (flagged ? 2u : 0u) + (person->home == obj->id ? 0u : 1u);
            const int32_t n = counts[static_cast<int32_t>(group)];
            list(group)[n] = person->index;
            counts[static_cast<int32_t>(group)] = n + 1;
        }
        uint32_t running = static_cast<uint32_t>(layer);
        for (uint32_t group = 0; group < 4; ++group)
        {
            if (open == 0 && group != 2)
                continue;
            const uint32_t limit = btype->groupSpots[group];
            if (limit == 0 || counts[static_cast<int32_t>(group)] == 0)
                continue;
            for (uint32_t i = 0; i < limit; ++i)
                slots[static_cast<int32_t>(i)] = kNoPerson;
            for (int32_t c = 0; c < counts[static_cast<int32_t>(group)]; ++c)
            {
                const uint32_t candidate = list(group)[c];
                for (uint32_t pos = 0; pos < limit; ++pos)
                {
                    const uint32_t held = slots[static_cast<int32_t>(pos)];
                    if (held != kNoPerson)
                    {
                        const bool before = group == 0 ? (people[static_cast<int32_t>(candidate)].member < people[static_cast<int32_t>(held)].member)
                                                       : candidate < held;
                        if (!before)
                            continue;
                    }
                    for (uint32_t k = limit - 1; pos < k; --k)
                        slots[static_cast<int32_t>(k)] = slots[static_cast<int32_t>(k - 1)];
                    slots[0] = static_cast<uint16_t>(candidate);
                    break;
                }
            }
            for (uint32_t i = 0; i < limit; ++i, running += 4u)
            {
                const uint32_t held = slots[static_cast<int32_t>(i)];
                if (held == kNoPerson)
                    continue;
                const GuestPtr<MapPerson> person = people + held;
                // Spot `running` of the type; past the sixteen it reads on into the type's later fields, as the game does.
                const GuestPtr<Vec4> spot = GuestPtr<Vec4>::of(btype->peopleSpots[0]) + static_cast<int32_t>(running);
                position->x = static_cast<float>(x + spot->x);
                position->y = static_cast<float>(y + spot->y);
                position->z = static_cast<float>(z + spot->z);
                const ps2f heading = ps2f(headings[layer]) + spot->w;
                person->heading = heading;
                const ps2f pi = ps2f(3.1415927f);
                if (heading <= -pi)
                    position->w = static_cast<float>(heading + pi);
                else if (heading <= pi)
                    position->w = static_cast<float>(heading);
                else
                {
                    position->w = static_cast<float>(heading);
                    position->w = static_cast<float>(ps2f(position->w) - ps2f(6.2831855f));
                }
                *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
                BldgType_DrawOccupant(btype, colour, person, city);
                drawPerson(classOf(person), city, person, colour.address, flags, position.address, 4);
            }
        }
    }

    PORT_FN(Bldg_DrawPeople);

    // ---- Building type destructors (plan P6 batch 2) ----

    namespace
    {
        // The destructor of a building type class: the vtable pointer (vtable) steps down the class chain (the
        // derived class's vtable first, the base class's last), then the base destructor and the delete.
        GuestPtr<BuildingType> destroyBuildingType(GuestPtr<BuildingType> btype, int16_t deleteFlag,
                                                   std::initializer_list<uint32_t> vtables)
        {
            if (btype)
            {
                for (const uint32_t vtable : vtables)
                    btype->vtable = GuestPtr<BuildingTypeVtable>{vtable};
                BldgType_BaseDtor(btype, 0);
                if (0 < deleteFlag)
                    sdk::operator_delete(btype.address);
            }
            return btype;
        }

        constexpr uint32_t kVtableBldg = 0x001BD100u;
        constexpr uint32_t kVtableBusiness = 0x001BD1C0u;
    }

    GuestPtr<BuildingType> Bldg_dtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        return destroyBuildingType(btype, deleteFlag, {kVtableBldg});
    }

    GuestPtr<BuildingType> BldgField_dtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        return destroyBuildingType(btype, deleteFlag, {0x001BD140u});
    }

    GuestPtr<BuildingType> BldgKeepOut_dtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        return destroyBuildingType(btype, deleteFlag, {0x001BD200u});
    }

    GuestPtr<BuildingType> BldgLeveled_dtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        return destroyBuildingType(btype, deleteFlag, {0x001BD240u});
    }

    GuestPtr<BuildingType> BldgRoad_dtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        return destroyBuildingType(btype, deleteFlag, {0x001BD340u});
    }

    GuestPtr<BuildingType> BldgSmallGoods_dtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        return destroyBuildingType(btype, deleteFlag, {0x001BD380u});
    }

    GuestPtr<BuildingType> BldgWasteland_dtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        return destroyBuildingType(btype, deleteFlag, {0x001BD3C0u});
    }

    // The business classes derive from the business base class, which derives from the building base class.
    GuestPtr<BuildingType> BldgBusiness_dtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        return destroyBuildingType(btype, deleteFlag, {kVtableBusiness, kVtableBldg});
    }

    GuestPtr<BuildingType> BldgAmusementPark_dtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        return destroyBuildingType(btype, deleteFlag, {0x001BD040u, kVtableBusiness, kVtableBldg});
    }

    GuestPtr<BuildingType> BldgOffice_dtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        return destroyBuildingType(btype, deleteFlag, {0x001BD080u, kVtableBusiness, kVtableBldg});
    }

    GuestPtr<BuildingType> BldgRanch_dtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        return destroyBuildingType(btype, deleteFlag, {0x001BD0C0u, kVtableBusiness, kVtableBldg});
    }

    GuestPtr<BuildingType> BldgPort_dtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        return destroyBuildingType(btype, deleteFlag, {0x001BD180u, kVtableBusiness, kVtableBldg});
    }

    GuestPtr<BuildingType> BldgHousing_dtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        return destroyBuildingType(btype, deleteFlag, {0x001BD280u, kVtableBusiness, kVtableBldg});
    }

    GuestPtr<BuildingType> BldgMyHome_dtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        return destroyBuildingType(btype, deleteFlag, {0x001BD2C0u, kVtableBusiness, kVtableBldg});
    }

    GuestPtr<BuildingType> BldgPigFarm_dtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        return destroyBuildingType(btype, deleteFlag, {0x001BD300u, kVtableBusiness, kVtableBldg});
    }

    PORT_FN(Bldg_dtor);
    PORT_FN(BldgField_dtor);
    PORT_FN(BldgKeepOut_dtor);
    PORT_FN(BldgLeveled_dtor);
    PORT_FN(BldgRoad_dtor);
    PORT_FN(BldgSmallGoods_dtor);
    PORT_FN(BldgWasteland_dtor);
    PORT_FN(BldgBusiness_dtor);
    PORT_FN(BldgAmusementPark_dtor);
    PORT_FN(BldgOffice_dtor);
    PORT_FN(BldgRanch_dtor);
    PORT_FN(BldgPort_dtor);
    PORT_FN(BldgHousing_dtor);
    PORT_FN(BldgMyHome_dtor);
    PORT_FN(BldgPigFarm_dtor);

    // ---- The city scene's destructor (plan P6 batch 2) ----

    // Deletes everything the city scene owns, in this order: pause menu, minimap, cursor, phone calls, event manager, wants
    // window, applicants window, player, map, lights, city camera, laptop, Town's Pages, camera, status panel, the talk, the
    // markers, the notice, the overlay object; then all building types and the stage object base (flag 0). Each child is
    // deleted with flag 1 and its field cleared; the applicants window, talk, markers and overlay delete themselves through
    // their own vtables (the deleting destructor is slot 2).
    GuestPtr<CityScene> CityScene_dtor(GuestPtr<CityScene> city, int16_t deleteFlag)
    {
        if (!city)
            return city;
        const auto direct = [&](uint32_t offset, auto destroy) {
            const uint32_t object = port::load<uint32_t>(city.address + offset);
            if (object != 0u)
            {
                destroy(object);
                port::store<uint32_t>(city.address + offset, 0u);
            }
        };
        const auto virtualDelete = [&](uint32_t offset, uint32_t vtableAt) {
            const uint32_t object = port::load<uint32_t>(city.address + offset);
            if (object != 0u)
            {
                port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(object + vtableAt) + 8u), object, 1);
                port::store<uint32_t>(city.address + offset, 0u);
            }
        };
        direct(0x08u, [](uint32_t o) { PauseMenu_dtor(GuestPtr<PauseMenu>{o}, 1); });
        direct(0x74u, [](uint32_t o) { Minimap_DeletingDtor(GuestPtr<Minimap>{o}, 1); });
        direct(0x70u, [](uint32_t o) { CityCursor_dtor(GuestPtr<CityCursor>{o}, 1); });
        direct(0x6Cu, [](uint32_t o) { PhoneCall_DeletingDtor(GuestPtr<PhoneCalls>{o}, 1); });
        direct(0x68u, [](uint32_t o) { EventMgr_dtor(GuestPtr<EventMgr>{o}, 1); });
        direct(0x5Cu, [](uint32_t o) { WantsWin_dtor(GuestPtr<WantsWin>{o}, 1); });
        virtualDelete(0x54u, 0x320u);
        direct(0x58u, [](uint32_t o) { Player_DeletingDtor(GuestPtr<Player>{o}, 1); });
        direct(0x50u, [](uint32_t o) { CityMap_DeletingDtor(GuestPtr<CityMap>{o}, 1); });
        direct(0x40u, [](uint32_t o) { LightSet_DeletingDtor(GuestPtr<LightSet>{o}, 1); });
        direct(0x4Cu, [](uint32_t o) { CityCamera_DeletingDtor(GuestPtr<CityCamera>{o}, 1); });
        direct(0x48u, [](uint32_t o) { Laptop_dtor(GuestPtr<Laptop>{o}, 1); });
        direct(0x04u, [](uint32_t o) { TownPages_dtor(GuestPtr<TownPages>{o}, 1); });
        direct(0x44u, [](uint32_t o) { Camera_DeletingDtor(GuestPtr<Camera>{o}, 1); });
        direct(0x00u, [](uint32_t o) { StatusPanel_dtor(GuestPtr<StatusPanel>{o}, 1); });
        virtualDelete(0x3Cu, 8u);
        virtualDelete(0x78u, 0x128u);
        direct(0x7Cu, [](uint32_t o) { Notice_dtor(GuestPtr<Notice>{o}, 1); });
        virtualDelete(0x80u, 0u);
        BldgType_DeleteAll();
        StageObj_DeletingDtor(static_cast<int>(city.address), 0);
        if (0 < deleteFlag)
            sdk::operator_delete(city.address);
        return city;
    }

    PORT_FN(CityScene_dtor);

    // ---- Saving from the pause menu (plan P6 batch 2) ----

    // One call per frame of the save: state 0 opens the file (mode 0x203), 1 waits for it (an open that failed, a negative
    // descriptor, ends it with 2), 2 packs the map's tiles and starts the write, 3 waits for the write, frees the pack buffer
    // and ends with 1 (done) or 2 (the write failed). The other states do nothing. 0 while it is not finished.
    int CityScene_SaveStep(GuestPtr<CityScene> city, GuestPtr<McManager> mc)
    {
        constexpr uint32_t kStarted = 0x001BDC60u, kState = 0x001BDC5Cu, kRequest = 0x001BDC64u;
        if (port::load<int8_t>(kStarted) == 0)
        {
            port::store<int32_t>(kState, 0);
            port::store<uint8_t>(kStarted, 1);
        }
        const auto advance = [&] { port::store<int32_t>(kState, port::load<int32_t>(kState) + 1); };
        switch (port::load<int32_t>(kState))
        {
        case 0:
            port::store<int32_t>(kRequest, SaveFile_Open(mc, 0x203));
            advance();
            return 0;
        case 1:
            if (SaveFile_IsDone(mc, port::load<int32_t>(kRequest)) == 0)
                return 0;
            if (mc->fd < 0)
            {
                port::store<int32_t>(kState, 0);
                return 2;
            }
            advance();
            return 0;
        case 2:
            MapSave_PackTiles(city->map);
            port::store<int32_t>(kRequest, CityScene_SaveGame(city, mc));
            advance();
            return 0;
        case 3:
            if (SaveFile_IsDone(mc, port::load<int32_t>(kRequest)) == 0)
                return 0;
            MapSave_FreeBuffer(city->map);
            port::store<int32_t>(kState, 0);
            return SaveFile_GetResult(mc, port::load<int32_t>(kRequest)) < 0 ? 2 : 1;
        default:
            return 0;
        }
    }

    PORT_FN(CityScene_SaveStep);

    // ---- The building type base constructor (plan P6 batch 2) ----

    // Fills the type object `btype` from its record in the building data file (`record`) for type number `id`: the table, the
    // name, the sizes, the opening hours, the meshes, and the four people spots, each turned for the four views (through the
    // view matrices at 0x1B2590, keeping the record's heading). The first type made loads the shared file 0x1B8040 and copies
    // the shared models; later ones only count (0x1BDBBC). A type with spots for any group of people also gets flag 0x800.
    GuestPtr<BuildingType> BldgType_BaseCtor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, uint16_t id)
    {
        port::GuestFrame<0xC0> frame;
        const uint32_t sp = frame.ptr().address;
        constexpr uint32_t kTypeCount = 0x001BDBBCu;
        btype->vtable = GuestPtr<BuildingTypeVtable>{0x001BD000u};
        if (port::load<int32_t>(kTypeCount) == 0)
        {
            const uint32_t models[] = {0x001F3B44u, 0x001F3B48u, 0x001F3B64u, 0x001F3B60u, 0x001F3B74u, 0x001F3B78u};
            for (uint32_t i = 0; i < 6u; ++i)
                port::store<uint32_t>(0x001BDBC4u + 4u * i, port::load<uint32_t>(models[i]));
            port::store<uint32_t>(0x001BDBDCu, port::load<uint32_t>(0x001F3CE4u));
            File_ctor(GuestPtr<GameFile>{sp + 0xB0u}, 0);
            port::store<int32_t>(0x001BDBC0u, File_LoadWhole(GuestPtr<GameFile>{sp + 0xB0u}, GuestPtr<char>{0x001B8040u}, GuestPtr<int32_t>{sp + 0xBCu}));
            File_dtor(GuestPtr<GameFile>{sp + 0xB0u}, -1);
        }
        port::store<int32_t>(kTypeCount, port::load<int32_t>(kTypeCount) + 1);
        btype->kind = id;
        sdk::strcpy(port::guest(btype->name), port::guest(record->name));
        btype->size = record->size;
        btype->groundTexture = record->groundTexture;
        btype->rule = record->rule;
        btype->flags = record->flags;
        btype->maxStack = record->maxStack;
        btype->category = record->category;
        btype->postGroup = record->postGroup;
        btype->claimB = record->claimB;
        btype->claimC = record->claimC;
        btype->unk_149 = record->unk_2C;
        btype->maxVisitors = record->maxVisitors;
        btype->schedule = record->schedule;
        btype->activity = record->activity;
        btype->fieldType = record->fieldType;
        btype->openMinute = record->openMinute;
        btype->closeMinute = record->closeMinute;
        btype->capacity = record->capacity;
        btype->cooldown = record->cooldown;
        btype->ambientSound = record->ambientSound;
        btype->builtMesh = record->builtMesh;
        btype->stageMesh = record->stageMesh;
        btype->openNode = record->openNode;
        btype->modelNode = record->modelNode;
        btype->builtCollision = record->builtCollision;
        btype->stageCollision = record->stageCollision;
        btype->talkEye = record->talkEye;
        sdk::memcpy(port::guest(&btype->callFlags), port::guest(&record->callFlags), 4);
        sdk::memcpy(port::guest(btype->groupSpots), port::guest(record->groupSpots), 4);
        const GuestPtr<Vec4> spot{sp + 0xA0u};
        for (int k = 0; k < 4; ++k)
        {
            *spot = record->peopleSpots[k];
            for (int view = 0; view < 4; ++view)
            {
                const GuestPtr<Vec4> matrix = GuestPtr<Vec4>{0x001B2590u} + 4 * view;
                matrix[3] = *GuestPtr<Vec4>{0x001B26A0u}; // no translation
                const GuestPtr<Vec4> out = GuestPtr<Vec4>::of(btype->peopleSpots[4 * k + view]);
                Math_MulMatVecVu0(out, matrix, spot);
                out->w = spot->w; // the heading stays
            }
        }
        if (btype->groupSpots[0] + btype->groupSpots[1] + btype->groupSpots[2] + btype->groupSpots[3] != 0)
            btype->flags = static_cast<uint16_t>(btype->flags | 0x800u);
        return btype;
    }

    PORT_FN(BldgType_BaseCtor);

    // ---- The building type constructors (plan P6 batch 2) ----
    //
    // Each runs the constructor of its base (the shared one, or the business one for the types built on the business type) with the
    // same arguments, then puts its own table in vtable, and takes what else it needs from the record.

    namespace
    {
        constexpr uint32_t kBaseCtor = 0, kBldg = 1, kBusiness = 2;

        GuestPtr<BuildingType> constructBuildingType(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id, int base, uint32_t table)
        {
            if (base == kBaseCtor)
                BldgType_BaseCtor(btype, record, static_cast<uint16_t>(id));
            else if (base == kBldg)
                Bldg_ctor(btype, record, id);
            else
                BldgBusiness_ctor(btype, record, id);
            btype->vtable = GuestPtr<BuildingTypeVtable>{table};
            return btype;
        }

    }

    GuestPtr<BuildingType> Bldg_ctor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id)
    {
        return constructBuildingType(btype, record, id, kBaseCtor, 0x001BD100u);
    }

    GuestPtr<BuildingType> BldgBusiness_ctor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id)
    {
        return constructBuildingType(btype, record, id, kBldg, 0x001BD1C0u);
    }

    GuestPtr<BuildingType> BldgPort_ctor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id)
    {
        return constructBuildingType(btype, record, id, kBusiness, 0x001BD180u);
    }

    GuestPtr<BuildingType> BldgKeepOut_ctor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id)
    {
        return constructBuildingType(btype, record, id, kBaseCtor, 0x001BD200u);
    }

    GuestPtr<BuildingType> BldgHousing_ctor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id)
    {
        return constructBuildingType(btype, record, id, kBusiness, 0x001BD280u);
    }

    GuestPtr<BuildingType> BldgMyHome_ctor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id)
    {
        return constructBuildingType(btype, record, id, kBusiness, 0x001BD2C0u);
    }

    GuestPtr<BuildingType> BldgSmallGoods_ctor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id)
    {
        return constructBuildingType(btype, record, id, kBaseCtor, 0x001BD380u);
    }

    GuestPtr<BuildingType> BldgWasteland_ctor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id)
    {
        return constructBuildingType(btype, record, id, kBaseCtor, 0x001BD3C0u);
    }

    // The pig farm shares the model at 0x1F3B5C (kept at 0x1BDC00).
    GuestPtr<BuildingType> BldgPigFarm_ctor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id)
    {
        constructBuildingType(btype, record, id, kBusiness, 0x001BD300u);
        port::store<uint32_t>(0x001BDC00u, port::load<uint32_t>(0x001F3B5Cu));
        return btype;
    }

    // The amusement park shares the ride model at 0x1F3B4C (kept at 0x1BDBE0).
    GuestPtr<BuildingType> BldgAmusementPark_ctor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id)
    {
        constructBuildingType(btype, record, id, kBusiness, 0x001BD040u);
        port::store<uint32_t>(0x001BDBE0u, port::load<uint32_t>(0x001F3B4Cu));
        return btype;
    }

    // The office keeps its extra mesh.
    GuestPtr<BuildingType> BldgOffice_ctor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id)
    {
        constructBuildingType(btype, record, id, kBusiness, 0x001BD080u);
        btype.as<OfficeType>()->extraMesh = record->extraMesh;
        return btype;
    }

    // The ranch shares the animal models at 0x1F3B50, 0x1F3B54 and 0x1F3B58 (kept at 0x1BDBE8, 0x1BDBEC and 0x1BDBF0).
    GuestPtr<BuildingType> BldgRanch_ctor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id)
    {
        constructBuildingType(btype, record, id, kBusiness, 0x001BD0C0u);
        port::store<uint32_t>(0x001BDBE8u, port::load<uint32_t>(0x001F3B50u));
        port::store<uint32_t>(0x001BDBECu, port::load<uint32_t>(0x001F3B54u));
        port::store<uint32_t>(0x001BDBF0u, port::load<uint32_t>(0x001F3B58u));
        return btype;
    }

    GuestPtr<BuildingType> BldgLeveled_ctor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id)
    {
        constructBuildingType(btype, record, id, kBaseCtor, 0x001BD240u);
        btype.as<LeveledType>()->mesh = record->builtMesh;
        return btype;
    }

    GuestPtr<BuildingType> BldgRoad_ctor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id)
    {
        constructBuildingType(btype, record, id, kBaseCtor, 0x001BD340u);
        const GuestPtr<RoadType> road = btype.as<RoadType>();
        road->lampMesh = record->builtMesh;
        road->lightMesh = record->stageMesh;
        road->slopeMesh = record->extraMesh;
        return btype;
    }

    // The field reads the record's four people spots (each x, y, z and a heading, the heading divided by pi) as its own: the
    // first two as its lifts and its first six growing heights, the last two (made bytes) as its six crop meshes and its
    // first two growing meshes; the group spot bytes are its last four growing meshes.
    GuestPtr<BuildingType> BldgField_ctor(GuestPtr<BuildingType> btype, GuestPtr<BuildingRecord> record, int id)
    {
        constructBuildingType(btype, record, id, kBaseCtor, 0x001BD140u);
        const GuestPtr<FieldType> field = btype.as<FieldType>();
        const ps2f pi(std::bit_cast<float>(0x40490FDBu));
        const auto meshOf = [](ps2f v) { return static_cast<uint8_t>(sdk::fptoui(v)); };
        field->cropLift = record->peopleSpots[0].x;
        field->peopleLift = record->peopleSpots[0].y;
        field->growingHeight[0] = record->peopleSpots[0].z;
        field->growingHeight[1] = record->peopleSpots[0].w / pi;
        field->growingHeight[2] = record->peopleSpots[1].x;
        field->growingHeight[3] = record->peopleSpots[1].y;
        field->growingHeight[4] = record->peopleSpots[1].z;
        field->growingHeight[5] = record->peopleSpots[1].w / pi;
        field->cropMesh[0] = meshOf(record->peopleSpots[2].x);
        field->cropMesh[1] = meshOf(record->peopleSpots[2].y);
        field->cropMesh[2] = meshOf(record->peopleSpots[2].z);
        field->cropMesh[3] = meshOf(record->peopleSpots[2].w / pi);
        field->cropMesh[4] = meshOf(record->peopleSpots[3].x);
        field->cropMesh[5] = meshOf(record->peopleSpots[3].y);
        field->growingMesh[0] = meshOf(record->peopleSpots[3].z);
        field->growingMesh[1] = meshOf(record->peopleSpots[3].w / pi);
        for (int i = 0; i < 4; ++i)
            field->growingMesh[2 + i] = record->groupSpots[i];
        return btype;
    }

    PORT_FN(Bldg_ctor);
    PORT_FN(BldgBusiness_ctor);
    PORT_FN(BldgPort_ctor);
    PORT_FN(BldgKeepOut_ctor);
    PORT_FN(BldgHousing_ctor);
    PORT_FN(BldgMyHome_ctor);
    PORT_FN(BldgSmallGoods_ctor);
    PORT_FN(BldgWasteland_ctor);
    PORT_FN(BldgPigFarm_ctor);
    PORT_FN(BldgAmusementPark_ctor);
    PORT_FN(BldgOffice_ctor);
    PORT_FN(BldgRanch_ctor);
    PORT_FN(BldgLeveled_ctor);
    PORT_FN(BldgRoad_ctor);
    PORT_FN(BldgField_ctor);

    // Adds the family to the applicants (with the month count) and shows it when `select` is not 0; then every resident of a
    // class that counts (flag 1 clear) whose introduction is this family gets a new one.
    void CityScene_AddApplicantFamily(GuestPtr<CityScene> city, uint32_t family, int select)
    {
        Applicants_AddAndShow(city->applicants, static_cast<int>(family), city->monthCount, select);
        const GuestPtr<MapPerson> people = city->map->people;
        for (GuestPtr<MapPerson> person = people; person != people + 6000; ++person)
        {
            const uint32_t type = person->charType;
            if (type == 0)
                continue;
            const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * type)};
            if ((cls->flags & 1u) != 0 || person->introduces != family)
                continue;
            person->introduces = static_cast<uint16_t>(Family_PickIntroduction(person->family, person->member, city));
        }
    }

    PORT_FN(CityScene_AddApplicantFamily);

    // Makes the building type object for the data record `record` with number `id`, of the class in byte 0x20 of the record (0
    // and 5 the base class, 1 wasteland, 2 leveled, 3 road, 4 field, 6 business, 7 office, 8 housing, 9 port, 10 my home, 11
    // amusement park, 12 ranch, 13 pig farm, 14 small goods, 15 keep-out); 0 when the memory is not there, and what the register
    // held for any other class.
    GuestPtr<BuildingType> BldgType_Create(int id, GuestPtr<BuildingRecord> record)
    {
        const uint32_t kind = record->typeClass;
        if (kind >= 0x10u)
            return GuestPtr<BuildingType>{static_cast<uint32_t>(port::guestV0())};
        constexpr uint32_t sizes[16] = {0x160, 0x160, 0x170, 0x170, 0x210, 0x160, 0x160, 0x170, 0x160, 0x160, 0x160, 0x160, 0x160, 0x160, 0x160, 0x160};
        const uint32_t memory = sdk::operator_new(sizes[kind]);
        if (memory == 0u)
            return GuestPtr<BuildingType>{0u};
        const GuestPtr<BuildingType> btype{memory};
        switch (kind)
        {
        case 1:
            return BldgWasteland_ctor(btype, record, id);
        case 2:
            return BldgLeveled_ctor(btype, record, id);
        case 3:
            return BldgRoad_ctor(btype, record, id);
        case 4:
            return BldgField_ctor(btype, record, id);
        case 6:
            return BldgBusiness_ctor(btype, record, id);
        case 7:
            return BldgOffice_ctor(btype, record, id);
        case 8:
            return BldgHousing_ctor(btype, record, id);
        case 9:
            return BldgPort_ctor(btype, record, id);
        case 10:
            return BldgMyHome_ctor(btype, record, id);
        case 11:
            return BldgAmusementPark_ctor(btype, record, id);
        case 12:
            return BldgRanch_ctor(btype, record, id);
        case 13:
            return BldgPigFarm_ctor(btype, record, id);
        case 14:
            return BldgSmallGoods_ctor(btype, record, id);
        case 15:
            return BldgKeepOut_ctor(btype, record, id);
        default:
            return BldgType_BaseCtor(btype, record, static_cast<uint16_t>(id));
        }
    }

    PORT_FN(BldgType_Create);

    // Loads the building data file (0x1B8150) and makes a type object for each of its 156-byte records into a new table (the
    // pointer at 0x1BDBF8, the count at 0x1BDBFC); the file's data are freed again.
    void BldgType_LoadAll()
    {
        port::GuestFrame<0x60> frame;
        const uint32_t file = frame.ptr().address + 0x50u, size = frame.ptr().address + 0x5Cu;
        File_ctor(GuestPtr<GameFile>{file}, 0);
        const uint32_t data = static_cast<uint32_t>(File_LoadWhole(GuestPtr<GameFile>{file}, GuestPtr<char>{0x001B8150u}, GuestPtr<int32_t>{size}));
        const uint32_t count = port::load<uint32_t>(size) / 0x9Cu;
        globals::buildingTypeCount.set(static_cast<int32_t>(count));
        globals::buildingTypeTable.set(sdk::mw_NewArray(static_cast<uint32_t>(globals::buildingTypeCount.get()) << 2));
        for (uint32_t i = 0; i < static_cast<uint32_t>(globals::buildingTypeCount.get()); ++i)
            port::store<uint32_t>(globals::buildingTypeTable.get() + 4u * i,
                                  BldgType_Create(static_cast<int>(i), GuestPtr<BuildingRecord>{data + 0x9Cu * i}).address);
        if (data != 0u)
            sdk::free(data);
        File_dtor(GuestPtr<GameFile>{file}, -1);
    }

    PORT_FN(BldgType_LoadAll);

    // ---- Talks and the approval meter (plan P6 batch 2) ----

    // Starts a talk (mm_Talk_Start on the city's session): when it starts, the city's mode stack moves one place down with mode 5
    // (the talk) on top, and the applicants window is set to 2. 1 when it started, otherwise 0.
    int CityScene_StartTalk(GuestPtr<CityScene> city, int byFamily, int target, int member, int arg5)
    {
        if (Talk_Start(city->talk, byFamily, target, member, arg5, city) == 0)
            return 0;
        for (int i = 3; i >= 0; --i)
            city->modeStack[i + 1] = city->modeStack[i];
        city->modeStack[0] = 5;
        city->modeDepth = city->modeDepth + 1;
        city->applicants->enabled = 2;
        return 1;
    }

    // Every sixth call the approval rating (approval) takes one step towards the pending change (pendingApproval), is kept within -99 and 120,
    // and the status panel is refreshed when it moved.
    void CityScene_UpdateApproval(GuestPtr<CityScene> city)
    {
        constexpr uint32_t kStarted = 0x001BDC58u, kTicks = 0x001BDC54u;
        if (port::load<int8_t>(kStarted) == 0)
        {
            port::store<int32_t>(kTicks, 0);
            port::store<uint8_t>(kStarted, 1);
        }
        const int32_t ticks = port::load<int32_t>(kTicks) + 1;
        port::store<int32_t>(kTicks, ticks);
        if (ticks < 6)
            return;
        port::store<int32_t>(kTicks, 0);
        bool moved = false;
        const int32_t pending = city->pendingApproval;
        if (pending > 0)
        {
            city->approval = city->approval + 1;
            moved = true;
            city->pendingApproval = pending - 1;
        }
        else if (pending < 0)
        {
            city->approval = city->approval - 1;
            moved = true;
            city->pendingApproval = city->pendingApproval + 1;
        }
        int32_t rating = city->approval;
        if (rating < -0x63)
            rating = -0x63;
        else if (rating >= 0x79)
            rating = 0x78;
        city->approval = rating;
        if (moved)
            StatusPanel_Update(city->statusPanel, city);
    }

    // When the view (its widget state) is active (3), the talk goes to state 6 with no conversation when it has no
    // single target; otherwise the conversation window starts, for the family (and member) when the person is 6000 and for
    // the person (at its building) otherwise, and the talk goes to state 2.
    void Talk_Opening(GuestPtr<TalkSession> talk)
    {
        const uint32_t view = talk->view.address;
        if (port::callGuest<int64_t>(widgetMethod(view, &WidgetVtable::getState), view) != 3)
            return;
        if (talk->singleTarget == 0)
        {
            talk->base.state = 6;
            return;
        }
        const GuestPtr<TalkConv> conv{talk->conversation.address};
        if (talk->person == 6000)
            TalkConv_Start(conv, 1, talk->family, talk->member, talk->slot, talk->arg5);
        else
            TalkConv_Start(conv, 0, talk->person, talk->building, talk->slot, talk->arg5);
        talk->base.state = 2;
    }

    PORT_FN(CityScene_StartTalk);
    PORT_FN(CityScene_UpdateApproval);
    PORT_FN(Talk_Opening);

    // With the window in mode 4: an applicant under the cursor that is waiting to be placed (state 3) goes to state 6 (its timer
    // set) and the footprint of its home type is highlighted on the map at its tile. Whatever its state, the pad's focus
    // is then taken for the window (owner 1, the Cross button), the window goes to mode 6 and its step to 0.02, the card text is
    // rebuilt and the notice line cleared.
    void Applicants_StartPlacing(GuestPtr<Applicants> applicants, GuestPtr<CityScene> city)
    {
        if (applicants->mode != 4)
            return;
        GuestPtr<ApplicantEntry> entry{0u}; // with no applicants the game reads the start of memory
        if (applicants->count != 0)
            entry = applicants.as<ApplicantEntry>() + applicants->cursor;
        if (static_cast<uint16_t>(entry->state) == 3)
        {
            entry->timer = 6;
            const uint32_t type = static_cast<uint32_t>(Family_GetTypeIndex(entry->family));
            const uint32_t tile = entry->tile;
            Map_HighlightArea(city->map, static_cast<int>(tile & 0x7Fu), static_cast<int>(tile >> 7), globals::buildingType(type)->size);
        }
        Pad_TakeFocus(1, 0x10);
        applicants->mode = 6;
        applicants->step = std::bit_cast<float>(0x3CA3D70Au); // 0.02
        Applicants_SetMode(applicants, 1);
        Notice_Clear(city->notice);
    }

    // An event actor points at a building: unless someone holds the pad focus, a talk about the building (its id at +2) starts
    // and its person is selected from the actor's family and member (+4, +6). 1 when the talk started.
    int EvActor_PointAtBuilding(GuestPtr<CityScene> city, int actor, int building)
    {
        if (globals::padFocusOwner.get() != 0)
            return 0;
        if (CityScene_StartTalk(city, 0, port::load<uint16_t>(static_cast<uint32_t>(building) + 2u), 0x1770, 0) == 0)
            return 0;
        CityScene_TalkSelectPerson(city, port::load<uint16_t>(static_cast<uint32_t>(actor) + 4u), port::load<uint16_t>(static_cast<uint32_t>(actor) + 6u));
        return 1;
    }

    PORT_FN(Applicants_StartPlacing);
    PORT_FN(EvActor_PointAtBuilding);

    // A person inside a building (not 0x3F80) is checked against the schedule: nothing happens for a person with flag 0x20 or 4, or
    // with flag 0x200 while the city's clock is below 0xC4E0, or whose activity is the building's one. Otherwise a
    // person with flag 0x200 walks on from the building's tile, and any other leaves the building (outside, walking to nowhere,
    // flag 0x100 and the cool-down cleared).
    void Person_CheckBuilding(GuestPtr<CharClass>, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        if (person->inside == 0x3F80)
            return;
        if ((person->flags & 0x20u) != 0)
            return;
        if ((person->flags & 0x200u) != 0 && city->clock < 0xC4E0)
            return;
        if ((person->flags & 4u) != 0)
            return;
        if (Schedule_GetBuildingActivity(city, person) == person->activity)
            return;
        if ((person->flags & 0x200u) != 0)
            person->walkTile = Map_GetRecord(city->map, person->inside)->tile;
        else
        {
            person->inside = 0x3F80;
            person->walkTile = 0x4000;
            person->flags = static_cast<uint16_t>(person->flags & 0xFEFFu);
            person->cooldown = 0;
        }
    }

    PORT_FN(Person_CheckBuilding);

    // The city scene: the stage object base, the starting state (6:00, the third month, approval 50, the four hudVisible flags and
    // the city flag bit 1 set, the fade at 128), the building types loaded, and each part made in turn and kept in its field: the talk,
    // status panel, camera (placed with the eye and target vectors at 0x1B2480 and 0x1B2490), pause menu, minimap, city cursor, phone
    // calls, event manager, wants window, town's pages, laptop, city camera, lights, player, applicants, markers, notice and the map,
    // which the terrain is then generated on (the stage's parameters). The stage's 32 introduced families are put in the applicants,
    // and the people of each (charType not zero and its type not marked) get their introduction picked. The pad's focus is
    // released.
    GuestPtr<CityScene> CityScene_ctor(GuestPtr<CityScene> city)
    {
        port::GuestFrame<0x90> frame;
        const uint32_t sp = frame.ptr().address;
        StageObj_ctor(static_cast<int>(city.address));
        city->clock = 0x5460; // 6:00
        city->monthCount = 3;
        city->approval = 0x32;
        city->weatherToday = 0;
        city->weatherTomorrow = 0;
        city->weatherYesterday = 0;
        city->flags = 0u;
        city->flags = city->flags | 1u;
        city->pendingApproval = 0;
        city->trafficPhase = 0;
        city->trafficTimer = 0;
        city->unk_9C = 0;
        city->waterLevel = 0.5f;
        city->prosperity = 0xFA;
        city->prosperityCount = 0;
        city->conduct = 0xFA;
        city->conductCount = 0;
        city->paydayScore = 0;
        city->hudVisible[0] = 1;
        city->hudVisible[1] = 1;
        city->hudVisible[2] = 1;
        city->hudVisible[3] = 1;
        city->talkOverlay = 0;
        city->fadeLevel = 0x80;
        city->fade = 0;
        city->fadeOverAll = 0;
        BldgType_LoadAll();
        // A part of `size` bytes made by `constructor` (0 when the allocation fails).
        const auto build = [](uint32_t size, auto constructor) -> uint32_t {
            uint32_t object = sdk::operator_new(size);
            if (object != 0u)
                object = constructor(object);
            return object;
        };
        city->talk = GuestPtr<TalkSession>{build(0xC0u, [](uint32_t o) { return Talk_ctor(GuestPtr<TalkSession>{o}).address; })};
        city->statusPanel = GuestPtr<StatusPanel>{build(0xC0u, [](uint32_t o) { return StatusPanel_ctor(GuestPtr<StatusPanel>{o}).address; })};
        city->camera = GuestPtr<Camera>{build(0x310u, [](uint32_t o) { return Camera_ctor(GuestPtr<Camera>{o}).address; })};
        *GuestPtr<Vec4>{sp + 0x70u} = *GuestPtr<Vec4>{0x001B2480u};
        *GuestPtr<Vec4>{sp + 0x80u} = *GuestPtr<Vec4>{0x001B2490u};
        Camera_SetEyeTarget(city->camera, GuestPtr<Vec4>{sp + 0x70u}, GuestPtr<Vec4>{sp + 0x80u});
        Camera_UpdateMatrices(city->camera);
        city->pauseMenu = GuestPtr<PauseMenu>{build(0x99Cu, [](uint32_t o) { return PauseMenu_ctor(GuestPtr<PauseMenu>{o}).address; })};
        city->minimap = GuestPtr<Minimap>{build(0x21DCu, [](uint32_t o) { return Minimap_ctor(GuestPtr<Minimap>{o}).address; })};
        city->cursor = GuestPtr<CityCursor>{build(0x1E0u, [](uint32_t o) { return CityCursor_ctor(GuestPtr<CityCursor>{o}).address; })};
        city->phoneCalls = GuestPtr<PhoneCalls>{build(0x7Cu, [](uint32_t o) { return PhoneCall_ctor(GuestPtr<PhoneCalls>{o}).address; })};
        city->events = GuestPtr<EventMgr>{build(0xB0u, [](uint32_t o) { return EventMgr_ctor(GuestPtr<EventMgr>{o}).address; })};
        city->wants = GuestPtr<WantsWin>{build(0x544u, [](uint32_t o) { return WantsWin_ctor(GuestPtr<WantsWin>{o}).address; })};
        city->bag = GuestPtr<Bag>{port::load<uint32_t>(0x001BDA18u)};
        city->stamina = GuestPtr<Stamina>{port::load<uint32_t>(0x001BDBB8u)};
        city->townPages = GuestPtr<TownPages>{build(0x64u, [](uint32_t o) { return TownPages_Init(GuestPtr<TownPages>{o}).address; })};
        city->laptop = GuestPtr<Laptop>{build(0x3E0u, [](uint32_t o) { return Laptop_ctor(GuestPtr<Laptop>{o}).address; })};
        city->cityCamera = GuestPtr<CityCamera>{build(0x540u, [](uint32_t o) { return CityCamera_ctor(GuestPtr<CityCamera>{o}).address; })};
        city->lights = GuestPtr<LightSet>{build(0x1B0u, [](uint32_t o) { return LightSet_ctor(GuestPtr<LightSet>{o}).address; })};
        city->player = GuestPtr<Player>{build(0x90u, [](uint32_t o) { return Player_ctor(GuestPtr<Player>{o}).address; })};
        city->applicants = GuestPtr<Applicants>{build(0x350u, [](uint32_t o) { return Applicants_ctor(GuestPtr<Applicants>{o}).address; })};
        city->markers = GuestPtr<Markers>{build(0x130u, [](uint32_t o) { return Markers_ctor(GuestPtr<Markers>{o}).address; })};
        city->notice = GuestPtr<Notice>{build(0x14u, [](uint32_t o) { return Notice_ctor(GuestPtr<Notice>{o}).address; })};
        city->map = GuestPtr<CityMap>{build(0x2F90u, [](uint32_t o) { return CityMap_ctor(GuestPtr<CityMap>{o}).address; })};
        city->overlay = GuestPtr<void>{0u};
        TerrainGen_Generate(city->map, GuestPtr<uint8_t>::of(globals::stageState()->terrain[0]));
        city->modeStack[0] = 0;
        city->modeDepth = 1;
        for (uint32_t i = 0; i < 0x20u; ++i)
        {
            const uint32_t family = globals::stageState()->introduced[i];
            if (family == 0u)
                continue;
            Applicants_AddAndShow(city->applicants, static_cast<int>(family), city->monthCount, 0);
            // Residents (classes without flag 1) who could introduce this family introduce another one.
            const GuestPtr<MapPerson> people = city->map->people;
            for (GuestPtr<MapPerson> person = people; person != people + 6000; ++person)
            {
                const uint32_t type = person->charType;
                if (type == 0u)
                    continue;
                const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * type)};
                if ((cls->flags & 1u) != 0u)
                    continue;
                if (person->introduces != family)
                    continue;
                person->introduces = static_cast<uint16_t>(Family_PickIntroduction(person->family, person->member, city));
            }
        }
        Pad_ReleaseFocus(-1);
        return city;
    }

    PORT_FN(CityScene_ctor);

    // One frame of the city scene: the building types are called with the clock, then the mode's update runs (the mode is modeStack[0]).
    // 0 loads a game (its result is the answer). 1 is the walk mode: Start (Circle not held) pushes the laptop mode (2), Select the
    // pause menu (6); otherwise the clock, phone, status panel, player, camera, map, applicants, wants window, events, cursor and
    // markers are stepped, and with Circle held the pad picks a player mode (up, left), the applicants (down: mode 3) or the town's
    // pages (right: mode 7, when laptop entry 4 is active); the debug flag word at 0x1C5600 (bit 1) adds button cheats. 2 steps the
    // laptop and status panel; 3 the applicants (1 pops the mode, any answer but 3 pushes it); 4 the player, camera and applicants
    // (3 pops); 5 the camera's time of day, the talk and the panel; 6 the pause menu (the answer 2 when it is done); 7 the town's
    // pages; 8 the wants window; 9 the bag; 10 the cursor in its list mode; 11 the player, camera and bag; 13 the first morning (mode 1
    // from 0x57A8 seconds). Then the common part: approval, an event's overlay, notices, and the whole draw: sky, terrain,
    // buildings, people, player, water and sky passes, minimap (debug flag bit 0), phone, markers, the mode's window, applicants,
    // cursor, status panel and fades. The answer is 1 when the city flag 0x10 is set (the city sound state reset and the member meters
    // cleared), else 0.
    uint32_t CityScene_Update(GuestPtr<CityScene> city)
    {
        port::GuestFrame<0x50> frame;
        // Widget methods (talk session) and the overlay's methods by vtable slot (neither vtable has a layout yet).
        const auto virtualCall = [](uint32_t object, uint32_t vtableAt, uint32_t slot, auto... args) {
            return port::callGuest<int64_t>(port::load<uint32_t>(port::load<uint32_t>(object + vtableAt) + slot), object, args...);
        };
        const auto updateMap = [&]() { port::callGuest<int64_t>(city->map->vtable->tick, city->map, city); };
        BldgType_CallAll(city->clock);
        const int32_t mode = city->modeStack[0];
        switch (mode)
        {
        case 0:
            DmaList_QueueClearScreen();
            return static_cast<uint32_t>(CityScene_LoadGame(city));
        case 1:
        {
            if ((Pad_Pressed(0) & 0x80u) != 0u && (Pad_Held(0) & 0x20u) == 0u)
                CityScene_PushMode(city, 2);
            else if ((Pad_Pressed(0) & 0x100u) != 0u && (Pad_Held(0) & 0x20u) == 0u)
                CityScene_PushMode(city, 6);
            else
            {
                CityScene_TickClock(city);
                const int pad = PhoneCall_Update(city->phoneCalls, city);
                StatusPanel_Update(city->statusPanel, city);
                Player_Update(city->player, city);
                CityCamera_Update(city->cityCamera, city);
                updateMap();
                Applicants_Update(city->applicants, city);
                WantsWin_Update(city->wants, city);
                Event_Update(city->events, city);
                CityCursor_Update(city->cursor, city, pad);
                Markers_CheckCircle(city->markers, city, pad);
                if (city->modeStack[0] == 1 && pad != 0 && (Pad_Held(0) & 0x20u) != 0u)
                {
                    if ((Pad_Pressed(0) & 0x1000u) != 0u)
                        Player_SetMode(city->player, 0);
                    else if ((Pad_Pressed(0) & 0x4000u) != 0u)
                        CityScene_PushMode(city, 3);
                    else if ((Pad_Pressed(0) & 0x8000u) != 0u)
                        Player_SetMode(city->player, 1);
                    else if ((Pad_Pressed(0) & 0x2000u) != 0u && (Laptop_CountActive() & 0x10u) != 0u)
                        CityScene_PushMode(city, 7);
                }
            }
            if ((globals::stageState()->flags & 2u) != 0u)
            {
                if ((Pad_Held(0) & 0x900u) == 0x900u)
                {
                    if ((Pad_Held(0) & 1u) != 0u)
                        globals::stageState()->stage = globals::stageState()->stage - 1;
                    city->flags = city->flags | 0x10u;
                }
                if ((Pad_Held(0) & 0x800u) != 0u)
                {
                    if ((Pad_Pressed(0) & 0x1000u) != 0u)
                        city->pendingApproval = city->pendingApproval + 10;
                    if ((Pad_Pressed(0) & 0x4000u) != 0u)
                        city->pendingApproval = city->pendingApproval - 10;
                }
            }
            break;
        }
        case 2:
            Laptop_Update(city->laptop, city);
            StatusPanel_Update(city->statusPanel, city);
            break;
        case 3:
        {
            const int answer = Applicants_Update(city->applicants, city);
            if (answer == 1)
                CityScene_PopMode(city);
            else if (answer != 3)
                CityScene_PushMode(city, answer);
            break;
        }
        case 4:
            Player_Update(city->player, city);
            CityCamera_Update(city->cityCamera, city);
            if (Applicants_Update(city->applicants, city) == 3)
                CityScene_PopMode(city);
            break;
        case 5:
            CityCamera_UpdateTimeOfDay(city->cityCamera, city);
            virtualCall(city->talk.address, 8u, 0x14u); // Update
            StatusPanel_Update(city->statusPanel, city);
            break;
        case 6:
            if (PauseMenu_Update(city->pauseMenu, city) != 0)
            {
                Sound_SetCity(GuestPtr<CityScene>{0u});
                return 2;
            }
            break;
        case 7:
            TownPages_Update(city->townPages, city);
            break;
        case 8:
            WantsWin_Update(city->wants, city);
            break;
        case 9:
            BagMenu_Update(city->bag, city);
            break;
        case 10:
            CityCursor_Update(city->cursor, city, 1);
            break;
        case 11:
            Player_Update(city->player, city);
            CityCamera_Update(city->cityCamera, city);
            BagMenu_Update(city->bag, city);
            break;
        case 13:
            CityScene_TickClock(city);
            if (!(city->clock < 0x57A8))
            {
                city->modeStack[0] = 1;
                Pad_ReleaseFocus(2);
            }
            StatusPanel_Update(city->statusPanel, city);
            Player_Update(city->player, city);
            CityCamera_Update(city->cityCamera, city);
            updateMap();
            Applicants_Update(city->applicants, city);
            WantsWin_Update(city->wants, city);
            PhoneCall_Update(city->phoneCalls, city);
            break;
        default:
            break;
        }
        CityScene_UpdateApproval(city);
        if (city->overlay)
            virtualCall(city->overlay.address, 0u, 0xCu, city.address);
        Notice_Tick(city->notice);
        DmaList_QueueClearZ();
        Sky_Draw(city->cityCamera);
        Packet_Draw(GuestPtr<uint32_t>::of(city->cityCamera->base.packet));
        const GuestPtr<CityCamera> camera = city->cityCamera;
        const int zoom = CityCamera_GetZoomLevel(camera, city);
        LightSet_UpdateByClock(city->lights, city->clock, city->weatherToday);
        Terrain_Draw(city->map, GuestPtr<Vec4>::of(camera->base.view[0]), GuestPtr<Vec4>::of(camera->base.eye), zoom, city->weatherToday, city->clock,
                     city->waterLevel);
        CityRender_DrawTerrain(city->map, city);
        DmaList_QueueGsDefaults();
        if (city->hudVisible[0] != 0)
        {
            Packet2_Draw(GuestPtr<uint32_t>::of(city->lights->packetA), 1);
            CityRender_CollectObjects(city->map, city);
        }
        if (city->modeStack[0] == 5)
            virtualCall(city->talk.address, 8u, 0x20u, 0); // Draw
        Packet2_Draw(GuestPtr<uint32_t>::of(city->lights->packetA), 0);
        Perf_AdaptDrawRange();
        if (city->hudVisible[1] != 0)
        {
            CityRender_UpdatePeople(city->map, city);
            Player_Draw(city->player, city);
        }
        if (city->modeStack[0] == 5)
            virtualCall(city->talk.address, 8u, 0x20u, 1);
        CityRender_DrawWater(city->map, city);
        const int32_t weather = city->weatherToday;
        if (weather == 2)
            CityRender_DrawWeather(city->map, city, 0, GuestPtr<Vec4>::of(city->cityCamera->base.view[0]));
        else if (weather == 3)
            CityRender_DrawWeather(city->map, city, 1, GuestPtr<Vec4>::of(city->cityCamera->base.view[0]));
        CityRender_DrawPlacement(city->map, 0);
        CityRender_DrawPlacement(city->map, 1);
        CityRender_DrawInstanceLayer(city->map);
        DmaList_QueueGsDefaults();
        Packet2_Draw(GuestPtr<uint32_t>::of(city->lights->packetA), 0);
        Packet_Draw(GuestPtr<uint32_t>::of(city->camera->packet));
        DmaList_QueueClearZ();
        if (city->hudVisible[3] != 0 && (globals::stageState()->flags & 1u) != 0u)
            Minimap_Draw(city->minimap);
        PhoneCall_Draw(city->phoneCalls);
        Markers_Draw(city->markers);
        switch (city->modeStack[0])
        {
        case 2:
            Laptop_Draw(city->laptop);
            break;
        case 5:
            virtualCall(city->talk.address, 8u, 0x20u, 2);
            break;
        case 7:
            TownPages_Draw(city->townPages);
            break;
        case 8:
            WantsWin_Draw(city->wants);
            break;
        case 9:
        case 11:
            BagMenu_Draw(city->bag);
            break;
        case 6:
            PauseMenu_Draw(city->pauseMenu);
            break;
        default:
            break;
        }
        Applicants_Draw(city->applicants);
        if (city->hudVisible[2] != 0)
            CityCursor_Draw(city->cursor);
        StatusPanel_Draw(city->statusPanel);
        if (city->overlay)
            virtualCall(city->overlay.address, 0u, 0x10u, 2);
        Notice_Draw(city->notice);
        if (city->talkOverlay != 0)
        {
            DmaList_QueueGsDefaults();
            DmaList_QueueOtherOverlay();
        }
        if (city->fadeLevel != 0)
            DmaList_QueueFade(city->fadeLevel, city->fadeOverAll);
        const int32_t fade = city->fade;
        if (fade != 0)
        {
            if (fade > 0)
            {
                city->fade = fade - city->fadeSpeed;
                if (city->fade < 0)
                    city->fade = 0;
                city->fadeLevel = city->fade;
            }
            else
            {
                city->fade = fade + city->fadeSpeed;
                if (city->fade > 0)
                    city->fade = 0;
                city->fadeLevel = city->fade + 0x80;
            }
        }
        if ((city->flags & 0x10u) == 0u)
            return 0;
        Sound_SetCity(GuestPtr<CityScene>{0u});
        MemberMeters_Reset(GuestPtr<int32_t>{0x001C5780u});
        return 1;
    }

    PORT_FN(CityScene_Update);

    // Loading a saved game, one step a call (the step in the word at 0x1BDC68; its state words: the card index 0x1BDA78, a "start a new
    // load" flag 0x1BDA7C, the wait texts 0x1BDC7C and 0x1BDC78, the sprite 0x1BDC80, the fade 0x1BDC84, a timer 0x1BDC70 and the
    // file handle 0x1BDC74). 0 without the flag takes the pad, fades and turns the city sound to its state (mode 13); with it, clears
    // the flag and makes the wait text, the sprite and a memory card manager (loadCard) and goes on to 1. 1 steps the manager and fades in,
    // then by the card's state: 4 waits, 3 and a missing save go to 0x15, 2 shows the "no memory card" lines (0x14, a timer of 200
    // frames); a save found whose time matches the chosen one resets the manager (step 2). 2 prepares the save folder (1 goes on, 2 to
    // 0xA), 3 opens the file, 4 waits for it (an open that failed goes to 0xA), 5 reads every part of the city (the stage state, the
    // 0x34 bytes from clock on, the camera, player, applicants, wants, families, map, bag, stamina, laptop and events) and closes the file,
    // 6 unpacks the map's tiles when the file is closed, 7 and 8 check the folder and the result (a bad result goes to 0xA), 9 fades out
    // (16 a frame) and then finishes the load: the city mode 1 and a fade in, the manager, sprite and text freed. 0xA gives up
    // (0x15), 0x14 counts out the message, 0x15 shows the failure text and 0x16 answers (Cross retries from step 1, Triangle goes
    // on to 0x17), 0x17 fades out and frees everything, 0x18 and 0x19 wait for the sound to be quiet (the answer 2). The result is 0
    // while loading goes on, 2 when it ended without a load.
    int CityScene_LoadGame(GuestPtr<CityScene> city)
    {
        constexpr uint32_t kState = 0x001BDC68u, kStarted = 0x001BDC6Cu, kCard = 0x001BDA78u, kFlag = 0x001BDA7Cu, kTextA = 0x001BDC7Cu,
                           kTextB = 0x001BDC78u, kSprite = 0x001BDC80u, kFade = 0x001BDC84u, kTimer = 0x001BDC70u, kHandle = 0x001BDC74u;
        if (port::load<int8_t>(kStarted) == 0)
        {
            port::store<int32_t>(kState, 0);
            port::store<uint8_t>(kStarted, 1);
        }
        const auto mc = [&]() { return city->loadCard; };
        const auto nextState = [&]() { port::store<int32_t>(kState, port::load<int32_t>(kState) + 1); };
        const auto setState = [&](int32_t value) { port::store<int32_t>(kState, value); };
        // A text box of `columns` by `rows` (style `style`), or 0 when the allocation fails.
        const auto newBox = [](int columns, int rows, int style) -> uint32_t {
            const uint32_t memory = sdk::operator_new(0x40u);
            return memory != 0u ? TextBox_Init(GuestPtr<TextBox>{memory}, columns, rows, style).address : 0u;
        };
        // The wait texts, the sprite and the manager, freed (the manager first only when asked).
        const auto freeBox = [&](uint32_t at) {
            const uint32_t box = port::load<uint32_t>(at);
            if (box != 0u)
            {
                TextBox_dtor(GuestPtr<TextBox>{box}, 1);
                port::store<uint32_t>(at, 0u);
            }
        };
        const auto freeManager = [&]() {
            if (mc().address != 0u)
            {
                const GuestPtr<McManager> manager = mc();
                city->loadCard = GuestPtr<McManager>{0u};
                McManager_dtor(manager, 1);
            }
        };
        const auto freeSprite = [&]() {
            const uint32_t sprite = port::load<uint32_t>(kSprite);
            if (sprite != 0u)
                port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(sprite) + 8u), sprite, 1);
            port::store<uint32_t>(kSprite, 0u);
        };
        bool draw = false;
        switch (port::load<int32_t>(kState))
        {
        case 0:
            if (port::load<int32_t>(kFlag) == 0)
            {
                city->modeStack[0] = 0xD;
                Pad_TakeFocus(2, -1);
                CityScene_StartFade(city, 2);
                Sound_SetCity(city);
            }
            else
            {
                port::store<int32_t>(kFlag, 0);
                port::store<uint32_t>(kTextA, newBox(0x14, 3, -2));
                TextBox_SetText(GuestPtr<TextBox>{port::load<uint32_t>(kTextA)}, GuestPtr<char>{globals::kLoadedSaveInfo}, 0);
                TextBox_SetPos(GuestPtr<TextBox>{port::load<uint32_t>(kTextA)}, 0x71A0, 0x7C20, 0xFF0000);
                TextBox_SetCharSize(GuestPtr<TextBox>{port::load<uint32_t>(kTextA)}, 0, 0xB0, 0, 0xE0);
                port::store<int32_t>(kTextB, 0);
                WaitText_Create(GuestPtr<char>{0x001B98E0u});
                uint32_t sprite = sdk::operator_new(0x38u);
                if (sprite != 0u)
                    sprite = static_cast<uint32_t>(TexSprite_Load(GuestPtr<TexSprite>{sprite}, 0x001B9940, 1));
                port::store<uint32_t>(kSprite, sprite);
                TexSprite_SetPosScale(GuestPtr<TexSprite>{port::load<uint32_t>(kSprite)}, 0, 0, 1, ps2f(2.0f), ps2f(2.0f));
                uint32_t manager = sdk::operator_new(0x28u);
                if (manager != 0u)
                    manager = McManager_ctor(GuestPtr<McManager>{manager}).address;
                city->loadCard = GuestPtr<McManager>{manager};
                McManager_ClearSlots(mc());
                port::store<int32_t>(kFade, 0x80);
                nextState();
                draw = true;
            }
            break;
        case 1:
        {
            McManagerUpdate(mc());
            if (port::load<int32_t>(kFade) > 0)
            {
                port::store<int32_t>(kFade, port::load<int32_t>(kFade) - 0x10);
                if (port::load<int32_t>(kFade) < 0)
                    port::store<int32_t>(kFade, 0);
            }
            const int status = McManager_GetCardStatus(mc(), port::load<int32_t>(kCard));
            draw = true;
            if (status == 4)
                break;
            if (status == 3)
            {
                setState(0x15);
                break;
            }
            if (status == 2)
            {
                WaitText_Set(GuestPtr<char>{0x001B9960u});
                WaitText_Append(GuestPtr<char>{port::load<int32_t>(kCard) != 0 ? 0x001B99B8u : 0x001B99C0u});
                port::store<int32_t>(kTimer, 0);
                setState(0x14);
                break;
            }
            const GuestPtr<CardDirEntry> loaded = globals::loadedSaveEntry();
            const GuestPtr<CardDirEntry> entry = McManager_FindSave(mc(), GuestPtr<char>::of(loaded->name[0]), port::load<int32_t>(kCard));
            if (entry.address == 0u)
            {
                setState(0x15);
                break;
            }
            const CardTime &now = entry->modified, &was = loaded->modified;
            if (now.year != was.year || now.month != was.month || now.day != was.day || now.hour != was.hour || now.minute != was.minute)
            {
                setState(0x15);
                break;
            }
            WaitText_Set(GuestPtr<char>{0x001B98E0u});
            McManager_ResetState(mc());
            nextState();
            break;
        }
        case 2:
        {
            const int result = SaveFile_PrepareDir(mc(), port::load<int32_t>(kCard), static_cast<int>(GuestPtr<char>::of(globals::loadedSaveEntry()->name[0]).address), 0);
            if (result == 1)
                nextState();
            else if (result == 2)
                setState(0xA);
            draw = true;
            break;
        }
        case 3:
            port::store<int32_t>(kHandle, SaveFile_Open(mc(), 1));
            nextState();
            draw = true;
            break;
        case 4:
            if (SaveFile_IsDone(mc(), port::load<int32_t>(kHandle)) != 0)
            {
                if (mc()->fd < 0)
                    setState(0xA);
                else
                    nextState();
            }
            draw = true;
            break;
        case 5:
        {
            const GuestPtr<McManager> manager = mc();
            MapSave_AllocBuffer(city->map);
            Stage_LoadState(static_cast<int>(city.address), manager);
            SaveFile_Read(manager, static_cast<int>(port::guest(&city->clock)), 0x34); // the clock through the flags
            CityCamera_LoadState(city->cityCamera, manager);
            Player_LoadState(city->player, manager);
            Applicants_LoadState(city->applicants, manager);
            Wants_LoadState(city->wants, manager);
            Family_LoadState(manager);
            Map_LoadState(city->map, manager);
            Bag_LoadState(city->bag, manager);
            Stamina_LoadState(city->stamina, manager);
            Laptop_LoadState(city->laptop, manager);
            Event_LoadState(city->events, manager);
            port::store<int32_t>(kHandle, SaveFile_Close(manager));
            nextState();
            draw = true;
            break;
        }
        case 6:
            if (SaveFile_IsDone(mc(), port::load<int32_t>(kHandle)) != 0)
            {
                MapSave_UnpackTiles(city->map);
                nextState();
            }
            draw = true;
            break;
        case 7:
        {
            const int result = SaveFile_PrepareDir(mc(), port::load<int32_t>(kCard), 0x001B99C8, 0);
            if (result == 1)
                nextState();
            else if (result == 2)
                setState(0x15);
            draw = true;
            break;
        }
        case 8:
            if (SaveFile_IsDone(mc(), port::load<int32_t>(kHandle)) != 0)
            {
                if (SaveFile_GetResult(mc(), port::load<int32_t>(kHandle)) != 0)
                    setState(0xA);
                else
                {
                    port::store<int32_t>(kFade, 0);
                    nextState();
                }
            }
            draw = true;
            break;
        case 9:
            port::store<int32_t>(kFade, port::load<int32_t>(kFade) + 0x10);
            if (port::load<int32_t>(kFade) <= 0x80)
            {
                draw = true;
                break;
            }
            setState(0);
            Sound_SetCity(city);
            Event_InitForCity(city->events);
            city->modeStack[0] = 1;
            CityScene_StartFade(city, 2);
            freeManager();
            freeSprite();
            freeBox(kTextA);
            WaitText_Delete();
            break;
        case 0xA:
        {
            const int result = SaveFile_PrepareDir(mc(), port::load<int32_t>(kCard), 0x001B99C8, 0);
            if (result != 0)
                setState(0x15);
            draw = true;
            break;
        }
        case 0x14:
            port::store<int32_t>(kTimer, port::load<int32_t>(kTimer) + 1);
            if (!(port::load<int32_t>(kTimer) < 0xC9))
                setState(0x15);
            draw = true;
            break;
        case 0x15:
            McManager_ClearSlots(mc());
            port::store<uint32_t>(kTextB, newBox(0x12, 2, -1));
            TextBox_SetText(GuestPtr<TextBox>{port::load<uint32_t>(kTextB)}, GuestPtr<char>{0x001B99D0u}, 0);
            TextBox_SetPos(GuestPtr<TextBox>{port::load<uint32_t>(kTextB)}, 0x7420, 0x81C0, 0xFF0000);
            WaitText_Set(GuestPtr<char>{0x001B9A10u});
            nextState();
            draw = true;
            break;
        case 0x16:
        {
            const uint32_t pad = Pad_Pressed(0);
            if (pad == 0x40u)
            {
                freeBox(kTextB);
                Sound_PlaySe(1, 0, 0x3C, 0.0f, 0.0f);
                WaitText_Set(GuestPtr<char>{0x001B9A30u});
                setState(1);
                draw = true;
            }
            else if (pad == 0x10u)
            {
                Sound_PlaySe(2, 0, 0x3C, 0.0f, 0.0f);
                nextState();
            }
            else
                draw = true;
            break;
        }
        case 0x17:
            port::store<int32_t>(kFade, port::load<int32_t>(kFade) + 0x10);
            if (port::load<int32_t>(kFade) <= 0x80)
            {
                draw = true;
                break;
            }
            freeBox(kTextB);
            freeManager();
            freeSprite();
            freeBox(kTextA);
            WaitText_Delete();
            nextState();
            break;
        case 0x18:
            if (Sound_IsBusy() == 0)
                nextState();
            break;
        case 0x19:
            if (Sound_IsDriverBusy() == 0)
            {
                setState(0);
                return 2;
            }
            break;
        default:
            break;
        }
        if (draw)
        {
            DmaList_QueueClearZ();
            TexSprite_Draw(GuestPtr<TexSprite>{port::load<uint32_t>(kSprite)});
            TextBox_Draw(GuestPtr<TextBox>{port::load<uint32_t>(kTextA)});
            if (port::load<uint32_t>(kTextB) != 0u)
                TextBox_Draw(GuestPtr<TextBox>{port::load<uint32_t>(kTextB)});
            WaitText_Draw();
            if (port::load<int32_t>(kFade) > 0)
            {
                DmaList_QueueClearZ();
                DmaList_QueueFade(port::load<int32_t>(kFade), 0);
            }
        }
        return 0;
    }

    PORT_FN(CityScene_LoadGame);
}
