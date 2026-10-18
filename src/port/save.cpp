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
}
