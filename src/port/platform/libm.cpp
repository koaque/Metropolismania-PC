// The platform layer, single precision math library (plan P7): the game's libm routines, written from what they do and proven against
// the recompiled functions by the difftest (captured calls and the numeric fuzz). Prototypes are in config/platform_signatures.txt.
//
// They are the usual single precision routines of the C library (a polynomial after an argument reduction, or a bit by bit square
// root), run on the EE's float arithmetic (ps2f: no NaN, no infinity, no denormals, results truncated). Where the recompiled version
// differs from the textbook one, for instance in leaving out the checks for NaN and infinity that the EE cannot have, this one does too.
// All constants are the library's, as bit patterns.
//
// sinf and cosf end in three routines that the runtime does not run as recompiled code: the argument reduction and the two kernels are
// handlers of the runtime library (stand-ins that use the host's sinf and cosf). They stay as they are, so these two are exactly what
// the game does today; the port only does the choice of quadrant around them.

#include "game_functions.h"
#include "sdk.h"

#include <bit>

namespace mm
{
    namespace
    {
        constexpr ps2f F(uint32_t bits) { return ps2f{std::bit_cast<float>(bits)}; }
        uint32_t bitsOf(ps2f value) { return std::bit_cast<uint32_t>(static_cast<float>(value)); }
        ps2f floatOf(uint32_t bits) { return ps2f{std::bit_cast<float>(bits)}; }

        constexpr uint32_t kKernelCosf = 0x00118E88u; // handlers of the runtime library
        constexpr uint32_t kRemPio2f = 0x00118A08u;
        constexpr uint32_t kKernelSinf = 0x00119840u;

        constexpr ps2f kZero = F(0x00000000u);
        constexpr ps2f kOne = F(0x3F800000u);
        constexpr ps2f kTwo = F(0x40000000u);
        constexpr ps2f kOneAndHalf = F(0x3FC00000u);
        constexpr ps2f kHalf = F(0x3F000000u);
        constexpr ps2f kPi = F(0x40490FDAu);
        constexpr ps2f kPiOver2 = F(0x3FC90FDAu);
        constexpr ps2f kPiLow = F(0x34222168u);
        constexpr ps2f kPiOver2Low = F(0x33A22168u);

        ps2f absolute(ps2f x) { return floatOf(bitsOf(x) & 0x7FFFFFFFu); }

        // Square root, bit by bit. Zero and denormals come back as they are; a negative number gives (x - x) / (x - x).
        ps2f squareRoot(ps2f x)
        {
            int32_t ix = static_cast<int32_t>(bitsOf(x));
            if ((ix & 0x7FFFFFFF) <= 0x7FFFFF)
                return x;
            if (ix < 0)
                return (x - x) / (x - x);
            int32_t m = (ix >> 23) - 127;
            ix = (ix & 0x7FFFFF) | 0x800000;
            if ((m & 1) != 0)
                ix += ix;
            m >>= 1;
            ix += ix;
            int32_t q = 0, s = 0, r = 0x1000000;
            while (r != 0)
            {
                const int32_t t = s + r;
                if (t <= ix)
                {
                    s = t + r;
                    ix -= t;
                    q += r;
                }
                ix += ix;
                r = static_cast<int32_t>(static_cast<uint32_t>(r) >> 1);
            }
            if (ix != 0)
                q += q & 1;
            ix = (q >> 1) + 0x3F000000;
            ix += m << 23;
            return floatOf(static_cast<uint32_t>(ix));
        }

        // Arc tangent: the argument is brought into [0, 0.4375] by one of four identities, then a polynomial in x^2.
        constexpr ps2f kAtanHigh[4] = {F(0x3EED6338u), F(0x3F490FDAu), F(0x3F7B985Du), F(0x3FC90FDAu)};
        constexpr ps2f kAtanLow[4] = {F(0x31AC3769u), F(0x33222167u), F(0x33140FB4u), F(0x33A22168u)};
        constexpr ps2f kAtanPoly[11] = {F(0x3EAAAAABu), F(0xBE4CCCCCu), F(0x3E124924u), F(0xBDE38E38u), F(0x3DBA2E6Eu), F(0xBD9D8795u),
                                        F(0x3D886B34u), F(0xBD6EF16Au), F(0x3D4BDA59u), F(0xBD15A221u), F(0x3C8569D7u)};
        constexpr ps2f kHuge = F(0x7149F2C9u);

        ps2f arcTangent(ps2f x)
        {
            const int32_t hx = static_cast<int32_t>(bitsOf(x));
            const int32_t ix = hx & 0x7FFFFFFF;
            if (ix > 0x507FFFFF)
            {
                // beyond 2^34: pi/2 (the sum of the two halves of its representation), negated for a negative argument
                if (hx > 0)
                    return kAtanHigh[3] + kAtanLow[3];
                return -kAtanHigh[3] - kAtanLow[3];
            }
            int id;
            if (ix < 0x3EE00000)
            {
                if (ix < 0x31000000 && kHuge + x > kOne)
                    return x;
                id = -1;
            }
            else
            {
                x = absolute(x);
                if (ix < 0x3F980000)
                {
                    if (ix < 0x3F300000)
                    {
                        id = 0;
                        x = (x + x - kOne) / (kTwo + x);
                    }
                    else
                    {
                        id = 1;
                        x = (x - kOne) / (x + kOne);
                    }
                }
                else if (ix < 0x401C0000)
                {
                    id = 2;
                    x = (x - kOneAndHalf) / (kOne + kOneAndHalf * x);
                }
                else
                {
                    id = 3;
                    x = F(0xBF800000u) / x;
                }
            }
            const ps2f z = x * x;
            const ps2f w = z * z;
            const ps2f s1 = z * (kAtanPoly[0] + w * (kAtanPoly[2] + w * (kAtanPoly[4] + w * (kAtanPoly[6] + w * (kAtanPoly[8] + w * kAtanPoly[10])))));
            const ps2f s2 = w * (kAtanPoly[1] + w * (kAtanPoly[3] + w * (kAtanPoly[5] + w * (kAtanPoly[7] + w * kAtanPoly[9]))));
            if (id < 0)
                return x - x * (s1 + s2);
            const ps2f result = kAtanHigh[id] - ((x * (s1 + s2) - kAtanLow[id]) - x);
            return hx < 0 ? -result : result;
        }

        // acos(x) = pi/2 - asin(x), through a rational approximation r(z) = p(z)/q(z) with z = x^2, or (1 -/+ x)/2 beyond one half.
        ps2f arcCosine(ps2f x)
        {
            constexpr ps2f pS0 = F(0x3E2AAAAAu), pS1 = F(0xBEA6B08Fu), pS2 = F(0x3E4E0AA8u), pS3 = F(0xBD241146u), pS4 = F(0x3A4F7F04u),
                           pS5 = F(0x3811EF08u);
            constexpr ps2f qS1 = F(0xC019D138u), qS2 = F(0x4001572Cu), qS3 = F(0xBF303360u), qS4 = F(0x3D9DC62Du);
            const auto numerator = [&](ps2f z) { return z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5))))); };
            const auto denominator = [&](ps2f z) { return kOne + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4))); };

            const int32_t hx = static_cast<int32_t>(bitsOf(x));
            const int32_t ix = hx & 0x7FFFFFFF;
            if (ix == 0x3F800000)
                return hx > 0 ? kZero : kPi;
            if (ix > 0x3F800000)
                return (x - x) / (x - x);
            if (ix < 0x3F000000)
            {
                if (ix <= 0x23000000)
                    return kPiOver2;
                const ps2f z = x * x;
                const ps2f r = numerator(z) / denominator(z);
                return kPiOver2 - (x - (kPiOver2Low - x * r));
            }
            if (hx < 0)
            {
                const ps2f z = (kOne + x) * kHalf;
                const ps2f p = numerator(z);
                const ps2f q = denominator(z);
                const ps2f s = squareRoot(z);
                const ps2f r = p / q;
                const ps2f sum = s + (r * s - kPiOver2Low);
                return kPi - (sum + sum);
            }
            const ps2f z = (kOne - x) * kHalf;
            const ps2f s = squareRoot(z);
            const ps2f df = floatOf(bitsOf(s) & 0xFFFFF000u); // the high half of the root
            const ps2f c = (z - df * df) / (s + df);
            const ps2f p = numerator(z);
            const ps2f q = denominator(z);
            const ps2f r = p / q;
            const ps2f sum = df + (r * s + c);
            return sum + sum;
        }

        // atan2(y, x): the quadrant from the signs, the angle from atan(|y/x|); no NaN or infinity handling. A zero (or denormal)
        // operand is told apart by its exponent field.
        ps2f arcTangent2(ps2f y, ps2f x)
        {
            const int32_t hx = static_cast<int32_t>(bitsOf(x));
            const int32_t ix = hx & 0x7FFFFFFF;
            const int32_t hy = static_cast<int32_t>(bitsOf(y));
            const int32_t iy = hy & 0x7FFFFFFF;
            if (hx == 0x3F800000)
                return arcTangent(y);
            const int m = ((hy >> 31) & 1) | ((hx >> 30) & 2); // 2 * sign(x) + sign(y)
            if (iy <= 0x7FFFFF)
            {
                switch (m)
                {
                case 2: return kPi;
                case 3: return F(0xC0490FDAu);
                default: return y;
                }
            }
            if (ix <= 0x7FFFFF)
                return hy < 0 ? F(0xBFC90FDAu) : kPiOver2;
            const int32_t k = (iy - ix) >> 23;
            ps2f z;
            if (k > 60)
                z = kPiOver2;
            else if (hx < 0 && k < -60)
                z = kZero;
            else
                z = arcTangent(absolute(y / x));
            switch (m)
            {
            case 0: return z;
            case 1: return floatOf(bitsOf(z) ^ 0x80000000u);
            case 2: return kPi - (z - kPiLow);
            default: return (z - kPiLow) - kPi;
            }
        }

        // fmod(x, y) by shift and subtract on the mantissas, the result with the sign of x. A zero y gives (x * y) / (x * y).
        ps2f floatRemainder(ps2f x, ps2f y)
        {
            int32_t hx = static_cast<int32_t>(bitsOf(x));
            const int32_t hy = static_cast<int32_t>(bitsOf(y)) & 0x7FFFFFFF;
            const int32_t sx = hx & static_cast<int32_t>(0x80000000u);
            const ps2f signedZero = sx != 0 ? F(0x80000000u) : kZero;
            hx ^= sx;
            if (hy <= 0x7FFFFF)
            {
                const ps2f product = x * y;
                return product / product;
            }
            if (hx < hy)
                return x;
            if (hx == hy)
                return signedZero;
            // Here |x| >= |y| >= the smallest normal number, so both have an exponent field of at least 1 (no denormals).
            const int32_t ix = (hx >> 23) - 127;
            int32_t iy = (hy >> 23) - 127;
            hx = (hx & 0x7FFFFF) | 0x800000;
            const int32_t my = (hy & 0x7FFFFF) | 0x800000;
            for (int32_t n = ix - iy; n-- != 0;)
            {
                const int32_t hz = hx - my;
                if (hz < 0)
                    hx += hx;
                else
                {
                    if (hz == 0)
                        return signedZero;
                    hx = hz + hz;
                }
            }
            const int32_t hz = hx - my;
            if (hz >= 0)
                hx = hz;
            if (hx == 0)
                return signedZero;
            while (hx < 0x800000)
            {
                hx += hx;
                --iy;
            }
            if (iy >= -126)
                return floatOf(static_cast<uint32_t>((hx - 0x800000) | ((iy + 127) << 23) | sx));
            return floatOf(static_cast<uint32_t>((hx >> (-126 - iy)) | sx));
        }

        // The sine and the cosine of y0 + y1 for an argument already reduced to [-pi/4, pi/4] (the runtime's handlers).
        ps2f kernelSine(ps2f y0, ps2f y1) { return port::callGuest<ps2f>(kKernelSinf, y0, y1, 1); }
        ps2f kernelCosine(ps2f y0, ps2f y1) { return port::callGuest<ps2f>(kKernelCosf, y0, y1); }
    }

    ps2f sdk_fabsf(ps2f x) { return absolute(x); }
    ps2f sdk_sqrtf(ps2f x) { return squareRoot(x); }
    ps2f sdk_atanf(ps2f x) { return arcTangent(x); }
    ps2f sdk_acosf(ps2f x) { return arcCosine(x); }
    ps2f sdk_atan2f(ps2f y, ps2f x) { return arcTangent2(y, x); }
    ps2f sdk_fmodf(ps2f x, ps2f y) { return floatRemainder(x, y); }

    // The sine: the kernel directly for |x| <= pi/4 (0x3F490FD8), else the quadrant of the reduction picks sin or cos of the remainder.
    ps2f sdk_sinf(ps2f x)
    {
        port::GuestFrame<0x30> frame;
        if (static_cast<int32_t>(bitsOf(x) & 0x7FFFFFFFu) <= 0x3F490FD8)
            return port::callGuest<ps2f>(kKernelSinf, x, kZero, 0);
        const uint32_t parts = frame.ptr().address;
        const int n = port::callGuest<int>(kRemPio2f, x, parts);
        const ps2f y0 = ps2f{port::load<float>(parts)};
        const ps2f y1 = ps2f{port::load<float>(parts + 4u)};
        switch (n & 3)
        {
        case 0: return kernelSine(y0, y1);
        case 1: return kernelCosine(y0, y1);
        case 2: return -kernelSine(y0, y1);
        default: return -kernelCosine(y0, y1);
        }
    }

    ps2f sdk_cosf(ps2f x)
    {
        port::GuestFrame<0x30> frame;
        if (static_cast<int32_t>(bitsOf(x) & 0x7FFFFFFFu) <= 0x3F490FD8)
            return port::callGuest<ps2f>(kKernelCosf, x, kZero);
        const uint32_t parts = frame.ptr().address;
        const int n = port::callGuest<int>(kRemPio2f, x, parts);
        const ps2f y0 = ps2f{port::load<float>(parts)};
        const ps2f y1 = ps2f{port::load<float>(parts + 4u)};
        switch (n & 3)
        {
        case 0: return kernelCosine(y0, y1);
        case 1: return -kernelSine(y0, y1);
        case 2: return -kernelCosine(y0, y1);
        default: return kernelSine(y0, y1);
        }
    }

    PORT_FN(sdk_fabsf);
    PORT_FN(sdk_sqrtf);
    PORT_FN(sdk_atanf);
    PORT_FN(sdk_acosf);
    PORT_FN(sdk_atan2f);
    PORT_FN(sdk_fmodf);
    PORT_FN(sdk_sinf);
    PORT_FN(sdk_cosf);
}
