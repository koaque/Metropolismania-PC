// The platform layer, libvu0: the vector and matrix routines of the SDK that run on the EE's first vector unit in macro mode
// (plan P7), written from what the functions do and proven against the recompiled functions by the difftest. Prototypes are in
// config/platform_signatures.txt.
//
// Each routine is written as the VU0 instructions it runs (src/port/vu0.h), not as plain float math, because the callers read
// what it leaves behind: the scratch registers vf4 to vf9 and the accumulator are compared by the difftest like any other result.
// A matrix is four vectors, one row each; a point is multiplied by a matrix as row0*x + row1*y + row2*z + row3*w.

#include "game_functions.h"
#include "sdk.h"
#include "vu0.h"

#include <bit>

namespace mm
{
    namespace
    {
        using namespace vu0;

        GuestPtr<Vec4> row(GuestPtr<Vec4> matrix, uint32_t index) { return GuestPtr<Vec4>{matrix.address + 0x10u * index}; }

        // vmr32: the lanes move down by one, x comes round to w.
        __m128 rotate32(__m128 v) { return _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 3, 2, 1)); }

        // The four rows of a matrix in vf4 to vf7, then for each of the four vectors at `in`: out = row0*x + row1*y + row2*z + row3*w, with
        // the vector in vf8 and the result in vf9 (what MulMatrix runs, and ApplyMatrix for one vector).
        void multiplyRows(GuestPtr<Vec4> out, GuestPtr<Vec4> matrix, GuestPtr<Vec4> in, uint32_t count)
        {
            for (uint32_t i = 0; i < 4; ++i)
                vf(4 + i) = load(row(matrix, i));
            for (uint32_t i = 0; i < count; ++i)
            {
                vf(8) = load(row(in, i));
                set(acc(), mul(vf(4), lane(vf(8), 0)), XYZW);
                set(acc(), add(acc(), mul(vf(5), lane(vf(8), 1))), XYZW);
                set(acc(), add(acc(), mul(vf(6), lane(vf(8), 2))), XYZW);
                set(vf(9), add(acc(), mul(vf(7), lane(vf(8), 3))), XYZW);
                store(row(out, i), vf(9));
            }
        }

        // The sine and the cosine of vf6.x (an angle within about +-pi/2) by a polynomial, left in vf4 as (+-cos, sin, 0, 0): the sign of the
        // cosine is flipped when `negate` is set. Leaves vf5 = 0 and the working values in vf6 to vf8 and Q.
        void cosineAndSine(bool negate)
        {
            // the polynomial's coefficients for x^9, x^7, x^5 and x^3 (x in lane 0)
            vf(5) = _mm_castsi128_ps(_mm_set_epi32(static_cast<int>(0xBE2AAAA4u), 0x3C08873E, static_cast<int>(0xB94FB21Fu), 0x362E9C14));
            set(vf(6), rotate32(vf(6)), W);
            set(vf(4), add(vf(0), lane(vf(6), 0)), X);
            set(vf(6), mul(vf(6), vf(6)), X);
            set(vf(4), mul(vf(4), lane(vf(0), 0)), Y | Z | W);
            set(vf(8), mul(vf(5), lane(vf(6), 3)), XYZW);
            set(vf(5), sub(vf(0), vf(0)), XYZW);
            set(vf(8), mul(vf(8), lane(vf(6), 0)), XYZW);
            set(vf(8), mul(vf(8), lane(vf(6), 0)), XYZ);
            set(vf(4), add(vf(4), lane(vf(8), 3)), X);
            set(vf(8), mul(vf(8), lane(vf(6), 0)), X | Y);
            set(vf(4), add(vf(4), lane(vf(8), 2)), X);
            set(vf(8), mul(vf(8), lane(vf(6), 0)), X);
            set(vf(4), add(vf(4), lane(vf(8), 1)), X);
            set(vf(4), add(vf(4), lane(vf(8), 0)), X);
            set(vf(4), add(vf(5), lane(vf(4), 0)), X | Y);
            // cos = sqrt(1 - sin^2)
            set(vf(7), mul(vf(4), vf(4)), X);
            set(vf(7), sub(vf(0), lane(vf(7), 0)), W);
            q() = sqrtQ(vu0::x(lane(vf(7), 3)));
            set(vf(7), add(vf(0), _mm_set1_ps(q())), X);
            if (negate)
                set(vf(4), sub(vf(5), lane(vf(7), 0)), X);
            else
                set(vf(4), add(vf(5), lane(vf(7), 0)), X);
        }

        // The angle as the three Rot routines take it: moved by a quarter turn (pi/2 - angle, or pi/2 + angle for a negative one, which
        // flips the sign of the cosine), then the sine and cosine of that, which leaves vf4 = (sin angle, cos angle, 0, 0) and vf5 = 0.
        void sineAndCosineOf(ps2f angle)
        {
            const ps2f quarterTurn{std::bit_cast<float>(0x3FC90FDBu)};
            bool negative;
            if (angle < ps2f(0.0f))
            {
                angle = quarterTurn + angle;
                negative = true;
            }
            else
            {
                angle = quarterTurn - angle;
                negative = false;
            }
            // mfc1 t0 / qmtc2 vf6: the angle in lane 0, the rest of the register as the move left it
            const uint32_t bits = std::bit_cast<uint32_t>(static_cast<float>(angle));
            const __m128i upper = _mm_unpackhi_epi64(context().r[8], context().r[8]);
            vf(6) = _mm_castsi128_ps(_mm_unpacklo_epi64(_mm_cvtsi64_si128(static_cast<int64_t>(static_cast<int32_t>(bits))), upper));
            cosineAndSine(negative);
        }

        // The rotation's matrix is in vf6 to vf9 (one row each): every row of `in` goes through it.
        void rotateRows(GuestPtr<Vec4> out, GuestPtr<Vec4> in)
        {
            for (uint32_t i = 0; i < 4; ++i)
            {
                vf(4) = load(row(in, i));
                set(acc(), mul(vf(6), lane(vf(4), 0)), XYZW);
                set(acc(), add(acc(), mul(vf(7), lane(vf(4), 1))), XYZW);
                set(acc(), add(acc(), mul(vf(8), lane(vf(4), 2))), XYZW);
                set(vf(5), add(acc(), mul(vf(9), lane(vf(4), 3))), XYZW);
                store(row(out, i), vf(5));
            }
        }
    }

    // out = matrix * point (the point's w picks up the fourth row).
    void sdk_sceVu0ApplyMatrix(GuestPtr<Vec4> out, GuestPtr<Vec4> matrix, GuestPtr<Vec4> point) { multiplyRows(out, matrix, point, 1); }

    // out = a * b: each row of b through a.
    void sdk_sceVu0MulMatrix(GuestPtr<Vec4> out, GuestPtr<Vec4> a, GuestPtr<Vec4> b) { multiplyRows(out, a, b, 4); }

    // The cross product of the first three lanes (w = 0).
    void sdk_sceVu0OuterProduct(GuestPtr<Vec4> out, GuestPtr<Vec4> a, GuestPtr<Vec4> b)
    {
        vf(4) = load(a);
        vf(5) = load(b);
        set(acc(), mul(yzx(vf(4)), zxy(vf(5))), XYZ);
        set(vf(6), sub(acc(), mul(yzx(vf(5)), zxy(vf(4)))), XYZ);
        set(vf(6), sub(vf(6), vf(6)), W);
        store(out, vf(6));
    }

    // The vector divided by its length (w = 0).
    void sdk_sceVu0Normalize(GuestPtr<Vec4> out, GuestPtr<Vec4> in)
    {
        vf(4) = load(in);
        set(vf(5), mul(vf(4), vf(4)), XYZ);
        set(vf(5), add(vf(5), lane(vf(5), 1)), X);
        set(vf(5), add(vf(5), lane(vf(5), 2)), X);
        q() = sqrtQ(vu0::x(vf(5)));
        set(vf(5), add(vf(0), _mm_set1_ps(q())), X);
        q() = divQ(vu0::x(lane(vf(0), 3)), vu0::x(vf(5)));
        set(vf(6), sub(vf(0), vf(0)), XYZW);
        set(vf(6), mul(vf(4), _mm_set1_ps(q())), XYZ);
        store(out, vf(6));
    }

    void sdk_sceVu0TransposeMatrix(GuestPtr<Vec4> out, GuestPtr<Vec4> in)
    {
        __m128 r0 = load(row(in, 0)), r1 = load(row(in, 1)), r2 = load(row(in, 2)), r3 = load(row(in, 3));
        _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
        store(row(out, 0), r0);
        store(row(out, 1), r1);
        store(row(out, 2), r2);
        store(row(out, 3), r3);
    }

    // The inverse of a rotation with a translation: the rotation transposed, the translation turned back through it.
    void sdk_sceVu0InversMatrix(GuestPtr<Vec4> out, GuestPtr<Vec4> in)
    {
        __m128 r0 = load(row(in, 0)), r1 = load(row(in, 1)), r2 = load(row(in, 2));
        vf(4) = load(row(in, 3));
        vf(5) = vf(4);
        set(vf(4), sub(vf(4), vf(4)), XYZ);
        vf(9) = vf(4);
        __m128 r3 = vf(4);
        _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
        vf(6) = r0;
        vf(7) = r1;
        vf(8) = r2;
        set(acc(), mul(vf(6), lane(vf(5), 0)), XYZ);
        set(acc(), add(acc(), mul(vf(7), lane(vf(5), 1))), XYZ);
        set(vf(4), add(acc(), mul(vf(8), lane(vf(5), 2))), XYZ);
        set(vf(4), sub(vf(9), vf(4)), XYZ);
        store(row(out, 0), r0);
        store(row(out, 1), r1);
        store(row(out, 2), r2);
        store(row(out, 3), vf(4));
    }

    // out = in * scale (all four lanes). The scale reaches vf5 through a general register, whose upper half the move leaves as it was.
    void sdk_sceVu0ScaleVector(GuestPtr<Vec4> out, GuestPtr<Vec4> in, ps2f scale)
    {
        vf(4) = load(in);
        const uint32_t bits = std::bit_cast<uint32_t>(static_cast<float>(scale));
        const __m128i upper = _mm_unpackhi_epi64(context().r[8], context().r[8]);
        vf(5) = _mm_castsi128_ps(_mm_unpacklo_epi64(_mm_cvtsi64_si128(static_cast<int64_t>(static_cast<int32_t>(bits))), upper));
        set(vf(6), mul(vf(4), lane(vf(5), 0)), XYZW);
        store(out, vf(6));
    }

    // The matrix with a translation added (x, y and z of the fourth row).
    void sdk_sceVu0TransMatrix(GuestPtr<Vec4> out, GuestPtr<Vec4> matrix, GuestPtr<Vec4> translation)
    {
        vf(4) = load(translation);
        vf(5) = load(row(matrix, 3));
        const __m128 r0 = load(row(matrix, 0)), r1 = load(row(matrix, 1)), r2 = load(row(matrix, 2));
        set(vf(5), add(vf(5), vf(4)), XYZ);
        store(row(out, 0), r0);
        store(row(out, 1), r1);
        store(row(out, 2), r2);
        store(row(out, 3), vf(5));
    }

    void sdk_sceVu0CopyVector(GuestPtr<Vec4> out, GuestPtr<Vec4> in) { store(out, load(in)); }

    void sdk_sceVu0UnitMatrix(GuestPtr<Vec4> out)
    {
        set(vf(4), sub(vf(0), vf(0)), XYZW);
        set(vf(4), add(vf(4), vf(0)), W);
        set(vf(5), rotate32(vf(4)), XYZW);
        set(vf(6), rotate32(vf(5)), XYZW);
        set(vf(7), rotate32(vf(6)), XYZW);
        store(row(out, 3), vf(4));
        store(row(out, 2), vf(5));
        store(row(out, 1), vf(6));
        store(row(out, 0), vf(7));
    }

    // The matrix turned about the z axis: in * (rotation by the angle).
    void sdk_sceVu0RotMatrixZ(GuestPtr<Vec4> out, GuestPtr<Vec4> in, ps2f angle)
    {
        sineAndCosineOf(angle);
        set(vf(6), vf(5), XYZW);
        set(vf(7), vf(5), XYZW);
        set(vf(9), vf(0), XYZW);
        set(vf(9), sub(vf(9), vf(9)), XYZ);
        set(vf(8), rotate32(vf(9)), XYZW);
        set(vf(4), sub(vf(4), vf(4)), Z | W);
        set(vf(6), add(vf(5), lane(vf(4), 0)), Y);
        set(vf(6), add(vf(5), lane(vf(4), 1)), X);
        set(vf(7), sub(vf(5), lane(vf(4), 0)), X);
        set(vf(7), add(vf(5), lane(vf(4), 1)), Y);
        rotateRows(out, in);
    }

    void sdk_sceVu0RotMatrixX(GuestPtr<Vec4> out, GuestPtr<Vec4> in, ps2f angle)
    {
        sineAndCosineOf(angle);
        for (int reg = 6; reg <= 9; ++reg)
            set(vf(reg), vf(5), XYZW);
        set(vf(6), add(vf(5), lane(vf(0), 3)), X);
        set(vf(9), add(vf(5), lane(vf(0), 3)), W);
        set(vf(4), sub(vf(4), vf(4)), Z | W);
        set(vf(7), add(vf(5), lane(vf(4), 0)), Z);
        set(vf(7), add(vf(5), lane(vf(4), 1)), Y);
        set(vf(8), sub(vf(5), lane(vf(4), 0)), Y);
        set(vf(8), add(vf(5), lane(vf(4), 1)), Z);
        rotateRows(out, in);
    }

    void sdk_sceVu0RotMatrixY(GuestPtr<Vec4> out, GuestPtr<Vec4> in, ps2f angle)
    {
        sineAndCosineOf(angle);
        for (int reg = 6; reg <= 9; ++reg)
            set(vf(reg), vf(5), XYZW);
        set(vf(7), add(vf(5), lane(vf(0), 3)), Y);
        set(vf(9), add(vf(5), lane(vf(0), 3)), W);
        set(vf(4), sub(vf(4), vf(4)), Z | W);
        set(vf(6), sub(vf(5), lane(vf(4), 0)), Z);
        set(vf(6), add(vf(5), lane(vf(4), 1)), X);
        set(vf(8), add(vf(5), lane(vf(4), 0)), X);
        set(vf(8), add(vf(5), lane(vf(4), 1)), Z);
        rotateRows(out, in);
    }

    // The world to camera matrix: the axes from the viewing direction and the up vector, then the inverse of the placement at `eye`.
    void sdk_sceVu0CameraMatrix(GuestPtr<Vec4> out, GuestPtr<Vec4> eye, GuestPtr<Vec4> direction, GuestPtr<Vec4> up)
    {
        port::GuestFrame<0xB0> frame;
        const uint32_t base = frame.ptr().address;
        const GuestPtr<Vec4> placement{base}, side{base + 0x40u};
        sdk_sceVu0UnitMatrix(placement);
        sdk_sceVu0OuterProduct(side, up, direction);
        sdk_sceVu0Normalize(placement, side);
        sdk_sceVu0Normalize(row(placement, 2), direction);
        sdk_sceVu0OuterProduct(row(placement, 1), row(placement, 2), placement);
        sdk_sceVu0TransMatrix(placement, placement, eye);
        sdk_sceVu0InversMatrix(out, placement);
    }

    // The light directions, negated and normalised, as the rows of a matrix with the fourth row (0, 0, 0, 1), then transposed.
    void sdk_sceVu0NormalLightMatrix(GuestPtr<Vec4> out, GuestPtr<Vec4> light0, GuestPtr<Vec4> light1, GuestPtr<Vec4> light2)
    {
        port::GuestFrame<0x60> frame;
        const GuestPtr<Vec4> scratch{frame.ptr().address};
        const ps2f minusOne{std::bit_cast<float>(0xBF800000u)};
        sdk_sceVu0ScaleVector(scratch, light0, minusOne);
        sdk_sceVu0Normalize(row(out, 0), scratch);
        sdk_sceVu0ScaleVector(scratch, light1, minusOne);
        sdk_sceVu0Normalize(row(out, 1), scratch);
        sdk_sceVu0ScaleVector(scratch, light2, minusOne);
        sdk_sceVu0Normalize(row(out, 2), scratch);
        port::store<float>(out.address + 0x30u, 0.0f);
        port::store<float>(out.address + 0x3Cu, 1.0f);
        port::store<float>(out.address + 0x38u, 0.0f);
        port::store<float>(out.address + 0x34u, 0.0f);
        sdk_sceVu0TransposeMatrix(out, out);
    }

    // The three light colours and the ambient colour as the rows of a matrix.
    void sdk_sceVu0LightColorMatrix(GuestPtr<Vec4> out, GuestPtr<Vec4> colour0, GuestPtr<Vec4> colour1, GuestPtr<Vec4> colour2,
                                    GuestPtr<Vec4> ambient)
    {
        sdk_sceVu0CopyVector(row(out, 0), colour0);
        sdk_sceVu0CopyVector(row(out, 1), colour1);
        sdk_sceVu0CopyVector(row(out, 2), colour2);
        sdk_sceVu0CopyVector(row(out, 3), ambient);
    }

    PORT_FN(sdk_sceVu0ApplyMatrix);
    PORT_FN(sdk_sceVu0MulMatrix);
    PORT_FN(sdk_sceVu0OuterProduct);
    PORT_FN(sdk_sceVu0Normalize);
    PORT_FN(sdk_sceVu0TransposeMatrix);
    PORT_FN(sdk_sceVu0InversMatrix);
    PORT_FN(sdk_sceVu0ScaleVector);
    PORT_FN(sdk_sceVu0TransMatrix);
    PORT_FN(sdk_sceVu0CopyVector);
    PORT_FN(sdk_sceVu0UnitMatrix);
    PORT_FN(sdk_sceVu0RotMatrixZ);
    PORT_FN(sdk_sceVu0RotMatrixX);
    PORT_FN(sdk_sceVu0RotMatrixY);
    PORT_FN(sdk_sceVu0CameraMatrix);
    PORT_FN(sdk_sceVu0NormalLightMatrix);
    PORT_FN(sdk_sceVu0LightColorMatrix);
}
