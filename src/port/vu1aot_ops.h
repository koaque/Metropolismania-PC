// Pre-timed VU1 programs: the native ops (plan P4, M3.5), shared by the graph executor (vu1aot_exec.cpp, through the tables
// in vu1aot_ops.cpp) and the compiled programs (vu1aot_emit.cpp writes them). See vu1aot_exec.h.
//
// Each op is VU1Interpreter::execUpper/execLower for one instruction, written for a Ctx instead of the interpreter's
// queues: vf, ACC, VI and store results land at once (the graph holds the timing that makes that exact), flag, Q and P
// results go to the program's slots. Anything not written here goes through the interpreter's own functions. The fuzz
// test in vu1aot_test.cpp (MMPC_VU1_AOT=fuzz) runs random instructions through both and compares everything.
//
// The ops are forced inline: a compiled program calls them with a constant node, so the register numbers, lane masks and
// slots fold to constants, and a flag result no later commit reads is not computed at all.

#pragma once

#include "vu1aot_exec.h"

#include "runtime/ps2_vu1_fmac.h"

#include <cmath>
#include <cstring>
#include <limits>

#if defined(_MSC_VER)
#define VU1AOT_INLINE __forceinline
#define VU1AOT_NOINLINE __declspec(noinline)
#else
#define VU1AOT_INLINE inline __attribute__((always_inline))
#define VU1AOT_NOINLINE __attribute__((noinline))
#endif

namespace vu1aot::ops
{
    VU1AOT_INLINE uint32_t fsOf(uint32_t i) { return (i >> 11) & 0x1Fu; }
    VU1AOT_INLINE uint32_t ftOf(uint32_t i) { return (i >> 16) & 0x1Fu; }
    VU1AOT_INLINE uint32_t viTOf(uint32_t i) { return (i >> 16) & 0xFu; }
    VU1AOT_INLINE uint32_t viSOf(uint32_t i) { return (i >> 11) & 0xFu; }
    VU1AOT_INLINE uint32_t viDOf(uint32_t i) { return (i >> 6) & 0xFu; }
    VU1AOT_INLINE uint32_t destOf(uint32_t i) { return (i >> 21) & 0xFu; }
    VU1AOT_INLINE int imm11(uint32_t i) { return static_cast<int16_t>(static_cast<int32_t>(i << 21) >> 21); }
    // The address of a quadword: (base + offset) * 16 inside the 16 KB data memory.
    VU1AOT_INLINE uint32_t quadAddress(int32_t base, int offset) { return (static_cast<uint32_t>(static_cast<int32_t>(base + offset)) * 16u) & 0x3FF0u; }
    VU1AOT_INLINE uint32_t quadAddressU16(int32_t base) { return (static_cast<uint32_t>(static_cast<uint16_t>(base)) * 16u) & 0x3FF0u; }
    // vu1fmac::reverseLanes, forced inline: in a large compiled program the compiler stops inlining that one and calls it, even
    // for a constant dest.
    VU1AOT_INLINE constexpr uint32_t reverseLanes(uint32_t set)
    {
        return ((set & 1u) << 3) | ((set & 2u) << 1) | ((set & 4u) >> 1) | ((set & 8u) >> 3);
    }

    // A write of the lanes in `dest` (bit 3 = x ... bit 0 = w).
    VU1AOT_INLINE __m128 laneMask(uint32_t dest)
    {
        return _mm_castsi128_ps(vu1fmac::laneTables().mask[reverseLanes(dest)]);
    }
    VU1AOT_INLINE void storeLanes(float *target, __m128 value, uint32_t dest)
    {
        _mm_storeu_ps(target, _mm_blendv_ps(_mm_loadu_ps(target), value, laneMask(dest)));
    }
    VU1AOT_INLINE void storeQuad(Ctx &c, uint32_t address, __m128 value, uint32_t dest)
    {
        if (c.xgkickPending != 0u)
            c.flushXgkick(c);
        if (c.log != nullptr)
            c.log->save(c.data, address);
        float *target = reinterpret_cast<float *>(c.data + address);
        _mm_storeu_ps(target, _mm_blendv_ps(_mm_loadu_ps(target), value, laneMask(dest)));
    }
    VU1AOT_INLINE __m128 loadQuad(const Ctx &c, uint32_t address) { return _mm_loadu_ps(reinterpret_cast<const float *>(c.data + address)); }
    VU1AOT_INLINE uint32_t firstLane(uint32_t dest) { return (dest & 8u) ? 0u : (dest & 4u) ? 1u : (dest & 2u) ? 2u : 3u; }

    // VU1Interpreter::normalizeOperand and normalizeResult.
    VU1AOT_INLINE float normalizeOperand(float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        const uint32_t exponent = (bits >> 23) & 0xFFu;
        if (exponent == 0u)
            bits &= 0x80000000u;
        else if (exponent == 0xFFu)
            bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    VU1AOT_INLINE float normalizeResult(float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        const uint32_t sign = bits & 0x80000000u;
        const uint32_t magnitude = bits & 0x7FFFFFFFu;
        const uint32_t exponent = (bits >> 23) & 0xFFu;
        if (magnitude != 0u && exponent == 0u)
            bits = sign;
        else if (magnitude != 0u && exponent == 0xFFu)
            bits = sign | 0x7F7FFFFFu;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    // ---- between pairs: the results that commit ----

    // The oldest queued flag result lands. `writes` is a FlagWrites set.
    VU1AOT_INLINE void commitFlag(Ctx &c, VU1State &st, uint32_t writes)
    {
        const FlagData &d = c.ring[c.head & (kMaxFlags - 1)];
        ++c.head;
        if ((writes & WritesMac) != 0)
            st.mac = d.mac;
        if ((writes & WritesStatus) != 0)
        {
            const uint32_t current = d.status & 0xFu;
            st.status = (st.status & 0xFF0u) | current | ((current | d.extra) << 6);
        }
        if ((writes & WritesSticky) != 0)
            st.status = (st.status & 0x03Fu) | (d.status & 0xFC0u);
        if ((writes & WritesClip) != 0)
            st.clip = d.clip;
    }
    VU1AOT_INLINE void commitQ(Ctx &c, VU1State &st)
    {
        st.q = c.qValue;
        const uint32_t di = c.qStatusDi & 0x30u;
        st.status = (st.status & 0xFCFu) | di | (di << 6);
    }
    VU1AOT_INLINE void commitP(Ctx &c, VU1State &st, uint32_t slot) { st.p = c.pValue[slot]; }

    // After a pair that ran an interpreter function or wrote vf0 from the upper half: vf0 and vi0 are put back.
    VU1AOT_INLINE void resetZero(VU1State &st)
    {
        st.vf[0][0] = 0.0f;
        st.vf[0][1] = 0.0f;
        st.vf[0][2] = 0.0f;
        st.vf[0][3] = 1.0f;
        st.vi[0] = 0;
    }

    // ---- upper half: the arithmetic family ----

    // The common cases of vu1fmac::arith, where its exact (double) result classifies as an ordinary number or is exactly zero:
    // then the flags are the result's signs and zeros and the result is the float result, which this computes without the double
    // arithmetic. True when it did the operation; false (nothing written) when arith must. Why the tests are enough (the run rounds
    // toward zero, and operands are normalized, so none is a denormal, an infinity or a NaN):
    //  - ADD, SUB, MUL: the float result r is the exact value rounded toward zero on the float grid, the double result d on a
    //    finer one, so |r| <= |d| <= |exact|; FLT_MIN <= |r| < FLT_MAX then puts d at least FLT_MIN and below FLT_MAX, with r's sign.
    //    The exact value is zero exactly when vs = -b (ADD), vs = b (SUB) or a factor is zero (MUL); float and double then both
    //    give that zero with the sign IEEE gives it, which is r's.
    //  - MADD, MSUB: r = acc +- p rounded, with p the float product (FLT_MIN <= |p| < FLT_MAX, so the exact product classifies as
    //    ordinary too, with p's sign), off the exact product by less than ulp(p). |r| >= 2 ulp(p) and >= 2^-125 (exponent tests)
    //    keep the exact sum at least FLT_MIN from zero with r's sign, and |r| < 2^127 keeps it below FLT_MAX.
    //    With a zero factor the product is exactly zero in float and double alike, so r and the exact sum are acc +- 0, which is
    //    acc: ordinary when r is, zero when r is.
    template <int Kind, int BSrc, bool ToAcc>
    VU1AOT_INLINE bool fmacOrdinary(Ctx &c, const XNode &n, float *target)
    {
        VU1State &st = *c.st;
        const uint32_t word = n.upperWord;
        const uint32_t dest = (word >> 21) & 0xFu;
        const uint32_t lanes = reverseLanes(dest);
        const __m128 vs = vu1fmac::normalizeLanes(_mm_loadu_ps(st.vf[(word >> 11) & 0x1Fu]));
        __m128 b;
        if constexpr (BSrc <= 3)
            b = vu1fmac::normalizeLanes(_mm_set1_ps(st.vf[(word >> 16) & 0x1Fu][BSrc]));
        else if constexpr (BSrc == 4)
            b = vu1fmac::normalizeLanes(_mm_set1_ps(st.q));
        else if constexpr (BSrc == 5)
            b = vu1fmac::normalizeLanes(_mm_set1_ps(st.i));
        else
            b = vu1fmac::normalizeLanes(_mm_loadu_ps(st.vf[(word >> 16) & 0x1Fu]));

        const __m128 signMask = _mm_set1_ps(-0.0f);
        const __m128 zero = _mm_setzero_ps();
        const __m128 floatMin = _mm_set1_ps(std::numeric_limits<float>::min());
        const __m128 floatMax = _mm_set1_ps(std::numeric_limits<float>::max());
        __m128 result, product;
        uint32_t ok, zeroResult, zeroProduct = 0u;
        if constexpr (Kind <= 2)
        {
            result = Kind == 0 ? _mm_add_ps(vs, b) : Kind == 1 ? _mm_sub_ps(vs, b) : _mm_mul_ps(vs, b);
            const __m128 magnitude = _mm_andnot_ps(signMask, result);
            const __m128 ordinary = _mm_and_ps(_mm_cmpge_ps(magnitude, floatMin), _mm_cmplt_ps(magnitude, floatMax));
            __m128 exactZero;
            if constexpr (Kind == 0)
                exactZero = _mm_cmpeq_ps(vs, _mm_xor_ps(b, signMask));
            else if constexpr (Kind == 1)
                exactZero = _mm_cmpeq_ps(vs, b);
            else
                exactZero = _mm_or_ps(_mm_cmpeq_ps(vs, zero), _mm_cmpeq_ps(b, zero));
            zeroResult = static_cast<uint32_t>(_mm_movemask_ps(exactZero));
            ok = static_cast<uint32_t>(_mm_movemask_ps(ordinary)) | zeroResult;
        }
        else
        {
            const __m128 acc = vu1fmac::normalizeLanes(_mm_loadu_ps(st.acc));
            product = _mm_mul_ps(vs, b);
            result = Kind == 3 ? _mm_add_ps(acc, product) : _mm_sub_ps(acc, product);
            const __m128 productMagnitude = _mm_andnot_ps(signMask, product);
            const __m128 productOk = _mm_and_ps(_mm_cmpge_ps(productMagnitude, floatMin), _mm_cmplt_ps(productMagnitude, floatMax));
            const __m128i byte = _mm_set1_epi32(0xFF);
            const __m128i productExp = _mm_and_si128(_mm_srli_epi32(_mm_castps_si128(product), 23), byte);
            const __m128i resultExp = _mm_and_si128(_mm_srli_epi32(_mm_castps_si128(result), 23), byte);
            const __m128i resultOk = _mm_and_si128(_mm_and_si128(_mm_cmpgt_epi32(_mm_set1_epi32(254), resultExp),
                                                                 _mm_cmpgt_epi32(resultExp, _mm_sub_epi32(productExp, _mm_set1_epi32(23)))),
                                                   _mm_cmpgt_epi32(resultExp, _mm_set1_epi32(1)));
            // A zero factor: the sum is acc itself, which only has to be ordinary or zero.
            const __m128 productZero = _mm_or_ps(_mm_cmpeq_ps(vs, zero), _mm_cmpeq_ps(b, zero));
            const __m128 resultMagnitude = _mm_andnot_ps(signMask, result);
            const __m128 resultOrdinary = _mm_and_ps(_mm_cmpge_ps(resultMagnitude, floatMin), _mm_cmplt_ps(resultMagnitude, floatMax));
            const __m128 resultZero = _mm_and_ps(productZero, _mm_cmpeq_ps(result, zero));
            zeroProduct = static_cast<uint32_t>(_mm_movemask_ps(productZero));
            zeroResult = static_cast<uint32_t>(_mm_movemask_ps(resultZero));
            ok = static_cast<uint32_t>(_mm_movemask_ps(_mm_or_ps(_mm_and_ps(productOk, _mm_castsi128_ps(resultOk)),
                                                                 _mm_and_ps(productZero, _mm_or_ps(resultOrdinary, resultZero)))));
        }
        if ((ok & lanes) != lanes)
            return false;

        if (dest != 0u)
        {
            const uint32_t negative = static_cast<uint32_t>(_mm_movemask_ps(result)) & lanes;
            const uint32_t zeros = zeroResult & lanes;
            FlagData &d = c.ring[(c.head + n.flagBase) & (kMaxFlags - 1)];
            d.mac = reverseLanes(zeros) | (reverseLanes(negative) << 4);
            d.status = (zeros != 0u ? 1u : 0u) | (negative != 0u ? 2u : 0u);
            if constexpr (Kind <= 2)
                d.extra = 0u;
            else
                d.extra = ((zeroProduct & lanes) != 0u ? 1u : 0u) | ((static_cast<uint32_t>(_mm_movemask_ps(product)) & lanes) != 0u ? 2u : 0u);
        }
        _mm_storeu_ps(target, _mm_blendv_ps(_mm_loadu_ps(target), result, _mm_castsi128_ps(vu1fmac::laneTables().mask[lanes])));
        return true;
    }

    // vu1fmac::arith out of line, for the operations fmacOrdinary leaves to it: one copy per instantiation instead of one per instruction.
    // Inlined after the fast path at all 249 FMAC instructions of the largest program, it took MSVC 5.9 GB and 390 s to compile
    // that one file (2.2 GB and 210 s like this).
    template <int Kind, int BSrc, bool ToAcc>
    VU1AOT_NOINLINE vu1fmac::Flags arithOutOfLine(VU1State &st, float *target, uint32_t word)
    {
        return vu1fmac::arith<Kind, BSrc, ToAcc>(st, target, word);
    }

    template <int Kind, int BSrc, bool ToAcc>
    VU1AOT_INLINE void upperFmac(Ctx &c, const XNode &n)
    {
        VU1State &st = *c.st;
        const uint32_t word = n.upperWord;
        const uint32_t fd = (word >> 6) & 0x1Fu;
        // A result for vf0 lands there like any other (the lower half of the pair sees it, as it does in the interpreter);
        // the node puts vf0 back after the pair.
        float *target = ToAcc ? st.acc : st.vf[fd];
        if (fmacOrdinary<Kind, BSrc, ToAcc>(c, n, target))
            return;
        const vu1fmac::Flags flags = arithOutOfLine<Kind, BSrc, ToAcc>(st, target, word);
        if (((word >> 21) & 0xFu) != 0u)
        {
            FlagData &d = c.ring[(c.head + n.flagBase) & (kMaxFlags - 1)];
            d.mac = flags.mac;
            d.status = flags.status;
            d.extra = flags.extraSticky;
        }
    }

    // ---- upper half: the rest of what this game runs ----

    // MAX and MINI against a broadcast lane of vt (Mode 0), against vt (1) or against I (2); no flag results.
    template <bool Max, int Mode>
    VU1AOT_INLINE void upperMinMax(Ctx &c, const XNode &n)
    {
        VU1State &st = *c.st;
        const uint32_t w = n.upperWord;
        const __m128 vs = vu1fmac::normalizeLanes(_mm_loadu_ps(st.vf[fsOf(w)]));
        __m128 b;
        if constexpr (Mode == 0)
            b = vu1fmac::normalizeLanes(_mm_set1_ps(st.vf[ftOf(w)][w & 3u]));
        else if constexpr (Mode == 1)
            b = vu1fmac::normalizeLanes(_mm_loadu_ps(st.vf[ftOf(w)]));
        else
            b = vu1fmac::normalizeLanes(_mm_set1_ps(st.i));
        // (a > b) ? a : b and (a < b) ? a : b, which are MAXPS and MINPS with the operands in this order.
        const __m128 r = Max ? _mm_max_ps(vs, b) : _mm_min_ps(vs, b);
        storeLanes(st.vf[(w >> 6) & 0x1Fu], r, destOf(w));
    }

    // ITOF: the integer bits of vf[fs] as a float, divided by 2^Shift (exact, so a multiplication does).
    template <int Shift>
    VU1AOT_INLINE void upperItof(Ctx &c, const XNode &n)
    {
        VU1State &st = *c.st;
        const uint32_t w = n.upperWord;
        __m128 r = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i *>(st.vf[fsOf(w)])));
        if constexpr (Shift != 0)
            r = _mm_mul_ps(r, _mm_set1_ps(1.0f / static_cast<float>(1 << Shift)));
        storeLanes(st.vf[ftOf(w)], r, destOf(w));
    }

    VU1AOT_INLINE int32_t floatToInt(float value, float scale)
    {
        const double scaled = static_cast<double>(value) * static_cast<double>(scale);
        if (scaled >= static_cast<double>(std::numeric_limits<int32_t>::max()))
            return std::numeric_limits<int32_t>::max();
        if (scaled <= static_cast<double>(std::numeric_limits<int32_t>::min()))
            return std::numeric_limits<int32_t>::min();
        return static_cast<int32_t>(scaled);
    }
    template <int Shift>
    VU1AOT_INLINE void upperFtoi(Ctx &c, const XNode &n)
    {
        VU1State &st = *c.st;
        const uint32_t w = n.upperWord;
        constexpr float scale = static_cast<float>(1 << Shift);
        float lanes[4];
        std::memcpy(lanes, st.vf[fsOf(w)], sizeof(lanes));
        int32_t out[4];
        for (int lane = 0; lane < 4; ++lane)
            out[lane] = floatToInt(normalizeOperand(lanes[lane]), scale);
        storeLanes(st.vf[ftOf(w)], _mm_loadu_ps(reinterpret_cast<const float *>(out)), destOf(w));
    }

    VU1AOT_INLINE void upperAbs(Ctx &c, const XNode &n)
    {
        VU1State &st = *c.st;
        const uint32_t w = n.upperWord;
        const __m128 vs = vu1fmac::normalizeLanes(_mm_loadu_ps(st.vf[fsOf(w)]));
        storeLanes(st.vf[ftOf(w)], _mm_andnot_ps(_mm_set1_ps(-0.0f), vs), destOf(w));
    }

    VU1AOT_INLINE void upperClip(Ctx &c, const XNode &n)
    {
        const VU1State &st = *c.st;
        const uint32_t w = n.upperWord;
        uint32_t wBits;
        std::memcpy(&wBits, &st.vf[ftOf(w)][3], sizeof(wBits));
        const int32_t limit = (wBits & 0x7F800000u) != 0u ? static_cast<int32_t>(wBits & 0x7FFFFFFFu) : 0x007FFFFF;
        const auto exceeds = [limit](float value, uint32_t signMask) {
            uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            bits ^= signMask;
            int32_t ordered;
            std::memcpy(&ordered, &bits, sizeof(ordered));
            return ordered > limit;
        };
        const float *vs = st.vf[fsOf(w)];
        uint32_t flags = 0u;
        if (exceeds(vs[0], 0x00000000u)) flags |= 0x01u;
        if (exceeds(vs[0], 0x80000000u)) flags |= 0x02u;
        if (exceeds(vs[1], 0x00000000u)) flags |= 0x04u;
        if (exceeds(vs[1], 0x80000000u)) flags |= 0x08u;
        if (exceeds(vs[2], 0x00000000u)) flags |= 0x10u;
        if (exceeds(vs[2], 0x80000000u)) flags |= 0x20u;
        // queueClip: the clip flag register is a six-bit shift register of these results.
        *c.workingClip = ((*c.workingClip << 6) | (flags & 0x3Fu)) & 0xFFFFFFu;
        c.ring[(c.head + n.flagBase) & (kMaxFlags - 1)].clip = *c.workingClip;
    }

    // OPMSUB (to vf[fd]) and OPMULA (to ACC): the cross product halves. The lanes are vs.yzx against vt.zxy; the w lane is
    // zero, though its product still counts for the sticky flags of OPMSUB.
    template <bool Sub>
    VU1AOT_INLINE void upperCross(Ctx &c, const XNode &n)
    {
        VU1State &st = *c.st;
        const uint32_t w = n.upperWord;
        const uint8_t dest = static_cast<uint8_t>(destOf(w));
        const vu1fmac::LaneTables &tables = vu1fmac::laneTables();
        const uint32_t lanes = reverseLanes(dest);
        const __m128 vs = vu1fmac::normalizeLanes(_mm_loadu_ps(st.vf[fsOf(w)]));
        const __m128 vt = vu1fmac::normalizeLanes(_mm_loadu_ps(st.vf[ftOf(w)]));
        const __m128 left = _mm_shuffle_ps(vs, vs, _MM_SHUFFLE(3, 0, 2, 1));
        const __m128 right = _mm_shuffle_ps(vt, vt, _MM_SHUFFLE(3, 1, 0, 2));
        const __m128 product = _mm_mul_ps(left, right);
        const __m256d exactProduct = _mm256_mul_pd(_mm256_cvtps_pd(left), _mm256_cvtps_pd(right));
        __m128 result;
        __m256d exact;
        uint32_t extraSticky = 0u;
        if constexpr (Sub)
        {
            const __m128 acc = vu1fmac::normalizeLanes(_mm_loadu_ps(st.acc));
            result = _mm_sub_ps(acc, product);
            exact = _mm256_sub_pd(_mm256_cvtps_pd(acc), exactProduct);
            __m256d zeroMask, underMask, overMask;
            const vu1fmac::ExactClass p = vu1fmac::classifyExact(exactProduct, zeroMask, underMask, overMask);
            extraSticky = ((p.zero & lanes) != 0u ? 1u : 0u) | ((p.negative & lanes) != 0u ? 2u : 0u) |
                          ((p.underflow & lanes) != 0u ? 4u : 0u) | ((p.overflow & lanes) != 0u ? 8u : 0u);
        }
        else
        {
            result = product;
            exact = exactProduct;
        }
        // The w lane: 0.0 whatever the products are.
        result = _mm_blend_ps(result, _mm_setzero_ps(), 0x8);
        exact = _mm256_blend_pd(exact, _mm256_setzero_pd(), 0x8);
        __m256d zeroMask, underMask, overMask;
        const vu1fmac::ExactClass k = vu1fmac::classifyExact(exact, zeroMask, underMask, overMask);
        const __m128i signBits = _mm_and_si128(tables.mask[k.negative], _mm_set1_epi32(static_cast<int>(0x80000000u)));
        __m128i resultBits = _mm_castps_si128(result);
        resultBits = _mm_blendv_epi8(resultBits, signBits, tables.mask[k.zero]);
        resultBits = _mm_blendv_epi8(resultBits, _mm_or_si128(signBits, _mm_set1_epi32(0x7F7FFFFF)), tables.mask[k.overflow]);
        result = _mm_castsi128_ps(resultBits);
        if (dest != 0u)
        {
            FlagData &d = c.ring[(c.head + n.flagBase) & (kMaxFlags - 1)];
            d.mac = static_cast<uint32_t>(tables.reverse[k.zero & lanes]) | (static_cast<uint32_t>(tables.reverse[k.negative & lanes]) << 4) |
                    (static_cast<uint32_t>(tables.reverse[k.underflow & lanes]) << 8) | (static_cast<uint32_t>(tables.reverse[k.overflow & lanes]) << 12);
            d.status = ((k.zero & lanes) != 0u ? 1u : 0u) | ((k.negative & lanes) != 0u ? 2u : 0u) | ((k.underflow & lanes) != 0u ? 4u : 0u) |
                       ((k.overflow & lanes) != 0u ? 8u : 0u);
            d.extra = extraSticky;
        }
        float *target = Sub ? st.vf[(w >> 6) & 0x1Fu] : st.acc;
        _mm_storeu_ps(target, _mm_blendv_ps(_mm_loadu_ps(target), result, _mm_castsi128_ps(tables.mask[lanes])));
    }

    // ---- lower half ----

    VU1AOT_INLINE void lowerLq(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        const uint32_t it = ftOf(w);
        if (it == 0u)
            return;
        storeLanes(c.st->vf[it], loadQuad(c, quadAddress(c.st->vi[viSOf(w)], imm11(w))), destOf(w));
    }
    VU1AOT_INLINE void lowerSq(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        storeQuad(c, quadAddress(c.st->vi[viTOf(w)], imm11(w)), _mm_loadu_ps(c.st->vf[fsOf(w)]), destOf(w));
    }
    VU1AOT_INLINE void lowerIlw(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        const uint32_t it = viTOf(w);
        if (it == 0u)
            return;
        uint32_t v;
        std::memcpy(&v, c.data + quadAddress(c.st->vi[viSOf(w)], imm11(w)) + firstLane(destOf(w)) * 4u, 4);
        c.st->vi[it] = static_cast<int32_t>(static_cast<int16_t>(v & 0xFFFFu));
    }
    VU1AOT_INLINE void storeWord(Ctx &c, uint32_t address, uint32_t value, uint32_t dest)
    {
        storeQuad(c, address, _mm_castsi128_ps(_mm_set1_epi32(static_cast<int>(value))), dest);
    }
    VU1AOT_INLINE void lowerIsw(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        storeWord(c, quadAddress(c.st->vi[viSOf(w)], imm11(w)), static_cast<uint32_t>(static_cast<uint16_t>(c.st->vi[viTOf(w)] & 0xFFFF)), destOf(w));
    }
    VU1AOT_INLINE int16_t immIadd(uint32_t w) { return static_cast<int16_t>(static_cast<int16_t>(w & 0x7FFu) | ((w >> 10) & 0x7800u)); }
    VU1AOT_INLINE void lowerIaddiu(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        const uint32_t it = viTOf(w);
        if (it != 0u)
            c.st->vi[it] = static_cast<int16_t>(c.st->vi[viSOf(w)] + immIadd(w));
    }
    VU1AOT_INLINE void lowerIsubiu(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        const uint32_t it = viTOf(w);
        if (it != 0u)
            c.st->vi[it] = static_cast<int16_t>(c.st->vi[viSOf(w)] - immIadd(w));
    }
    VU1AOT_INLINE void lowerIadd(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        if (viDOf(w) != 0u)
            c.st->vi[viDOf(w)] = static_cast<int16_t>(c.st->vi[viSOf(w)] + c.st->vi[viTOf(w)]);
    }
    VU1AOT_INLINE void lowerIsub(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        if (viDOf(w) != 0u)
            c.st->vi[viDOf(w)] = static_cast<int16_t>(c.st->vi[viSOf(w)] - c.st->vi[viTOf(w)]);
    }
    VU1AOT_INLINE void lowerIaddi(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        const int16_t imm5 = static_cast<int16_t>(static_cast<int32_t>((w >> 6) & 0x1Fu) << 27 >> 27);
        if (viTOf(w) != 0u)
            c.st->vi[viTOf(w)] = static_cast<int16_t>(c.st->vi[viSOf(w)] + imm5);
    }
    VU1AOT_INLINE void lowerIand(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        if (viDOf(w) != 0u)
            c.st->vi[viDOf(w)] = c.st->vi[viSOf(w)] & c.st->vi[viTOf(w)];
    }
    VU1AOT_INLINE void lowerIor(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        if (viDOf(w) != 0u)
            c.st->vi[viDOf(w)] = c.st->vi[viSOf(w)] | c.st->vi[viTOf(w)];
    }
    VU1AOT_INLINE void lowerMove(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        if (ftOf(w) != 0u)
            storeLanes(c.st->vf[ftOf(w)], _mm_loadu_ps(c.st->vf[fsOf(w)]), destOf(w));
    }
    VU1AOT_INLINE void lowerMr32(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        if (ftOf(w) == 0u)
            return;
        const __m128 v = _mm_loadu_ps(c.st->vf[fsOf(w)]);
        storeLanes(c.st->vf[ftOf(w)], _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 3, 2, 1)), destOf(w));
    }
    VU1AOT_INLINE void lowerLqi(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        const uint32_t is = viSOf(w);
        if (ftOf(w) != 0u)
            storeLanes(c.st->vf[ftOf(w)], loadQuad(c, quadAddressU16(c.st->vi[is])), destOf(w));
        if (is != 0u)
            c.st->vi[is] = static_cast<int16_t>(c.st->vi[is] + 1);
    }
    VU1AOT_INLINE void lowerSqi(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        const uint32_t it = viTOf(w);
        storeQuad(c, quadAddressU16(c.st->vi[it]), _mm_loadu_ps(c.st->vf[fsOf(w)]), destOf(w));
        if (it != 0u)
            c.st->vi[it] = static_cast<int16_t>(c.st->vi[it] + 1);
    }
    VU1AOT_INLINE void lowerLqd(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        const uint32_t is = viSOf(w);
        if (is != 0u)
            c.st->vi[is] = static_cast<int16_t>(c.st->vi[is] - 1);
        if (ftOf(w) != 0u)
            storeLanes(c.st->vf[ftOf(w)], loadQuad(c, quadAddressU16(c.st->vi[is])), destOf(w));
    }
    VU1AOT_INLINE void lowerSqd(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        const uint32_t it = viTOf(w);
        if (it != 0u)
            c.st->vi[it] = static_cast<int16_t>(c.st->vi[it] - 1);
        storeQuad(c, quadAddressU16(c.st->vi[it]), _mm_loadu_ps(c.st->vf[fsOf(w)]), destOf(w));
    }
    VU1AOT_INLINE void lowerMtir(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        if (viTOf(w) == 0u)
            return;
        uint32_t bits;
        std::memcpy(&bits, &c.st->vf[fsOf(w)][(w >> 21) & 3u], 4);
        c.st->vi[viTOf(w)] = static_cast<int32_t>(static_cast<int16_t>(bits & 0xFFFFu));
    }
    VU1AOT_INLINE void lowerMfir(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        if (ftOf(w) == 0u)
            return;
        const int32_t value = static_cast<int32_t>(static_cast<int16_t>(c.st->vi[viSOf(w)] & 0xFFFF));
        storeLanes(c.st->vf[ftOf(w)], _mm_castsi128_ps(_mm_set1_epi32(value)), destOf(w));
    }
    VU1AOT_INLINE void lowerIlwr(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        if (viTOf(w) == 0u)
            return;
        uint32_t v;
        std::memcpy(&v, c.data + quadAddressU16(c.st->vi[viSOf(w)]) + firstLane(destOf(w)) * 4u, 4);
        c.st->vi[viTOf(w)] = static_cast<int32_t>(static_cast<int16_t>(v & 0xFFFFu));
    }
    VU1AOT_INLINE void lowerIswr(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        storeWord(c, quadAddressU16(c.st->vi[viSOf(w)]), static_cast<uint32_t>(static_cast<uint16_t>(c.st->vi[viTOf(w)] & 0xFFFF)), destOf(w));
    }
    VU1AOT_INLINE void lowerMfp(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        if (ftOf(w) != 0u)
            storeLanes(c.st->vf[ftOf(w)], _mm_set1_ps(c.st->p), destOf(w));
    }
    VU1AOT_INLINE void lowerXtop(Ctx &c, const XNode &n)
    {
        if (viTOf(n.lowerWord) != 0u)
            c.st->vi[viTOf(n.lowerWord)] = static_cast<int32_t>(c.st->top & 0x3FFu);
    }
    VU1AOT_INLINE void lowerXitop(Ctx &c, const XNode &n)
    {
        if (viTOf(n.lowerWord) != 0u)
            c.st->vi[viTOf(n.lowerWord)] = static_cast<int32_t>(c.st->itop & 0x3FFu);
    }

    // Flag reads (the committed values).
    VU1AOT_INLINE void lowerFceq(Ctx &c, const XNode &n) { c.st->vi[1] = ((c.st->clip & 0xFFFFFFu) == (n.lowerWord & 0xFFFFFFu)) ? 1 : 0; }
    VU1AOT_INLINE void lowerFcand(Ctx &c, const XNode &n) { c.st->vi[1] = ((c.st->clip & (n.lowerWord & 0xFFFFFFu)) != 0u) ? 1 : 0; }
    VU1AOT_INLINE void lowerFcor(Ctx &c, const XNode &n) { c.st->vi[1] = ((c.st->clip | (n.lowerWord & 0xFFFFFFu)) == 0xFFFFFFu) ? 1 : 0; }
    VU1AOT_INLINE uint16_t imm12(uint32_t w) { return static_cast<uint16_t>((((w >> 21) & 0x1u) << 11) | (w & 0x7FFu)); }
    VU1AOT_INLINE void lowerFseq(Ctx &c, const XNode &n)
    {
        const uint32_t it = viTOf(n.lowerWord);
        if (it != 0u)
            c.st->vi[it] = ((c.st->status & 0xFFFu) == imm12(n.lowerWord)) ? 1 : 0;
    }
    VU1AOT_INLINE void lowerFsand(Ctx &c, const XNode &n)
    {
        const uint32_t it = viTOf(n.lowerWord);
        if (it != 0u)
            c.st->vi[it] = static_cast<int32_t>((c.st->status & 0xFFFu) & imm12(n.lowerWord));
    }
    VU1AOT_INLINE void lowerFsor(Ctx &c, const XNode &n)
    {
        const uint32_t it = viTOf(n.lowerWord);
        if (it != 0u)
            c.st->vi[it] = static_cast<int32_t>((c.st->status & 0xFFFu) | imm12(n.lowerWord));
    }
    VU1AOT_INLINE void lowerFmeq(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        if (viTOf(w) != 0u)
            c.st->vi[viTOf(w)] = ((c.st->mac & 0xFFFFu) == static_cast<uint32_t>(static_cast<uint16_t>(c.st->vi[viSOf(w)]))) ? 1 : 0;
    }
    VU1AOT_INLINE void lowerFmand(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        if (viTOf(w) != 0u)
            c.st->vi[viTOf(w)] = static_cast<int32_t>(c.st->mac & static_cast<uint32_t>(static_cast<uint16_t>(c.st->vi[viSOf(w)])));
    }
    VU1AOT_INLINE void lowerFmor(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        if (viTOf(w) != 0u)
            c.st->vi[viTOf(w)] = static_cast<int32_t>(c.st->mac | static_cast<uint32_t>(static_cast<uint16_t>(c.st->vi[viSOf(w)])));
    }
    VU1AOT_INLINE void lowerFcget(Ctx &c, const XNode &n)
    {
        if (viTOf(n.lowerWord) != 0u)
            c.st->vi[viTOf(n.lowerWord)] = static_cast<int32_t>(c.st->clip & 0x0FFFu);
    }

    // Q: DIV, SQRT, RSQRT. The result waits in the program's slot for its commit.
    VU1AOT_INLINE void lowerDiv(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        const int fsf = static_cast<int>((w >> 21) & 3u), ftf = static_cast<int>((w >> 23) & 3u);
        const float num = normalizeOperand(c.st->vf[fsOf(w)][fsf]);
        const float den = normalizeOperand(c.st->vf[ftOf(w)][ftf]);
        uint32_t statusDi = 0u;
        float result;
        if (den == 0.0f)
        {
            statusDi = num == 0.0f ? 0x10u : 0x20u;
            result = std::signbit(num) != std::signbit(den) ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max();
        }
        else
            result = num / den;
        c.qValue = normalizeResult(result);
        c.qStatusDi = statusDi & 0x30u;
    }
    VU1AOT_INLINE void lowerSqrt(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        const int ftf = static_cast<int>((w >> 23) & 3u);
        const float val = normalizeOperand(c.st->vf[ftOf(w)][ftf]);
        c.qValue = normalizeResult(std::sqrt(std::fabs(val)));
        c.qStatusDi = (val < 0.0f ? 0x10u : 0u) & 0x30u;
    }
    VU1AOT_INLINE void lowerRsqrt(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        const int fsf = static_cast<int>((w >> 21) & 3u), ftf = static_cast<int>((w >> 23) & 3u);
        const float num = normalizeOperand(c.st->vf[fsOf(w)][fsf]);
        const float radicand = normalizeOperand(c.st->vf[ftOf(w)][ftf]);
        const float den = std::sqrt(std::fabs(radicand));
        uint32_t statusDi = radicand < 0.0f ? 0x10u : 0u;
        float result;
        if (den != 0.0f)
            result = num / den;
        else
        {
            statusDi = num == 0.0f ? 0x10u : 0x20u;
            result = std::signbit(num) ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max();
        }
        c.qValue = normalizeResult(result);
        c.qStatusDi = statusDi & 0x30u;
    }

    // P: the two EFU ops this game uses.
    VU1AOT_INLINE void lowerEsadd(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        const float x = normalizeOperand(c.st->vf[fsOf(w)][0]);
        const float y = normalizeOperand(c.st->vf[fsOf(w)][1]);
        const float z = normalizeOperand(c.st->vf[fsOf(w)][2]);
        c.pValue[n.efuSlot] = normalizeResult(x * x + y * y + z * z);
    }
    VU1AOT_INLINE void lowerErsqrt(Ctx &c, const XNode &n)
    {
        const uint32_t w = n.lowerWord;
        const float value = normalizeOperand(c.st->vf[fsOf(w)][(w >> 21) & 3u]);
        float result = value;
        if (result >= 0.0f)
        {
            result = std::sqrt(result);
            if (result != 0.0f)
                result = 1.0f / result;
        }
        c.pValue[n.efuSlot] = normalizeResult(result);
    }

    // Branches: the delay slot node picks the way from `taken`.
    VU1AOT_INLINE int32_t branchVi(const Ctx &c, const XNode &n, uint32_t reg)
    {
        if (reg == 0u)
            return 0;
        return reg == n.readBackupReg ? c.backupValue : c.st->vi[reg];
    }
    VU1AOT_INLINE void lowerB(Ctx &c, const XNode &) { c.taken = true; }
    VU1AOT_INLINE void lowerBal(Ctx &c, const XNode &n)
    {
        const uint32_t it = viTOf(n.lowerWord);
        if (it != 0u)
            c.st->vi[it] = static_cast<int32_t>((n.pc + 16u) / 8u);
        c.taken = true;
    }
    VU1AOT_INLINE void lowerIbeq(Ctx &c, const XNode &n)
    {
        c.taken = static_cast<int16_t>(branchVi(c, n, viSOf(n.lowerWord))) == static_cast<int16_t>(branchVi(c, n, viTOf(n.lowerWord)));
    }
    VU1AOT_INLINE void lowerIbne(Ctx &c, const XNode &n)
    {
        c.taken = static_cast<int16_t>(branchVi(c, n, viSOf(n.lowerWord))) != static_cast<int16_t>(branchVi(c, n, viTOf(n.lowerWord)));
    }
    VU1AOT_INLINE void lowerIbltz(Ctx &c, const XNode &n) { c.taken = static_cast<int16_t>(branchVi(c, n, viSOf(n.lowerWord))) < 0; }
    VU1AOT_INLINE void lowerIbgtz(Ctx &c, const XNode &n) { c.taken = static_cast<int16_t>(branchVi(c, n, viSOf(n.lowerWord))) > 0; }
    VU1AOT_INLINE void lowerIblez(Ctx &c, const XNode &n) { c.taken = static_cast<int16_t>(branchVi(c, n, viSOf(n.lowerWord))) <= 0; }
    VU1AOT_INLINE void lowerIbgez(Ctx &c, const XNode &n) { c.taken = static_cast<int16_t>(branchVi(c, n, viSOf(n.lowerWord))) >= 0; }
}
