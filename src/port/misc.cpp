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

    // ---- claim data lookups (batch 38)

    namespace
    {
        // The claim texts (0x1BDA74): two halfword tables of offsets into the texts, the second for codes with bit 15.
        int claimText(uint32_t code)
        {
            const uint32_t texts = port::load<uint32_t>(0x001BDA74u);
            const uint32_t table = texts + 2u * port::load<uint16_t>(texts + ((code & 0x8000u) != 0 ? 2u : 0u));
            return static_cast<int>(texts + port::load<uint16_t>(table + 2u * (code & 0xFFFF7FFFu)));
        }
    }

    int Claim_PickVariant(uint32_t claim) { return claimText(claim); }

    int Claim_GetSectionB(int index)
    {
        return static_cast<int>(globals::claimData.get() + 4u * port::load<uint32_t>(port::load<uint32_t>(0x001BDA54u) + 4u * static_cast<uint32_t>(index)));
    }

    int Claim_GetSectionD(uint32_t index)
    {
        return static_cast<int>(globals::claimData.get() + 4u * port::load<uint32_t>(port::load<uint32_t>(0x001BDA5Cu) + 4u * (index & 0x7FFFu)));
    }

    // The text of line `line` of claim `claim` (section B entry +2, 8 bytes per line).
    int Claim_GetLine(int claim, int line)
    {
        const uint32_t entry = static_cast<uint32_t>(Claim_GetSectionB(claim)) + 8u * static_cast<uint32_t>(line);
        return claimText(port::load<uint16_t>(entry + 2u));
    }

    PORT_FN(Claim_PickVariant);
    PORT_FN(Claim_GetSectionB);
    PORT_FN(Claim_GetSectionD);
    PORT_FN(Claim_GetLine);

    // ---- laptop entries (batch 38)

    namespace
    {
        GuestPtr<int32_t> laptopEntry(int i) { return GuestPtr<int32_t>{globals::kLaptopEntries + 4u * static_cast<uint32_t>(i)}; }
    }

    // Adds an entry (14 slots, -1 free) unless it is there. Entries 6 to 8 are tiers of one thing: a better one
    // replaces a worse, a worse one is not added.
    void Laptop_AddEntry(GuestPtr<Laptop>, int entry)
    {
        for (int i = 0; i < 14; ++i)
            if (*laptopEntry(i) == entry)
                return;
        if (entry >= 6 && entry < 9)
        {
            for (int i = 0; i < 14; ++i)
            {
                const int32_t existing = *laptopEntry(i);
                if (existing < 6)
                    continue;
                if (entry >= existing)
                {
                    *laptopEntry(i) = entry;
                    return;
                }
                if (existing < 9)
                    return;
            }
        }
        for (int i = 0; i < 14; ++i)
        {
            if (*laptopEntry(i) != -1)
                continue;
            *laptopEntry(i) = entry;
            globals::laptopEntryCount.set(globals::laptopEntryCount.get() + 1);
            return;
        }
    }

    // Removes the entry: the ones after it move up, the last slot is freed, and the cursor (+0x3D0) steps back when
    // nothing follows.
    void Laptop_RemoveEntry(GuestPtr<Laptop> laptop, int entry)
    {
        for (int i = 0; i < 14; ++i)
        {
            if (*laptopEntry(i) != entry)
                continue;
            int last = i;
            for (; last < 13; ++last)
                *laptopEntry(last) = *laptopEntry(last + 1);
            *laptopEntry(last) = -1;
            globals::laptopEntryCount.set(globals::laptopEntryCount.get() - 1);
            if (*laptopEntry(i) == -1)
                port::store<int32_t>(laptop.address + 0x3D0u, port::load<int32_t>(laptop.address + 0x3D0u) - 1);
            return;
        }
    }

    void Laptop_SaveState(GuestPtr<Laptop>, GuestPtr<McManager> mc)
    {
        SaveFile_Write(mc, static_cast<int>(globals::laptopEntryCount.address), 4);
        SaveFile_Write(mc, static_cast<int>(globals::kLaptopEntries), 0x38);
    }

    PORT_FN(Laptop_AddEntry);
    PORT_FN(Laptop_RemoveEntry);
    PORT_FN(Laptop_SaveState);

    // ---- people: visits, idle workers, room in a building (batch 38)

    namespace
    {
        // The person tile from the position (x + 64, 64 - z; z converted first, as the originals do).
        uint32_t personTileZFirst(GuestPtr<MapPerson> person)
        {
            const int z = sdk::fptosi(ps2f(64.0f) - ps2f(person->z));
            const int x = sdk::fptosi(ps2f(64.0f) + ps2f(person->x));
            return (static_cast<uint32_t>(x) + (static_cast<uint32_t>(z) << 7)) & 0xFFFFu;
        }

        bool isResidentClass(GuestPtr<MapPerson> person)
        {
            const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
            return (cls->flags & 1u) == 0;
        }
    }

    // Whether the building on a walkable tile has room: fewer than four residents inside (people with flag 0x100
    // not counted).
    int Person_TileHasCompanion(GuestPtr<CityScene> city, GuestPtr<MapPerson>, GuestPtr<MapObject> building)
    {
        if (Map_TileIndexFlag10(city->map, building->tile) == 0)
            return 0;
        port::GuestLocal<uint16_t> cursor;
        *cursor = building->firstPerson;
        int inside = 0;
        for (;;)
        {
            const GuestPtr<MapPerson> person = People_Next(city->map, cursor);
            if (!person)
                return 1;
            if ((person->flags & 0x100u) != 0 || person->inside != building->id || !isResidentClass(person))
                continue;
            if (++inside >= 4)
                return 0;
        }
    }

    // Where the person goes visiting: a target kept while the building there has room, else (friendship 48 and
    // up, the workplace claim meter at level 0, its entrance building with room) the entrance building, else the
    // workplace. The workplace itself is never kept as a target.
    uint16_t Person_TargetVisit(GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        if (person->target == person->work)
            person->target = kNoObject;
        const GuestPtr<MapObject> work = Map_GetRecord(city->map, person->work);
        if (person->target != kNoObject)
        {
            if (Person_TileHasCompanion(city, person, Map_GetRecord(city->map, person->target)) == 0)
                person->target = person->work;
            return person->target;
        }
        if (Family_GetFriendship(person->family, person->member) < 48 || ClaimMeter_Level(work->claimMeter) != 0 ||
            work->entranceTile == kNoObject)
        {
            person->target = person->work;
            return person->target;
        }
        const GuestPtr<MapObject> entrance = Map_GetTileFirstObject(city->map, work->entranceTile);
        person->target = Person_TileHasCompanion(city, person, entrance) != 0 ? entrance->id : person->work;
        return person->target;
    }

    // Pairs the person with a free family member at their workplace (a resident of the family in schedule state
    // 2): that member heads for their target (or their work target) and follows the person, who waits for them
    // (flag 0x200). Returns the target, 0x3F80 when nobody fits.
    int16_t Person_TargetHome(GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        port::GuestLocal<uint16_t> cursor;
        *cursor = Map_GetRecord(city->map, person->work)->firstPerson;
        while (*cursor != kNoPerson)
        {
            const GuestPtr<MapPerson> member = People_Next(city->map, cursor);
            if (!isResidentClass(member) || member->family != person->family || member->schedState != 2)
                continue;
            uint16_t target = member->target;
            if (target == kNoObject || target == member->work)
                target = Person_TargetWork(city, member);
            if (target == kNoObject)
                continue;
            member->flags |= 0x20u;
            member->follow = person->index;
            person->flags |= 0x200u;
            person->follow = member->index;
            return static_cast<int16_t>(target);
        }
        return static_cast<int16_t>(kNoObject);
    }

    // A person of the character type working at the building whose workplace claim meter is at level 0, or 0.
    GuestPtr<MapPerson> Person_FindIdleOfType(GuestPtr<CityScene> city, GuestPtr<MapObject> building, uint32_t charType)
    {
        port::GuestLocal<uint16_t> cursor;
        *cursor = building->firstPerson;
        while (const GuestPtr<MapPerson> person = People_Next(city->map, cursor))
        {
            if (person->charType != charType)
                continue;
            if (ClaimMeter_Level(Map_GetRecord(city->map, person->work)->claimMeter) == 0)
                return person;
        }
        return GuestPtr<MapPerson>{0};
    }

    // Whether the actor (or an actor without a person) is at the target building: inside it, or not inside
    // anything and standing on it.
    int Person_IsAtBuilding(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor, GuestPtr<ActorRef> target)
    {
        if (actor->person == kNoPerson)
            return 1;
        const GuestPtr<CityMap> map = city->map;
        const GuestPtr<MapPerson> person = map->people + actor->person;
        if (person->inside != kNoObject && person->inside != target->object)
            return 0;
        const GuestPtr<MapObject> on = Map_FindPersonObjectOnTile(map, person->index, personTileZFirst(person));
        if (!on)
            return 0;
        return on->id == target->object ? 1 : 0;
    }

    // Whether the actor stands on a building of theirs that does not keep its people inside (type flag 0x40),
    // never while flag 4 is set.
    int Person_IsOnOpenTile(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor)
    {
        const GuestPtr<CityMap> map = city->map;
        const GuestPtr<MapPerson> person = map->people + actor->person;
        if ((person->flags & 4u) != 0)
            return 0;
        const GuestPtr<MapObject> on = Map_FindPersonObjectOnTile(map, person->index, personTileZFirst(person));
        if (!on)
            return 0;
        return (GuestPtr<BuildingType>{globals::buildingTypeAddress(on->type)}->flags & 0x40u) != 0 ? 0 : 1;
    }

    PORT_FN(Person_TileHasCompanion);
    PORT_FN(Person_TargetVisit);
    PORT_FN(Person_TargetHome);
    PORT_FN(Person_FindIdleOfType);
    PORT_FN(Person_IsAtBuilding);
    PORT_FN(Person_IsOnOpenTile);

    // ---- phone calls, the player, distances (batch 38)

    namespace
    {
        // The queued calls (four of 0xC bytes from +0x10: a word, two halfwords, a word) move up one; the last is
        // left as it was except its first halfword cleared.
        void shiftCalls(uint32_t phone)
        {
            for (uint32_t entry = phone + 0x10u; entry < phone + 0x34u; entry += 0xCu)
            {
                port::store<uint32_t>(entry, port::load<uint32_t>(entry + 0xCu));
                port::store<uint16_t>(entry + 4u, port::load<uint16_t>(entry + 0x10u));
                port::store<uint16_t>(entry + 6u, port::load<uint16_t>(entry + 0x12u));
                port::store<uint32_t>(entry + 8u, port::load<uint32_t>(entry + 0x14u));
            }
            port::store<uint16_t>(phone + 0x38u, 0);
        }

        // Ending the call being shown: the queue moves up, the pad focus is released, the call state (+4, +0xC)
        // cleared, +0x40 negated and the notice cleared.
        void endShownCall(uint32_t phone, GuestPtr<CityScene> city)
        {
            shiftCalls(phone);
            Pad_ReleaseFocus(1);
            port::store<int32_t>(phone + 0xCu, 0);
            port::store<int32_t>(phone + 4u, 0);
            port::store<int32_t>(phone + 0x40u, -port::load<int32_t>(phone + 0x40u));
            Notice_Clear(city->notice);
        }
    }

    // The shown call ends (if one is shown) and the whole queue is cleared.
    void PhoneCall_Next(GuestPtr<PhoneCalls> phone, GuestPtr<CityScene> city)
    {
        if (port::load<int32_t>(phone.address + 4u) != 0)
            endShownCall(phone.address, city);
        sdk::memset(phone.address + 0x10u, 0, 0x30);
    }

    // Withdraws the queued call of the family member: the shown one ends, another is cut out of the queue.
    void PhoneCall_Remove(GuestPtr<PhoneCalls> phone, GuestPtr<CityScene> city, uint32_t family, uint32_t member)
    {
        const uint32_t first = phone.address + 0x10u, end = first + 0x30u;
        for (uint32_t entry = first; entry < end; entry += 0xCu)
        {
            if (port::load<uint16_t>(entry + 4u) != family || port::load<uint16_t>(entry + 6u) != member)
                continue;
            if (entry == first && port::load<int32_t>(phone.address + 4u) != 0)
            {
                endShownCall(phone.address, city);
                return;
            }
            const int32_t left = static_cast<int32_t>(end - entry) / 12;
            if (left < 2)
                sdk::memset(entry, 0, 12);
            else
                sdk::memmove(entry, entry + 12u, (left - 1) * 12);
            return;
        }
    }

    int Player_GetMode(GuestPtr<Player> player) { return port::load<int32_t>(player.address); }

    void Player_SaveState(GuestPtr<Player> player, GuestPtr<McManager> mc) { SaveFile_Write(mc, static_cast<int>(player.address), 0x38); }

    // Tiles from the player to the actor building: 0 on it, else the largest of the four distances beyond its
    // footprint edges.
    int Player_DistanceToBuilding(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor)
    {
        port::GuestFrame<0xA0> frame;
        const GuestPtr<MapObject> building = Map_GetRecord(city->map, actor->object);
        const int size = GuestPtr<BuildingType>{globals::buildingTypeAddress(building->type)}->size;
        const int x0 = building->tile & 0x7F, z0 = building->tile >> 7, x1 = x0 + size, z1 = z0 + size;
        const GuestPtr<Vec4> matrix{frame.ptr().address + 0x60u};
        Player_GetMatrix(city->player, matrix);
        const int px = sdk::fptosi(ps2f(64.0f) + ps2f(matrix[3].x));
        const int pz = sdk::fptosi(ps2f(64.0f) - ps2f(matrix[3].z));
        if (px >= x0 && px < x1 && pz >= z0 && pz < z1)
            return 0;
        int distance = x0 - px;
        if (distance < px - x1)
            distance = px - x1;
        if (distance < z0 - pz)
            distance = z0 - pz;
        if (distance < pz - z1)
            distance = pz - z1;
        return distance;
    }

    namespace
    {
        // The horizontal distance between a point and the player (the player matrix in the frame at +0x20).
        ps2f distanceToPlayer(GuestPtr<CityScene> city, uint32_t frameBase, uint32_t point)
        {
            const GuestPtr<Vec4> matrix{frameBase + 0x20u};
            Player_GetMatrix(city->player, matrix);
            const ps2f dx = ps2f(port::load<float>(point)) - ps2f(matrix[3].x);
            const ps2f dz = ps2f(port::load<float>(point + 8u)) - ps2f(matrix[3].z);
            return sdk::sqrtf(dx * dx + dz * dz);
        }
    }

    ps2f Player_DistanceToPerson(GuestPtr<CityScene> city, GuestPtr<ActorRef> actor)
    {
        port::GuestFrame<0x60> frame;
        return distanceToPlayer(city, frame.ptr().address, (city->map->people + actor->person).address);
    }

    ps2f Player_DistanceToPoint(GuestPtr<CityScene> city, GuestPtr<ps2f> point)
    {
        port::GuestFrame<0x60> frame;
        return distanceToPlayer(city, frame.ptr().address, point.address);
    }

    // The horizontal distance between two actors.
    ps2f Person_Distance(GuestPtr<CityScene> city, GuestPtr<ActorRef> actorA, GuestPtr<ActorRef> actorB)
    {
        const GuestPtr<MapPerson> a = city->map->people + actorA->person, b = city->map->people + actorB->person;
        const ps2f dx = ps2f(a->x) - ps2f(b->x);
        const ps2f dz = ps2f(a->z) - ps2f(b->z);
        return sdk::sqrtf(dx * dx + dz * dz);
    }

    PORT_FN(PhoneCall_Next);
    PORT_FN(PhoneCall_Remove);
    PORT_FN(Player_GetMode);
    PORT_FN(Player_SaveState);
    PORT_FN(Player_DistanceToBuilding);
    PORT_FN(Player_DistanceToPerson);
    PORT_FN(Player_DistanceToPoint);
    PORT_FN(Person_Distance);

    // ---- stamina, status bytes, talk helpers (batch 38)

    void Stage_SaveState(int, GuestPtr<McManager> mc) { SaveFile_Write(mc, static_cast<int>(globals::kStageState), 0x284); }

    void Stamina_Init(GuestPtr<Stamina> stamina)
    {
        port::store<int32_t>(stamina.address, 0);
        port::store<int32_t>(stamina.address + 4u, 0x61A7F); // 399999
        port::store<int32_t>(stamina.address + 8u, 0);
    }

    void Stamina_SaveState(GuestPtr<Stamina> stamina, GuestPtr<McManager> mc) { SaveFile_Write(mc, static_cast<int>(stamina.address), 0xC); }

    int Stamina_Zero(GuestPtr<Stamina>) { return 0; }

    // Food (goods categories 0x11 to 0x14) adds 1000 per point (goods +5), up to 2000000; any goods clear +8.
    void Stamina_Eat(GuestPtr<Stamina> stamina, GuestPtr<uint8_t> goods)
    {
        const uint8_t category = goods[4];
        if (category >= 0x11 && category <= 0x14)
        {
            const uint32_t value = stamina.address + 4u;
            port::store<int32_t>(value, static_cast<int32_t>(static_cast<uint32_t>(port::load<int32_t>(value)) + 1000u * goods[5]));
            if (port::load<int32_t>(value) > 2000000)
                port::store<int32_t>(value, 2000000);
        }
        port::store<int32_t>(stamina.address + 8u, 0);
    }

    namespace
    {
        uint32_t statusLevel(uint8_t status) { return (status >> 4) & 0xFu; }
    }

    uint8_t StatusByte_Low2(GuestPtr<uint8_t> status) { return *status & 3u; }
    uint32_t StatusByte_Mid2(GuestPtr<uint8_t> status) { return (*status >> 2) & 3u; }
    int StatusByte_LevelAbove13(GuestPtr<uint8_t> status) { return statusLevel(*status) >= 14 ? 1 : 0; }

    // Advances the low fields (each up to 2; the middle pair only from level 2), then the level starts over.
    void StatusByte_Advance(GuestPtr<uint8_t> status)
    {
        if (statusLevel(*status) >= 2)
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

    void StatusByte_ClearMid2(GuestPtr<uint8_t> status)
    {
        if (((*status >> 2) & 3u) != 0)
            *status = static_cast<uint8_t>(*status & 0xF3u);
    }

    // Levels 12 and 13 go one up (14 is the top).
    void StatusByte_IncHighLevel(GuestPtr<uint8_t> status)
    {
        const uint32_t level = statusLevel(*status);
        if (level < 12 || level >= 14)
            return;
        *status = static_cast<uint8_t>((*status & 0x0Fu) | (((level + 1u) & 0xFu) << 4));
    }

    // From level 12 the member gains `amount` friendship points.
    void StatusByte_OnLevel12(int family, int member, GuestPtr<uint8_t> status, int amount, GuestPtr<uint32_t> approval)
    {
        if (statusLevel(*status) >= 12)
            Family_AddFriendship(family, member, GuestPtr<int32_t>{approval.address}, amount);
    }

    // The talk view (+0x10) has no person chosen (-1) and the choosing ends (+0x3C).
    void Talk_EndChoosing(GuestPtr<TalkSession> talk)
    {
        const uint32_t view = port::load<uint32_t>(talk.address + 0x10u);
        port::store<int32_t>(view + 0xB8u, -1);
        port::store<int32_t>(talk.address + 0x3Cu, 1);
    }

    void Talk_SelectPerson(GuestPtr<TalkSession> talk, uint32_t family, uint32_t member)
    {
        const uint32_t view = port::load<uint32_t>(talk.address + 0x10u);
        port::store<int32_t>(view + 0xB8u, TalkView_FindPersonSlot(GuestPtr<TalkView>{view}, family, member));
    }

    void Talk_SetViewLit(GuestPtr<TalkSession> talk)
    {
        const uint32_t view = port::load<uint32_t>(talk.address + 0x10u);
        if (view != 0)
            port::store<int32_t>(view + 0x258u, 1);
    }

    // The pose of a family member in the talk (32 entries of family, member, pose from +0x40): the entry of the
    // member, else the first free one (family 0).
    void Talk_SetPose(GuestPtr<TalkSession> talk, uint16_t family, uint8_t member, uint8_t pose)
    {
        for (uint32_t i = 0; i < 32; ++i)
        {
            const uint32_t entry = talk.address + 0x40u + 4u * i;
            if (port::load<uint16_t>(entry) == family && port::load<uint8_t>(entry + 2u) == member)
            {
                port::store<uint8_t>(entry + 3u, pose);
                return;
            }
        }
        for (uint32_t i = 0; i < 32; ++i)
        {
            const uint32_t entry = talk.address + 0x40u + 4u * i;
            if (port::load<uint16_t>(entry) != 0)
                continue;
            port::store<uint16_t>(entry, family);
            port::store<uint8_t>(entry + 2u, member);
            port::store<uint8_t>(entry + 3u, pose);
            return;
        }
    }

    PORT_FN(Stage_SaveState);
    PORT_FN(Stamina_Init);
    PORT_FN(Stamina_SaveState);
    PORT_FN(Stamina_Zero);
    PORT_FN(Stamina_Eat);
    PORT_FN(StatusByte_Low2);
    PORT_FN(StatusByte_Mid2);
    PORT_FN(StatusByte_LevelAbove13);
    PORT_FN(StatusByte_Advance);
    PORT_FN(StatusByte_ClearMid2);
    PORT_FN(StatusByte_IncHighLevel);
    PORT_FN(StatusByte_OnLevel12);
    PORT_FN(Talk_EndChoosing);
    PORT_FN(Talk_SelectPerson);
    PORT_FN(Talk_SetViewLit);
    PORT_FN(Talk_SetPose);

    // ---- empty base methods (batch 38)

    void App_OnInit() {}
    void CharType_Draw(GuestPtr<CharClass>) {}
    void CharType_Draw2(GuestPtr<CharClass>) {}
    void CharType_Slot18(GuestPtr<CharClass>) {}
    int CharType_Update(GuestPtr<CharClass>) { return 0; }

    PORT_FN(App_OnInit);
    PORT_FN(CharType_Draw);
    PORT_FN(CharType_Draw2);
    PORT_FN(CharType_Slot18);
    PORT_FN(CharType_Update);

    // ---- batch 38

    // The person leaves the building they are in: a building that keeps its people inside (type flag 0x40) has
    // one occupant fewer, unless it is their workplace or their target. The walk restarts (tiles 0x4000).
    void Person_ExitBuilding(GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        if (person->inside == kNoObject)
            return;
        const GuestPtr<MapObject> obj = Map_GetRecord(city->map, person->inside);
        if (obj->id != person->work && obj->id != person->target &&
            (GuestPtr<BuildingType>{globals::buildingTypeAddress(obj->type)}->flags & 0x40u) != 0 && obj->occupants > 0)
            obj->occupants = static_cast<uint8_t>(obj->occupants - 1);
        person->walkTile = 0x4000;
        person->previousTile = 0x4000;
        person->inside = kNoObject;
    }

    PORT_FN(Person_ExitBuilding);

    // ---- people's targets (batch 39)

    // 1 when the person is outside the building's footprint widened by `range`, or stands on another building (of a
    // category other than 0); 0 on the building itself, on nothing or on plain ground.
    int Person_IsNearBuilding(GuestPtr<CityScene> city, GuestPtr<MapPerson> person, GuestPtr<MapObject> obj, int range)
    {
        const int size = GuestPtr<BuildingType>{globals::buildingTypeAddress(obj->type)}->size;
        const int x0 = obj->tile % 128 - range, z0 = obj->tile / 128 - range;
        const int x1 = range + (obj->tile % 128 + size), z1 = range + (obj->tile / 128 + size);
        const int z = sdk::fptosi(ps2f(64.0f) - ps2f(person->z));
        const int x = sdk::fptosi(ps2f(64.0f) + ps2f(person->x));
        if (x < x0 || x >= x1 || z < z0 || z >= z1)
            return 1;
        const GuestPtr<MapObject> on = Map_FindPersonObjectOnTile(city->map, person->index, (static_cast<uint32_t>(x) + (static_cast<uint32_t>(z) << 7)) & 0xFFFFu);
        if (!on || GuestPtr<BuildingType>{globals::buildingTypeAddress(on->type)}->category == 0)
            return 0;
        return on->id != obj->id ? 1 : 0;
    }

    // A shop for someone at (x, z): tile lists from a random one on (64 tries), a shop (type flag 0x40) with a
    // walkable entrance 5 to 9 tiles away (along x plus along z), taken with a one-in-three chance. 0x3F80 for none.
    uint16_t Person_RandomTarget(GuestPtr<CityScene> city, GuestPtr<MapPerson>, int x, int z)
    {
        port::GuestLocal<uint32_t> count;
        int list = Random_TimerMix() & 0x3F;
        for (int tries = 0; tries < 0x40; ++tries)
        {
            const uint32_t items = Map_GetTileList(city->map, list, count).address;
            if (++list >= 0x40)
                list = 0;
            for (uint32_t i = 0, item = items; static_cast<int32_t>(i) < static_cast<int32_t>(*count); ++i, item += 2u)
            {
                const GuestPtr<MapObject> shop = Map_GetRecord(city->map, port::load<uint16_t>(item));
                if ((GuestPtr<BuildingType>{globals::buildingTypeAddress(shop->type)}->flags & 0x40u) == 0 ||
                    Map_TileIndexFlag10(city->map, shop->entranceTile) == 0)
                    continue;
                const GuestPtr<MapObject> at = Map_GetTileFirstObject(city->map, shop->entranceTile);
                const int distance = std::abs(at->tile % 128 - x) + std::abs(at->tile / 128 - z);
                if (distance >= 10 || distance < 5)
                    continue;
                if ((Random_TimerMix() & 0xFFFF) < 0x5555)
                    return at->id;
            }
        }
        return kNoObject;
    }

    namespace
    {
        // A family member of the person among the people listed at the building, free to go along (a resident in
        // schedule state 2).
        GuestPtr<MapPerson> companionAt(GuestPtr<CityScene> city, GuestPtr<MapObject> building, GuestPtr<MapPerson> person,
                                        GuestPtr<uint16_t> cursor)
        {
            *cursor = building->firstPerson;
            while (*cursor != kNoPerson)
            {
                const GuestPtr<MapPerson> member = People_Next(city->map, cursor);
                const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * member->charType)};
                if ((cls->flags & 1u) == 0 && member->family == person->family && member->schedState == 2)
                    return member;
            }
            return GuestPtr<MapPerson>{0};
        }
    }

    // Shopping together: at the building on tile (x, z), unless it is the person's workplace, a family member free
    // to go along follows the person, who waits (flag 0x200), and the workplace is the target. Otherwise the first
    // shop of tile list 0x17 with such a member there; 0x3F80 for none.
    uint16_t Person_TargetShop(GuestPtr<CityScene> city, GuestPtr<MapPerson> person, int x, int z)
    {
        port::GuestLocal<uint16_t> cursor;
        const GuestPtr<MapObject> here = Map_GetTileObject(city->map, x, z);
        if (person->work != here->id)
        {
            if (const GuestPtr<MapPerson> member = companionAt(city, here, person, cursor))
            {
                member->flags |= 0x20u;
                member->follow = person->index;
                person->flags |= 0x200u;
                person->follow = member->index;
                return person->work;
            }
        }
        port::GuestLocal<uint32_t> count;
        const uint32_t items = Map_GetTileList(city->map, 0x17, count).address;
        for (uint32_t i = 0, item = items; static_cast<int32_t>(i) < static_cast<int32_t>(*count); ++i, item += 2u)
        {
            const GuestPtr<MapObject> shop = Map_GetRecord(city->map, port::load<uint16_t>(item));
            if (companionAt(city, shop, person, cursor))
                return shop->id;
        }
        return kNoObject;
    }

    // Everyone goes back to work: each person leaves the building lists of the tile they stand on and stands at the
    // middle of their workplace tile (height 0), outside, with walk tiles and flags reset; residents lose their
    // target, vehicles get 6000.
    void Map_ReturnPeopleToWork(GuestPtr<CityMap> map)
    {
        for (int i = 0; i < 6000; ++i)
        {
            const GuestPtr<MapPerson> person = map->people + i;
            if (person->charType == 0)
                continue;
            const uint16_t index = person->index;
            const int z = sdk::fptosi(ps2f(64.0f) - ps2f(person->z));
            const int x = sdk::fptosi(ps2f(64.0f) + ps2f(person->x));
            GuestPtr<MapObject> obj = Map_GetTileFirstObject(map, static_cast<uint32_t>(x + (z << 7)));
            while (People_UnlinkFromList(map, GuestPtr<uint16_t>::of(obj->firstPerson), index) == 0 && obj->nextOnTile != kNoObject)
                obj = map->objects + obj->nextOnTile;
            const GuestPtr<MapObject> work = Map_GetRecord(map, person->work);
            person->x = ps2f(0.5f) + (fromInt(work->tile % 128) - ps2f(64.0f));
            person->y = 0.0f;
            person->z = (fromInt(128 - work->tile / 128) - ps2f(64.0f)) - ps2f(0.5f);
            person->inside = kNoObject;
            person->destination = kNoObject;
            person->walkTile = 0x4000;
            person->previousTile = 0x4000;
            person->cooldown = 0;
            person->flags &= 0xFC1Bu;
            const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
            person->target = (cls->flags & 1u) != 0 ? kNoPerson : kNoObject;
        }
    }

    // "H:MM", the separator glyph 0xC2 (0x1B9AD8) and "H:MM" of two minutes of the day (an hour of 10 and up takes
    // two digits).
    void Text_FormatTimeRange(GuestPtr<char> out, int fromMinute, int toMinute)
    {
        const auto put = [](uint32_t at, int minute) {
            const int hour = minute / 60;
            if (hour / 10 != 0)
                port::store<uint8_t>(at++, static_cast<uint8_t>('0' + hour / 10));
            port::store<uint8_t>(at, static_cast<uint8_t>('0' + hour % 10));
            port::store<uint8_t>(at + 1u, ':');
            port::store<uint8_t>(at + 2u, static_cast<uint8_t>('0' + minute % 60 / 10));
            port::store<uint8_t>(at + 3u, static_cast<uint8_t>('0' + minute % 60 % 10));
            port::store<uint8_t>(at + 4u, 0);
        };
        put(out.address, fromMinute);
        sdk::strcat(out.address, 0x001B9AD8u);
        put(out.address + static_cast<uint32_t>(sdk::strlen(out.address)), toMinute);
    }

    PORT_FN(Person_IsNearBuilding);
    PORT_FN(Person_RandomTarget);
    PORT_FN(Person_TargetShop);
    PORT_FN(Map_ReturnPeopleToWork);
    PORT_FN(Text_FormatTimeRange);

    // ---- the laptop's menu (batch 39)

    // Runs the selected laptop entry (+0x3D0 into the entry list) and returns the scene mode to go to (the current one
    // when nothing changes): 0 mode 6; 1, 2, 9 and 11 put the player in mode 0, 1, 3 or 4 (mode 1); 3 mode 3; 4 mode 7;
    // 5 toggles stage flag 1; 6 to 8 mode 9; 10 mode 8; 12 queues event 6 and drops its entry (mode 1). 13 is a debug
    // unlock: every family a friend, stage flag 2, entries 4, 5, 8, 9, 0xA and 0xC added and 0xD dropped, the largest
    // bag and 1000000000 money.
    int Laptop_RunEntry(GuestPtr<Laptop> laptop, GuestPtr<CityScene> city)
    {
        int mode = CityScene_GetMode(city);
        const uint32_t entry = port::load<uint32_t>(globals::kLaptopEntries + 4u * port::load<uint32_t>(laptop.address + 0x3D0u));
        const auto player = [&](int playerMode) {
            Player_SetMode(city->player, playerMode);
            return 1;
        };
        switch (entry)
        {
        case 0:
            return 6;
        case 1:
            return player(0);
        case 2:
            return player(1);
        case 3:
            return 3;
        case 4:
            return 7;
        case 5:
            globals::stageFlags.set(static_cast<uint16_t>(globals::stageFlags.get() ^ 1u));
            return mode;
        case 6:
        case 7:
        case 8:
            return 9;
        case 9:
            return player(3);
        case 10:
            return 8;
        case 11:
            return player(4);
        case 12:
            Event_Trigger(city->events, 6, city, GuestPtr<MapPerson>{0});
            Laptop_RemoveEntry(laptop, 0xC);
            return 1;
        case 13:
            Family_BefriendAll();
            globals::stageFlags.set(static_cast<uint16_t>(globals::stageFlags.get() | 2u));
            for (const int added : {4, 5, 8, 9, 0xA, 0xC})
                Laptop_AddEntry(laptop, added);
            Laptop_RemoveEntry(laptop, 0xD);
            port::store<uint16_t>(city->bag.address, 3);
            globals::stageState()->money = 1000000000;
            return mode;
        default:
            return mode;
        }
    }

    PORT_FN(Laptop_RunEntry);

    // ---- Object lifetime and start-up data (plan P6 batch 1) ----

    GuestPtr<Player> Player_DeletingDtor(GuestPtr<Player> player, int16_t deleteFlag)
    {
        if (player && 0 < deleteFlag)
            sdk::operator_delete(player.address);
        return player;
    }

    // The vtable goes back to the base class's before the object is deleted.
    GuestPtr<CharClass> CharType_dtor(GuestPtr<CharClass> cls, int16_t deleteFlag)
    {
        if (cls)
        {
            port::store<uint32_t>(cls.address + 0x20u, 0x001BCE70u);
            if (0 < deleteFlag)
                sdk::operator_delete(cls.address);
        }
        return cls;
    }

    GuestPtr<Markers> Markers_dtor(GuestPtr<Markers> markers, int16_t deleteFlag)
    {
        if (markers)
        {
            port::store<uint32_t>(markers.address + 0x128u, 0x001BCFC8u);
            if (0 < deleteFlag)
                sdk::operator_delete(markers.address);
        }
        return markers;
    }

    // The building types share one block (allocated by the first), which goes with the last.
    GuestPtr<BuildingType> BldgType_BaseDtor(GuestPtr<BuildingType> btype, int16_t deleteFlag)
    {
        if (btype)
        {
            port::store<uint32_t>(btype.address + 0x15Cu, 0x001BD000u);
            globals::buildingTypeInstances.set(globals::buildingTypeInstances.get() - 1);
            if (globals::buildingTypeInstances.get() == 0 && globals::buildingTypeShared.get() != 0u)
            {
                sdk::free(globals::buildingTypeShared.get());
                globals::buildingTypeShared.set(0u);
            }
            if (0 < deleteFlag)
                sdk::operator_delete(btype.address);
        }
        return btype;
    }

    // Deletes every building type object (virtual deleting destructor, vtable at +0x15C slot 2) and the table.
    void BldgType_DeleteAll()
    {
        for (int32_t i = 0; i < globals::buildingTypeCount.get(); ++i)
        {
            const uint32_t slot = globals::buildingTypeTable.get() + 4u * static_cast<uint32_t>(i);
            const uint32_t object = port::load<uint32_t>(slot);
            if (object == 0u)
                continue;
            port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(object + 0x15Cu) + 8u), object, 1);
            port::store<uint32_t>(globals::buildingTypeTable.get() + 4u * static_cast<uint32_t>(i), 0u);
        }
        sdk::mw_DeleteArray(globals::buildingTypeTable.get());
    }

    // Returns every event to the queue's free level, queues the ones still in the manager's slots, frees the two talk
    // events and the buffer at +0xAC.
    GuestPtr<EventMgr> EventMgr_dtor(GuestPtr<EventMgr> events, int16_t deleteFlag)
    {
        if (events)
        {
            const int queue = static_cast<int>(globals::eventQueueObject.get());
            for (int level = 5; level >= 0; --level)
                EventList_MoveAll(queue, level, 6);
            const uint32_t slotsEnd = events.address + 4u + 0xA0u;
            for (uint32_t slot = events.address + 4u; port::load<uint8_t>(slot) != 0 && slot < slotsEnd; slot += 0x20u)
                EventQueue_InsertByPriority(queue, GuestPtr<uint8_t>{slot}, 6, 0);
            for (int i = 0; i < 2; ++i)
            {
                const uint32_t talkAt = events.address + 0xA4u + 4u * static_cast<uint32_t>(i);
                const uint32_t talk = port::load<uint32_t>(talkAt);
                if (port::load<uint8_t>(talk + 8u) != 0)
                    EventQueue_InsertByPriority(queue, GuestPtr<uint8_t>{talk + 8u}, 6, 0);
                sdk::operator_delete(port::load<uint32_t>(talkAt));
            }
            sdk::operator_delete(port::load<uint32_t>(events.address + 0xACu));
            if (0 < deleteFlag)
                sdk::operator_delete(events.address);
        }
        return events;
    }

    // The player's bag object.
    void Bag_CreateGlobal()
    {
        const uint32_t memory = sdk::operator_new(0x1ACu);
        uint32_t bag = 0u;
        if (memory != 0u)
            bag = Bag_ctor(GuestPtr<Bag>{memory}).address;
        globals::bagObject.set(bag);
    }

    // The stamina record: 0, the limit 399999, 0.
    void Stamina_CreateGlobal()
    {
        const uint32_t record = sdk::operator_new(0x10u);
        if (record != 0u)
        {
            port::store<uint32_t>(record, 0u);
            port::store<uint32_t>(record + 8u, 0u);
            port::store<uint32_t>(record + 4u, 399999u);
        }
        globals::staminaObject.set(record);
    }

    // Loads DATA\BIN\SCHEDULE.BIN whole.
    void Schedule_Load()
    {
        port::GuestLocal<GameFile> file;
        File_ctor(file, 0);
        globals::scheduleData.set(static_cast<uint32_t>(File_LoadWhole(file, GuestPtr<char>{0x001B7F30u}, GuestPtr<int32_t>{0u})));
        File_dtor(file, -1);
    }

    // Loads DATA\BIN\CLAIM.BIN whole; words 2 to 8 are offsets (in words) of its sections, kept as pointers.
    void Claim_Load()
    {
        port::GuestLocal<GameFile> file;
        File_ctor(file, 0);
        globals::claimData.set(static_cast<uint32_t>(File_LoadWhole(file, GuestPtr<char>{0x001B5BB0u}, GuestPtr<int32_t>{0u})));
        struct Section
        {
            uint32_t global;
            uint32_t offsetWord;
        };
        const Section sections[] = {
            {globals::claimSection54.address, 0x08u},   {globals::claimSectionC.address, 0x0Cu},  {globals::claimSection5C.address, 0x10u},
            {globals::eventTileLists.address, 0x14u},  {globals::claimIconSets.address, 0x18u},  {globals::claimIconColours.address, 0x1Cu},
            {globals::claimSection74.address, 0x20u},
        };
        for (const Section &section : sections)
        {
            const uint32_t data = globals::claimData.get();
            port::store<uint32_t>(section.global, data + static_cast<uint32_t>(port::load<int32_t>(data + section.offsetWord)) * 4u);
        }
        File_dtor(file, -1);
    }

    // Loads DATA\BIN\GOODS.BIN whole; its first words are offsets (in words) of the sections, kept as pointers.
    void Goods_Load()
    {
        port::GuestLocal<GameFile> file;
        File_ctor(file, 0);
        globals::goodsData.set(static_cast<uint32_t>(File_LoadWhole(file, GuestPtr<char>{0x001B63D0u}, GuestPtr<int32_t>{0u})));
        const auto word = [](uint32_t index) { return port::load<int32_t>(globals::goodsData.get() + 4u * index); };
        const auto section = [&](uint32_t index) { return globals::goodsData.get() + static_cast<uint32_t>(word(index)) * 4u; };
        globals::goodsSectionA.set(globals::goodsData.get() + static_cast<uint32_t>(word(0)) * 4u);
        globals::goodsSectionB.set(section(1));
        globals::goodsSectionC.set(section(2));
        globals::goodsSectionD.set(section(3));
        globals::goodsSectionE.set(section(4));
        globals::goodsTextCount.set(port::load<int32_t>(globals::goodsData.get() + static_cast<uint32_t>(word(5)) * 4u));
        globals::shoppingTexts.set(globals::goodsData.get() + static_cast<uint32_t>(word(5)) * 4u + 4u);
        File_dtor(file, -1);
    }

    int StageObj_DeletingDtor(int stage, int16_t deleteFlag)
    {
        if (stage != 0 && 0 < deleteFlag)
            sdk::operator_delete(static_cast<uint32_t>(stage));
        return stage;
    }

    // Moves on to the next stage: resets the families, counts the stage up and reads its 0xB4-byte record from
    // DATA\BIN\STAGE.BIN (25 records, repeating). Past the 24th stage the target grows by half and the 32 bytes of
    // per-stage totals at +0x240 of four 0x10-byte blocks are cleared.
    void Stage_Load()
    {
        constexpr uint32_t kStageNumber = 0x001C5548u;
        constexpr uint32_t kStageRecord = 0x001C554Cu;
        Family_ResetForStage();
        const int32_t stage = port::load<int32_t>(kStageNumber) + 1;
        port::store<int32_t>(kStageNumber, stage);
        port::GuestLocal<GameFile> file;
        File_ctor(file, 0);
        File_Open(file, GuestPtr<char>{0x001B5D50u});
        File_Seek(file, (stage % 0x19) * 0xB4, 0, 0);
        File_Read(file, static_cast<int>(kStageRecord), 1, 0xB4, 0);
        File_Close(file);
        if (0x18 < stage)
        {
            int32_t target = static_cast<int32_t>(port::load<uint32_t>(kStageRecord) * 3u);
            if (target < 0)
                target += 1;
            port::store<int32_t>(kStageRecord, target >> 1);
            for (uint32_t block = 0; block < 4u; ++block)
                for (uint32_t half = 0; half < 8u; ++half)
                    port::store<uint16_t>(0x001C5380u + 0x10u * block + 0x240u + 2u * half, 0);
        }
        File_dtor(file, -1);
    }

    // The stage's target text: the population goal, and when the stage has a theme (word at 0x1C5550, not 0) the theme share
    // with the theme's name. The strings are in tables at 0x1B5C90 and 0x1B5D30.
    void Stage_FormatTargetText(int, GuestPtr<char> out)
    {
        constexpr uint32_t kStageTheme = 0x001C5550u;
        constexpr uint32_t kStageGoal = 0x001C554Cu;
        constexpr uint32_t kStageThemeShare = 0x001C5554u;
        sdk::sprintf(out.address, 0x001B5E70u, port::load<uint32_t>(0x001B5C90u + 4u * port::load<uint32_t>(kStageTheme)),
                     port::load<uint32_t>(kStageGoal));
        const int32_t theme = port::load<int32_t>(kStageTheme);
        if (port::load<int32_t>(kStageTheme) != 0)
        {
            const int length = sdk::strlen(out.address);
            sdk::sprintf(out.address + static_cast<uint32_t>(length), 0x001B5EB0u, port::load<uint32_t>(kStageThemeShare),
                         port::load<uint32_t>(0x001B5D30u + 4u * static_cast<uint32_t>(theme)));
        }
    }

    // The stage's goal text: the population goal, and for a themed stage the share with its closing words by theme.
    void Stage_FormatGoalText(int, GuestPtr<char> out)
    {
        constexpr uint32_t kStageTheme = 0x001C5550u;
        constexpr uint32_t kStageGoal = 0x001C554Cu;
        constexpr uint32_t kStageThemeShare = 0x001C5554u;
        sdk::sprintf(out.address, 0x001B5D68u, port::load<uint32_t>(kStageGoal));
        if (port::load<int32_t>(kStageTheme) != 0)
        {
            const int length = sdk::strlen(out.address);
            sdk::sprintf(out.address + static_cast<uint32_t>(length), 0x001B5D80u, port::load<uint32_t>(kStageThemeShare));
            const int32_t theme = port::load<int32_t>(kStageTheme);
            uint32_t ending;
            if (theme == 5)
                ending = 0x001B5C40u;
            else if (theme == 4)
                ending = 0x001B5E20u;
            else if (theme == 3)
                ending = 0x001B5E00u;
            else if (theme == 2)
                ending = 0x001B5DD0u;
            else if (theme == 1)
                ending = 0x001B5DB0u;
            else
                ending = 0x001B5E40u;
            sdk::strcat(out.address, ending);
        }
        sdk::strcat(out.address, 0x001B5E60u);
    }

    PORT_FN(Stage_FormatTargetText);
    PORT_FN(Stage_FormatGoalText);
    PORT_FN(StageObj_DeletingDtor);
    PORT_FN(Stage_Load);
    PORT_FN(Player_DeletingDtor);
    PORT_FN(CharType_dtor);
    PORT_FN(Markers_dtor);
    PORT_FN(BldgType_BaseDtor);
    PORT_FN(BldgType_DeleteAll);
    PORT_FN(EventMgr_dtor);
    PORT_FN(Bag_CreateGlobal);
    PORT_FN(Stamina_CreateGlobal);
    PORT_FN(Schedule_Load);
    PORT_FN(Claim_Load);
    PORT_FN(Goods_Load);

    // ---- Destructors (plan P6 batch 2) ----

    // The character classes: their own vtable, then CharType_dtor's base state.
    GuestPtr<CharClass> Char2_dtor(GuestPtr<CharClass> cls, int16_t deleteFlag)
    {
        if (cls)
        {
            port::store<uint32_t>(cls.address + 0x20u, 0x001BCE90u);
            CharType_dtor(cls, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(cls.address);
        }
        return cls;
    }

    GuestPtr<CharClass> Person_dtor(GuestPtr<CharClass> cls, int16_t deleteFlag)
    {
        if (cls)
        {
            port::store<uint32_t>(cls.address + 0x20u, 0x001BCEB0u);
            CharType_dtor(cls, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(cls.address);
        }
        return cls;
    }

    GuestPtr<CityCamera> CityCamera_DeletingDtor(GuestPtr<CityCamera> camera, int16_t deleteFlag)
    {
        if (camera)
        {
            Camera_DeletingDtor(GuestPtr<Camera>{camera.address}, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(camera.address);
        }
        return camera;
    }

    GuestPtr<LightSet> LightSet_DeletingDtor(GuestPtr<LightSet> lights, int16_t deleteFlag)
    {
        if (lights)
        {
            A3DObjectBase_DeletingDtor(GuestPtr<A3DObject>{lights.address}, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(lights.address);
        }
        return lights;
    }

    // The request queue object (+0) and the file (+0x20), each deleted with flag 1.
    GuestPtr<McManager> McManager_dtor(GuestPtr<McManager> mc, int16_t deleteFlag)
    {
        if (mc)
        {
            const uint32_t queue = port::load<uint32_t>(mc.address);
            if (queue != 0u)
            {
                McManagerSub_DeletingDtor(static_cast<int>(queue), 1);
                port::store<uint32_t>(mc.address, 0u);
            }
            const uint32_t file = port::load<uint32_t>(mc.address + 0x20u);
            if (file != 0u)
            {
                File_dtor(GuestPtr<GameFile>{file}, 1);
                port::store<uint32_t>(mc.address + 0x20u, 0u);
            }
            if (0 < deleteFlag)
                sdk::operator_delete(mc.address);
        }
        return mc;
    }

    // The application object: shuts down the sound driver, frees the display list and the font data.
    int App_dtor(int app, int16_t deleteFlag)
    {
        if (app != 0)
        {
            port::store<uint32_t>(static_cast<uint32_t>(app), 0x001BD8D0u);
            Sound_Shutdown();
            DmaList_Free();
            Font_Free();
            if (0 < deleteFlag)
                sdk::operator_delete(static_cast<uint32_t>(app));
        }
        return app;
    }

    PORT_FN(Char2_dtor);
    PORT_FN(Person_dtor);
    PORT_FN(CityCamera_DeletingDtor);
    PORT_FN(LightSet_DeletingDtor);
    PORT_FN(McManager_dtor);
    PORT_FN(App_dtor);

    // Reads DATA\BIN\CHARTYPE.BIN (24-byte records) and builds one character class object per record: kind 0 a plain type
    // (0x24 bytes), 1 a person (0x28 bytes), 2 a vehicle (0x24 bytes); the table at 0x1BDA3C holds the objects and 0x1BDA40
    // their number. A record of any other kind repeats the previous object (the compiler left the last one in a register).
    void CharType_LoadAll()
    {
        Schedule_Load();
        port::GuestLocal<GameFile> file;
        port::GuestLocal<uint32_t> size;
        File_ctor(file, 0);
        const uint32_t data = static_cast<uint32_t>(File_LoadWhole(file, GuestPtr<char>{0x001B5B90u}, GuestPtr<int32_t>{size.ptr().address}));
        const uint32_t count = *size / 0x18u;
        port::store<uint32_t>(0x001BDA40u, count);
        const uint32_t table = sdk::mw_NewArray(count << 2);
        port::store<uint32_t>(0x001BDA3Cu, table);
        uint32_t cls = port::guestS2();
        for (int32_t i = 0; i < static_cast<int32_t>(port::load<uint32_t>(0x001BDA40u)); ++i)
        {
            const uint32_t record = data + 0x18u * static_cast<uint32_t>(i);
            const uint8_t kind = port::load<uint8_t>(record);
            if (kind == 2)
            {
                const uint32_t memory = sdk::operator_new(0x24u);
                cls = memory != 0u ? Char2_ctor(GuestPtr<CharClass>{memory}, GuestPtr<uint8_t>{record}).address : 0u;
            }
            else if (kind == 1)
            {
                const uint32_t memory = sdk::operator_new(0x28u);
                cls = memory != 0u ? Person_ctor(GuestPtr<PersonClass>{memory}, GuestPtr<uint8_t>{record}).address : 0u;
            }
            else if (kind == 0)
            {
                const uint32_t memory = sdk::operator_new(0x24u);
                cls = memory != 0u ? CharType_ctor(GuestPtr<CharClass>{memory}, GuestPtr<uint8_t>{record}).address : 0u;
            }
            port::store<uint32_t>(port::load<uint32_t>(0x001BDA3Cu) + 4u * static_cast<uint32_t>(i), cls);
        }
        if (data != 0u)
            sdk::free(data);
        CharType_ClearSlots();
        File_dtor(file, -1);
    }

    PORT_FN(CharType_LoadAll);

    // The schedule a family member follows (0 to 0xF), from the member's age group (byte +2: under 4 a baby, under 7 a child, ...)
    // for the youngest groups, and for the others from the member's place in the family (member 0, 1 or the rest), whether the
    // family has a baby or school-age child (kinds 1 and 2) and whether the member has flag 1. Homes of type flag 0x400 and the
    // grown-up age groups always get 0xF.
    uint8_t Schedule_Evaluate(uint32_t family, int member)
    {
        constexpr uint32_t kTableA4 = 0x001BD9A4u, kTableA8 = 0x001BD9A8u, kTableAC = 0x001BD9ACu;
        const GuestPtr<Family> record = Family_Get(static_cast<int>(family), 0);
        const uint32_t self = record.address + 0x24u + 0x48u * static_cast<uint32_t>(member);
        const uint8_t age = port::load<uint8_t>(self + 2u);
        int children = 0;  // 1: a member under age group 2, 2: one under 6 (and none under 2)
        const uint32_t count = port::load<uint16_t>(record.address + 0x22u);
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint8_t group = port::load<uint8_t>(record.address + 0x24u + 0x48u * i + 2u);
            if (group < 2)
                children = 1;
            else if (group < 6 && children == 0)
                children = 2;
        }
        const bool flagged = (port::load<uint8_t>(self + 3u) & 1u) != 0;
        const int type = Family_GetTypeIndex(static_cast<int>(family & 0xFFFFu));
        const uint32_t typeFlags = port::load<uint16_t>(globals::buildingTypeAddress(static_cast<uint32_t>(type)) + 0x136u);
        const auto second = [&]() -> uint8_t {
            if (children == 1)
                return 9;
            if (children == 2)
                return 0xA;
            if (!flagged)
                return port::load<uint8_t>(kTableA8 + ((family + static_cast<uint32_t>(member)) & 1u));
            return port::load<uint8_t>(kTableA4 + static_cast<uint32_t>(remainder(static_cast<int32_t>(family + static_cast<uint32_t>(member)), 3)));
        };
        if (age < 4)
            return 1;
        if (age < 7)
            return 2;
        if (age < 0xD)
            return 3;
        if (age < 0x10)
            return 4;
        if (age < 0x13)
            return 5;
        if (age < 0x18)
        {
            if ((typeFlags & 0x400u) != 0)
                return 0xF;
            if (member == 0)
                return type == 0xB3 ? 6 : 0xF;
            if (member == 1)
                return second();
            return port::load<uint8_t>(kTableAC + static_cast<uint32_t>(remainder(static_cast<int32_t>(family + static_cast<uint32_t>(member)), 3)));
        }
        if (age < 0x3C)
        {
            if ((typeFlags & 0x400u) != 0 || member == 0)
                return 0xF;
            if (member == 1)
                return second();
            return port::load<uint8_t>(kTableA8 + static_cast<uint32_t>(remainder(static_cast<int32_t>(family + static_cast<uint32_t>(member)), 3)));
        }
        if ((typeFlags & 0x400u) != 0 || member == 0)
            return 0xF;
        if (member == 1)
            return children == 1 ? 9 : (children == 2 ? 0xA : 0xD);
        return 0xD;
    }

    PORT_FN(Schedule_Evaluate);

    // char: where a resident goes next. The activity comes from the person's schedule entry (mm_Schedule_PickEntry): the clock,
    // shifted by a spread of the person (the square root bits of 6 * family + member, mod 3600), picks the hour, and the row is
    // chosen by the holiday month (+2) and bad weather (+1). It is kept in the person (+0x27) and in *activity (its low 5 bits).
    // How a building is found depends on it: 0 the home (mm_Person_PlaceHomeObject); 1 and 2 a building of list 10 or 8, kept as
    // the target (its heading count goes up); 3 the target, or the workplace; 4 the work target; 5, 6, 11 and 12 the workplace;
    // 7 and 8 a building of lists 11 to 13 or 18 and 19, picked by the count of timer 0; 9 a random target; 10 a list from the
    // table at 0x1B0BD0 by schedule state and timer (36 with person flag 0x400; none gives the workplace); 13 the home target;
    // 14 a shop; 15 the workplace as the target; 16 a visit; 17 lists 18 and 19 or 13; 18 list 20. When nothing was found
    // (0x3F80) the person's target stands if it is on the tile the person is on (and a type with flag 0x80 resets the activity),
    // otherwise the workplace. The result is the building.
    int16_t Person_ChooseDestination(GuestPtr<CityScene> city, GuestPtr<MapPerson> person, GuestPtr<uint32_t> activity, int exclude)
    {
        const int x = sdk::fptosi(ps2f(64.0f) + person->x);
        const int z = sdk::fptosi(ps2f(64.0f) - person->z);
        uint32_t destination = kNoObject;
        const int32_t timer = static_cast<int32_t>(port::readHardware32(0x10000000u));
        if ((person->flags & 0x200u) != 0)
        {
            person->flags = static_cast<uint16_t>(person->flags & 0xFDFFu);
            const GuestPtr<MapPerson> other = city->map->people + person->follow;
            other->flags = static_cast<uint16_t>(other->flags & 0xFFDFu);
        }
        if (person->schedState == 0)
            person->schedState = Schedule_Evaluate(person->family, person->member);
        const GuestPtr<MapObject> workRecord = Map_GetRecord(city->map, person->work);
        const int flag8 = CityScene_HasFlag8(city);
        const int entry = Schedule_PickEntry(person->family, person->member, workRecord->type, person->schedState, flag8, person->flags & 0x10u);
        int32_t shifted = city->clock - static_cast<int32_t>(Math_SqrtBits(person->member + 6 * person->family)) % 3600;
        if (shifted < 0)
            shifted += 86400;
        int row = (city->flags & 2u) != 0 ? 2 : 0;
        if (city->weatherToday != 0)
            row += 1;
        *activity = port::load<uint8_t>(static_cast<uint32_t>(entry + shifted / 3600 + 24 * row)) & 0x1Fu;
        person->activity = static_cast<uint8_t>(*activity);
        const int16_t skip = static_cast<int16_t>(exclude);
        const auto findIn = [&](int list, int mode, int16_t skipB) {
            return static_cast<uint16_t>(Person_FindBuildingInList(city, list, x, z, mode, static_cast<int16_t>(person->work), skipB));
        };
        // Activities 1 and 2: the target stands unless there is none or it is the workplace; a new one has its heading count raised.
        const auto targetFromList = [&](int list) {
            if (person->target == kNoObject || person->target == person->work)
            {
                person->target = static_cast<uint16_t>(findIn(list, 0, kNoObject));
                if (person->target != kNoObject)
                {
                    const GuestPtr<MapObject> goal = Map_GetRecord(city->map, person->target);
                    goal->headingHere = static_cast<uint8_t>(goal->headingHere + 1);
                }
            }
            return static_cast<uint32_t>(person->target);
        };
        switch (static_cast<uint32_t>(*activity))
        {
        case 0:
            destination = Person_PlaceHomeObject(city, person) & 0xFFFFu;
            break;
        case 1:
            destination = targetFromList(0xA);
            break;
        case 2:
            destination = targetFromList(8);
            break;
        case 3:
            destination = person->target == kNoObject ? person->work : person->target;
            break;
        case 4:
            destination = Person_TargetWork(city, person);
            break;
        case 5:
        case 6:
        case 11:
        case 12:
            destination = person->work;
            break;
        case 7:
            destination = findIn(0xB + timer % 3, 1, skip);
            break;
        case 8:
            destination = findIn(0x12 + (timer & 1), 1, skip);
            break;
        case 9:
            destination = Person_RandomTarget(city, person, x, z);
            break;
        case 10:
        {
            const int list = (person->flags & 0x400u) != 0
                                 ? 0x24
                                 : port::load<uint8_t>(0x001B0BD0u + 8u * person->schedState + static_cast<uint32_t>(timer & 7));
            destination = list == 0 ? person->work : findIn(list, 1, skip);
            break;
        }
        case 13:
            destination = static_cast<uint16_t>(Person_TargetHome(city, person));
            break;
        case 14:
            destination = Person_TargetShop(city, person, x, z);
            break;
        case 15:
            person->target = person->work;
            destination = person->work;
            break;
        case 16:
            destination = Person_TargetVisit(city, person);
            break;
        case 17:
        {
            int list = 0x12 + timer % 3;
            if (list == 0x14)
                list = 0xD;
            destination = findIn(list, 1, skip);
            break;
        }
        case 18:
            destination = findIn(0x14, 1, skip);
            break;
        default:
            break;
        }
        if (static_cast<uint16_t>(destination) == kNoObject)
        {
            if (person->target == kNoObject)
                destination = person->work;
            else
            {
                const GuestPtr<MapObject> goal = Map_GetRecord(city->map, person->target);
                const GuestPtr<MapObject> here = Map_GetTileFirstObject(city->map, x + (z << 7));
                if (goal->tile != here->tile)
                    destination = person->work;
                else
                {
                    destination = person->target;
                    if ((globals::buildingTypeFlags(goal->type) & 0x80u) != 0)
                        person->activity = 0;
                }
            }
        }
        return static_cast<int16_t>(destination);
    }

    PORT_FN(Person_ChooseDestination);

    // Placing a Small Goods item from the bag with the player's cursor (mode 5): the item's footprint (the size byte at +0x142 of its
    // building type, 0x115 + the bag item's index) is centred on the tile under the cursor (half a tile further for an odd size)
    // and tested (mm_Map_CanPlace); the footprint is highlighted in blue when it can be placed and red when not. Cross places it
    // (mm_Map_PlaceSmallGoods, sound 3, one of the item used up) or, when it cannot be placed, plays sound 4; Triangle cancels
    // (sound 2, the item kept). Placing and cancelling, and an item with no setup kind (-1), take the player back to its previous
    // mode with the step, sway and target cleared.
    void Player_PlaceBagItem(GuestPtr<Player> player, GuestPtr<CityScene> city)
    {
        port::GuestFrame<0xB0> frame;
        const uint32_t sp = frame.ptr().address;
        const auto leave = [&]() {
            const int32_t previous = player->previousMode;
            player->previousMode = player->mode;
            player->mode = previous;
            port::store<uint32_t>(player.address + 0x80u, 0u);
            port::store<uint32_t>(player.address + 0x8u, 0u);
            port::store<uint32_t>(player.address + 0x34u, 0u);
            player->target = -1;
            Pad_ClearBit40();
        };
        const uint32_t kind = BagMenu_GetSetupKind(city->bag, GuestPtr<uint32_t>{sp + 0xACu});
        if (kind == 0xFFFFFFFFu)
        {
            leave();
            return;
        }
        const uint32_t typeIndex = port::load<uint32_t>(sp + 0xACu) + 0x115u;
        const int32_t size = port::load<uint8_t>(globals::buildingTypeAddress(typeIndex) + 0x142u);
        const ps2f half = fromInt(size & 1) / ps2f(2.0f);
        const int x = sdk::fptosi(ps2f(64.0f) + (player->position.x + half - ps2f(0.5f)));
        const int z = sdk::fptosi(ps2f(64.0f) - (ps2f(0.5f) + (player->position.z - half)));
        const int before = (size - 1) / 2, after = size / 2;
        const int x0 = x - before, z0 = z - before;
        port::store<int32_t>(sp + 0x80u, x0);
        port::store<int32_t>(sp + 0x84u, z0);
        port::store<int32_t>(sp + 0x88u, x + after);
        port::store<int32_t>(sp + 0x8Cu, z + after);
        const int canPlace = Map_CanPlace(city->map, 0, static_cast<int>(typeIndex), x0, z0);
        if (canPlace == 1)
        {
            port::store<uint32_t>(sp + 0x90u, 0u);
            port::store<uint32_t>(sp + 0x94u, 0u);
            port::store<uint32_t>(sp + 0x98u, 0x437F0000u);
            port::store<uint32_t>(sp + 0x9Cu, 0x42800000u);
        }
        else
        {
            port::store<uint32_t>(sp + 0x90u, 0x437F0000u);
            port::store<uint32_t>(sp + 0x94u, 0u);
            port::store<uint32_t>(sp + 0x98u, 0u);
            port::store<uint32_t>(sp + 0x9Cu, 0x42800000u);
        }
        Map_SetPlacementHighlight(city->map, 1, GuestPtr<int32_t>{sp + 0x80u}, GuestPtr<uint32_t>{sp + 0x90u});
        if ((Pad_Pressed(0) & 0x40u) != 0)
        {
            if (canPlace == 1)
            {
                Map_PlaceSmallGoods(city->map, x0, z0, static_cast<int>(city.address), static_cast<uint8_t>(kind),
                                    static_cast<int>(port::load<uint32_t>(sp + 0xACu)));
                Sound_PlaySe(3, 0, 0x3C, 0.0f, 0.0f);
                BagMenu_FinishUse(city->bag, 1);
                leave();
            }
            else
                Sound_PlaySe(4, 0, 0x3C, 0.0f, 0.0f);
            return;
        }
        if ((Pad_Pressed(0) & 0x10u) != 0)
        {
            Sound_PlaySe(2, 0, 0x3C, 0.0f, 0.0f);
            BagMenu_FinishUse(city->bag, 0);
            leave();
        }
    }

    PORT_FN(Player_PlaceBagItem);

    // One frame of the phone calls (four queued, the first shown; each entry: the person, or 6000 for a call from a family member
    // without a person on the map, the family, the member and a flag). State 0 (nothing shown) takes the first queued call: the
    // pad focus is taken and the state becomes 1 (the entry is +8). State 1 (ringing) builds the caller's text once ("Unknown", the
    // family without friendship, or the member's name) and shows it as a notice; Circle accepts (state 2); the call ends after 600
    // frames, with Square, when the family is not in the city or (for a call of the family) when its flag is set and the city is not
    // popular, or when the person has no place on the map. State 2 waits 30 frames and then starts a talk with the person or the
    // member (a call of the family only when its flag is 0 or the city is popular with fewer than 32 applicants waiting) and ends
    // the call. Ending a call moves the later ones up, gives the pad back and swaps the sign of +0x40 (the notice is cleared when it
    // was shown). The result is 1 when no call is on.
    int PhoneCall_Update(GuestPtr<PhoneCalls> phone, GuestPtr<CityScene> city)
    {
        const uint32_t p = phone.address;
        const uint32_t c = city.address;
        const auto entryOf = [&](int32_t index) { return p + 0x10u + 12u * static_cast<uint32_t>(index); };
        const auto endCall = [&](bool clearNotice) {
            int32_t index = port::load<int32_t>(p + 8u);
            for (; index < 3; ++index)
            {
                const uint32_t to = entryOf(index), from = to + 12u;
                port::store<uint32_t>(to, port::load<uint32_t>(from));
                port::store<uint16_t>(to + 4u, port::load<uint16_t>(from + 4u));
                port::store<uint16_t>(to + 6u, port::load<uint16_t>(from + 6u));
                port::store<uint32_t>(to + 8u, port::load<uint32_t>(from + 8u));
            }
            port::store<uint16_t>(entryOf(index) + 4u, 0);
            Pad_ReleaseFocus(1);
            port::store<int32_t>(p + 0xCu, 0);
            port::store<int32_t>(p + 4u, 0);
            port::store<int32_t>(p + 0x40u, -port::load<int32_t>(p + 0x40u));
            if (clearNotice)
                Notice_Clear(city->notice);
        };
        // The object the person is standing on, if the map has one for the person's tile.
        const auto objectOf = [&](uint32_t person) {
            const GuestPtr<MapPerson> who = city->map->people + static_cast<int>(person);
            const int x = sdk::fptosi(ps2f(64.0f) + who->x);
            const int z = sdk::fptosi(ps2f(64.0f) - who->z);
            return Map_FindPersonObjectOnTile(city->map, person & 0xFFFFu, static_cast<uint32_t>(x + (z << 7)) & 0xFFFFu);
        };
        const int32_t state = port::load<int32_t>(p + 4u);
        if (state == 0)
        {
            for (int32_t i = 0; i < 4; ++i)
            {
                if (port::load<uint16_t>(entryOf(i) + 4u) == 0)
                    continue;
                port::store<int32_t>(p + 4u, 1);
                port::store<uint8_t>(p + 0x48u, 0);
                port::store<int32_t>(p + 8u, i);
                port::store<int32_t>(p + 0xCu, 0);
                Pad_TakeFocus(1, 0x1F0);
                port::store<int32_t>(p + 0x40u, 1);
                port::store<int32_t>(p + 0x44u, 0);
                break;
            }
        }
        else if (state == 1)
        {
            const uint32_t entry = entryOf(port::load<int32_t>(p + 8u));
            if (port::load<int8_t>(p + 0x48u) == 0)
            {
                const uint32_t family = port::load<uint16_t>(entry + 4u);
                const uint32_t record = static_cast<uint32_t>(Family_Prefetch(static_cast<int>(family), 0));
                if (record != 0u)
                {
                    if (Family_GetTypeIndex(static_cast<int>(family)) == 0xB2)
                        sdk::sprintf(p + 0x48u, 0x001B8198u);
                    else if (Family_GetFriendship(static_cast<int>(family), port::load<uint16_t>(entry + 6u)) == 0)
                        sdk::sprintf(p + 0x48u, 0x001B81B0u);
                    else
                        sdk::sprintf(p + 0x48u, 0x001B81C8u, record + 0x48u * port::load<uint16_t>(entry + 6u) + 0x4Cu);
                }
            }
            if (port::load<int8_t>(p + 0x48u) != 0)
                Notice_ShowCentered(city->notice, GuestPtr<char>{p + 0x48u}, 0x001B8170);
            if ((Pad_Pressed(1) & 0x20u) != 0)
            {
                port::store<int32_t>(p + 0xCu, 0);
                port::store<int32_t>(p + 4u, 2);
                Notice_Clear(city->notice);
            }
            else
            {
                const int32_t counter = port::load<int32_t>(p + 0xCu) + 1;
                port::store<int32_t>(p + 0xCu, counter);
                if (counter >= 600 || (Pad_Pressed(1) & 0x80u) != 0)
                    endCall(true);
                else
                {
                    const uint32_t shown = entryOf(port::load<int32_t>(p + 8u));
                    const uint32_t family = port::load<uint16_t>(shown + 4u);
                    if (port::load<int32_t>(shown) == 6000)
                    {
                        if (Family_InCity(family) != 0 || (port::load<int32_t>(shown + 8u) != 0 && (city->flags & 4u) == 0))
                            endCall(true);
                    }
                    else if (Family_InCity(family) == 0 || !objectOf(port::load<uint32_t>(shown)))
                        endCall(true);
                }
            }
        }
        else
        {
            const int32_t counter = port::load<int32_t>(p + 0xCu) + 1;
            port::store<int32_t>(p + 0xCu, counter);
            if (counter >= 0x1E)
            {
                const uint32_t shown = entryOf(port::load<int32_t>(p + 8u));
                const uint32_t family = port::load<uint16_t>(shown + 4u);
                if (port::load<int32_t>(shown) == 6000)
                {
                    if (port::load<int32_t>(shown + 8u) == 0 ||
                        ((city->flags & 4u) != 0 && port::load<int32_t>(port::load<uint32_t>(c + 0x54u) + 0x280u) < 0x20))
                        CityScene_StartTalk(city, 1, static_cast<int>(family), port::load<uint16_t>(shown + 6u), 3);
                }
                else if (Family_InCity(family) != 0)
                {
                    const GuestPtr<MapObject> object = objectOf(port::load<uint32_t>(shown));
                    if (object)
                        CityScene_StartTalk(city, 0, object->id, port::load<int32_t>(shown), 3);
                }
                endCall(false);
            }
        }
        return port::load<int32_t>(p + 4u) == 0 ? 1 : 0;
    }

    PORT_FN(PhoneCall_Update);

    namespace
    {
        // The schedule table (0x1BDBAC points at it) has rows of 96 bytes: 24 hours for each of four variants (+2 in a holiday month, +1
        // in bad weather). The column of the hour is the city's clock shifted back by a spread of the family member (the square root
        // bits of 6 * family + member, mod 3600), so people do not all change at the same second.
        uint8_t scheduleByte(GuestPtr<CityScene> city, uint32_t family, int member, uint32_t row)
        {
            int32_t shifted = city->clock - static_cast<int32_t>(Math_SqrtBits(member + 6 * static_cast<int>(family))) % 3600;
            if (shifted < 0)
                shifted += 86400;
            int variant = (city->flags & 2u) != 0 ? 2 : 0;
            if (city->weatherToday != 0)
                variant += 1;
            return port::load<uint8_t>(port::load<uint32_t>(0x001BDBACu) + 96u * row + static_cast<uint32_t>(shifted / 3600 + 24 * variant));
        }

        // The row of a family type's own schedule: the entry (the schedule state), or for 0xF the type's kind (+0x14B) after row 15.
        uint32_t scheduleRow(uint32_t entry, uint32_t type)
        {
            return entry == 0xFu ? port::load<uint8_t>(globals::buildingTypeAddress(type) + 0x14Bu) + 0xFu : entry;
        }

        // The row a person's schedule uses: the schedule state is worked out first when it is 0; a city with flag 8 uses row 29, a
        // person with flag 0x10 row 30, any other the state's row (0xF: the workplace type's).
        uint32_t personScheduleRow(GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
        {
            if (person->schedState == 0)
                person->schedState = Schedule_Evaluate(person->family, person->member);
            const GuestPtr<MapObject> work = Map_GetRecord(city->map, person->work);
            const bool lifted = (person->flags & 0x10u) != 0;
            if (CityScene_HasFlag8(city) != 0)
                return 29;
            if (lifted)
                return 30;
            return scheduleRow(person->schedState, work->type);
        }
    }

    // char: the schedule of a family member at this hour, its upper three bits: when they are set, the kind (+0x14C) of the
    // building type of the family, else 0.
    uint8_t Schedule_QueryA(GuestPtr<CityScene> city, uint32_t family, int member)
    {
        const int type = Family_GetTypeIndex(static_cast<int>(family & 0xFFFFu));
        const uint32_t row = scheduleRow(Schedule_Evaluate(family, member), static_cast<uint32_t>(type));
        const uint32_t byte = scheduleByte(city, family, member, row);
        return ((byte >> 5) & 7u) == 0 ? 0 : port::load<uint8_t>(globals::buildingTypeAddress(static_cast<uint32_t>(type)) + 0x14Cu);
    }

    // The same lookup's activity: the low five bits of the schedule byte.
    uint8_t Schedule_QueryB(GuestPtr<CityScene> city, uint32_t family, int member)
    {
        const int type = Family_GetTypeIndex(static_cast<int>(family & 0xFFFFu));
        const uint32_t row = scheduleRow(Schedule_Evaluate(family, member), static_cast<uint32_t>(type));
        return scheduleByte(city, family, member, row) & 0x1Fu;
    }

    // What the person's schedule makes of the building it heads for (the target, or the workplace with none): the kind of the
    // building's type (+0x14C) when the schedule byte's upper part is not 0, else 0.
    uint8_t Schedule_GetActivity(GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        const uint32_t row = personScheduleRow(city, person);
        const uint32_t byte = scheduleByte(city, person->family, person->member, row);
        if (((byte >> 5) & 7u) == 0)
            return 0;
        const GuestPtr<MapObject> goal = person->target == kNoObject ? Map_GetRecord(city->map, person->work) : Map_GetRecord(city->map, person->target);
        return port::load<uint8_t>(globals::buildingTypeAddress(goal->type) + 0x14Cu);
    }

    // The same lookup's activity: the low five bits of the person's schedule byte.
    uint8_t Schedule_GetBuildingActivity(GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
    {
        const uint32_t row = personScheduleRow(city, person);
        return scheduleByte(city, person->family, person->member, row) & 0x1Fu;
    }

    PORT_FN(Schedule_QueryA);
    PORT_FN(Schedule_QueryB);
    PORT_FN(Schedule_GetActivity);
    PORT_FN(Schedule_GetBuildingActivity);

    namespace
    {
        // The model of a person drawn with a class: part 0 to 3 by the low bits of the person's flags, else (flags 0) part 0 for an
        // odd family and part 3 for an even one.
        uint32_t personPart(GuestPtr<CharClass> cls, GuestPtr<MapPerson> person)
        {
            uint32_t index = static_cast<uint32_t>(person->flags) & 3u;
            if (index == 0)
                index = (person->family & 1u) != 0 ? 0u : 3u;
            return port::load<uint32_t>(cls.address + 4u * index);
        }

        // Whether a model still fits the frame's draw budget (two counters, at 0x1BDA28 against 0x708 and at 0x1BDA34 against the
        // limit at 0x1BD980), by the cost halfword (+0x18) of the model's header.
        bool withinDrawBudget(uint32_t model)
        {
            const int32_t cost = port::load<int16_t>(port::load<uint32_t>(model) + 0x18u);
            return cost + port::load<int32_t>(0x001BDA28u) < 0x708 && cost + port::load<int32_t>(0x001BDA34u) < port::load<int32_t>(0x001BD980u);
        }

        // Whether the claim meter of the person's workplace has reached the first threshold of the table at 0x1BDA50 (+2).
        int workplaceClaimed(GuestPtr<CityScene> city, GuestPtr<MapPerson> person)
        {
            return Map_GetRecord(city->map, person->work)->claimMeter >= port::load<uint16_t>(port::load<uint32_t>(0x001BDA50u) + 2u) ? 1 : 0;
        }

        float floatAt(uint32_t address) { return std::bit_cast<float>(port::load<uint32_t>(address)); }
    }

    // Draws a person with a pose of the class's model. The model's matrix is turned by the heading in the position block (t1:
    // x, y, z at +0 to +8 place the model, the heading is at +0x0C, and +4 is also checked to be 0.5 or less before drawing) while the
    // person's own height eases to the ground under it (0.1 a call, or at once from over 0.2 away). The animation (t2) is 7 for
    // the two first ones while the person is not in flag 0x100; the person's frame (+0x1C) steps on by the animation's table
    // (0x1AF080) and restarts at its table's value (0x1AF050: -1 a fifth of the length, -2 the class's start for the person's
    // flags, else the value). People whose friendship reaches 48 turn to the player (but not in animation 5).
    void Person_Draw(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> person, int colour, int flags, GuestPtr<int32_t> position, int animation)
    {
        port::GuestFrame<0xC0> frame; // the callees' frames land where they did under the original's
        const uint32_t model = personPart(cls, person);
        if (!withinDrawBudget(model))
            return;
        person->flags = static_cast<uint16_t>(person->flags | 0x1000u);
        const ps2f height = Map_SampleHeight(city->map, person->x, person->z);
        const ps2f y = person->y;
        if (y < height)
        {
            if (!(height - y <= ps2f(0.2f)))
                person->y = height;
            else
            {
                const ps2f up = y + ps2f(0.1f);
                person->y = up;
                if (!(up <= height))
                    person->y = height;
            }
        }
        else if (!(y <= height))
        {
            if (!(y - height <= ps2f(0.2f)))
                person->y = height;
            else
            {
                const ps2f down = y - ps2f(0.1f);
                person->y = down;
                if (down < height)
                    person->y = height;
            }
        }
        if (!(ps2f(floatAt(position.address + 4u)) <= ps2f(0.5f)))
            return;
        const uint32_t matrix = model + 0x10u;
        sdk::sceVu0RotMatrixY(matrix, 0x001B2550u, ps2f(floatAt(position.address + 0xCu)));
        port::store<uint32_t>(matrix + 0x30u, port::load<uint32_t>(position.address));
        port::store<uint32_t>(matrix + 0x34u, port::load<uint32_t>(position.address + 4u));
        port::store<uint32_t>(matrix + 0x38u, port::load<uint32_t>(position.address + 8u));
        const uint32_t frameAt = person.address + 0x1Cu;
        if ((person->flags & 0x100u) == 0)
        {
            if (animation < 2)
                animation = 7;
            else if (animation == 6)
                port::store<uint16_t>(frameAt, 0);
        }
        const int length = sdk::fptosi(A3D_GetMeshField0C(GuestPtr<A3DObject>{model}, animation));
        port::store<uint16_t>(frameAt, static_cast<uint16_t>(port::load<uint16_t>(frameAt) + port::load<uint16_t>(0x001AF080u + 4u * static_cast<uint32_t>(animation))));
        if (!(static_cast<int32_t>(port::load<uint16_t>(frameAt)) < length))
        {
            const int32_t restart = port::load<int32_t>(0x001AF050u + 4u * static_cast<uint32_t>(animation));
            if (restart == -1)
                port::store<uint16_t>(frameAt, static_cast<uint16_t>(length / 5));
            else if (restart == -2)
                port::store<uint16_t>(frameAt, port::load<uint16_t>(cls.address + 0x10u + 2u * (person->flags & 3u)));
            else
                port::store<uint16_t>(frameAt, static_cast<uint16_t>(restart));
        }
        const int attach = animation == 6 ? 0 : animation == 5 ? 1 : 4;
        const int claimed = workplaceClaimed(city, person);
        const uint32_t activity = Schedule_GetActivity(city, person);
        const int shifted = (activity & 0x80u) != 0 ? 1 : 0;
        const int inside = (person->flags & 0x400u) != 0 ? 1 : 0;
        Instance_DrawSwaying2(GuestPtr<A3DObject>{model}, animation, fromInt(port::load<uint16_t>(frameAt)));
        A3DObject_UpdateMatrices(GuestPtr<A3DObject>{model});
        if (animation != 5 && Family_GetFriendship(person->family, person->member) >= 0x30)
            Person_TurnToPlayer(cls, city, person);
        Instance_DrawModelEx(GuestPtr<A3DObject>{model}, GuestPtr<Vec4>{static_cast<uint32_t>(colour)}, flags, claimed, static_cast<int>(activity & 0x7Fu), shifted, inside, attach);
    }

    PORT_FN(Person_Draw);

    // The same with a pose already chosen: the class's part and the schedule byte are those of the person, or (with none) of the
    // family and member given: the member's part byte (+0x2B of its record) when set, else by the family's parity as above. The
    // pose of the talk session for the family and member is fetched into a local (claim state first for a person). The matrix is
    // copied to the model and the animation phase (*phase, not before 0) goes on by 2 a call until it passes the length of the
    // mesh (*mesh), when it restarts and the mesh switches between 2 and 3.
    void Person_Draw2(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> person, uint32_t family, uint32_t member,
                      GuestPtr<Vec4> matrix, int colour, GuestPtr<int32_t> mesh, GuestPtr<ps2f> phase)
    {
        port::GuestFrame<0xB0> frame;
        const uint32_t local = frame.ptr().address + 0xACu;
        uint32_t model;
        uint32_t activity;
        if (person)
        {
            model = personPart(cls, person);
            port::store<int32_t>(local, workplaceClaimed(city, person));
            Talk_LookupPose(city->talk, person->family, person->member, GuestPtr<uint32_t>{local});
            activity = Schedule_GetActivity(city, person);
        }
        else
        {
            const uint32_t part = port::load<uint8_t>(Family_Get(static_cast<int>(family), 0).address + 0x48u * member + 0x2Bu);
            model = port::load<uint32_t>(cls.address + 4u * (part != 0 ? part : ((family & 1u) != 0 ? 0u : 3u)));
            activity = Schedule_QueryA(city, family, static_cast<int>(member));
            port::store<int32_t>(local, 0);
            Talk_LookupPose(city->talk, family, member, GuestPtr<uint32_t>{local});
        }
        for (uint32_t offset = 0; offset < 0x40u; offset += 8u)
            port::store<uint64_t>(model + 0x10u + offset, port::load<uint64_t>(matrix.address + offset));
        const ps2f now{floatAt(phase.address)};
        if (!(now < ps2f(0.0f)))
        {
            const ps2f length = A3D_GetMeshField0C(GuestPtr<A3DObject>{model}, port::load<int32_t>(mesh.address));
            const ps2f next = ps2f(floatAt(phase.address)) + ps2f(2.0f);
            port::store<uint32_t>(phase.address, std::bit_cast<uint32_t>(next.v));
            if (!(next < length))
            {
                port::store<uint32_t>(phase.address, 0u);
                port::store<int32_t>(mesh.address, port::load<int32_t>(mesh.address) == 2 ? 3 : 2);
            }
        }
        const int shifted = (activity & 0x80u) != 0 ? 1 : 0;
        const int inside = person && (person->flags & 0x400u) != 0 ? 1 : 0;
        if (!withinDrawBudget(model))
            return;
        Instance_DrawSwaying2(GuestPtr<A3DObject>{model}, port::load<int32_t>(mesh.address), ps2f(floatAt(phase.address)));
        A3DObject_UpdateMatrices(GuestPtr<A3DObject>{model});
        Instance_DrawModelEx(GuestPtr<A3DObject>{model}, GuestPtr<Vec4>{static_cast<uint32_t>(colour)}, 0, port::load<int32_t>(local), static_cast<int>(activity & 0x7Fu), shifted, inside, 4);
    }

    PORT_FN(Person_Draw2);

    // Placing the selected applicant's building with the player's cursor (mode 6): the footprint of the building type of the family
    // (the size byte at +0x142 of the type) is centred on the tile under the cursor as for a bag item and tested (mm_Map_CanPlace for
    // the family; 1 means free and is highlighted in blue). Any other answer but 2 asks mm_Placement_FindSpot for a spot nearby
    // (red when there is none); the highlight is mode 2 (cleared) when the spot is taken. Cross places it (levels the ground, the
    // applicant's state 3 and its tile, back to the list, sound 3) when it can be placed, else plays sound 4; Triangle goes back to the
    // list (sound 2).
    void Player_PlaceForApplicant(GuestPtr<Player> player, GuestPtr<CityScene> city)
    {
        port::GuestFrame<0xD0> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t entry = static_cast<uint32_t>(Applicants_GetSelected(city->applicants));
        const uint32_t family = port::load<uint16_t>(entry + 4u);
        const int typeIndex = Family_GetTypeIndex(static_cast<int>(family));
        const int32_t size = port::load<uint8_t>(globals::buildingTypeAddress(static_cast<uint32_t>(typeIndex)) + 0x142u);
        const ps2f half = fromInt(size & 1) / ps2f(2.0f);
        const int x = sdk::fptosi(ps2f(64.0f) + (player->position.x + half - ps2f(0.5f)));
        const int z = sdk::fptosi(ps2f(64.0f) - (ps2f(0.5f) + (player->position.z - half)));
        const int before = (size - 1) / 2, after = size / 2;
        const int x0 = x - before, z0 = z - before;
        port::store<int32_t>(sp + 0xB0u, x0);
        port::store<int32_t>(sp + 0xB4u, z0);
        port::store<int32_t>(sp + 0xB8u, x + after);
        port::store<int32_t>(sp + 0xBCu, z + after);
        int placeable = Map_CanPlace(city->map, static_cast<int>(family), typeIndex, x0, z0);
        const GuestPtr<int32_t> rect{sp + 0xB0u};
        const GuestPtr<uint32_t> colour{sp + 0xC0u};
        if (placeable == 1)
        {
            port::store<uint32_t>(sp + 0xC0u, 0u);
            port::store<uint32_t>(sp + 0xC4u, 0u);
            port::store<uint32_t>(sp + 0xC8u, 0x437F0000u);
            port::store<uint32_t>(sp + 0xCCu, 0x42800000u);
            Map_SetPlacementHighlight(city->map, 1, rect, colour);
        }
        else if (placeable == 2)
            Map_SetPlacementHighlight(city->map, 2, rect, colour);
        else
        {
            placeable = Placement_FindSpot(player, city, static_cast<int>(family), typeIndex, x, z, before, rect);
            if (placeable != 0)
                Map_SetPlacementHighlight(city->map, 2, rect, colour);
            else
            {
                port::store<uint32_t>(sp + 0xC0u, 0x437F0000u);
                port::store<uint32_t>(sp + 0xC4u, 0u);
                port::store<uint32_t>(sp + 0xC8u, 0u);
                port::store<uint32_t>(sp + 0xCCu, 0x42800000u);
                Map_SetPlacementHighlight(city->map, 1, rect, colour);
            }
        }
        if ((Pad_Pressed(0) & 0x40u) != 0)
        {
            if (placeable == 0)
            {
                Sound_PlaySe(4, 0, 0x3C, 0.0f, 0.0f);
                return;
            }
            Map_LevelArea(city->map, port::load<int32_t>(sp + 0xB0u), port::load<int32_t>(sp + 0xB4u), size, static_cast<int>(city.address));
            port::store<uint16_t>(entry + 2u, 3);
            port::store<uint16_t>(entry + 6u, static_cast<uint16_t>(port::load<int16_t>(sp + 0xB0u) + (port::load<int16_t>(sp + 0xB4u) << 7)));
            Applicants_StartPlacing(city->applicants, city);
            Sound_PlaySe(3, 0, 0x3C, 0.0f, 0.0f);
            return;
        }
        if ((Pad_Pressed(0) & 0x10u) != 0)
        {
            Sound_PlaySe(2, 0, 0x3C, 0.0f, 0.0f);
            Applicants_StartPlacing(city->applicants, city);
        }
    }

    PORT_FN(Player_PlaceForApplicant);

    // One update of a vehicle (a person record of the vehicle classes, its driver the person at +0x28, 6000 for none). With the
    // time flag 2 a vehicle in a building whose driver's activity has changed heads for the exit (walk tile = the building's tile).
    // Inside a building: on the building's own tile the driver gets out (the vehicle outside, both given the "nowhere" tiles, the
    // driver put where the vehicle is, no driver left); on another tile the vehicle follows the path (mm_Path_NextWaypoint at the
    // walk tile 0x4000 or on the tile of the first object at it) or moves by the kind of its class (flag 0x1000: B, else A), the
    // driver carried along. Outside: a counter (+0x2E, 3 a call) runs to 0xFD20, when the vehicle is taken off its tile and put
    // back on its workplace's. With flag 0x800 the vehicle is then linked to the object under it (or its building); flag 0x1000 is cleared.
    void Char2_Update(GuestPtr<CharClass> cls, GuestPtr<CityScene> city, GuestPtr<MapPerson> vehicle, uint32_t timeFlags)
    {
        const uint32_t v = vehicle.address;
        constexpr uint16_t kNowhere = 0x3F80, kNoDriver = 0x1770, kNoTile = 0x4000;
        if ((timeFlags & 2u) != 0 && vehicle->inside != kNowhere && vehicle->target != kNoDriver)
        {
            const GuestPtr<MapPerson> driver = city->map->people + vehicle->target;
            if (Schedule_GetBuildingActivity(city, driver) != driver->activity)
                vehicle->walkTile = Map_GetRecord(city->map, vehicle->inside)->tile;
        }
        const int x = sdk::fptosi(ps2f(64.0f) + vehicle->x);
        const int z = sdk::fptosi(ps2f(64.0f) - vehicle->z);
        const int tile = x + (z << 7);
        const auto carryDriver = [&]() {
            const GuestPtr<MapPerson> driver = city->map->people + port::load<uint16_t>(v + 0x28u);
            port::store<uint32_t>(driver.address, port::load<uint32_t>(v));
            port::store<uint32_t>(driver.address + 4u, port::load<uint32_t>(v + 4u));
            port::store<uint32_t>(driver.address + 8u, port::load<uint32_t>(v + 8u));
        };
        if (vehicle->inside != kNowhere)
        {
            port::store<uint16_t>(v + 0x2Eu, 0);
            const GuestPtr<MapObject> building = Map_GetRecord(city->map, vehicle->inside);
            const GuestPtr<MapObject> here = Map_GetTileFirstObject(city->map, tile);
            if (building->tile == here->tile)
            {
                vehicle->inside = kNowhere;
                port::store<uint16_t>(v + 0x1Eu, kNowhere);
                vehicle->walkTile = kNoTile;
                vehicle->previousTile = kNoTile;
                port::store<uint16_t>(v + 0x2Cu, 0);
                const GuestPtr<MapPerson> driver = city->map->people + vehicle->target;
                driver->flags = static_cast<uint16_t>(driver->flags & 0xFFFBu);
                driver->inside = kNowhere;
                port::store<uint16_t>(driver.address + 0x1Eu, kNowhere);
                driver->walkTile = kNoTile;
                driver->previousTile = kNoTile;
                carryDriver();
                vehicle->target = kNoDriver;
            }
            else if (vehicle->walkTile == kNoTile || Map_GetTileFirstObject(city->map, vehicle->walkTile)->tile == here->tile)
                Path_NextWaypoint(cls, city, vehicle, tile);
            else
            {
                if ((vehicle->flags & 0x1000u) != 0)
                    Char2_MoveB(cls, city, vehicle, here, tile);
                else
                    Char2_MoveA(cls, city, vehicle, here, tile);
                carryDriver();
            }
        }
        else
        {
            const uint16_t counter = static_cast<uint16_t>(port::load<uint16_t>(v + 0x2Eu) + 3u);
            port::store<uint16_t>(v + 0x2Eu, counter);
            if (static_cast<int32_t>(port::load<uint16_t>(v + 0x2Eu)) >= 0xFD20)
            {
                Map_UnlinkPersonFromTile(city->map, tile, vehicle->index);
                port::store<uint16_t>(v + 0x2Eu, 0);
                const uint32_t home = Map_GetRecord(city->map, vehicle->work)->tile;
                vehicle->x = ps2f(0.5f) + (fromInt(static_cast<int32_t>(home & 0x7Fu)) - ps2f(64.0f));
                vehicle->z = (fromInt(0x80 - static_cast<int32_t>(home >> 7)) - ps2f(64.0f)) - ps2f(0.5f);
                vehicle->walkTile = kNoTile;
                vehicle->previousTile = kNoTile;
            }
        }
        if ((vehicle->flags & 0x800u) != 0)
        {
            const int cx = sdk::fptosi(ps2f(64.0f) + vehicle->x);
            const int cz = sdk::fptosi(ps2f(64.0f) - vehicle->z);
            const GuestPtr<MapObject> under = Map_GetTileObject(city->map, cx, cz);
            if (vehicle->inside != kNowhere)
            {
                const GuestPtr<MapObject> building = Map_GetRecord(city->map, vehicle->inside);
                MapObj_AddPerson(city->map, building->tile == under->tile ? building->id : under->id, vehicle->index);
            }
            else
                MapObj_AddPerson(city->map, under->id, vehicle->index);
        }
        vehicle->flags = static_cast<uint16_t>(vehicle->flags & 0xEFFFu);
    }

    PORT_FN(Char2_Update);

    // The player for one frame. The move speed (+0x54) follows the stamina object's counter (the count over 100000 over 140, plus 0.04;
    // 0.02 with none). The tile the player stands on (+0x10 and +0x18, 64 added, as x and as the row) is found, then the move vector
    // (sp+0x80, zero to start) is made by the mode (+0x0): 0 and 3 turn and show the placing highlight, 1 walks (Cross held lays a
    // road on the tile) and shows it, 2 turns and places a family, 4 only turns, 5 turns and places a bag item (3 and 0 also pick the
    // target). Moving, or rising, uses stamina. The vector is turned by the heading (the model's matrix, +0x88) and added to the
    // position; Triangle or the two shoulder buttons jump (the speed -0.25 when not already rising fast, two jumps at most); gravity
    // then speeds the fall (the time +0x50 over 60, 9.8 and 0.01) and moves the height (+0x14). The new spot (0.15 lower) is checked
    // against the nearest object (radius 0.05 over the speed, at least 0.2): in the walk mode a hit flips the grid step (+0x8) and mirrors
    // the step frames (2 over the speed minus them), in the other modes the player is pushed out along the unit direction to the hit; a
    // second hit puts x and z back. The height of the ground (at most 0.4 over +0xB0 of the city, or the colliding object's top) then lands
    // a fall (speed 0), settles a standing player (eased by a fifth when far above, snapped when close) or ends a jump; the model's matrix
    // follows, the building occupant of the tile is drawn, the action, footstep and zoom flag (+0x3C) are updated, the cursor is shown
    // in the modes 0 and 3, the minimap view is set and the pad focus 3 is let go of unless Cross is held.
    void Player_Update(GuestPtr<Player> player, GuestPtr<CityScene> city)
    {
        port::GuestFrame<0xE0> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t p = player.address, c = city.address;
        const auto loadF = [](uint32_t at) { return ps2f(std::bit_cast<float>(port::load<uint32_t>(at))); };
        const auto storeF = [](uint32_t at, ps2f value) { port::store<uint32_t>(at, std::bit_cast<uint32_t>(value.v)); };
        const auto constant = [](uint32_t bits) { return ps2f(std::bit_cast<float>(bits)); };
        const auto copyVec = [](uint32_t to, uint32_t from) {
            for (uint32_t k = 0; k < 16u; k += 4u)
                port::store<uint32_t>(to + k, port::load<uint32_t>(from + k));
        };
        const auto map = [c]() { return GuestPtr<CityMap>{port::load<uint32_t>(c + 0x50u)}; };
        const uint32_t move = sp + 0x80u, hit = sp + 0xA0u, spot = sp + 0xB0u, before = sp + 0xC0u, distance = sp + 0xDCu;
        copyVec(move, 0x001B26B0u);
        const uint32_t stamina = port::load<uint32_t>(c + 0x64u);
        const int32_t count = port::load<int32_t>(stamina + 4u);
        if (count == 0)
            port::store<uint32_t>(p + 0x54u, 0x3CA3D70Au);
        else
        {
            const ps2f base = ps2f(static_cast<float>(count / 100000));
            storeF(p + 0x54u, base);
            storeF(p + 0x54u, constant(0x3D23D70Au) + base / constant(0x430C0000u));
        }
        const int tileX = sdk::fptosi(constant(0x42800000u) + loadF(p + 0x10u));
        const int tileZ = sdk::fptosi(constant(0x42800000u) - loadF(p + 0x18u));
        const auto showHighlight = [&]() {
            Map_SetPlacementHighlight(map(), 0, GuestPtr<int32_t>{sp + 0x70u}, GuestPtr<uint32_t>{sp + 0x90u});
        };
        switch (port::load<uint32_t>(p))
        {
        case 0:
            Player_Turn(player, GuestPtr<ps2f>{move});
            showHighlight();
            Player_PickTarget(player, city);
            break;
        case 1:
            Player_Move(player, GuestPtr<Vec4>{move});
            if ((Pad_Held(0) & 0x40u) != 0u)
                Map_PlaceRoad(map(), static_cast<uint32_t>(tileX), static_cast<uint32_t>(tileZ), static_cast<int>(c));
            showHighlight();
            break;
        case 2:
            Player_Turn(player, GuestPtr<ps2f>{move});
            Player_PlaceForApplicant(player, city);
            break;
        case 3:
            Player_Turn(player, GuestPtr<ps2f>{move});
            showHighlight();
            Player_PickTarget(player, city);
            break;
        case 4:
            Player_Turn(player, GuestPtr<ps2f>{move});
            break;
        case 5:
            Player_Turn(player, GuestPtr<ps2f>{move});
            Player_PlaceBagItem(player, city);
            break;
        default:
            break;
        }
        if (!(ps2f(0.0f) == loadF(move)) || !(ps2f(0.0f) == loadF(move + 8u)) || loadF(p + 0x4Cu) < ps2f(0.0f))
            Stamina_Tick(GuestPtr<Stamina>{port::load<uint32_t>(c + 0x64u)}, city);
        copyVec(before, p + 0x10u);
        const uint32_t matrix = port::load<uint32_t>(p + 0x88u) + 0x10u;
        sdk::sceVu0RotMatrixY(matrix, 0x001B2550u, loadF(p + 0xCu));
        sdk::sceVu0ApplyMatrix(move, matrix, move);
        {
            using namespace vu0;
            vf(4) = load(GuestPtr<Vec4>{p + 0x10u});
            vf(5) = load(GuestPtr<Vec4>{move});
            set(vf(4), add(vf(4), vf(5)), XYZW);
            store(GuestPtr<Vec4>{p + 0x10u}, vf(4));
        }
        if ((Pad_Pressed(0) & 0x10u) != 0u || (Pad_Pressed(0) & 0x200u) != 0u || (Pad_Pressed(0) & 0x400u) != 0u)
        {
            const ps2f speed = loadF(p + 0x4Cu);
            if (!(speed < constant(0xBE99999Au)) && speed <= constant(0x3D8F5C29u) && port::load<int32_t>(p + 0x84u) < 2)
            {
                port::store<uint32_t>(p + 0x4Cu, 0xBE800000u);
                port::store<uint32_t>(p + 0x50u, 0u);
                const int32_t jumps = port::load<int32_t>(p + 0x84u);
                port::store<int32_t>(p + 0x84u, jumps + 1);
                Se_PlayVariant(6);
            }
        }
        if (!(ps2f(0.0f) == loadF(p + 0x4Cu)))
        {
            storeF(p + 0x50u, loadF(p + 0x50u) + constant(0x3C888889u));
            const ps2f pull = constant(0x3C23D70Au) * (constant(0x411CCCCDu) * loadF(p + 0x50u));
            const ps2f speed = loadF(p + 0x4Cu) + pull;
            storeF(p + 0x4Cu, speed);
            storeF(p + 0x14u, loadF(p + 0x14u) + speed);
        }
        copyVec(spot, p + 0x10u);
        storeF(spot + 4u, loadF(spot + 4u) - constant(0x3E19999Au));
        ps2f radius = constant(0x3D4CCCCDu) + loadF(p + 0x54u);
        if (radius < constant(0x3E4CCCCDu))
            radius = constant(0x3E4CCCCDu);
        if (Player_NearestObject(map(), GuestPtr<ps2f>{spot}, GuestPtr<uint32_t>{hit}, GuestPtr<ps2f>{distance}, radius) != 0)
        {
            if (port::load<int32_t>(p) == 1)
            {
                const int32_t step = port::load<int32_t>(p + 8u);
                if (step == 1 || step == 2)
                {
                    port::store<int32_t>(p + 8u, step == 1 ? 2 : 1);
                    storeF(p + 0x80u, ps2f(2.0f) / loadF(p + 0x54u) - loadF(p + 0x80u));
                }
            }
            else
            {
                using namespace vu0;
                vf(4) = load(GuestPtr<Vec4>{hit});
                vf(5) = load(GuestPtr<Vec4>{spot});
                set(vf(4), sub(vf(4), vf(5)), XYZW);
                store(GuestPtr<Vec4>{p + 0x60u}, vf(4));
                port::store<uint32_t>(p + 0x64u, 0u);
                vf(1) = load(GuestPtr<Vec4>{p + 0x60u});
                set(vf(2), mul(vf(1), vf(1)), XYZW);
                set(vf(2), add(vf(2), lane(vf(2), 1)), X);
                set(vf(2), add(vf(2), lane(vf(2), 2)), X);
                q() = rsqrt(x(vf(2)));
                set(vf(2), mul(vf(1), _mm_set1_ps(q())), XYZ);
                store(GuestPtr<Vec4>{p + 0x60u}, vf(2));
                const ps2f push = (constant(0x3A83126Fu) + radius) - loadF(distance);
                storeF(spot, loadF(spot) - loadF(p + 0x60u) * push);
                storeF(spot + 8u, loadF(spot + 8u) - loadF(p + 0x68u) * push);
            }
        }
        if (Player_NearestObject(map(), GuestPtr<ps2f>{spot}, GuestPtr<uint32_t>{hit}, GuestPtr<ps2f>{distance}, radius) != 0)
        {
            port::store<uint32_t>(spot, port::load<uint32_t>(before));
            port::store<uint32_t>(spot + 8u, port::load<uint32_t>(before + 8u));
        }
        port::store<uint32_t>(p + 0x10u, port::load<uint32_t>(spot));
        port::store<uint32_t>(p + 0x18u, port::load<uint32_t>(spot + 8u));
        ps2f ground = Map_SampleHeight(map(), loadF(p + 0x10u), loadF(p + 0x18u));
        const ps2f ceiling = constant(0x3ECCCCCDu) + loadF(c + 0xB0u);
        if (!(ground <= ceiling))
            ground = ceiling;
        if (Player_CollideObjects(map(), GuestPtr<ps2f>{p + 0x10u}, static_cast<int>(hit), constant(0x3F666666u)) != 0)
            ground = loadF(hit + 4u);
        const ps2f speed = loadF(p + 0x4Cu);
        if (!(speed <= ps2f(0.0f)))
        {
            // Falling: lands when the player is below the ground.
            if (ground < loadF(p + 0x14u))
            {
                storeF(p + 0x50u, ps2f(0.0f));
                storeF(p + 0x4Cu, ps2f(0.0f));
                storeF(p + 0x14u, ground);
                port::store<int32_t>(p + 0x84u, 0);
            }
        }
        else if (ps2f(0.0f) == speed)
        {
            const ps2f height = loadF(p + 0x14u);
            const ps2f above = height - ground;
            if (!(above <= constant(0x3DCCCCCDu)))
                storeF(p + 0x4Cu, (-above) / ps2f(5.0f));
            else if (ground < height)
            {
                storeF(p + 0x50u, ps2f(0.0f));
                storeF(p + 0x14u, ground);
            }
            else
            {
                const ps2f below = ground - height;
                if (!(below <= constant(0x3DCCCCCDu)))
                    port::store<uint32_t>(p + 0x4Cu, 0x3A03126Fu);
                else if (!(ground <= height))
                {
                    storeF(p + 0x50u, ps2f(0.0f));
                    storeF(p + 0x14u, ground);
                }
            }
        }
        storeF(matrix + 0x30u, loadF(p + 0x10u));
        storeF(matrix + 0x34u, loadF(p + 0x14u));
        storeF(matrix + 0x38u, loadF(p + 0x18u));
        const uint32_t tile = static_cast<uint32_t>(Map_GetTileObject(map(), tileX, tileZ).address);
        BldgType_DrawOccupant(GuestPtr<BuildingType>{port::load<uint32_t>(port::load<uint32_t>(0x001BDBF8u) + 4u * port::load<uint16_t>(tile))}, GuestPtr<Vec4>{p + 0x20u},
                              GuestPtr<MapPerson>{p + 0x10u}, city);
        Player_UpdateAction(player, GuestPtr<Vec4>{move}, city);
        Player_PlayFootstep(player, city);
        port::store<int32_t>(p + 0x3Cu, CityCamera_GetZoomLevel(GuestPtr<CityCamera>{port::load<uint32_t>(c + 0x4Cu)}, city) != 0 ? 1 : 0);
        const int32_t mode = port::load<int32_t>(p);
        port::store<int32_t>(port::load<uint32_t>(c + 0x70u) + 0x1A0u, (mode == 0 || mode == 3) ? 1 : 0);
        Minimap_SetView(GuestPtr<Minimap>{port::load<uint32_t>(c + 0x74u)}, GuestPtr<ps2f>{p + 0x10u}, loadF(p + 0xCu));
        if ((Pad_Held(3) & 0x40u) == 0u)
            Pad_ReleaseFocus(3);
    }

    PORT_FN(Player_Update);
}
