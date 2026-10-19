// Pre-timed VU1 programs: the native ops (plan P4, M3.5). See vu1aot_exec.h.
//
// Each op is VU1Interpreter::execUpper/execLower for one instruction, written for a Ctx instead of the interpreter's
// queues: vf, ACC, VI and store results land at once (the graph holds the timing that makes that exact), flag, Q and P
// results go to the program's slots. Anything not written here goes through the interpreter's own functions. The fuzz
// test in vu1aot_test.cpp (MMPC_VU1_AOT=fuzz) runs random instructions through both and compares everything.

#include "vu1aot_exec.h"

#include "runtime/ps2_vu1_fmac.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace vu1aot
{
    namespace
    {
        inline uint32_t fsOf(uint32_t i) { return (i >> 11) & 0x1Fu; }
        inline uint32_t ftOf(uint32_t i) { return (i >> 16) & 0x1Fu; }
        inline uint32_t viTOf(uint32_t i) { return (i >> 16) & 0xFu; }
        inline uint32_t viSOf(uint32_t i) { return (i >> 11) & 0xFu; }
        inline uint32_t viDOf(uint32_t i) { return (i >> 6) & 0xFu; }
        inline uint32_t destOf(uint32_t i) { return (i >> 21) & 0xFu; }
        inline int imm11(uint32_t i) { return static_cast<int16_t>(static_cast<int32_t>(i << 21) >> 21); }
        // The address of a quadword: (base + offset) * 16 inside the 16 KB data memory.
        inline uint32_t quadAddress(int32_t base, int offset) { return (static_cast<uint32_t>(static_cast<int32_t>(base + offset)) * 16u) & 0x3FF0u; }
        inline uint32_t quadAddressU16(int32_t base) { return (static_cast<uint32_t>(static_cast<uint16_t>(base)) * 16u) & 0x3FF0u; }

        // A write of the lanes in `dest` (bit 3 = x ... bit 0 = w).
        inline __m128 laneMask(uint32_t dest)
        {
            const vu1fmac::LaneTables &tables = vu1fmac::laneTables();
            return _mm_castsi128_ps(tables.mask[tables.reverse[dest]]);
        }
        inline void storeLanes(float *target, __m128 value, uint32_t dest)
        {
            _mm_storeu_ps(target, _mm_blendv_ps(_mm_loadu_ps(target), value, laneMask(dest)));
        }
        inline void storeQuad(Ctx &c, uint32_t address, __m128 value, uint32_t dest)
        {
            float *target = reinterpret_cast<float *>(c.data + address);
            _mm_storeu_ps(target, _mm_blendv_ps(_mm_loadu_ps(target), value, laneMask(dest)));
        }
        inline __m128 loadQuad(const Ctx &c, uint32_t address) { return _mm_loadu_ps(reinterpret_cast<const float *>(c.data + address)); }
        inline uint32_t firstLane(uint32_t dest) { return (dest & 8u) ? 0u : (dest & 4u) ? 1u : (dest & 2u) ? 2u : 3u; }

        // VU1Interpreter::normalizeOperand and normalizeResult.
        inline float normalizeOperand(float value)
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
        inline float normalizeResult(float value)
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

        // ---- upper half: the arithmetic family ----

        template <int Kind, int BSrc, bool ToAcc>
        void upperFmac(Ctx &c, const XNode &n)
        {
            VU1State &st = *c.st;
            const uint32_t word = n.upperWord;
            const uint32_t fd = (word >> 6) & 0x1Fu;
            // A result for vf0 lands there like any other (the lower half of the pair sees it, as it does in the interpreter);
            // the node puts vf0 back after the pair.
            float *target = ToAcc ? st.acc : st.vf[fd];
            const vu1fmac::Flags flags = vu1fmac::arith<Kind, BSrc, ToAcc>(st, target, word);
            if (((word >> 21) & 0xFu) != 0u)
            {
                FlagData &d = c.ring[(c.head + n.flagBase) & (kMaxFlags - 1)];
                d.mac = flags.mac;
                d.status = flags.status;
                d.extra = flags.extraSticky;
            }
        }

        struct UpperTable
        {
            OpFn main[0x3C]{};
            OpFn special[0x30]{};
            UpperTable()
            {
                const auto build = [&](bool special) {
                    const auto put = [&](uint32_t op, OpFn fn) { (special ? this->special[op] : this->main[op]) = fn; };
#define VU1AOT_BC(OPBASE, KIND)                                                                                              \
    put((OPBASE) + 0, special ? &upperFmac<KIND, 0, true> : &upperFmac<KIND, 0, false>);                                      \
    put((OPBASE) + 1, special ? &upperFmac<KIND, 1, true> : &upperFmac<KIND, 1, false>);                                      \
    put((OPBASE) + 2, special ? &upperFmac<KIND, 2, true> : &upperFmac<KIND, 2, false>);                                      \
    put((OPBASE) + 3, special ? &upperFmac<KIND, 3, true> : &upperFmac<KIND, 3, false>)
#define VU1AOT_ONE(OP, KIND, BSRC) put((OP), special ? &upperFmac<KIND, BSRC, true> : &upperFmac<KIND, BSRC, false>)
                    VU1AOT_BC(0x00, 0);
                    VU1AOT_BC(0x04, 1);
                    VU1AOT_BC(0x08, 3);
                    VU1AOT_BC(0x0C, 4);
                    VU1AOT_BC(0x18, 2);
                    VU1AOT_ONE(0x1C, 2, 4); // MULq
                    VU1AOT_ONE(0x1E, 2, 5); // MULi
                    VU1AOT_ONE(0x20, 0, 4); // ADDq
                    VU1AOT_ONE(0x21, 3, 4); // MADDq
                    VU1AOT_ONE(0x22, 0, 5); // ADDi
                    VU1AOT_ONE(0x23, 3, 5); // MADDi
                    VU1AOT_ONE(0x24, 1, 4); // SUBq
                    VU1AOT_ONE(0x25, 4, 4); // MSUBq
                    VU1AOT_ONE(0x26, 1, 5); // SUBi
                    VU1AOT_ONE(0x27, 4, 5); // MSUBi
                    VU1AOT_ONE(0x28, 0, 6); // ADD
                    VU1AOT_ONE(0x29, 3, 6); // MADD
                    VU1AOT_ONE(0x2A, 2, 6); // MUL
                    VU1AOT_ONE(0x2C, 1, 6); // SUB
                    VU1AOT_ONE(0x2D, 4, 6); // MSUB
#undef VU1AOT_BC
#undef VU1AOT_ONE
                };
                build(false);
                build(true);
            }
        };
        const UpperTable g_upper;

        // ---- upper half: the rest of what this game runs ----

        // MAX and MINI against a broadcast lane of vt (Mode 0), against vt (1) or against I (2); no flag results.
        template <bool Max, int Mode>
        void upperMinMax(Ctx &c, const XNode &n)
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
        void upperItof(Ctx &c, const XNode &n)
        {
            VU1State &st = *c.st;
            const uint32_t w = n.upperWord;
            __m128 r = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i *>(st.vf[fsOf(w)])));
            if constexpr (Shift != 0)
                r = _mm_mul_ps(r, _mm_set1_ps(1.0f / static_cast<float>(1 << Shift)));
            storeLanes(st.vf[ftOf(w)], r, destOf(w));
        }

        inline int32_t floatToInt(float value, float scale)
        {
            const double scaled = static_cast<double>(value) * static_cast<double>(scale);
            if (scaled >= static_cast<double>(std::numeric_limits<int32_t>::max()))
                return std::numeric_limits<int32_t>::max();
            if (scaled <= static_cast<double>(std::numeric_limits<int32_t>::min()))
                return std::numeric_limits<int32_t>::min();
            return static_cast<int32_t>(scaled);
        }
        template <int Shift>
        void upperFtoi(Ctx &c, const XNode &n)
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

        void upperAbs(Ctx &c, const XNode &n)
        {
            VU1State &st = *c.st;
            const uint32_t w = n.upperWord;
            const __m128 vs = vu1fmac::normalizeLanes(_mm_loadu_ps(st.vf[fsOf(w)]));
            storeLanes(st.vf[ftOf(w)], _mm_andnot_ps(_mm_set1_ps(-0.0f), vs), destOf(w));
        }

        void upperClip(Ctx &c, const XNode &n)
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
        void upperCross(Ctx &c, const XNode &n)
        {
            VU1State &st = *c.st;
            const uint32_t w = n.upperWord;
            const uint8_t dest = static_cast<uint8_t>(destOf(w));
            const vu1fmac::LaneTables &tables = vu1fmac::laneTables();
            const uint32_t lanes = tables.reverse[dest];
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

        OpFn nativeUpperOther(uint32_t op, uint32_t special)
        {
            if (op < 0x3Cu)
            {
                if (op >= 0x10u && op <= 0x13u) return &upperMinMax<true, 0>;
                if (op >= 0x14u && op <= 0x17u) return &upperMinMax<false, 0>;
                switch (op)
                {
                case 0x1D: return &upperMinMax<true, 2>;
                case 0x1F: return &upperMinMax<false, 2>;
                case 0x2B: return &upperMinMax<true, 1>;
                case 0x2F: return &upperMinMax<false, 1>;
                case 0x2E: return &upperCross<true>;
                default: return nullptr;
                }
            }
            switch (special)
            {
            case 0x10: return &upperItof<0>;
            case 0x11: return &upperItof<4>;
            case 0x12: return &upperItof<12>;
            case 0x13: return &upperItof<15>;
            case 0x14: return &upperFtoi<0>;
            case 0x15: return &upperFtoi<4>;
            case 0x16: return &upperFtoi<12>;
            case 0x17: return &upperFtoi<15>;
            case 0x1D: return &upperAbs;
            case 0x1F: return &upperClip;
            case 0x2E: return &upperCross<false>;
            default: return nullptr;
            }
        }

        // ---- lower half ----

        void lowerLq(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            const uint32_t it = ftOf(w);
            if (it == 0u)
                return;
            storeLanes(c.st->vf[it], loadQuad(c, quadAddress(c.st->vi[viSOf(w)], imm11(w))), destOf(w));
        }
        void lowerSq(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            storeQuad(c, quadAddress(c.st->vi[viTOf(w)], imm11(w)), _mm_loadu_ps(c.st->vf[fsOf(w)]), destOf(w));
        }
        void lowerIlw(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            const uint32_t it = viTOf(w);
            if (it == 0u)
                return;
            uint32_t v;
            std::memcpy(&v, c.data + quadAddress(c.st->vi[viSOf(w)], imm11(w)) + firstLane(destOf(w)) * 4u, 4);
            c.st->vi[it] = static_cast<int32_t>(static_cast<int16_t>(v & 0xFFFFu));
        }
        void storeWord(Ctx &c, uint32_t address, uint32_t value, uint32_t dest)
        {
            storeQuad(c, address, _mm_castsi128_ps(_mm_set1_epi32(static_cast<int>(value))), dest);
        }
        void lowerIsw(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            storeWord(c, quadAddress(c.st->vi[viSOf(w)], imm11(w)), static_cast<uint32_t>(static_cast<uint16_t>(c.st->vi[viTOf(w)] & 0xFFFF)), destOf(w));
        }
        inline int16_t immIadd(uint32_t w) { return static_cast<int16_t>(static_cast<int16_t>(w & 0x7FFu) | ((w >> 10) & 0x7800u)); }
        void lowerIaddiu(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            const uint32_t it = viTOf(w);
            if (it != 0u)
                c.st->vi[it] = static_cast<int16_t>(c.st->vi[viSOf(w)] + immIadd(w));
        }
        void lowerIsubiu(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            const uint32_t it = viTOf(w);
            if (it != 0u)
                c.st->vi[it] = static_cast<int16_t>(c.st->vi[viSOf(w)] - immIadd(w));
        }
        void lowerIadd(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            if (viDOf(w) != 0u)
                c.st->vi[viDOf(w)] = static_cast<int16_t>(c.st->vi[viSOf(w)] + c.st->vi[viTOf(w)]);
        }
        void lowerIsub(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            if (viDOf(w) != 0u)
                c.st->vi[viDOf(w)] = static_cast<int16_t>(c.st->vi[viSOf(w)] - c.st->vi[viTOf(w)]);
        }
        void lowerIaddi(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            const int16_t imm5 = static_cast<int16_t>(static_cast<int32_t>((w >> 6) & 0x1Fu) << 27 >> 27);
            if (viTOf(w) != 0u)
                c.st->vi[viTOf(w)] = static_cast<int16_t>(c.st->vi[viSOf(w)] + imm5);
        }
        void lowerIand(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            if (viDOf(w) != 0u)
                c.st->vi[viDOf(w)] = c.st->vi[viSOf(w)] & c.st->vi[viTOf(w)];
        }
        void lowerIor(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            if (viDOf(w) != 0u)
                c.st->vi[viDOf(w)] = c.st->vi[viSOf(w)] | c.st->vi[viTOf(w)];
        }
        void lowerMove(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            if (ftOf(w) != 0u)
                storeLanes(c.st->vf[ftOf(w)], _mm_loadu_ps(c.st->vf[fsOf(w)]), destOf(w));
        }
        void lowerMr32(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            if (ftOf(w) == 0u)
                return;
            const __m128 v = _mm_loadu_ps(c.st->vf[fsOf(w)]);
            storeLanes(c.st->vf[ftOf(w)], _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 3, 2, 1)), destOf(w));
        }
        void lowerLqi(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            const uint32_t is = viSOf(w);
            if (ftOf(w) != 0u)
                storeLanes(c.st->vf[ftOf(w)], loadQuad(c, quadAddressU16(c.st->vi[is])), destOf(w));
            if (is != 0u)
                c.st->vi[is] = static_cast<int16_t>(c.st->vi[is] + 1);
        }
        void lowerSqi(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            const uint32_t it = viTOf(w);
            storeQuad(c, quadAddressU16(c.st->vi[it]), _mm_loadu_ps(c.st->vf[fsOf(w)]), destOf(w));
            if (it != 0u)
                c.st->vi[it] = static_cast<int16_t>(c.st->vi[it] + 1);
        }
        void lowerLqd(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            const uint32_t is = viSOf(w);
            if (is != 0u)
                c.st->vi[is] = static_cast<int16_t>(c.st->vi[is] - 1);
            if (ftOf(w) != 0u)
                storeLanes(c.st->vf[ftOf(w)], loadQuad(c, quadAddressU16(c.st->vi[is])), destOf(w));
        }
        void lowerSqd(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            const uint32_t it = viTOf(w);
            if (it != 0u)
                c.st->vi[it] = static_cast<int16_t>(c.st->vi[it] - 1);
            storeQuad(c, quadAddressU16(c.st->vi[it]), _mm_loadu_ps(c.st->vf[fsOf(w)]), destOf(w));
        }
        void lowerMtir(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            if (viTOf(w) == 0u)
                return;
            uint32_t bits;
            std::memcpy(&bits, &c.st->vf[fsOf(w)][(w >> 21) & 3u], 4);
            c.st->vi[viTOf(w)] = static_cast<int32_t>(static_cast<int16_t>(bits & 0xFFFFu));
        }
        void lowerMfir(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            if (ftOf(w) == 0u)
                return;
            const int32_t value = static_cast<int32_t>(static_cast<int16_t>(c.st->vi[viSOf(w)] & 0xFFFF));
            storeLanes(c.st->vf[ftOf(w)], _mm_castsi128_ps(_mm_set1_epi32(value)), destOf(w));
        }
        void lowerIlwr(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            if (viTOf(w) == 0u)
                return;
            uint32_t v;
            std::memcpy(&v, c.data + quadAddressU16(c.st->vi[viSOf(w)]) + firstLane(destOf(w)) * 4u, 4);
            c.st->vi[viTOf(w)] = static_cast<int32_t>(static_cast<int16_t>(v & 0xFFFFu));
        }
        void lowerIswr(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            storeWord(c, quadAddressU16(c.st->vi[viSOf(w)]), static_cast<uint32_t>(static_cast<uint16_t>(c.st->vi[viTOf(w)] & 0xFFFF)), destOf(w));
        }
        void lowerMfp(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            if (ftOf(w) != 0u)
                storeLanes(c.st->vf[ftOf(w)], _mm_set1_ps(c.st->p), destOf(w));
        }
        void lowerXtop(Ctx &c, const XNode &n)
        {
            if (viTOf(n.lowerWord) != 0u)
                c.st->vi[viTOf(n.lowerWord)] = static_cast<int32_t>(c.st->top & 0x3FFu);
        }
        void lowerXitop(Ctx &c, const XNode &n)
        {
            if (viTOf(n.lowerWord) != 0u)
                c.st->vi[viTOf(n.lowerWord)] = static_cast<int32_t>(c.st->itop & 0x3FFu);
        }

        // Flag reads (the committed values).
        void lowerFceq(Ctx &c, const XNode &n) { c.st->vi[1] = ((c.st->clip & 0xFFFFFFu) == (n.lowerWord & 0xFFFFFFu)) ? 1 : 0; }
        void lowerFcand(Ctx &c, const XNode &n) { c.st->vi[1] = ((c.st->clip & (n.lowerWord & 0xFFFFFFu)) != 0u) ? 1 : 0; }
        void lowerFcor(Ctx &c, const XNode &n) { c.st->vi[1] = ((c.st->clip | (n.lowerWord & 0xFFFFFFu)) == 0xFFFFFFu) ? 1 : 0; }
        inline uint16_t imm12(uint32_t w) { return static_cast<uint16_t>((((w >> 21) & 0x1u) << 11) | (w & 0x7FFu)); }
        void lowerFseq(Ctx &c, const XNode &n)
        {
            const uint32_t it = viTOf(n.lowerWord);
            if (it != 0u)
                c.st->vi[it] = ((c.st->status & 0xFFFu) == imm12(n.lowerWord)) ? 1 : 0;
        }
        void lowerFsand(Ctx &c, const XNode &n)
        {
            const uint32_t it = viTOf(n.lowerWord);
            if (it != 0u)
                c.st->vi[it] = static_cast<int32_t>((c.st->status & 0xFFFu) & imm12(n.lowerWord));
        }
        void lowerFsor(Ctx &c, const XNode &n)
        {
            const uint32_t it = viTOf(n.lowerWord);
            if (it != 0u)
                c.st->vi[it] = static_cast<int32_t>((c.st->status & 0xFFFu) | imm12(n.lowerWord));
        }
        void lowerFmeq(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            if (viTOf(w) != 0u)
                c.st->vi[viTOf(w)] = ((c.st->mac & 0xFFFFu) == static_cast<uint32_t>(static_cast<uint16_t>(c.st->vi[viSOf(w)]))) ? 1 : 0;
        }
        void lowerFmand(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            if (viTOf(w) != 0u)
                c.st->vi[viTOf(w)] = static_cast<int32_t>(c.st->mac & static_cast<uint32_t>(static_cast<uint16_t>(c.st->vi[viSOf(w)])));
        }
        void lowerFmor(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            if (viTOf(w) != 0u)
                c.st->vi[viTOf(w)] = static_cast<int32_t>(c.st->mac | static_cast<uint32_t>(static_cast<uint16_t>(c.st->vi[viSOf(w)])));
        }
        void lowerFcget(Ctx &c, const XNode &n)
        {
            if (viTOf(n.lowerWord) != 0u)
                c.st->vi[viTOf(n.lowerWord)] = static_cast<int32_t>(c.st->clip & 0x0FFFu);
        }

        // Q: DIV, SQRT, RSQRT. The result waits in the program's slot for its commit.
        void lowerDiv(Ctx &c, const XNode &n)
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
        void lowerSqrt(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            const int ftf = static_cast<int>((w >> 23) & 3u);
            const float val = normalizeOperand(c.st->vf[ftOf(w)][ftf]);
            c.qValue = normalizeResult(std::sqrt(std::fabs(val)));
            c.qStatusDi = (val < 0.0f ? 0x10u : 0u) & 0x30u;
        }
        void lowerRsqrt(Ctx &c, const XNode &n)
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
        void lowerEsadd(Ctx &c, const XNode &n)
        {
            const uint32_t w = n.lowerWord;
            const float x = normalizeOperand(c.st->vf[fsOf(w)][0]);
            const float y = normalizeOperand(c.st->vf[fsOf(w)][1]);
            const float z = normalizeOperand(c.st->vf[fsOf(w)][2]);
            c.pValue[n.efuSlot] = normalizeResult(x * x + y * y + z * z);
        }
        void lowerErsqrt(Ctx &c, const XNode &n)
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
        inline int32_t branchVi(const Ctx &c, const XNode &n, uint32_t reg)
        {
            if (reg == 0u)
                return 0;
            return reg == n.readBackupReg ? c.backupValue : c.st->vi[reg];
        }
        void lowerB(Ctx &c, const XNode &) { c.taken = true; }
        void lowerBal(Ctx &c, const XNode &n)
        {
            const uint32_t it = viTOf(n.lowerWord);
            if (it != 0u)
                c.st->vi[it] = static_cast<int32_t>((n.pc + 16u) / 8u);
            c.taken = true;
        }
        void lowerIbeq(Ctx &c, const XNode &n)
        {
            c.taken = static_cast<int16_t>(branchVi(c, n, viSOf(n.lowerWord))) == static_cast<int16_t>(branchVi(c, n, viTOf(n.lowerWord)));
        }
        void lowerIbne(Ctx &c, const XNode &n)
        {
            c.taken = static_cast<int16_t>(branchVi(c, n, viSOf(n.lowerWord))) != static_cast<int16_t>(branchVi(c, n, viTOf(n.lowerWord)));
        }
        void lowerIbltz(Ctx &c, const XNode &n) { c.taken = static_cast<int16_t>(branchVi(c, n, viSOf(n.lowerWord))) < 0; }
        void lowerIbgtz(Ctx &c, const XNode &n) { c.taken = static_cast<int16_t>(branchVi(c, n, viSOf(n.lowerWord))) > 0; }
        void lowerIblez(Ctx &c, const XNode &n) { c.taken = static_cast<int16_t>(branchVi(c, n, viSOf(n.lowerWord))) <= 0; }
        void lowerIbgez(Ctx &c, const XNode &n) { c.taken = static_cast<int16_t>(branchVi(c, n, viSOf(n.lowerWord))) >= 0; }

        OpFn nativeLower(uint32_t w)
        {
            switch ((w >> 25) & 0x7Fu)
            {
            case 0x00: return &lowerLq;
            case 0x01: return &lowerSq;
            case 0x04: return &lowerIlw;
            case 0x05: return &lowerIsw;
            case 0x08: return &lowerIaddiu;
            case 0x09: return &lowerIsubiu;
            case 0x10: return &lowerFceq;
            case 0x12: return &lowerFcand;
            case 0x13: return &lowerFcor;
            case 0x14: return &lowerFseq;
            case 0x16: return &lowerFsand;
            case 0x17: return &lowerFsor;
            case 0x18: return &lowerFmeq;
            case 0x1A: return &lowerFmand;
            case 0x1B: return &lowerFmor;
            case 0x1C: return &lowerFcget;
            case 0x20: return &lowerB;
            case 0x21: return &lowerBal;
            case 0x28: return &lowerIbeq;
            case 0x29: return &lowerIbne;
            case 0x2C: return &lowerIbltz;
            case 0x2D: return &lowerIbgtz;
            case 0x2E: return &lowerIblez;
            case 0x2F: return &lowerIbgez;
            case 0x40:
                break;
            default:
                return nullptr;
            }
            switch (w & 0x3Fu)
            {
            case 0x30: return &lowerIadd;
            case 0x31: return &lowerIsub;
            case 0x32: return &lowerIaddi;
            case 0x34: return &lowerIand;
            case 0x35: return &lowerIor;
            default:
                break;
            }
            if ((w & 0x3Fu) < 0x3Cu)
                return nullptr;
            switch ((w & 3u) | ((w >> 4) & 0x7Cu))
            {
            case 0x30: return &lowerMove;
            case 0x31: return &lowerMr32;
            case 0x34: return &lowerLqi;
            case 0x35: return &lowerSqi;
            case 0x36: return &lowerLqd;
            case 0x37: return &lowerSqd;
            case 0x38: return &lowerDiv;
            case 0x39: return &lowerSqrt;
            case 0x3A: return &lowerRsqrt;
            case 0x3C: return &lowerMtir;
            case 0x3D: return &lowerMfir;
            case 0x3E: return &lowerIlwr;
            case 0x3F: return &lowerIswr;
            case 0x64: return &lowerMfp;
            case 0x68: return &lowerXtop;
            case 0x69: return &lowerXitop;
            case 0x70: return &lowerEsadd;
            case 0x79: return &lowerErsqrt;
            default:
                return nullptr;
            }
        }

        // A lower half that does nothing at run time (WAITQ and WAITP only matter to the timing).
        bool lowerIsWait(uint32_t w)
        {
            if (((w >> 25) & 0x7Fu) != 0x40u || (w & 0x3Fu) < 0x3Cu)
                return false;
            const uint32_t special = (w & 3u) | ((w >> 4) & 0x7Cu);
            return special == 0x3Bu || special == 0x7Bu;
        }
    }

    Natives chooseOps(uint32_t upper, uint32_t lower, bool iBit, bool lowerNop, const Node &, bool allowNative)
    {
        Natives out;
        // Upper half
        const uint32_t op = upper & 0x3Fu;
        const uint32_t special = (upper & 3u) | ((upper >> 4) & 0x7Cu);
        const bool nop = op >= 0x3Cu && (special == 0x2Fu || special == 0x30u);
        if (!nop)
        {
            OpFn fn = nullptr;
            if (allowNative)
            {
                if (op < 0x3Cu)
                    fn = g_upper.main[op];
                else if (special < 0x30u)
                    fn = g_upper.special[special];
                if (fn == nullptr)
                    fn = nativeUpperOther(op, special);
            }
            if (fn != nullptr)
            {
                out.upper = fn;
                // The vf an op writes: fd for the main group, ft for the special group's vf-writing ops (ITOF, FTOI, ABS).
                const bool writesVfFd = op < 0x3Cu;
                const bool writesVfFt = op >= 0x3Cu && ((special >= 0x10u && special <= 0x17u) || special == 0x1Du);
                out.upperWritesVf0 = (writesVfFd && ((upper >> 6) & 0x1Fu) == 0u) || (writesVfFt && ((upper >> 16) & 0x1Fu) == 0u);
            }
            else
                out.upperGeneral = true;
        }
        // Lower half
        if (!iBit && !lowerNop && !lowerIsWait(lower))
        {
            OpFn fn = allowNative ? nativeLower(lower) : nullptr;
            if (fn != nullptr)
                out.lower = fn;
            else
                out.lowerGeneral = true;
        }
        return out;
    }
}
