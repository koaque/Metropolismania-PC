// The bag: up to 20 slots (4, 10 or 20 by its size) of goods, each naming a goods record by group and index
// (records of 0x88 bytes from 0x1BDAA0; each group's first record in the table at 0x1BDA9C), and the ten music
// tracks bought. Also the applicants list's small helpers.

#include <bit>

#include "game_functions.h"
#include "game_globals.h"
#include "widgets.h"
#include "lifetime.h"
#include "sdk.h"

namespace mm
{
    namespace
    {
        // The goods record of a group and index (section A holds each group's first record).
        GuestPtr<GoodsRecord> goodsRecordOf(uint32_t group, uint32_t index)
        {
            const uint32_t first = port::load<uint16_t>(globals::goodsSectionA.get() + 4u * group);
            return GuestPtr<GoodsRecord>{globals::goodsSectionB.get() + 0x88u * (index + first)};
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
    void Bag_AddItem(GuestPtr<Bag> bag, int group, int index, int16_t stage, int month, int16_t seller, uint8_t t2, uint8_t t3)
    {
        if (bag->size == 0)
            bag->size = 1;
        GuestPtr<BagSlot> slot{slotAddress(bag, 0)};
        while (slot->group != 0)
            ++slot;
        slot->group = static_cast<uint16_t>(group);
        slot->index = static_cast<uint16_t>(index);
        slot->stage = stage;
        slot->month = month;
        slot->seller = static_cast<uint16_t>(seller);
        slot->unk_0E = t2;
        slot->unk_0F = t3;
        slot->count = goodsRecordOf(static_cast<uint32_t>(group), static_cast<uint32_t>(index))->count;
    }

    // A music goods record puts its track (its value) in the first free music track entry (0xFFFF), unless one has it
    // already, and enables it in the BGM slot table.
    void Bag_AddMusicTrack(GuestPtr<Bag> bag, int group, int index)
    {
        const uint8_t track = goodsRecordOf(static_cast<uint32_t>(group), static_cast<uint32_t>(index))->value;
        for (const BagMusicTrack &music : bag->musicTracks)
            if (static_cast<uint16_t>(music.track) == track)
                return;
        for (BagMusicTrack &music : bag->musicTracks)
        {
            if (static_cast<uint16_t>(music.track) != 0xFFFFu)
                continue;
            music.track = track;
            music.enabled = 1;
            Bgm_SetTrackSlot(static_cast<char>(track), 1, 1);
            return;
        }
    }

    void Bag_Clear(GuestPtr<Bag> bag)
    {
        bag->size = 0;
        bag->outfit = 0;
        for (BagSlot &slot : bag->slots)
            slot.group = 0;
        for (BagMusicTrack &music : bag->musicTracks)
            music.track = -1;
    }

    // The slot whose goods record's name (offset) is `id`. Slots are read up to the bag's size, and past it while
    // they hold goods; 0 when none matches.
    int Bag_FindItem(GuestPtr<Bag> bag, uint32_t id)
    {
        const uint32_t end = slotAddress(bag, port::load<uint8_t>(globals::kBagSlotsBySize + bag->size));
        for (uint32_t slot = slotAddress(bag, 0);; slot += 16u)
        {
            const uint16_t group = GuestPtr<BagSlot>{slot}->group;
            if (group == 0 && !(slot < end))
                return 0;
            if (goodsRecordOf(group, GuestPtr<BagSlot>{slot}->index)->name == id)
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
            if (bag->size == 1 && bag->slots[0].group == 0)
                bag->size = 0;
            return;
        }
    }

    // The goods record of the slot being set up (setupSlot, -1 none): returns its value and writes how it is used for
    // categories 0x17 (0), 0x1A (1), 0x1B (2) and 0x1C (3).
    uint32_t BagMenu_GetSetupKind(GuestPtr<Bag> bag, GuestPtr<uint32_t> kind)
    {
        const int32_t setup = bag->setupSlot;
        if (setup == -1)
            return 0xFFFFFFFFu;
        const GuestPtr<BagSlot> slot{slotAddress(bag, static_cast<uint32_t>(setup))};
        const GuestPtr<GoodsRecord> record = goodsRecordOf(slot->group, slot->index);
        switch (record->kind)
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
        return record->value;
    }

    // The menu's texts for the slot under the cursor (cursor): the goods name (the record's, or the empty text) in the
    // text box nameText, and for usable goods the action in labels[0] with the usable flag (usable).
    void BagMenu_UpdateItemText(GuestPtr<Bag> bag)
    {
        const GuestPtr<BagSlot> slot{slotAddress(bag, static_cast<uint32_t>(bag->cursor))};
        const GuestPtr<TextBox> nameBox = bag->nameText;
        const uint16_t group = slot->group;
        if (group == 0)
        {
            TextBox_SetText(nameBox, GuestPtr<char>{0x001B5B10u}, 0);
            return;
        }
        const GuestPtr<GoodsRecord> record = goodsRecordOf(group, slot->index);
        TextBox_SetText(nameBox, GuestPtr<char>::of(record->text[0]), 0);
        uint32_t action;
        int32_t usable = 1;
        switch (record->kind)
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
        TextBox_SetText(bag->labels[0], GuestPtr<char>{action}, 0);
        bag->usable = usable;
    }

    // ---- goods records

    // The goods' price: its price digits (signed) times ten to the power of its exponent.
    int Goods_GetPrice(int item)
    {
        const GuestPtr<GoodsRecord> record{static_cast<uint32_t>(item)};
        const uint64_t exponent = sdk::d_utod(record->priceExponent);
        const int scale = sdk::dptoli(sdk::pow(0x4024000000000000ull, exponent)); // 10.0
        return static_cast<int>(static_cast<uint32_t>(record->priceDigits) * static_cast<uint32_t>(scale));
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
    // packets and texts (the item name and, with the usable flag, its description) and the closing widget are drawn.
    void BagMenu_Draw(GuestPtr<Bag> bag)
    {
        const uint32_t b = bag.address;
        const GuestPtr<A3DObject> frameModel{port::load<uint32_t>(0x001F3B08u)};
        A3DObject_UpdateMatrices(bag->model);
        int node = bag->size + 5;
        const uint32_t source = port::load<uint32_t>(port::load<uint32_t>(bag->model.address) + 8u) + 0x70u;
        if (node == 5)
            ++node;
        const uint32_t dest = port::load<uint32_t>(frameModel->model.address + 8u) + 0xC0u * static_cast<uint32_t>(node);
        for (uint32_t row = 0; row < 0x40; row += 0x10)
            *GuestPtr<Vec4>{dest + 0x70u + row} = *GuestPtr<Vec4>{source + row};
        A3DObject_DrawNode(frameModel, node);
        if (static_cast<uint32_t>(bag->state) >= 5)
            return;
        const auto packet = [&](uint32_t offset) {
            if (port::load<int32_t>(b + offset) != 0)
                Dma_AppendCall(port::load<int32_t>(b + offset));
        };
        const auto text = [&](uint32_t offset) {
            if (port::load<uint32_t>(b + offset) != 0)
                TextBox_Draw(GuestPtr<TextBox>{port::load<uint32_t>(b + offset)});
        };
        if (bag->list.address != 0)
        {
            DmaList_QueueClearZ();
            const uint32_t nodes = port::load<uint32_t>(port::load<uint32_t>(bag->model.address) + 8u) + 0x130u;
            Mat4_Copy(GuestPtr<Vec4>{nodes}, GuestPtr<Vec4>{globals::kIdentityMatrix});
            port::store<uint32_t>(nodes + 0x38u, 0xC08CCCCDu);
            A3DObject_DrawNode(bag->model, 1);
        }
        if (bag->list.address != 0)
            ListMenu_Draw(bag->list);
        text(0x170u);
        if (port::load<uint16_t>(b + 16u * static_cast<uint32_t>(bag->cursor) + 4u) != 0)
        {
            packet(0x184u);
            text(0x190u);
            if (bag->usable != 0)
            {
                packet(0x17Cu);
                text(0x188u);
            }
        }
        packet(0x180u);
        text(0x18Cu);
        const uint32_t closing = bag->question.address;
        if (closing != 0)
            port::callGuest<void>(widgetMethod(closing, &WidgetVtable::draw), closing, 2);
    }

    PORT_FN(BagMenu_Draw);

    // Adds a family to the applicants list (up to 32): state, family, the month, the home type of the family, and the
    // 0x4000 / 0x3F80 markers of an entry not yet placed. Returns the new entry's index, 0 when the list is full.
    int Applicants_Add(GuestPtr<Applicants> applicants, int family, int month, uint16_t state)
    {
        const int32_t index = applicants->count;
        if (!(index < 0x20))
            return 0;
        const GuestPtr<Family> record = Family_Get(family, 0);
        const GuestPtr<ApplicantEntry> entry = GuestPtr<ApplicantEntry>::of(applicants->entries[index]);
        entry->state = static_cast<int16_t>(state);
        entry->family = static_cast<uint16_t>(family);
        entry->month = month;
        entry->home = kNoObject;
        entry->type = record->homeType;
        entry->tile = 0x4000;
        entry->index = static_cast<uint16_t>(index);
        entry->timer = 0;
        applicants->count = applicants->count + 1;
        return index;
    }

    PORT_FN(Applicants_Add);

    // ---- The bag's item list (plan P6 batch 2) ----

    // Rebuilds the list menu (list) from the bag slots, up to the first empty one and within the slots its size gives: the goods'
    // name (the offset in the record, into the text at 0x1BDAA4), and for the goods of kinds 0x17, 0x1A, 0x1B and 0x1C that
    // stack, a count appended when there are two or more. The list is then updated against the selection (cursor) and
    // its result returned.
    int BagMenu_RefreshList(GuestPtr<Bag> bag)
    {
        port::GuestFrame<0x90> frame;
        const uint32_t line = frame.ptr().address + 0x50u;
        const auto list = [&] { return bag->list; };
        ListMenu_ClearItem(list(), -1);
        for (int i = 0; i < port::load<uint8_t>(0x001B5B08u + bag->size); ++i)
        {
            const GuestPtr<BagSlot> slot{bag.address + 4u + 0x10u * static_cast<uint32_t>(i)};
            if (slot->group == 0)
                break;
            const GuestPtr<GoodsRecord> record = goodsRecordOf(slot->group, slot->index);
            sdk::strcpy(line, globals::goodsSectionC.get() + record->name);
            const uint32_t kind = record->kind;
            if (kind == 0x17 || kind - 0x1Au < 2u || kind == 0x1C)
            {
                const uint32_t count = static_cast<uint8_t>(slot->count);
                if (static_cast<int32_t>(count) >= 2)
                    sdk::sprintf(line + static_cast<uint32_t>(sdk::strlen(line)), 0x001B5B48u, count);
            }
            ListMenu_SetItemText(list(), GuestPtr<char>{line}, -1);
        }
        return ListMenu_HandleInput(list(), GuestPtr<int32_t>{bag.address + 0x1A0u});
    }

    // Ends the use of the selected slot (state 7, the slide time, slide, cleared): when something was used up one is taken from
    // the slot's count, and an empty slot is removed; the selection is cleared and the list rebuilt.
    void BagMenu_FinishUse(GuestPtr<Bag> bag, int used)
    {
        const uint32_t b = bag.address;
        bag->state = 7;
        bag->slide = 0.0f;
        const auto count = [&] { return b + (static_cast<uint32_t>(bag->setupSlot) << 4) + 0xAu; };
        if (used != 0)
            port::store<uint8_t>(count(), static_cast<uint8_t>(port::load<uint8_t>(count()) - 1u));
        if (port::load<uint8_t>(count()) == 0)
            Bag_RemoveSlot(bag, GuestPtr<uint16_t>{count() - 6u});
        bag->setupSlot = -1;
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
        const GuestPtr<ApplicantEntry> entry = GuestPtr<ApplicantEntry>::of(applicants->entries[index]);
        const auto setState = [&](uint16_t state) { entry->state = static_cast<int16_t>(state); };
        const auto tick = [&] {
            const int32_t timer = entry->timer + 1;
            entry->timer = timer;
            return timer;
        };
        switch (static_cast<uint16_t>(entry->state))
        {
        case 1:
            if (tick() >= 5)
            {
                int active = 0;
                for (uint32_t i = 0; i < 0x20u; ++i)
                {
                    const uint32_t state = static_cast<uint16_t>(applicants->entries[i].state);
                    if (state >= 2 && state < 5)
                        ++active;
                }
                if (active < 3)
                {
                    setState(2);
                    entry->timer = 0;
                }
            }
            break;
        case 3:
            if (tick() >= 9)
            {
                const uint32_t family = entry->family;
                const uint32_t tile = entry->tile;
                const uint32_t homeType = Family_Get(static_cast<int>(family), 0)->homeType;
                Map_UnhighlightArea(city->map, static_cast<int>(tile & 0x7Fu), static_cast<int>(tile >> 7),
                                    globals::buildingType(homeType)->size);
                const int placed = Map_PlaceObject(city->map, static_cast<int>(family), static_cast<int>(homeType),
                                                   static_cast<int>(tile & 0x7Fu), static_cast<int>(tile >> 7), static_cast<int>(city.address));
                if (placed == 0x3F80)
                    setState(1);
                else
                {
                    entry->home = static_cast<uint16_t>(placed);
                    setState(4);
                }
            }
            break;
        case 4:
            if ((Map_GetRecord(city->map, entry->home)->flags & 0x20u) != 0)
                setState(5);
            break;
        case 5:
            Family_AssignHome(city->map, city, entry->family, entry->home);
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

    // Uses the goods of the selected slot (cursor) by their kind (byte 4 of the record): kinds 0x17, 0x1A, 0x1B and 0x1C start the
    // use (state 6, a slide time of 120.0, sound 0x10) and mark the slot as the one in use (setupSlot); kind 0x15 swaps
    // the player's outfit for the record's value, takes the slot out and puts the old outfit back into the bag (goods group 5 when
    // it was the default 0, otherwise group 0xE5 with index old - 1), then rebuilds the list; kinds 0x13 and 0x11 are eaten (mm_Stamina_Eat) and the slot
    // is taken out, the list rebuilt and sound 0xE played. Other kinds do nothing.
    void BagMenu_UseSelected(GuestPtr<Bag> bag, GuestPtr<CityScene> city)
    {
        const uint32_t b = bag.address;
        const GuestPtr<BagSlot> slot{b + (static_cast<uint32_t>(bag->cursor) << 4) + 4u};
        const GuestPtr<GoodsRecord> record = goodsRecordOf(slot->group, slot->index);
        switch (record->kind)
        {
        case 0x1C:
        case 0x1B:
        case 0x1A:
        case 0x17:
            bag->state = 6;
            bag->slide = std::bit_cast<float>(0x42F00000u);
            Sound_PlaySe(0x10, 0, 0x3C, ps2f(0.0f), ps2f(0.0f));
            bag->setupSlot = static_cast<int32_t>(static_cast<uint32_t>(bag->cursor));
            break;
        case 0x15:
        {
            const uint32_t previous = bag->outfit;
            bag->outfit = record->value;
            Bag_RemoveSlot(bag, GuestPtr<uint16_t>{b + (static_cast<uint32_t>(bag->cursor) << 4) + 4u});
            Bag_AddItem(bag, previous == 0 ? 5 : 0xE5, previous == 0 ? 0 : static_cast<int>(previous) - 1, static_cast<int16_t>(globals::stageState()->stage),
                        city->monthCount, 0, 0xFF, 0);
            BagMenu_RefreshList(bag);
            break;
        }
        case 0x13:
        case 0x11:
            Stamina_Eat(city->stamina, record);
            Bag_RemoveSlot(bag, GuestPtr<uint16_t>{b + (static_cast<uint32_t>(bag->cursor) << 4) + 4u});
            BagMenu_RefreshList(bag);
            Sound_PlaySe(0xE, 0, 0x3C, ps2f(0.0f), ps2f(0.0f));
            break;
        default:
            break;
        }
    }

    PORT_FN(BagMenu_UseSelected);

    // One frame of the applicants list (city modes 3 and 4). The card moves with a point (cardPoint: x, y, z) and a speed (step)
    // that doubles every frame; the states (mode): 0 closed: every 11 frames the next entry (of 32, assignIndex) is assigned a
    // building (mm_Applicants_AssignToBuilding), and in city mode 3 the list opens (state 1, speed 0.02, sound 7); 1 slides in
    // (the y falls by the speed to 0.21), then waits (2) and the HUD flags of the city (hudVisible[2], [3]) go off; 2 waits for
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
        const auto loadF = [](uint32_t at) { return ps2f(std::bit_cast<float>(port::load<uint32_t>(at))); };
        const auto storeF = [](uint32_t at, ps2f value) { port::store<uint32_t>(at, std::bit_cast<uint32_t>(value.v)); };
        const auto place = [&]() { Applicants_PlaceAt(applicants, GuestPtr<ps2f>{a + 0x330u}); };
        // The cursor stays among the entries; the family of the entry it lands on is prefetched.
        const auto moveCursor = [&](int32_t step) {
            applicants->cursor = applicants->cursor + step;
            const int32_t moved = applicants->cursor;
            int32_t clamped = 0;
            if (moved >= 0)
            {
                clamped = applicants->count - 1;
                if (!(clamped < moved))
                    clamped = moved;
            }
            applicants->cursor = clamped;
            const int32_t at = applicants->cursor;
            Family_Prefetch(applicants->entries[at].family, 0);
        };
        // The entry under the cursor, or 0 with none.
        const auto selected = [&]() { return applicants->count == 0 ? 0u : a + 20u * static_cast<uint32_t>(applicants->cursor); };
        int result = CityScene_GetMode(city);
        switch (static_cast<uint32_t>(applicants->mode))
        {
        case 0:
        {
            const int32_t timer = applicants->assignTimer + 1;
            applicants->assignTimer = timer;
            if (timer >= 11)
            {
                applicants->assignTimer = 0;
                Applicants_AssignToBuilding(applicants, city, applicants->assignIndex);
                const int32_t next = applicants->assignIndex + 1;
                applicants->assignIndex = next;
                if (!(next < 0x20))
                    applicants->assignIndex = 0;
            }
            if (result == 3)
            {
                applicants->mode = 1;
                applicants->step = std::bit_cast<float>(0x3CA3D70Au);
                Applicants_SetMode(applicants, 1);
                Sound_PlaySe(7, 0, 0x3C, 0.0f, 0.0f);
            }
            return result;
        }
        case 1:
        {
            const ps2f arrival(std::bit_cast<float>(0x3E570A3Du));
            applicants->cardPoint.y = applicants->cardPoint.y - applicants->step;
            applicants->step = applicants->step + applicants->step;
            if (applicants->cardPoint.y <= arrival)
            {
                applicants->mode = 2;
                applicants->cardPoint.y = arrival;
                city->hudVisible[2] = 0;
                city->hudVisible[3] = 0;
            }
            place();
            return result;
        }
        case 2:
            if ((Pad_Pressed(0) & 0x10u) != 0)
            {
                applicants->mode = 3;
                applicants->step = std::bit_cast<float>(0x3CA3D70Au);
                city->hudVisible[2] = 1;
                city->hudVisible[3] = 1;
                Sound_PlaySe(8, 0, 0x3C, 0.0f, 0.0f);
            }
            else if ((Pad_Pressed(0) & 0x40u) != 0)
            {
                const uint32_t kind = static_cast<uint16_t>(applicants->entries[applicants->cursor].state);
                if ((kind != 0 && kind < 4) || kind == 8)
                {
                    applicants->mode = 5;
                    applicants->step = std::bit_cast<float>(0x3CA3D70Au);
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
            applicants->cardPoint.y = applicants->cardPoint.y + applicants->step;
            applicants->step = applicants->step + applicants->step;
            if (!(applicants->cardPoint.y < out))
            {
                applicants->mode = 0;
                applicants->cardPoint.y = out;
                Applicants_SetMode(applicants, 0);
                result = 1;
            }
            place();
            return result;
        }
        case 4:
        {
            const GuestPtr<ApplicantEntry> entry{selected()};
            const int32_t kind = static_cast<uint16_t>(entry->state);
            if (kind == 1 || kind == 2)
            {
                const int type = Family_GetTypeIndex(entry->family);
                const GuestPtr<BuildingType> record{globals::buildingTypeAddress(static_cast<uint32_t>(type))};
                const uint8_t sort = record->rule;
                if (sort == 4)
                    Notice_Show(city->notice, 5);
                else if (sort == 0)
                    Notice_Show(city->notice, (record->flags & 0x10u) != 0 ? 6 : 2);
                else
                    Notice_Show(city->notice, 4);
            }
            else if (kind == 3)
                Notice_Show(city->notice, 3);
            return result;
        }
        case 5:
        {
            applicants->cardPoint.x = applicants->cardPoint.x - ps2f(0.8f) * applicants->step;
            applicants->cardPoint.y = applicants->cardPoint.y + ps2f(0.5f) * applicants->step;
            applicants->cardPoint.z = applicants->cardPoint.z + ps2f(2.0f) * applicants->step;
            applicants->step = applicants->step + applicants->step;
            if (!(applicants->cardPoint.y < ps2f(std::bit_cast<float>(0x3FE66666u))))
            {
                const GuestPtr<ApplicantEntry> entry{selected()};
                if (static_cast<uint16_t>(entry->state) == 3)
                {
                    const int type = Family_GetTypeIndex(entry->family);
                    const uint32_t position = entry->tile;
                    Map_UnhighlightArea(city->map, static_cast<int>(position & 0x7Fu), static_cast<int>(position >> 7),
                                        globals::buildingType(static_cast<uint32_t>(type))->size);
                }
                Player_SetMode(city->player, 2);
                result = 4;
                applicants->mode = 4;
                applicants->cardPoint.x = std::bit_cast<float>(0xC0400000u);
                applicants->cardPoint.y = std::bit_cast<float>(0x3FE66666u);
                applicants->cardPoint.z = std::bit_cast<float>(0x40E00000u);
            }
            place();
            return result;
        }
        case 6:
        {
            const ps2f arrival(std::bit_cast<float>(0x3E570A3Du));
            applicants->cardPoint.x = applicants->cardPoint.x + ps2f(0.8f) * applicants->step;
            applicants->cardPoint.y = applicants->cardPoint.y - ps2f(0.5f) * applicants->step;
            applicants->cardPoint.z = applicants->cardPoint.z - ps2f(2.0f) * applicants->step;
            applicants->step = applicants->step + applicants->step;
            if (applicants->cardPoint.y <= arrival)
            {
                Player_SetMode(city->player, -1);
                result = 3;
                applicants->mode = 2;
                applicants->cardPoint.x = 0.0f;
                applicants->cardPoint.y = arrival;
                applicants->cardPoint.z = 0.0f;
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
        // as the FPU's accumulator does), and the model's animation is set to that time.
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

    // One frame of the bag menu (city mode 11 and the modes around it). `time` (slide) counts the bag model's slide in 10s up to
    // 120; the states (state): 0 clears the HUD flags and the applicants' flag, plays sound 7 and starts the slide; 1 slides it in
    // (the model moves from 0x1AEF10 to 0x1AEF00), and at 120 builds the list menu (4, 10 or 20 items by the bag size), the text
    // boxes and frames, and goes to 2; 2 is the menu: the list takes the pad (the choice is cursor), the item text is updated, Cross
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
        const auto loadF = [](uint32_t at) { return ps2f(std::bit_cast<float>(port::load<uint32_t>(at))); };
        const auto storeF = [](uint32_t at, ps2f value) { port::store<uint32_t>(at, std::bit_cast<uint32_t>(value.v)); };
        const auto setState = [&](int32_t state) { bag->state = state; };
        const auto slotOf = [&]() { return b + 16u * static_cast<uint32_t>(bag->cursor); };
        const auto model = [&]() { return bag->model; };
        const auto hudBack = [&]() {
            city->hudVisible[1] = 1;
            city->hudVisible[2] = 1;
            city->applicants->enabled = 1;
        };
        switch (static_cast<uint32_t>(bag->state))
        {
        case 0:
            bag->slide = 0.0f;
            setState(1);
            city->hudVisible[1] = 0;
            city->hudVisible[2] = 0;
            city->applicants->enabled = 0;
            Sound_PlaySe(7, 0, 0x3C, 0.0f, 0.0f);
            break;
        case 1:
        {
            bag->slide = bag->slide + ps2f(10.0f);
            slideBag(b, bag->slide, 0x001AEF10u);
            A3D_Animate(model(), 0, bag->slide);
            if (bag->slide < ps2f(120.0f))
                break;
            setState(2);
            uint32_t list = sdk::operator_new(0x2A0u);
            if (list != 0u)
                list = ListMenu_ctor(GuestPtr<ListMenu>{list}, 6, 0x10, 0x127, 0x5A, 0x3C, 0xFF0000).address;
            bag->list = GuestPtr<ListMenu>{list};
            port::store<uint32_t>(list + 0x298u, 0u);
            port::store<uint32_t>(list + 0x294u, 0u);
            for (int i = 0; i < port::load<uint8_t>(globals::kBagSlotsBySize + bag->size); ++i)
                ListMenu_AddItems(bag->list, 0xC, 1, 0, -1, 0, -1);
            BagMenu_RefreshList(bag);
            bag->nameText = GuestPtr<TextBox>{makeTextBox(7, 8, -1)};
            TextBox_SetPos(bag->nameText, 0x8640, 0x7C20, 0xFF0000);
            bag->frames[0] = GuestPtr<Frame>{makeFrame(0x14, 0x1A4, 0x90, 0x18, 0xC, 0xFF0000u)};
            bag->frames[2] = GuestPtr<Frame>{makeFrame(0x15, 0x1A4, 0x9C, 0x18, 0xC, 0xFF0000u)};
            bag->frames[1] = GuestPtr<Frame>{makeFrame(0x17, 0x1A4, 0xA8, 0x18, 0xC, 0xFF0000u)};
            bag->labels[0] = GuestPtr<TextBox>{makeTextBox(4, 1, -1)};
            bag->labels[1] = GuestPtr<TextBox>{makeTextBox(4, 1, -1)};
            bag->labels[2] = GuestPtr<TextBox>{makeTextBox(4, 1, -1)};
            TextBox_SetPos(bag->labels[0], 0x8820, 0x8200, 0xFF0000);
            TextBox_SetPos(bag->labels[2], 0x8820, 0x82C0, 0xFF0000);
            TextBox_SetPos(bag->labels[1], 0x8820, 0x8380, 0xFF0000);
            TextBox_SetText(bag->labels[0], GuestPtr<char>{0x001B5B40u}, 0);
            TextBox_SetText(bag->labels[1], GuestPtr<char>{0x001B5B50u}, 0);
            TextBox_SetText(bag->labels[2], GuestPtr<char>{0x001B5B58u}, 0);
            break;
        }
        case 2:
            ListMenu_HandleInput(bag->list, GuestPtr<int32_t>{b + 0x1A0u});
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
                    bag->question = GuestPtr<MenuWin>{question};
                    MenuWin_Open(bag->question, 0, 2);
                    MenuWin_FillItems(bag->question, GuestPtr<char>{0x001B5B58u});
                    MenuWin_FillItems(bag->question, GuestPtr<char>{0x001B5B60u});
                }
            }
            else if ((Pad_Pressed(0) & 0x10u) != 0)
            {
                setState(3);
                bag->slide = std::bit_cast<float>(0x42F00000u);
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
            bag->slide = bag->slide - ps2f(10.0f);
            A3D_Animate(model(), 0, bag->slide);
            slideBag(b, bag->slide, 0x001AEF10u);
            if (bag->slide <= ps2f(0.0f))
            {
                setState(0);
                CityScene_PopMode(city);
                hudBack();
            }
            break;
        case 4:
        {
            const uint32_t question = bag->question.address;
            port::callGuest<void>(widgetMethod(question, &WidgetVtable::update), question);
            if (port::callGuest<int>(port::load<uint32_t>(bag->question->base.vtable.address + 0x24u), bag->question.address) == 0)
                break;
            const int32_t answer = port::load<int32_t>(bag->question.address + 0x18u);
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
            const GuestPtr<BagSlot> slot{slotOf() + 4u};
            const uint32_t item = slot->group;
            if (item != 0)
            {
                const uint32_t first = port::load<uint16_t>(globals::goodsSectionA.get() + 4u * item);
                const GuestPtr<GoodsRecord> record{globals::goodsSectionB.get() + 0x88u * (slot->index + first)};
                sdk::strcpy(buffer, globals::goodsSectionC.get() + record->name);
                const uint32_t kind = record->kind;
                if (kind == 0x17 || kind - 0x1Au < 2u || kind == 0x1C)
                {
                    const uint32_t count = static_cast<uint8_t>(GuestPtr<BagSlot>{slotOf() + 4u}->count);
                    if (static_cast<int32_t>(count) >= 2)
                        sdk::sprintf(buffer + static_cast<uint32_t>(sdk::strlen(buffer)), 0x001B5B48u, count);
                }
            }
            Notice_ShowText(city->notice, GuestPtr<char>{buffer});
            break;
        }
        case 6:
            bag->slide = bag->slide - ps2f(10.0f);
            slideBag(b, bag->slide, 0x001AEF20u);
            A3D_Animate(model(), 0, bag->slide);
            if (bag->slide <= ps2f(0.0f))
            {
                city->hudVisible[1] = 1;
                CityScene_PushMode(city, 0xB);
                Player_SetMode(city->player, 5);
                setState(5);
            }
            break;
        case 7:
            bag->slide = bag->slide + ps2f(10.0f);
            slideBag(b, bag->slide, 0x001AEF20u);
            A3D_Animate(model(), 0, bag->slide);
            if (bag->slide < ps2f(120.0f))
                break;
            Notice_Clear(city->notice);
            city->hudVisible[1] = 0;
            CityScene_PopMode(city);
            setState(2);
            break;
        default:
            break;
        }
    }

    PORT_FN(BagMenu_Update);

    // The lighting of the applicant card's claim icons as the cursor and the shown entry (top) move. Idle (rollDir
    // 0), a gap between them starts a step: forward (top < cursor: direction +1, the icon of the top entry lit on the eight nodes of the
    // table at 0x1B7A10, the cursor's on the first) or backward (direction -1, rollPhase at 40, the other way round). Moving, the
    // phase runs by twice the direction; at 20 the shown entry steps toward the cursor (by the direction when they are next to each
    // other, else to the entry beside it), the info text is rebuilt (mode 1) and sound 9 plays; past 40 or below 0 the step ends and
    // the shown entry's icon is lit on all the nodes. An icon's colour (mm_ClaimIcon_GetColor, level 1) is brightened by halving
    // its distance from 1 on each of red, green and blue.
    void Applicants_DrawDetails(GuestPtr<Applicants> applicants)
    {
        port::GuestFrame<0x90> frame;
        const uint32_t sp = frame.ptr().address;
        const auto loadF = [](uint32_t at) { return ps2f(std::bit_cast<float>(port::load<uint32_t>(at))); };
        const auto storeF = [](uint32_t at, ps2f value) { port::store<uint32_t>(at, std::bit_cast<uint32_t>(value.v)); };
        // The index in the field at `at` is kept among the entries (0 to count - 1).
        const auto clampIndex = [&](uint32_t at) {
            const int32_t index = port::load<int32_t>(at);
            int32_t clamped = 0;
            if (index >= 0)
            {
                clamped = applicants->count - 1;
                if (!(clamped < index))
                    clamped = index;
            }
            port::store<int32_t>(at, clamped);
        };
        const auto entryColour = [&](int32_t entry, uint32_t out) {
            const uint32_t icon = Claim_GetSupplies(applicants->claims[entry])->id;
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
        const auto model = [&]() { return applicants->frameModel; };
        const auto lightAll = [&](uint32_t colour) {
            for (uint32_t node = 0; node < 8; ++node)
                A3DObject_ChangeLuminosity(model(), GuestPtr<uint32_t>{colour}, port::load<uint8_t>(0x001B7A10u + node));
        };
        const auto lightFirst = [&](uint32_t colour) { A3DObject_ChangeLuminosity(model(), GuestPtr<uint32_t>{colour}, 0); };
        if (applicants->rollDir == ps2f(0.0f))
        {
            if (applicants->count == 0)
                return;
            const int32_t top = applicants->top, cursor = applicants->cursor;
            if (top < cursor)
            {
                applicants->rollDir = std::bit_cast<float>(0x3F800000u);
                clampIndex(port::guest(&applicants->cursor));
                clampIndex(port::guest(&applicants->top));
                entryColour(applicants->top, sp + 0x70u);
                lightAll(sp + 0x70u);
                entryColour(applicants->cursor, sp + 0x70u);
                lightFirst(sp + 0x70u);
                applicants->stateColour = 0xFF;
            }
            else if (cursor < top)
            {
                applicants->rollDir = std::bit_cast<float>(0xBF800000u);
                applicants->rollPhase = std::bit_cast<float>(0x42200000u);
                clampIndex(port::guest(&applicants->cursor));
                clampIndex(port::guest(&applicants->top));
                entryColour(applicants->top, sp + 0x80u);
                lightFirst(sp + 0x80u);
                entryColour(applicants->cursor, sp + 0x80u);
                lightAll(sp + 0x80u);
            }
            return;
        }
        if (applicants->rollPhase == ps2f(20.0f))
        {
            const int32_t top = applicants->top, cursor = applicants->cursor;
            if (std::abs(top - cursor) < 2)
                applicants->top = applicants->top + sdk::fptosi(applicants->rollDir);
            else if (cursor < top)
                applicants->top = cursor + 1;
            else
                applicants->top = cursor - 1;
            if (applicants->top < 0)
                applicants->top = 0;
            applicants->cardShown = 1;
            Applicants_BuildInfo(applicants);
            Sound_PlaySe(9, 0, 0x3C, 0.0f, 0.0f);
        }
        const ps2f phase = applicants->rollPhase + ps2f(2.0f) * applicants->rollDir;
        applicants->rollPhase = phase;
        if (phase < ps2f(40.0f) && !(phase < ps2f(0.0f)))
            return;
        applicants->rollPhase = 0.0f;
        applicants->rollDir = 0.0f;
        entryColour(applicants->top, sp + 0x60u);
        lightAll(sp + 0x60u);
        lightFirst(sp + 0x60u);
    }

    PORT_FN(Applicants_DrawDetails);

    // The applicants panel for one frame (nothing while enabled is 0). With mode 1 and entries: the details step (Applicants_
    // DrawDetails), the card model (frameModel) played at rollPhase and, once relight was set, lit again with the shown entry's
    // colour (the flag is cleared) on all of its nodes; then the model is drawn, a changed entry count (shownCount) scrolls to the top, and
    // the entries' icons are placed on the nodes of the icon model (cardModel): the node is the second of the table at 0x1B7A18 for
    // an entry of kind 4 to 7, else the first; the matrix at that node is the identity moved by the entry's place (the spacing is
    // 2.2 over the count, kept between 0.05 and 0.1; the shown entry sits 0.07 lower, the others 0.001 a place of distance from
    // the cursor, less 0.03) and the node is drawn with the entry's brightened claim colour. Past the entries the marker (cardNodeB,
    // when stateColour is not 0xFF) is placed beside the card (a depth from the table at 0x1B7A20) and drawn on node 2. With no entries
    // a white mesh 4 of the icon model is drawn instead. Whenever the mode is not 0 and sliding is set, the layout runs and the
    // shown entry's icon is drawn on node 0; the card text (cardText) shows when cardShown is set and there are entries.
    void Applicants_Draw(GuestPtr<Applicants> applicants)
    {
        port::GuestFrame<0xB0> frame;
        const uint32_t sp = frame.ptr().address;
        const auto loadF = [](uint32_t at) { return ps2f(std::bit_cast<float>(port::load<uint32_t>(at))); };
        const auto storeF = [](uint32_t at, ps2f value) { port::store<uint32_t>(at, std::bit_cast<uint32_t>(value.v)); };
        const auto constant = [](uint32_t bits) { return ps2f(std::bit_cast<float>(bits)); };
        const auto cardModel = [applicants]() { return applicants->frameModel; };
        const auto iconModel = [applicants]() { return applicants->cardModel; };
        // The claim colour of the entry (the icon of the first item its home supplies, level 1) with each of red, green and
        // blue brought halfway to 1 from where it is, and 12 in the fourth place.
        const auto entryColour = [&](int32_t entry, uint32_t out) {
            const uint32_t icon = Claim_GetSupplies(applicants->claims[entry])->id;
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
        const uint8_t mode = applicants->enabled;
        if (mode == 0)
            return;
        if (mode == 1)
        {
            DmaList_QueueClearZ();
            const int32_t count = applicants->count;
            if (count > 0)
            {
                Applicants_DrawDetails(applicants);
                A3D_Animate(cardModel(), 0, applicants->rollPhase);
                const int32_t relight = applicants->relight;
                applicants->relight = 0;
                if (relight != 0)
                {
                    entryColour(applicants->top, sp + 0xA0u);
                    A3DObject_ChangeLuminosity(cardModel(), GuestPtr<uint32_t>{sp + 0xA0u}, 0);
                    for (uint32_t node = 0; node < 8; ++node)
                        A3DObject_ChangeLuminosity(cardModel(), GuestPtr<uint32_t>{sp + 0xA0u}, port::load<uint8_t>(0x001B7A10u + node));
                }
                A3DObject_Draw(cardModel());
                const int32_t shown = applicants->count;
                const int32_t before = applicants->shownCount;
                applicants->shownCount = shown;
                if (shown != before)
                    Applicants_ScrollTo(applicants, -1);
                ps2f spacing = constant(0x400CCCCDu) / ps2f(static_cast<float>(applicants->count));
                if (spacing < constant(0x3D4CCCCDu))
                    spacing = constant(0x3D4CCCCDu);
                else if (!(spacing <= constant(0x3DCCCCCDu)))
                    spacing = constant(0x3DCCCCCDu);
                for (int32_t i = 0; i < applicants->count; ++i)
                {
                    if (applicants->sliding != 0 && i == applicants->top)
                        continue;
                    const uint32_t kind = static_cast<uint16_t>(applicants->entries[i].state);
                    const uint32_t slot = kind >= 4u ? (kind < 8u ? 1u : 0u) : 0u;
                    const uint32_t nodeAt = 0x001B7A18u + slot;
                    const uint32_t node = port::load<uint8_t>(nodeAt);
                    const uint32_t matrix = port::load<uint32_t>(port::load<uint32_t>(iconModel().address) + 8u) + 0xC0u * node + 0x70u;
                    Mat4_Copy(GuestPtr<Vec4>{matrix}, GuestPtr<Vec4>{0x001B2550u});
                    const ps2f place = ps2f(static_cast<float>(i)) * spacing;
                    storeF(matrix + 0x30u, (loadF(applicants->cardMatrix.address + 0x30u) + place) - constant(0x3F866666u));
                    storeF(matrix + 0x34u, constant(0xBF3AE148u) + loadF(applicants->cardMatrix.address + 0x34u));
                    const int32_t cursor = applicants->sliding != 0 ? applicants->savedTop : applicants->cursor;
                    if (cursor == i)
                        storeF(matrix + 0x38u, loadF(applicants->cardMatrix.address + 0x38u) - constant(0x3D8F5C29u));
                    else
                    {
                        const int32_t distance = std::abs(cursor - i);
                        storeF(matrix + 0x38u, (loadF(applicants->cardMatrix.address + 0x38u) + constant(0x3A83126Fu) * ps2f(static_cast<float>(distance))) - constant(0x3CF5C28Fu));
                    }
                    entryColour(i, sp + 0x90u);
                    Instance_DrawNode(iconModel(), GuestPtr<Vec4>{sp + 0x90u}, static_cast<int>(node), 0);
                }
                const uint8_t marker = applicants->stateColour;
                if (marker != 0xFF)
                {
                    const uint32_t markerMatrix = applicants->cardNodeB.address;
                    Mat4_Copy(GuestPtr<Vec4>{markerMatrix}, GuestPtr<Vec4>{0x001B2550u});
                    storeF(applicants->cardNodeB.address + 0x30u, loadF(applicants->frameMatrix.address + 0x30u) + loadF(0x001B7A20u + 4u * marker));
                    port::store<uint32_t>(applicants->cardNodeB.address + 0x34u, port::load<uint32_t>(applicants->frameMatrix.address + 0x34u));
                    port::store<uint32_t>(applicants->cardNodeB.address + 0x38u, port::load<uint32_t>(applicants->frameMatrix.address + 0x38u));
                    Instance_DrawNode(iconModel(), GuestPtr<Vec4>{sp + 0x90u}, 2, 0);
                }
            }
            else
            {
                port::store<uint32_t>(sp + 0x90u, 0x437F0000u);
                port::store<uint32_t>(sp + 0x94u, 0x437F0000u);
                port::store<uint32_t>(sp + 0x98u, 0x437F0000u);
                port::store<int32_t>(sp + 0x9Cu, 12);
                Instance_DrawMesh(iconModel(), GuestPtr<Vec4>{applicants->frameMatrix.address}, GuestPtr<Vec4>{sp + 0x90u}, 4, 0);
            }
        }
        if (applicants->sliding != 0)
        {
            Applicants_Layout(applicants);
            DmaList_QueueClearZ();
            entryColour(applicants->top, sp + 0x90u);
            Instance_DrawNode(iconModel(), GuestPtr<Vec4>{sp + 0x90u}, 0, 0);
        }
        if (applicants->cardShown != 0 && applicants->count != 0)
            TextBox_Draw(applicants->cardText);
    }

    PORT_FN(Applicants_Draw);
}
