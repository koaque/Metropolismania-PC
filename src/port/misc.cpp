// Small functions of subsystems that have no other ported code yet: base objects, the player and camera
// matrices, files, fonts, claims, memory card queue results, events and the stage state.

#include "game_functions.h"
#include "game_globals.h"

namespace mm
{
    // core: constructors of the common base object and of two-byte array elements.
    int BaseObject_ctor(int object) { return object; }

    int Pair_ctor(int pair)
    {
        port::store<uint8_t>(static_cast<uint32_t>(pair), 0);
        port::store<uint8_t>(static_cast<uint32_t>(pair) + 1u, 0);
        return pair;
    }

    // char: the player model's matrix.
    void Player_GetMatrix(GuestPtr<Player> player, GuestPtr<Vec4> matrix)
    {
        for (int row = 0; row < 4; ++row)
            matrix[row] = player->model->matrix[row];
    }

    // camera
    void Camera_SetEyeTarget(GuestPtr<Camera> camera, GuestPtr<Vec4> eye, GuestPtr<Vec4> target)
    {
        camera->eye = *eye;
        camera->target = *target;
    }

    // Fog range: stores near and far and the two fog coefficients the renderer uses,
    // (255 - 255 (far + near) / (far - near)) / 2 and 255 far near / (far - near), in that operation order.
    void Camera_SetFogRange(GuestPtr<Camera> camera, ps2f fogNear, ps2f fogFar)
    {
        camera->fogNear = fogNear;
        camera->fogFar = fogFar;
        const ps2f range = fogFar - fogNear;
        ps2f offset = ps2f(-255.0f) * (fogFar + fogNear);
        offset = offset / range;
        ps2f scale = fogFar * fogNear;
        scale = scale * ps2f(255.0f);
        offset = ps2f(255.0f) + offset;
        scale = scale / range;
        camera->fogA = Vec4{0.0f, 0.0f, 0.0f, offset / ps2f(2.0f)};
        camera->fogB = Vec4{0.0f, 0.0f, 0.0f, scale};
    }

    // math
    void Mat4_Copy(GuestPtr<Vec4> dst, GuestPtr<Vec4> src)
    {
        for (int row = 0; row < 4; ++row)
            dst[row] = src[row];
    }

    // file: a closed handle.
    GuestPtr<GameFile> File_ctor(GuestPtr<GameFile> file, int async)
    {
        file->async = async;
        file->fd = -1;
        return file;
    }

    // font: glyph `ch` in the loaded font (0x90 bytes each).
    int Font_GetGlyph(uint32_t ch) { return static_cast<int>(globals::fontData.get() + (ch & 0xFFu) * 0x90u); }

    // claim: entry `index` of CLAIM.BIN section C (word offsets from the start of the file).
    int Claim_GetSectionC(int index)
    {
        const uint32_t offset = port::load<uint32_t>(globals::claimSectionC.get() + 4u * static_cast<uint32_t>(index));
        return static_cast<int>(globals::claimData.get() + offset * 4u);
    }

    // save: the result word of request slot `request` (1-based; 0 is the last of the 64 slots).
    int McQueue_GetResult(int, int request)
    {
        int slot = request - 1;
        if (slot < 0)
            slot = 63;
        return port::load<int32_t>(globals::kMcRequests + 0x24u * static_cast<uint32_t>(slot) + 0xCu);
    }

    void McManager_ResetState(GuestPtr<McManager> mc) { mc->state = 0; }

    namespace
    {
        // A request is done once it is not between the queue head and tail any more (scanning back from the
        // head, wrapping at 64 slots); an empty queue has everything done.
        int requestDone(int32_t head, int32_t tail, int request)
        {
            if (head == tail)
                return 1;
            for (int32_t i = head; i != tail;)
            {
                if (i == request)
                    return 1;
                if (--i < 0)
                    i = 63;
            }
            return 0;
        }
    }

    int McQueue_IsDone(int, int request) { return requestDone(globals::mcQueueHead.get(), globals::mcQueueTail.get(), request); }

    // file
    int FileQueue_IsDone(int, int request) { return requestDone(globals::fileQueueHead.get(), globals::fileQueueTail.get(), request); }

    // family status bytes (one per acquaintance): bits 0-1, bits 2-3 and a level in the high nibble.
    int StatusByte_ctorE0(int status)
    {
        port::store<uint8_t>(static_cast<uint32_t>(status) + 3u, 0xE0); // level 14, low bits clear
        return status;
    }

    // Level +1 while below 12; from level 2 on, bits 0-1 are cleared.
    void StatusByte_IncLevel(GuestPtr<uint8_t> status)
    {
        const uint8_t level = *status >> 4;
        if (level >= 12)
            return;
        const uint8_t next = (level + 1) & 0xF;
        *status = static_cast<uint8_t>((*status & 0x0F) | (next << 4));
        if (next >= 2)
            *status &= 0xFC;
    }

    // Ages the 256 member status entries in use, from the city clock (seconds) and month count: every
    // 600 s the level rises (StatusByte_IncLevel), each midnight bits 2-3 clear, and at midnight of every
    // 12th month levels 12 and 13 rise once more.
    void MemberStatus_Tick(int clock, int month)
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            const uint32_t entry = globals::kMemberStatus + 4u * i;
            if (port::load<int16_t>(entry) == 0)
                continue;
            const GuestPtr<uint8_t> status{entry + 3u};
            if (clock % 600 == 0)
                StatusByte_IncLevel(status);
            if (clock % 86400 == 0 && ((*status & 0xFu) >> 2) != 0)
                *status &= 0xF3;
            if (clock == 0 && month % 12 == 0)
            {
                const uint8_t level = *status >> 4;
                if (level > 11 && level < 14)
                    *status = static_cast<uint8_t>((*status & 0x0F) | (((level + 1) & 0xF) << 4));
            }
        }
    }

    // char: counts down the 25 character type slot timers and frees a slot (6000) when its time is up.
    void CharType_TickSlots()
    {
        for (uint32_t i = 0; i < 25; ++i)
        {
            const uint32_t timer = globals::kCharTypeTimers + 4u * i;
            if (port::load<int32_t>(timer) != 0)
                port::store<int32_t>(timer, port::load<int32_t>(timer) - 1);
            if (port::load<int32_t>(timer) == 0)
                port::store<int32_t>(globals::kCharTypeSlots + 4u * i, 6000);
        }
    }

    // event: the data size of the event handler classes (vtable slot).
    int EventData_GetSize1C() { return 0x1C; }
    int EventData_GetSize7C() { return 0x7C; }

    // Static constructor: the 256 member status bytes (4 bytes apart; __construct_array in the original).
    void StaticInit_StatusBytes()
    {
        for (uint32_t i = 0; i < 0x100; ++i)
            StatusByte_ctorE0(static_cast<int>(globals::kMemberStatus + 4u * i));
    }

    // core: the game object's sound state and scene pointers start empty.
    void GameInit(int game)
    {
        Sound_InitState();
        const uint32_t g = static_cast<uint32_t>(game);
        port::store<int32_t>(g + 0x4, 0);
        port::store<int32_t>(g + 0xC, 0);
        port::store<int32_t>(g + 0x10, 0);
        port::store<int32_t>(g + 0x8, 0);
    }

    // char: the second character class type, a CharType with its own vtable.
    GuestPtr<CharClass> Char2_ctor(GuestPtr<CharClass> cls, GuestPtr<uint8_t> record)
    {
        CharType_ctor(cls, record);
        cls->vtable = GuestPtr<void>{0x001BCE90u};
        return cls;
    }

    // char: switches the player to `mode` (-1: back to the previous one) and swallows the Cross press that
    // chose it.
    void Player_SetMode(GuestPtr<Player> player, int mode)
    {
        if (mode == -1)
            mode = player->previousMode;
        player->previousMode = player->mode;
        player->mode = mode;
        player->unk_80 = 0;
        player->unk_08 = 0;
        player->unk_34 = 0;
        player->unk_40 = -1;
        Pad_ClearBit40();
    }

    // file: empties the disc request queue.
    void FileQueue_Init()
    {
        for (uint32_t i = 0; i < 0x40; ++i)
            port::store<int32_t>(globals::kFileRequests + 0x1Cu * i, 0);
        globals::fileQueueTail.set(0);
        globals::fileQueueHead.set(0);
        globals::fileQueueCount.set(0);
    }

    // laptop
    GuestPtr<Laptop> Laptop_ctor(GuestPtr<Laptop> laptop)
    {
        laptop->unk_00 = GuestPtr<void>{port::load<uint32_t>(0x001F3B08u)};
        laptop->unk_3C = 0;
        laptop->unk_44 = 0;
        laptop->unk_3D0 = 0;
        return laptop;
    }

    // events: hunger fires when the player's stamina is out; quota-80 once the population reaches 80% of the
    // stage's quota (0x1C554C).
    int EvHunger_Check(GuestPtr<EventData>, GuestPtr<CityScene> city) { return Stamina_HungerLevel(city->stamina) == 0; }

    int EvQuota80_Check(GuestPtr<EventData>, GuestPtr<CityScene> city)
    {
        const ps2f target = ps2f(0.8f) * fromInt(globals::stageState()->quota);
        return !(fromInt(city->map->population) < target);
    }

    // Paydays fall in months 5 and 11 of each year (one month per in-game day); in stage 0 not before month 11.
    int EvPayday_Check(GuestPtr<EventData>, GuestPtr<CityScene> city)
    {
        if (globals::stageState()->stage == 0 && city->monthCount < 11)
            return 0;
        const int month = city->monthCount % 12;
        return month == 5 || month == 11;
    }

    // stage: no stage loaded, empty names, and the two starting values.
    void Stage_Reset()
    {
        const GuestPtr<StageState> stage = globals::stageState();
        stage->stage = -1;
        stage->unk_1C0 = 3000;
        stage->unk_1C4 = 2500;
        stage->unk_280 = 0;
        for (StageName &name : stage->names)
            name.text[0] = '\0';
    }

    // char: a character type from its CHARTYPE.BIN record.
    GuestPtr<CharClass> CharType_ctor(GuestPtr<CharClass> cls, GuestPtr<uint8_t> record)
    {
        const auto part = [](uint8_t index) { return GuestPtr<void>{port::load<uint32_t>(globals::kCharParts + 4u * index)}; };
        cls->vtable = GuestPtr<void>{0x001BCE70u};
        cls->parts[0] = part(record[8]);
        cls->parts[1] = part(record[9]);
        cls->parts[2] = part(record[10]);
        cls->parts[3] = part(record[14]);
        cls->unk_10 = record[11];
        cls->unk_12 = record[12];
        cls->unk_14 = record[13];
        cls->unk_16 = record[2];
        cls->unk_18 = port::load<uint16_t>(record.address + 4u);
        cls->unk_1C = port::load<uint32_t>(record.address + 0x14u);
        return cls;
    }

    // char: the person type adds its walking speed, record halfword 6 in millionths.
    GuestPtr<PersonClass> Person_ctor(GuestPtr<PersonClass> cls, GuestPtr<uint8_t> record)
    {
        CharType_ctor(GuestPtr<CharClass>::of(cls->base), record);
        cls->base.vtable = GuestPtr<void>{0x001BCEB0u};
        cls->walkSpeed = fromInt(port::load<uint16_t>(record.address + 6u)) / ps2f(1000000.0f);
        return cls;
    }

    // char: frees all 25 character type slots.
    void CharType_ClearSlots()
    {
        for (uint32_t i = 0; i < 25; ++i)
        {
            port::store<int32_t>(globals::kCharTypeSlots + 4u * i, 6000);
            port::store<int32_t>(globals::kCharTypeTimers + 4u * i, 0);
        }
    }

    // char: the player starts in mode 0 at the origin, with its model from 0x1F3CD4.
    GuestPtr<Player> Player_ctor(GuestPtr<Player> player)
    {
        player->model = GuestPtr<A3DObject>{port::load<uint32_t>(0x001F3CD4u)};
        player->mode = 0;
        player->previousMode = 0;
        player->unk_08 = 0;
        player->unk_80 = 0;
        player->unk_34 = 0;
        player->unk_0C = 0;
        player->position.x = 0.0f;
        player->position.y = 0.0f;
        player->position.z = 0.0f;
        player->position.w = 0.0f;
        player->unk_3C = 0;
        player->unk_40 = -1;
        player->unk_44 = 0;
        player->unk_48 = 0;
        player->unk_50 = 0;
        player->jumpSpeed = 0.0f;
        player->jumps = 0;
        return player;
    }

    // phone: an empty call queue.
    GuestPtr<PhoneCalls> PhoneCall_ctor(GuestPtr<PhoneCalls> phone)
    {
        phone->unk_00 = GuestPtr<void>{port::load<uint32_t>(0x001F3B08u)};
        for (PhoneCallEntry &call : phone->calls)
            call.family = 0;
        phone->unk_04 = 0;
        phone->unk_08 = 0;
        phone->unk_0C = 0;
        phone->unk_40 = 0;
        return phone;
    }

    // bag: empty slots, no shortcuts, and the bag model scaled by 7 at its place from 0x1AEF10.
    GuestPtr<Bag> Bag_ctor(GuestPtr<Bag> bag)
    {
        for (BagSlot &slot : bag->slots)
            slot.item = 0;
        for (BagShortcut &shortcut : bag->shortcuts)
            shortcut.slot = -1;
        bag->model = GuestPtr<A3DObject>{port::load<uint32_t>(0x001F3B24u)};
        bag->unk_194 = 0;
        bag->matrix = GuestPtr<Vec4>{bag->model.address + 0x10u};
        bag->matrix[2].z = 7.0f;
        bag->matrix[1].y = 7.0f;
        bag->matrix[0].x = 7.0f;
        const GuestPtr<Vec4> place{0x001AEF10u};
        bag->matrix[3].x = place->x;
        bag->matrix[3].y = place->y;
        bag->matrix[3].z = place->z;
        bag->unk_19C = 0;
        bag->unk_174 = 0;
        bag->unk_178 = 0;
        bag->unk_170 = 0;
        bag->unk_000 = 0;
        bag->unk_1A0 = 0;
        bag->unk_002 = 0;
        for (uint8_t &b : bag->unk_17C)
            b = 0;
        bag->unk_1A4 = -1;
        return bag;
    }

    // markers: four marker matrices, identity moved to the offsets at 0x1B0DB0 and 0.1 down.
    GuestPtr<Markers> Markers_ctor(GuestPtr<Markers> markers)
    {
        markers->vtable = GuestPtr<void>{0x001BCFC8u};
        markers->model = GuestPtr<A3DObject>{port::load<uint32_t>(0x001F3B28u)};
        markers->unk_004 = GuestPtr<void>{port::load<uint32_t>(0x001F3B08u)};
        markers->unk_110 = 1;
        markers->unk_114 = 2;
        markers->unk_118 = 3;
        markers->unk_11C = -1;
        const GuestPtr<ps2f> offsets{0x001B0DB0u};
        for (int i = 0; i < 4; ++i)
        {
            const GuestPtr<Vec4> matrix = GuestPtr<Vec4>::of(markers->matrices[4 * i]);
            Mat4_Copy(matrix, GuestPtr<Vec4>{0x001B2550u}); // identity
            matrix[3].x = offsets[2 * i];
            matrix[3].y = offsets[2 * i + 1];
            matrix[3].z = -0.1f;
        }
        markers->unk_120 = 0;
        markers->unk_124 = 0;
        return markers;
    }

    // laptop: the first-time entries from the template; the default count is 4 until something changes it.
    void Laptop_InitEntries()
    {
        if (globals::laptopDefaultSet.get() == 0)
        {
            globals::laptopDefaultCount.set(4);
            globals::laptopDefaultSet.set(1);
        }
        for (uint32_t i = 0; i < 14; ++i)
            port::store<uint32_t>(globals::kLaptopEntries + 4u * i, port::load<uint32_t>(globals::kLaptopTemplate + 4u * i));
        globals::laptopEntryCount.set(globals::laptopDefaultCount.get());
    }

    // stage: the stage object starts with an empty city name.
    int StageObj_ctor(int stage)
    {
        port::store<uint8_t>(globals::kStageState, 0);
        return stage;
    }

    PORT_FN(BaseObject_ctor);
    PORT_FN(Pair_ctor);
    PORT_FN(Player_GetMatrix);
    PORT_FN(Camera_SetEyeTarget);
    PORT_FN(Mat4_Copy);
    PORT_FN(File_ctor);
    PORT_FN(Font_GetGlyph);
    PORT_FN(Claim_GetSectionC);
    PORT_FN(McQueue_GetResult);
    PORT_FN(McManager_ResetState);
    PORT_FN(Camera_SetFogRange);
    PORT_FN(McQueue_IsDone);
    PORT_FN(FileQueue_IsDone);
    PORT_FN(StatusByte_ctorE0);
    PORT_FN(StatusByte_IncLevel);
    PORT_FN(CharType_TickSlots);
    PORT_FN(MemberStatus_Tick);
    PORT_FN(EventData_GetSize1C);
    PORT_FN(EventData_GetSize7C);
    PORT_FN(StageObj_ctor);
    PORT_FN(StaticInit_StatusBytes);
    PORT_FN(GameInit);
    PORT_FN(Char2_ctor);
    PORT_FN(Player_SetMode);
    PORT_FN(FileQueue_Init);
    PORT_FN(Laptop_ctor);
    PORT_FN(EvHunger_Check);
    PORT_FN(EvQuota80_Check);
    PORT_FN(EvPayday_Check);
    PORT_FN(Stage_Reset);
    PORT_FN(CharType_ctor);
    PORT_FN(CharType_ClearSlots);
    PORT_FN(Player_ctor);
    PORT_FN(PhoneCall_ctor);
    PORT_FN(Bag_ctor);
    PORT_FN(Laptop_InitEntries);
    PORT_FN(Person_ctor);
    PORT_FN(Markers_ctor);
}
