// The platform layer, double precision math library (plan P7): subtraction, integer to double, copysign, scalbn, square root and pow,
// written from what the functions do and proven against the recompiled functions by the difftest (the numeric fuzz). Prototypes are in
// config/platform_signatures.txt.
//
// A double is a 64 bit integer in one register and every operation is a call into the soft float routines (softfloat.cpp), which round
// toward zero and have no denormals. The routines here are the usual double precision ones of the C library (an argument reduction with
// the high and low 32 bit words handled as integers, and polynomials for pow), and every constant is the library's, as the 64 bit
// pattern: some differ from the textbook values in the last bit, because the library's decimal to binary conversion truncated.

#include "game_functions.h"
#include "sdk.h"

#include <bit>

namespace mm
{
    namespace
    {
        struct Dbl
        {
            uint64_t bits;
        };

        Dbl operator+(Dbl a, Dbl b) { return Dbl{sdk_dpadd(a.bits, b.bits)}; }
        Dbl operator-(Dbl a, Dbl b) { return Dbl{sdk_dpsub(a.bits, b.bits)}; }
        Dbl operator*(Dbl a, Dbl b) { return Dbl{sdk_dpmul(a.bits, b.bits)}; }
        Dbl operator/(Dbl a, Dbl b) { return Dbl{sdk_dpdiv(a.bits, b.bits)}; }

        int32_t highWord(Dbl d) { return static_cast<int32_t>(d.bits >> 32); }
        uint32_t lowWord(Dbl d) { return static_cast<uint32_t>(d.bits); }
        Dbl withHigh(Dbl d, int32_t high) { return Dbl{(static_cast<uint64_t>(static_cast<uint32_t>(high)) << 32) | (d.bits & 0xFFFFFFFFull)}; }
        Dbl withLowZero(Dbl d) { return Dbl{d.bits & 0xFFFFFFFF00000000ull}; }
        Dbl fromWords(int32_t high, uint32_t low) { return Dbl{(static_cast<uint64_t>(static_cast<uint32_t>(high)) << 32) | low}; }

        constexpr Dbl kZero{0x0000000000000000ull};
        constexpr Dbl kOne{0x3FF0000000000000ull};
        constexpr Dbl kMinusOne{0xBFF0000000000000ull};
        constexpr Dbl kTwo{0x4000000000000000ull};
        constexpr Dbl kThree{0x4008000000000000ull};
        constexpr Dbl kHalf{0x3FE0000000000000ull};
        constexpr Dbl kQuarter{0x3FD0000000000000ull};
        constexpr Dbl kThird{0x3FD5555555555555ull};
        constexpr Dbl kTwo53{0x4340000000000000ull};
        constexpr Dbl kTwo54{0x4350000000000000ull};
        constexpr Dbl kTwoMinus54{0x3C90000000000000ull};
        constexpr Dbl kHuge{0x7E37E43C8800759Bull};  // 1e300
        constexpr Dbl kTiny{0x01A56E1FC2F8F358ull};  // 1e-300
        constexpr Dbl kLargest{0x7FEFFFFFFFFFFFFFull}; // what the library's huge * huge came to when its compiler folded it

        // pow's constants: bp = {1, 1.5}, dp_h and dp_l = the high and low parts of log2(1.5), then the polynomials of log (L) and exp (P),
        // ln2 in three parts, the overflow threshold, 2/(3 ln 2) and 1/ln2 in three parts.
        constexpr Dbl kBp[2] = {Dbl{0x3FF0000000000000ull}, Dbl{0x3FF8000000000000ull}};
        constexpr Dbl kDpHigh[2] = {Dbl{0x0000000000000000ull}, Dbl{0x3FE2B8033FFFFFFFull}};
        constexpr Dbl kDpLow[2] = {Dbl{0x0000000000000000ull}, Dbl{0x3E4CFDEB43CFD005ull}};
        constexpr Dbl kL1{0x3FE3333333333302ull}, kL2{0x3FDB6DB6DB6FABFFull}, kL3{0x3FD55555518F264Cull}, kL4{0x3FD17460A91D4100ull},
            kL5{0x3FCD864A93C9DB64ull}, kL6{0x3FCA7E284A454EEFull};
        constexpr Dbl kP1{0x3FC555555555553Eull}, kP2{0xBF66C16C16BEBD92ull}, kP3{0x3F11566AAF25DE2Bull}, kP4{0xBEBBBD41C5D26BF1ull},
            kP5{0x3E66376972BEA4D0ull};
        constexpr Dbl kLg2{0x3FE62E42FEFA39EFull}, kLg2High{0x3FE62E4300000000ull}, kLg2Low{0xBE205C610CA86C38ull};
        constexpr Dbl kOvt{0x3C971547652B82FEull};
        constexpr Dbl kCp{0x3FEEC709DC3A03FDull}, kCpHigh{0x3FEEC709DFFFFFFFull}, kCpLow{0xBE3E2FE0145B01F4ull};
        constexpr Dbl kIvln2{0x3FF71547652B82FDull}, kIvln2High{0x3FF715475FFFFFFFull}, kIvln2Low{0x3E54AE0BF85DDF44ull};

        Dbl copySign(Dbl magnitude, Dbl sign)
        {
            return fromWords((highWord(magnitude) & 0x7FFFFFFF) | (highWord(sign) & static_cast<int32_t>(0x80000000u)), lowWord(magnitude));
        }

        // x * 2^n by changing the exponent field.
        Dbl scaleByPowerOfTwo(Dbl x, int n)
        {
            int32_t hx = highWord(x);
            const uint32_t lx = lowWord(x);
            int32_t k = (hx & 0x7FF00000) >> 20;
            if (k == 0)
            {
                if ((lx | static_cast<uint32_t>(hx & 0x7FFFFFFF)) == 0)
                    return x;
                x = x * kTwo54;
                hx = highWord(x);
                k = ((hx & 0x7FF00000) >> 20) - 54;
                if (n < -50000)
                    return kTiny * x;
            }
            if (k == 0x7FF)
                return x + x;
            k += n;
            if (k > 0x7FE)
                return copySign(kHuge, x) * kHuge;
            if (k > 0)
                return withHigh(x, (hx & static_cast<int32_t>(0x800FFFFFu)) | (k << 20));
            if (k <= -54)
            {
                if (n > 50000)
                    return copySign(kHuge, x) * kHuge;
                return copySign(kTiny, x) * kTiny;
            }
            k += 54;
            return withHigh(x, (hx & static_cast<int32_t>(0x800FFFFFu)) | (k << 20)) * kTwoMinus54;
        }

        // Square root, bit by bit on the two words of the mantissa.
        Dbl squareRoot(Dbl x)
        {
            constexpr uint32_t kSign = 0x80000000u;
            int32_t ix0 = highWord(x);
            uint32_t ix1 = lowWord(x);
            if ((ix0 & 0x7FF00000) == 0x7FF00000)
                return x * x + x;
            if (ix0 <= 0)
            {
                if (((ix0 & 0x7FFFFFFF) | static_cast<int32_t>(ix1)) == 0)
                    return x;
                if (ix0 < 0)
                    return (x - x) / (x - x);
            }
            int32_t m = ix0 >> 20;
            if (m == 0)
            {
                while (ix0 == 0)
                {
                    m -= 21;
                    ix0 |= static_cast<int32_t>(ix1 >> 11);
                    ix1 <<= 21;
                }
                int32_t i = 0;
                for (; (ix0 & 0x00100000) == 0; ++i)
                    ix0 <<= 1;
                m -= i - 1;
                ix0 |= static_cast<int32_t>(ix1 >> ((32 - i) & 31)); // a shift by 32 is a shift by 0 on the EE
                ix1 <<= i;
            }
            m -= 1023;
            ix0 = (ix0 & 0x000FFFFF) | 0x00100000;
            if ((m & 1) != 0)
            {
                ix0 += ix0 + static_cast<int32_t>((ix1 & kSign) >> 31);
                ix1 += ix1;
            }
            m >>= 1;
            ix0 += ix0 + static_cast<int32_t>((ix1 & kSign) >> 31);
            ix1 += ix1;
            int32_t q = 0, s0 = 0;
            uint32_t q1 = 0, s1 = 0;
            for (int32_t r = 0x00200000; r != 0; r >>= 1)
            {
                const int32_t t = s0 + r;
                if (t <= ix0)
                {
                    s0 = t + r;
                    ix0 -= t;
                    q += r;
                }
                ix0 += ix0 + static_cast<int32_t>((ix1 & kSign) >> 31);
                ix1 += ix1;
            }
            for (uint32_t r = kSign; r != 0; r >>= 1)
            {
                const uint32_t t1 = s1 + r;
                const int32_t t = s0;
                if (t < ix0 || (t == ix0 && t1 <= ix1))
                {
                    s1 = t1 + r;
                    if ((t1 & kSign) == kSign && (s1 & kSign) == 0)
                        s0 += 1;
                    ix0 -= t;
                    if (ix1 < t1)
                        ix0 -= 1;
                    ix1 -= t1;
                    q1 += r;
                }
                ix0 += ix0 + static_cast<int32_t>((ix1 & kSign) >> 31);
                ix1 += ix1;
            }
            if ((static_cast<uint32_t>(ix0) | ix1) != 0)
            {
                // Rounding: the original tests 1 - tiny >= 1 and 1 + tiny > 1 here, which came out as equal comparisons.
                if (q1 == 0xFFFFFFFFu)
                {
                    q1 = 0;
                    q += 1;
                }
                else
                    q1 += q1 & 1u;
            }
            ix0 = (q >> 1) + 0x3FE00000;
            ix1 = q1 >> 1;
            if ((q & 1) == 1)
                ix1 |= kSign;
            ix0 += m << 20;
            return fromWords(ix0, ix1);
        }

        // x ** y: log2(x) as a sum of two doubles t1 + t2, multiplied by y with the same split, then 2 ** that.
        Dbl power(Dbl x, Dbl y)
        {
            const int32_t hx = highWord(x), hy = highWord(y);
            const uint32_t lx = lowWord(x), ly = lowWord(y);
            const int32_t ix = hx & 0x7FFFFFFF, iy = hy & 0x7FFFFFFF;

            if ((static_cast<uint32_t>(iy) | ly) == 0)
                return kOne;
            if (ix > 0x7FF00000 || (ix == 0x7FF00000 && lx != 0) || iy > 0x7FF00000 || (iy == 0x7FF00000 && ly != 0))
                return x + y;

            // 0: y is not an integer, 1: odd, 2: even (only needed for a negative x)
            int yIsInt = 0;
            if (hx < 0)
            {
                if (iy >= 0x43400000)
                    yIsInt = 2;
                else if (iy >= 0x3FF00000)
                {
                    const int32_t k = (iy >> 20) - 0x3FF;
                    if (k > 20)
                    {
                        const uint32_t j = ly >> (52 - k);
                        if ((j << (52 - k)) == ly)
                            yIsInt = 2 - static_cast<int>(j & 1u);
                    }
                    else if (ly == 0)
                    {
                        const int32_t j = iy >> (20 - k);
                        if ((j << (20 - k)) == iy)
                            yIsInt = 2 - (j & 1);
                    }
                }
            }

            if (ly == 0)
            {
                if (iy == 0x7FF00000)
                {
                    if (static_cast<uint32_t>(ix - 0x3FF00000) == 0 && lx == 0)
                        return y - y;
                    if (ix >= 0x3FF00000)
                        return hy >= 0 ? y : kZero;
                    return hy < 0 ? kZero - y : kZero;
                }
                if (iy == 0x3FF00000)
                    return hy < 0 ? kOne / x : x;
                if (hy == 0x40000000)
                    return x * x;
                if (hy == 0x3FE00000 && hx >= 0)
                    return squareRoot(x);
            }

            // |x|. The recompiled code gets it from a call of the runtime's fabs handler, which reads a float and leaves v0 as it was: the
            // register then holds what the delay slots on the way here put in it (docs/PLATFORM_FINDINGS.md). That is reproduced while
            // g_recompiledQuirks is set; otherwise it is the absolute value.
            Dbl ax{x.bits & 0x7FFFFFFFFFFFFFFFull};
            if (g_recompiledQuirks)
            {
                uint64_t leftoverV0 = 0x3FE00000u;
                if (ly != 0)
                    leftoverV0 = (hx < 0 && iy >= 0x3FF00000 && iy < 0x43400000 && (iy >> 20) - 0x3FF <= 20) ? 0x14u : 0x7FF00000u;
                ax = Dbl{leftoverV0};
            }
            const int32_t xIsNegative = static_cast<int32_t>(static_cast<uint32_t>(hx) >> 31);
            if (lx == 0 && (ix == 0x7FF00000 || ix == 0 || ix == 0x3FF00000))
            {
                Dbl z = ax; // +-0, +-infinity, +-1
                if (hy < 0)
                    z = kOne / z;
                if (hx < 0)
                {
                    if ((static_cast<uint32_t>(ix - 0x3FF00000) | static_cast<uint32_t>(yIsInt)) == 0)
                        z = (z - z) / (z - z);
                    else if (yIsInt == 1)
                        z = kZero - z;
                }
                return z;
            }
            if (((xIsNegative - 1) | yIsInt) == 0)
                return (x - x) / (x - x); // a negative number to a non-integer power

            Dbl t1, t2; // log2(|x|) = t1 + t2
            if (iy > 0x41E00000)
            {
                // |y| > 2^31: a result that is certain to overflow or underflow unless x is within 2^-20 of 1
                if (iy > 0x43F00000)
                {
                    if (ix <= 0x3FEFFFFF)
                        return hy < 0 ? kLargest : kZero;
                    if (ix >= 0x3FF00000)
                        return hy > 0 ? kLargest : kZero;
                }
                if (ix < 0x3FEFFFFF)
                    return hy < 0 ? kLargest : kZero;
                if (ix > 0x3FF00000)
                    return hy > 0 ? kLargest : kZero;
                // log(x) = t - t^2/2 + t^3/3 - t^4/4 for t = x - 1
                const Dbl t = x - kOne;
                const Dbl w = (t * t) * (kHalf - t * (kThird - t * kQuarter));
                const Dbl u = kIvln2High * t;
                const Dbl v = t * kIvln2Low - w * kIvln2;
                t1 = withLowZero(u + v);
                t2 = v - (t1 - u);
            }
            else
            {
                Dbl scaled = ax;
                int32_t n = 0;
                int32_t scaledHigh = ix;
                if (ix < 0x00100000)
                {
                    scaled = scaled * kTwo53;
                    n -= 53;
                    scaledHigh = highWord(scaled);
                }
                n += (scaledHigh >> 20) - 0x3FF;
                const int32_t j = scaledHigh & 0x000FFFFF;
                scaledHigh = j | 0x3FF00000;
                int k;
                if (j <= 0x3988E)
                    k = 0;
                else if (j < 0xBB67A)
                    k = 1;
                else
                {
                    k = 0;
                    n += 1;
                    scaledHigh -= 0x00100000;
                }
                scaled = withHigh(scaled, scaledHigh);

                // log(scaled) from s = (scaled - bp)/(scaled + bp), kept as s_h + s_l
                const Dbl u = scaled - kBp[k];
                const Dbl v = kOne / (scaled + kBp[k]);
                const Dbl s = u * v;
                const Dbl sHigh = withLowZero(s);
                Dbl tHigh = withHigh(kZero, ((scaledHigh >> 1) | 0x20000000) + 0x00080000 + (k << 18));
                Dbl tLow = scaled - (tHigh - kBp[k]);
                const Dbl sLow = v * ((u - sHigh * tHigh) - sHigh * tLow);
                Dbl s2 = s * s;
                Dbl r = (s2 * s2) * (kL1 + s2 * (kL2 + s2 * (kL3 + s2 * (kL4 + s2 * (kL5 + s2 * kL6)))));
                r = r + sLow * (sHigh + s);
                s2 = sHigh * sHigh;
                tHigh = withLowZero((kThree + s2) + r);
                tLow = r - ((tHigh - kThree) - s2);
                const Dbl u2 = sHigh * tHigh;
                const Dbl v2 = sLow * tHigh + tLow * s;
                const Dbl pHigh = withLowZero(u2 + v2);
                const Dbl pLow = v2 - (pHigh - u2);
                const Dbl zHigh = kCpHigh * pHigh;
                const Dbl zLow = (kCpLow * pHigh + pLow * kCp) + kDpLow[k];
                const Dbl nn = Dbl{sdk_litodp(n)};
                t1 = withLowZero(((zHigh + zLow) + kDpHigh[k]) + nn);
                t2 = zLow - (((t1 - nn) - kDpHigh[k]) - zHigh);
            }

            Dbl sign = kOne; // -1 for a negative x to an odd power
            if (((xIsNegative - 1) | (yIsInt - 1)) == 0)
                sign = kMinusOne;

            // (y1 + y2) * (t1 + t2), then 2 ** that
            const Dbl y1 = withLowZero(y);
            const Dbl pLow = (y - y1) * t1 + y * t2;
            Dbl pHigh = y1 * t1;
            Dbl z = pLow + pHigh;
            const int32_t zj = highWord(z);
            const uint32_t zi = lowWord(z);
            if (zj >= 0x40900000)
            {
                if (((static_cast<uint32_t>(zj) - 0x40900000u) | zi) != 0)
                    return (sign * kHuge) * kHuge;
                if (sdk_dpcmp((pLow + kOvt).bits, (z - pHigh).bits) > 0)
                    return (sign * kHuge) * kHuge;
            }
            else if ((zj & 0x7FFFFFFF) >= 0x4090CC00)
            {
                if (((static_cast<uint32_t>(zj) - 0xC090CC00u) | zi) != 0)
                    return (sign * kTiny) * kTiny;
                if (sdk_dpcmp(pLow.bits, (z - pHigh).bits) <= 0)
                    return (sign * kTiny) * kTiny;
            }

            const int32_t i2 = zj & 0x7FFFFFFF;
            int32_t k2 = (i2 >> 20) - 0x3FF;
            int32_t n2 = 0;
            if (i2 > 0x3FE00000)
            {
                n2 = zj + (0x00100000 >> (k2 + 1));
                k2 = ((n2 & 0x7FFFFFFF) >> 20) - 0x3FF;
                const Dbl rounded = withHigh(kZero, n2 & ~(0x000FFFFF >> k2));
                n2 = ((n2 & 0x000FFFFF) | 0x00100000) >> (20 - k2);
                if (zj < 0)
                    n2 = -n2;
                pHigh = pHigh - rounded;
            }
            Dbl t = withLowZero(pLow + pHigh);
            const Dbl u = t * kLg2High;
            const Dbl v = (pLow - (t - pHigh)) * kLg2 + t * kLg2Low;
            z = u + v;
            const Dbl w = v - (z - u);
            t = z * z;
            const Dbl t1b = z - t * (kP1 + t * (kP2 + t * (kP3 + t * (kP4 + t * kP5))));
            const Dbl r = (z * t1b) / (t1b - kTwo) - (w + z * w);
            z = kOne - (r - z);
            const int32_t resultHigh = highWord(z) + (n2 << 20);
            if ((resultHigh >> 20) <= 0)
                z = scaleByPowerOfTwo(z, n2);
            else
                z = withHigh(z, resultHigh);
            return sign * z;
        }
    }

    uint64_t sdk_copysign(uint64_t x, uint64_t y) { return copySign(Dbl{x}, Dbl{y}).bits; }
    uint64_t sdk_scalbn(uint64_t x, int n) { return scaleByPowerOfTwo(Dbl{x}, n).bits; }
    uint64_t sdk_sqrt(uint64_t x) { return squareRoot(Dbl{x}).bits; }
    uint64_t sdk_pow(uint64_t x, uint64_t y) { return power(Dbl{x}, Dbl{y}).bits; }

    PORT_FN(sdk_copysign);
    PORT_FN(sdk_scalbn);
    PORT_FN(sdk_sqrt);
    PORT_FN(sdk_pow);
}
