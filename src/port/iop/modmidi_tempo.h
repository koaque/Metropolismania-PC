// Optional, independently expressed MIDI tempo services. See docs/IOP_SOUND_PILOT.md.
#pragma once

#include <cstdint>

namespace ps2x::iop
{
    class NativeContext;
    class IopSubsystem;
}

namespace iop::midi
{
    struct TempoResult
    {
        bool handled = false;
        uint32_t value = 0;
        uint32_t instructions = 0;
    };

    // RAM is the IOP's 2 MiB backing store. Unsupported addresses/divisors return
    // handled=false without modifying RAM, so the original can perform its faults.
    TempoResult getAbsoluteTempo(const uint8_t *ram, uint32_t context, uint32_t port, uint32_t stackPointer);
    TempoResult setAbsoluteTempo(uint8_t *ram, uint32_t context, uint32_t port, uint32_t tempo, uint32_t stackPointer);

    void getAbsoluteTempoNative(ps2x::iop::NativeContext &context);
    void setAbsoluteTempoNative(ps2x::iop::NativeContext &context);
    void installTempoPilot(ps2x::iop::IopSubsystem &subsystem);

    // Identity and entry offsets for the local USA-disc MODMIDI module only.
    inline constexpr uint32_t kModuleBase = 0x49900;
    inline constexpr uint32_t kModuleImageSize = 15348;
    inline constexpr uint64_t kModuleImageHash = 0x3A298BC1E83A4D7Full;
    inline constexpr uint32_t kSetTempoOffset = 0x1F78;
    inline constexpr uint32_t kGetTempoOffset = 0x1FDC;
}
