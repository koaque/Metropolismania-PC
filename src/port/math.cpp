// Math helpers, and the vector and matrix routines that run on VU0 in macro mode.

#include "game_functions.h"
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
    PORT_FN(Math_ProjectPoint);
    PORT_FN(Math_SqrtBits);
    PORT_FN(Mat4_MulVu0);
    PORT_FN(Math_MulMatVecVu0);
    PORT_FN(Vec3_Normalize);
    PORT_FN(Math_TriangleNormal);
}
