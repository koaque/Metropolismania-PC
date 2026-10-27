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
        mc->scanState = 0;
        for (uint32_t i = 0; i < 2; ++i)
        {
            port::store<int32_t>(globals::kCardType + 4u * i, 0);
            port::store<int32_t>(globals::kCardFree + 4u * i, 0);
            port::store<int32_t>(globals::kCardFormatted + 4u * i, 0);
        }
    }

    // The memory card scan, one step a frame (+0x08). 0 to 5: card info for both ports, asked a second time
    // when a PS2 card is unformatted (a port reporting a new card is marked unchecked). 6: a port whose card
    // is new or of another type forgets its 8 slots; if a port is unchecked and a PS2 card is in, the slot
    // scan starts, else the types count as checked. 10 to 13: each port with a PS2 card goes to its root.
    // 20 to 30: for the slot being filled (8 per port) the next directory entry whose name starts with the
    // save folder prefix; in that folder the file of the same name must be 0xE67E8 bytes and icon.sys 0x3C4,
    // and then the icon.sys title, the entry, the entry count and the folder name fill the slot. 50 to 52:
    // back to the root. 100: the next slot, or once all 16 are done 200 and 201: card info again; when no
    // card changed meanwhile the types count as checked, else it starts over at 2. A port without a PS2
    // card or out of entries moves on to the next port's slots. Returns 0 when idle, 2 from opening a save
    // folder until back at the root (states 23 to 99), else 1. The name check uses a 32-byte buffer at the
    // original's sp + 0x20.
    int McManagerUpdate(GuestPtr<McManager> mc)
    {
        port::GuestFrame<0x40> frame;
        const uint32_t nameBuffer = frame.ptr().address + 0x20u;
        const int queue = queueOf(mc);
        const GuestPtr<int32_t> type{globals::kCardType}, formatted{globals::kCardFormatted}, result{globals::kCardResult};
        const GuestPtr<int32_t> checked{globals::kCardTypeChecked}, scanned{globals::kCardTypeScanned};
        const GuestPtr<int32_t> none{0};
        const int entryName = static_cast<int>(globals::kScanDirEntry + 0x20u);
        int32_t &state = mc->scanState;
        const auto cardInfo = [&] {
            for (uint32_t p = 0; p < 2; ++p)
                mc->request = McQueue_GetInfo(queue, static_cast<int>(p), static_cast<int>(globals::kCardType + 4u * p),
                                              static_cast<int>(globals::kCardFree + 4u * p), static_cast<int>(globals::kCardFormatted + 4u * p),
                                              result + static_cast<int32_t>(p));
        };
        const auto done = [&] { return McQueue_IsDone(queue, mc->request) != 0; };
        const auto lastResult = [&] { return McQueue_GetResult(queue, mc->request); };
        const auto cardPort = [&] { return mc->scanSlot / 8; };
        const auto isNewCard = [](int32_t r) { return r == -1 || r == -2; };
        const auto forgetSlots = [&](uint32_t p) {
            for (uint32_t slot = 8u * p; slot < 8u * p + 8u; ++slot)
                port::store<char>(globals::kSaveSlotNames + 0x44u * slot, '\0');
            for (uint32_t slot = 8u * p; slot < 8u * p + 8u; ++slot)
                port::store<int32_t>(globals::kSaveSlotSizes + 4u * slot, -4);
            checked[static_cast<int32_t>(p)] = -1;
        };
        const auto nextPort = [&] {
            if (mc->scanSlot < 8)
            {
                mc->scanSlot = 8;
                mc->dirIndex = 0;
                return false;
            }
            state = 100;
            mc->scanSlot = 16;
            return true;
        };

        switch (state)
        {
        case 0:
        case 3:
        case 200:
            cardInfo();
            ++state;
            break;
        case 1:
        case 4:
        case 11:
        case 50:
            if (done())
                ++state;
            break;
        case 2:
        case 5:
            if ((type[0] == 2 && formatted[0] == 0) || (type[1] == 2 && formatted[1] == 0))
            {
                if (isNewCard(result[0]))
                    checked[0] = -1;
                if (isNewCard(result[1]))
                    checked[1] = -1;
                ++state;
            }
            else
                state = 6;
            break;
        case 6:
            if (isNewCard(result[0]) || type[0] != checked[0])
                forgetSlots(0);
            if (isNewCard(result[1]) || type[1] != checked[1])
                forgetSlots(1);
            if (checked[0] != -1 && checked[1] != -1)
                state = 0;
            else if (type[0] == 2 || type[1] == 2)
            {
                state = 10;
                mc->scanSlot = 0;
                mc->dirIndex = 0;
            }
            else
            {
                checked[0] = type[0];
                checked[1] = type[1];
                state = 0;
            }
            break;
        case 10:
            if (type[0] != 2)
            {
                state = 12;
                break;
            }
            mc->request = McQueue_Chdir(queue, 0, static_cast<int>(globals::kParentDirName), 0, none);
            ++state;
            break;
        case 12:
            if (type[1] != 2)
            {
                state = 20;
                break;
            }
            mc->request = McQueue_Chdir(queue, 1, static_cast<int>(globals::kParentDirName), 0, none);
            ++state;
            break;
        case 13:
            if (done())
                state = 20;
            break;
        case 20:
            if (type[cardPort()] == 2)
            {
                const int entry = mc->dirIndex;
                mc->dirIndex = entry + 1;
                mc->request = McQueue_GetDir(queue, cardPort(), static_cast<int>(globals::kAnyEntryName), entry, 1,
                                             static_cast<int>(globals::kScanDirEntry), GuestPtr<int32_t>{globals::cardDirEntries.address});
                ++state;
            }
            else if (mc->scanSlot == 0)
            {
                mc->scanSlot = 8;
                mc->dirIndex = 0;
            }
            else
            {
                state = 100;
                mc->scanSlot = 16;
            }
            break;
        case 21:
            if (!done())
                break;
            if (globals::cardDirEntries.get() <= 0)
            {
                if (!nextPort())
                    --state;
                break;
            }
            sdk::strcpy(nameBuffer, static_cast<uint32_t>(entryName));
            port::store<uint8_t>(nameBuffer + static_cast<uint32_t>(sdk::strlen(globals::saveFolderPrefix.get())), 0);
            if (sdk::strcmp(nameBuffer, globals::saveFolderPrefix.get()) == 0)
                ++state;
            else
                --state;
            break;
        case 22:
            mc->request = McQueue_Chdir(queue, cardPort(), entryName, 0, none);
            ++state;
            break;
        case 23:
        case 26:
            if (!done())
                break;
            if (lastResult() < 0)
                state = 51;
            else
            {
                const int file = state == 23 ? entryName : static_cast<int>(globals::kIconSysName);
                mc->request = McQueue_Open(queue, cardPort(), file, 1, GuestPtr<int32_t>::of(mc->fd));
                ++state;
            }
            break;
        case 24:
        case 27:
            if (!done())
                break;
            if (mc->fd < 0)
                state = 51;
            else
            {
                mc->request = McQueue_Seek(queue, mc->fd, 0, 2);
                ++state;
            }
            break;
        case 25:
            if (!done())
                break;
            if (lastResult() == 0xE67E8)
                ++state;
            else
                state = 50;
            mc->request = McQueue_Close(queue, mc->fd, none);
            break;
        case 28:
            if (!done())
                break;
            if (lastResult() == 0x3C4)
            {
                mc->request = McQueue_Seek(queue, mc->fd, 0, 0);
                ++state;
            }
            else
            {
                mc->request = McQueue_Close(queue, mc->fd, none);
                state = 50;
            }
            break;
        case 29:
            if (!done())
                break;
            mc->request = McQueue_Read(queue, mc->fd, static_cast<int>(globals::kIconSysBuffer), 0x3C4, none);
            ++state;
            break;
        case 30:
            if (!done())
                break;
            if (lastResult() >= 0)
            {
                const auto slot = static_cast<uint32_t>(mc->scanSlot);
                sdk::memcpy(globals::kSaveSlotNames + 0x44u * slot, globals::kIconSysBuffer + 0xC0u, 0x44);
                const uint32_t header = globals::kSaveSlotHeaders + 0x40u * slot;
                for (uint32_t i = 0; i < 0x40u; ++i)
                    port::store<uint8_t>(header + i, port::load<uint8_t>(globals::kScanDirEntry + i));
                port::store<int32_t>(globals::kSaveSlotSizes + 4u * slot, globals::cardDirEntries.get());
                sdk::strcpy(globals::kSaveSlotFolders + 0x20u * slot, header + 0x20u);
                ++mc->scanSlot;
            }
            mc->request = McQueue_Close(queue, mc->fd, none);
            state = 50;
            break;
        case 51:
            mc->request = McQueue_Chdir(queue, cardPort(), static_cast<int>(globals::kParentDirName), 0, none);
            ++state;
            break;
        case 52:
            if (done())
                state = 100;
            break;
        case 100:
            if (mc->scanSlot < 16)
                state = 20;
            else
            {
                state = 200;
                scanned[0] = type[0];
                scanned[1] = type[1];
            }
            break;
        case 201:
            if (!done())
                break;
            if (scanned[0] == type[0] && !isNewCard(result[0]) && scanned[1] == type[1] && !isNewCard(result[1]))
            {
                state = 0;
                checked[0] = type[0];
                checked[1] = type[1];
            }
            else
                state = 2;
            break;
        default:
            break;
        }
        if (state == 0)
            return 0;
        return state < 23 || !(state < 100) ? 1 : 2;
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
    PORT_FN(McManagerUpdate);
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

    // ---- save file requests (batch 38)

    // Writes a block to the open save file (a queued request without a result word).
    int SaveFile_Write(GuestPtr<McManager> mc, int buffer, int size)
    {
        return McQueue_Write(static_cast<int>(mc->queue.address), mc->fd, buffer, size, GuestPtr<int32_t>{0});
    }

    int SaveFile_PollDone(GuestPtr<McManager> mc) { return McQueue_IsDone(static_cast<int>(mc->queue.address), mc->request); }

    void SaveFile_ResetState(GuestPtr<McManager> mc) { mc->state = 0; }

    // Deletes icon.sys from the directory; both card types are to be checked again (-1).
    void SaveFile_DeleteIconSys(GuestPtr<McManager> mc, int dir)
    {
        mc->request = McQueue_Delete(static_cast<int>(mc->queue.address), dir, static_cast<int>(globals::kIconSysName));
        port::store<int32_t>(globals::kCardTypeChecked, -1);
        port::store<int32_t>(globals::kCardTypeChecked + 4u, -1);
    }

    // Formatting in two steps: state 0 queues it (returning 0), state 1 polls: 0 while busy, then 1 for success or 2
    // when the result word (0x1BDBA8) is set.
    int SaveFile_Format(GuestPtr<McManager> mc, int port)
    {
        if (mc->state == 0)
        {
            mc->request = McQueue_Format(static_cast<int>(mc->queue.address), port, GuestPtr<int32_t>{0x001BDBA8u});
            ++mc->state;
            return 0;
        }
        if (mc->state != 1 || McQueue_IsDone(static_cast<int>(mc->queue.address), mc->request) == 0)
            return 0;
        mc->state = 0;
        return port::load<int32_t>(0x001BDBA8u) != 0 ? 2 : 1;
    }

    PORT_FN(SaveFile_Write);
    PORT_FN(SaveFile_PollDone);
    PORT_FN(SaveFile_ResetState);
    PORT_FN(SaveFile_DeleteIconSys);
    PORT_FN(SaveFile_Format);

    // ---- member status, memory card slots (batch 38)

    namespace
    {
        uint8_t statusHigh(uint8_t status) { return static_cast<uint8_t>((status >> 4) & 0xFu); }
    }

    uint8_t MemberStatus_GetLow2(int family, int member) { return *MemberStatus_Lookup(family, member) & 3u; }
    uint32_t MemberStatus_GetMid2(int family, int member) { return (*MemberStatus_Lookup(family, member) >> 2) & 3u; }
    int MemberStatus_IsLevelAbove13(int family, int member) { return statusHigh(*MemberStatus_Lookup(family, member)) >= 14 ? 1 : 0; }

    // Advances the low fields (each up to 2; the middle pair only once the level is 2 or more), then the level
    // (high nibble) starts over.
    void MemberStatus_Advance(int family, int member)
    {
        const GuestPtr<uint8_t> status = MemberStatus_Lookup(family, member);
        if (statusHigh(*status) >= 2)
        {
            const uint32_t mid = (*status >> 2) & 3u;
            if (mid < 2)
                *status = static_cast<uint8_t>((*status & 0xF3u) | (((mid + 1u) & 3u) << 2));
        }
        const uint32_t low = *status & 3u;
        if (low < 2)
            *status = static_cast<uint8_t>((*status & 0xFCu) | ((low + 1u) & 3u));
        *status = static_cast<uint8_t>(*status & 0x0Fu);
    }

    // From level 12 the member gains friendship points (mm_Family_AddFriendship); below it the result is the status
    // byte address (what the original leaves in v0).
    int MemberStatus_OnLevel12(int family, int member, int points, int approval)
    {
        const GuestPtr<uint8_t> status = MemberStatus_Lookup(family, member);
        if (statusHigh(*status) < 12)
            return static_cast<int>(status.address);
        port::setGuestV0(static_cast<int32_t>(status.address)); // Family_AddFriendship leaves v0 as it finds it
        return Family_AddFriendship(family, member, GuestPtr<int32_t>{static_cast<uint32_t>(approval)}, points);
    }

    // Counts the member bytes (all 60000, friend flag included) by band: 2 to 24, 25 to 48, 49 to 100, 101 and up.
    void MemberMeters_Reset(GuestPtr<int32_t> meters)
    {
        for (int i = 0; i < 4; ++i)
            meters[i] = 0;
        for (uint32_t i = 0; i < 60000u; ++i)
        {
            const uint8_t value = port::load<uint8_t>(globals::kMemberBytes + i);
            if (value >= 101)
                ++meters[3];
            else if (value >= 49)
                ++meters[2];
            else if (value >= 25)
                ++meters[1];
            else if (value >= 2)
                ++meters[0];
        }
    }

    // The save slot number `index` among those with entries and a title (16 slots, 8 per card): its folder name,
    // and its card.
    int McManager_FindSlotB(GuestPtr<McManager>, int index, GuestPtr<int32_t> card)
    {
        int found = 0;
        for (int i = 0; i < 16; ++i)
        {
            if (port::load<int32_t>(globals::kSaveSlotSizes + 4u * static_cast<uint32_t>(i)) <= 0)
                continue;
            if (port::load<int8_t>(globals::kSaveSlotNames + 0x44u * static_cast<uint32_t>(i)) == 0)
                continue;
            if (found != index)
            {
                ++found;
                continue;
            }
            *card = i / 8;
            return static_cast<int>(globals::kSaveSlotFolders + 0x20u * static_cast<uint32_t>(i));
        }
        return 0;
    }

    // A card is usable when it is a PS2 memory card (type 2) and its word at kCardFormatted is 0.
    int McManager_IsCardUsable(GuestPtr<McManager>, int card)
    {
        if (port::load<int32_t>(globals::kCardType + 4u * static_cast<uint32_t>(card)) != 2)
            return 0;
        return port::load<int32_t>(globals::kCardFormatted + 4u * static_cast<uint32_t>(card)) != 0 ? 0 : 1;
    }

    PORT_FN(MemberStatus_GetLow2);
    PORT_FN(MemberStatus_GetMid2);
    PORT_FN(MemberStatus_IsLevelAbove13);
    PORT_FN(MemberStatus_Advance);
    PORT_FN(MemberStatus_OnLevel12);
    PORT_FN(MemberMeters_Reset);
    PORT_FN(McManager_FindSlotB);
    PORT_FN(McManager_IsCardUsable);

    // ---- member status table (batch 39)

    // The status byte of a family member in the table at 0x1F32F0 (256 entries: family, member, status). A member
    // without an entry takes the first free one (family 0). With the table full, the entry of the highest level
    // (ties: the lower middle field, then the lower low field; the first on a full tie) is reused with its status
    // reset to level 14 and both fields 0; its family and member stay as they were.
    GuestPtr<uint8_t> MemberStatus_Lookup(int family, int member)
    {
        const auto entry = [](uint32_t i) { return globals::kMemberStatus + 4u * i; };
        for (uint32_t i = 0; i < 256; ++i)
            if (static_cast<int>(port::load<uint16_t>(entry(i))) == family && static_cast<int>(port::load<uint8_t>(entry(i) + 2u)) == member)
                return GuestPtr<uint8_t>{entry(i) + 3u};
        for (uint32_t i = 0; i < 256; ++i)
        {
            if (port::load<uint16_t>(entry(i)) != 0)
                continue;
            port::store<uint16_t>(entry(i), static_cast<uint16_t>(family));
            port::store<uint8_t>(entry(i) + 2u, static_cast<uint8_t>(member));
            return GuestPtr<uint8_t>{entry(i) + 3u};
        }
        uint32_t worst = 0;
        for (uint32_t i = 1; i < 256; ++i)
        {
            const uint8_t mine = port::load<uint8_t>(entry(i) + 3u), theirs = port::load<uint8_t>(entry(worst) + 3u);
            if ((theirs >> 4) < (mine >> 4))
                worst = i;
            else if ((theirs >> 4) == (mine >> 4))
            {
                if (((mine >> 2) & 3u) < ((theirs >> 2) & 3u))
                    worst = i;
                else if (((mine >> 2) & 3u) == ((theirs >> 2) & 3u) && (mine & 3u) < (theirs & 3u))
                    worst = i;
            }
        }
        const uint32_t status = entry(worst) + 3u;
        port::store<uint8_t>(status, 0xE0);
        return GuestPtr<uint8_t>{status};
    }

    PORT_FN(MemberStatus_Lookup);

    // ---- Memory card manager lifetime (plan P6 batch 1) ----

    int McManagerSub_DeletingDtor(int queue, int16_t deleteFlag)
    {
        if (queue != 0 && 0 < deleteFlag)
            sdk::operator_delete(static_cast<uint32_t>(queue));
        return queue;
    }

    // Creates the request queue (1 byte) and the save file object (async 0), sets the slot tables up and clears the scan
    // state and the card type, free space and formatted flag of both ports.
    GuestPtr<McManager> McManager_ctor(GuestPtr<McManager> mc)
    {
        const uint32_t queueMemory = sdk::operator_new(1u);
        uint32_t queue = queueMemory;
        if (queueMemory != 0u)
            queue = static_cast<uint32_t>(McManagerSub_ctor(static_cast<int>(queueMemory)));
        mc->queue = GuestPtr<void>{queue};
        const uint32_t fileMemory = sdk::operator_new(8u);
        uint32_t file = fileMemory;
        if (fileMemory != 0u)
            file = File_ctor(GuestPtr<GameFile>{fileMemory}, 0).address;
        mc->file = GuestPtr<GameFile>{file};
        McManager_InitSlots();
        mc->scanState = 0;
        // Card type, free space and formatted flag of port 0, then of port 1.
        for (const uint32_t word : {globals::kCardType, globals::kCardFree, globals::kCardFormatted, globals::kCardType + 4u,
                                    globals::kCardFree + 4u, globals::kCardFormatted + 4u})
            port::store<uint32_t>(word, 0u);
        return mc;
    }

    PORT_FN(McManagerSub_DeletingDtor);
    PORT_FN(McManager_ctor);

    // ---- Preparing the save directory on a card (plan P6 batch 2) ----

    // Gets the save directory of card `port` ready, a step per call (the state is mc->state): ask for the card's type (2 is a PS2
    // card), change into the directory `name`, and when it is missing and `allowCreate` is set make it, change into it and write
    // the save's icon file (DATA\STATIC.ICO, 0x13968 bytes) into it. Returns 1 when done, 0 while working and 2 when the card
    // is not a PS2 card or the directory is missing and may not be made.
    int SaveFile_PrepareDir(GuestPtr<McManager> mc, int portNumber, int name, int allowCreate)
    {
        constexpr uint32_t kCardType = 0x001BDB60u, kCardFree = 0x001BDB68u, kCardFormat = 0x001BDB70u;
        constexpr uint32_t kKnownType0 = 0x001BDB78u, kKnownType1 = 0x001BDB7Cu;
        port::store<int32_t>(0x001BDB94u, portNumber);
        port::store<int32_t>(0x001BDB98u, name);
        const uint32_t m = mc.address;
        const int queue = static_cast<int>(port::load<uint32_t>(m));
        const uint32_t at = 4u * static_cast<uint32_t>(portNumber);
        const GuestPtr<int32_t> result{m + 0x18u};
        const auto advance = [&] { mc->state = mc->state + 1; };
        const auto isDone = [&] { return McQueue_IsDone(queue, mc->request) != 0; };
        switch (static_cast<uint32_t>(mc->state))
        {
        case 0:
        case 5:
            mc->request = McQueue_GetInfo(queue, portNumber, static_cast<int>(kCardType + at), static_cast<int>(kCardFree + at),
                                          static_cast<int>(kCardFormat + at), GuestPtr<int32_t>{0u});
            advance();
            break;
        case 1:
        case 6:
            if (isDone())
                advance();
            break;
        case 2:
            if (port::load<int32_t>(kCardType + at) != 2)
            {
                mc->state = 0;
                return 2;
            }
            advance();
            break;
        case 3:
            mc->request = McQueue_Chdir(queue, portNumber, name, 0, result);
            advance();
            break;
        case 4:
            if (isDone())
            {
                if (*result == 0)
                    mc->state = 0;
                else
                {
                    if (allowCreate == 0)
                    {
                        mc->state = 0;
                        return 2;
                    }
                    advance();
                }
            }
            break;
        case 7:
            if (port::load<int32_t>(kCardType) != port::load<int32_t>(kKnownType0))
                port::store<int32_t>(kKnownType0, -1);
            if (port::load<int32_t>(kCardType + 4u) != port::load<int32_t>(kKnownType1))
                port::store<int32_t>(kKnownType1, -1);
            if (port::load<int32_t>(kCardType + at) != 2)
            {
                mc->state = 0;
                return 2;
            }
            mc->request = McQueue_Mkdir(queue, portNumber, name);
            mc->request = McQueue_Chdir(queue, portNumber, name, 0, result);
            advance();
            break;
        case 8:
            if (isDone())
            {
                if (*result != 0)
                {
                    mc->state = 0;
                    return 2;
                }
                advance();
            }
            break;
        case 9:
            port::store<uint32_t>(m + 0x1Cu, sdk::malloc(0x13968u));
            mc->request = File_Open(mc->file, GuestPtr<char>{0x001B7E30u});
            mc->request = File_Read(mc->file, static_cast<int>(port::load<uint32_t>(m + 0x1Cu)), 1, 0x13968, 0);
            advance();
            break;
        case 10:
            if (FileQueue_IsDone(static_cast<int>(mc->file.address), mc->request) != 0)
                advance();
            break;
        case 11:
            mc->request = McQueue_Open(queue, portNumber, 0x001B7E40, 0x202, GuestPtr<int32_t>{m + 0x24u});
            advance();
            [[fallthrough]];
        case 12:
            if (isDone())
                advance();
            break;
        case 13:
            mc->request = McQueue_Write(queue, mc->fd, static_cast<int>(port::load<uint32_t>(m + 0x1Cu)), 0x13968, GuestPtr<int32_t>{0u});
            mc->request = McQueue_Close(queue, mc->fd, GuestPtr<int32_t>{0u});
            advance();
            break;
        case 14:
            if (isDone())
                advance();
            break;
        case 15:
            mc->request = File_Close(mc->file);
            advance();
            break;
        case 16:
            if (FileQueue_IsDone(static_cast<int>(mc->file.address), mc->request) != 0)
            {
                if (port::load<uint32_t>(m + 0x1Cu) != 0u)
                {
                    sdk::free(port::load<uint32_t>(m + 0x1Cu));
                    port::store<uint32_t>(m + 0x1Cu, 0u);
                }
                mc->state = 0;
            }
            break;
        default:
            break;
        }
        return mc->state != 0 ? 0 : 1;
    }

    PORT_FN(SaveFile_PrepareDir);

    // ---- The save slot's line in the load menu (plan P6 batch 2) ----

    namespace
    {
        // The padded number text uses the Shift-JIS full-width digits; the leading full-width zeros (0x82 0x4F) are meant to
        // become full-width spaces (0x81 0x40), at most `pairs` of them. The original compares the signed byte with +0x82, which
        // it can never equal, so the loop stops at once and the zeros stay; the port keeps the comparison as it is.
        void blankLeadingZeros(uint32_t text, int pairs)
        {
            for (int i = 0; i < pairs; ++i)
            {
                const uint32_t at = text + 2u * static_cast<uint32_t>(i);
                if (static_cast<int32_t>(port::load<int8_t>(at)) != 0x82 || port::load<int8_t>(at + 1u) != 0x4F)
                    break;
                port::store<uint8_t>(at, 0x81);
                port::store<uint8_t>(at + 1u, 0x40);
            }
        }
    }

    // Appends the line for save slot `slot` (0x44-byte records from 0x1F4340; none when the slot is empty) to `out`: a header,
    // the day (+0x42, shown from 1 and wrapped at 100) in two digits, the name in the game's own letter table (halfword
    // characters at the offset past the prefix at 0x1B7C50 mapped to ASCII-like codes by their place in the table at 0x1B7C70),
    // a four-digit number (+0x40 mod 10000), a signed three-digit number (+0x3F) and, with `marked`, a trailing mark.
    void McManager_FormatSlotLine(GuestPtr<McManager>, int slot, GuestPtr<char> out, int marked)
    {
        port::GuestFrame<0x70> frame;
        const uint32_t number = frame.ptr().address + 0x60u;
        const uint32_t record = 0x001F4340u + 0x44u * static_cast<uint32_t>(slot);
        if (port::load<int8_t>(record) == 0)
            return;
        const auto append = [&](uint32_t format) {
            sdk::sprintf(out.address + static_cast<uint32_t>(sdk::strlen(out.address)), format, number);
        };
        sdk::strcat(out.address, 0x001B7DA0u);
        const int32_t day = static_cast<int32_t>(port::load<int8_t>(record + 0x42u)) + (static_cast<int32_t>(port::load<int8_t>(record + 0x43u)) << 8);
        Text_FormatPadded(GuestPtr<char>{number}, remainder(day + 1, 100), 2);
        blankLeadingZeros(number, 1);
        append(0x001B7DA8u);
        uint32_t dst = out.address + static_cast<uint32_t>(sdk::strlen(out.address));
        const uint32_t nameStart = record + static_cast<uint32_t>(sdk::strlen(0x001B7C50u));
        for (uint32_t src = nameStart; port::load<int8_t>(src) != 0; src += 2u)
        {
            uint32_t code = 0;
            for (uint32_t table = 0x001B7C70u; port::load<int8_t>(table) != 0 && port::load<uint16_t>(src) != port::load<uint16_t>(table); table += 2u)
                ++code;
            port::store<uint8_t>(dst, static_cast<uint8_t>(code + 0x20u));
            ++dst;
        }
        port::store<uint8_t>(dst, 0);
        append(0x001B7DB0u);
        const int32_t value = static_cast<int32_t>(port::load<uint8_t>(record + 0x40u)) + (static_cast<int32_t>(port::load<int8_t>(record + 0x41u)) << 8);
        Text_FormatPadded(GuestPtr<char>{number}, remainder(value, 10000), 4);
        blankLeadingZeros(number, 3);
        append(0x001B7DC0u);
        const int32_t third = port::load<int8_t>(record + 0x3Fu);
        if (third < 0)
        {
            const int32_t magnitude = -third;
            Text_FormatPadded(GuestPtr<char>{number}, magnitude, 3);
            blankLeadingZeros(number, 2);
            port::store<uint8_t>(number + (magnitude < 10 ? 1u : 0u), '-');
        }
        else
        {
            Text_FormatPadded(GuestPtr<char>{number}, third, 3);
            blankLeadingZeros(number, 2);
        }
        append(0x001B7DD0u);
        if (marked != 0)
            sdk::strcat(out.address, 0x001B7DE0u);
    }

    PORT_FN(McManager_FormatSlotLine);

    // Makes a name for a new save on card slot `card` (32-byte names from 0x1F3CF0): the game's id string (pointer at
    // 0x1BD9A0) followed by eight digits of the console clock in seconds (the day of the month and the month taken in
    // 31-day units), or of the timer mix when the clock cannot be read. The original then compares the name with the other
    // slots' of the same card group and returns without doing anything about a match, so the port leaves that out.
    void McManager_NewSaveName(GuestPtr<McManager>, uint32_t card)
    {
        port::GuestFrame<0x70> frame;
        const uint32_t clock = frame.ptr().address + 0x68u;
        uint32_t value;
        if (sdk::sceCdReadClock(clock) == 0)
            value = static_cast<uint32_t>(Random_TimerMix());
        else
        {
            const auto decimal = [&](uint32_t offset) {
                const uint32_t bcd = port::load<uint8_t>(clock + offset);
                return (bcd & 0xFu) + (bcd >> 4) * 10u;
            };
            value = decimal(6) * 0x28DE80u + decimal(5) * 0x15180u + decimal(3) * 0xE10u + decimal(2) * 0x3Cu + decimal(1);
        }
        const uint32_t name = 0x001F3CF0u + (card & 0xFFu) * 0x20u;
        const uint32_t prefix = port::load<uint32_t>(0x001BD9A0u);
        sdk::strcpy(name, prefix);
        const uint32_t length = static_cast<uint32_t>(sdk::strlen(prefix));
        for (uint32_t digit = length + 7u; digit >= length; --digit)
        {
            port::store<uint8_t>(name + digit, static_cast<uint8_t>('0' + value % 10u));
            value /= 10u;
        }
        port::store<uint8_t>(name + length + 8u, 0);
    }

    PORT_FN(McManager_NewSaveName);

    // The text of the `index`th used save slot (a slot is used when its card state at 0x1F4780 is positive and its record is not
    // empty), for the load screen's description: a header, the day, the month and year counters (+0x3D as a 16-bit count of
    // months), the name, the four-digit number and the signed three-digit number, with their separators. Empty `out` when there
    // is no such slot. The records are the ones McManager_FormatSlotLine reads.
    void McManager_FormatSaveInfo(GuestPtr<McManager>, int index, GuestPtr<char> out)
    {
        port::GuestFrame<0x60> frame;
        const uint32_t number = frame.ptr().address + 0x50u;
        int found = -1;
        int used = 0;
        for (int slot = 0; slot < 0x10; ++slot)
        {
            if (0 < port::load<int32_t>(0x001F4780u + 4u * static_cast<uint32_t>(slot)) && port::load<int8_t>(0x001F4340u + 0x44u * static_cast<uint32_t>(slot)) != 0)
            {
                if (used == index)
                {
                    found = slot;
                    break;
                }
                ++used;
            }
        }
        if (found == -1)
        {
            port::store<uint8_t>(out.address, 0);
            return;
        }
        const uint32_t record = 0x001F4340u + 0x44u * static_cast<uint32_t>(found);
        const auto append = [&](uint32_t format) {
            sdk::sprintf(out.address + static_cast<uint32_t>(sdk::strlen(out.address)), format, number);
        };
        sdk::strcpy(out.address, 0x001B7D40u);
        const int32_t day = static_cast<int32_t>(port::load<int8_t>(record + 0x42u)) + (static_cast<int32_t>(port::load<int8_t>(record + 0x43u)) << 8);
        Text_FormatPadded(GuestPtr<char>{number}, remainder(day + 1, 100), 2);
        blankLeadingZeros(number, 1);
        sdk::strcat(out.address, number);
        sdk::strcat(out.address, 0x001B7D48u);
        const int32_t months = static_cast<int32_t>(port::load<uint8_t>(record + 0x3Du)) + (static_cast<int32_t>(port::load<int8_t>(record + 0x3Eu)) << 8);
        Text_FormatPadded(GuestPtr<char>{number}, remainder(months, 12) + 1, 2);
        blankLeadingZeros(number, 1);
        append(0x001B7D50u);
        Text_FormatPadded(GuestPtr<char>{number}, remainder(divide(months, 12), 1000) + 1, 3);
        blankLeadingZeros(number, 2);
        append(0x001B7D60u);
        uint32_t dst = out.address + static_cast<uint32_t>(sdk::strlen(out.address));
        const uint32_t nameStart = record + static_cast<uint32_t>(sdk::strlen(0x001B7C50u));
        for (uint32_t src = nameStart; port::load<int8_t>(src) != 0; src += 2u)
        {
            uint32_t code = 0;
            for (uint32_t table = 0x001B7C70u; port::load<int8_t>(table) != 0 && port::load<uint16_t>(src) != port::load<uint16_t>(table); table += 2u)
                ++code;
            port::store<uint8_t>(dst, static_cast<uint8_t>(code + 0x20u));
            ++dst;
        }
        port::store<uint8_t>(dst, 0);
        sdk::strcat(out.address, 0x001B7D70u);
        const int32_t value = static_cast<int32_t>(port::load<uint8_t>(record + 0x40u)) + (static_cast<int32_t>(port::load<int8_t>(record + 0x41u)) << 8);
        Text_FormatPadded(GuestPtr<char>{number}, remainder(value, 10000), 4);
        blankLeadingZeros(number, 3);
        sdk::strcat(out.address, number);
        sdk::strcat(out.address, 0x001B7D80u);
        const int32_t third = port::load<int8_t>(record + 0x3Fu);
        if (third < 0)
        {
            const int32_t magnitude = -third;
            Text_FormatPadded(GuestPtr<char>{number}, magnitude, 3);
            blankLeadingZeros(number, 2);
            port::store<uint8_t>(number + (magnitude < 10 ? 1u : 0u), '-');
        }
        else
        {
            Text_FormatPadded(GuestPtr<char>{number}, third, 3);
            blankLeadingZeros(number, 2);
        }
        sdk::strcat(out.address, number);
        sdk::strcat(out.address, 0x001B7D98u);
    }

    PORT_FN(McManager_FormatSaveInfo);

    // The first free slot of the card (8 per card; a slot is free when its state at 0x1F4780 is not positive) gets a new name
    // (McManager_NewSaveName) and its record address (0x20-byte records from 0x1F3CF0) is the result; 0 when all are used.
    int McManager_PrepareNewSlot(GuestPtr<McManager> mc, int card)
    {
        for (int i = 0; i < 8; ++i)
        {
            if (port::load<int32_t>(0x001F4780u + 4u * static_cast<uint32_t>(card * 8 + i)) <= 0)
            {
                McManager_NewSaveName(mc, static_cast<uint32_t>(i & 0xFF));
                return 0x001F3CF0 + i * 0x20;
            }
        }
        return 0;
    }

    PORT_FN(McManager_PrepareNewSlot);

    // Starts writing the icon and the save description file (mm_McSave_WriteIconSys on the manager's queue with the three arguments
    // given): the request is kept at +0x14 and the two state words at 0x1BDB78 and 0x1BDB7C go back to -1.
    void SaveFile_WriteIconSys(GuestPtr<McManager> mc, int a1, int a2, int a3)
    {
        mc->request = McSave_WriteIconSys(static_cast<int>(mc->queue.address), a1, a2, a3);
        port::store<int32_t>(0x001BDB78u, -1);
        port::store<int32_t>(0x001BDB7Cu, -1);
    }

    PORT_FN(SaveFile_WriteIconSys);

    // The load screen's slot list, one slot per call. The newest of the 16 slots is found first (a slot counts when its size is
    // above 0 and it has a name; the saved time is compared by year, month, day, hour, minute, second). The slot at the cursor is
    // then taken, or the next one that counts (the cursor wraps to 0 with nothing written when none is left): its saved time
    // (a clock as the BIOS keeps it: second, minute, hour, day, month and year at +9..+0xE of its header, each a plain number)
    // is turned into the console's local time (sceScfGetLocalTimefromRTC) and written as "Slot %d: %s %s%s, %s. %shrs %smins"
    // (the card number, the month name, the day with its ordinal suffix, the year, the hours and the minutes). The day suffix is
    // "th" except for the days ending in 1, 2 and 3, where 11 and 12 (but not 13) keep "th". *isNewest tells whether this is the
    // newest slot, and the short line for the slot is made as well (mm_McManager_FormatSlotLine). The cursor moves on (wrapping at
    // 16) and is the result.
    int McManager_FormatSlotText(GuestPtr<McManager> mc, GuestPtr<int32_t> cursor, GuestPtr<char> text, GuestPtr<uint32_t> isNewest)
    {
        port::GuestFrame<0xD0> frame;
        const uint32_t sp = frame.ptr().address;
        int newest = -1;
        for (int slot = 0; slot < 0x10; ++slot)
        {
            const uint32_t index = static_cast<uint32_t>(slot);
            if (port::load<int32_t>(globals::kSaveSlotSizes + 4u * index) <= 0 || port::load<int8_t>(globals::kSaveSlotNames + 0x44u * index) == 0)
                continue;
            if (newest == -1)
            {
                newest = slot;
                continue;
            }
            const uint32_t best = globals::kSaveSlotHeaders + 0x40u * static_cast<uint32_t>(newest);
            const uint32_t current = globals::kSaveSlotHeaders + 0x40u * index;
            if (port::load<uint16_t>(best + 0xEu) != port::load<uint16_t>(current + 0xEu))
            {
                if (port::load<uint16_t>(best + 0xEu) < port::load<uint16_t>(current + 0xEu))
                    newest = slot;
                continue;
            }
            for (uint32_t at = 0xD; at >= 9; --at)
            {
                const uint8_t was = port::load<uint8_t>(best + at), now = port::load<uint8_t>(current + at);
                if (was != now)
                {
                    if (was < now)
                        newest = slot;
                    break;
                }
            }
        }
        int32_t slot;
        for (;;)
        {
            slot = *cursor;
            if (port::load<int32_t>(globals::kSaveSlotSizes + 4u * static_cast<uint32_t>(slot)) > 0 &&
                port::load<int8_t>(globals::kSaveSlotNames + 0x44u * static_cast<uint32_t>(slot)) != 0)
                break;
            *cursor = *cursor + 1;
            if (*cursor >= 0x10)
            {
                *cursor = 0;
                return *cursor;
            }
        }
        const uint32_t header = globals::kSaveSlotHeaders + 0x40u * static_cast<uint32_t>(slot);
        port::store<uint64_t>(sp + 0xB0u, port::load<uint64_t>(0x001BDBA0u));
        const auto toBcd = [](uint32_t value) { return static_cast<uint8_t>((value / 10u) * 16u + value % 10u); };
        const auto fromBcd = [](uint8_t value) { return static_cast<int32_t>(value & 0xFu) + static_cast<int32_t>(value >> 4) * 10; };
        port::store<uint8_t>(sp + 0xB1u, toBcd(port::load<uint8_t>(header + 9u)));
        port::store<uint8_t>(sp + 0xB2u, toBcd(port::load<uint8_t>(header + 0xAu)));
        port::store<uint8_t>(sp + 0xB3u, toBcd(port::load<uint8_t>(header + 0xBu)));
        port::store<uint8_t>(sp + 0xB5u, toBcd(port::load<uint8_t>(header + 0xCu)));
        port::store<uint8_t>(sp + 0xB6u, toBcd(port::load<uint8_t>(header + 0xDu)));
        port::store<uint8_t>(sp + 0xB7u, toBcd(port::load<uint16_t>(header + 0xEu) % 100u));
        sdk::sceScfGetLocalTimefromRTC(sp + 0xB0u);
        const int32_t year = 2000 + fromBcd(port::load<uint8_t>(sp + 0xB7u));
        const int32_t day = fromBcd(port::load<uint8_t>(sp + 0xB5u));
        Text_FormatPadded(GuestPtr<char>{sp + 0xB8u}, year, 4);
        Text_FormatPadded(GuestPtr<char>{sp + 0xC4u}, day, 2);
        Text_FormatPadded(GuestPtr<char>{sp + 0xC8u}, fromBcd(port::load<uint8_t>(sp + 0xB3u)), 2);
        Text_FormatPadded(GuestPtr<char>{sp + 0xCCu}, fromBcd(port::load<uint8_t>(sp + 0xB2u)), 2);
        uint32_t suffix = 0;
        switch (day % 10)
        {
        case 1:
            suffix = day == 11 ? 0u : 1u;
            break;
        case 2:
            suffix = day == 12 ? 0u : 2u;
            break;
        case 3:
            suffix = 3;
            break;
        default:
            break;
        }
        const int32_t month = fromBcd(port::load<uint8_t>(sp + 0xB6u));
        sdk::sprintf(text.address, 0x001B7EA0u, *cursor < 8 ? 1 : 2, port::load<uint32_t>(0x001B0B8Cu + 4u * static_cast<uint32_t>(month)),
                     sp + 0xC4u, port::load<uint32_t>(0x001B0BC0u + 4u * suffix), sp + 0xB8u, sp + 0xC8u, sp + 0xCCu);
        *isNewest = *cursor == newest ? 1u : 0u;
        McManager_FormatSlotLine(mc, *cursor, text, *cursor == newest ? 1 : 0);
        *cursor = *cursor + 1;
        if (*cursor >= 0x10)
            *cursor = 0;
        return *cursor;
    }

    PORT_FN(McManager_FormatSlotText);

    // Writes icon.sys of a save folder on the memory card: the PS2 icon description (the "PS2D" header, the title split at the line
    // break position, the background transparency, three light directions, three light colours and the ambient colour, the title
    // (0x44 bytes, `title`) and the three icon file names) is built in the static buffer at 0x1F65F0 (0x3C4 bytes) and queued as three
    // requests on the card queue: open icon.sys for writing (port `port`; the file number comes back at 0x1BDCDC), then, when that is
    // done (the queue is pumped meanwhile, with the original's atanf pause as a checkpoint), write the buffer and close the file with its result at
    // `result`. The result is the queue position after the close.
    int McSave_WriteIconSys(int queue, int port, int title, int result)
    {
        constexpr uint32_t kImage = 0x001F65F0u, kFileNumber = 0x001BDCDCu;
        sdk::memset(kImage, 0, 0x3C4u);
        sdk::strcpy(kImage, 0x001B9C60u);
        port::store<uint16_t>(kImage + 6u, static_cast<uint16_t>(sdk::strlen(0x001B9C40u)));
        port::store<uint32_t>(kImage + 0xCu, 0x80u);
        // Light directions (0x1F6640), light colours (0x1F6670) and the ambient colour (0x1F66A0), four floats each.
        static constexpr uint32_t kLights[] = {
            0x00000000u, 0x3FC00000u, 0x3F000000u, 0x3F800000u, 0x3FC00000u, 0xBF000000u, 0x3F000000u, 0x3F800000u,
            0xBFC00000u, 0x3F000000u, 0x3F000000u, 0x3F800000u, 0x3F333333u, 0x3F333333u, 0x3F333333u, 0x3F800000u,
            0x3E4CCCCDu, 0x3E4CCCCDu, 0x3E4CCCCDu, 0x3F800000u, 0x3E4CCCCDu, 0x3E4CCCCDu, 0x3E4CCCCDu, 0x3F800000u,
            0x3EE66666u, 0x3EE66666u, 0x3EE66666u, 0x3F800000u};
        for (uint32_t i = 0; i < 28; ++i)
            port::store<uint32_t>(kImage + 0x50u + 4u * i, kLights[i]);
        sdk::memcpy(kImage + 0xC0u, static_cast<uint32_t>(title), 0x44u);
        sdk::strcpy(kImage + 0x104u, 0x001B9C68u);
        sdk::strcpy(kImage + 0x144u, 0x001B9C68u);
        sdk::strcpy(kImage + 0x184u, 0x001B9C68u);
        const int opened = McQueue_Open(0, port, 0x001B9C78, 0x203, GuestPtr<int32_t>{kFileNumber});
        while (McQueue_IsDone(queue, opened) == 0)
        {
            McQueue_Pump();
            port::checkpoint();
        }
        const int fd = port::load<int32_t>(kFileNumber);
        McQueue_Write(0, fd, static_cast<int>(kImage), 0x3C4, GuestPtr<int32_t>{0});
        McQueue_Close(0, fd, GuestPtr<int32_t>{static_cast<uint32_t>(result)});
        return globals::mcQueueTail.get();
    }

    PORT_FN(McSave_WriteIconSys);
}
