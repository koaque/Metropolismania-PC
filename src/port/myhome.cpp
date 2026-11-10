// Views with a free camera: the person views' constructors, and walking around inside a home (its constructor, the
// walls and floor it collides with, and the pad input).

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"
#include "vu0.h"

#include <bit>
#include <cstddef>

namespace mm
{
    namespace
    {
        // A constant written as the bits the compiler loaded it with.
        constexpr ps2f F(uint32_t bits) { return ps2f(std::bit_cast<float>(bits)); }

        GuestPtr<MyHomeView> home(GuestPtr<TalkView> view) { return GuestPtr<MyHomeView>{view.address}; }

        // The interior's triangles are in the view's collision blob: a table of (floor start, wall start) index
        // pairs per building type's modelNode byte, then the triangles as in the
        // exterior collision meshes. The index of the next pair ends a range.
        uint32_t tableEntry(GuestPtr<MyHomeView> view, GuestPtr<BuildingType> btype, uint32_t word)
        {
            return port::load<uint32_t>(view->collision.address + 8u * btype->modelNode + 4u * word);
        }

        GuestPtr<BuildingType> homeType(GuestPtr<MyHomeView> view)
        {
            const GuestPtr<MapObject> record = Map_GetRecord(view->base.city->map, view->base.targetObject);
            return GuestPtr<BuildingType>{globals::buildingTypeAddress(record->type)};
        }

        // MyHome_FloorHit's frame.
        struct FloorHitFrame
        {
            uint8_t gap_00[0xA0];
            Vec4 corners[3];           // +0xA0 the triangle, moved by the matrix
            Vec4 below;                // +0xD0 the position 0.51 lower, where the segment starts
            Vec4 crossing;             // +0xE0 where it crosses the triangle
        };
        static_assert(offsetof(FloorHitFrame, corners) == 0xA0 && offsetof(FloorHitFrame, below) == 0xD0 &&
                      offsetof(FloorHitFrame, crossing) == 0xE0 && sizeof(FloorHitFrame) == 0xF0);

        // MyHome_WallHit's frame.
        struct WallHitFrame
        {
            uint8_t gap_00[0xA0];
            Vec4 corners[3];           // +0xA0 the triangle, moved by the matrix
            Vec4 hit;                  // +0xD0 where the ray meets it
            uint8_t gap_E0[0xC];
            ps2f distance;             // +0xEC how far along the ray
        };
        static_assert(offsetof(WallHitFrame, corners) == 0xA0 && offsetof(WallHitFrame, hit) == 0xD0 &&
                      offsetof(WallHitFrame, distance) == 0xEC && sizeof(WallHitFrame) == 0xF0);

        // MyHome_Collide's frame.
        struct CollideFrame
        {
            uint8_t gap_00[0x30];
            Vec4 matrix[4];            // +0x30 the home object's matrix
            Vec4 hit;                  // +0x70 the wall's or the floor's hit point
            Vec4 toWall;               // +0x80 from the viewer to the wall's hit, flat and normalised
            uint8_t gap_90[0xC];
            ps2f distance;             // +0x9C the wall's distance
        };
        static_assert(offsetof(CollideFrame, matrix) == 0x30 && offsetof(CollideFrame, hit) == 0x70 &&
                      offsetof(CollideFrame, toWall) == 0x80 && offsetof(CollideFrame, distance) == 0x9C &&
                      sizeof(CollideFrame) == 0xA0);

        // MyHome_Active's frame.
        struct ActiveFrame
        {
            uint8_t gap_00[0x20];
            Vec4 eye;                  // +0x20 the building camera's, for a home that is not walkable
            Vec4 target;               // +0x30
            Vec4 move;                 // +0x40 this frame's step, then the look ahead
            Vec4 turn[4];              // +0x50 the turn by the facing
        };
        static_assert(offsetof(ActiveFrame, eye) == 0x20 && offsetof(ActiveFrame, move) == 0x40 &&
                      offsetof(ActiveFrame, turn) == 0x50 && sizeof(ActiveFrame) == 0x90);

        // MyHome_ctor's frame.
        struct HomeCtorFrame
        {
            uint8_t gap_00[0x50];
            Vec4 step;                 // +0x50 the step in from the entrance
            Vec4 turn[4];              // +0x60 the turn by the facing
        };
        static_assert(offsetof(HomeCtorFrame, step) == 0x50 && offsetof(HomeCtorFrame, turn) == 0x60 &&
                      sizeof(HomeCtorFrame) == 0xA0);

        // The person views' constructors' frame.
        struct PersonViewFrame
        {
            uint8_t gap_00[0x40];
            Vec4 direction;            // +0x40 the view's own eye to target, flat and normalised
            Vec4 step;                 // +0x50 the eye's move back, 1.3 along z before the turn
            Vec4 turn[4];              // +0x60 the turn to that direction
        };
        static_assert(offsetof(PersonViewFrame, direction) == 0x40 && offsetof(PersonViewFrame, step) == 0x50 &&
                      offsetof(PersonViewFrame, turn) == 0x60 && sizeof(PersonViewFrame) == 0xA0);
    }

    // The home's floor under the viewer: the segment from 0.51 below the position up to it (heights grow downwards)
    // against the interior's floor triangles; of the crossings the highest (smallest y) wins and is stored in *hit. 1
    // when there was one.
    int MyHome_FloorHit(GuestPtr<TalkView> talkView, GuestPtr<Vec4> matrix, GuestPtr<Vec4> hit)
    {
        port::GuestFrameOf<FloorHitFrame> frame;
        const GuestPtr<Vec4> corners = frame.at(&FloorHitFrame::corners);
        const GuestPtr<MyHomeView> view = home(talkView);
        const GuestPtr<BuildingType> btype = homeType(view);
        uint32_t index = tableEntry(view, btype, 0);
        uint32_t offset = index * 4u;
        frame->below = view->position;
        frame->below.y = frame->below.y + F(0x3F028F5Cu);
        ps2f nearest(100.0f);
        while (index < tableEntry(view, btype, 1))
        {
            const uint32_t triangle = view->collision.address + offset;
            vu0::applyMatrix(matrix, GuestPtr<Vec4>{triangle}, corners);
            vu0::applyMatrix(matrix, GuestPtr<Vec4>{triangle + 0x10u}, corners + 1);
            vu0::applyMatrix(matrix, GuestPtr<Vec4>{triangle + 0x20u}, corners + 2);
            if (Collide_SegmentTriangle(frame.at(&FloorHitFrame::below), view.field(&MyHomeView::position), corners,
                                        corners + 1, corners + 2, frame.at(&FloorHitFrame::crossing)) != 0)
            {
                const ps2f y = frame->crossing.y;
                if (!(nearest <= y))
                {
                    nearest = y;
                    *hit = frame->crossing;
                }
            }
            offset += 0x30u;
            index += 12u;
        }
        return nearest == ps2f(100.0f) ? 0 : 1;
    }

    // The walls: the same triangles of the wall range against the ray from the viewer along each wall's normal
    // within 0.2; the nearest distance is left in *distance (100 when none) and its hit point in *hit.
    int MyHome_WallHit(GuestPtr<TalkView> talkView, GuestPtr<Vec4> matrix, GuestPtr<Vec4> hit, GuestPtr<ps2f> distance)
    {
        port::GuestFrameOf<WallHitFrame> frame;
        const GuestPtr<Vec4> corners = frame.at(&WallHitFrame::corners);
        const GuestPtr<MyHomeView> view = home(talkView);
        const GuestPtr<BuildingType> btype = homeType(view);
        *distance = 100.0f;
        uint32_t index = tableEntry(view, btype, 1);
        uint32_t offset = index * 4u;
        while (index < tableEntry(view, btype, 2))
        {
            const uint32_t triangle = view->collision.address + offset;
            vu0::applyMatrix(matrix, GuestPtr<Vec4>{triangle}, corners);
            vu0::applyMatrix(matrix, GuestPtr<Vec4>{triangle + 0x10u}, corners + 1);
            vu0::applyMatrix(matrix, GuestPtr<Vec4>{triangle + 0x20u}, corners + 2);
            if (Collide_NormalRayTriangle(view.field(&MyHomeView::position), corners, corners + 1, corners + 2,
                                          frame.at(&WallHitFrame::hit), frame.at(&WallHitFrame::distance),
                                          F(0x3E4CCCCDu)) != 0)
            {
                const ps2f found = frame->distance;
                if (!(*distance <= found))
                {
                    *distance = found;
                    *hit = frame->hit;
                }
            }
            offset += 0x30u;
            index += 12u;
        }
        return *distance == ps2f(100.0f) ? 0 : 1;
    }

    // Keeps the viewer out of the walls (two passes: each wall found pushes the viewer back along its direction by what
    // is missing from 0.2) and on the floor (0.5 over a floor hit, else sinking by 0.05, never above -0.5).
    void MyHome_Collide(GuestPtr<TalkView> talkView)
    {
        using namespace vu0;
        port::GuestFrameOf<CollideFrame> frame;
        const GuestPtr<Vec4> matrix = frame.at(&CollideFrame::matrix), hit = frame.at(&CollideFrame::hit);
        const GuestPtr<Vec4> toWall = frame.at(&CollideFrame::toWall);
        const GuestPtr<MyHomeView> view = home(talkView);
        MapObj_GetMatrix(view->base.city->map, view->base.targetObject, matrix);
        for (int pass = 0; pass < 2; ++pass)
        {
            if (MyHome_WallHit(talkView, matrix, hit, frame.at(&CollideFrame::distance)) == 0)
                continue;
            vf(4) = load(hit);
            vf(5) = load(view.field(&MyHomeView::position));
            set(vf(4), sub(vf(4), vf(5)), XYZW);
            store(toWall, vf(4));
            frame->toWall.y = F(0);
            vf(1) = load(toWall);
            set(vf(2), mul(vf(1), vf(1)), XYZW);
            set(vf(2), add(vf(2), lane(vf(2), 1)), X);
            set(vf(2), add(vf(2), lane(vf(2), 2)), X);
            q() = rsqrt(x(vf(2)));
            set(vf(2), mul(vf(1), _mm_set1_ps(q())), XYZ);
            store(toWall, vf(2));
            if (frame->distance == ps2f(0.0f))
            {
                frame->toWall.x = F(0x3F000000u);
                frame->toWall.z = F(0x3F000000u);
                frame->distance = F(0x3C23D70Au);
            }
            const ps2f push = F(0x3E4DD2F2u);
            view->position.x = view->position.x - (push - frame->distance) * frame->toWall.x;
            view->position.z = view->position.z - (push - frame->distance) * frame->toWall.z;
        }
        if (MyHome_FloorHit(talkView, matrix, hit) != 0)
            view->position.y = frame->hit.y - ps2f(0.5f);
        else
            view->position.y = view->position.y + F(0x3D4CCCCDu);
        if (!(view->position.y <= F(0xBF000000u)))
            view->position.y = F(0xBF000000u);
    }

    // Walking with the pad: Triangle forwards and Cross backwards (0.04 a frame along z), Circle and Square turn by
    // 0.02 (wrapping at plus and minus pi), the two bumpers tilt the view (limits -0.05 and 0.1), the other pad bits
    // step sideways by 0.04. The move is turned by the facing and added to the position, the walls and floor push
    // back, the position stays at least 0.5 above the building's base, and the camera looks from the position
    // at the point 0.1 ahead. A home that is not walkable (flag 0x20 of its object clear) just keeps its camera.
    void MyHome_Active(GuestPtr<TalkView> talkView)
    {
        using namespace vu0;
        port::GuestFrameOf<ActiveFrame> frame;
        const GuestPtr<MyHomeView> view = home(talkView);
        const GuestPtr<CityScene> city = view->base.city;
        if ((Map_GetRecord(city->map, view->base.targetObject)->flags & 0x20u) == 0)
        {
            const GuestPtr<Vec4> eye = frame.at(&ActiveFrame::eye), target = frame.at(&ActiveFrame::target);
            TalkView_BuildingCamera(talkView, view->base.targetObject, eye, target);
            Camera_SetEyeTarget(GuestPtr<Camera>{view->base.city->cityCamera.address}, eye, target);
            Camera_UpdateMatrices(GuestPtr<Camera>{view->base.city->cityCamera.address});
            return;
        }
        const GuestPtr<Vec4> move = frame.at(&ActiveFrame::move), turn = frame.at(&ActiveFrame::turn);
        *move = *GuestPtr<Vec4>{globals::kZeroVector};
        if ((Pad_Held(0) & 0x1000u) != 0)
            frame->move.z = F(0xBD23D70Au);
        if ((Pad_Held(0) & 0x4000u) != 0)
            frame->move.z = F(0x3D23D70Au);
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
            frame->move.x = F(0x3D23D70Au);
        if ((Pad_Held(0) & 0x200000u) != 0)
            frame->move.x = F(0xBD23D70Au);
        ps2f pitch = view->pitch;
        if (pitch < F(0xBD4CCCCDu))
            pitch = F(0xBD4CCCCDu);
        else if (!(pitch <= F(0x3DCCCCCDu)))
            pitch = F(0x3DCCCCCDu);
        view->pitch = pitch;
        sdk::sceVu0RotMatrixY(turn.address, globals::kIdentityMatrix, view->facing);
        applyMatrix(turn, move, move);
        vf(4) = load(view.field(&MyHomeView::position));
        vf(5) = load(move);
        set(vf(4), add(vf(4), vf(5)), XYZW);
        store(view.field(&MyHomeView::position), vf(4));
        MyHome_Collide(talkView);
        const ps2f floor = MapObj_GetHeight(city->map, static_cast<uint32_t>(view->base.targetObject)) - ps2f(0.5f);
        if (!(view->position.y <= floor))
            view->position.y = floor;
        frame->move = Vec4{F(0), F(0), F(0x3DCCCCCDu), F(0)};
        applyMatrix(turn, move, move);
        vf(4) = load(view.field(&MyHomeView::position));
        vf(5) = load(move);
        set(vf(4), sub(vf(4), vf(5)), XYZW);
        store(view.field(&MyHomeView::target), vf(4));
        view->target.y = view->target.y + view->pitch;
        Camera_SetEyeTarget(GuestPtr<Camera>{city->cityCamera.address}, view.field(&MyHomeView::position), view.field(&MyHomeView::target));
        Camera_UpdateMatrices(GuestPtr<Camera>{city->cityCamera.address});
        city->talkOverlay = 0;
    }

    // The view of a home's inside: the walls' blob, the object, the home's size; the viewer starts at the entrance
    // (x from the tile and half the size, y 0.5 under the object's height, z by the mirrored tile row minus half the
    // size), faces by the tile's facing and steps in by size / 2 - 0.1 along that facing.
    GuestPtr<TalkView> MyHome_ctor(GuestPtr<TalkView> talkView, GuestPtr<CityScene> city, int object)
    {
        using namespace vu0;
        port::GuestFrameOf<HomeCtorFrame> frame;
        const GuestPtr<Vec4> step = frame.at(&HomeCtorFrame::step), turn = frame.at(&HomeCtorFrame::turn);
        const GuestPtr<MyHomeView> view = home(talkView);
        TalkView_ctor(talkView, city);
        view->base.base.vtable = globals::kMyHomeVtable;
        view->collision = GuestPtr<void>{port::load<uint32_t>(globals::kMyHomeBlocks)};
        view->base.targetObject = object;
        const GuestPtr<MapObject> record = Map_GetRecord(view->base.city->map, view->base.targetObject);
        const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(record->type)};
        view->size = btype->size;
        const ps2f footprint = fromInt(btype->size);
        const ps2f half = footprint / F(0x40000000u);
        view->target.x = (fromInt(record->tile & 0x7F) - F(0x42800000u)) + half;
        view->target.y = MapObj_GetHeight(view->base.city->map, static_cast<uint32_t>(object)) - F(0x3F000000u);
        view->target.z = (fromInt(0x80 - (record->tile >> 7)) - F(0x42800000u)) - half;
        view->target.w = std::bit_cast<float>(0x3F800000u);
        view->position = view->target;
        const int facing = Map_GetTileFacing(view->base.city->map, record->tile);
        port::store<uint32_t>(view.field(&MyHomeView::facing).address,
                              port::load<uint32_t>(globals::kFacingHeadings + 4u * static_cast<uint32_t>(facing)));
        sdk::sceVu0RotMatrixY(turn.address, globals::kIdentityMatrix, view->facing);
        frame->step.x = F(0);
        frame->step.y = F(0);
        frame->step.z = F(0x3F000000u) * footprint - F(0x3DCCCCCDu);
        frame->step.w = F(0);
        applyMatrix(turn, step, step);
        vf(4) = load(view.field(&MyHomeView::position));
        vf(5) = load(step);
        set(vf(4), add(vf(4), vf(5)), XYZW);
        store(view.field(&MyHomeView::position), vf(4));
        view->pitch = F(0);
        return talkView;
    }

    // The near and far views of a person share everything but the vtable: the person's height is set from the terrain
    // (it is written back into the person), the target is 0.45 above the person's position, the eye starts 0.55 above
    // it and moves 1.3 along the flat view direction (the view's own eye to target, normalised), and stays at least 0.5
    // above the terrain (heights grow downwards); the facing is that direction's angle.
    namespace
    {
        GuestPtr<TalkView> personViewCtor(GuestPtr<TalkView> talkView, GuestPtr<CityScene> city, int person,
                                          GuestPtr<WidgetVtable> vtable)
        {
            using namespace vu0;
            port::GuestFrameOf<PersonViewFrame> frame;
            const GuestPtr<Vec4> direction = frame.at(&PersonViewFrame::direction);
            const GuestPtr<Vec4> step = frame.at(&PersonViewFrame::step), turn = frame.at(&PersonViewFrame::turn);
            const GuestPtr<TalkPersonView> view{talkView.address};
            TalkView_ctor(talkView, city);
            view->base.base.vtable = vtable;
            view->base.targetObject = person;
            const GuestPtr<MapPerson> who = city->map->people + view->base.targetObject;
            who->y = Map_SampleHeight(city->map, who->x, who->z);
            view->target.x = who->x;
            view->target.y = who->y - F(0x3EE66666u);
            view->target.z = who->z;
            view->target.w = std::bit_cast<float>(0x3F800000u);
            view->eye.x = who->x;
            view->eye.y = who->y - F(0x3F0CCCCDu);
            view->eye.z = who->z;
            view->eye.w = std::bit_cast<float>(0x3F800000u);
            frame->step.x = F(0);
            frame->step.y = F(0);
            frame->step.z = F(0x3FA66666u);
            frame->step.w = F(0);
            vf(4) = load(view.field(&TalkPersonView::base).field(&TalkView::target));
            vf(5) = load(view.field(&TalkPersonView::base).field(&TalkView::eye));
            set(vf(4), sub(vf(4), vf(5)), XYZW);
            store(direction, vf(4));
            frame->direction.y = F(0);
            vf(1) = load(direction);
            set(vf(2), mul(vf(1), vf(1)), XYZW);
            set(vf(2), add(vf(2), lane(vf(2), 1)), X);
            set(vf(2), add(vf(2), lane(vf(2), 2)), X);
            q() = rsqrt(x(vf(2)));
            set(vf(2), mul(vf(1), _mm_set1_ps(q())), XYZ);
            store(direction, vf(2));
            Mat4_Copy(turn, GuestPtr<Vec4>{globals::kIdentityMatrix});
            const ps2f directionZ = frame->direction.z, directionX = frame->direction.x;
            frame->turn[0].x = -directionZ;
            frame->turn[0].z = directionX;
            frame->turn[2].x = -directionX;
            frame->turn[2].z = -directionZ;
            applyMatrix(turn, step, step);
            vf(4) = load(view.field(&TalkPersonView::eye));
            vf(5) = load(step);
            set(vf(4), add(vf(4), vf(5)), XYZW);
            store(view.field(&TalkPersonView::eye), vf(4));
            const ps2f floor = Map_SampleHeight(city->map, view->eye.x, view->eye.z) - ps2f(0.5f);
            if (!(view->eye.y <= floor))
                view->eye.y = floor;
            view->facing = sdk::atan2f(frame->direction.x, frame->direction.z);
            return talkView;
        }
    }

    GuestPtr<TalkView> TalkPersonFar_ctor(GuestPtr<TalkView> view, GuestPtr<CityScene> city, int person)
    {
        return personViewCtor(view, city, person, globals::kTalkPersonFarVtable);
    }

    GuestPtr<TalkView> TalkPersonNear_ctor(GuestPtr<TalkView> view, GuestPtr<CityScene> city, int person)
    {
        return personViewCtor(view, city, person, globals::kTalkPersonNearVtable);
    }

    PORT_FN(TalkPersonFar_ctor);
    PORT_FN(TalkPersonNear_ctor);
    PORT_FN(MyHome_FloorHit);
    PORT_FN(MyHome_WallHit);
    PORT_FN(MyHome_Collide);
    PORT_FN(MyHome_Active);
    PORT_FN(MyHome_ctor);
}
