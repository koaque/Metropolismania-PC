// Segment tests against the collision meshes of the buildings.

#include "game_functions.h"
#include "game_globals.h"
#include "vu0.h"

namespace mm
{
    namespace
    {
        // The collision blob: a table of (first, last) vertex indices per mesh, then the triangles, 12 floats each
        // (three vertices of four).
        uint32_t collisionBlob() { return port::load<uint32_t>(0x001BDBDCu); }

        void transformVertex(GuestPtr<Vec4> matrix, uint32_t vertex, uint32_t out)
        {
            vu0::applyMatrix(matrix, GuestPtr<Vec4>{vertex}, GuestPtr<Vec4>{out});
        }
    }

    // Whether the segment from `point` up by `lift` (to y - lift; heights grow downwards) crosses a triangle of the
    // mesh (the matrix puts the mesh in the world): of the crossings the highest (smallest y) wins and is stored in
    // *hit. The mesh number counts two words of the blob's table per mesh.
    int Collide_MeshFloorBelow(int, int mesh, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix, GuestPtr<Vec4> hit, ps2f lift)
    {
        struct Locals
        {
            uint8_t gap_00[0xA0];
            Vec4 corners[3];           // +0xA0 the triangle, moved by the matrix
            Vec4 above;                // +0xD0 the point `lift` higher (y - lift), the segment's end
            Vec4 crossing;             // +0xE0 where it crosses the triangle
        };
        static_assert(offsetof(Locals, corners) == 0xA0 && offsetof(Locals, above) == 0xD0 && offsetof(Locals, crossing) == 0xE0 &&
                      sizeof(Locals) == 0xF0);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<Vec4> corners = frame.at(&Locals::corners);
        const uint32_t row = 4u * static_cast<uint32_t>(mesh);
        uint32_t index = port::load<uint32_t>(collisionBlob() + row);
        uint32_t offset = index * 4u;
        ps2f nearest(100.0f);
        frame->above = *point;
        frame->above.y = frame->above.y - lift;
        while (index < port::load<uint32_t>(collisionBlob() + row + 4u))
        {
            const uint32_t triangle = collisionBlob() + offset;
            transformVertex(matrix, triangle, corners.address);
            transformVertex(matrix, triangle + 0x10u, (corners + 1).address);
            transformVertex(matrix, triangle + 0x20u, (corners + 2).address);
            if (Collide_SegmentTriangle(point, frame.at(&Locals::above), corners, corners + 1, corners + 2,
                                        frame.at(&Locals::crossing)) != 0)
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

    // The same for a ray from `point` along the triangles' normals within `range`: the nearest crossing's distance
    // is left in *distance (100 when none).
    int Collide_MeshNearestHit(int, int mesh, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix, GuestPtr<Vec4> hit, GuestPtr<ps2f> distance,
                         ps2f range)
    {
        struct Locals
        {
            uint8_t gap_00[0xB0];
            Vec4 corners[3];           // +0xB0 the triangle, moved by the matrix
            Vec4 crossing;             // +0xE0 where the ray meets it
            uint8_t gap_F0[0xC];
            ps2f found;                // +0xFC how far along the ray
        };
        static_assert(offsetof(Locals, corners) == 0xB0 && offsetof(Locals, crossing) == 0xE0 && offsetof(Locals, found) == 0xFC &&
                      sizeof(Locals) == 0x100);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<Vec4> corners = frame.at(&Locals::corners);
        *distance = 100.0f;
        const uint32_t row = 4u * static_cast<uint32_t>(mesh);
        uint32_t index = port::load<uint32_t>(collisionBlob() + row + 4u);
        uint32_t offset = index * 4u;
        while (index < port::load<uint32_t>(collisionBlob() + row + 8u))
        {
            const uint32_t triangle = collisionBlob() + offset;
            transformVertex(matrix, triangle, corners.address);
            transformVertex(matrix, triangle + 0x10u, (corners + 1).address);
            transformVertex(matrix, triangle + 0x20u, (corners + 2).address);
            if (Collide_NormalRayTriangle(point, corners, corners + 1, corners + 2, frame.at(&Locals::crossing),
                                          frame.at(&Locals::found), range) != 0)
            {
                const ps2f found = frame->found;
                if (!(*distance <= found))
                {
                    *distance = found;
                    *hit = frame->crossing;
                }
            }
            offset += 0x30u;
            index += 12u;
        }
        return *distance == ps2f(100.0f) ? 0 : 1;
    }

    PORT_FN(Collide_MeshFloorBelow);
    PORT_FN(Collide_MeshNearestHit);
}
