// Pre-timed VU1 programs: the compiled form a run executes (plan P4, M3.5). See vu1aot.h.
//
// A graph node becomes an XNode: the two halves of the pair as function pointers with their instruction words, the
// results that commit before it, and where it goes next. The functions work on a Ctx (the interpreter's state plus the
// program's slots for queued results) and are the interpreter's own semantics, written once without its queues
// (vu1aot_ops.cpp); what is not written natively goes through the interpreter's own execUpper/execLower
// (vu1aot_exec.cpp), which stays the reference.

#pragma once

#include "vu1aot.h"

#include "runtime/ps2_vu1.h"

#include <vector>

namespace vu1aot
{
    struct FlagData
    {
        uint32_t mac, status, extra, clip;
    };

    struct Ctx
    {
        VU1State *st = nullptr;
        uint8_t *data = nullptr; // the 16 KB VU1 data memory
        FlagData ring[kMaxFlags]{};
        uint32_t head = 0; // ring position of the oldest queued flag result
        float qValue = 0.0f;
        uint32_t qStatusDi = 0;
        float pValue[kEfuSlots]{};
        uint32_t *workingClip = nullptr; // the interpreter's m_workingClip (CLIP shifts a result into it)
        int32_t oldVi = 0;      // the VI the pair being run writes, as it was before
        int32_t backupValue = 0; // the value a branch reads in place of the VI the pair before it wrote
        bool taken = false;     // the branch just run was taken
        bool failed = false;    // an op met something it cannot do; the run bails
        void *vu = nullptr;     // the interpreter, for the ops that go through it
        const void *run = nullptr;
    };

    struct XNode;
    using OpFn = void (*)(Ctx &, const XNode &);

    enum PairForm : uint8_t
    {
        FormNormal, // upper, then lower
        FormIBit,   // upper, then I = iValue
        FormShadow  // the lower half reads a vf the upper half writes: it sees the old value
    };

    enum XFlags : uint8_t
    {
        XfXgkick = 1,      // the lower half is XGKICK
        XfResetZero = 2,   // an interpreter function ran: vf0 and vi0 are put back
        XfBackward = 4,    // DelaySlot: the branch goes to a lower or equal pc
        XfAlways = 8,      // DelaySlot: the branch is always taken
        XfInDelaySlot = 16 // this pair is a delay slot (its target is `target`)
    };

    struct XNode
    {
        OpFn upper = nullptr, lower = nullptr;
        uint32_t upperWord = 0, lowerWord = 0;
        float iValue = 0.0f;
        uint32_t commitStart = 0;
        uint32_t endCommitStart = 0;
        int32_t next = -1, nextTaken = -1;
        uint16_t pc = 0;
        uint16_t advance = 0;
        uint16_t target = 0;
        uint16_t endExtra = 0;
        uint8_t commitCount = 0;
        uint8_t endCommitCount = 0;
        uint8_t flagBase = 0;        // ring position of the first flag result this pair queues
        uint8_t lowerFlagBase = 0;   // and of the lower half's (after the upper half's)
        uint8_t upperPushes = 0, lowerPushes = 0;
        uint8_t kind = 0; // NodeKind
        uint8_t form = FormNormal;
        uint8_t shadowReg = 0;
        uint8_t writtenVi = 0;
        uint8_t recordBackup = 0; // the pair writes a VI a branch right after it reads the old value of
        uint8_t readBackupReg = 0;
        uint8_t backupAfterTaken = 0, backupAfterFall = 0;
        uint8_t efuSlot = 0xFF;
        uint8_t flags = 0;
    };

    // The native ops, and the choice between them and the interpreter's own.
    struct Natives
    {
        OpFn upper = nullptr;  // null: nothing to do, or `general`
        OpFn lower = nullptr;
        const char *upperName = nullptr; // the same ops by name, in namespace vu1aot (vu1aot_ops.h), for a compiled program
        const char *lowerName = nullptr;
        bool upperGeneral = false; // go through execUpper
        bool lowerGeneral = false; // go through execLower
        bool upperWritesVf0 = false; // a native upper op whose result goes to vf0 (put back after the pair)
    };
    Natives chooseOps(uint32_t upper, uint32_t lower, bool iBit, bool lowerNop, const Node &node, bool allowNative);
}
