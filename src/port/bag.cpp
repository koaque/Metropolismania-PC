// The bag: up to 20 slots (4, 10 or 20 by its size) of goods, each naming a goods record by group and index
// (records of 0x88 bytes from 0x1BDAA0; each group's first record in the table at 0x1BDA9C), and ten shortcuts
// holding music tracks. Also the applicants list's small helpers.

#include <bit>

#include "game_functions.h"
#include "game_globals.h"
#include "lifetime.h"
#include "sdk.h"

namespace mm
{
    namespace
    {
        // The goods record of a group and index (0x88 bytes each).
        uint32_t goodsRecordOf(uint32_t group, uint32_t index)
        {
            const uint32_t first = port::load<uint16_t>(port::load<uint32_t>(0x001BDA9Cu) + 4u * group);
            return port::load<uint32_t>(0x001BDAA0u) + 0x88u * (index + first);
        }

        uint32_t slotAddress(GuestPtr<Bag> bag, uint32_t slot) { return bag.address + 4u + 16u * slot; }
    }

    // ---- applicants

    // Entries waiting or in an earlier state (state below 7).
    int Applicants_CountActive(GuestPtr<Applicants> applicants)
    {
        int active = 0;
        for (int i = 0; i < applicants->count; ++i)
            if (static_cast<uint16_t>(applicants->entries[i].state) < 7)
                ++active;
        return active;
    }

    // The entry under the cursor, 0 with no applicants.
    int Applicants_GetSelected(GuestPtr<Applicants> applicants)
    {
        if (applicants->count == 0)
            return 0;
        return static_cast<int>(applicants.address + 0x14u * static_cast<uint32_t>(applicants->cursor));
    }

    void Applicants_RemoveFamily(GuestPtr<Applicants> applicants, uint32_t family)
    {
        for (int i = 0; i < applicants->count; ++i)
        {
            if (family == applicants->entries[i].family)
            {
                Applicants_RemoveAt(applicants, i);
                return;
            }
        }
    }

    void Applicants_SaveState(GuestPtr<Applicants> applicants, GuestPtr<McManager> mc) { SaveFile_Write(mc, static_cast<int>(applicants.address), 0x284); }

    // ---- the bag

    void Bag_SaveState(GuestPtr<Bag> bag, GuestPtr<McManager> mc) { SaveFile_Write(mc, static_cast<int>(bag.address), 0x16C); }

    // Puts the goods in the first empty slot (the bag gets size 1 if it had none); byte 6 comes from the goods
    // record.
    void Bag_AddItem(GuestPtr<Bag> bag, int group, int index, int16_t a3, int t0, int16_t t1, uint8_t t2, uint8_t t3)
    {
        if (bag->size == 0)
            bag->size = 1;
        uint32_t slot = slotAddress(bag, 0);
        while (port::load<uint16_t>(slot) != 0)
            slot += 16u;
        port::store<uint16_t>(slot + 0u, static_cast<uint16_t>(group));
        port::store<uint16_t>(slot + 2u, static_cast<uint16_t>(index));
        port::store<uint16_t>(slot + 4u, static_cast<uint16_t>(a3));
        port::store<uint32_t>(slot + 8u, static_cast<uint32_t>(t0));
        port::store<uint16_t>(slot + 0xCu, static_cast<uint16_t>(t1));
        port::store<uint8_t>(slot + 0xEu, t2);
        port::store<uint8_t>(slot + 0xFu, t3);
        port::store<int8_t>(slot + 6u, port::load<int8_t>(goodsRecordOf(static_cast<uint32_t>(group), static_cast<uint32_t>(index)) + 6u));
    }

    // A music goods record puts its track (record +5) in the first free shortcut (0xFFFF), unless a shortcut has it
    // already, and enables it in the BGM slot table.
    void Bag_AddMusicTrack(GuestPtr<Bag> bag, int group, int index)
    {
        const uint8_t track = port::load<uint8_t>(goodsRecordOf(static_cast<uint32_t>(group), static_cast<uint32_t>(index)) + 5u);
        for (const BagShortcut &shortcut : bag->shortcuts)
            if (static_cast<uint16_t>(shortcut.slot) == track)
                return;
        for (BagShortcut &shortcut : bag->shortcuts)
        {
            if (static_cast<uint16_t>(shortcut.slot) != 0xFFFFu)
                continue;
            shortcut.slot = track;
            shortcut.unk_02 = 1;
            Bgm_SetTrackSlot(static_cast<char>(track), 1, 1);
            return;
        }
    }

    void Bag_Clear(GuestPtr<Bag> bag)
    {
        bag->size = 0;
        bag->unk_002 = 0;
        for (BagSlot &slot : bag->slots)
            slot.item = 0;
        for (BagShortcut &shortcut : bag->shortcuts)
            shortcut.slot = -1;
    }

    // The slot whose goods record's first halfword is `id`. Slots are read up to the bag's size, and past it while
    // they hold goods; 0 when none matches.
    int Bag_FindItem(GuestPtr<Bag> bag, uint32_t id)
    {
        const uint32_t end = slotAddress(bag, port::load<uint8_t>(globals::kBagSlotsBySize + bag->size));
        for (uint32_t slot = slotAddress(bag, 0);; slot += 16u)
        {
            const uint16_t group = port::load<uint16_t>(slot);
            if (group == 0 && !(slot < end))
                return 0;
            if (port::load<uint16_t>(goodsRecordOf(group, port::load<uint16_t>(slot + 2u))) == id)
                return static_cast<int>(slot);
        }
    }

    // Removes the slot: the ones after it move up and the last of the size is emptied; a size 1 bag left empty
    // has no size again.
    void Bag_RemoveSlot(GuestPtr<Bag> bag, GuestPtr<uint16_t> slot)
    {
        const int slots = port::load<uint8_t>(globals::kBagSlotsBySize + bag->size);
        for (int i = 0; i < slots; ++i)
        {
            if (slotAddress(bag, static_cast<uint32_t>(i)) != slot.address)
                continue;
            for (int j = i; j < slots - 1; ++j)
                for (uint32_t k = 0; k < 16u; k += 4u)
                    port::store<uint32_t>(slotAddress(bag, static_cast<uint32_t>(j)) + k, port::load<uint32_t>(slotAddress(bag, static_cast<uint32_t>(j + 1)) + k));
            port::store<uint16_t>(slotAddress(bag, static_cast<uint32_t>(slots - 1)), 0);
            if (bag->size == 1 && bag->slots[0].item == 0)
                bag->size = 0;
            return;
        }
    }

    // The goods record of the slot being set up (+0x1A4, -1 none): returns its +5 byte and writes how it is used for
    // categories 0x17 (0), 0x1A (1), 0x1B (2) and 0x1C (3).
    uint32_t BagMenu_GetSetupKind(GuestPtr<Bag> bag, GuestPtr<uint32_t> kind)
    {
        const int32_t setup = bag->unk_1A4;
        if (setup == -1)
            return 0xFFFFFFFFu;
        const uint32_t slot = slotAddress(bag, static_cast<uint32_t>(setup));
        const uint32_t record = goodsRecordOf(port::load<uint16_t>(slot), port::load<uint16_t>(slot + 2u));
        switch (port::load<uint8_t>(record + 4u))
        {
        case 0x1C:
            *kind = 3;
            break;
        case 0x1B:
            *kind = 2;
            break;
        case 0x1A:
            *kind = 1;
            break;
        case 0x17:
            *kind = 0;
            break;
        default:
            break;
        }
        return port::load<uint8_t>(record + 5u);
    }

    // The menu's texts for the slot under the cursor (+0x1A0): the goods name (record +8, or the empty text) in the
    // text box at +0x170, and for usable goods the action in the one at +0x188 with the usable flag (+0x1A8).
    void BagMenu_UpdateItemText(GuestPtr<Bag> bag)
    {
        const uint32_t slot = slotAddress(bag, static_cast<uint32_t>(bag->unk_1A0));
        const GuestPtr<TextBox> nameBox{static_cast<uint32_t>(bag->unk_170)};
        const uint16_t group = port::load<uint16_t>(slot);
        if (group == 0)
        {
            TextBox_SetText(nameBox, GuestPtr<char>{0x001B5B10u}, 0);
            return;
        }
        const uint32_t record = goodsRecordOf(group, port::load<uint16_t>(slot + 2u));
        TextBox_SetText(nameBox, GuestPtr<char>{record + 8u}, 0);
        uint32_t action;
        int32_t usable = 1;
        switch (port::load<uint8_t>(record + 4u))
        {
        case 0x1C:
        case 0x1B:
        case 0x1A:
        case 0x17:
            action = 0x001B5B40u;
            break;
        case 0x15:
            action = 0x001B5B30u;
            break;
        case 0x13:
            action = 0x001B5B28u;
            break;
        case 0x11:
            action = 0x001B5B20u;
            break;
        case 0x10:
            action = 0x001B5B00u;
            usable = 0;
            break;
        default:
            return;
        }
        TextBox_SetText(GuestPtr<TextBox>{port::load<uint32_t>(bag.address + 0x188u)}, GuestPtr<char>{action}, 0);
        port::store<int32_t>(bag.address + 0x1A8u, usable);
    }

    // ---- goods records

    // The goods' price: the record's signed byte 2 times ten to the power of byte 3.
    int Goods_GetPrice(int item)
    {
        const uint32_t record = static_cast<uint32_t>(item);
        const uint64_t exponent = sdk::d_utod(port::load<uint8_t>(record + 3u));
        const int scale = sdk::dptoli(sdk::pow(0x4024000000000000ull, exponent)); // 10.0
        return static_cast<int>(static_cast<uint32_t>(port::load<int8_t>(record + 2u)) * static_cast<uint32_t>(scale));
    }

    // The goods record of a group: the offset (in and out) advances by the group's records per variant (byte 3) for
    // variant a1 (modulo byte 2), and by a2 more when the group has two or more records per variant (the season).
    int Goods_GetRecordOffset(int group, int a1, int a2, GuestPtr<int32_t> offset)
    {
        const uint32_t entry = port::load<uint32_t>(0x001BDA9Cu) + 4u * static_cast<uint32_t>(group);
        const uint32_t variants = port::load<uint8_t>(entry + 2u), perVariant = port::load<uint8_t>(entry + 3u);
        *offset = static_cast<int32_t>(static_cast<uint32_t>(*offset) + perVariant * (static_cast<uint32_t>(remainder(a1, static_cast<int32_t>(variants))) * 4u));
        if (port::load<uint8_t>(entry + 3u) >= 2)
            *offset = static_cast<int32_t>(static_cast<uint32_t>(*offset) + static_cast<uint32_t>(a2) * 4u);
        return static_cast<int>(port::load<uint32_t>(0x001BDAA0u) + 0x88u * (static_cast<uint32_t>(*offset) + port::load<uint16_t>(entry)));
    }

    PORT_FN(Goods_GetPrice);
    PORT_FN(Goods_GetRecordOffset);
    PORT_FN(Applicants_CountActive);
    PORT_FN(Applicants_GetSelected);
    PORT_FN(Applicants_RemoveFamily);
    PORT_FN(Applicants_SaveState);
    PORT_FN(Bag_SaveState);
    PORT_FN(Bag_AddItem);
    PORT_FN(Bag_AddMusicTrack);
    PORT_FN(Bag_Clear);
    PORT_FN(Bag_FindItem);
    PORT_FN(Bag_RemoveSlot);
    PORT_FN(BagMenu_GetSetupKind);
    PORT_FN(BagMenu_UpdateItemText);

    // ---- the bag menu's draw (batch 40)

    // The bag in the city menu: the bag model's matrix (node 0) is copied to the frame node of the size (size + 5, or 6 for
    // no bag) of the shared bag-slot model, which is drawn. While the menu is in a state below 5 the list's backdrop (node 1
    // of the bag model, matrix reset, 4.4 away, with the Z buffer cleared first), its list and text, the selected slot's
    // packets and texts (the item name and, with the flag in +0x1A8, its description) and the closing widget are drawn.
    void BagMenu_Draw(GuestPtr<Bag> bag)
    {
        const uint32_t b = bag.address;
        const GuestPtr<A3DObject> frameModel{port::load<uint32_t>(0x001F3B08u)};
        A3DObject_UpdateMatrices(GuestPtr<A3DObject>{port::load<uint32_t>(b + 0x16Cu)});
        int node = bag->size + 5;
        const uint32_t source = port::load<uint32_t>(port::load<uint32_t>(port::load<uint32_t>(b + 0x16Cu)) + 8u) + 0x70u;
        if (node == 5)
            ++node;
        const uint32_t dest = port::load<uint32_t>(port::load<uint32_t>(frameModel.address) + 8u) + 0xC0u * static_cast<uint32_t>(node);
        for (uint32_t row = 0; row < 0x40; row += 0x10)
            *GuestPtr<Vec4>{dest + 0x70u + row} = *GuestPtr<Vec4>{source + row};
        A3DObject_DrawNode(frameModel, node);
        if (port::load<uint32_t>(b + 0x194u) >= 5)
            return;
        const auto packet = [&](uint32_t offset) {
            if (port::load<int32_t>(b + offset) != 0)
                Dma_AppendCall(port::load<int32_t>(b + offset));
        };
        const auto text = [&](uint32_t offset) {
            if (port::load<uint32_t>(b + offset) != 0)
                TextBox_Draw(GuestPtr<TextBox>{port::load<uint32_t>(b + offset)});
        };
        if (port::load<uint32_t>(b + 0x174u) != 0)
        {
            DmaList_QueueClearZ();
            const uint32_t nodes = port::load<uint32_t>(port::load<uint32_t>(port::load<uint32_t>(b + 0x16Cu)) + 8u) + 0x130u;
            Mat4_Copy(GuestPtr<Vec4>{nodes}, GuestPtr<Vec4>{globals::kIdentityMatrix});
            port::store<uint32_t>(nodes + 0x38u, 0xC08CCCCDu);
            A3DObject_DrawNode(GuestPtr<A3DObject>{port::load<uint32_t>(b + 0x16Cu)}, 1);
        }
        if (port::load<uint32_t>(b + 0x174u) != 0)
            ListMenu_Draw(GuestPtr<ListMenu>{port::load<uint32_t>(b + 0x174u)});
        text(0x170u);
        if (port::load<uint16_t>(b + 16u * port::load<uint32_t>(b + 0x1A0u) + 4u) != 0)
        {
            packet(0x184u);
            text(0x190u);
            if (port::load<uint32_t>(b + 0x1A8u) != 0)
            {
                packet(0x17Cu);
                text(0x188u);
            }
        }
        packet(0x180u);
        text(0x18Cu);
        const uint32_t closing = port::load<uint32_t>(b + 0x178u);
        if (closing != 0)
            port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(closing + 8u) + 0x20u), closing, 2);
    }

    PORT_FN(BagMenu_Draw);

    // Adds a family to the applicants list (up to 32): state, family, the month, the home type of the family, and the
    // 0x4000 / 0x3F80 markers of an entry not yet placed. Returns the new entry's index, 0 when the list is full.
    int Applicants_Add(GuestPtr<Applicants> applicants, int family, int month, uint16_t state)
    {
        const int32_t index = port::load<int32_t>(applicants.address + 0x280u);
        if (!(index < 0x20))
            return 0;
        const GuestPtr<Family> record = Family_Get(family, 0);
        const uint32_t entry = applicants.address + 0x14u * static_cast<uint32_t>(index);
        port::store<uint16_t>(entry + 2u, state);
        port::store<uint16_t>(entry + 4u, static_cast<uint16_t>(family));
        port::store<int32_t>(entry + 0xCu, month);
        port::store<uint16_t>(entry + 8u, 0x3F80);
        port::store<uint16_t>(entry + 0xAu, port::load<uint16_t>(record.address + 0x20u));
        port::store<uint16_t>(entry + 6u, 0x4000);
        port::store<uint16_t>(entry, static_cast<uint16_t>(index));
        port::store<int32_t>(entry + 0x10u, 0);
        port::store<int32_t>(applicants.address + 0x280u, port::load<int32_t>(applicants.address + 0x280u) + 1);
        return index;
    }

    PORT_FN(Applicants_Add);

    // ---- The bag's item list (plan P6 batch 2) ----

    // Rebuilds the list menu (+0x174) from the bag slots, up to the first empty one and within the slots its size gives: the goods'
    // name (the offset in the record, into the text at 0x1BDAA4), and for the goods of kinds 0x17, 0x1A, 0x1B and 0x1C that
    // stack, a count appended when there are two or more. The list is then updated against the selection at +0x1A0 and
    // its result returned.
    int BagMenu_RefreshList(GuestPtr<Bag> bag)
    {
        port::GuestFrame<0x90> frame;
        const uint32_t line = frame.ptr().address + 0x50u;
        const auto list = [&] { return GuestPtr<ListMenu>{port::load<uint32_t>(bag.address + 0x174u)}; };
        ListMenu_ClearItem(list(), -1);
        for (int i = 0; i < port::load<uint8_t>(0x001B5B08u + bag->size); ++i)
        {
            const uint32_t slot = bag.address + 4u + 0x10u * static_cast<uint32_t>(i);
            if (port::load<uint16_t>(slot) == 0)
                break;
            const uint32_t record = goodsRecordOf(port::load<uint16_t>(slot), port::load<uint16_t>(slot + 2u));
            sdk::strcpy(line, port::load<uint32_t>(0x001BDAA4u) + port::load<uint16_t>(record));
            const uint32_t kind = port::load<uint8_t>(record + 4u);
            if (kind == 0x17 || kind - 0x1Au < 2u || kind == 0x1C)
            {
                const uint32_t count = port::load<uint8_t>(slot + 6u);
                if (static_cast<int32_t>(count) >= 2)
                    sdk::sprintf(line + static_cast<uint32_t>(sdk::strlen(line)), 0x001B5B48u, count);
            }
            ListMenu_SetItemText(list(), GuestPtr<char>{line}, -1);
        }
        return ListMenu_HandleInput(list(), GuestPtr<int32_t>{bag.address + 0x1A0u});
    }

    // Ends the use of the selected slot (state 7, the use flag at +0x19C cleared): when something was used up one is taken from
    // the slot's count, and an empty slot is removed; the selection is cleared and the list rebuilt.
    void BagMenu_FinishUse(GuestPtr<Bag> bag, int used)
    {
        const uint32_t b = bag.address;
        port::store<int32_t>(b + 0x194u, 7);
        port::store<int32_t>(b + 0x19Cu, 0);
        const auto count = [&] { return b + (port::load<uint32_t>(b + 0x1A4u) << 4) + 0xAu; };
        if (used != 0)
            port::store<uint8_t>(count(), static_cast<uint8_t>(port::load<uint8_t>(count()) - 1u));
        if (port::load<uint8_t>(count()) == 0)
            Bag_RemoveSlot(bag, GuestPtr<uint16_t>{count() - 6u});
        port::store<int32_t>(b + 0x1A4u, -1);
        BagMenu_RefreshList(bag);
    }

    PORT_FN(BagMenu_RefreshList);
    PORT_FN(BagMenu_FinishUse);

    // Adds the family to the applicants in state 1 and, when `select` is not 0, moves the cursor to the new entry.
    void Applicants_AddAndShow(GuestPtr<Applicants> applicants, int family, int month, int select)
    {
        const int index = Applicants_Add(applicants, family, month, 1);
        if (select != 0)
            Applicants_Select(applicants, index);
    }

    PORT_FN(Applicants_AddAndShow);

    // One step of an applicant on its way to a home, by the entry's state (0, 2 and 8 and above do nothing here): 1 waits five
    // frames and, unless three entries are already in states 2 to 4, goes to 2; 3 waits nine frames, clears the footprint's
    // highlight on the map and tries to place the family's home (mm_Map_PlaceObject): no room (0x3F80) sends it back to 1,
    // otherwise the building (+8) is kept and it goes to 4; 4 waits for the building to be built (flag 0x20); 5 moves the family
    // in (mm_Family_AssignHome); 6 goes on to 7 and 7 takes the entry off the list.
    void Applicants_AssignToBuilding(GuestPtr<Applicants> applicants, GuestPtr<CityScene> city, int index)
    {
        const uint32_t entry = applicants.address + 0x14u * static_cast<uint32_t>(index);
        const auto setState = [&](uint16_t state) { port::store<uint16_t>(entry + 2u, state); };
        const auto tick = [&] {
            const int32_t timer = port::load<int32_t>(entry + 0x10u) + 1;
            port::store<int32_t>(entry + 0x10u, timer);
            return timer;
        };
        switch (port::load<uint16_t>(entry + 2u))
        {
        case 1:
            if (tick() >= 5)
            {
                int active = 0;
                for (uint32_t i = 0; i < 0x20u; ++i)
                {
                    const uint32_t state = port::load<uint16_t>(applicants.address + 0x14u * i + 2u);
                    if (state >= 2 && state < 5)
                        ++active;
                }
                if (active < 3)
                {
                    setState(2);
                    port::store<int32_t>(entry + 0x10u, 0);
                }
            }
            break;
        case 3:
            if (tick() >= 9)
            {
                const uint32_t family = port::load<uint16_t>(entry + 4u);
                const uint32_t tile = port::load<uint16_t>(entry + 6u);
                const uint32_t homeType = port::load<uint16_t>(Family_Get(static_cast<int>(family), 0).address + 0x20u);
                Map_UnhighlightArea(city->map, static_cast<int>(tile & 0x7Fu), static_cast<int>(tile >> 7),
                                    port::load<uint8_t>(globals::buildingTypeAddress(homeType) + 0x142u));
                const int placed = Map_PlaceObject(city->map, static_cast<int>(family), static_cast<int>(homeType),
                                                   static_cast<int>(tile & 0x7Fu), static_cast<int>(tile >> 7), static_cast<int>(city.address));
                if (placed == 0x3F80)
                    setState(1);
                else
                {
                    port::store<uint16_t>(entry + 8u, static_cast<uint16_t>(placed));
                    setState(4);
                }
            }
            break;
        case 4:
            if ((Map_GetRecord(city->map, port::load<uint16_t>(entry + 8u))->flags & 0x20u) != 0)
                setState(5);
            break;
        case 5:
            Family_AssignHome(city->map, city, port::load<uint16_t>(entry + 4u), port::load<uint16_t>(entry + 8u));
            setState(6);
            break;
        case 6:
            setState(7);
            break;
        case 7:
            Applicants_RemoveAt(applicants, index);
            break;
        default:
            break;
        }
    }

    PORT_FN(Applicants_AssignToBuilding);

    // Uses the goods of the selected slot (+0x1A0) by their kind (byte 4 of the record): kinds 0x17, 0x1A, 0x1B and 0x1C start the
    // use (state 6 at +0x194, a time of 120.0 at +0x19C, sound 0x10) and mark the slot as the one in use (+0x1A4); kind 0x15 swaps
    // the bag's halfword at +2 for the record's byte 5, takes the slot out and adds goods (group 5 when the old value was 0,
    // otherwise group 0xE5 with index old - 1), then rebuilds the list; kinds 0x13 and 0x11 are eaten (mm_Stamina_Eat) and the slot
    // is taken out, the list rebuilt and sound 0xE played. Other kinds do nothing.
    void BagMenu_UseSelected(GuestPtr<Bag> bag, GuestPtr<CityScene> city)
    {
        const uint32_t b = bag.address;
        const uint32_t slot = b + (port::load<uint32_t>(b + 0x1A0u) << 4) + 4u;
        const uint32_t record = goodsRecordOf(port::load<uint16_t>(slot), port::load<uint16_t>(slot + 2u));
        switch (port::load<uint8_t>(record + 4u))
        {
        case 0x1C:
        case 0x1B:
        case 0x1A:
        case 0x17:
            port::store<int32_t>(b + 0x194u, 6);
            port::store<uint32_t>(b + 0x19Cu, 0x42F00000u);
            Sound_PlaySe(0x10, 0, 0x3C, ps2f(0.0f), ps2f(0.0f));
            port::store<uint32_t>(b + 0x1A4u, port::load<uint32_t>(b + 0x1A0u));
            break;
        case 0x15:
        {
            const uint32_t previous = port::load<uint16_t>(b + 2u);
            port::store<uint16_t>(b + 2u, port::load<uint8_t>(record + 5u));
            Bag_RemoveSlot(bag, GuestPtr<uint16_t>{b + (port::load<uint32_t>(b + 0x1A0u) << 4) + 4u});
            Bag_AddItem(bag, previous == 0 ? 5 : 0xE5, previous == 0 ? 0 : static_cast<int>(previous) - 1, static_cast<int16_t>(port::load<int32_t>(0x001C5548u)),
                        port::load<int32_t>(city.address + 0x88u), 0, 0xFF, 0);
            BagMenu_RefreshList(bag);
            break;
        }
        case 0x13:
        case 0x11:
            Stamina_Eat(GuestPtr<Stamina>{port::load<uint32_t>(city.address + 0x64u)}, GuestPtr<uint8_t>{record});
            Bag_RemoveSlot(bag, GuestPtr<uint16_t>{b + (port::load<uint32_t>(b + 0x1A0u) << 4) + 4u});
            BagMenu_RefreshList(bag);
            Sound_PlaySe(0xE, 0, 0x3C, ps2f(0.0f), ps2f(0.0f));
            break;
        default:
            break;
        }
    }

    PORT_FN(BagMenu_UseSelected);

    // One frame of the applicants list (city modes 3 and 4). The card moves with a point at +0x330 (x, y, z) and a speed at +0x340
    // that doubles every frame; the states at +0x31C: 0 closed: every 11 frames the next entry (of 32, +0x324) is assigned a
    // building (mm_Applicants_AssignToBuilding), and in city mode 3 the list opens (state 1, speed 0.02, sound 7); 1 slides in
    // (the y falls by the speed to 0.21), then waits (2) and the HUD flags of the city (+0x34, +0x38) go off; 2 waits for
    // Triangle (close: state 3, sound 8), Cross (place the selected entry, when it is of a kind 1 to 3 or 8: state 5) or left and
    // right (the next family of the entry prefetched); 3 slides out (y rises to 2.3) and the list is shut (mode 0, result 1);
    // 4 shows the notice for the selected entry's kind (1 and 2: by the building type, 3: notice 3); 5 flies the card away (x
    // falls, y rises to 1.8, z rises) and then, for a kind 3 entry, unhighlights its place on the map, switches the player to
    // mode 2 and goes to state 4 (result 4); 6 flies it back (y falls to 0.21), switches the player back, scrolls to nothing and
    // releases the pad (result 3). The card is placed at the end of every state but 0 and 4. The result is the city mode, or
    // the value set above.
    int Applicants_Update(GuestPtr<Applicants> applicants, GuestPtr<CityScene> city)
    {
        const uint32_t a = applicants.address;
        const uint32_t c = city.address;
        const auto loadF = [](uint32_t at) { return ps2f(std::bit_cast<float>(port::load<uint32_t>(at))); };
        const auto storeF = [](uint32_t at, ps2f value) { port::store<uint32_t>(at, std::bit_cast<uint32_t>(value.v)); };
        const auto place = [&]() { Applicants_PlaceAt(applicants, GuestPtr<ps2f>{a + 0x330u}); };
        // The cursor stays among the entries; the family of the entry it lands on is prefetched.
        const auto moveCursor = [&](int32_t step) {
            port::store<int32_t>(a + 0x2CCu, port::load<int32_t>(a + 0x2CCu) + step);
            const int32_t moved = port::load<int32_t>(a + 0x2CCu);
            int32_t clamped = 0;
            if (moved >= 0)
            {
                clamped = port::load<int32_t>(a + 0x280u) - 1;
                if (!(clamped < moved))
                    clamped = moved;
            }
            port::store<int32_t>(a + 0x2CCu, clamped);
            const int32_t at = port::load<int32_t>(a + 0x2CCu);
            Family_Prefetch(port::load<uint16_t>(a + 20u * static_cast<uint32_t>(at) + 4u), 0);
        };
        // The entry under the cursor, or 0 with none.
        const auto selected = [&]() { return port::load<int32_t>(a + 0x280u) == 0 ? 0u : a + 20u * port::load<uint32_t>(a + 0x2CCu); };
        int result = CityScene_GetMode(city);
        switch (port::load<uint32_t>(a + 0x31Cu))
        {
        case 0:
        {
            const int32_t timer = port::load<int32_t>(a + 0x328u) + 1;
            port::store<int32_t>(a + 0x328u, timer);
            if (timer >= 11)
            {
                port::store<int32_t>(a + 0x328u, 0);
                Applicants_AssignToBuilding(applicants, city, port::load<int32_t>(a + 0x324u));
                const int32_t next = port::load<int32_t>(a + 0x324u) + 1;
                port::store<int32_t>(a + 0x324u, next);
                if (!(next < 0x20))
                    port::store<int32_t>(a + 0x324u, 0);
            }
            if (result == 3)
            {
                port::store<int32_t>(a + 0x31Cu, 1);
                port::store<uint32_t>(a + 0x340u, 0x3CA3D70Au);
                Applicants_SetMode(applicants, 1);
                Sound_PlaySe(7, 0, 0x3C, 0.0f, 0.0f);
            }
            return result;
        }
        case 1:
        {
            const ps2f arrival(std::bit_cast<float>(0x3E570A3Du));
            storeF(a + 0x334u, loadF(a + 0x334u) - loadF(a + 0x340u));
            storeF(a + 0x340u, loadF(a + 0x340u) + loadF(a + 0x340u));
            if (loadF(a + 0x334u) <= arrival)
            {
                port::store<int32_t>(a + 0x31Cu, 2);
                storeF(a + 0x334u, arrival);
                port::store<uint32_t>(c + 0x34u, 0u);
                port::store<uint32_t>(c + 0x38u, 0u);
            }
            place();
            return result;
        }
        case 2:
            if ((Pad_Pressed(0) & 0x10u) != 0)
            {
                port::store<int32_t>(a + 0x31Cu, 3);
                port::store<uint32_t>(a + 0x340u, 0x3CA3D70Au);
                port::store<uint32_t>(c + 0x34u, 1u);
                port::store<uint32_t>(c + 0x38u, 1u);
                Sound_PlaySe(8, 0, 0x3C, 0.0f, 0.0f);
            }
            else if ((Pad_Pressed(0) & 0x40u) != 0)
            {
                const uint32_t kind = port::load<uint16_t>(a + 20u * port::load<uint32_t>(a + 0x2CCu) + 2u);
                if ((kind != 0 && kind < 4) || kind == 8)
                {
                    port::store<int32_t>(a + 0x31Cu, 5);
                    port::store<uint32_t>(a + 0x340u, 0x3CA3D70Au);
                }
            }
            else if ((Pad_GetRepeat() & 0x8000) != 0)
                moveCursor(-1);
            else if ((Pad_GetRepeat() & 0x2000) != 0)
                moveCursor(1);
            return result;
        case 3:
        {
            const ps2f out(std::bit_cast<float>(0x40133333u));
            storeF(a + 0x334u, loadF(a + 0x334u) + loadF(a + 0x340u));
            storeF(a + 0x340u, loadF(a + 0x340u) + loadF(a + 0x340u));
            if (!(loadF(a + 0x334u) < out))
            {
                port::store<int32_t>(a + 0x31Cu, 0);
                storeF(a + 0x334u, out);
                Applicants_SetMode(applicants, 0);
                result = 1;
            }
            place();
            return result;
        }
        case 4:
        {
            const uint32_t entry = selected();
            const int32_t kind = port::load<uint16_t>(entry + 2u);
            if (kind == 1 || kind == 2)
            {
                const int type = Family_GetTypeIndex(port::load<uint16_t>(entry + 4u));
                const uint32_t record = globals::buildingTypeAddress(static_cast<uint32_t>(type));
                const uint8_t sort = port::load<uint8_t>(record + 0x144u);
                if (sort == 4)
                    Notice_Show(city->notice, 5);
                else if (sort == 0)
                    Notice_Show(city->notice, (port::load<uint16_t>(record + 0x136u) & 0x10u) != 0 ? 6 : 2);
                else
                    Notice_Show(city->notice, 4);
            }
            else if (kind == 3)
                Notice_Show(city->notice, 3);
            return result;
        }
        case 5:
        {
            storeF(a + 0x330u, loadF(a + 0x330u) - ps2f(0.8f) * loadF(a + 0x340u));
            storeF(a + 0x334u, loadF(a + 0x334u) + ps2f(0.5f) * loadF(a + 0x340u));
            storeF(a + 0x338u, loadF(a + 0x338u) + ps2f(2.0f) * loadF(a + 0x340u));
            storeF(a + 0x340u, loadF(a + 0x340u) + loadF(a + 0x340u));
            if (!(loadF(a + 0x334u) < ps2f(std::bit_cast<float>(0x3FE66666u))))
            {
                const uint32_t entry = selected();
                if (port::load<uint16_t>(entry + 2u) == 3)
                {
                    const int type = Family_GetTypeIndex(port::load<uint16_t>(entry + 4u));
                    const uint32_t position = port::load<uint16_t>(entry + 6u);
                    Map_UnhighlightArea(city->map, static_cast<int>(position & 0x7Fu), static_cast<int>(position >> 7),
                                        port::load<uint8_t>(globals::buildingTypeAddress(static_cast<uint32_t>(type)) + 0x142u));
                }
                Player_SetMode(city->player, 2);
                result = 4;
                port::store<int32_t>(a + 0x31Cu, 4);
                port::store<uint32_t>(a + 0x330u, 0xC0400000u);
                port::store<uint32_t>(a + 0x334u, 0x3FE66666u);
                port::store<uint32_t>(a + 0x338u, 0x40E00000u);
            }
            place();
            return result;
        }
        case 6:
        {
            const ps2f arrival(std::bit_cast<float>(0x3E570A3Du));
            storeF(a + 0x330u, loadF(a + 0x330u) + ps2f(0.8f) * loadF(a + 0x340u));
            storeF(a + 0x334u, loadF(a + 0x334u) - ps2f(0.5f) * loadF(a + 0x340u));
            storeF(a + 0x338u, loadF(a + 0x338u) - ps2f(2.0f) * loadF(a + 0x340u));
            storeF(a + 0x340u, loadF(a + 0x340u) + loadF(a + 0x340u));
            if (loadF(a + 0x334u) <= arrival)
            {
                Player_SetMode(city->player, -1);
                result = 3;
                port::store<int32_t>(a + 0x31Cu, 2);
                port::store<uint32_t>(a + 0x330u, 0u);
                storeF(a + 0x334u, arrival);
                port::store<uint32_t>(a + 0x338u, 0u);
                Applicants_ScrollTo(applicants, -1);
                Pad_ReleaseFocus(1);
            }
            place();
            return result;
        }
        default:
            return result;
        }
    }

    PORT_FN(Applicants_Update);

    namespace
    {
        // A text box of `columns` by `rows` with the style given, or 0 when the allocation fails.
        uint32_t makeTextBox(int columns, int rows, int style)
        {
            const uint32_t memory = sdk::operator_new(0x40u);
            return memory != 0u ? TextBox_Init(GuestPtr<TextBox>{memory}, columns, rows, style).address : 0u;
        }

        // A frame of the given style, place, size and colour, or 0 when the allocation fails.
        uint32_t makeFrame(int style, int x, int y, int width, int height, uint32_t colour)
        {
            const uint32_t memory = sdk::operator_new(0xC0u);
            return memory != 0u ? Frame_ctor(GuestPtr<Frame>{memory}, style, x, y, width, height, colour).address : 0u;
        }

        // The bag model's place slides between two points by t = time / 120: the place is (1 - t) * from + t * to (the products added
        // as the FPU's accumulator does), and the model is drawn swaying at that time.
        void slideBag(uint32_t bag, ps2f time, uint32_t from)
        {
            constexpr uint32_t kTo = 0x001AEF00u;
            const ps2f t = time / ps2f(120.0f);
            const ps2f rest = ps2f(1.0f) - t;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const ps2f a = ps2f(std::bit_cast<float>(port::load<uint32_t>(from + 4u * axis)));
                const ps2f b = ps2f(std::bit_cast<float>(port::load<uint32_t>(kTo + 4u * axis)));
                const ps2f place = (rest * a) + (t * b);
                port::store<uint32_t>(port::load<uint32_t>(bag + 0x198u) + 0x30u + 4u * axis, std::bit_cast<uint32_t>(place.v));
            }
        }
    }

    // One frame of the bag menu (city mode 11 and the modes around it). `time` (+0x19C) counts the bag model's slide in 10s up to
    // 120; the states at +0x194: 0 clears the HUD flags and the applicants' flag, plays sound 7 and starts the slide; 1 slides it in
    // (the model moves from 0x1AEF10 to 0x1AEF00), and at 120 builds the list menu (4, 10 or 20 items by the bag size), the text
    // boxes and frames, and goes to 2; 2 is the menu: the list takes the pad (the choice is +0x1A0), the item text is updated, Cross
    // uses a filled slot (mm_BagMenu_UseSelected), Square opens the throw away question (state 4) and Triangle shuts it all (sound
    // 8, state 3); 3 slides out and gives the HUD back and leaves the mode; 4 runs the question, and a Yes (0) removes the slot (sound
    // 15) and refreshes the list; 5 shows the selected item's name (and its count when it is a food, drink or clothes item with
    // two or more) as a notice; 6 and 7 slide the model out and in between 0x1AEF20 and 0x1AEF00 for a bag item being set up, 6 on
    // to mode 11 and the player's mode 5, 7 back.
    void BagMenu_Update(GuestPtr<Bag> bag, GuestPtr<CityScene> city)
    {
        port::GuestFrame<0x80> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t b = bag.address;
        const uint32_t c = city.address;
        const auto loadF = [](uint32_t at) { return ps2f(std::bit_cast<float>(port::load<uint32_t>(at))); };
        const auto storeF = [](uint32_t at, ps2f value) { port::store<uint32_t>(at, std::bit_cast<uint32_t>(value.v)); };
        const auto setState = [&](int32_t state) { port::store<int32_t>(b + 0x194u, state); };
        const auto slotOf = [&]() { return b + 16u * port::load<uint32_t>(b + 0x1A0u); };
        const auto model = [&]() { return GuestPtr<A3DObject>{port::load<uint32_t>(b + 0x16Cu)}; };
        const auto hudBack = [&]() {
            port::store<uint32_t>(c + 0x30u, 1u);
            port::store<uint32_t>(c + 0x34u, 1u);
            port::store<uint8_t>(port::load<uint32_t>(c + 0x54u) + 0x2C9u, 1);
        };
        switch (port::load<uint32_t>(b + 0x194u))
        {
        case 0:
            port::store<uint32_t>(b + 0x19Cu, 0u);
            setState(1);
            port::store<uint32_t>(c + 0x30u, 0u);
            port::store<uint32_t>(c + 0x34u, 0u);
            port::store<uint8_t>(port::load<uint32_t>(c + 0x54u) + 0x2C9u, 0);
            Sound_PlaySe(7, 0, 0x3C, 0.0f, 0.0f);
            break;
        case 1:
        {
            storeF(b + 0x19Cu, loadF(b + 0x19Cu) + ps2f(10.0f));
            slideBag(b, loadF(b + 0x19Cu), 0x001AEF10u);
            Instance_DrawSwaying(model(), 0, loadF(b + 0x19Cu));
            if (loadF(b + 0x19Cu) < ps2f(120.0f))
                break;
            setState(2);
            uint32_t list = sdk::operator_new(0x2A0u);
            if (list != 0u)
                list = ListMenu_ctor(GuestPtr<ListMenu>{list}, 6, 0x10, 0x127, 0x5A, 0x3C, 0xFF0000).address;
            port::store<uint32_t>(b + 0x174u, list);
            port::store<uint32_t>(list + 0x298u, 0u);
            port::store<uint32_t>(list + 0x294u, 0u);
            for (int i = 0; i < port::load<uint8_t>(globals::kBagSlotsBySize + port::load<uint16_t>(b)); ++i)
                ListMenu_AddItems(GuestPtr<ListMenu>{port::load<uint32_t>(b + 0x174u)}, 0xC, 1, 0, -1, 0, -1);
            BagMenu_RefreshList(bag);
            port::store<uint32_t>(b + 0x170u, makeTextBox(7, 8, -1));
            TextBox_SetPos(GuestPtr<TextBox>{port::load<uint32_t>(b + 0x170u)}, 0x8640, 0x7C20, 0xFF0000);
            port::store<uint32_t>(b + 0x17Cu, makeFrame(0x14, 0x1A4, 0x90, 0x18, 0xC, 0xFF0000u));
            port::store<uint32_t>(b + 0x184u, makeFrame(0x15, 0x1A4, 0x9C, 0x18, 0xC, 0xFF0000u));
            port::store<uint32_t>(b + 0x180u, makeFrame(0x17, 0x1A4, 0xA8, 0x18, 0xC, 0xFF0000u));
            port::store<uint32_t>(b + 0x188u, makeTextBox(4, 1, -1));
            port::store<uint32_t>(b + 0x18Cu, makeTextBox(4, 1, -1));
            port::store<uint32_t>(b + 0x190u, makeTextBox(4, 1, -1));
            TextBox_SetPos(GuestPtr<TextBox>{port::load<uint32_t>(b + 0x188u)}, 0x8820, 0x8200, 0xFF0000);
            TextBox_SetPos(GuestPtr<TextBox>{port::load<uint32_t>(b + 0x190u)}, 0x8820, 0x82C0, 0xFF0000);
            TextBox_SetPos(GuestPtr<TextBox>{port::load<uint32_t>(b + 0x18Cu)}, 0x8820, 0x8380, 0xFF0000);
            TextBox_SetText(GuestPtr<TextBox>{port::load<uint32_t>(b + 0x188u)}, GuestPtr<char>{0x001B5B40u}, 0);
            TextBox_SetText(GuestPtr<TextBox>{port::load<uint32_t>(b + 0x18Cu)}, GuestPtr<char>{0x001B5B50u}, 0);
            TextBox_SetText(GuestPtr<TextBox>{port::load<uint32_t>(b + 0x190u)}, GuestPtr<char>{0x001B5B58u}, 0);
            break;
        }
        case 2:
            ListMenu_HandleInput(GuestPtr<ListMenu>{port::load<uint32_t>(b + 0x174u)}, GuestPtr<int32_t>{b + 0x1A0u});
            BagMenu_UpdateItemText(bag);
            if ((Pad_Pressed(0) & 0x40u) != 0)
            {
                if (port::load<uint16_t>(slotOf() + 4u) != 0)
                    BagMenu_UseSelected(bag, city);
            }
            else if ((Pad_Pressed(0) & 0x80u) != 0)
            {
                if (port::load<uint16_t>(slotOf() + 4u) != 0)
                {
                    setState(4);
                    uint32_t question = sdk::operator_new(0x20u);
                    if (question != 0u)
                        question = MenuWin_ctor(GuestPtr<MenuWin>{question}).address;
                    port::store<uint32_t>(b + 0x178u, question);
                    MenuWin_Open(GuestPtr<MenuWin>{port::load<uint32_t>(b + 0x178u)}, 0, 2);
                    MenuWin_FillItems(GuestPtr<MenuWin>{port::load<uint32_t>(b + 0x178u)}, GuestPtr<char>{0x001B5B58u});
                    MenuWin_FillItems(GuestPtr<MenuWin>{port::load<uint32_t>(b + 0x178u)}, GuestPtr<char>{0x001B5B60u});
                }
            }
            else if ((Pad_Pressed(0) & 0x10u) != 0)
            {
                setState(3);
                port::store<uint32_t>(b + 0x19Cu, 0x42F00000u);
                lifetime::deleteWith(b + 0x174u, [](uint32_t list) { ListMenu_dtor(GuestPtr<ListMenu>{list}, 1); });
                lifetime::deleteVirtual(b + 0x178u, 8);
                lifetime::deleteTextBox(b + 0x170u);
                lifetime::deleteFrame(b + 0x17Cu);
                lifetime::deleteFrame(b + 0x180u);
                lifetime::deleteFrame(b + 0x184u);
                lifetime::deleteTextBox(b + 0x188u);
                lifetime::deleteTextBox(b + 0x18Cu);
                lifetime::deleteTextBox(b + 0x190u);
                Sound_PlaySe(8, 0, 0x3C, 0.0f, 0.0f);
            }
            break;
        case 3:
            storeF(b + 0x19Cu, loadF(b + 0x19Cu) - ps2f(10.0f));
            Instance_DrawSwaying(model(), 0, loadF(b + 0x19Cu));
            slideBag(b, loadF(b + 0x19Cu), 0x001AEF10u);
            if (loadF(b + 0x19Cu) <= ps2f(0.0f))
            {
                setState(0);
                CityScene_PopMode(city);
                hudBack();
            }
            break;
        case 4:
        {
            const uint32_t question = port::load<uint32_t>(b + 0x178u);
            port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(question + 8u) + 0x14u), question);
            if (port::callGuest<int>(port::load<uint32_t>(port::load<uint32_t>(port::load<uint32_t>(b + 0x178u) + 8u) + 0x24u), port::load<uint32_t>(b + 0x178u)) == 0)
                break;
            const int32_t answer = port::load<int32_t>(port::load<uint32_t>(b + 0x178u) + 0x18u);
            setState(2);
            lifetime::deleteVirtual(b + 0x178u, 8);
            if (answer == 0)
            {
                Sound_PlaySe(0xF, 0, 0x3C, 0.0f, 0.0f);
                Bag_RemoveSlot(bag, GuestPtr<uint16_t>{slotOf() + 4u});
                BagMenu_RefreshList(bag);
            }
            break;
        }
        case 5:
        {
            const uint32_t buffer = sp + 0x40u;
            port::store<uint8_t>(buffer, 0);
            const uint32_t slot = slotOf();
            const uint32_t item = port::load<uint16_t>(slot + 4u);
            if (item != 0)
            {
                const uint32_t first = port::load<uint16_t>(globals::goodsSectionA.get() + 4u * item);
                const uint32_t record = globals::goodsSectionB.get() + 0x88u * (port::load<uint16_t>(slot + 6u) + first);
                sdk::strcpy(buffer, globals::goodsSectionC.get() + port::load<uint16_t>(record));
                const uint32_t kind = port::load<uint8_t>(record + 4u);
                if (kind == 0x17 || kind - 0x1Au < 2u || kind == 0x1C)
                {
                    const uint32_t count = port::load<uint8_t>(slotOf() + 0xAu);
                    if (static_cast<int32_t>(count) >= 2)
                        sdk::sprintf(buffer + static_cast<uint32_t>(sdk::strlen(buffer)), 0x001B5B48u, count);
                }
            }
            Notice_ShowText(city->notice, GuestPtr<char>{buffer});
            break;
        }
        case 6:
            storeF(b + 0x19Cu, loadF(b + 0x19Cu) - ps2f(10.0f));
            slideBag(b, loadF(b + 0x19Cu), 0x001AEF20u);
            Instance_DrawSwaying(model(), 0, loadF(b + 0x19Cu));
            if (loadF(b + 0x19Cu) <= ps2f(0.0f))
            {
                port::store<uint32_t>(c + 0x30u, 1u);
                CityScene_PushMode(city, 0xB);
                Player_SetMode(city->player, 5);
                setState(5);
            }
            break;
        case 7:
            storeF(b + 0x19Cu, loadF(b + 0x19Cu) + ps2f(10.0f));
            slideBag(b, loadF(b + 0x19Cu), 0x001AEF20u);
            Instance_DrawSwaying(model(), 0, loadF(b + 0x19Cu));
            if (loadF(b + 0x19Cu) < ps2f(120.0f))
                break;
            Notice_Clear(city->notice);
            port::store<uint32_t>(c + 0x30u, 0u);
            CityScene_PopMode(city);
            setState(2);
            break;
        default:
            break;
        }
    }

    PORT_FN(BagMenu_Update);

    // The lighting of the applicant card's claim icons as the cursor (+0x2CC) and the shown entry (top, +0x2D0) move. Idle (+0x2B4 =
    // 0), a gap between them starts a step: forward (top < cursor: direction +1, the icon of the top entry lit on the eight nodes of the
    // table at 0x1B7A10, the cursor's on the first) or backward (direction -1, the phase +0x2B0 at 40, the other way round). Moving, the
    // phase runs by twice the direction; at 20 the shown entry steps toward the cursor (by the direction when they are next to each
    // other, else to the entry beside it), the info text is rebuilt (mode 1) and sound 9 plays; past 40 or below 0 the step ends and
    // the shown entry's icon is lit on all the nodes. An icon's colour (mm_ClaimIcon_GetColor, level 1) is brightened by halving
    // its distance from 1 on each of red, green and blue.
    void Applicants_DrawDetails(GuestPtr<Applicants> applicants)
    {
        port::GuestFrame<0x90> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t a = applicants.address;
        const auto loadF = [](uint32_t at) { return ps2f(std::bit_cast<float>(port::load<uint32_t>(at))); };
        const auto storeF = [](uint32_t at, ps2f value) { port::store<uint32_t>(at, std::bit_cast<uint32_t>(value.v)); };
        // The index in the field at `at` is kept among the entries (0 to count - 1).
        const auto clampIndex = [&](uint32_t at) {
            const int32_t index = port::load<int32_t>(at);
            int32_t clamped = 0;
            if (index >= 0)
            {
                clamped = port::load<int32_t>(a + 0x280u) - 1;
                if (!(clamped < index))
                    clamped = index;
            }
            port::store<int32_t>(at, clamped);
        };
        const auto entryColour = [&](int32_t entry, uint32_t out) {
            const uint32_t icon = port::load<uint16_t>(static_cast<uint32_t>(Claim_GetSectionC(port::load<uint16_t>(a + 0x2DCu + 2u * static_cast<uint32_t>(entry)))));
            ClaimIcon_GetColor(icon, 1, GuestPtr<Vec4>{out});
            for (uint32_t lane = 0; lane < 3; ++lane)
            {
                const ps2f value = loadF(out + 4u * lane);
                if (value < ps2f(1.0f))
                    storeF(out + 4u * lane, ps2f(1.0f) - ps2f(0.5f) * (ps2f(1.0f) - value));
                else
                    storeF(out + 4u * lane, ps2f(1.0f) + ps2f(0.5f) * (value - ps2f(1.0f)));
            }
            port::store<int32_t>(out + 0xCu, 12);
        };
        const auto model = [&]() { return GuestPtr<A3DObject>{port::load<uint32_t>(a + 0x28Cu)}; };
        const auto lightAll = [&](uint32_t colour) {
            for (uint32_t node = 0; node < 8; ++node)
                A3DObject_ChangeLuminosity(model(), GuestPtr<uint32_t>{colour}, port::load<uint8_t>(0x001B7A10u + node));
        };
        const auto lightFirst = [&](uint32_t colour) { A3DObject_ChangeLuminosity(model(), GuestPtr<uint32_t>{colour}, 0); };
        if (loadF(a + 0x2B4u) == ps2f(0.0f))
        {
            if (port::load<int32_t>(a + 0x280u) == 0)
                return;
            const int32_t top = port::load<int32_t>(a + 0x2D0u), cursor = port::load<int32_t>(a + 0x2CCu);
            if (top < cursor)
            {
                port::store<uint32_t>(a + 0x2B4u, 0x3F800000u);
                clampIndex(a + 0x2CCu);
                clampIndex(a + 0x2D0u);
                entryColour(port::load<int32_t>(a + 0x2D0u), sp + 0x70u);
                lightAll(sp + 0x70u);
                entryColour(port::load<int32_t>(a + 0x2CCu), sp + 0x70u);
                lightFirst(sp + 0x70u);
                port::store<uint8_t>(a + 0x2C8u, 0xFF);
            }
            else if (cursor < top)
            {
                port::store<uint32_t>(a + 0x2B4u, 0xBF800000u);
                port::store<uint32_t>(a + 0x2B0u, 0x42200000u);
                clampIndex(a + 0x2CCu);
                clampIndex(a + 0x2D0u);
                entryColour(port::load<int32_t>(a + 0x2D0u), sp + 0x80u);
                lightFirst(sp + 0x80u);
                entryColour(port::load<int32_t>(a + 0x2CCu), sp + 0x80u);
                lightAll(sp + 0x80u);
            }
            return;
        }
        if (loadF(a + 0x2B0u) == ps2f(20.0f))
        {
            const int32_t top = port::load<int32_t>(a + 0x2D0u), cursor = port::load<int32_t>(a + 0x2CCu);
            if (std::abs(top - cursor) < 2)
                port::store<int32_t>(a + 0x2D0u, port::load<int32_t>(a + 0x2D0u) + sdk::fptosi(loadF(a + 0x2B4u)));
            else if (cursor < top)
                port::store<int32_t>(a + 0x2D0u, cursor + 1);
            else
                port::store<int32_t>(a + 0x2D0u, cursor - 1);
            if (port::load<int32_t>(a + 0x2D0u) < 0)
                port::store<int32_t>(a + 0x2D0u, 0);
            port::store<int32_t>(a + 0x29Cu, 1);
            Applicants_BuildInfo(applicants);
            Sound_PlaySe(9, 0, 0x3C, 0.0f, 0.0f);
        }
        const ps2f phase = loadF(a + 0x2B0u) + ps2f(2.0f) * loadF(a + 0x2B4u);
        storeF(a + 0x2B0u, phase);
        if (phase < ps2f(40.0f) && !(phase < ps2f(0.0f)))
            return;
        port::store<uint32_t>(a + 0x2B0u, 0u);
        port::store<uint32_t>(a + 0x2B4u, 0u);
        entryColour(port::load<int32_t>(a + 0x2D0u), sp + 0x60u);
        lightAll(sp + 0x60u);
        lightFirst(sp + 0x60u);
    }

    PORT_FN(Applicants_DrawDetails);

    // The applicants panel for one frame (nothing while +0x2C9 is 0). With mode 1 and entries: the details step (Applicants_
    // DrawDetails), the card model (+0x28C) played at the phase +0x2B0 and, once +0x2BC was set, lit again with the shown entry's
    // colour (the flag is cleared) on all of its nodes; then the model is drawn, a changed entry count (+0x284) scrolls to the top, and
    // the entries' icons are placed on the nodes of the icon model (+0x290): the node is the second of the table at 0x1B7A18 for
    // an entry of kind 4 to 7, else the first; the matrix at that node is the identity moved by the entry's place (the spacing is
    // 2.2 over the count, kept between 0.05 and 0.1; the shown entry sits 0.07 lower, the others 0.001 a place of distance from
    // the cursor, less 0.03) and the node is drawn with the entry's brightened claim colour. Past the entries the marker (+0x2AC,
    // when +0x2C8 is not 0xFF) is placed beside the card (a depth from the table at 0x1B7A20) and drawn on node 2. With no entries
    // a white mesh 4 of the icon model is drawn instead. Whenever the mode is not 0 and +0x2B8 is set, the layout runs and the
    // shown entry's icon is drawn on node 0; the card text (+0x294) shows when +0x29C is set and there are entries.
    void Applicants_Draw(GuestPtr<Applicants> applicants)
    {
        port::GuestFrame<0xB0> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t a = applicants.address;
        const auto loadF = [](uint32_t at) { return ps2f(std::bit_cast<float>(port::load<uint32_t>(at))); };
        const auto storeF = [](uint32_t at, ps2f value) { port::store<uint32_t>(at, std::bit_cast<uint32_t>(value.v)); };
        const auto constant = [](uint32_t bits) { return ps2f(std::bit_cast<float>(bits)); };
        const auto cardModel = [a]() { return GuestPtr<A3DObject>{port::load<uint32_t>(a + 0x28Cu)}; };
        const auto iconModel = [a]() { return GuestPtr<A3DObject>{port::load<uint32_t>(a + 0x290u)}; };
        // The claim colour of the entry (the icon of its section C, level 1) with each of red, green and blue brought halfway to 1 from
        // where it is, and 12 in the fourth place.
        const auto entryColour = [&](int32_t entry, uint32_t out) {
            const uint32_t icon = port::load<uint16_t>(static_cast<uint32_t>(Claim_GetSectionC(port::load<uint16_t>(a + 0x2DCu + 2u * static_cast<uint32_t>(entry)))));
            ClaimIcon_GetColor(icon, 1, GuestPtr<Vec4>{out});
            for (uint32_t lane = 0; lane < 3; ++lane)
            {
                const ps2f value = loadF(out + 4u * lane);
                if (value < ps2f(1.0f))
                    storeF(out + 4u * lane, ps2f(1.0f) - ps2f(0.5f) * (ps2f(1.0f) - value));
                else
                    storeF(out + 4u * lane, ps2f(1.0f) + ps2f(0.5f) * (value - ps2f(1.0f)));
            }
            port::store<int32_t>(out + 0xCu, 12);
        };
        const uint8_t mode = port::load<uint8_t>(a + 0x2C9u);
        if (mode == 0)
            return;
        if (mode == 1)
        {
            DmaList_QueueClearZ();
            const int32_t count = port::load<int32_t>(a + 0x280u);
            if (count > 0)
            {
                Applicants_DrawDetails(applicants);
                Instance_DrawSwaying(cardModel(), 0, loadF(a + 0x2B0u));
                const int32_t relight = port::load<int32_t>(a + 0x2BCu);
                port::store<int32_t>(a + 0x2BCu, 0);
                if (relight != 0)
                {
                    entryColour(port::load<int32_t>(a + 0x2D0u), sp + 0xA0u);
                    A3DObject_ChangeLuminosity(cardModel(), GuestPtr<uint32_t>{sp + 0xA0u}, 0);
                    for (uint32_t node = 0; node < 8; ++node)
                        A3DObject_ChangeLuminosity(cardModel(), GuestPtr<uint32_t>{sp + 0xA0u}, port::load<uint8_t>(0x001B7A10u + node));
                }
                A3DObject_Draw(cardModel());
                const int32_t shown = port::load<int32_t>(a + 0x280u);
                const int32_t before = port::load<int32_t>(a + 0x284u);
                port::store<int32_t>(a + 0x284u, shown);
                if (shown != before)
                    Applicants_ScrollTo(applicants, -1);
                ps2f spacing = constant(0x400CCCCDu) / ps2f(static_cast<float>(port::load<int32_t>(a + 0x280u)));
                if (spacing < constant(0x3D4CCCCDu))
                    spacing = constant(0x3D4CCCCDu);
                else if (!(spacing <= constant(0x3DCCCCCDu)))
                    spacing = constant(0x3DCCCCCDu);
                for (int32_t i = 0; i < port::load<int32_t>(a + 0x280u); ++i)
                {
                    if (port::load<int32_t>(a + 0x2B8u) != 0 && i == port::load<int32_t>(a + 0x2D0u))
                        continue;
                    const uint32_t kind = port::load<uint16_t>(a + 0x14u * static_cast<uint32_t>(i) + 2u);
                    const uint32_t slot = kind >= 4u ? (kind < 8u ? 1u : 0u) : 0u;
                    const uint32_t nodeAt = 0x001B7A18u + slot;
                    const uint32_t node = port::load<uint8_t>(nodeAt);
                    const uint32_t matrix = port::load<uint32_t>(port::load<uint32_t>(iconModel().address) + 8u) + 0xC0u * node + 0x70u;
                    Mat4_Copy(GuestPtr<Vec4>{matrix}, GuestPtr<Vec4>{0x001B2550u});
                    const ps2f place = ps2f(static_cast<float>(i)) * spacing;
                    storeF(matrix + 0x30u, (loadF(port::load<uint32_t>(a + 0x2A4u) + 0x30u) + place) - constant(0x3F866666u));
                    storeF(matrix + 0x34u, constant(0xBF3AE148u) + loadF(port::load<uint32_t>(a + 0x2A4u) + 0x34u));
                    const int32_t cursor = port::load<int32_t>(a + 0x2B8u) != 0 ? port::load<int32_t>(a + 0x2C4u) : port::load<int32_t>(a + 0x2CCu);
                    if (cursor == i)
                        storeF(matrix + 0x38u, loadF(port::load<uint32_t>(a + 0x2A4u) + 0x38u) - constant(0x3D8F5C29u));
                    else
                    {
                        const int32_t distance = std::abs(cursor - i);
                        storeF(matrix + 0x38u, (loadF(port::load<uint32_t>(a + 0x2A4u) + 0x38u) + constant(0x3A83126Fu) * ps2f(static_cast<float>(distance))) - constant(0x3CF5C28Fu));
                    }
                    entryColour(i, sp + 0x90u);
                    Instance_DrawNode(iconModel(), GuestPtr<Vec4>{sp + 0x90u}, static_cast<int>(node), 0);
                }
                const uint8_t marker = port::load<uint8_t>(a + 0x2C8u);
                if (marker != 0xFF)
                {
                    const uint32_t markerMatrix = port::load<uint32_t>(a + 0x2ACu);
                    Mat4_Copy(GuestPtr<Vec4>{markerMatrix}, GuestPtr<Vec4>{0x001B2550u});
                    storeF(port::load<uint32_t>(a + 0x2ACu) + 0x30u, loadF(port::load<uint32_t>(a + 0x2A0u) + 0x30u) + loadF(0x001B7A20u + 4u * marker));
                    port::store<uint32_t>(port::load<uint32_t>(a + 0x2ACu) + 0x34u, port::load<uint32_t>(port::load<uint32_t>(a + 0x2A0u) + 0x34u));
                    port::store<uint32_t>(port::load<uint32_t>(a + 0x2ACu) + 0x38u, port::load<uint32_t>(port::load<uint32_t>(a + 0x2A0u) + 0x38u));
                    Instance_DrawNode(iconModel(), GuestPtr<Vec4>{sp + 0x90u}, 2, 0);
                }
            }
            else
            {
                port::store<uint32_t>(sp + 0x90u, 0x437F0000u);
                port::store<uint32_t>(sp + 0x94u, 0x437F0000u);
                port::store<uint32_t>(sp + 0x98u, 0x437F0000u);
                port::store<int32_t>(sp + 0x9Cu, 12);
                Instance_DrawMesh(iconModel(), GuestPtr<Vec4>{port::load<uint32_t>(a + 0x2A0u)}, GuestPtr<Vec4>{sp + 0x90u}, 4, 0);
            }
        }
        if (port::load<int32_t>(a + 0x2B8u) != 0)
        {
            Applicants_Layout(applicants);
            DmaList_QueueClearZ();
            entryColour(port::load<int32_t>(a + 0x2D0u), sp + 0x90u);
            Instance_DrawNode(iconModel(), GuestPtr<Vec4>{sp + 0x90u}, 0, 0);
        }
        if (port::load<int32_t>(a + 0x29Cu) != 0 && port::load<int32_t>(a + 0x280u) != 0)
            TextBox_Draw(GuestPtr<TextBox>{port::load<uint32_t>(a + 0x294u)});
    }

    PORT_FN(Applicants_Draw);
}
