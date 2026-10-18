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
        port::GuestFrame<0x20> frame; // Map_SampleHeight's stack vectors land where they did in the original
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
        Instance_DrawModelLit(player->model, GuestPtr<Vec4>{player.address + 0x20u}, player->mode, city->bag->unk_002);
    }

    // char: the player's animation (+0x38) from its movement and the pad. Standing still: 1 when at least 0.4
    // above the water level, else 2 while approval is below 0, else 0. Moving, by direction (pad bits
    // or the player's +0x08 code): 0x400000, 0x4000 or code 2 gives 5; 0x800000 or code 5 gives 7; 0x200000
    // or code 6 gives 6; 0x100000, 0x1000 or code 1 gives 4; plus 8 while walking (mode 1) with pad 0x40 held,
    // else plus 4 when at least 0.4 above the level. A changed animation restarts at frame 0 (+0x34); frames
    // advance by 1 and wrap to 0, or to a fifth of the length (mm_A3D_GetMeshField0C) for animations 4 and up.
    // Walking with pad 0x40 held while moving plays sound 5.
    void Player_UpdateAction(GuestPtr<Player> player, GuestPtr<Vec4> move, GuestPtr<CityScene> city)
    {
        const bool high = !(player->position.y < ps2f(0.4f) + city->waterLevel);
        const auto still = [&] { return move->x == ps2f(0.0f) && move->z == ps2f(0.0f); };
        int anim = 0;
        if (still())
            anim = high ? 1 : (city->approval < 0 ? 2 : 0);
        else
        {
            const auto pick = [&](int base) {
                if (player->mode == 1 && (Pad_Held(0) & 0x40u) != 0)
                    return base + 8;
                return high ? base + 4 : base;
            };
            if ((Pad_Held(0) & 0x400000u) != 0 || (Pad_Held(0) & 0x4000u) != 0 || player->step == 2)
                anim = pick(5);
            else if ((Pad_Held(0) & 0x800000u) != 0 || player->step == 5)
                anim = pick(7);
            else if ((Pad_Held(0) & 0x200000u) != 0 || player->step == 6)
                anim = pick(6);
            else if ((Pad_Held(0) & 0x100000u) != 0 || (Pad_Held(0) & 0x1000u) != 0 || player->step == 1)
                anim = pick(4);
        }
        if (player->swayArg != anim)
        {
            player->swayArg = anim;
            player->swayPhase = 0.0f;
        }
        const ps2f length = A3D_GetMeshField0C(player->model, player->swayArg);
        player->swayPhase = player->swayPhase + ps2f(1.0f);
        if (!(player->swayPhase < length))
            player->swayPhase = player->swayArg < 4 ? ps2f(0.0f) : length / ps2f(5.0f);
        if (player->mode != 1 || (Pad_Held(0) & 0x40u) == 0 || still())
            return;
        Sound_PlaySe(5, 0, 0x3C, 0.0f, 0.0f);
    }

    // char: the player's footsteps (mm_Se_PlayVariant), on frames 10 and 30 of animations 4-7 and frame 10 of
    // animations 8-11 (3), and only on the ground (jump speed 0..0.01). For 4-7, at or above the water level
    // gives 4; below it the object on the tile decides: a road (type 4) or a type with flag 0x40 gives 0, type 2
    // with +0x14 at 1..9 gives 2, anything else 1. In snow (weather 3) from 3:00 to 23:00, 0 becomes 1.
    void Player_PlayFootstep(GuestPtr<Player> player, GuestPtr<CityScene> city)
    {
        const int anim = player->swayArg;
        uint8_t variant;
        if (anim >= 4 && anim < 8)
        {
            if (!(player->swayPhase == ps2f(10.0f)) && !(player->swayPhase == ps2f(30.0f)))
                return;
            variant = 4;
            if (player->position.y < city->waterLevel)
            {
                const int z = sdk::fptosi(ps2f(64.0f) - player->position.z);
                const int x = sdk::fptosi(ps2f(64.0f) + player->position.x);
                const GuestPtr<MapObject> obj = Map_GetTileObject(city->map, x, z);
                const uint16_t type = obj->type;
                const uint8_t kind = static_cast<uint8_t>(obj->entranceTile);
                if (type == 4 || (GuestPtr<BuildingType>{globals::buildingTypeAddress(type)}->flags & 0x40u) != 0)
                    variant = 0;
                else
                    variant = type == 2 && kind != 0 && kind < 10 ? 2 : 1;
            }
            if (city->weatherToday == 3 && variant == 0 && city->clock >= 10800 && city->clock <= 82800)
                variant = 1;
        }
        else if (anim >= 8 && anim < 12 && player->swayPhase == ps2f(10.0f))
            variant = 3;
        else
            return;
        if (player->jumpSpeed < ps2f(0.0f) || !(player->jumpSpeed <= ps2f(0x1.47AE14p-7f)))
            return;
        Se_PlayVariant(variant);
    }

    // char: what the player points at. A kept target is dropped when it is gone (an empty record, a person not
    // found on its tile or inside a building with flag 0x40) or after 30 frames more than pi/8 off the
    // player's heading. Then a ray steps from the player along the heading (0x1B0930 turned by it) towards a
    // point 8 ahead, tile by tile, until it leaves the map or reaches that point's tile: an object of a
    // type with flag 0x40 it hits (mm_MapObj_RayHit) becomes the target; on other tiles, while walking (mode
    // 0), the nearest resident whose distance to the ray is at most 0.3 and who is less than 10 away. The
    // city cursor is then set to the target.
    void Player_PickTarget(GuestPtr<Player> player, GuestPtr<CityScene> city)
    {
        using namespace vu0;
        port::GuestFrame<0x170> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<Vec4> aim{sp + 0x90u}, from{sp + 0xB0u}, to{sp + 0xC0u}, walk{sp + 0xD0u}, hit{sp + 0xE0u};
        const GuestPtr<Vec4> turn{sp + 0xF0u}, step{sp + 0x130u};
        const GuestPtr<uint32_t> hitData{sp + 0x140u};
        const GuestPtr<ps2f> distance{sp + 0x168u};
        const GuestPtr<uint16_t> cursor{sp + 0x16Eu};

        if (globals::pickTimerStarted.get() == 0)
        {
            globals::pickOffFrames.set(0);
            globals::pickTimerStarted.set(1);
        }
        const auto dropTarget = [&] {
            player->target = -1;
            globals::pickOffFrames.set(0);
        };
        bool checkAim = false;
        if (player->target != -1)
        {
            if (player->targetIsPerson == 0)
            {
                const GuestPtr<MapObject> obj = Map_GetRecord(city->map, player->target);
                if (obj->type == 0)
                    dropTarget();
                else
                {
                    const ps2f half = fromInt(GuestPtr<BuildingType>{globals::buildingTypeAddress(obj->type)}->size) / ps2f(2.0f);
                    aim->x = static_cast<float>((fromInt(obj->tile & 0x7F) - ps2f(64.0f)) + half);
                    port::store<uint32_t>(aim.address + 4u, 0);
                    aim->z = static_cast<float>((fromInt(128 - (obj->tile >> 7)) - ps2f(64.0f)) - half);
                    aim->w = 1.0f;
                }
                // A target found empty still takes the heading test, on the vector left in the frame.
                checkAim = true;
            }
            else
            {
                const GuestPtr<MapPerson> person = city->map->people + player->target;
                const int tx = sdk::fptosi(ps2f(64.0f) + person->x);
                const int tz = sdk::fptosi(ps2f(64.0f) - person->z);
                const GuestPtr<MapObject> obj = Map_FindPersonObjectOnTile(city->map, person->index, static_cast<uint32_t>(tx + tz * 128) & 0xFFFFu);
                if (!obj || (GuestPtr<BuildingType>{globals::buildingTypeAddress(obj->type)}->flags & 0x40u) != 0)
                    dropTarget();
                else
                {
                    aim->x = person->x;
                    port::store<uint32_t>(aim.address + 4u, 0);
                    aim->z = person->z;
                    aim->w = 1.0f;
                    checkAim = true;
                }
            }
        }
        if (checkAim)
        {
            const GuestPtr<Vec4> direction{sp + 0xA0u};
            vf(4) = load(aim);
            vf(5) = load(GuestPtr<Vec4>{player.address + 0x10u});
            set(vf(4), sub(vf(4), vf(5)), XYZW);
            vf(1) = vf(4);
            set(vf(2), mul(vf(1), vf(1)), XYZW);
            set(vf(2), add(vf(2), lane(vf(2), 1)), X);
            set(vf(2), add(vf(2), lane(vf(2), 2)), X);
            q() = rsqrt(x(vf(2)));
            set(vf(2), mul(vf(1), _mm_set1_ps(q())), XYZ);
            store(direction, vf(2));
            const ps2f angle = sdk::atan2f(direction->x, direction->z);
            const ps2f off = (ps2f(0x1.921FB6p+0f) + (-angle)) - player->heading;
            if (off <= ps2f(-0x1.921FB6p-2f) || !(off < ps2f(0x1.921FB6p-2f)))
            {
                globals::pickOffFrames.set(globals::pickOffFrames.get() + 1);
                if (globals::pickOffFrames.get() >= 30)
                    dropTarget();
            }
        }

        sdk::sceVu0RotMatrixY(turn.address, globals::kIdentityMatrix, player->heading);
        from->x = player->position.x;
        port::store<uint32_t>(from.address + 4u, 0);
        from->z = player->position.z;
        from->w = 1.0f;
        port::store<uint32_t>(to.address, 0);
        port::store<uint32_t>(to.address + 4u, 0);
        to->z = -8.0f;
        to->w = 1.0f;
        Math_MulMatVecVu0(to, turn, to);
        vf(4) = load(to);
        vf(5) = load(from);
        set(vf(4), add(vf(4), vf(5)), XYZW);
        store(to, vf(4));
        *step = *GuestPtr<Vec4>{globals::kPickStep};
        Math_MulMatVecVu0(step, turn, step);
        *walk = *GuestPtr<Vec4>{player.address + 0x10u};
        const int endX = sdk::fptosi(ps2f(64.0f) + to->x);
        const int endZ = sdk::fptosi(ps2f(64.0f) - to->z);

        GuestPtr<MapObject> last{0};
        for (;;)
        {
            walk->x = static_cast<float>(ps2f(walk->x) + step->x);
            walk->z = static_cast<float>(ps2f(walk->z) + step->z);
            const int tx = sdk::fptosi(ps2f(64.0f) + walk->x);
            const int tz = sdk::fptosi(ps2f(64.0f) - walk->z);
            if (tx < 0 || tx >= 0x80 || tz < 0 || tz >= 0x80 || (tx == endX && tz == endZ))
                break;
            const GuestPtr<MapObject> obj = Map_GetTileObject(city->map, tx, tz);
            if (last == obj)
                continue;
            last = obj;
            const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(obj->type)};
            if (btype->kind == 0x118)
                continue;
            if ((btype->flags & 0x40u) != 0)
            {
                if (MapObj_RayHit(city->map, obj->tile, static_cast<int>(from.address), static_cast<int>(to.address),
                                  static_cast<int>(hitData.address)) == 0)
                    continue;
                player->target = obj->id;
                player->targetIsPerson = 0;
                break;
            }
            if (player->mode != 0)
                continue;
            *cursor = obj->firstPerson;
            ps2f best = 10.0f;
            uint16_t nearest = kNoPerson;
            while (const GuestPtr<MapPerson> person = People_Next(city->map, cursor))
            {
                const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
                if ((cls->flags & 1u) == 0)
                {
                    aim->x = person->x;
                    port::store<uint32_t>(aim.address + 4u, 0);
                    aim->z = person->z;
                    aim->w = 1.0f;
                    Math_RayPlaneDistance(from, to, aim, hit, distance);
                    if (*distance <= ps2f(0x1.333334p-2f))
                    {
                        vf(4) = load(from);
                        vf(5) = load(hit);
                        set(vf(6), sub(vf(5), vf(4)), XYZW);
                        set(vf(7), mul(vf(6), vf(6)), XYZ);
                        set(vf(8), add(vf(7), lane(vf(7), 1)), X);
                        set(vf(8), add(vf(8), lane(vf(7), 2)), X);
                        q() = sqrtQ(x(vf(8)));
                        set(vf(9), add(vf(0), _mm_set1_ps(q())), XYZW);
                        const ps2f along = x(vf(9));
                        *distance = along;
                        if (!(best <= along))
                        {
                            nearest = person->index;
                            best = along;
                        }
                    }
                }
                *cursor = person->nextOnTile;
            }
            if (nearest == kNoPerson)
                continue;
            player->target = nearest;
            player->targetIsPerson = 1;
            break;
        }
        CityCursor_SetTarget(city->cursor, static_cast<uint16_t>(player->target), player->targetIsPerson, to);
    }

    // char: the player's grid steps. With no step under way and pad 0x20 not held, the pad starts one, each test
    // in turn and the last match winning: Up or 0x100000 forwards (1), Down or 0x400000 back (2), 0x200000 (6)
    // and 0x800000 (5) to the sides, each lasting 2 / speed frames; Right (3) and Left (4) turn for 32 frames.
    // With no step under way the position also eases onto the middle of its 2 x 2 block (at most the speed per
    // frame) and the heading onto the nearest quarter turn (pi/64 per frame). The step then moves the move
    // vector by the speed along z or x, or turns the heading by pi/64 (kept within -pi..pi), until its frames
    // run out.
    void Player_Move(GuestPtr<Player> player, GuestPtr<Vec4> move)
    {
        // The float constants of the original: pi and its quarters, and the turn step pi/64.
        const ps2f pi = 0x1.921FB6p+1f, halfPi = 0x1.921FB6p+0f, quarterPi = 0x1.921FB6p-1f, threeQuarterPi = 0x1.2D97C8p+1f;
        const ps2f twoPi = 0x1.921FB6p+2f, turn = 0x1.921FB6p-5f, zero = 0.0f;
        if (player->step == 0)
        {
            if ((Pad_Held(0) & 0x20u) == 0)
            {
                const auto start = [&](int step) {
                    player->stepFrames = ps2f(2.0f) / player->moveSpeed;
                    player->step = step;
                };
                if ((Pad_Held(0) & 0x1000u) != 0 || (Pad_Held(0) & 0x100000u) != 0) // up
                    start(1);
                if ((Pad_Held(0) & 0x4000u) != 0 || (Pad_Held(0) & 0x400000u) != 0) // down
                    start(2);
                if ((Pad_Held(0) & 0x200000u) != 0)
                    start(6);
                if ((Pad_Held(0) & 0x800000u) != 0)
                    start(5);
                if ((Pad_Held(0) & 0x2000u) != 0) // right
                {
                    player->step = 3;
                    player->stepFrames = 32.0f;
                }
                if ((Pad_Held(0) & 0x8000u) != 0) // left
                {
                    player->step = 4;
                    player->stepFrames = 32.0f;
                }
            }
            const ps2f centre = 64.0f, half = 0.5f;
            const auto toFloat = [](int v) { return ps2f(static_cast<float>(v)); };
            const ps2f x = player->position.x;
            const ps2f targetX = (half + (toFloat(sdk::fptosi(centre + x) | 1) - centre)) - half;
            const int column = sdk::fptosi(centre - player->position.z) | 1;
            const ps2f targetZ = half + ((toFloat(128 - column) - centre) - half);
            const ps2f speed = player->moveSpeed;
            const ps2f dx = x - targetX;
            if (!(dx <= speed))
                player->position.x = player->position.x - speed;
            else if (dx <= -speed)
                player->position.x = player->position.x + speed;
            else
                player->position.x = targetX;
            const ps2f z = player->position.z;
            const ps2f zSpeed = player->moveSpeed;
            const ps2f dz = z - targetZ;
            if (!(dz <= zSpeed))
                player->position.z = z - zSpeed;
            else if (dz <= -zSpeed)
                player->position.z = z + zSpeed;
            else
                player->position.z = targetZ;

            const ps2f h = player->heading;
            ps2f heading;
            if (!(h <= threeQuarterPi))
                heading = pi - h <= turn ? -pi : h + turn;
            else if (!(h <= halfPi))
                heading = h - halfPi <= turn ? halfPi : h - turn;
            else if (!(h <= quarterPi))
                heading = halfPi - h <= turn ? halfPi : h + turn;
            else if (!(h <= zero))
                heading = h <= turn ? zero : h - turn;
            else if (!(h <= -quarterPi))
                heading = h < -turn ? h + turn : zero;
            else if (!(h <= -halfPi))
                heading = halfPi + h <= turn ? -halfPi : h - turn;
            else if (!(h <= -threeQuarterPi))
                heading = -halfPi - h <= turn ? -halfPi : h + turn;
            else
                heading = pi + h <= turn ? -pi : h - turn;
            player->heading = heading;
        }
        const auto countDown = [&] {
            const ps2f left = player->stepFrames - ps2f(1.0f);
            player->stepFrames = left;
            if (left <= zero)
            {
                player->stepFrames = zero;
                player->step = 0;
            }
        };
        switch (player->step)
        {
        case 1:
            move->z = move->z - player->moveSpeed;
            countDown();
            break;
        case 2:
            move->z = move->z + player->moveSpeed;
            countDown();
            break;
        case 3:
        {
            const ps2f h = player->heading + turn;
            player->heading = h;
            if (!(h < pi))
                player->heading = h - twoPi;
            countDown();
            break;
        }
        case 4:
        {
            const ps2f h = player->heading - turn;
            player->heading = h;
            if (h < -pi)
                player->heading = h + twoPi;
            countDown();
            break;
        }
        case 5:
            move->x = move->x + player->moveSpeed;
            countDown();
            break;
        case 6:
            move->x = move->x - player->moveSpeed;
            countDown();
            break;
        default:
            break;
        }
    }

    // A camera's defaults: three light directions and colours with an ambient level (and their matrices, as
    // mm_Camera_UpdateLightMatrices, which the original has inline), the projection (distance 768, scale 1 by
    // 0.47, centre 2048, depth 1 to 16777000, near 0.01, far 50, view 2046 by 2046), an eye at (0, 0, -5)
    // looking at the origin with y up (view and world-to-screen matrices), fog from 24 to 28 and the VU1
    // packet. The original's frame is reserved whole: the view direction is its slot at sp + 0x20.
    GuestPtr<Camera> Camera_ctor(GuestPtr<Camera> camera)
    {
        using namespace vu0;
        port::GuestFrame<0x30> frame;
        const GuestPtr<Vec4> direction{frame.ptr().address + 0x20u};
        const auto at = [&](const Vec4 &v) { return GuestPtr<Vec4>::of(v).address; };
        const auto set4 = [](Vec4 &v, float x, float y, float z, float w) {
            v.x = x;
            v.y = y;
            v.z = z;
            v.w = w;
        };
        BaseObject_ctor(static_cast<int>(camera.address));
        set4(camera->lightDir[0], 0.0f, 1.5f, 0.5f, 1.0f);
        set4(camera->lightDir[1], 1.5f, -0.5f, 0.5f, 1.0f);
        set4(camera->lightDir[2], -1.5f, 0.5f, 0.5f, 1.0f);
        set4(camera->lightRgb[0], 0.5f, 0.5f, 0.5f, 1.0f);
        set4(camera->lightRgb[1], 0.2f, 0.2f, 0.2f, 1.0f);
        set4(camera->lightRgb[2], 0.2f, 0.2f, 0.2f, 1.0f);
        set4(camera->ambient, 0.45f, 0.45f, 0.45f, 1.0f);
        Camera_UpdateLightMatrices(camera);
        camera->distance = 768.0f;
        camera->scaleX = 1.0f;
        camera->scaleY = 0.47f;
        camera->centreX = 2048.0f;
        camera->centreY = 2048.0f;
        camera->depthMax = 1.0f;
        camera->depthMin = 16777000.0f;
        camera->zNear = 0.01f;
        camera->zFar = 50.0f;
        camera->viewWidth = 2046.0f;
        camera->viewHeight = 2046.0f;
        camera->unk_1D8 = 1.0f;
        camera->unk_1DC = 10.0f;
        Camera_UpdateProjection(camera);
        set4(camera->eye, 0.0f, 0.0f, -5.0f, 1.0f);
        set4(camera->target, 0.0f, 0.0f, 0.0f, 1.0f);
        set4(camera->up, 0.0f, 1.0f, 0.0f, 1.0f);
        vf(4) = load(GuestPtr<Vec4>::of(camera->target));
        vf(5) = load(GuestPtr<Vec4>::of(camera->eye));
        set(vf(4), sub(vf(4), vf(5)), XYZW);
        store(direction, vf(4));
        Vec3_Normalize(direction, direction);
        sdk::sceVu0CameraMatrix(at(camera->view[0]), at(camera->eye), direction.address, at(camera->up));
        sdk::sceVu0MulMatrix(at(camera->worldScreenA[0]), at(camera->screenA[0]), at(camera->view[0]));
        sdk::sceVu0MulMatrix(at(camera->worldScreenB[0]), at(camera->screenB[0]), at(camera->view[0]));
        camera->fogNear = 24.0f;
        camera->fogFar = 28.0f;
        set4(camera->fogA, 0.0f, 0.0f, 0.0f, -1530.0f);
        set4(camera->fogB, 0.0f, 0.0f, 0.0f, 42840.0f);
        Camera_BuildPacket(camera);
        return camera;
    }

    // The city's lights and sky by the clock, each frame. The tables hold twelve two-hour slots per day (and
    // twelve more for weather other than clear); every colour is blended between this slot's and the next
    // slot's entry by the seconds t into the slot, ((7200 - t) x this + t x next) / 7200. Lights 0 and 1 point along (0, 1, 0) and (0, -1, 0) turned by the day's angle about z and pi/8
    // about x, light 2 straight up in 0.2 grey, and the light matrices follow. The sky packet (from +0x340)
    // gets its two colours (x 256 as GS bytes), its horizon from the view's pitch, its scroll from a frame
    // counter and the view's heading, and a shimmering grey. The original's frame is reserved whole.
    void CityCamera_UpdateTimeOfDay(GuestPtr<CityCamera> camera, GuestPtr<CityScene> city)
    {
        using namespace vu0;
        port::GuestFrame<0x170> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<Vec4> dir0{sp + 0xC0u}, dir1{sp + 0xD0u}, turn{sp + 0xE0u}, tilt{sp + 0x120u}, view{sp + 0x160u};
        const GuestPtr<Vec4> identity{globals::kIdentityMatrix};
        // The float constants of the original: pi, 2 pi, pi/8, the scroll step (2 pi per 69120 frames) and 3/pi.
        const ps2f pi = 0x1.921FB6p+1f, twoPi = 0x1.921FB6p+2f, eighthPi = 0x1.921FB6p-2f;
        const ps2f scrollStep = 0x1.7D45E4p-14f, toUnits = 0x1.E8EC8Ap-1f, one = 1.0f;
        if (globals::timeOfDayStarted.get() == 0)
        {
            globals::timeOfDayFrames.set(0);
            globals::timeOfDayStarted.set(1);
        }
        globals::timeOfDayFrames.set(globals::timeOfDayFrames.get() + 1);
        const ps2f slotLength = 7200.0f;
        const ps2f t = sdk::fmodf(fromInt(city->clock), slotLength);
        int slot = city->clock / 7200;
        int next = slot + 1;
        if (!(next < 12))
            next -= 12;
        if (city->weatherToday != 0)
        {
            slot += 12;
            next += 12;
        }
        const auto blend = [&](uint32_t table, uint32_t lane) {
            const ps2f now = port::load<float>(table + 12u * static_cast<uint32_t>(slot) + 4u * lane);
            const ps2f later = port::load<float>(table + 12u * static_cast<uint32_t>(next) + 4u * lane);
            return ((slotLength - t) * now + t * later) / slotLength;
        };
        const auto setColour = [&](Vec4 &out, uint32_t table) {
            out.x = blend(table, 0);
            out.y = blend(table, 1);
            out.z = blend(table, 2);
            out.w = one;
        };

        *dir0 = *GuestPtr<Vec4>{globals::kSunDirections};
        *dir1 = *GuestPtr<Vec4>{globals::kSunDirections + 0x10u};
        sdk::sceVu0RotMatrixZ(turn.address, identity.address, (twoPi * fromInt(city->clock)) / ps2f(86400.0f) - pi);
        sdk::sceVu0RotMatrixX(tilt.address, identity.address, eighthPi);
        Mat4_MulVu0(turn, tilt, turn);
        Math_MulMatVecVu0(dir0, turn, dir0);
        Math_MulMatVecVu0(dir1, turn, dir1);
        Camera &base = camera->base;
        base.lightDir[0] = *dir0;
        setColour(base.lightRgb[0], globals::kLightColourBySlot0);
        base.lightDir[1] = *dir1;
        setColour(base.lightRgb[1], globals::kLightColourBySlot1);
        base.lightDir[2].x = 0.0f;
        base.lightDir[2].y = one;
        base.lightDir[2].z = 0.0f;
        base.lightDir[2].w = one;
        base.lightRgb[2].x = 0.2f;
        base.lightRgb[2].y = 0.2f;
        base.lightRgb[2].z = 0.2f;
        base.lightRgb[2].w = one;
        setColour(base.ambient, globals::kAmbientBySlot);
        Camera_UpdateLightMatrices(GuestPtr<Camera>{camera.address});

        const auto skyByte = [&](uint32_t table, uint32_t lane) { return sdk::fptoui(ps2f(256.0f) * blend(table, lane)); };
        const uint32_t topR = skyByte(globals::kSkyColourBySlotA, 0);
        const uint32_t topG = skyByte(globals::kSkyColourBySlotA, 1);
        const uint32_t topB = skyByte(globals::kSkyColourBySlotA, 2);
        const uint32_t lowR = skyByte(globals::kSkyColourBySlotB, 0);
        const uint32_t lowG = skyByte(globals::kSkyColourBySlotB, 1);
        const uint32_t lowB = skyByte(globals::kSkyColourBySlotB, 2);
        const uint32_t sky = camera.address;
        const uint32_t lowColour = lowR | lowG << 8 | lowB << 16 | 0x80000000u;
        port::store<uint32_t>(sky + 0x340u, topR | topG << 8 | topB << 16 | 0x80000000u);
        port::store<uint32_t>(sky + 0x370u, lowColour);
        LightSet_SetField20(GuestPtr<LightSet>{camera.address}, port::load<int32_t>(sky + 0x370u)); // into the camera's own packet

        vf(4) = load(GuestPtr<Vec4>::of(base.target));
        vf(5) = load(GuestPtr<Vec4>::of(base.eye));
        set(vf(4), sub(vf(4), vf(5)), XYZW);
        store(view, vf(4));
        Vec3_Normalize(view, view);
        int horizon = static_cast<int>(0u - static_cast<uint32_t>(sdk::fptosi(ps2f(2048.0f) * view->y)));
        if (horizon < -0x400)
            horizon = -0x400;
        else if (!(horizon < 0x201))
            horizon = 0x200;
        const uint32_t high = static_cast<uint32_t>(horizon) << 16;
        port::store<uint32_t>(sky + 0x350u, high + 0x77006C00u);
        port::store<uint32_t>(sky + 0x360u, high + 0x77009400u);
        port::store<uint32_t>(sky + 0x380u, high + 0x7E006C00u);
        port::store<uint32_t>(sky + 0x390u, high + 0x7E009400u);

        const int scroll = globals::timeOfDayFrames.get() % 0x10E00;
        const ps2f heading = sdk::atan2f(-view->x, -view->z);
        ps2f angle = (scrollStep * fromInt(scroll) - pi) + heading;
        if (!(angle <= pi))
            angle = angle - twoPi;
        else if (angle <= -pi)
            angle = angle + twoPi;
        const ps2f scrollUnits = toUnits * angle;
        port::store<float>(sky + 0x420u, static_cast<float>(scrollUnits));
        port::store<float>(sky + 0x440u, static_cast<float>(one + scrollUnits));
        const uint32_t lift = static_cast<uint32_t>(sdk::fptosi(ps2f(4096.0f) * -view->y)) << 16;
        port::store<uint32_t>(sky + 0x430u, lift + 0x70806A00u);
        port::store<uint32_t>(sky + 0x450u, lift + 0x81009400u);
        port::store<uint32_t>(sky + 0x4E0u, lift + 0x70806A00u);
        port::store<uint32_t>(sky + 0x500u, lift + 0x81009400u);
        port::store<uint32_t>(sky + 0x3F0u, lowColour);
        const ps2f turnUnits = toUnits * heading;
        port::store<float>(sky + 0x4D0u, static_cast<float>(turnUnits));
        port::store<float>(sky + 0x4F0u, static_cast<float>(one + turnUnits));
        int shimmer = globals::timeOfDayFrames.get() % 30;
        if (!(shimmer < 15))
            shimmer = 30 - shimmer;
        const uint32_t grey = 0xEAu - (topB + static_cast<uint32_t>(shimmer) * 2u);
        port::store<uint32_t>(sky + 0x4A0u, grey << 24 | grey << 16 | grey << 8 | grey);
    }

    // The sky's three packets in the city camera, filled in each frame by mm_CityCamera_UpdateTimeOfDay: at +0x310
    // a gouraud strip of two bands (colours at +0x340 and +0x370); at +0x3D0 a blended sprite of the 512 x 128
    // 4-bit texture at block 0x3EAE (palette 0x3F2E); at +0x470 an additive sprite (ALPHA 0x48, fixed 0x30) of
    // the 256 x 128 texture at 0x3F2F (palette 0x3F6F), the usual blend (0x44) restored after it. The sprites'
    // ST and heights are rewritten later.
    void Sky_BuildPackets(GuestPtr<CityCamera> camera)
    {
        const auto tex0 = [](uint32_t block, uint32_t width, int log2W, int log2H, uint32_t palette) {
            return block | (static_cast<uint64_t>(width) << 14) | (0x14ull << 20) | (static_cast<uint64_t>(log2W & 0xF) << 26) |
                   (static_cast<uint64_t>(log2H & 0xF) << 30) | (1ull << 34) | (static_cast<uint64_t>(palette) << 37) | (1ull << 61);
        };
        const int skyW = Math_Log2(0x200), skyH = Math_Log2(0x80);
        const uint64_t skyTex = tex0(0x3EAE, 8, skyW, skyH, 0x3F2E);
        const int glowW = Math_Log2(0x100), glowH = Math_Log2(0x80);
        const uint64_t glowTex = tex0(0x3F2F, 4, glowW, glowH, 0x3F6F);
        const uint32_t words[] = {
            // +0x310: the bands
            0x1000000Au, 0, 0x11000000u, 0x5000000Au,                         // cnt 10; VIF flush, direct 10
            0x00008009u, 0x10000000u, 0x0000000Eu, 0,                         // GIF tag: 9 loops, eop, A+D
            0x0Cu, 0, 0, 0,                                                   // PRIM: gouraud triangle strip
            0x80800000u, 0x3F800000u, 0x01u, 0,                               // RGBAQ: the top colour
            0x79006C00u, 1, 0x04u, 0,                                         // XYZ2 (heights from the view's pitch)
            0x79009400u, 1, 0x04u, 0,
            0x80000000u, 0x3F800000u, 0x01u, 0,                               // RGBAQ: the low colour
            0x7E006C00u, 1, 0x04u, 0,
            0x7E009400u, 1, 0x04u, 0,
            0x87006C00u, 1, 0x04u, 0,
            0x87009400u, 1, 0x04u, 0,
            0x60000000u, 0, 0, 0,                                             // ret
            // +0x3D0: the sky texture
            0x10000008u, 0, 0x11000000u, 0x50000008u,                         // cnt 8; flush, direct 8
            0x00008007u, 0x10000000u, 0x0000000Eu, 0,                         // GIF tag: 7 loops, eop, A+D
            0x70808080u, 0x3F800000u, 0x01u, 0,                               // RGBAQ
            static_cast<uint32_t>(skyTex), static_cast<uint32_t>(skyTex >> 32), 0x06u, 0, // TEX0_1
            0x56u, 0, 0, 0,                                                   // PRIM: blended textured sprite
            0, 0, 0x02u, 0,                                                   // ST (the scroll)
            0x79006C00u, 1, 0x04u, 0,                                         // XYZ2
            0x3F800000u, 0x3F800000u, 0x02u, 0,                               // ST
            0x80009400u, 1, 0x04u, 0,                                         // XYZ2
            0x60000000u, 0, 0, 0,                                             // ret
            // +0x470: the glow
            0x1000000Au, 0, 0x11000000u, 0x5000000Au,                         // cnt 10; flush, direct 10
            0x00008009u, 0x10000000u, 0x0000000Eu, 0,                         // GIF tag: 9 loops, eop, A+D
            0x48u, 0x30u, 0x42u, 0,                                           // ALPHA_1: additive, fixed 0x30
            0xFFFFFFFFu, 0x3F800000u, 0x01u, 0,                               // RGBAQ
            static_cast<uint32_t>(glowTex), static_cast<uint32_t>(glowTex >> 32), 0x06u, 0, // TEX0_1
            0x56u, 0, 0, 0,                                                   // PRIM
            0, 0, 0x02u, 0,                                                   // ST (the heading)
            0x79006C00u, 1, 0x04u, 0,                                         // XYZ2
            0x3F800000u, 0x3F800000u, 0x02u, 0,                               // ST
            0x80009400u, 1, 0x04u, 0,                                         // XYZ2
            0x44u, 0x20u, 0x42u, 0,                                           // ALPHA_1: the usual blend
            0x60000000u, 0, 0, 0,                                             // ret
        };
        const GuestPtr<uint32_t> p{camera.address + 0x310u};
        for (int32_t i = 0; i < static_cast<int32_t>(sizeof(words) / sizeof(words[0])); ++i)
            p[i] = words[i];
    }

    // The city camera each frame (heights grow downwards). The projection is reset, then the pad steps the
    // zoom level: L1 or R1 out to 3, L2 or R2 in to 0. Each level has an eye offset in the player's frame, from
    // the clear weather table or the other one, blended over the last two hours of a day before bad weather
    // and the first two after it. At levels 2 and 3 the offset shortens while the player is below 0, down to
    // level 3's. The wanted eye stays no higher than level 3's height and at least 0.6 above the ground, and
    // the camera's eye moves towards it by at most 0.8 a frame (0.1 in mode 13): height first, rising 1.5
    // times as fast, while sinking lets the sideways step grow by 1.3. Level 0 holds the eye 0.5 above the
    // player. The target is 0.7 above the player at levels 2 and 3; at 0 and 1 it is 2 ahead of the player
    // (level 1 at that point's height), eased a fifth of the way a frame to 0.7 above the ground there (level
    // 0: two thirds of the eye's height less 0.5, if that is higher, and at once while the player jumps), and
    // at least 0.2 above zero. The fog's far distance is interpolated by the eye's height between the levels'
    // heights, its near distance 3 closer, and 14 more in bad weather (ramped over the same two hours). The
    // original's frame is reserved whole: the level table sits at sp + 0xF0 and the fog search reads past it.
    void CityCamera_Update(GuestPtr<CityCamera> camera, GuestPtr<CityScene> city)
    {
        using namespace vu0;
        port::GuestFrame<0x150> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<Vec4> playerMatrix{sp + 0x50u}, turn{sp + 0x90u}, position{sp + 0xD0u}, wanted{sp + 0xE0u};
        const GuestPtr<Vec4> levels{sp + 0xF0u}, ahead{sp + 0x130u}, distance{sp + 0x140u};
        Camera &base = camera->base;
        const GuestPtr<Camera> cam = GuestPtr<Camera>::of(base);
        base.viewWidth = 2046.0f;
        base.viewHeight = 2046.0f;
        base.unk_1D8 = 1.0f;
        base.unk_1DC = 10.0f;
        base.depthMin = 16770000.0f;
        base.zNear = 0.1f;
        Camera_UpdateProjection(cam);
        Player_GetMatrix(city->player, playerMatrix);
        if ((Pad_Pressed(0) & 0x4u) != 0 || (Pad_Pressed(0) & 0x8u) != 0)
        {
            camera->zoomLevel = camera->zoomLevel + 1;
            if (!(camera->zoomLevel < 4))
                camera->zoomLevel = 3;
        }
        if ((Pad_Pressed(0) & 0x1u) != 0 || (Pad_Pressed(0) & 0x2u) != 0)
        {
            camera->zoomLevel = camera->zoomLevel - 1;
            if (camera->zoomLevel < 0)
                camera->zoomLevel = 0;
        }

        const ps2f slotLength = 7200.0f, one = 1.0f;
        const auto copyLevels = [&](uint32_t table) {
            for (int i = 0; i < 4; ++i)
                levels[i] = GuestPtr<Vec4>{table}[i];
        };
        const auto blendLevels = [&](uint32_t from, uint32_t to, ps2f t) {
            const ps2f keep = one - t;
            for (int i = 0; i < 4; ++i)
            {
                const Vec4 &a = GuestPtr<Vec4>{from}[i], &b = GuestPtr<Vec4>{to}[i];
                levels[i].x = keep * a.x + t * b.x;
                levels[i].y = keep * a.y + t * b.y;
                levels[i].z = keep * a.z + t * b.z;
                levels[i].w = one;
            }
        };
        const bool lateBeforeWeather = city->weatherTomorrow != 0 && city->clock > 79200;
        const bool earlyAfterWeather = city->weatherYesterday != 0 && city->clock < 7200;
        if (city->weatherToday != 0)
            copyLevels(globals::kCameraLevelsWeather);
        else if (lateBeforeWeather)
            blendLevels(globals::kCameraLevelsClear, globals::kCameraLevelsWeather, fromInt(city->clock - 79200) / slotLength);
        else if (earlyAfterWeather)
            blendLevels(globals::kCameraLevelsWeather, globals::kCameraLevelsClear, fromInt(city->clock) / slotLength);
        else
            copyLevels(globals::kCameraLevelsClear);

        // The player's matrix without its translation, and its position.
        turn[0] = playerMatrix[0];
        turn[1] = playerMatrix[1];
        turn[2] = playerMatrix[2];
        store(GuestPtr<Vec4>{turn.address + 0x30u}, vf(0));
        *position = playerMatrix[3];
        const int zoom = camera->zoomLevel;
        *wanted = levels[zoom];
        if (!(zoom < 2) && position->y < ps2f(0.0f))
        {
            const ps2f z = wanted->z + position->y;
            wanted->z = z;
            if (z < levels[3].z)
                wanted->z = levels[3].z;
        }
        Math_MulMatVecVu0(wanted, turn, wanted);
        vf(4) = load(position);
        vf(5) = load(wanted);
        set(vf(4), add(vf(4), vf(5)), XYZW);
        store(wanted, vf(4));
        if (wanted->y < levels[3].y)
            wanted->y = levels[3].y;
        const ps2f eyeGround = Map_SampleHeight(city->map, wanted->x, wanted->z) - ps2f(0.6f);
        if (!(wanted->y <= eyeGround))
            wanted->y = eyeGround;

        ps2f step = 0.8f;
        if (CityScene_GetMode(city) == 13)
            step = 0.1f;
        vf(4) = load(GuestPtr<Vec4>::of(base.eye));
        vf(5) = load(wanted);
        set(vf(6), sub(vf(5), vf(4)), XYZW);
        set(vf(7), mul(vf(6), vf(6)), XYZ);
        set(vf(8), add(vf(7), lane(vf(7), 1)), X);
        set(vf(8), add(vf(8), lane(vf(7), 2)), X);
        q() = sqrtQ(x(vf(8)));
        set(vf(9), add(vf(0), _mm_set1_ps(q())), XYZW);
        store(distance, vf(9));
        if (distance->x <= step)
            step = distance->x;
        if (zoom == 0)
            base.eye.y = playerMatrix[3].y - ps2f(0.5f);
        else
        {
            const ps2f y = base.eye.y;
            if (!(y <= wanted->y))
            {
                const ps2f risen = y - ps2f(1.5f) * step;
                base.eye.y = risen;
                if (risen < wanted->y)
                    base.eye.y = wanted->y;
            }
            else if (y < wanted->y)
            {
                const ps2f sunk = y + step;
                base.eye.y = sunk;
                if (!(sunk <= wanted->y))
                    base.eye.y = wanted->y;
                step = step * ps2f(1.3f);
            }
        }
        const auto approach = [&](ps2f &value, ps2f goal) {
            const ps2f v = value;
            if (!(v <= goal))
            {
                value = v - step;
                if (value < goal)
                    value = goal;
            }
            else if (v < goal)
            {
                value = v + step;
                if (!(value <= goal))
                    value = goal;
            }
        };
        approach(base.eye.x, wanted->x);
        approach(base.eye.z, wanted->z);

        if (!(zoom < 2))
        {
            base.target = *position;
            base.target.y = base.target.y - ps2f(0.7f);
        }
        else
        {
            *ahead = *GuestPtr<Vec4>{globals::kCameraLookAhead};
            Math_MulMatVecVu0(ahead, turn, ahead);
            base.target.x = position->x + ahead->x;
            base.target.z = position->z + ahead->z;
            if (zoom == 1)
                base.target.y = position->y + ahead->y;
            const ps2f targetGround = Map_SampleHeight(city->map, base.target.x, base.target.z) - ps2f(0.7f);
            const auto ease = [&](ps2f goal) {
                const ps2f y = base.target.y;
                if (y < goal)
                {
                    const ps2f eased = y + (goal - y) / ps2f(5.0f);
                    base.target.y = eased;
                    if (!(eased <= goal))
                        base.target.y = goal;
                }
                else if (!(y <= goal))
                {
                    const ps2f eased = y + (goal - y) / ps2f(5.0f);
                    base.target.y = eased;
                    if (eased < goal)
                        base.target.y = goal;
                }
            };
            if (zoom == 0)
            {
                ps2f goal = ps2f(2.0f) * (base.eye.y / ps2f(3.0f)) - ps2f(0.5f);
                if (!(goal <= targetGround))
                    goal = targetGround;
                if (ps2f(0.0f) == city->player->jumpSpeed)
                    ease(goal);
                else
                    base.target.y = goal;
            }
            else
                ease(targetGround);
            if (!(base.target.y <= ps2f(-0.2f)))
                base.target.y = -0.2f;
        }
        Camera_UpdateMatrices(cam);

        const ps2f eyeY = base.eye.y;
        int level = 0;
        while (eyeY < levels[level + 1].y)
            ++level;
        const ps2f low = levels[level].y;
        const ps2f u = (eyeY - low) / (levels[level + 1].y - low);
        const auto fogAt = [](int i) { return ps2f(port::load<float>(globals::kFogFarByLevel + 4u * static_cast<uint32_t>(i))); };
        const ps2f fogFar = (one - u) * fogAt(level) + u * fogAt(level + 1);
        ps2f extra = 0.0f;
        if (city->weatherToday != 0)
            extra = 14.0f;
        else if (lateBeforeWeather)
            extra = fromInt(city->clock - 79200) / ps2f(512.0f);
        else if (earlyAfterWeather)
            extra = ps2f(14.0f) - fromInt(city->clock) / ps2f(512.0f);
        Camera_SetFogRange(cam, fogFar - (ps2f(3.0f) + extra), fogFar);
        CityCamera_UpdateTimeOfDay(camera, city);
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

    // char: turns a person's model to face the player. The direction from the person to the player (or, when
    // the model's matrix faces away from the player, the way its first node faces already), blended 9:1 with
    // the direction its slot keeps from before, becomes the rotation of nodes 0 and 3 of the model part chosen
    // by flags & 3 (part 3 when that is 0 and the family's bit 0 is clear). The person's slot (timer 2) keeps
    // the new direction; a person without a slot claims every free one, as in the original. The original's
    // frame is reserved whole and its vectors and matrices kept at their offsets.
    void Person_TurnToPlayer(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        using namespace vu0;
        port::GuestFrame<0xF0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<Vec4> direction{sp + 0x40u}, facing{sp + 0x50u}, playerMatrix{sp + 0x60u}, turn{sp + 0xA0u}, dot{sp + 0xE0u};
        const int pose = static_cast<int16_t>(person->flags) & 3;
        const int part = (pose != 0 || (person->family & 1u) != 0) ? pose : 3;
        const GuestPtr<A3DObject> model{cls->parts[part].address};
        Player_GetMatrix(city->player, playerMatrix);
        *direction = *GuestPtr<Vec4>{globals::kOriginPoint};
        direction->x = playerMatrix[3].x - person->x;
        direction->z = playerMatrix[3].z - person->z;
        Vec3_Normalize(direction, direction);
        *facing = *GuestPtr<Vec4>{globals::kOriginPoint};
        facing->x = model->matrix[0].z;
        facing->z = -model->matrix[0].x;
        vf(4) = load(direction);
        vf(5) = load(facing);
        set(vf(4), mul(vf(4), vf(5)), XYZ);
        set(vf(4), add(vf(4), lane(vf(4), 1)), X);
        set(vf(4), add(vf(4), lane(vf(4), 2)), X);
        store(dot, vf(4));
        const auto nodes = [&] { return port::load<uint32_t>(model->model.address + 8u); };
        if (dot->x < ps2f(0.0f))
        {
            direction->x = port::load<float>(nodes() + 0x78u);
            direction->z = -ps2f(port::load<float>(nodes() + 0x70u));
            Vec3_Normalize(direction, direction);
        }
        const auto slotPerson = [](int i) { return GuestPtr<int32_t>{globals::kCharTypeSlots + 4u * static_cast<uint32_t>(i)}; };
        const auto slotTimer = [](int i) { return GuestPtr<int32_t>{globals::kCharTypeTimers + 4u * static_cast<uint32_t>(i)}; };
        const auto slotFacing = [](int i) { return GuestPtr<Vec4>{globals::kCharTypeFacings + 0x10u * static_cast<uint32_t>(i)}; };
        int slot = -1;
        for (int i = 0; i < 25; ++i)
            if (*slotPerson(i) == person->index)
            {
                slot = i;
                break;
            }
        if (slot != -1)
        {
            const ps2f keep = 0.9f, take = 0.1f;
            direction->x = keep * slotFacing(slot)->x + take * direction->x;
            direction->z = keep * slotFacing(slot)->z + take * direction->z;
            Vec3_Normalize(direction, direction);
        }
        Mat4_Copy(turn, GuestPtr<Vec4>{globals::kIdentityMatrix});
        const ps2f dz = -direction->z;
        turn[0].x = dz;
        turn[0].z = direction->x;
        turn[2].x = -direction->x;
        turn[2].z = dz;
        for (uint32_t row = 0; row < 3; ++row)
            GuestPtr<Vec4>{nodes() + 0x70u + 0x10u * row}[0] = turn[row];
        for (uint32_t row = 0; row < 3; ++row)
            GuestPtr<Vec4>{nodes() + 0x2B0u + 0x10u * row}[0] = turn[row];
        const int index = person->index;
        if (slot != -1)
        {
            *slotPerson(slot) = index;
            *slotTimer(slot) = 2;
            *slotFacing(slot) = *direction;
            return;
        }
        for (int i = 0; i < 25; ++i)
        {
            if (*slotTimer(i) != 0)
                continue;
            *slotPerson(i) = index;
            *slotTimer(i) = 2;
            *slotFacing(i) = *direction;
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

    // char: class 2 slot +0x10, a character of this class on the terrain, while there is room for another
    // instance (fewer than 1800 queued, fewer than the limit at 0x1BD980 of the limited ones). Marks the person
    // drawn (flag 0x1000) and eases its height towards the ground: 0.1 per frame within 0.2 of it, straight
    // onto it beyond. Only a person at height 0.5 or below is drawn: lit (colour w word 12) before 4:00 and
    // from 17:00 unless +0x28 holds 6000 (compared as a person id here), as node 7 or 8 of the first model
    // part when its workplace is of type 0xAF or 0xB1, else node (own index % 7), turned by place.w about y
    // and moved to place.xyz.
    void Char2_Draw(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> person, GuestPtr<Vec4> colour,
                    int flags, GuestPtr<Vec4> place)
    {
        if (!(globals::instanceCount.get() + 1 < 0x708))
            return;
        if (!(globals::instanceLimitedCount.get() + 1 < globals::instanceLimit.get()))
            return;
        person->flags |= 0x1000u;
        port::GuestFrame<0x70> frame; // Map_SampleHeight's stack vectors land where they did in the original
        const ps2f ground = Map_SampleHeight(city->map, person->x, person->z);
        const ps2f y = person->y;
        const ps2f snap = 0.2f, ease = 0.1f;
        if (y < ground)
        {
            const ps2f raised = y + ease;
            if (!(ground - y <= snap) || !(raised <= ground))
                person->y = ground;
            else
                person->y = raised;
        }
        else if (!(y <= ground))
        {
            const ps2f lowered = y - ease;
            if (!(y - ground <= snap) || lowered < ground)
                person->y = ground;
            else
                person->y = lowered;
        }
        if (!(person->y <= ps2f(0.5f)))
            return;
        const int clock = city->clock;
        const bool night = clock >= 61200 || clock < 14400;
        port::store<int32_t>(colour.address + 0xCu, night && person->target != kNoPerson ? 12 : 0);
        const int workType = Map_GetRecord(city->map, person->work)->type;
        const int node = workType == 0xAF ? 7 : workType == 0xB1 ? 8 : person->index % 7;
        const GuestPtr<A3DObject> model{cls->parts[0].address};
        const GuestPtr<Vec4> matrix{port::load<uint32_t>(model->model.address + 8u) + static_cast<uint32_t>(node) * 0xC0u + 0x70u};
        sdk::sceVu0RotMatrixY(matrix.address, globals::kIdentityMatrix, place->w);
        matrix[3].x = place->x;
        matrix[3].y = place->y;
        matrix[3].z = place->z;
        Instance_DrawNodeLimited(GuestPtr<A3DObject>{cls->parts[0].address}, colour, node, flags);
    }

    // char: switches the player to `mode` (-1: back to the previous one) and swallows the Cross press that
    // chose it.
    void Player_SetMode(GuestPtr<Player> player, int mode)
    {
        if (mode == -1)
            mode = player->previousMode;
        player->previousMode = player->mode;
        player->mode = mode;
        player->stepFrames = 0.0f;
        player->step = 0;
        player->swayPhase = 0.0f;
        player->target = -1;
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
        laptop->state = 0;
        laptop->unk_44 = 0;
        laptop->selected = 0;
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
        stage->money = 3000;
        stage->salary = 2500;
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

    // char: a resident's home, kept in its target field. The current one stands while it is a home type (flag 0x80)
    // of the person's family with its claim meter below 120. Otherwise, around the workplace (24 offsets, 0x1B0C50
    // and 0x1B0CB0): a home of the family with the meter below 60 is taken, or, where the four tiles of the block
    // (0x1B0D10 and 0x1B0D20) are ground, wasteland or type 10 lying flat (corners within 0.05), a new home of the
    // workplace type's home type (+0x13A) is placed. Failing all, the workplace. Returns the home.
    uint32_t Person_PlaceHomeObject(GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        const auto homeOfFamily = [&](GuestPtr<MapObject> obj, int meterBelow) {
            return (GuestPtr<BuildingType>{globals::buildingTypeAddress(obj->type)}->flags & 0x80u) != 0 &&
                   obj->family == person->family && obj->claimMeter < meterBelow;
        };
        if (person->target != kNoObject && person->target != person->work &&
            homeOfFamily(Map_GetRecord(city->map, person->target), 120))
            return person->target;

        port::GuestFrame<0xD0> frame;
        const GuestPtr<ps2f> corners{frame.ptr().address + 0xC0u};
        const GuestPtr<MapObject> work = Map_GetRecord(city->map, person->work);
        const GuestPtr<BuildingType> workType{globals::buildingTypeAddress(work->type)};
        const int x = work->tile & 0x7F, z = work->tile >> 7;
        for (int i = 0; i < 24; ++i)
        {
            const int bx = x + port::load<int32_t>(0x001B0C50u + 4u * i);
            const int bz = z + port::load<int32_t>(0x001B0CB0u + 4u * i);
            const GuestPtr<MapObject> here = Map_GetTileObject(city->map, bx, bz);
            if (homeOfFamily(here, 60))
            {
                person->target = here->id;
                return here->id;
            }
            bool open = true;
            for (int k = 0; k < 4 && open; ++k)
            {
                const int tx = bx + port::load<int32_t>(0x001B0D10u + 4u * k);
                const int tz = bz + port::load<int32_t>(0x001B0D20u + 4u * k);
                const uint16_t type = Map_GetTileObject(city->map, tx, tz)->type;
                if (type != 2 && type != 3 && type != 10)
                {
                    open = false;
                    break;
                }
                Map_GetTileVertexClamped(city->map, tx, tz, corners);
                for (int c = 0; c < 4; ++c)
                    if (!(corners[c] <= ps2f(0x1.99999Ap-5f)))
                    {
                        open = false;
                        break;
                    }
            }
            if (!open)
                continue;
            const int placed = Map_PlaceObject(city->map, person->family, port::load<uint16_t>(workType.address + 0x13Au), bx, bz,
                                               static_cast<int>(city.address));
            if (placed != kNoObject)
            {
                person->target = static_cast<uint16_t>(placed);
                return static_cast<uint32_t>(placed) & 0xFFFFu;
            }
        }
        person->target = person->work;
        return person->work;
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
        player->step = 0;
        player->stepFrames = 0.0f;
        player->swayPhase = 0.0f;
        player->heading = 0.0f;
        player->position.x = 0.0f;
        player->position.y = 0.0f;
        player->position.z = 0.0f;
        player->position.w = 0.0f;
        player->visible = 0;
        player->target = -1;
        player->targetIsPerson = 0;
        player->unk_48 = 0;
        player->unk_50 = 0;
        player->jumpSpeed = 0.0f;
        player->jumps = 0;
        return player;
    }

    // phone: an empty call queue.
    // Queues a phone call from a family member, unless that family already has one queued: the first free entry
    // (family 0) of four. 1 when queued.
    int PhoneCall_Post(GuestPtr<PhoneCalls> phone, int person, uint32_t family, uint16_t member, int t0)
    {
        for (int i = 0; i < 4; ++i)
            if (phone->calls[i].family == family)
                return 0;
        for (int i = 0; i < 4; ++i)
        {
            PhoneCallEntry &call = phone->calls[i];
            if (call.family != 0)
                continue;
            call.person = person;
            call.family = static_cast<uint16_t>(family);
            call.member = member;
            call.unk_08 = t0;
            return 1;
        }
        return 0;
    }

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
        bag->size = 0;
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

    // char: a person's walk each frame. It aims at the middle of the building on the tile it walks to; when
    // following (flag 0x20) someone now on its own tile (the player when it follows no one, kept within 62 of
    // the middle), at that one instead, though as in the original the followed z lands in the aim's y, so only
    // x follows. The aim runs straight along an axis when within 0.01. On road it keeps to a pavement lane by
    // its offset across its two-tile block (0 to 2), heading pi/8 to one side or the other: one way in 0.5 to 1
    // or above 1.875, the other in 1 to 1.5 or below 0.125; in between straight on, unless the tile ahead is
    // taken (mm_Person_ProbeTile), when the quarter it is in decides. It turns by the gap scaled by its
    // distance to the aim (up to 2) over 2 (a gap past pi wrapped as the original does), taking the aim
    // outright when that is within 0.04, else turning by the gap squared over 40 but at least 0.04. It steps
    // the class's walking speed plus (index & 3) / 2000 (twice when running, flag 0x80) along its heading,
    // through the shared step vector, and a person that left `tile` is unlinked from it unless flag 0x800
    // holds it there. The original's frame is reserved whole.
    void Person_Walk(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> person, GuestPtr<MapObject> obj, int tile)
    {
        using namespace vu0;
        port::GuestFrame<0x150> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<Vec4> goal{sp + 0x80u}, here{sp + 0x90u}, way{sp + 0xA0u}, playerMatrix{sp + 0xB0u};
        const GuestPtr<Vec4> turn{sp + 0xF0u}, moved{sp + 0x130u}, distance{sp + 0x140u};
        const ps2f pi = 0x1.921FB6p+1f, twoPi = 0x1.921FB6p+2f, halfPi = 0x1.921FB6p+0f;
        const ps2f quarterPi = 0x1.921FB6p-1f, threeQuartersPi = 0x1.2D97C8p+1f;
        const ps2f eighthPi = 0x1.921FB6p-2f, threeEighthsPi = 0x1.2D97C8p+0f;
        const ps2f fiveEighthsPi = 0x1.F6A7A4p+0f, sevenEighthsPi = 0x1.5FDBC0p+1f;
        const ps2f centre = 64.0f, two = 2.0f;

        const GuestPtr<MapObject> target = Map_GetTileFirstObject(city->map, person->walkTile);
        const ps2f size = fromInt(GuestPtr<BuildingType>{globals::buildingTypeAddress(target->type)}->size);
        goal->w = 1.0f;
        goal->z = (fromInt(128 - person->walkTile / 128) - centre) - size / two;
        goal->y = 0.0f;
        goal->x = (fromInt(person->walkTile % 128) - centre) + size / two;
        if ((person->flags & 0x20u) != 0)
        {
            ps2f fx, fz;
            if (person->follow == kNoPerson)
            {
                Player_GetMatrix(city->player, playerMatrix);
                const ps2f edge = 62.0f;
                const auto clampToMap = [&](ps2f v) {
                    if (v < -edge)
                        return -edge;
                    return v <= edge ? v : edge;
                };
                fx = clampToMap(playerMatrix[3].x);
                fz = clampToMap(playerMatrix[3].z);
            }
            else
            {
                const MapPerson &followed = city->map->people[person->follow];
                fx = followed.x;
                fz = followed.z;
            }
            const int tileZ = sdk::fptosi(centre - fz);
            const int tileX = sdk::fptosi(centre + fx);
            if (Map_GetTileObject(city->map, tileX, tileZ)->tile == obj->tile)
            {
                goal->x = fx;
                goal->y = fz;
            }
        }
        here->w = 1.0f;
        here->z = person->z;
        here->y = 0.0f;
        here->x = person->x;
        vf(4) = load(goal);
        vf(5) = load(here);
        set(vf(4), sub(vf(4), vf(5)), XYZW);
        store(way, vf(4));
        const ps2f nearZero = 0.01f;
        ps2f aim = 0.0f;
        if (way->x < nearZero && !(way->x <= -nearZero))
            aim = way->z <= aim ? aim : -pi;
        else if (way->z < nearZero && !(way->z <= -nearZero))
            aim = way->x <= ps2f(0.0f) ? halfPi : -halfPi;
        else
        {
            Vec3_Normalize(way, way);
            aim = sdk::atan2f(-way->x, -way->z);
        }

        const ps2f offset = 128.0f;
        const ps2f acrossX = sdk::fmodf(offset + person->x, two);
        const ps2f acrossZ = sdk::fmodf(offset + person->z, two);
        const int blocked = Person_ProbeTile(cls, city, person, obj->tile);
        if (Map_TileIndexFlag10(city->map, obj->tile) != 0)
        {
            const auto pavement = [&](ps2f f, ps2f oneSide, ps2f otherSide, ps2f straight) {
                if ((!(f < ps2f(0.5f)) && f <= ps2f(1.0f)) || !(f <= ps2f(1.875f)))
                    return oneSide;
                if ((!(f <= ps2f(1.0f)) && f <= ps2f(1.5f)) || f < ps2f(0.125f))
                    return otherSide;
                if (blocked == 0)
                    return straight;
                if (f < ps2f(0.25f))
                    return otherSide;
                if (f < ps2f(1.0f))
                    return oneSide;
                return f < ps2f(1.75f) ? otherSide : oneSide;
            };
            if (!(aim < -quarterPi) && aim < quarterPi)
                aim = pavement(acrossX, eighthPi, -eighthPi, 0.0f);
            else if (!(aim < -quarterPi) && aim < threeQuartersPi)
                aim = pavement(acrossZ, threeEighthsPi, fiveEighthsPi, halfPi);
            else if (!(aim < -threeQuartersPi) && aim < threeQuartersPi)
                aim = pavement(acrossZ, -threeEighthsPi, -fiveEighthsPi, -halfPi);
            else
                aim = pavement(acrossX, sevenEighthsPi, -sevenEighthsPi, -pi);
        }

        vf(4) = load(goal);
        vf(5) = load(here);
        set(vf(6), sub(vf(5), vf(4)), XYZW);
        set(vf(7), mul(vf(6), vf(6)), XYZ);
        set(vf(8), add(vf(7), lane(vf(7), 1)), X);
        set(vf(8), add(vf(8), lane(vf(7), 2)), X);
        q() = sqrtQ(x(vf(8)));
        set(vf(9), add(vf(0), _mm_set1_ps(q())), XYZW);
        store(distance, vf(9));
        ps2f reach = distance->x;
        if (!(reach <= two))
            reach = two;
        ps2f gap = aim - person->heading;
        const ps2f scale = reach / two;
        if (!(gap <= pi))
            gap = pi - gap;
        else if (gap <= -pi)
            gap = -pi - gap;
        const ps2f step = gap * scale, least = 0.04f;
        if (step <= least && !(step < -least))
            person->heading = aim;
        else
        {
            ps2f amount = gap * gap / ps2f(40.0f);
            if (amount < least)
                amount = least;
            if (!(gap <= ps2f(0.0f)))
            {
                const ps2f h = person->heading + amount;
                person->heading = h;
                if (!(h < pi))
                    person->heading = h - twoPi;
            }
            else
            {
                const ps2f h = person->heading - amount;
                person->heading = h;
                if (h <= -pi)
                    person->heading = h + twoPi;
            }
        }

        sdk::sceVu0RotMatrixY(turn.address, globals::kIdentityMatrix, person->heading);
        const ps2f walkSpeed = GuestPtr<PersonClass>{cls.address}->walkSpeed;
        ps2f stride = -(walkSpeed + fromInt(person->index & 3) / ps2f(2000.0f));
        if ((person->flags & 0x80u) != 0)
            stride = stride * two;
        const GuestPtr<Vec4> walkStride{globals::kWalkStride};
        walkStride->z = stride;
        Math_MulMatVecVu0(moved, turn, walkStride);
        person->x = person->x + moved->x;
        person->z = person->z + moved->z;
        if ((person->flags & 0x800u) != 0)
            return;
        const int tileX = sdk::fptosi(centre + person->x);
        const int tileZ = sdk::fptosi(centre - person->z);
        if (static_cast<uint32_t>(tile) != static_cast<uint32_t>(tileX) + (static_cast<uint32_t>(tileZ) << 7))
            Map_UnlinkPersonFromTile(city->map, tile, person->index);
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

    namespace
    {
        // path: the distance between two tiles on the ground plane, on VU0, through three stack slots of the
        // caller's: only x and z go into `from` and `to`, so their y and w lanes (converted and subtracted too)
        // are whatever the stack held, and callers keep the slots where the original had them.
        ps2f tileDistanceIn(GuestPtr<Vec4> from, GuestPtr<Vec4> to, GuestPtr<Vec4> result, uint32_t tileA, uint32_t tileB)
        {
            using namespace vu0;
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
    }

    // path: the distance between two tiles on the ground plane (tileDistanceIn), its slots declared to land
    // where the original's are (result at entry sp - 0x10, to at - 0x20, from at - 0x30).
    ps2f Path_TileDistance(int, uint32_t tileA, uint32_t tileB)
    {
        port::GuestLocal<Vec4> result;
        port::GuestLocal<Vec4> to;
        port::GuestLocal<Vec4> from;
        return tileDistanceIn(from, to, result, tileA, tileB);
    }

    // path: the link (+0x10, a road block tile) to take from road block `from` towards road block `target`.
    // Each link of `from` is followed, turning at bends (shapes 3, 6, 9, 0xC), for up to 9 blocks; skipped
    // are the direction of the block edge the person stands within 0.05 of, and paths that reach the person's
    // previous waypoint (+0x1A), no road, or a dead end. Reaching one of `target`'s links returns that first link at once;
    // reaching a junction (shapes 7, 0xB, 0xD, 0xE, 0xF) keeps it when the junction lies nearer to one of
    // `target`'s links than any before (within 128 tiles). Returns 0x4000 when nothing qualifies. The tile
    // distance goes through this function's own slots, as in the original: from (link) at entry sp - 0x10, to
    // (junction) at - 0x20, result at - 0x30. They are declared before the first guest call so that fmodf's
    // frame stays below them, as in the original.
    uint16_t Path_PickRoadLink(GuestPtr<CharClass>, GuestPtr<CityScene> city, GuestPtr<MapObject> target, GuestPtr<MapObject> from,
                               GuestPtr<MapPerson> person)
    {
        port::GuestLocal<Vec4> linkSlot;
        port::GuestLocal<Vec4> junctionSlot;
        port::GuestLocal<Vec4> distanceSlot;
        const ps2f blockSize = 2.0f, edge = 0.05f, farEdge = 1.95f;
        const ps2f inX = sdk::fmodf(ps2f(128.0f) + person->x, blockSize);
        const ps2f inZ = sdk::fmodf(ps2f(128.0f) + person->z, blockSize);
        int skip = -1;
        if (inZ < edge)
            skip = 2;
        else if (!(inZ <= farEdge))
            skip = 0;
        else if (inX < edge)
            skip = 3;
        else if (!(inX <= farEdge))
            skip = 1;

        const auto links = [](GuestPtr<MapObject> block) { return GuestPtr<uint16_t>{block.address + 0x10u}; };
        ps2f nearest = 128.0f;
        uint16_t result = 0x4000;
        for (int dir = 0; dir < 4; ++dir)
        {
            if (dir == skip)
                continue;
            uint16_t tile = links(from)[dir];
            int heading = dir;
            for (int step = 1; step < 10; ++step)
            {
                if (tile == 0x4000 || tile == person->previousTile)
                    break;
                if (Map_TileIndexFlag10(city->map, tile) == 0)
                    break;
                const GuestPtr<MapObject> block = Map_GetTileFirstObject(city->map, tile);
                for (int i = 0; i < 4; ++i)
                    if (links(target)[i] == tile)
                        return links(from)[dir];
                const int shape = block->variant;
                if (shape == 7 || shape == 0xB || shape == 0xD || shape == 0xE || shape == 0xF)
                {
                    for (int i = 0; i < 4; ++i)
                    {
                        const uint16_t link = links(target)[i];
                        if (link == 0x4000)
                            continue;
                        const ps2f distance = tileDistanceIn(linkSlot, junctionSlot, distanceSlot, link, tile);
                        if (distance < nearest)
                        {
                            result = links(from)[dir];
                            nearest = distance;
                        }
                    }
                    break;
                }
                if (shape == 3)
                    heading = heading == 2 ? 1 : heading == 3 ? 0 : heading;
                else if (shape == 6)
                    heading = heading == 0 ? 1 : heading == 3 ? 2 : heading;
                else if (shape == 9)
                    heading = heading == 1 ? 0 : heading == 2 ? 3 : heading;
                else if (shape == 0xC)
                    heading = heading == 0 ? 3 : heading == 1 ? 2 : heading;
                else
                    break;
                tile = links(block)[heading];
            }
        }
        return result;
    }

    // path: the next waypoint for `person` at `tile`, towards the building it is bound for (+0x16). Each person
    // gets one every eighth frame (index % 8 = frame counter % 8); other frames return 0x4000 and change nothing.
    // The waypoint becomes its +0x18 and the old one its +0x1A, except that on a bend +0x1A stays.
    //  - On a road, to a building with people inside (type flag 0x40) and a road entrance: the building itself
    //    from the entrance block; the entrance block when it links to this one; else this block's link that the
    //    entrance block links to as well (the entrance block when the two line up in x or z); else
    //    mm_Path_PickRoadLink's link, or the entrance block when it finds none.
    //  - On a road, to any other building: this block's link (not back to +0x1A) nearest the building, when
    //    nearer than this block; else the building.
    //  - Off the road: a building with people inside and a road entrance is left by it; within 8 tiles the
    //    building itself; else the first road within two block steps (by distance, then direction), or the first
    //    tile of another object stepping diagonally towards the building.
    // The original's frame is reserved whole: the tile distances use its slots and those of the callees.
    uint16_t Path_NextWaypoint(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> person, int tile)
    {
        port::GuestFrame<0xF0> frame;
        if ((person->index & 7) != (globals::frameCounter.get() & 7))
            return 0x4000;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<MapObject> dest = Map_GetRecord(city->map, person->inside);
        const auto links = [](GuestPtr<MapObject> block) { return GuestPtr<uint16_t>{block.address + 0x10u}; };
        const auto linksTo = [&](GuestPtr<MapObject> block, uint16_t to) {
            for (int i = 0; i < 4; ++i)
                if (links(block)[i] == to)
                    return true;
            return false;
        };
        const auto peopleInside = [](GuestPtr<MapObject> obj) { return (globals::buildingTypeFlags(obj->type) & 0x40u) != 0; };
        const auto advance = [&](uint16_t waypoint) {
            person->previousTile = person->walkTile;
            person->walkTile = waypoint;
            return waypoint;
        };

        GuestPtr<MapObject> here = Map_GetTileFirstObject(city->map, tile);
        if (Map_TileIndexFlag10(city->map, static_cast<uint32_t>(tile)) != 0)
        {
            if (peopleInside(dest) && Map_TileIndexFlag10(city->map, dest->entranceTile) != 0)
            {
                const GuestPtr<MapObject> entrance = Map_GetTileFirstObject(city->map, dest->entranceTile);
                uint16_t waypoint;
                if (entrance->tile == here->tile)
                    waypoint = dest->tile;
                else if (linksTo(entrance, here->tile))
                    waypoint = entrance->tile;
                else
                {
                    uint16_t shared = 0x4000;
                    for (int i = 0; i < 4; ++i)
                        if (links(here)[i] != 0x4000 && linksTo(entrance, links(here)[i]))
                        {
                            shared = links(here)[i];
                            break;
                        }
                    if (shared != 0x4000)
                    {
                        const bool inLine = here->tile % 128 == entrance->tile % 128 || here->tile / 128 == entrance->tile / 128;
                        waypoint = inLine ? entrance->tile : shared;
                    }
                    else
                    {
                        const uint16_t pick = Path_PickRoadLink(cls, city, entrance, here, person);
                        waypoint = pick != 0x4000 ? pick : entrance->tile;
                    }
                }
                const int shape = here->variant;
                if (shape == 3 || shape == 6 || shape == 9 || shape == 0xC)
                {
                    person->walkTile = waypoint;
                    return waypoint;
                }
                return advance(waypoint);
            }
            const uint16_t back = person->previousTile;
            const uint16_t goal = dest->tile;
            uint16_t waypoint = goal;
            ps2f nearest = Path_TileDistance(static_cast<int>(cls.address), here->tile, goal);
            for (int dir = 0; dir < 4; ++dir)
            {
                const uint16_t link = links(here)[dir];
                if (link == 0x4000 || link == back)
                    continue;
                const ps2f distance = Path_TileDistance(static_cast<int>(cls.address), link, goal);
                if (distance < nearest)
                {
                    waypoint = links(here)[dir];
                    nearest = distance;
                }
            }
            return advance(waypoint);
        }

        here = Map_GetTileFirstObject(city->map, tile);
        if (peopleInside(here) && Map_TileIndexFlag10(city->map, here->entranceTile) != 0)
            return advance(here->entranceTile);
        const GuestPtr<Vec4> from{sp + 0xE0u}, to{sp + 0xD0u}, result{sp + 0xC0u};
        if (tileDistanceIn(from, to, result, here->tile, dest->tile) < ps2f(8.0f))
            return advance(dest->tile);
        const int x = here->tile % 128, z = here->tile / 128;
        for (int ring = 0; ring < 3; ++ring)
            for (int dir = 0; dir < 4; ++dir)
            {
                const int tx = x + ring * port::load<int32_t>(globals::kPathStepX + 4u * static_cast<uint32_t>(dir));
                const int tz = z + ring * port::load<int32_t>(globals::kPathStepZ + 4u * static_cast<uint32_t>(dir));
                if (Map_TileFlag10(city->map, tx, tz) == 0)
                    continue;
                const uint16_t road = Map_GetTileObject(city->map, tx, tz)->tile;
                if (road != 0x4000)
                    return advance(road);
                ring = 3; // a road object without a tile: on to the stepping below, as in the original
                break;
            }
        const int goalX = dest->tile % 128, goalZ = dest->tile / 128;
        const int stepX = goalX < x ? -1 : (x < goalX ? 1 : 0);
        const int stepZ = goalZ < z ? -1 : (z < goalZ ? 1 : 0);
        int tx = x, tz = z;
        uint16_t next;
        do
        {
            tx += stepX;
            tz += stepZ;
            next = Map_GetTileObject(city->map, tx, tz)->tile;
        } while (next == here->tile);
        return advance(next);
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

    namespace
    {
        // How hard a class 2 vehicle brakes, slows, follows and speeds up (movement modes A and B).
        struct SpeedRules
        {
            int brake;  // nearer than 1.2 to a vehicle ahead, or close to a junction against the lights
            int slow;   // further from a vehicle ahead on its block, or early on a junction's approach
            int follow; // speed divisor behind a vehicle on the next block
            int gain;   // on straight road, or with the tiles around clear
        };

        // A vehicle's speed (+0x2C) for its step from block `obj` in direction `dir` (0..3: tile z down, x up,
        // z up, x down; world z runs against tile z), `dx` and `dz` its offset within its two-tile block
        // (-1..1). Other vehicles (class flag 1) count when heading within 3pi/8 of the same way: the first
        // ahead of it on its block brakes it (to as low as 0) when nearer than 1.2, else slows it (to 150);
        // failing that, when the next block is road, the first anywhere on it brakes it the same or slows it
        // by its speed over `follow` (direction 3: only within 3). Else the next block decides: straight road
        // (shape 5 along z, 10 along x) here, there and on the block after speeds it up; a junction (shapes 7,
        // 11, 13, 14 and 15) against the lights (traffic phase 0 lets z through, 2 lets x) brakes it within a
        // quarter block of the junction, else slows it by 50 past its block's middle or by `slow`. Anything
        // else costs 5. Direction 4: a vehicle close by on the tiles around slows it by 15 (to 1), else it
        // speeds up to at most 300. The speed stays within that floor (otherwise 300) and 800.
        void updateVehicleSpeed(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> vehicle, GuestPtr<MapObject> obj,
                                int dir, ps2f dx, ps2f dz, const SpeedRules &rules)
        {
            port::GuestFrame<0xD0> frame; // the people cursor sits at the original's sp + 0xCE
            const GuestPtr<uint16_t> cursor{frame.ptr().address + 0xCEu};
            const int speed = vehicle->introduces;
            const int x = obj->tile % 128, z = obj->tile / 128;
            int change = -5, floor = 300, ceiling = 800;
            const auto brake = [&](ps2f gap, int slow) {
                if (gap < ps2f(1.2f))
                {
                    change = rules.brake;
                    floor = 0;
                }
                else
                {
                    change = slow;
                    floor = 150;
                }
            };

            const auto lookAhead = [&] {
                static constexpr int kStepX[4] = {0, 1, 0, -1}, kStepZ[4] = {-1, 0, 1, 0};
                const bool alongZ = dir == 0 || dir == 2;
                const GuestPtr<MapObject> nextBlock = Map_GetTileObject(city->map, x + 2 * kStepX[dir], z + 2 * kStepZ[dir]);
                const GuestPtr<MapObject> blockAfter = Map_GetTileObject(city->map, x + 4 * kStepX[dir], z + 4 * kStepZ[dir]);
                // Headings within 3pi/8 of the way (as the original compares them, NaN included) and the gap ahead.
                const ps2f fiveEighthsPi = 0x1.F6A7A4p+0f, threeEighthsPi = 0x1.2D97C8p+0f;
                const ps2f eighthPi = 0x1.921FB6p-2f, sevenEighthsPi = 0x1.5FDBC0p+1f;
                const auto sameWay = [&](ps2f h) {
                    switch (dir)
                    {
                    case 0: return h < -fiveEighthsPi || !(h < fiveEighthsPi);
                    case 1: return !(h < -sevenEighthsPi) && h < -eighthPi;
                    case 2: return !(h < -threeEighthsPi) && h < threeEighthsPi;
                    default: return !(h < eighthPi) && h < sevenEighthsPi;
                    }
                };
                const auto gapTo = [&](const MapPerson &other) -> ps2f {
                    switch (dir)
                    {
                    case 0: return other.z - vehicle->z;
                    case 1: return other.x - vehicle->x;
                    case 2: return vehicle->z - other.z;
                    default: return vehicle->x - other.x;
                    }
                };
                const auto inFront = [&](const MapPerson &other) {
                    switch (dir)
                    {
                    case 0: return !(other.z <= vehicle->z);
                    case 1: return !(other.x <= vehicle->x);
                    case 2: return other.z < vehicle->z;
                    default: return other.x < vehicle->x;
                    }
                };
                const auto nextVehicle = [&] {
                    GuestPtr<MapPerson> other;
                    do
                        other = People_Next(city->map, cursor);
                    while (other && (other->index == vehicle->index || (charClassOf(*other)->flags & 1u) == 0));
                    return other;
                };

                *cursor = obj->firstPerson;
                while (const GuestPtr<MapPerson> other = nextVehicle())
                    if (sameWay(other->heading) && inFront(*other))
                    {
                        brake(gapTo(*other), rules.slow);
                        return;
                    }
                if (Map_TileIndexFlag10(city->map, nextBlock->tile) == 0)
                    return;
                *cursor = nextBlock->firstPerson;
                while (const GuestPtr<MapPerson> other = nextVehicle())
                    if (sameWay(other->heading))
                    {
                        const ps2f gap = gapTo(*other);
                        if (dir != 3 || gap < ps2f(3.0f))
                            brake(gap, -(speed / rules.follow));
                        return;
                    }

                const uint16_t straight = alongZ ? 5 : 10;
                const uint16_t shape = nextBlock->variant;
                if (shape == straight)
                {
                    if (obj->variant == straight && Map_TileIndexFlag10(city->map, blockAfter->tile) != 0 && blockAfter->variant == straight)
                        change = rules.gain;
                    return;
                }
                const bool junction = shape == 7 || shape == 11 || shape == 13 || shape == 14 || shape == 15;
                if (!junction || city->trafficPhase == (alongZ ? 0 : 2))
                    return;
                const ps2f along = alongZ ? dz : dx;
                const bool rising = dir == 0 || dir == 1;
                const bool close = rising ? !(along <= ps2f(0.5f)) : along < ps2f(-0.5f);
                const bool pastMiddle = rising ? !(along <= ps2f(0.0f)) : along < ps2f(0.0f);
                if (close)
                {
                    change = rules.brake;
                    floor = 0;
                }
                else
                {
                    change = pastMiddle ? -50 : rules.slow;
                    floor = 150;
                }
            };
            if (dir == 4)
            {
                if (Char_CheckNeighbourTile(cls, city, vehicle, x, z) != 0)
                {
                    change = -15;
                    floor = 1;
                }
                else
                {
                    change = rules.gain;
                    ceiling = 300;
                }
            }
            else if (dir >= 0 && dir < 4)
                lookAhead();

            const int next = speed + change;
            if (next < floor)
                vehicle->introduces = static_cast<uint16_t>(floor);
            else
                vehicle->introduces = static_cast<uint16_t>(ceiling < next ? ceiling : next);
        }
    }

    namespace
    {
        using SpeedUpdate = void (*)(GuestPtr<CharClass>, GuestPtr<CityScene>, GuestPtr<MapPerson>, GuestPtr<MapObject>, int, ps2f, ps2f);

        // A class 2 vehicle's step from tile `tile` (object `obj`). When it steers (mode A: on one frame in
        // four, by its index), it aims at the middle of its waypoint's object (+0x18), straight along an axis
        // when that is within 0.01, and its speed is updated for its offset within its two-tile block: off
        // road with the tiles-around test; on road for the way it heads (within pi/4), aiming pi/8 off that
        // way to get back into its lane (0.2 to 0.3 from the block's middle line, to the same side of its
        // heading whichever way it goes, so opposite ways keep apart). It turns by at most `turnStep` a frame
        // (taking the aim outright when that is nearer, or when the gap is at least 2 pi less a step). Every
        // frame it moves a 10000th of its speed along its heading, and leaves the tile's list when the move
        // takes it to another tile (unless flagged 0x800). The original's frame is reserved whole.
        void moveVehicle(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> vehicle, GuestPtr<MapObject> obj,
                         int tile, bool everyFourthFrame, ps2f turnStep, ps2f snapLimit, SpeedUpdate updateSpeed)
        {
            using namespace vu0;
            port::GuestFrame<0xF0> frame;
            const uint32_t sp = frame.ptr().address;
            const GuestPtr<Vec4> turn{sp + 0x70u}, goal{sp + 0xB0u}, here{sp + 0xC0u}, way{sp + 0xD0u}, step{sp + 0xE0u};
            const ps2f pi = 0x1.921FB6p+1f, twoPi = 0x1.921FB6p+2f, halfPi = 0x1.921FB6p+0f;
            const ps2f quarterPi = 0x1.921FB6p-1f, threeQuartersPi = 0x1.2D97C8p+1f;
            const ps2f eighthPi = 0x1.921FB6p-2f, threeEighthsPi = 0x1.2D97C8p+0f;
            const ps2f fiveEighthsPi = 0x1.F6A7A4p+0f, sevenEighthsPi = 0x1.5FDBC0p+1f;
            if (!everyFourthFrame || (vehicle->index & 3u) == (static_cast<uint32_t>(globals::frameCounter.get()) & 3u))
            {
                const GuestPtr<MapObject> target = Map_GetTileFirstObject(city->map, vehicle->walkTile);
                const ps2f size = fromInt(GuestPtr<BuildingType>{globals::buildingTypeAddress(target->type)}->size);
                const ps2f centre = 64.0f;
                goal->w = 1.0f;
                goal->z = (fromInt(128 - vehicle->walkTile / 128) - centre) - size / ps2f(2.0f);
                goal->y = 0.0f;
                here->w = 1.0f;
                goal->x = (fromInt(vehicle->walkTile % 128) - centre) + size / ps2f(2.0f);
                here->z = vehicle->z;
                here->y = 0.0f;
                here->x = vehicle->x;
                vf(4) = load(goal);
                vf(5) = load(here);
                set(vf(4), sub(vf(4), vf(5)), XYZW);
                store(way, vf(4));
                const ps2f nearZero = 0.01f;
                ps2f aim = 0.0f;
                if (way->x < nearZero && !(way->x <= -nearZero))
                    aim = way->z <= aim ? aim : -pi;
                else if (way->z < nearZero && !(way->z <= -nearZero))
                    aim = way->x <= ps2f(0.0f) ? halfPi : -halfPi;
                else
                {
                    Vec3_Normalize(way, way);
                    aim = sdk::atan2f(-way->x, -way->z);
                }

                const ps2f one = 1.0f, two = 2.0f, offset = 128.0f;
                const ps2f dx = sdk::fmodf(offset + vehicle->x, two) - one;
                const ps2f dz = sdk::fmodf(offset + vehicle->z, two) - one;
                if (Map_TileIndexFlag10(city->map, obj->tile) == 0)
                    updateSpeed(cls, city, vehicle, obj, 4, dx, dz);
                else
                {
                    // Below `low` steer one way, up to `high` straight on, past it the other way.
                    const auto lane = [](ps2f along, ps2f low, ps2f high, ps2f below, ps2f straight, ps2f above) {
                        if (along < low)
                            return below;
                        return along <= high ? straight : above;
                    };
                    int dir;
                    if (!(aim < -quarterPi) && aim < quarterPi)
                    {
                        dir = 2;
                        aim = lane(dx, 0.2f, 0.3f, -eighthPi, 0.0f, eighthPi);
                    }
                    else if (!(aim < -quarterPi) && aim < threeQuartersPi)
                    {
                        dir = 3;
                        aim = lane(dz, -0.3f, -0.2f, fiveEighthsPi, halfPi, threeEighthsPi);
                    }
                    else if (!(aim < -threeQuartersPi) && aim < threeQuartersPi)
                    {
                        dir = 1;
                        aim = lane(dz, 0.2f, 0.3f, -fiveEighthsPi, -halfPi, -threeEighthsPi);
                    }
                    else
                    {
                        dir = 0;
                        aim = lane(dx, -0.3f, -0.2f, -sevenEighthsPi, -pi, sevenEighthsPi);
                    }
                    updateSpeed(cls, city, vehicle, obj, dir, dx, dz);
                }

                const ps2f gap = (pi + aim) - (pi + vehicle->heading);
                if ((!(gap < -turnStep) && gap < turnStep) || gap <= -snapLimit || !(gap < snapLimit))
                    vehicle->heading = aim;
                else if (gap < -pi || (!(gap < ps2f(0.0f)) && gap < pi))
                {
                    const ps2f h = vehicle->heading + turnStep;
                    vehicle->heading = h;
                    if (!(h < pi))
                        vehicle->heading = h - twoPi;
                }
                else
                {
                    const ps2f h = vehicle->heading - turnStep;
                    vehicle->heading = h;
                    if (h < -pi)
                        vehicle->heading = h + twoPi;
                }
            }

            sdk::sceVu0RotMatrixY(turn.address, globals::kIdentityMatrix, vehicle->heading);
            step->w = 1.0f;
            step->z = fromInt(-static_cast<int32_t>(vehicle->introduces)) / ps2f(10000.0f);
            step->y = 0.0f;
            step->x = 0.0f;
            Math_MulMatVecVu0(step, turn, step);
            vehicle->x = vehicle->x + step->x;
            vehicle->z = vehicle->z + step->z;
            if ((vehicle->flags & 0x800u) != 0)
                return;
            const int tileX = sdk::fptosi(ps2f(64.0f) + vehicle->x);
            const int tileZ = sdk::fptosi(ps2f(64.0f) - vehicle->z);
            if (static_cast<uint32_t>(tile) != static_cast<uint32_t>(tileX) + (static_cast<uint32_t>(tileZ) << 7))
                Map_UnlinkPersonFromTile(city->map, tile, vehicle->index);
        }
    }

    // char (class 2): movement modes A and B. A steers on one frame in four and turns up to 0.16, B steers
    // every frame and turns up to 0.04 (with their own speed rules).
    void Char2_MoveA(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> vehicle, GuestPtr<MapObject> obj, int tile)
    {
        moveVehicle(cls, city, vehicle, obj, tile, true, 0.16f, 0x1.87E246p+2f, Char2_UpdateSpeedA);
    }

    void Char2_MoveB(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> vehicle, GuestPtr<MapObject> obj, int tile)
    {
        moveVehicle(cls, city, vehicle, obj, tile, false, 0.04f, 0x1.8F905Ap+2f, Char2_UpdateSpeedB);
    }

    // char (class 2): the vehicle speed of movement modes A and B, which differ only in their rules.
    void Char2_UpdateSpeedA(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> vehicle, GuestPtr<MapObject> obj,
                            int dir, ps2f dx, ps2f dz)
    {
        updateVehicleSpeed(cls, city, vehicle, obj, dir, dx, dz, {-180, -60, 40, 40});
    }

    void Char2_UpdateSpeedB(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> vehicle, GuestPtr<MapObject> obj,
                            int dir, ps2f dx, ps2f dz)
    {
        updateVehicleSpeed(cls, city, vehicle, obj, dir, dx, dz, {-80, -20, 20, 10});
    }

    PORT_FN(BaseObject_ctor);
    PORT_FN(Char2_UpdateSpeedA);
    PORT_FN(Char2_UpdateSpeedB);
    PORT_FN(Char2_MoveA);
    PORT_FN(Char2_MoveB);
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
    PORT_FN(Camera_ctor);
    PORT_FN(CityCamera_UpdateTimeOfDay);
    PORT_FN(CityCamera_Update);
    PORT_FN(Sky_BuildPackets);
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
    PORT_FN(Char2_Draw);
    PORT_FN(Player_UpdateAction);
    PORT_FN(Player_PlayFootstep);
    PORT_FN(Player_PickTarget);
    PORT_FN(Player_Move);
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
    PORT_FN(PhoneCall_Post);
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
    PORT_FN(Person_PlaceHomeObject);
    PORT_FN(Person_FindBuildingInList);
    PORT_FN(Player_Turn);
    PORT_FN(EvThreeUnsatisfied_Check);
    PORT_FN(Person_TickCounters);
    PORT_FN(Person_UpdatePose);
    PORT_FN(Person_Walk);
    PORT_FN(EventQueue_InsertByPriority);
    PORT_FN(Path_TileDistance);
    PORT_FN(Path_PickRoadLink);
    PORT_FN(Path_NextWaypoint);
    PORT_FN(Person_TurnToPlayer);
    PORT_FN(Person_ProbeTile);
    PORT_FN(Char_CheckNeighbourTile);
}
