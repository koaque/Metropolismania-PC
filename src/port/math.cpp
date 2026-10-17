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
}
