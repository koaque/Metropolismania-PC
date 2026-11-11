#include "modmidi_tempo.h"
#include "ps2x/iop/iop_native.h"

namespace iop::midi
{
    namespace
    {
        void finish(ps2x::iop::NativeContext &context, TempoResult result, uint32_t entryOffset)
        {
            if (result.handled)
            {
                context.setReg(2, result.value);
                context.charge(result.instructions);
                return;
            }
            // Both fingerprinted entries begin by reserving a 32-byte stack frame.
            // Resume after that single instruction so the replacement is not entered
            // recursively. No other registers or memory have been changed.
            context.setReg(29, context.reg(29) - 32u);
            context.charge(1);
            context.jump(kModuleBase + entryOffset + 4u);
        }
    }

    void getAbsoluteTempoNative(ps2x::iop::NativeContext &context)
    {
        finish(context, getAbsoluteTempo(context.ram(), context.reg(4), context.reg(5), context.reg(29)), kGetTempoOffset);
    }

    void setAbsoluteTempoNative(ps2x::iop::NativeContext &context)
    {
        finish(context, setAbsoluteTempo(context.ram(), context.reg(4), context.reg(5), context.reg(6), context.reg(29)), kSetTempoOffset);
    }
}
