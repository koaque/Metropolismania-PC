// Small functions of subsystems that have no other ported code yet: base objects, the player and camera
// matrices, files, fonts, claims, memory card queue results, events and the stage state.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"
#include "vu0.h"

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

    // The light matrices from the three light directions and colours (the colour matrix's w column is
    // cleared afterwards).
    void Camera_UpdateLightMatrices(GuestPtr<Camera> camera)
    {
        const auto at = [&](const Vec4 &v) { return GuestPtr<Vec4>::of(v).address; };
        sdk::sceVu0NormalLightMatrix(at(camera->normalLight[0]), at(camera->lightDir[0]), at(camera->lightDir[1]), at(camera->lightDir[2]));
        sdk::sceVu0LightColorMatrix(at(camera->lightColour[0]), at(camera->lightRgb[0]), at(camera->lightRgb[1]), at(camera->lightRgb[2]),
                                    at(camera->ambient));
        camera->lightColour[0].w = 0.0f;
        camera->lightColour[1].w = 0.0f;
        camera->lightColour[2].w = 0.0f;
    }

    // The view matrix from eye, target and up, and the two world-to-screen matrices. The view direction is
    // normalized on VU0 through a 16-byte stack slot, as in the original (its w ends up squared).
    void Camera_UpdateMatrices(GuestPtr<Camera> camera)
    {
        using namespace vu0;
        const auto at = [&](const Vec4 &v) { return GuestPtr<Vec4>::of(v).address; };
        port::GuestLocal<Vec4> direction;
        vf(4) = load(GuestPtr<Vec4>::of(camera->target));
        vf(5) = load(GuestPtr<Vec4>::of(camera->eye));
        set(vf(4), sub(vf(4), vf(5)), XYZW);
        store(direction, vf(4));
        vf(1) = load(direction);
        set(vf(2), mul(vf(1), vf(1)), XYZW);
        set(vf(2), add(vf(2), lane(vf(2), 1)), X);
        set(vf(2), add(vf(2), lane(vf(2), 2)), X);
        q() = rsqrt(x(vf(2)));
        set(vf(2), mul(vf(1), _mm_set1_ps(q())), XYZ);
        store(direction, vf(2));
        sdk::sceVu0CameraMatrix(at(camera->view[0]), at(camera->eye), direction.ptr().address, at(camera->up));
        sdk::sceVu0MulMatrix(at(camera->worldScreenA[0]), at(camera->screenA[0]), at(camera->view[0]));
        sdk::sceVu0MulMatrix(at(camera->worldScreenB[0]), at(camera->screenB[0]), at(camera->view[0]));
    }

    // Zoom levels 0 and 1 are kept; from 2 on, the camera's height above the ground decides: within 1.15
    // level 0, within 7.1 level 1, else 2. (The original then compares the height with -20.5 in double
    // precision through the SDK's soft-float helpers, but returns 2 either way.)
    int CityCamera_GetZoomLevel(GuestPtr<CityCamera> camera, GuestPtr<CityScene> city)
    {
        const int zoom = camera->zoomLevel;
        if (zoom < 2)
            return zoom;
        const ps2f height = camera->base.eye.y - Map_SampleHeight(city->map, camera->base.eye.x, camera->base.eye.z);
        if (ps2f(-1.15f) < height)
            return 0;
        if (ps2f(-7.1f) < height)
            return 1;
        return 2;
    }

    // char: the player's model, swaying while there is room for it, lit, holding the bag's current item
    // (bag +2, provisional).
    void Player_Draw(GuestPtr<Player> player, GuestPtr<CityScene> city)
    {
        if (player->visible == 0)
            return;
        Instance_DrawSwayingIfRoom(player->model, player->swayArg, player->swayPhase);
        A3DObject_UpdateMatrices(player->model);
        Instance_DrawModelLit(player->model, GuestPtr<uint32_t>{player.address + 0x20u}, player->mode, city->bag->unk_002);
    }

    // The camera's three projection matrices from its view size, distance, scale, centre, depth range and near
    // and far planes: screenA (a perspective divide by z, then scale, centre and depth mapping, multiplied
    // on VU0 through a stack matrix), screenB (near/far projection) and screenC (view size to screen).
    void Camera_UpdateProjection(GuestPtr<Camera> camera)
    {
        // Declared in this order, the rows take the original's stack matrix at entry sp - 0x40.
        port::GuestLocal<Vec4> row3;
        port::GuestLocal<Vec4> row2;
        port::GuestLocal<Vec4> row1;
        port::GuestLocal<Vec4> row0;
        const ps2f zNear = camera->zNear, depthMin = camera->depthMin, zFar = camera->zFar, depthMax = camera->depthMax;
        const ps2f halfHeight = zNear * camera->viewHeight / camera->distance;
        const ps2f halfWidth = zNear * camera->viewWidth / camera->distance;
        const ps2f span = -zNear + zFar;
        const ps2f depthOffset = (-depthMin * zNear + depthMax * zFar) / span;
        const auto matrix = [](Vec4 &row0Of) { return GuestPtr<Vec4>::of(row0Of).address; };

        sdk::sceVu0UnitMatrix(matrix(camera->screenA[0]));
        camera->screenA[0].x = camera->distance;
        camera->screenA[1].y = camera->distance;
        camera->screenA[2].z = 0.0f;
        camera->screenA[3].w = 0.0f;
        camera->screenA[3].z = 1.0f;
        camera->screenA[2].w = 1.0f;
        const GuestPtr<Vec4> scale = row0.ptr();
        sdk::sceVu0UnitMatrix(scale.address);
        scale[0].x = camera->scaleX;
        scale[1].y = camera->scaleY;
        scale[2].z = zFar * zNear * (-depthMax + depthMin) / span;
        scale[3].x = camera->centreX;
        scale[3].y = camera->centreY;
        scale[3].z = depthOffset;
        sdk::sceVu0MulMatrix(matrix(camera->screenA[0]), scale.address, matrix(camera->screenA[0]));

        sdk::sceVu0UnitMatrix(matrix(camera->screenB[0]));
        camera->screenB[0].x = ps2f(2.0f) * camera->zNear / (halfWidth + halfWidth);
        camera->screenB[1].y = ps2f(2.0f) * camera->zNear / (halfHeight + halfHeight);
        camera->screenB[2].z = (camera->zFar + camera->zNear) / (camera->zFar - camera->zNear);
        camera->screenB[3].z = ps2f(-2.0f) * (camera->zFar * camera->zNear) / (camera->zFar - camera->zNear);
        camera->screenB[2].w = 1.0f;
        camera->screenB[3].w = 0.0f;

        sdk::sceVu0UnitMatrix(matrix(camera->screenC[0]));
        camera->screenC[0].x = halfWidth * (camera->distance * camera->scaleX) / camera->zNear;
        camera->screenC[1].y = halfHeight * (camera->distance * camera->scaleY) / camera->zNear;
        camera->screenC[2].z = (-camera->depthMin + camera->depthMax) / ps2f(2.0f);
        camera->screenC[3].z = (camera->depthMin + camera->depthMax) / ps2f(2.0f);
        camera->screenC[3].x = camera->centreX;
        camera->screenC[3].y = camera->centreY;
        camera->screenC[3].w = 1.0f;
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
        player->swayPhase = 0.0f;
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

    // Mission cleared: the population has reached the quota and the mission progress its target.
    int EvMissionCleared_Check(GuestPtr<EventData>, GuestPtr<CityScene> city)
    {
        if (city->map->population < globals::stageState()->quota)
            return 0;
        return !(CityScene_MissionProgress(city) < globals::stageState()->missionTarget);
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
        cls->flags = port::load<uint16_t>(record.address + 4u);
        cls->unk_1C = port::load<uint32_t>(record.address + 0x14u);
        return cls;
    }

    // char: the nearest building (city-block distance from (x, z), within the list's own range at 0x1B0D30)
    // of tile list `list` whose family lives in the city, other than skipA and skipB. Mode 0 also wants
    // room for visitors (+0x16 below type +0x14A), mode 1 room for occupants (+0x17 below the capacity).
    // Ties go to the later entry. Returns 0x3F80 when none qualifies.
    int16_t Person_FindBuildingInList(GuestPtr<CityScene> city, int list, int x, int z, int mode, int16_t skipA, int16_t skipB)
    {
        uint16_t best = 0x3F80;
        int bestDistance = port::load<uint16_t>(0x001B0D30u + 2u * static_cast<uint32_t>(list));
        port::GuestLocal<uint32_t> count;
        const GuestPtr<uint16_t> items = Map_GetTileList(city->map, list, count);
        for (int i = 0; i < static_cast<int>(*count); ++i)
        {
            const uint16_t id = items[i];
            if (id == static_cast<uint16_t>(skipA) || id == static_cast<uint16_t>(skipB))
                continue;
            const GuestPtr<MapObject> obj = Map_GetRecord(city->map, id);
            if (Family_InCity(obj->family) == 0)
                continue;
            const uint32_t btype = globals::buildingTypeAddress(obj->type);
            const GuestPtr<uint8_t> people{obj.address + 0x16u}; // [0] visitors, [1] occupants
            if (mode == 0 && people[0] >= port::load<uint8_t>(btype + 0x14Au))
                continue;
            if (mode == 1 && people[1] >= port::load<uint16_t>(btype + 0x140u))
                continue;
            const int distance = sdk::abs((obj->tile & 0x7F) - x) + sdk::abs((obj->tile >> 7) - z);
            if (distance <= bestDistance)
            {
                best = items[i];
                bestDistance = distance;
            }
        }
        return static_cast<int16_t>(best);
    }

    // char: turning and moving with the pad unless Circle is held: up and down set the z step, the analog
    // stick's sideways bits the x step, right and left turn by 0.02 (wrapping at pi).
    void Player_Turn(GuestPtr<Player> player, GuestPtr<ps2f> move)
    {
        if ((Pad_Held(0) & 0x20u) != 0)
            return;
        if ((Pad_Held(0) & 0x1000u) != 0 || (Pad_Held(0) & 0x100000u) != 0)
            move[2] = -player->moveSpeed;
        if ((Pad_Held(0) & 0x4000u) != 0 || (Pad_Held(0) & 0x400000u) != 0)
            move[2] = player->moveSpeed;
        if ((Pad_Held(0) & 0x200000u) != 0)
            move[0] = -player->moveSpeed;
        if ((Pad_Held(0) & 0x800000u) != 0)
            move[0] = player->moveSpeed;
        const ps2f pi = 3.14159274f;
        if ((Pad_Held(0) & 0x2000u) != 0)
        {
            player->heading = player->heading + ps2f(0.02f);
            if (!(player->heading < pi))
                player->heading = -pi;
        }
        if ((Pad_Held(0) & 0x8000u) != 0)
        {
            player->heading = player->heading - ps2f(0.02f);
            if (player->heading < -pi)
                player->heading = pi;
        }
    }

    // events: three different kinds of claim unmet at once on buildings of type flag 8 (claim slot 0x18 =
    // none), counting the claim kinds in the claim section entries.
    int EvThreeUnsatisfied_Check(GuestPtr<EventData>, GuestPtr<CityScene> city)
    {
        int8_t first = 0, second = 0;
        for (int list = 0; list < 0x50; ++list)
        {
            port::GuestLocal<uint32_t> count;
            GuestPtr<uint16_t> item = Map_GetTileList(city->map, list, count);
            const GuestPtr<uint16_t> end = item + static_cast<int>(*count);
            for (; item.address < end.address; item = item + 1)
            {
                const GuestPtr<MapObject> obj = Map_GetRecord(city->map, *item);
                const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(obj->type)};
                if ((btype->flags & 8u) == 0 || obj->claimSlot == 0x18 || ClaimMeter_Level(obj->claimMeter) == 0)
                    continue;
                const int8_t kind = port::load<int8_t>(static_cast<uint32_t>(Claim_GetSectionB(btype->claimB)) + 8u * obj->claimSlot);
                if (first == 0)
                    first = kind;
                else if (kind != first)
                {
                    if (second == 0)
                        second = kind;
                    else if (kind != second)
                        return 1;
                }
            }
        }
        return 0;
    }

    // char: a person without a target of its own (none, or the workplace) picks a building near work from
    // the list for its schedule state (2..6: lists 0x17..0x1B, else 0x1B); the chosen building counts one
    // more visitor (+0x16). Returns the target.
    uint16_t Person_TargetWork(GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        if (person->target != 0x3F80 && person->target != person->work)
            return person->target;
        const GuestPtr<MapObject> work = Map_GetRecord(city->map, person->work);
        int list = 0x1B;
        switch (person->schedState)
        {
        case 2: list = 0x17; break;
        case 3: list = 0x18; break;
        case 4: list = 0x19; break;
        case 5: list = 0x1A; break;
        default: break;
        }
        person->target = static_cast<uint16_t>(
            Person_FindBuildingInList(city, list, work->tile & 0x7F, work->tile >> 7, 0, static_cast<int16_t>(person->work), 0x3F80));
        if (person->target != 0x3F80)
        {
            const GuestPtr<uint8_t> visitors{Map_GetRecord(city->map, person->target).address + 0x16u};
            ++*visitors;
        }
        return person->target;
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
        player->swayPhase = 0.0f;
        player->heading = 0.0f;
        player->position.x = 0.0f;
        player->position.y = 0.0f;
        player->position.z = 0.0f;
        player->position.w = 0.0f;
        player->visible = 0;
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
        markers->markerModel = GuestPtr<A3DObject>{port::load<uint32_t>(0x001F3B08u)};
        markers->nodes[0] = 1;
        markers->nodes[1] = 2;
        markers->nodes[2] = 3;
        markers->nodes[3] = -1;
        const GuestPtr<ps2f> offsets{0x001B0DB0u};
        for (int i = 0; i < 4; ++i)
        {
            const GuestPtr<Vec4> matrix = GuestPtr<Vec4>::of(markers->matrices[4 * i]);
            Mat4_Copy(matrix, GuestPtr<Vec4>{0x001B2550u}); // identity
            matrix[3].x = offsets[2 * i];
            matrix[3].y = offsets[2 * i + 1];
            matrix[3].z = -0.1f;
        }
        markers->visible = 0;
        markers->unk_124 = 0;
        return markers;
    }

    // markers: the base model, then each marker's node with its matrix.
    void Markers_Draw(GuestPtr<Markers> markers)
    {
        if (markers->visible == 0)
            return;
        A3DObject_Draw(markers->model);
        for (int i = 0; i < 4; ++i)
        {
            const int node = markers->nodes[i];
            if (node == -1)
                continue;
            const GuestPtr<A3DObject> model = markers->markerModel;
            const GuestPtr<Vec4> nodeMatrix{port::load<uint32_t>(model->model.address + 8u) + static_cast<uint32_t>(node) * 0xC0u + 0x70u};
            for (int row = 0; row < 4; ++row)
                nodeMatrix[row] = markers->matrices[4 * i + row];
            A3DObject_DrawNode(model, node);
        }
    }

    // char: stamina drains by 1 + 1/100000 of itself per tick; the notice (city notice line, id 0) comes when
    // it drops below a 100000 step, at most every 8000 ticks.
    void Stamina_Tick(GuestPtr<Stamina> stamina, GuestPtr<CityScene> city)
    {
        const int before = stamina->value;
        stamina->value = before - (before / 100000 + 1);
        if (stamina->value < 0)
            stamina->value = 0;
        ++stamina->unk_08;
        if (stamina->value < before - before % 100000 && stamina->unk_08 > 8000)
        {
            Notice_Show(city->notice, 0);
            stamina->unk_08 = 0;
        }
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

    namespace
    {
        // How far `other` stands from `person` on the ground, on VU0 through the 16-byte stack slot as in the
        // original: only x and z are stored, so the slot's y and w (the last result, or what was there) go into
        // vf4 as well, and the slot ends up holding the distance in every lane.
        ps2f groundDistance(const MapPerson &person, const MapPerson &other, GuestPtr<Vec4> slot)
        {
            using namespace vu0;
            slot->x = other.x - person.x;
            slot->z = other.z - person.z;
            vf(4) = load(slot);
            set(vf(5), mul(vf(4), vf(4)), X | Z);
            set(vf(6), add(vf(5), lane(vf(5), 2)), X);
            q() = sqrtQ(x(vf(6)));
            set(vf(7), add(vf(0), _mm_set1_ps(q())), XYZW);
            store(slot, vf(7));
            return slot->x;
        }

        GuestPtr<CharClass> charClassOf(const MapPerson &person)
        {
            return GuestPtr<CharClass>{port::load<uint32_t>(globals::charClassTable.get() + 4u * person.charType)};
        }
    }

    // char: a resident's tick. Friendship rises (+0x2F), the countdown at +0x25 runs down, and while the city has
    // flag 4 the introduction calls go on: when the tick count reaches the next call's, the family this resident
    // introduces phones about moving in, provided it can apply, the applicants have room (under 32) and its type
    // phones (+0x154 bit 7); each call lowers the city's pending approval, and after four they stop. The wait
    // grows by one tick after every try.
    void Person_TickCounters(GuestPtr<CharClass>, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        StatusByte_IncLevel(GuestPtr<uint8_t>::of(person->status));
        if (person->cooldown != 0)
            --person->cooldown;
        if (globals::introCallDue.get() == 0)
            return;
        globals::introCallTick.set(globals::introCallTick.get() + 1);
        if (globals::introCallTick.get() != globals::introCallDue.get())
            return;
        if ((city->flags & 4u) == 0)
        {
            globals::introCallDue.set(0);
            return;
        }
        if (city->applicants->count < 0x20 && Family_CanApply(person->introduces, city) != 0)
        {
            const uint32_t type = port::load<uint32_t>(globals::buildingTypeTable.get() + 4u * static_cast<uint32_t>(Family_GetTypeIndex(person->introduces)));
            if (GuestPtr<BuildingType>{type}->callFlags < 0 && PhoneCall_Post(city->phoneCalls, kNoPerson, person->introduces, 0, 1) != 0)
            {
                globals::introCallCount.set(globals::introCallCount.get() + 1);
                if (globals::introCallCount.get() >= 4)
                {
                    globals::introCallDue.set(0);
                    globals::introCallCount.set(0);
                }
                --city->pendingApproval;
            }
        }
        if (globals::introCallDue.get() != 0)
            globals::introCallDue.set(globals::introCallDue.get() + 1);
    }

    // char: on every 8th frame (by the person's index), turns the person towards the middle of the building on
    // the tile it walks to and moves it one step along its facing (VU0, the matrix times the step vector): the
    // class's walking speed plus (index & 3) / 2000, times 8, doubled while running (flag 0x80). A person that
    // left `tile` is unlinked from it, unless flag 0x800 holds it there. The heading follows the facing.
    void Person_UpdatePose(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> person, int, int tile)
    {
        using namespace vu0;
        if ((person->index & 7) != (globals::frameCounter.get() & 7))
            return;
        // Declared in this order, they take the original's slots (entry sp - 0x10 down to - 0x70).
        port::GuestLocal<Vec4> moved;
        port::GuestLocal<Vec4> row3;
        port::GuestLocal<Vec4> row2;
        port::GuestLocal<Vec4> row1;
        port::GuestLocal<Vec4> row0;
        port::GuestLocal<Vec4> position;
        port::GuestLocal<Vec4> facing;
        const GuestPtr<Vec4> matrix = row0.ptr();
        const int walkTile = person->walkTile;
        const GuestPtr<MapObject> obj = Map_GetTileFirstObject(city->map, walkTile);
        const GuestPtr<BuildingType> type{port::load<uint32_t>(globals::buildingTypeTable.get() + 4u * obj->type)};
        const ps2f size = fromInt(type->size);
        facing->w = 1.0f;
        facing->z = (fromInt(128 - (walkTile >> 7)) - ps2f(64.0f)) - size / ps2f(2.0f);
        facing->y = 0.0f;
        facing->x = (fromInt(walkTile & 0x7F) - ps2f(64.0f)) + size / ps2f(2.0f);
        position->w = 1.0f;
        position->z = person->z;
        position->y = 0.0f;
        position->x = person->x;
        vf(4) = load(facing);
        vf(5) = load(position);
        set(vf(4), sub(vf(4), vf(5)), XYZW);
        store(facing, vf(4));
        vf(1) = load(facing);
        set(vf(2), mul(vf(1), vf(1)), XYZW);
        set(vf(2), add(vf(2), lane(vf(2), 1)), X);
        set(vf(2), add(vf(2), lane(vf(2), 2)), X);
        q() = rsqrt(x(vf(2)));
        set(vf(2), mul(vf(1), _mm_set1_ps(q())), XYZ);
        store(facing, vf(2));
        Mat4_Copy(matrix, GuestPtr<Vec4>{globals::kIdentityMatrix});
        const ps2f fz = facing->z, fx = facing->x;
        matrix[0].x = -fz;
        matrix[0].z = fx;
        matrix[2].x = -fx;
        matrix[2].z = -fz;
        const ps2f walkSpeed = GuestPtr<PersonClass>{cls.address}->walkSpeed;
        ps2f step = ps2f(8.0f) * -(walkSpeed + fromInt(person->index & 3) / ps2f(2000.0f));
        if ((person->flags & 0x80u) != 0)
            step = step * ps2f(2.0f);
        const GuestPtr<Vec4> walkStep{globals::kWalkStep};
        walkStep->z = step;
        for (int row = 0; row < 4; ++row)
            vf(1 + row) = load(matrix + row);
        vf(5) = load(walkStep);
        set(acc(), mul(vf(1), lane(vf(5), 0)), XYZW);
        set(acc(), add(acc(), mul(vf(2), lane(vf(5), 1))), XYZW);
        set(acc(), add(acc(), mul(vf(3), lane(vf(5), 2))), XYZW);
        set(vf(9), add(acc(), mul(vf(4), lane(vf(5), 3))), XYZW);
        store(moved, vf(9));
        person->x = person->x + moved->x;
        person->z = person->z + moved->z;
        if ((person->flags & 0x800u) == 0)
        {
            const int x = sdk::fptosi(ps2f(64.0f) + person->x);
            const int z = sdk::fptosi(ps2f(64.0f) - person->z);
            if (tile != x + z * 128)
                Map_UnlinkPersonFromTile(city->map, tile, person->index);
        }
        person->heading = sdk::atan2f(-facing->x, -facing->z);
    }

    // event: inserts the two-byte entry `id` (event type, data byte) into level `level` of the queue, searching
    // from `start` within the level for the first entry whose type has no lower priority; the later levels' starts
    // move up one entry. The queue keeps nine level starts (in entries; the ninth is the entry count) and the
    // entries from +0x24.
    void EventQueue_InsertByPriority(int queue, GuestPtr<uint8_t> id, int level, int start)
    {
        const GuestPtr<int32_t> levels{static_cast<uint32_t>(queue)};
        const uint32_t entries = static_cast<uint32_t>(queue) + 0x24u;
        const auto priority = [](uint8_t type) { return port::load<int8_t>(globals::eventTypes.get() + 0x4Cu * type + 0x22u); };
        uint32_t slot = entries + 2u * static_cast<uint32_t>(levels[level] + start);
        const uint32_t end = entries + 2u * static_cast<uint32_t>(levels[level + 1]);
        const int8_t own = priority(id[0]);
        while (slot < end && priority(port::load<uint8_t>(slot)) < own)
            slot += 2u;
        const int bytes = static_cast<int32_t>(entries + 2u * static_cast<uint32_t>(levels[8]) - slot) / 2 * 2;
        sdk::memmove(slot + 2u, slot, bytes);
        port::store<uint8_t>(slot, id[0]);
        port::store<uint8_t>(slot + 1u, id[1]);
        for (int l = level + 1; l < 9; ++l)
            ++levels[l];
    }

    // path: the distance between two tiles on the ground plane, on VU0. As in the original only x and z go into
    // the two stack vectors, so their y and w lanes (converted and subtracted too) are whatever the stack held:
    // the slots are declared to land where the original's are (entry sp - 0x10, - 0x20, - 0x30).
    ps2f Path_TileDistance(int, uint32_t tileA, uint32_t tileB)
    {
        using namespace vu0;
        port::GuestLocal<Vec4> result;
        port::GuestLocal<Vec4> to;
        port::GuestLocal<Vec4> from;
        const auto put = [](GuestPtr<Vec4> slot, uint32_t tile) {
            const int t = static_cast<int>(tile & 0xFFFFu);
            port::store<int32_t>(slot.address, t % 128);
            port::store<int32_t>(slot.address + 8u, t / 128);
        };
        put(from, tileA);
        put(to, tileB);
        vf(4) = load(from);
        vf(5) = load(to);
        set(vf(4), itof0(vf(4)), XYZW);
        set(vf(5), itof0(vf(5)), XYZW);
        set(vf(6), sub(vf(5), vf(4)), XYZW);
        set(vf(7), mul(vf(6), vf(6)), X | Z);
        set(vf(8), add(vf(7), lane(vf(7), 2)), X);
        q() = sqrtQ(x(vf(8)));
        set(vf(9), add(vf(0), _mm_set1_ps(q())), XYZW);
        store(result, vf(9));
        return result->x;
    }

    // char: whether someone with a higher index stands within 0.25 of `person` on the tile it would step onto.
    // The list is walked in index order: the first lower or equal index ends the search.
    int Person_ProbeTile(GuestPtr<CharClass>, GuestPtr<CityScene> city, GuestPtr<MapPerson> person, uint32_t tile)
    {
        port::GuestLocal<uint16_t> cursor; // declared first: the offset lands on the original's stack slot
        port::GuestLocal<Vec4> offset;
        const int t = static_cast<int32_t>(tile);
        *cursor = Map_GetTileObject(city->map, t % 128, t / 128)->firstPerson;
        while (const GuestPtr<MapPerson> other = People_Next(city->map, cursor))
        {
            if (!(person->index < other->index))
                return 0;
            if (groundDistance(*person, *other, offset) <= ps2f(0.25f))
                return 1;
        }
        return 0;
    }

    // char: whether, on the tile (x, z) or the eight around it, someone with a higher index who is outdoors and
    // whose character class has flag 1 stands within 0.8 of `person`.
    int Char_CheckNeighbourTile(GuestPtr<CharClass>, GuestPtr<CityScene> city, GuestPtr<MapPerson> person, int x, int z)
    {
        port::GuestLocal<uint16_t> cursor;
        port::GuestLocal<Vec4> offset;
        for (uint32_t i = 0; i < 9; ++i)
        {
            const int stepX = port::load<int32_t>(globals::kNeighbourStepX + 4u * i);
            const int stepZ = port::load<int32_t>(globals::kNeighbourStepZ + 4u * i);
            *cursor = Map_GetTileObject(city->map, x + stepX, z + stepZ)->firstPerson;
            for (;;)
            {
                GuestPtr<MapPerson> other;
                do
                    other = People_Next(city->map, cursor);
                while (other && (other->index == person->index || (charClassOf(*other)->flags & 1u) == 0));
                if (!other)
                    break;
                if (person->index < other->index && other->inside != kNoObject &&
                    groundDistance(*person, *other, offset) <= ps2f(0.8f))
                    return 1;
            }
        }
        return 0;
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
    PORT_FN(Camera_UpdateProjection);
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
    PORT_FN(Camera_UpdateLightMatrices);
    PORT_FN(Camera_UpdateMatrices);
    PORT_FN(Player_Draw);
    PORT_FN(EvMissionCleared_Check);
    PORT_FN(Markers_Draw);
    PORT_FN(Stamina_Tick);
    PORT_FN(CityCamera_GetZoomLevel);
    PORT_FN(Person_TargetWork);
    PORT_FN(Person_FindBuildingInList);
    PORT_FN(Player_Turn);
    PORT_FN(EvThreeUnsatisfied_Check);
    PORT_FN(Person_TickCounters);
    PORT_FN(Person_UpdatePose);
    PORT_FN(EventQueue_InsertByPriority);
    PORT_FN(Path_TileDistance);
    PORT_FN(Person_ProbeTile);
    PORT_FN(Char_CheckNeighbourTile);
}
