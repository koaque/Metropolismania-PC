#pragma once

// The vector arithmetic of the native VU1 programs (render_vu1_*.cpp), a lane at a time in the order the VU's MULA / MADDA
// chains add, so each lane's result is the scalar one bit for bit (under the programs' rounding toward zero). On x64 the
// four lanes go through SSE together (separate multiplies and adds, never fused); elsewhere a lane at a time.

#include <algorithm>
#include <cstdint>
#include <cstring>

#if defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#define MM_VU_SSE 1
#else
#define MM_VU_SSE 0
#endif

namespace mm::render::vumath
{
    struct Vec4
    {
        float x = 0, y = 0, z = 0, w = 0;
        float &operator[](int i) { return (&x)[i]; }
        float operator[](int i) const { return (&x)[i]; }
        Vec4 operator-(const Vec4 &o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    };

#if MM_VU_SSE
    inline __m128 get(const Vec4 &v) { return _mm_loadu_ps(&v.x); }
    inline Vec4 put(__m128 r)
    {
        Vec4 o;
        _mm_storeu_ps(&o.x, r);
        return o;
    }
#endif

    // The VU's ITOF12 and ITOF0 of a qword's four integer lanes: with 12 fraction bits, or none.
    inline Vec4 fixed12(const uint32_t *lanes)
    {
#if MM_VU_SSE
        const __m128 f = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i *>(lanes)));
        return put(_mm_mul_ps(f, _mm_set1_ps(1.0f / 4096.0f)));
#else
        const float k = 1.0f / 4096.0f;
        return {static_cast<int32_t>(lanes[0]) * k, static_cast<int32_t>(lanes[1]) * k, static_cast<int32_t>(lanes[2]) * k,
                static_cast<int32_t>(lanes[3]) * k};
#endif
    }

    inline Vec4 integers(const uint32_t *lanes)
    {
#if MM_VU_SSE
        return put(_mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i *>(lanes))));
#else
        return {static_cast<float>(static_cast<int32_t>(lanes[0])), static_cast<float>(static_cast<int32_t>(lanes[1])),
                static_cast<float>(static_cast<int32_t>(lanes[2])), static_cast<float>(static_cast<int32_t>(lanes[3]))};
#endif
    }

    // v.x * m[0] + v.y * m[1] + v.z * m[2] + v.w * m[3], in the order the VU's MULA / MADDA chain adds them.
    inline Vec4 transform(const Vec4 &v, const Vec4 m[4])
    {
#if MM_VU_SSE
        __m128 r = _mm_add_ps(_mm_mul_ps(get(m[0]), _mm_set1_ps(v.x)), _mm_mul_ps(get(m[1]), _mm_set1_ps(v.y)));
        r = _mm_add_ps(r, _mm_mul_ps(get(m[2]), _mm_set1_ps(v.z)));
        return put(_mm_add_ps(r, _mm_mul_ps(get(m[3]), _mm_set1_ps(v.w))));
#else
        Vec4 r;
        for (int i = 0; i < 4; ++i)
            r[i] = ((m[0][i] * v.x + m[1][i] * v.y) + m[2][i] * v.z) + m[3][i] * v.w;
        return r;
#endif
    }

    inline Vec4 transform3(const Vec4 &v, const Vec4 m[3])
    {
#if MM_VU_SSE
        const __m128 r = _mm_add_ps(_mm_mul_ps(get(m[0]), _mm_set1_ps(v.x)), _mm_mul_ps(get(m[1]), _mm_set1_ps(v.y)));
        return put(_mm_add_ps(r, _mm_mul_ps(get(m[2]), _mm_set1_ps(v.z))));
#else
        Vec4 r;
        for (int i = 0; i < 4; ++i)
            r[i] = (m[0][i] * v.x + m[1][i] * v.y) + m[2][i] * v.z;
        return r;
#endif
    }

    // Each lane at least 0 as std::max(lane, 0.0f) has it (NaN and -0 kept).
    inline Vec4 atLeastZero(const Vec4 &v)
    {
#if MM_VU_SSE
        return put(_mm_max_ps(_mm_setzero_ps(), get(v)));
#else
        Vec4 r;
        for (int i = 0; i < 4; ++i)
            r[i] = std::max(v[i], 0.0f);
        return r;
#endif
    }

    // ((c[0] * t.x + c[1] * t.y) + c[2] * t.z) + c[3]: three light colours by their terms, plus the ambient.
    inline Vec4 lightSum(const Vec4 &t, const Vec4 c[4])
    {
#if MM_VU_SSE
        __m128 r = _mm_add_ps(_mm_mul_ps(get(c[0]), _mm_set1_ps(t.x)), _mm_mul_ps(get(c[1]), _mm_set1_ps(t.y)));
        r = _mm_add_ps(r, _mm_mul_ps(get(c[2]), _mm_set1_ps(t.z)));
        return put(_mm_add_ps(r, get(c[3])));
#else
        Vec4 r;
        for (int i = 0; i < 4; ++i)
            r[i] = ((c[0][i] * t.x + c[1][i] * t.y) + c[2][i] * t.z) + c[3][i];
        return r;
#endif
    }
}
