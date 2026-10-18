// Views with a free camera: the person views' constructors, and walking around inside a home (its constructor, the
// walls and floor it collides with, and the pad input).

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"
#include "vu0.h"

#include <bit>

namespace mm
{
    namespace
    {
        // A constant written as the bits the compiler loaded it with.
        constexpr ps2f F(uint32_t bits) { return ps2f(std::bit_cast<float>(bits)); }

        GuestPtr<MyHomeView> home(GuestPtr<TalkView> view) { return GuestPtr<MyHomeView>{view.address}; }

        // The interior's triangles are in the blob at +0x298 of the view: a table of (floor start, wall start) index
        // pairs per building size class (the byte at +0x150 of the building type), then the triangles as in the
        // exterior collision meshes. The index of the next pair ends a range.
        uint32_t tableEntry(GuestPtr<MyHomeView> view, GuestPtr<BuildingType> btype, uint32_t word)
        {
            return port::load<uint32_t>(view->collision.address + 8u * port::load<uint8_t>(btype.address + 0x150u) + 4u * word);
        }

        GuestPtr<BuildingType> homeType(GuestPtr<MyHomeView> view)
        {
            const GuestPtr<MapObject> record = Map_GetRecord(view->base.city->map, view->base.targetObject);
            return GuestPtr<BuildingType>{globals::buildingTypeAddress(record->type)};
        }
    }

    // The home's floor under the viewer: the segment from 0.51 above the position down to it against the interior's
    // floor triangles; of the crossings the lowest y wins and is stored in *hit. 1 when there was one.
    int MyHome_FloorHit(GuestPtr<TalkView> talkView, GuestPtr<Vec4> matrix, GuestPtr<uint32_t> hit)
    {
        port::GuestFrame<0xF0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<MyHomeView> view = home(talkView);
        const GuestPtr<BuildingType> btype = homeType(view);
        uint32_t index = tableEntry(view, btype, 0);
        uint32_t offset = index * 4u;
        *GuestPtr<Vec4>{sp + 0xD0u} = view->position;
        port::store<float>(sp + 0xD4u, static_cast<float>(ps2f(port::load<float>(sp + 0xD4u)) + F(0x3F028F5Cu)));
        ps2f nearest(100.0f);
        while (index < tableEntry(view, btype, 1))
        {
            const uint32_t triangle = view->collision.address + offset;
            vu0::applyMatrix(matrix, GuestPtr<Vec4>{triangle}, GuestPtr<Vec4>{sp + 0xA0u});
            vu0::applyMatrix(matrix, GuestPtr<Vec4>{triangle + 0x10u}, GuestPtr<Vec4>{sp + 0xB0u});
            vu0::applyMatrix(matrix, GuestPtr<Vec4>{triangle + 0x20u}, GuestPtr<Vec4>{sp + 0xC0u});
            if (Collide_RayTriangleB(GuestPtr<Vec4>{sp + 0xD0u}, GuestPtr<Vec4>{view.address + 0x270u}, GuestPtr<Vec4>{sp + 0xA0u},
                                     GuestPtr<Vec4>{sp + 0xB0u}, GuestPtr<Vec4>{sp + 0xC0u}, GuestPtr<Vec4>{sp + 0xE0u}) != 0)
            {
                const ps2f y(port::load<float>(sp + 0xE4u));
                if (!(nearest <= y))
                {
                    nearest = y;
                    *GuestPtr<Vec4>{hit.address} = *GuestPtr<Vec4>{sp + 0xE0u};
                }
            }
            offset += 0x30u;
            index += 12u;
        }
        return nearest == ps2f(100.0f) ? 0 : 1;
    }

    // The walls: the same triangles of the wall range against the ray from the viewer along each wall's normal
    // within 0.2; the nearest distance is left in *distance (100 when none) and its hit point in *hit.
    int MyHome_WallHit(GuestPtr<TalkView> talkView, GuestPtr<Vec4> matrix, GuestPtr<uint32_t> hit, GuestPtr<ps2f> distance)
    {
        port::GuestFrame<0xF0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<MyHomeView> view = home(talkView);
        const GuestPtr<BuildingType> btype = homeType(view);
        port::store<float>(distance.address, 100.0f);
        uint32_t index = tableEntry(view, btype, 1);
        uint32_t offset = index * 4u;
        while (index < tableEntry(view, btype, 2))
        {
            const uint32_t triangle = view->collision.address + offset;
            vu0::applyMatrix(matrix, GuestPtr<Vec4>{triangle}, GuestPtr<Vec4>{sp + 0xA0u});
            vu0::applyMatrix(matrix, GuestPtr<Vec4>{triangle + 0x10u}, GuestPtr<Vec4>{sp + 0xB0u});
            vu0::applyMatrix(matrix, GuestPtr<Vec4>{triangle + 0x20u}, GuestPtr<Vec4>{sp + 0xC0u});
            if (Collide_RayTriangleA(GuestPtr<Vec4>{view.address + 0x270u}, GuestPtr<Vec4>{sp + 0xA0u}, GuestPtr<Vec4>{sp + 0xB0u},
                                     GuestPtr<Vec4>{sp + 0xC0u}, GuestPtr<Vec4>{sp + 0xD0u}, GuestPtr<ps2f>{sp + 0xECu}, F(0x3E4CCCCDu)) != 0)
            {
                const ps2f found(port::load<float>(sp + 0xECu));
                if (!(ps2f(port::load<float>(distance.address)) <= found))
                {
                    port::store<float>(distance.address, static_cast<float>(found));
                    *GuestPtr<Vec4>{hit.address} = *GuestPtr<Vec4>{sp + 0xD0u};
                }
            }
            offset += 0x30u;
            index += 12u;
        }
        return ps2f(port::load<float>(distance.address)) == ps2f(100.0f) ? 0 : 1;
    }

    // Keeps the viewer out of the walls (two passes: each wall found pushes the viewer back along its direction by what
    // is missing from 0.2) and on the floor (0.5 over a floor hit, else sinking by 0.05, never above -0.5).
    void MyHome_Collide(GuestPtr<TalkView> talkView)
    {
        using namespace vu0;
        port::GuestFrame<0xA0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<MyHomeView> view = home(talkView);
        MapObj_GetMatrix(view->base.city->map, view->base.targetObject, GuestPtr<Vec4>{sp + 0x30u});
        for (int pass = 0; pass < 2; ++pass)
        {
            if (MyHome_WallHit(talkView, GuestPtr<Vec4>{sp + 0x30u}, GuestPtr<uint32_t>{sp + 0x70u}, GuestPtr<ps2f>{sp + 0x9Cu}) == 0)
                continue;
            vf(4) = load(GuestPtr<Vec4>{sp + 0x70u});
            vf(5) = load(GuestPtr<Vec4>{view.address + 0x270u});
            set(vf(4), sub(vf(4), vf(5)), XYZW);
            store(GuestPtr<Vec4>{sp + 0x80u}, vf(4));
            port::store<uint32_t>(sp + 0x84u, 0);
            vf(1) = load(GuestPtr<Vec4>{sp + 0x80u});
            set(vf(2), mul(vf(1), vf(1)), XYZW);
            set(vf(2), add(vf(2), lane(vf(2), 1)), X);
            set(vf(2), add(vf(2), lane(vf(2), 2)), X);
            q() = rsqrt(x(vf(2)));
            set(vf(2), mul(vf(1), _mm_set1_ps(q())), XYZ);
            store(GuestPtr<Vec4>{sp + 0x80u}, vf(2));
            if (ps2f(port::load<float>(sp + 0x9Cu)) == ps2f(0.0f))
            {
                port::store<uint32_t>(sp + 0x80u, 0x3F000000u);
                port::store<uint32_t>(sp + 0x88u, 0x3F000000u);
                port::store<uint32_t>(sp + 0x9Cu, 0x3C23D70Au);
            }
            const ps2f push = F(0x3E4DD2F2u);
            view->position.x = view->position.x - (push - ps2f(port::load<float>(sp + 0x9Cu))) * ps2f(port::load<float>(sp + 0x80u));
            view->position.z = view->position.z - (push - ps2f(port::load<float>(sp + 0x9Cu))) * ps2f(port::load<float>(sp + 0x88u));
        }
        if (MyHome_FloorHit(talkView, GuestPtr<Vec4>{sp + 0x30u}, GuestPtr<uint32_t>{sp + 0x70u}) != 0)
            view->position.y = ps2f(port::load<float>(sp + 0x74u)) - ps2f(0.5f);
        else
            view->position.y = view->position.y + F(0x3D4CCCCDu);
        if (!(view->position.y <= F(0xBF000000u)))
            view->position.y = F(0xBF000000u);
    }

    // Walking with the pad: Triangle forwards and Cross backwards (0.04 a frame along z), Circle and Square turn by
    // 0.02 (wrapping at plus and minus pi), the two bumpers tilt the view (limits -0.05 and 0.1), the other pad bits
    // step sideways by 0.04. The move is turned by the facing and added to the position, the walls and floor push
    // back, the position may not rise above the floor 0.5 below the building, and the camera looks from the position
    // at the point 0.1 ahead. A home that is not walkable (flag 0x20 of its object clear) just keeps its camera.
    void MyHome_Active(GuestPtr<TalkView> talkView)
    {
        using namespace vu0;
        port::GuestFrame<0x90> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<MyHomeView> view = home(talkView);
        const GuestPtr<CityScene> city = view->base.city;
        if ((Map_GetRecord(city->map, view->base.targetObject)->flags & 0x20u) == 0)
        {
            TalkView_BuildingCamera(talkView, view->base.targetObject, GuestPtr<Vec4>{sp + 0x20u}, GuestPtr<Vec4>{sp + 0x30u});
            Camera_SetEyeTarget(GuestPtr<Camera>{view->base.city->cityCamera.address}, GuestPtr<Vec4>{sp + 0x20u}, GuestPtr<Vec4>{sp + 0x30u});
            Camera_UpdateMatrices(GuestPtr<Camera>{view->base.city->cityCamera.address});
            return;
        }
        const GuestPtr<Vec4> move{sp + 0x40u};
        *move = *GuestPtr<Vec4>{0x001B26B0u};
        if ((Pad_Held(0) & 0x1000u) != 0)
            port::store<uint32_t>(sp + 0x48u, 0xBD23D70Au);
        if ((Pad_Held(0) & 0x4000u) != 0)
            port::store<uint32_t>(sp + 0x48u, 0x3D23D70Au);
        if ((Pad_Held(0) & 0x2000u) != 0)
        {
            const ps2f turned = view->facing + F(0x3CA3D70Au);
            view->facing = turned;
            if (!(turned < F(0x40490FDBu)))
                view->facing = F(0xC0490FDBu);
        }
        if ((Pad_Held(0) & 0x8000u) != 0)
        {
            const ps2f turned = view->facing - F(0x3CA3D70Au);
            view->facing = turned;
            if (turned < F(0xC0490FDBu))
                view->facing = F(0x40490FDBu);
        }
        if ((Pad_Held(0) & 0x100000u) != 0)
            view->pitch = view->pitch - F(0x3A83126Fu);
        if ((Pad_Held(0) & 0x400000u) != 0)
            view->pitch = view->pitch + F(0x3A83126Fu);
        if ((Pad_Held(0) & 0x800000u) != 0)
            port::store<uint32_t>(sp + 0x40u, 0x3D23D70Au);
        if ((Pad_Held(0) & 0x200000u) != 0)
            port::store<uint32_t>(sp + 0x40u, 0xBD23D70Au);
        ps2f pitch = view->pitch;
        if (pitch < F(0xBD4CCCCDu))
            pitch = F(0xBD4CCCCDu);
        else if (!(pitch <= F(0x3DCCCCCDu)))
            pitch = F(0x3DCCCCCDu);
        view->pitch = pitch;
        sdk::sceVu0RotMatrixY(sp + 0x50u, globals::kIdentityMatrix, view->facing);
        applyMatrix(GuestPtr<Vec4>{sp + 0x50u}, move, move);
        vf(4) = load(GuestPtr<Vec4>{view.address + 0x270u});
        vf(5) = load(move);
        set(vf(4), add(vf(4), vf(5)), XYZW);
        store(GuestPtr<Vec4>{view.address + 0x270u}, vf(4));
        MyHome_Collide(talkView);
        const ps2f floor = MapObj_GetHeight(city->map, static_cast<uint32_t>(view->base.targetObject)) - ps2f(0.5f);
        if (!(view->position.y <= floor))
            view->position.y = floor;
        port::store<uint32_t>(sp + 0x40u, 0);
        port::store<uint32_t>(sp + 0x44u, 0);
        port::store<uint32_t>(sp + 0x48u, 0x3DCCCCCDu);
        port::store<uint32_t>(sp + 0x4Cu, 0);
        applyMatrix(GuestPtr<Vec4>{sp + 0x50u}, move, move);
        vf(4) = load(GuestPtr<Vec4>{view.address + 0x270u});
        vf(5) = load(move);
        set(vf(4), sub(vf(4), vf(5)), XYZW);
        store(GuestPtr<Vec4>{view.address + 0x280u}, vf(4));
        view->target.y = view->target.y + view->pitch;
        Camera_SetEyeTarget(GuestPtr<Camera>{city->cityCamera.address}, GuestPtr<Vec4>{view.address + 0x270u}, GuestPtr<Vec4>{view.address + 0x280u});
        Camera_UpdateMatrices(GuestPtr<Camera>{city->cityCamera.address});
        port::store<int32_t>(city.address + 0x28u, 0);
    }

    // The view of a home's inside: the walls' blob, the object, the home's size; the viewer starts at the entrance
    // (x from the tile and half the size, y 0.5 under the object's height, z by the mirrored tile row minus half the
    // size), faces by the tile's kind and steps in by size / 2 - 0.1 along that facing.
    GuestPtr<TalkView> MyHome_ctor(GuestPtr<TalkView> talkView, GuestPtr<CityScene> city, int object)
    {
        using namespace vu0;
        port::GuestFrame<0xA0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<MyHomeView> view = home(talkView);
        TalkView_ctor(talkView, city);
        view->base.base.vtable = GuestPtr<void>{0x001BD810u};
        view->collision = GuestPtr<void>{port::load<uint32_t>(0x001F3CE0u)};
        view->base.targetObject = object;
        const GuestPtr<MapObject> record = Map_GetRecord(view->base.city->map, view->base.targetObject);
        const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(record->type)};
        view->size = btype->size;
        const ps2f footprint = fromInt(btype->size);
        const ps2f half = footprint / F(0x40000000u);
        view->target.x = (fromInt(record->tile & 0x7F) - F(0x42800000u)) + half;
        view->target.y = MapObj_GetHeight(view->base.city->map, static_cast<uint32_t>(object)) - F(0x3F000000u);
        view->target.z = (fromInt(0x80 - (record->tile >> 7)) - F(0x42800000u)) - half;
        port::store<uint32_t>(view.address + 0x28Cu, 0x3F800000u);
        *GuestPtr<Vec4>{view.address + 0x270u} = *GuestPtr<Vec4>{view.address + 0x280u};
        const int kind = Map_GetTileKind(view->base.city->map, record->tile);
        port::store<uint32_t>(view.address + 0x290u, port::load<uint32_t>(0x001B2690u + 4u * static_cast<uint32_t>(kind)));
        sdk::sceVu0RotMatrixY(sp + 0x60u, globals::kIdentityMatrix, view->facing);
        port::store<uint32_t>(sp + 0x50u, 0);
        port::store<uint32_t>(sp + 0x54u, 0);
        port::store<float>(sp + 0x58u, static_cast<float>(F(0x3F000000u) * footprint - F(0x3DCCCCCDu)));
        port::store<uint32_t>(sp + 0x5Cu, 0);
        applyMatrix(GuestPtr<Vec4>{sp + 0x60u}, GuestPtr<Vec4>{sp + 0x50u}, GuestPtr<Vec4>{sp + 0x50u});
        vf(4) = load(GuestPtr<Vec4>{view.address + 0x270u});
        vf(5) = load(GuestPtr<Vec4>{sp + 0x50u});
        set(vf(4), add(vf(4), vf(5)), XYZW);
        store(GuestPtr<Vec4>{view.address + 0x270u}, vf(4));
        view->pitch = F(0);
        return talkView;
    }

    // The near and far views of a person share everything but the vtable: the person's height is set from the terrain (it is
    // written back into the person), the target is the person 0.9 under its position, the eye starts 0.55 under it
    // and moves 1.3 along the flat view direction (the view's own eye to target, normalised), and may not rise above
    // the terrain minus 0.5; the facing is that direction's angle.
    namespace
    {
        GuestPtr<TalkView> personViewCtor(GuestPtr<TalkView> talkView, GuestPtr<CityScene> city, int person, uint32_t vtable)
        {
            using namespace vu0;
            port::GuestFrame<0xA0> frame;
            const uint32_t sp = frame.ptr().address;
            const GuestPtr<TalkPersonView> view{talkView.address};
            TalkView_ctor(talkView, city);
            view->base.base.vtable = GuestPtr<void>{vtable};
            view->base.targetObject = person;
            const GuestPtr<MapPerson> who = city->map->people + view->base.targetObject;
            who->y = Map_SampleHeight(city->map, who->x, who->z);
            view->target.x = who->x;
            view->target.y = who->y - F(0x3EE66666u);
            view->target.z = who->z;
            port::store<uint32_t>(view.address + 0x28Cu, 0x3F800000u);
            view->eye.x = who->x;
            view->eye.y = who->y - F(0x3F0CCCCDu);
            view->eye.z = who->z;
            port::store<uint32_t>(view.address + 0x27Cu, 0x3F800000u);
            port::store<uint32_t>(sp + 0x50u, 0);
            port::store<uint32_t>(sp + 0x54u, 0);
            port::store<uint32_t>(sp + 0x58u, 0x3FA66666u);
            port::store<uint32_t>(sp + 0x5Cu, 0);
            vf(4) = load(GuestPtr<Vec4>{view.address + 0x20u});
            vf(5) = load(GuestPtr<Vec4>{view.address + 0x10u});
            set(vf(4), sub(vf(4), vf(5)), XYZW);
            store(GuestPtr<Vec4>{sp + 0x40u}, vf(4));
            port::store<uint32_t>(sp + 0x44u, 0);
            vf(1) = load(GuestPtr<Vec4>{sp + 0x40u});
            set(vf(2), mul(vf(1), vf(1)), XYZW);
            set(vf(2), add(vf(2), lane(vf(2), 1)), X);
            set(vf(2), add(vf(2), lane(vf(2), 2)), X);
            q() = rsqrt(x(vf(2)));
            set(vf(2), mul(vf(1), _mm_set1_ps(q())), XYZ);
            store(GuestPtr<Vec4>{sp + 0x40u}, vf(2));
            Mat4_Copy(GuestPtr<Vec4>{sp + 0x60u}, GuestPtr<Vec4>{globals::kIdentityMatrix});
            const ps2f directionZ(port::load<float>(sp + 0x48u)), directionX(port::load<float>(sp + 0x40u));
            port::store<float>(sp + 0x60u, static_cast<float>(-directionZ));
            port::store<float>(sp + 0x68u, static_cast<float>(directionX));
            port::store<float>(sp + 0x80u, static_cast<float>(-directionX));
            port::store<float>(sp + 0x88u, static_cast<float>(-directionZ));
            applyMatrix(GuestPtr<Vec4>{sp + 0x60u}, GuestPtr<Vec4>{sp + 0x50u}, GuestPtr<Vec4>{sp + 0x50u});
            vf(4) = load(GuestPtr<Vec4>{view.address + 0x270u});
            vf(5) = load(GuestPtr<Vec4>{sp + 0x50u});
            set(vf(4), add(vf(4), vf(5)), XYZW);
            store(GuestPtr<Vec4>{view.address + 0x270u}, vf(4));
            const ps2f floor = Map_SampleHeight(city->map, view->eye.x, view->eye.z) - ps2f(0.5f);
            if (!(view->eye.y <= floor))
                view->eye.y = floor;
            view->facing = sdk::atan2f(ps2f(port::load<float>(sp + 0x40u)), ps2f(port::load<float>(sp + 0x48u)));
            return talkView;
        }
    }

    GuestPtr<TalkView> TalkPersonFar_ctor(GuestPtr<TalkView> view, GuestPtr<CityScene> city, int person)
    {
        return personViewCtor(view, city, person, 0x001BD680u);
    }

    GuestPtr<TalkView> TalkPersonNear_ctor(GuestPtr<TalkView> view, GuestPtr<CityScene> city, int person)
    {
        return personViewCtor(view, city, person, 0x001BD6D0u);
    }

    PORT_FN(TalkPersonFar_ctor);
    PORT_FN(TalkPersonNear_ctor);
    PORT_FN(MyHome_FloorHit);
    PORT_FN(MyHome_WallHit);
    PORT_FN(MyHome_Collide);
    PORT_FN(MyHome_Active);
    PORT_FN(MyHome_ctor);
}
