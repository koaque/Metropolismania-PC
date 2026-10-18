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

    // Whether the segment from `point` down by `lift` crosses a triangle of the mesh (the matrix puts the mesh in
    // the world): of the crossings the one with the lowest y wins and is stored in *hit. The mesh number counts two
    // words of the blob's table per mesh.
    int Collide_SegmentA(int, int mesh, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix, GuestPtr<uint32_t> hit, ps2f lift)
    {
        port::GuestFrame<0xF0> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t row = 4u * static_cast<uint32_t>(mesh);
        uint32_t index = port::load<uint32_t>(collisionBlob() + row);
        uint32_t offset = index * 4u;
        ps2f nearest(100.0f);
        *GuestPtr<Vec4>{sp + 0xD0u} = *point;
        port::store<float>(sp + 0xD4u, static_cast<float>(ps2f(port::load<float>(sp + 0xD4u)) - lift));
        while (index < port::load<uint32_t>(collisionBlob() + row + 4u))
        {
            const uint32_t triangle = collisionBlob() + offset;
            transformVertex(matrix, triangle, sp + 0xA0u);
            transformVertex(matrix, triangle + 0x10u, sp + 0xB0u);
            transformVertex(matrix, triangle + 0x20u, sp + 0xC0u);
            if (Collide_RayTriangleB(point, GuestPtr<Vec4>{sp + 0xD0u}, GuestPtr<Vec4>{sp + 0xA0u}, GuestPtr<Vec4>{sp + 0xB0u},
                                     GuestPtr<Vec4>{sp + 0xC0u}, GuestPtr<Vec4>{sp + 0xE0u}) != 0)
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

    // The same for a ray from `point` along the triangles' normals within `range`: the nearest crossing's distance
    // is left in *distance (100 when none).
    int Collide_SegmentB(int, int mesh, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix, GuestPtr<uint32_t> hit, GuestPtr<ps2f> distance,
                         ps2f range)
    {
        port::GuestFrame<0x100> frame;
        const uint32_t sp = frame.ptr().address;
        port::store<float>(distance.address, 100.0f);
        const uint32_t row = 4u * static_cast<uint32_t>(mesh);
        uint32_t index = port::load<uint32_t>(collisionBlob() + row + 4u);
        uint32_t offset = index * 4u;
        while (index < port::load<uint32_t>(collisionBlob() + row + 8u))
        {
            const uint32_t triangle = collisionBlob() + offset;
            transformVertex(matrix, triangle, sp + 0xB0u);
            transformVertex(matrix, triangle + 0x10u, sp + 0xC0u);
            transformVertex(matrix, triangle + 0x20u, sp + 0xD0u);
            if (Collide_RayTriangleA(point, GuestPtr<Vec4>{sp + 0xB0u}, GuestPtr<Vec4>{sp + 0xC0u}, GuestPtr<Vec4>{sp + 0xD0u},
                                     GuestPtr<Vec4>{sp + 0xE0u}, GuestPtr<ps2f>{sp + 0xFCu}, range) != 0)
            {
                const ps2f found(port::load<float>(sp + 0xFCu));
                if (!(ps2f(port::load<float>(distance.address)) <= found))
                {
                    port::store<float>(distance.address, static_cast<float>(found));
                    *GuestPtr<Vec4>{hit.address} = *GuestPtr<Vec4>{sp + 0xE0u};
                }
            }
            offset += 0x30u;
            index += 12u;
        }
        return ps2f(port::load<float>(distance.address)) == ps2f(100.0f) ? 0 : 1;
    }

    PORT_FN(Collide_SegmentA);
    PORT_FN(Collide_SegmentB);
}
