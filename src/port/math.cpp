// Math helpers, and the vector and matrix routines that run on VU0 in macro mode.

#include "game_functions.h"
#include "sdk.h"
#include "vu0.h"

namespace mm
{
    // floor(log2(value)) for value >= 1; 0 for anything smaller.
    int Math_Log2(int value)
    {
        int bits = 0;
        for (; value > 1; value >>= 1)
            ++bits;
        return bits;
    }

    // Where the line through `point` with direction (dx, dy, dz) meets the plane a x + b y + c z + d = 0:
    // out = (x, y, z, 1) and 1, or 0 (out untouched) when the line runs parallel to the plane. Directions
    // with zero components take their own formulas. Every step keeps the original's operation order (PS2
    // rounding), and point is read again after each store to out, as the original does.
    int Math_LinePlaneIntersect(GuestPtr<ps2f> point, GuestPtr<ps2f> out, ps2f dx, ps2f dy, ps2f dz, ps2f a, ps2f b, ps2f c, ps2f d)
    {
        const ps2f zero = 0.0f;
        const ps2f adx = a * dx, bdy = b * dy, cdz = c * dz;
        const ps2f adxBdy = adx + bdy;
        const ps2f denom = cdz + adxBdy;
        if (denom == zero)
            return 0;
        if (dz * (dx * dy) != zero)
        {
            // x from the plane, then y and z along the line.
            const ps2f byCz = bdy + cdz;
            const ps2f numerator = point[0] * byCz - dx * (d + (b * point[1] + c * point[2]));
            const ps2f x = numerator / denom;
            out[0] = x;
            out[1] = point[1] + dy * (x - point[0]) / dx;
            out[2] = point[2] + dz * (out[0] - point[0]) / dx;
        }
        else if (dx == zero)
        {
            if (dy * dz != zero)
            {
                // x fixed; y from the plane, z along the line.
                const ps2f byCz = bdy + cdz;
                out[0] = point[0];
                const ps2f dPlusAx = d + a * point[0];
                const ps2f cross = dz * point[1] - dy * point[2];
                const ps2f y = (c * cross + -dy * dPlusAx) / byCz;
                out[1] = y;
                out[2] = point[2] + dz * (y - point[1]) / dy;
            }
            else if (dy == zero)
            {
                // Along z only.
                out[0] = point[0];
                out[1] = point[1];
                out[2] = -(d + (a * point[0] + b * point[1])) / c;
            }
            else
            {
                // Along y only.
                out[0] = point[0];
                out[1] = -(d + (a * point[0] + c * point[2])) / b;
                out[2] = point[2];
            }
        }
        else if (dy == zero)
        {
            if (dx * dz != zero)
            {
                // y fixed; x from the plane, z along the line.
                const ps2f axCz = adx + cdz;
                const ps2f dPlusBy = d + b * point[1];
                const ps2f cross = dz * point[0] - dx * point[2];
                out[0] = (-dx * dPlusBy + c * cross) / axCz;
                out[1] = point[1];
                out[2] = point[2] + dz * (out[0] - point[0]) / dx;
            }
            else
            {
                // Along x only.
                out[0] = -(d + (b * point[1] + c * point[2])) / a;
                out[1] = point[1];
                out[2] = point[2];
            }
        }
        else
        {
            // z fixed; x from the plane, y along the line.
            const ps2f dPlusCz = d + c * point[2];
            const ps2f cross = dy * point[0] - dx * point[1];
            const ps2f x = (b * cross - dx * dPlusCz) / adxBdy;
            out[0] = x;
            out[1] = point[1] + dy * (x - point[0]) / dx;
            out[2] = point[2];
        }
        out[3] = 1.0f;
        return 1;
    }

    // Perspective projection for a screen half-size (screen[0], screen[1]) at `distance`, with near and far
    // planes: the identity, then the scale, depth and perspective terms.
    void Math_ProjectionMatrix(GuestPtr<ps2f> matrix, GuestPtr<ps2f> screen, ps2f distance, ps2f zNear, ps2f zFar)
    {
        const ps2f halfWidth = zNear * screen[0] / distance;
        const ps2f halfHeight = zNear * screen[1] / distance;
        sdk::sceVu0UnitMatrix(matrix.address);
        const ps2f twoNear = ps2f(2.0f) * zNear;
        matrix[0] = twoNear / (halfWidth + halfWidth);
        matrix[5] = twoNear / (halfHeight + halfHeight);
        const ps2f depth = zFar - zNear;
        matrix[10] = (zFar + zNear) / depth;
        matrix[14] = ps2f(-2.0f) * (zFar * zNear) / depth;
        matrix[11] = 1.0f;
        matrix[15] = 0.0f;
    }

    // ---- VU0 (registers as the original leaves them: vf1-vf12, ACC, Q)

    // out = a x b for 4x4 matrices of row vectors: each row of a times the rows of b. All of a and b is
    // loaded first, so out may be either of them.
    void Mat4_MulVu0(GuestPtr<Vec4> out, GuestPtr<Vec4> a, GuestPtr<Vec4> b)
    {
        using namespace vu0;
        for (int row = 0; row < 4; ++row)
            vf(1 + row) = load(a + row);
        for (int row = 0; row < 4; ++row)
            vf(5 + row) = load(b + row);
        for (int row = 0; row < 4; ++row)
        {
            const __m128 r = vf(1 + row);
            set(acc(), mul(vf(5), lane(r, 0)), XYZW);
            set(acc(), add(acc(), mul(vf(6), lane(r, 1))), XYZW);
            set(acc(), add(acc(), mul(vf(7), lane(r, 2))), XYZW);
            set(vf(9 + row), add(acc(), mul(vf(8), lane(r, 3))), XYZW);
        }
        for (int row = 0; row < 4; ++row)
            store(out + row, vf(9 + row));
    }

    // out = v x matrix (row vector).
    void Math_MulMatVecVu0(GuestPtr<Vec4> out, GuestPtr<Vec4> matrix, GuestPtr<Vec4> v)
    {
        using namespace vu0;
        for (int row = 0; row < 4; ++row)
            vf(1 + row) = load(matrix + row);
        vf(5) = load(v);
        set(acc(), mul(vf(1), lane(vf(5), 0)), XYZW);
        set(acc(), add(acc(), mul(vf(2), lane(vf(5), 1))), XYZW);
        set(acc(), add(acc(), mul(vf(3), lane(vf(5), 2))), XYZW);
        set(vf(9), add(acc(), mul(vf(4), lane(vf(5), 3))), XYZW);
        store(out, vf(9));
    }

    // out.xyz = v.xyz / |v.xyz|; out.w = v.w squared (left over from the squared length).
    void Vec3_Normalize(GuestPtr<Vec4> out, GuestPtr<Vec4> v)
    {
        using namespace vu0;
        vf(1) = load(v);
        set(vf(2), mul(vf(1), vf(1)), XYZW);
        set(vf(2), add(vf(2), lane(vf(2), 1)), X);
        set(vf(2), add(vf(2), lane(vf(2), 2)), X);
        q() = rsqrt(x(vf(2)));
        set(vf(2), mul(vf(1), _mm_set1_ps(q())), XYZ);
        store(out, vf(2));
    }

    // out.xyz = the unit normal of triangle (a, b, c): (b - a) x (c - a), normalized. out.w is vf6.w squared,
    // vf6.w being whatever earlier VU0 code left there (the cross product only writes xyz).
    void Math_TriangleNormal(GuestPtr<Vec4> out, GuestPtr<Vec4> a, GuestPtr<Vec4> b, GuestPtr<Vec4> c)
    {
        using namespace vu0;
        vf(1) = load(a);
        vf(2) = load(b);
        vf(3) = load(c);
        set(vf(4), sub(vf(2), vf(1)), XYZW);
        set(vf(5), sub(vf(3), vf(1)), XYZW);
        set(acc(), mul(yzx(vf(4)), zxy(vf(5))), XYZ);
        set(vf(6), sub(acc(), mul(yzx(vf(5)), zxy(vf(4)))), XYZ);
        set(vf(7), mul(vf(6), vf(6)), XYZW);
        set(vf(7), add(vf(7), lane(vf(7), 1)), X);
        set(vf(7), add(vf(7), lane(vf(7), 2)), X);
        q() = rsqrt(x(vf(7)));
        set(vf(7), mul(vf(6), _mm_set1_ps(q())), XYZ);
        store(out, vf(7));
    }

    // Whether point p lies inside triangle (a, b, c), on VU0: the normals (a - p) x (c - p), (b - a) x (c - a)
    // and (p - b) x (c - b) must all face the same way (no dot product of neighbours below 0). The original keeps
    // its vectors in stack temporaries it always writes in full before reading them back, so only the register
    // sequence matters, including the w lanes the xyz operations leave alone.
    int Collide_PointInTriangle(GuestPtr<Vec4> p, GuestPtr<Vec4> a, GuestPtr<Vec4> b, GuestPtr<Vec4> c)
    {
        using namespace vu0;
        const auto difference = [](GuestPtr<Vec4> to, GuestPtr<Vec4> from) {
            vf(4) = load(to);
            vf(5) = load(from);
            set(vf(4), sub(vf(4), vf(5)), XYZ);
            return vf(4);
        };
        const auto cross = [](__m128 u, __m128 v) {
            vf(1) = u;
            vf(2) = v;
            set(acc(), mul(yzx(vf(1)), zxy(vf(2))), XYZ);
            set(vf(3), sub(acc(), mul(yzx(vf(2)), zxy(vf(1)))), XYZ);
            return vf(3);
        };
        const auto dot = [](__m128 u, __m128 v) {
            vf(4) = u;
            vf(5) = v;
            set(vf(4), mul(vf(4), vf(5)), XYZ);
            set(vf(4), add(vf(4), lane(vf(4), 1)), X);
            set(vf(4), add(vf(4), lane(vf(4), 2)), X);
            return ps2f(x(vf(4)));
        };
        const __m128 edgeA = difference(a, p);
        const __m128 edgeC = difference(c, p);
        const __m128 n0 = cross(edgeA, edgeC);
        const __m128 edgeAB = difference(b, a);
        const __m128 edgeAC = difference(c, a);
        const __m128 n1 = cross(edgeAB, edgeAC);
        if (dot(n0, n1) < ps2f(0.0f))
            return 0;
        const __m128 edgeBP = difference(p, b);
        const __m128 edgeBC = difference(c, b);
        const __m128 n2 = cross(edgeBP, edgeBC);
        if (dot(n1, n2) < ps2f(0.0f))
            return 0;
        return dot(n2, n0) < ps2f(0.0f) ? 0 : 1;
    }

    // Whether the line from `origin` along the triangle's normal meets triangle (a, b, c) closer than `range`: the
    // triangle's plane, the line-plane intersection into `hit`, the distance to it (VU0) into *distance, then the
    // point-in-triangle test.
    int Collide_RayTriangleA(GuestPtr<Vec4> origin, GuestPtr<Vec4> a, GuestPtr<Vec4> b, GuestPtr<Vec4> c, GuestPtr<Vec4> hit,
                             GuestPtr<ps2f> distance, ps2f range)
    {
        using namespace vu0;
        port::GuestLocal<Vec4> length; // declared in this order: the original's slots, entry sp - 0x10 and - 0x20
        port::GuestLocal<Vec4> plane;
        Math_TriangleNormal(plane, a, b, c);
        plane->w = (-plane->x * a->x - plane->y * a->y) - plane->z * a->z;
        if (Math_LinePlaneIntersect(GuestPtr<ps2f>{origin.address}, GuestPtr<ps2f>{hit.address}, -plane->x, -plane->y, -plane->z,
                                    plane->x, plane->y, plane->z, plane->w) == 0)
            return 0;
        vf(4) = load(hit);
        vf(5) = load(origin);
        set(vf(6), sub(vf(5), vf(4)), XYZW);
        set(vf(7), mul(vf(6), vf(6)), XYZ);
        set(vf(8), add(vf(7), lane(vf(7), 1)), X);
        set(vf(8), add(vf(8), lane(vf(7), 2)), X);
        q() = sqrtQ(x(vf(8)));
        set(vf(9), add(vf(0), _mm_set1_ps(q())), XYZW);
        store(length, vf(9));
        *distance = length->x;
        if (!(length->x < range))
            return 0;
        return Collide_PointInTriangle(a, b, c, hit);
    }

    // Whether the segment from `start` to `end` crosses triangle (a, b, c): the line along the segment's unit
    // direction meets the triangle's plane at `hit`, which must lie between the ends (the vectors to them point
    // apart) and inside the triangle.
    int Collide_RayTriangleB(GuestPtr<Vec4> start, GuestPtr<Vec4> end, GuestPtr<Vec4> a, GuestPtr<Vec4> b, GuestPtr<Vec4> c,
                             GuestPtr<Vec4> hit)
    {
        using namespace vu0;
        // Declared in this order, they take the original's slots, entry sp - 0x10 down to - 0x50.
        port::GuestLocal<Vec4> product;
        port::GuestLocal<Vec4> toStart;
        port::GuestLocal<Vec4> toEnd;
        port::GuestLocal<Vec4> plane;
        port::GuestLocal<Vec4> direction;
        vf(4) = load(end);
        vf(5) = load(start);
        set(vf(4), sub(vf(4), vf(5)), XYZW);
        store(direction, vf(4));
        vf(1) = load(direction);
        set(vf(2), mul(vf(1), vf(1)), XYZW);
        set(vf(2), add(vf(2), lane(vf(2), 1)), X);
        set(vf(2), add(vf(2), lane(vf(2), 2)), X);
        q() = rsqrt(x(vf(2)));
        set(vf(2), mul(vf(1), _mm_set1_ps(q())), XYZ);
        store(direction, vf(2));
        Math_TriangleNormal(plane, a, b, c);
        plane->w = (-plane->x * a->x - plane->y * a->y) - plane->z * a->z;
        if (Math_LinePlaneIntersect(GuestPtr<ps2f>{start.address}, GuestPtr<ps2f>{hit.address}, direction->x, direction->y, direction->z,
                                    plane->x, plane->y, plane->z, plane->w) == 0)
            return 0;
        vf(4) = load(hit);
        vf(5) = load(start);
        set(vf(4), sub(vf(4), vf(5)), XYZW);
        store(toStart, vf(4));
        vf(4) = load(hit);
        vf(5) = load(end);
        set(vf(4), sub(vf(4), vf(5)), XYZW);
        store(toEnd, vf(4));
        vf(4) = load(toStart);
        vf(5) = load(toEnd);
        set(vf(4), mul(vf(4), vf(5)), XYZ);
        set(vf(4), add(vf(4), lane(vf(4), 1)), X);
        set(vf(4), add(vf(4), lane(vf(4), 2)), X);
        store(product, vf(4));
        if (!(product->x <= ps2f(0.0f)))
            return 0;
        return Collide_PointInTriangle(a, b, c, hit) != 0 ? 1 : 0;
    }

    // Screen position of a point: point x matrix, divided by its w, in 12.4 fixed point (out as four ints).
    // A w of 0 gives 0 (the runtime's vdiv).
    void Math_ProjectPoint(int, GuestPtr<Vec4> out, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix)
    {
        using namespace vu0;
        for (int row = 0; row < 4; ++row)
            vf(1 + row) = load(matrix + row);
        vf(5) = load(point);
        set(acc(), mul(vf(1), lane(vf(5), 0)), XYZW);
        set(acc(), add(acc(), mul(vf(2), lane(vf(5), 1))), XYZW);
        set(acc(), add(acc(), mul(vf(3), lane(vf(5), 2))), XYZW);
        set(vf(9), add(acc(), mul(vf(4), lane(vf(5), 3))), XYZW);
        q() = divQ(x(lane(vf(0), 3)), x(lane(vf(9), 3)));
        set(vf(9), mul(vf(9), _mm_set1_ps(q())), XYZW);
        set(vf(10), ftoi4(vf(9)), XYZW);
        store(out, vf(10));
    }

    // The bit pattern of sqrt((float)value), through VU0 and a 16-byte stack slot as in the original, whose
    // other 12 bytes end up converted in vf4's y, z and w.
    uint32_t Math_SqrtBits(int value)
    {
        using namespace vu0;
        port::GuestLocal<Vec4> slot;
        port::store<int32_t>(slot.ptr().address, value);
        vf(4) = load(slot);
        set(vf(4), itof0(vf(4)), XYZW);
        q() = sqrtQ(x(vf(4)));
        set(vf(9), add(vf(0), _mm_set1_ps(q())), XYZW);
        store(slot, vf(9));
        return port::load<uint32_t>(slot.ptr().address);
    }

    PORT_FN(Math_Log2);
    PORT_FN(Math_LinePlaneIntersect);
    PORT_FN(Math_ProjectionMatrix);
    PORT_FN(Math_ProjectPoint);
    PORT_FN(Math_SqrtBits);
    PORT_FN(Mat4_MulVu0);
    PORT_FN(Math_MulMatVecVu0);
    PORT_FN(Vec3_Normalize);
    PORT_FN(Math_TriangleNormal);
    PORT_FN(Collide_PointInTriangle);
    PORT_FN(Collide_RayTriangleA);
    PORT_FN(Collide_RayTriangleB);
}
