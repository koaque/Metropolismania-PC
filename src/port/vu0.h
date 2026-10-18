// VU0 macro mode for ported code: the registers of the running guest context and the runtime's own vector
// operations, so ported VU0 code computes exactly what the recompiled code computes and leaves the registers
// the same way. That matters: some game code reads lanes it never wrote (see README.md, "VU0 registers").
//
// Each helper mirrors one instruction form: the operation runs on all four lanes, then only the destination
// lanes are written, as in the generated code. The MAC and status flags are not kept (nothing reads them
// across functions).

#pragma once

#include "abi.h"

#include <algorithm>
#include <cmath>
#include <immintrin.h>

namespace mm::vu0
{
    // Destination lanes, as in an instruction's .xyzw field.
    enum Lanes : uint32_t
    {
        X = 1,
        Y = 2,
        Z = 4,
        W = 8,
        XYZ = X | Y | Z,
        XYZW = X | Y | Z | W,
    };

    inline R5900Context &context() { return *port::g_call.ctx; }
    inline __m128 &vf(int index) { return context().vu0_vf[index]; }
    inline __m128 &acc() { return context().vu0_acc; }
    inline float &q() { return context().vu0_q; }

    // dest.lanes = value.lanes; the other lanes keep their value.
    inline void set(__m128 &dest, __m128 value, uint32_t lanes)
    {
        const __m128 mask = _mm_castsi128_ps(_mm_set_epi32((lanes & W) ? -1 : 0, (lanes & Z) ? -1 : 0, (lanes & Y) ? -1 : 0,
                                                           (lanes & X) ? -1 : 0));
        dest = _mm_blendv_ps(dest, value, mask);
    }

    // One lane in all four (the bc field of vaddx, vmulay ...): 0 = x, 1 = y, 2 = z, 3 = w.
    inline __m128 lane(__m128 v, int index)
    {
        switch (index)
        {
        case 0: return _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 0, 0, 0));
        case 1: return _mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 1, 1, 1));
        case 2: return _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 2, 2, 2));
        default: return _mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 3, 3, 3));
        }
    }

    inline __m128 add(__m128 a, __m128 b) { return ps2_vadd(a, b); }
    inline __m128 sub(__m128 a, __m128 b) { return ps2_vsub(a, b); }
    inline __m128 mul(__m128 a, __m128 b) { return ps2_vmul(a, b); }

    // vopmula / vopmsub operand orders (the cross product): (y, z, x) and (z, x, y).
    inline __m128 yzx(__m128 v) { return _mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 0, 2, 1)); }
    inline __m128 zxy(__m128 v) { return _mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 1, 0, 2)); }

    // Q results as the runtime evaluates them: vsqrt, vrsqrt (fs = vf0w = 1), vdiv.
    inline float sqrtQ(float ft) { return sqrtf(std::max(0.0f, ft)); }
    inline float rsqrt(float ft) { return ft > 0.0f ? 1.0f / sqrtf(ft) : 0.0f; }
    inline float divQ(float fs, float ft) { return ft != 0.0f ? fs / ft : 0.0f; }

    // vitof0 and vftoi4 (4 fraction bits), lane by lane.
    inline __m128 itof0(__m128 v) { return _mm_mul_ps(_mm_cvtepi32_ps(_mm_castps_si128(v)), _mm_set1_ps(1.0f)); }
    inline __m128 ftoi4(__m128 v) { return _mm_castsi128_ps(_mm_cvttps_epi32(_mm_mul_ps(v, _mm_set1_ps(16.0f)))); }

    inline float x(__m128 v) { return _mm_cvtss_f32(v); }

    // lqc2 / sqc2.
    inline __m128 load(GuestPtr<Vec4> p)
    {
        __m128 v;
        std::memcpy(&v, p.get(), sizeof(v));
        return v;
    }
    inline void store(GuestPtr<Vec4> p, __m128 v) { std::memcpy(p.get(), &v, sizeof(v)); }

    // A point through a matrix as the game does it in macro mode: the four rows in vf1 to vf4, the point in vf5,
    // the sum of the first three products in the accumulator and the result (plus row 4 times w) in vf9.
    inline void applyMatrix(GuestPtr<Vec4> matrix, GuestPtr<Vec4> point, GuestPtr<Vec4> out)
    {
        vf(1) = load(matrix);
        vf(2) = load(GuestPtr<Vec4>{matrix.address + 0x10u});
        vf(3) = load(GuestPtr<Vec4>{matrix.address + 0x20u});
        vf(4) = load(GuestPtr<Vec4>{matrix.address + 0x30u});
        vf(5) = load(point);
        set(acc(), mul(vf(1), lane(vf(5), 0)), XYZW);
        set(acc(), add(acc(), mul(vf(2), lane(vf(5), 1))), XYZW);
        set(acc(), add(acc(), mul(vf(3), lane(vf(5), 2))), XYZW);
        set(vf(9), add(acc(), mul(vf(4), lane(vf(5), 3))), XYZW);
        store(out, vf(9));
    }
}
