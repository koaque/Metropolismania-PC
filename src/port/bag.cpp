// The bag: up to 20 slots (4, 10 or 20 by its size) of goods, each naming a goods record by group and index
// (records of 0x88 bytes from 0x1BDAA0; each group's first record in the table at 0x1BDA9C), and ten shortcuts
// holding music tracks. Also the applicants list's small helpers.

#include "game_functions.h"
#include "game_globals.h"
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
}
