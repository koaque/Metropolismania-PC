// Pre-timed VU1 programs: choosing the native op for each half of a pair (plan P4, M3.5). The ops are in vu1aot_ops.h; see
// vu1aot_exec.h. The graph executor calls them through the pointers chosen here, and the compiled programs (vu1aot_emit.cpp)
// call them by the names chosen here.

#include "vu1aot_ops.h"

namespace vu1aot
{
    namespace
    {
        // An op and its name in a compiled program (relative to namespace vu1aot).
        struct OpRef
        {
            OpFn fn = nullptr;
            const char *name = nullptr;
        };
#define VU1AOT_REF(...) OpRef{&__VA_ARGS__, #__VA_ARGS__}

        // ---- upper half: the arithmetic family ----

        struct UpperTable
        {
            OpRef main[0x3C]{};
            OpRef special[0x30]{};
            UpperTable()
            {
                const auto build = [&](bool special) {
                    const auto put = [&](uint32_t op, OpRef ref) { (special ? this->special[op] : this->main[op]) = ref; };
#define VU1AOT_ONE(OP, KIND, BSRC) put((OP), special ? VU1AOT_REF(ops::upperFmac<KIND, BSRC, true>) : VU1AOT_REF(ops::upperFmac<KIND, BSRC, false>))
#define VU1AOT_BC(OPBASE, KIND)                                                                                              \
    VU1AOT_ONE((OPBASE) + 0, KIND, 0);                                                                                       \
    VU1AOT_ONE((OPBASE) + 1, KIND, 1);                                                                                       \
    VU1AOT_ONE((OPBASE) + 2, KIND, 2);                                                                                       \
    VU1AOT_ONE((OPBASE) + 3, KIND, 3)
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

        OpRef nativeUpperOther(uint32_t op, uint32_t special)
        {
            if (op < 0x3Cu)
            {
                if (op >= 0x10u && op <= 0x13u) return VU1AOT_REF(ops::upperMinMax<true, 0>);
                if (op >= 0x14u && op <= 0x17u) return VU1AOT_REF(ops::upperMinMax<false, 0>);
                switch (op)
                {
                case 0x1D: return VU1AOT_REF(ops::upperMinMax<true, 2>);
                case 0x1F: return VU1AOT_REF(ops::upperMinMax<false, 2>);
                case 0x2B: return VU1AOT_REF(ops::upperMinMax<true, 1>);
                case 0x2F: return VU1AOT_REF(ops::upperMinMax<false, 1>);
                case 0x2E: return VU1AOT_REF(ops::upperCross<true>);
                default: return {};
                }
            }
            switch (special)
            {
            case 0x10: return VU1AOT_REF(ops::upperItof<0>);
            case 0x11: return VU1AOT_REF(ops::upperItof<4>);
            case 0x12: return VU1AOT_REF(ops::upperItof<12>);
            case 0x13: return VU1AOT_REF(ops::upperItof<15>);
            case 0x14: return VU1AOT_REF(ops::upperFtoi<0>);
            case 0x15: return VU1AOT_REF(ops::upperFtoi<4>);
            case 0x16: return VU1AOT_REF(ops::upperFtoi<12>);
            case 0x17: return VU1AOT_REF(ops::upperFtoi<15>);
            case 0x1D: return VU1AOT_REF(ops::upperAbs);
            case 0x1F: return VU1AOT_REF(ops::upperClip);
            case 0x2E: return VU1AOT_REF(ops::upperCross<false>);
            default: return {};
            }
        }

        // ---- lower half ----

        OpRef nativeLower(uint32_t w)
        {
            switch ((w >> 25) & 0x7Fu)
            {
            case 0x00: return VU1AOT_REF(ops::lowerLq);
            case 0x01: return VU1AOT_REF(ops::lowerSq);
            case 0x04: return VU1AOT_REF(ops::lowerIlw);
            case 0x05: return VU1AOT_REF(ops::lowerIsw);
            case 0x08: return VU1AOT_REF(ops::lowerIaddiu);
            case 0x09: return VU1AOT_REF(ops::lowerIsubiu);
            case 0x10: return VU1AOT_REF(ops::lowerFceq);
            case 0x12: return VU1AOT_REF(ops::lowerFcand);
            case 0x13: return VU1AOT_REF(ops::lowerFcor);
            case 0x14: return VU1AOT_REF(ops::lowerFseq);
            case 0x16: return VU1AOT_REF(ops::lowerFsand);
            case 0x17: return VU1AOT_REF(ops::lowerFsor);
            case 0x18: return VU1AOT_REF(ops::lowerFmeq);
            case 0x1A: return VU1AOT_REF(ops::lowerFmand);
            case 0x1B: return VU1AOT_REF(ops::lowerFmor);
            case 0x1C: return VU1AOT_REF(ops::lowerFcget);
            case 0x20: return VU1AOT_REF(ops::lowerB);
            case 0x21: return VU1AOT_REF(ops::lowerBal);
            case 0x28: return VU1AOT_REF(ops::lowerIbeq);
            case 0x29: return VU1AOT_REF(ops::lowerIbne);
            case 0x2C: return VU1AOT_REF(ops::lowerIbltz);
            case 0x2D: return VU1AOT_REF(ops::lowerIbgtz);
            case 0x2E: return VU1AOT_REF(ops::lowerIblez);
            case 0x2F: return VU1AOT_REF(ops::lowerIbgez);
            case 0x40:
                break;
            default:
                return {};
            }
            switch (w & 0x3Fu)
            {
            case 0x30: return VU1AOT_REF(ops::lowerIadd);
            case 0x31: return VU1AOT_REF(ops::lowerIsub);
            case 0x32: return VU1AOT_REF(ops::lowerIaddi);
            case 0x34: return VU1AOT_REF(ops::lowerIand);
            case 0x35: return VU1AOT_REF(ops::lowerIor);
            default:
                break;
            }
            if ((w & 0x3Fu) < 0x3Cu)
                return {};
            switch ((w & 3u) | ((w >> 4) & 0x7Cu))
            {
            case 0x30: return VU1AOT_REF(ops::lowerMove);
            case 0x31: return VU1AOT_REF(ops::lowerMr32);
            case 0x34: return VU1AOT_REF(ops::lowerLqi);
            case 0x35: return VU1AOT_REF(ops::lowerSqi);
            case 0x36: return VU1AOT_REF(ops::lowerLqd);
            case 0x37: return VU1AOT_REF(ops::lowerSqd);
            case 0x38: return VU1AOT_REF(ops::lowerDiv);
            case 0x39: return VU1AOT_REF(ops::lowerSqrt);
            case 0x3A: return VU1AOT_REF(ops::lowerRsqrt);
            case 0x3C: return VU1AOT_REF(ops::lowerMtir);
            case 0x3D: return VU1AOT_REF(ops::lowerMfir);
            case 0x3E: return VU1AOT_REF(ops::lowerIlwr);
            case 0x3F: return VU1AOT_REF(ops::lowerIswr);
            case 0x64: return VU1AOT_REF(ops::lowerMfp);
            case 0x68: return VU1AOT_REF(ops::lowerXtop);
            case 0x69: return VU1AOT_REF(ops::lowerXitop);
            case 0x70: return VU1AOT_REF(ops::lowerEsadd);
            case 0x79: return VU1AOT_REF(ops::lowerErsqrt);
            default:
                return {};
            }
        }
#undef VU1AOT_REF

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
            OpRef ref;
            if (allowNative)
            {
                if (op < 0x3Cu)
                    ref = g_upper.main[op];
                else if (special < 0x30u)
                    ref = g_upper.special[special];
                if (ref.fn == nullptr)
                    ref = nativeUpperOther(op, special);
            }
            if (ref.fn != nullptr)
            {
                out.upper = ref.fn;
                out.upperName = ref.name;
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
            const OpRef ref = allowNative ? nativeLower(lower) : OpRef{};
            if (ref.fn != nullptr)
            {
                out.lower = ref.fn;
                out.lowerName = ref.name;
            }
            else
                out.lowerGeneral = true;
        }
        return out;
    }
}
