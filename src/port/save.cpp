// Saving and loading: the open save file of the memory card manager, and the subsystems that read their
// state from it.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

namespace mm
{
    namespace
    {
        int queueOf(GuestPtr<McManager> mc) { return static_cast<int>(mc->queue.address); }

        template <typename T>
        int addressOf(GuestPtr<T> p) { return static_cast<int>(p.address); }
    }

    int SaveFile_GetResult(GuestPtr<McManager> mc, int request) { return McQueue_GetResult(queueOf(mc), request); }
    int SaveFile_IsDone(GuestPtr<McManager> mc, int request) { return McQueue_IsDone(queueOf(mc), request); }
    int SaveFile_Close(GuestPtr<McManager> mc) { return McQueue_Close(queueOf(mc), mc->fd, GuestPtr<int32_t>{0}); }

    int SaveFile_Read(GuestPtr<McManager> mc, int buffer, int size)
    {
        return McQueue_Read(queueOf(mc), mc->fd, buffer, size, GuestPtr<int32_t>{0});
    }

    // Opens the current save file (its card port and name are kept at 0x1BDB94/0x1BDB98) with `mode`; the
    // descriptor arrives in mc->fd.
    int SaveFile_Open(GuestPtr<McManager> mc, int mode)
    {
        return McQueue_Open(queueOf(mc), port::load<int32_t>(0x001BDB94u), port::load<int32_t>(0x001BDB98u), mode,
                            GuestPtr<int32_t>::of(mc->fd));
    }

    int McManagerSub_ctor(int queue) { return queue; }

    // Whether any of the 16 save slots holds a save (a positive size and a name).
    int McManager_HasAnySave()
    {
        for (uint32_t i = 0; i < 16; ++i)
            if (port::load<int32_t>(globals::kSaveSlotSizes + 4u * i) >= 1 && port::load<char>(globals::kSaveSlotNames + 0x44u * i) != '\0')
                return 1;
        return 0;
    }

    // The header of the index-th save found over both cards (8 slots per card), and that save's card.
    int McManager_FindSlotA(GuestPtr<McManager>, int index, GuestPtr<int32_t> card)
    {
        int found = 0;
        for (int slot = 0; slot < 16; ++slot)
        {
            const uint32_t s = static_cast<uint32_t>(slot);
            if (port::load<int32_t>(globals::kSaveSlotSizes + 4u * s) < 1 || port::load<char>(globals::kSaveSlotNames + 0x44u * s) == '\0')
                continue;
            if (found == index)
            {
                *card = slot / 8;
                return static_cast<int>(globals::kSaveSlotHeaders + 0x40u * s);
            }
            ++found;
        }
        return 0;
    }

    // The save's title for the memory card browser: the prefix, then the city name in full-width characters
    // (anything outside ASCII 0x20..0x7D as a stand-in) or a default when it has none; cut at 0x3C bytes,
    // followed by the month count, approval, population and stage as little-endian bytes.
    void McManager_BuildSaveHeader(GuestPtr<McManager>, GuestPtr<CityScene> city, GuestPtr<char> out)
    {
        sdk::strcpy(out.address, globals::kSaveTitlePrefix);
        if (port::load<int8_t>(globals::kStageState) == 0)
            sdk::strcat(out.address, globals::kSaveTitleDefault);
        else
        {
            uint32_t dst = out.address + static_cast<uint32_t>(sdk::strlen(out.address));
            for (uint32_t src = globals::kStageState; port::load<int8_t>(src) != 0; ++src, dst += 2u)
            {
                const int8_t c = port::load<int8_t>(src);
                const uint32_t glyph = c < 0x20 || c >= 0x7E ? globals::kFullWidthUnknown
                                                             : globals::kFullWidthAscii + 2u * static_cast<uint32_t>(c - 0x20);
                sdk::memcpy(dst, glyph, 2);
            }
            port::store<uint8_t>(dst, 0);
        }
        const auto putShort = [&](uint32_t offset, int32_t value) {
            out[offset] = static_cast<char>(value);
            out[offset + 1] = static_cast<char>(value / 256);
        };
        out[0x3C] = 0;
        putShort(0x3D, city->monthCount);
        out[0x3F] = static_cast<char>(city->approval);
        putShort(0x40, city->map->population);
        putShort(0x42, globals::stageState()->stage);
    }

    // The header of the save named `name` on a card (slots 0-7 for card 0, 8-15 for card 1), or 0.
    int McManager_FindSave(GuestPtr<McManager>, GuestPtr<char> name, int card)
    {
        const uint32_t first = card != 0 ? 8u : 0u, end = card != 0 ? 16u : 8u;
        for (uint32_t slot = first; slot < end; ++slot)
        {
            const uint32_t header = globals::kSaveSlotHeaders + 0x40u * slot;
            if (port::load<int32_t>(globals::kSaveSlotSizes + 4u * slot) >= 1 && port::load<char>(globals::kSaveSlotNames + 0x44u * slot) != 0 &&
                sdk::strcmp(header + 0x20u, name.address) == 0)
                return static_cast<int>(header);
        }
        return 0;
    }

    // Card state for the menus (provisional meanings): 4 no card or it changed since the last check,
    // 2 not a PS2 memory card, 3 unformatted, else 1 when less than 0x3EE units are free and 0 when there is room.
    uint8_t McManager_GetCardStatus(GuestPtr<McManager>, int card)
    {
        const uint32_t c = 4u * static_cast<uint32_t>(card);
        const int32_t type = port::load<int32_t>(globals::kCardType + c);
        if (type != port::load<int32_t>(globals::kCardTypeChecked + c))
            return 4;
        const int32_t result = port::load<int32_t>(globals::kCardResult + c);
        if (result == -1 || result == -2)
            return 4;
        if (type != 2)
            return 2;
        if (port::load<int32_t>(globals::kCardFormatted + c) == 0)
            return 3;
        return port::load<int32_t>(globals::kCardFree + c) < 0x3EE;
    }

    // Forgets both cards and every save slot.
    void McManager_InitSlots()
    {
        for (uint32_t i = 0; i < 2; ++i)
        {
            port::store<int32_t>(globals::kCardTypeChecked + 4u * i, -1);
            port::store<int32_t>(globals::kCardResult + 4u * i, -4);
        }
        for (uint32_t slot = 0; slot < 16; ++slot)
        {
            port::store<int32_t>(globals::kSaveSlotSizes + 4u * slot, -4);
            port::store<char>(globals::kSaveSlotNames + 0x44u * slot, '\0');
        }
    }

    void McManager_ClearSlots(GuestPtr<McManager> mc)
    {
        McManager_InitSlots();
        mc->unk_08 = 0;
        for (uint32_t i = 0; i < 2; ++i)
        {
            port::store<int32_t>(globals::kCardType + 4u * i, 0);
            port::store<int32_t>(globals::kCardFree + 4u * i, 0);
            port::store<int32_t>(globals::kCardFormatted + 4u * i, 0);
        }
    }

    // ---- state of each subsystem in the save
    void Bag_LoadState(GuestPtr<Bag> bag, GuestPtr<McManager> mc) { SaveFile_Read(mc, addressOf(bag), 0x16C); }
    void Wants_LoadState(GuestPtr<WantsWin> wants, GuestPtr<McManager> mc) { SaveFile_Read(mc, addressOf(wants), 0x504); }
    void Applicants_LoadState(GuestPtr<Applicants> applicants, GuestPtr<McManager> mc) { SaveFile_Read(mc, addressOf(applicants), 0x284); }
    void Player_LoadState(GuestPtr<Player> player, GuestPtr<McManager> mc) { SaveFile_Read(mc, addressOf(player), 0x38); }
    void Stamina_LoadState(GuestPtr<Stamina> stamina, GuestPtr<McManager> mc) { SaveFile_Read(mc, addressOf(stamina), 0xC); }
    void Stage_LoadState(int, GuestPtr<McManager> mc) { SaveFile_Read(mc, static_cast<int>(globals::kStageState), 0x284); }

    void Laptop_LoadState(GuestPtr<Laptop>, GuestPtr<McManager> mc)
    {
        SaveFile_Read(mc, static_cast<int>(globals::laptopEntryCount.address), 4);
        SaveFile_Read(mc, static_cast<int>(globals::kLaptopEntries), 0x38);
    }

    void TileLists_LoadState(GuestPtr<TileLists> lists, GuestPtr<McManager> mc)
    {
        SaveFile_Read(mc, addressOf(lists), 0x7F00);
        SaveFile_Read(mc, static_cast<int>(GuestPtr<uint16_t>::of(lists->start[0]).address), 0xA0);
        SaveFile_Read(mc, static_cast<int>(GuestPtr<uint16_t>::of(lists->count[0]).address), 0xA0);
    }

    // The map: packed tiles, the 0x20 bytes at +0x10, the objects, the tile lists, the people and the three
    // words from +0x6C (population among them).
    void Map_LoadState(GuestPtr<CityMap> map, GuestPtr<McManager> mc)
    {
        SaveFile_Read(mc, static_cast<int>(map->packedTiles.address), 0x18000);
        SaveFile_Read(mc, static_cast<int>(map.address + 0x10u), 0x20);
        SaveFile_Read(mc, static_cast<int>(map->objects.address), 0x6F200);
        TileLists_LoadState(map->tileLists, mc);
        SaveFile_Read(mc, static_cast<int>(map->people.address), 0x46500);
        SaveFile_Read(mc, static_cast<int>(map.address + 0x6Cu), 0xC);
    }

    // Families: member bytes, the three bitsets, the member status entries and the three counters.
    void Family_LoadState(GuestPtr<McManager> mc)
    {
        SaveFile_Read(mc, static_cast<int>(globals::kMemberBytes), 60000);
        SaveFile_Read(mc, static_cast<int>(globals::kFamiliesInCity), 0x4E4);
        SaveFile_Read(mc, static_cast<int>(globals::kFamiliesKnown), 0x4E4);
        SaveFile_Read(mc, static_cast<int>(globals::kFamiliesLeft), 0x4E4);
        SaveFile_Read(mc, static_cast<int>(globals::kMemberStatus), 0x400);
        SaveFile_Read(mc, static_cast<int>(globals::familiesMovedIn.address), 0xC);
    }

    void CityCamera_LoadState(GuestPtr<CityCamera> camera, GuestPtr<McManager> mc)
    {
        SaveFile_Read(mc, static_cast<int>(GuestPtr<int32_t>::of(camera->zoomLevel).address), 4);
        SaveFile_Read(mc, static_cast<int>(GuestPtr<Vec4>::of(camera->base.eye).address), 0x1D4);
    }

    PORT_FN(SaveFile_GetResult);
    PORT_FN(SaveFile_IsDone);
    PORT_FN(SaveFile_Close);
    PORT_FN(SaveFile_Read);
    PORT_FN(SaveFile_Open);
    PORT_FN(McManagerSub_ctor);
    PORT_FN(McManager_HasAnySave);
    PORT_FN(McManager_FindSlotA);
    PORT_FN(McManager_FindSave);
    PORT_FN(McManager_BuildSaveHeader);
    PORT_FN(McManager_GetCardStatus);
    PORT_FN(McManager_InitSlots);
    PORT_FN(McManager_ClearSlots);
    PORT_FN(Bag_LoadState);
    PORT_FN(Wants_LoadState);
    PORT_FN(Applicants_LoadState);
    PORT_FN(Player_LoadState);
    PORT_FN(Stamina_LoadState);
    PORT_FN(Stage_LoadState);
    PORT_FN(Laptop_LoadState);
    PORT_FN(CityCamera_LoadState);
    PORT_FN(TileLists_LoadState);
    PORT_FN(Family_LoadState);
    PORT_FN(Map_LoadState);
}
