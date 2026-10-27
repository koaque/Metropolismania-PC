// The platform layer, the compiler runtime's soft floating point (plan P7), written from what the functions do and proven against the
// recompiled functions by the difftest (captured calls and the numeric fuzz). Prototypes are in config/platform_signatures.txt.
//
// The EE's FPU does single precision only, so a double is a 64 bit integer in one register and every double operation is a call.
// The routines take a number apart into a class, a sign, an exponent and a fraction, work on the parts and put them together again,
// the usual scheme of a software floating point library. What differs from IEEE arithmetic is kept as it is, because the game's
// results depend on it:
//   - nothing is rounded: a result keeps the leading bits of its fraction and drops the rest (round toward zero);
//   - a double with exponent 0 (zero or denormal) is a zero, and a result below the smallest normal number is a zero;
//   - a single has no NaN and no infinity: exponent 255 is an ordinary number, and a result too large for a single is exponent 255
//     with a zero mantissa;
//   - a double operation that has to return a NaN returns the quiet NaN 0x7FF8000000000000 (the library's static "not a number" is
//     all zero in the image), or one of its operands with the quiet bit set.
// A number's fraction has its leading one at bit 60 (double) or bit 30 (single); an addition keeps two more bits, 61 and 62.

#include "game_functions.h"
#include "sdk.h"

#include <bit>

namespace mm
{
    namespace
    {
        enum Class : uint32_t
        {
            SignalingNaN = 0,
            QuietNaN = 1,
            Zero = 2,
            Number = 3,
            Infinity = 4,
        };

        struct Parts
        {
            uint32_t cls;
            uint32_t sign;       // 0 or 1
            int32_t exponent;    // the value is fraction * 2^(exponent - 60), or - 30 for a single
            uint64_t fraction;   // a NaN keeps its stored mantissa here
        };

        constexpr uint64_t kLeadingOne = 1ull << 60;
        constexpr uint64_t kQuietBit = 1ull << 51;
        constexpr uint64_t kMantissaMask = (1ull << 52) - 1;
        constexpr Parts kNaN = {SignalingNaN, 0, 0, 0};

        // ------------------------------------------------------------ single

        Parts unpackSingle(uint32_t bits)
        {
            const uint32_t exponent = (bits >> 23) & 0xFFu;
            const uint32_t sign = bits >> 31;
            if (exponent == 0)
                return {Zero, sign, 0, 0};
            return {Number, sign, static_cast<int32_t>(exponent) - 127, (static_cast<uint64_t>(bits & 0x7FFFFFu) << 7) | 0x40000000u};
        }

        uint32_t packSingle(const Parts &p)
        {
            const uint32_t fraction = static_cast<uint32_t>(p.fraction);
            uint32_t exponent = 0;
            uint32_t mantissa = 0;
            if (p.cls != Zero && fraction != 0)
            {
                if (p.exponent < -126)
                {
                    // too small: zero
                }
                else if (p.exponent > 128)
                    exponent = 0xFF;
                else if (p.exponent == 128)
                {
                    exponent = 0xFF;
                    if (static_cast<int32_t>(fraction) >= 0)
                        mantissa = fraction >> 7;
                }
                else
                {
                    exponent = static_cast<uint32_t>(p.exponent + 127);
                    mantissa = fraction >> 7;
                }
            }
            return ((p.sign & 1u) << 31) | ((exponent & 0xFFu) << 23) | (mantissa & 0x7FFFFFu);
        }

        // ------------------------------------------------------------ double

        Parts unpackDouble(uint64_t bits)
        {
            const uint32_t exponent = static_cast<uint32_t>(bits >> 52) & 0x7FFu;
            const uint32_t sign = static_cast<uint32_t>(bits >> 63);
            const uint64_t mantissa = bits & kMantissaMask;
            if (exponent == 0)
                return {Zero, sign, 0, 0};
            if (exponent == 0x7FF)
            {
                if (mantissa == 0)
                    return {Infinity, sign, 0x3FF, kLeadingOne};
                return {(mantissa & kQuietBit) != 0 ? QuietNaN : SignalingNaN, sign, 0x3FF, mantissa};
            }
            return {Number, sign, static_cast<int32_t>(exponent) - 0x3FF, (mantissa << 8) | kLeadingOne};
        }

        uint64_t packDouble(const Parts &p)
        {
            uint64_t fraction = p.fraction;
            uint32_t exponent = 0;
            if (p.cls < Zero)
            {
                exponent = 0x7FF;
                fraction |= kQuietBit;
            }
            else if (p.cls == Infinity)
            {
                exponent = 0x7FF;
                fraction = 0;
            }
            else if (p.cls == Zero || fraction == 0)
                fraction = 0;
            else if (p.exponent < -1022)
                fraction = 0;
            else if (p.exponent < 1024)
            {
                exponent = static_cast<uint32_t>(p.exponent + 0x3FF);
                fraction >>= 8;
            }
            else
            {
                exponent = 0x7FF;
                fraction = 0;
            }
            return (static_cast<uint64_t>(p.sign & 1u) << 63) | (static_cast<uint64_t>(exponent & 0x7FFu) << 52) | (fraction & kMantissaMask);
        }

        // One bit to the right, the lowest bit staying set if it was (so that a later subtraction still sees "something below").
        uint64_t shiftRightSticky(uint64_t value) { return (value & 1u) | (value >> 1); }

        // The 128 bit product of two 64 bit numbers from four 32 bit partial products.
        void multiply(uint64_t x, uint64_t y, uint64_t &high, uint64_t &low)
        {
            const uint64_t xLow = x & 0xFFFFFFFFu, xHigh = x >> 32, yLow = y & 0xFFFFFFFFu, yHigh = y >> 32;
            const uint64_t p0 = xLow * yLow, p1 = xLow * yHigh, p2 = xHigh * yLow, p3 = xHigh * yHigh;
            const uint64_t middle = p1 + p2;
            const uint64_t middleCarry = middle < p1 ? 1 : 0;
            low = p0 + (middle << 32);
            const uint64_t lowCarry = low < p0 ? 1 : 0;
            high = p3 + (middle >> 32) + (middleCarry << 32) + lowCarry;
        }

        Parts addParts(Parts a, Parts b)
        {
            if (a.cls < Zero)
                return a;
            if (b.cls < Zero)
                return b;
            if (a.cls == Infinity)
            {
                // Infinities of opposite signs have no sum.
                if (b.cls == Infinity && a.sign != b.sign)
                    return kNaN;
                return a;
            }
            if (b.cls == Infinity)
                return b;
            if (b.cls == Zero)
            {
                if (a.cls == Zero)
                {
                    a.sign = a.sign & b.sign; // -0 + -0 is -0, every other zero sum +0
                    return a;
                }
                return a;
            }
            if (a.cls == Zero)
                return b;

            // Two numbers: bring them to the larger exponent, shifting the smaller one right (a difference of 64 or more leaves it zero).
            int32_t aExponent = a.exponent, bExponent = b.exponent;
            uint64_t aFraction = a.fraction, bFraction = b.fraction;
            int32_t difference = aExponent - bExponent;
            if (difference < 0)
                difference = -difference;
            if (difference < 64)
            {
                if (bExponent < aExponent)
                {
                    for (int32_t n = aExponent - bExponent; n > 0; --n)
                        bFraction = shiftRightSticky(bFraction);
                    bExponent = aExponent;
                }
                else if (aExponent < bExponent)
                {
                    do
                    {
                        ++aExponent;
                        aFraction = shiftRightSticky(aFraction);
                    } while (aExponent < bExponent);
                }
            }
            else if (bExponent < aExponent)
                bFraction = 0;
            else
            {
                aExponent = bExponent;
                aFraction = 0;
            }

            Parts sum;
            sum.cls = Number;
            sum.exponent = aExponent;
            if (a.sign != b.sign)
            {
                const uint64_t difference64 = a.sign != 0 ? bFraction - aFraction : aFraction - bFraction;
                // The recompiled code decides the sign of the difference by a branch that the recompiler translates as a test of the
                // low 32 bits only, so a negative difference whose low word looks positive is not turned round: the result is then a
                // huge fraction. Kept as the game runs today (docs/PLATFORM_FINDINGS.md); the correct test is on all 64 bits.
                if (static_cast<int32_t>(static_cast<uint32_t>(difference64)) < 0)
                {
                    sum.sign = 1;
                    sum.fraction = 0 - difference64;
                }
                else
                {
                    sum.sign = 0;
                    sum.fraction = difference64;
                }
                // Cancellation: move the leading one back up to bit 60.
                while (sum.fraction != 0 && sum.fraction < kLeadingOne)
                {
                    sum.fraction <<= 1;
                    --sum.exponent;
                }
            }
            else
            {
                sum.sign = a.sign;
                sum.fraction = aFraction + bFraction;
            }
            if (sum.fraction >= (kLeadingOne << 1))
            {
                sum.fraction = shiftRightSticky(sum.fraction);
                ++sum.exponent;
            }
            return sum;
        }

        Parts multiplyParts(Parts a, Parts b)
        {
            const uint32_t sign = a.sign != b.sign ? 1u : 0u;
            if (a.cls < Zero)
            {
                a.sign = sign;
                return a;
            }
            if (b.cls < Zero)
            {
                b.sign = sign;
                return b;
            }
            if (a.cls == Infinity)
            {
                if (b.cls == Zero)
                    return kNaN;
                a.sign = sign;
                return a;
            }
            if (b.cls == Infinity)
            {
                if (a.cls == Zero)
                    return kNaN;
                b.sign = sign;
                return b;
            }
            if (a.cls == Zero)
            {
                a.sign = sign;
                return a;
            }
            if (b.cls == Zero)
            {
                b.sign = sign;
                return b;
            }

            // The product of the two fractions is 122 bits at most; its upper 64 bits are the new fraction (the lower ones are dropped
            // except for the bit that a right shift hands down), moved so that the leading one is at bit 60.
            uint64_t high, low;
            multiply(a.fraction, b.fraction, high, low);
            Parts product;
            product.cls = Number;
            product.sign = sign;
            product.exponent = a.exponent + b.exponent + 4;
            while (high >= (kLeadingOne << 1))
            {
                ++product.exponent;
                if ((high & 1u) != 0)
                    low = (low >> 1) | (1ull << 63);
                high >>= 1;
            }
            while (high < kLeadingOne)
            {
                --product.exponent;
                high = (high << 1) | ((low >> 63) & 1u);
                low <<= 1;
            }
            product.fraction = high;
            return product;
        }

        Parts divideParts(Parts a, Parts b)
        {
            if (a.cls < Zero)
                return a;
            if (b.cls < Zero)
                return b;
            a.sign ^= b.sign;
            if (a.cls == Infinity || a.cls == Zero)
            {
                // infinity / infinity and zero / zero have no value; otherwise the dividend's class stays
                if (a.cls == b.cls)
                    return kNaN;
                return a;
            }
            if (b.cls == Infinity)
            {
                a.fraction = 0;
                a.exponent = 0;
                return a;
            }
            if (b.cls == Zero)
            {
                a.cls = Infinity;
                return a;
            }
            // Long division, one quotient bit at a time (61 of them): the numerator is brought to at least the divisor first.
            uint64_t numerator = a.fraction;
            const uint64_t divisor = b.fraction;
            a.exponent -= b.exponent;
            if (numerator < divisor)
            {
                --a.exponent;
                numerator <<= 1;
            }
            uint64_t quotient = 0;
            for (uint64_t bit = kLeadingOne; bit != 0; bit >>= 1)
            {
                if (numerator >= divisor)
                {
                    quotient |= bit;
                    numerator -= divisor;
                }
                numerator <<= 1;
            }
            a.fraction = quotient;
            return a;
        }

        // -1, 0 or 1 for a < b, a == b, a > b; 1 when either is a NaN. Zeros of either sign are equal.
        int compareParts(const Parts &a, const Parts &b)
        {
            if (a.cls < Zero || b.cls < Zero)
                return 1;
            if (a.cls == Infinity)
            {
                if (b.cls == Infinity)
                    return static_cast<int>(b.sign) - static_cast<int>(a.sign);
                return a.sign != 0 ? -1 : 1;
            }
            if (b.cls == Infinity)
                return b.sign != 0 ? 1 : -1;
            if (a.cls == Zero)
            {
                if (b.cls == Zero)
                    return 0;
                return b.sign != 0 ? 1 : -1;
            }
            if (b.cls == Zero)
                return a.sign != 0 ? -1 : 1;
            if (a.sign != b.sign)
                return a.sign != 0 ? -1 : 1;
            // Same sign: the larger magnitude is the larger number for a positive pair and the smaller for a negative one.
            if (a.exponent > b.exponent)
                return a.sign != 0 ? -1 : 1;
            if (a.exponent < b.exponent)
                return a.sign != 0 ? 1 : -1;
            if (a.fraction > b.fraction)
                return a.sign != 0 ? -1 : 1;
            if (a.fraction < b.fraction)
                return a.sign != 0 ? 1 : -1;
            return 0;
        }

        uint32_t bitsOf(ps2f value) { return std::bit_cast<uint32_t>(static_cast<float>(value)); }
        ps2f floatOf(uint32_t bits) { return ps2f{std::bit_cast<float>(bits)}; }
    }

    // ---------------------------------------------------------------- single to integer and double

    // Float to int, truncating; zero and values below one give 0, magnitudes of 2^31 and more saturate.
    int sdk_fptosi(ps2f value)
    {
        const Parts p = unpackSingle(bitsOf(value));
        if (p.cls == Zero || p.exponent < 0)
            return 0;
        if (p.exponent >= 31)
            return p.sign != 0 ? INT32_MIN : INT32_MAX;
        const int32_t magnitude = static_cast<int32_t>(static_cast<uint32_t>(p.fraction) >> (30 - p.exponent));
        return p.sign != 0 ? -magnitude : magnitude;
    }

    // Float to unsigned int (returned as the register holds it, sign extended): negative numbers and values below one give 0,
    // 2^32 and more give all ones.
    int sdk_fptoui(ps2f value)
    {
        const Parts p = unpackSingle(bitsOf(value));
        if (p.cls == Zero || p.sign != 0 || p.exponent < 0)
            return 0;
        if (p.exponent >= 32)
            return -1;
        const uint32_t fraction = static_cast<uint32_t>(p.fraction);
        if (p.exponent < 31)
            return static_cast<int>(fraction >> (30 - p.exponent));
        return static_cast<int>(fraction << (p.exponent - 30));
    }

    // Float to double (exact).
    uint64_t sdk_fptodp(ps2f value)
    {
        Parts p = unpackSingle(bitsOf(value));
        p.fraction = static_cast<uint64_t>(static_cast<uint32_t>(p.fraction)) << 30;
        return packDouble(p);
    }

    // Double to float, keeping the leading 24 bits (a NaN becomes infinity, or zero when its stored mantissa is small).
    ps2f sdk_dptofp(uint64_t value)
    {
        Parts p = unpackDouble(value);
        const uint32_t high = static_cast<uint32_t>(static_cast<int64_t>(p.fraction << 2) >> 32);
        p.fraction = (p.fraction & 0x3FFFFFFFull) != 0 ? (high | 1u) : high;
        return floatOf(packSingle(p));
    }

    // Double to int, truncating; NaN and zero give 0, magnitudes of 2^31 and more saturate.
    int sdk_dptoli(uint64_t value)
    {
        const Parts p = unpackDouble(value);
        if (p.cls <= Zero)
            return 0;
        if (p.cls == Infinity)
            return p.sign != 0 ? INT32_MIN : INT32_MAX;
        if (p.exponent < 0)
            return 0;
        if (p.exponent >= 31)
            return p.sign != 0 ? INT32_MIN : INT32_MAX;
        const int32_t magnitude = static_cast<int32_t>(static_cast<uint32_t>(p.fraction >> (60 - p.exponent)));
        return p.sign != 0 ? -magnitude : magnitude;
    }

    namespace
    {
        // A number below 2^33 as a double (exact).
        uint64_t exactDouble(uint64_t value)
        {
            if (value == 0)
                return 0;
            const int top = 63 - std::countl_zero(value);
            const uint64_t mantissa = (value << (52 - top)) & kMantissaMask;
            return (static_cast<uint64_t>(top + 0x3FF) << 52) | mantissa;
        }
    }

    // Unsigned int to double. A value of 2^31 and more takes the library's path for a negative number (the recompiled code tests the
    // sign of the low 32 bits of the zero extended register, see dpadd): half of it, with the lowest bit kept if it was set, is
    // converted and doubled, so an odd value comes out as the even number below it, or above it when its second bit is clear.
    uint64_t sdk_d_utod(uint32_t value)
    {
        if ((value & 0x80000000u) != 0)
            return exactDouble(2ull * ((value >> 1) | (value & 1u)));
        return exactDouble(value);
    }

    // ---------------------------------------------------------------- double arithmetic

    uint64_t sdk_dpadd(uint64_t a, uint64_t b) { return packDouble(addParts(unpackDouble(a), unpackDouble(b))); }

    // a - b: the sum with the sign of b flipped (also for a zero or a NaN).
    uint64_t sdk_dpsub(uint64_t a, uint64_t b)
    {
        Parts negated = unpackDouble(b);
        negated.sign ^= 1u;
        return packDouble(addParts(unpackDouble(a), negated));
    }

    // An int to a double (exact).
    uint64_t sdk_litodp(int value)
    {
        if (value == 0)
            return 0;
        const uint32_t magnitude = value < 0 ? 0u - static_cast<uint32_t>(value) : static_cast<uint32_t>(value);
        return (value < 0 ? 0x8000000000000000ull : 0ull) | sdk_d_utod(magnitude);
    }

    uint64_t sdk_dpmul(uint64_t a, uint64_t b) { return packDouble(multiplyParts(unpackDouble(a), unpackDouble(b))); }
    uint64_t sdk_dpdiv(uint64_t a, uint64_t b) { return packDouble(divideParts(unpackDouble(a), unpackDouble(b))); }

    // The ordering of two doubles: -1, 0, 1 (1 when either is a NaN).
    int sdk_dpcmp(uint64_t a, uint64_t b) { return compareParts(unpackDouble(a), unpackDouble(b)); }

    // a < b as 1 or 0 (a NaN compares as not less).
    int sdk_fp_ltdf2(uint64_t a, uint64_t b) { return sdk_dpcmp(a, b) < 0 ? 1 : 0; }

    PORT_FN(sdk_fptosi);
    PORT_FN(sdk_fptoui);
    PORT_FN(sdk_fptodp);
    PORT_FN(sdk_dptofp);
    PORT_FN(sdk_dptoli);
    PORT_FN(sdk_d_utod);
    PORT_FN(sdk_dpadd);
    PORT_FN(sdk_dpsub);
    PORT_FN(sdk_litodp);
    PORT_FN(sdk_dpmul);
    PORT_FN(sdk_dpdiv);
    PORT_FN(sdk_dpcmp);
    PORT_FN(sdk_fp_ltdf2);
}
