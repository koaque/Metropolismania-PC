// Behavioral implementation: tempo in BPM <-> microseconds per quarter note.
// Layout facts are confined to this adapter; no SDK headers or decompiled bodies.
#include "modmidi_tempo.h"

#include <algorithm>

namespace iop::midi
{
    namespace
    {
        constexpr uint32_t kRamSize = 2u * 1024u * 1024u;
        constexpr uint32_t kMicrosecondsPerMinute = 60'000'000u;
        constexpr uint32_t kPrivateState = 0x54;
        constexpr uint32_t kTickStep = 0x20;
        constexpr uint32_t kTimingDivisor = 0x24;
        constexpr uint32_t kQuarterNotePeriod = 0x28;
        constexpr uint32_t kTempoScale = 0x2C;

        bool validStack(uint32_t stackPointer)
        {
            const uint32_t physical = stackPointer & 0x1FFFFFFFu;
            return (physical & 3u) == 0 && physical >= 32u && physical <= kRamSize;
        }

        bool wordInRam(uint32_t address, uint32_t stackPointer)
        {
            const uint32_t physical = address & 0x1FFFFFFFu;
            const uint32_t stack = stackPointer & 0x1FFFFFFFu;
            // The original writes its frame before reading the descriptors. If
            // they alias that frame, let it execute those writes in order.
            return (physical & 3u) == 0 && physical <= kRamSize - 4 &&
                   !(physical < stack && physical + 4 > stack - 32u);
        }

        uint32_t readWord(const uint8_t *ram, uint32_t address)
        {
            const uint8_t *p = ram + (address & 0x1FFFFFFFu);
            return uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
        }

        void writeWord(uint8_t *ram, uint32_t address, uint32_t value)
        {
            uint8_t *p = ram + (address & 0x1FFFFFFFu);
            for (unsigned byte = 0; byte < 4; ++byte)
                p[byte] = static_cast<uint8_t>(value >> (byte * 8));
        }

        enum class Lookup { Found, NullContext, MissingPort, Original };
        struct PortState
        {
            Lookup lookup = Lookup::Original;
            uint32_t environment = 0;
        };

        PortState lookupPort(const uint8_t *ram, uint32_t context, uint32_t port, uint32_t stackPointer)
        {
            if (!validStack(stackPointer))
                return {};
            if (context == 0)
                return {Lookup::NullContext};
            if (!wordInRam(context + 4, stackPointer))
                return {};
            const uint32_t group = readWord(ram, context + 4);
            if (!wordInRam(group, stackPointer))
                return {};
            // Each port uses two descriptors; the odd one holds its environment.
            // Guest index arithmetic intentionally wraps at 32 bits.
            const uint32_t index = port * 2u + 1u;
            if (index >= readWord(ram, group))
                return {Lookup::MissingPort};
            if (!wordInRam(group + 4, stackPointer))
                return {};
            const uint32_t descriptor = readWord(ram, group + 4) + index * 8u;
            if (!wordInRam(descriptor + 4, stackPointer))
                return {};
            return {Lookup::Found, readWord(ram, descriptor + 4)};
        }
    }

    TempoResult getAbsoluteTempo(const uint8_t *ram, uint32_t context, uint32_t port, uint32_t stackPointer)
    {
        const PortState state = lookupPort(ram, context, port, stackPointer);
        if (state.lookup == Lookup::NullContext)
            return {true, 0, 15};
        if (state.lookup == Lookup::MissingPort)
            return {true, 0, 22};
        if (state.lookup != Lookup::Found || !wordInRam(state.environment + 4, stackPointer))
            return {};
        if (readWord(ram, state.environment + 4) == 0xFFFFFFFFu)
            return {true, 0, 35}; // No sequence selected.
        const uint32_t periodAddress = state.environment + kPrivateState + kQuarterNotePeriod;
        if (!wordInRam(periodAddress, stackPointer))
            return {};
        const uint32_t period = readWord(ram, periodAddress);
        if (period == 0)
            return {true, 0, 43};
        const uint32_t bpm = kMicrosecondsPerMinute / period;
        const uint32_t cost = bpm < 20 ? 51u : (bpm < 256 ? 50u : 52u);
        return {true, std::clamp(bpm, 20u, 255u), cost};
    }

    TempoResult setAbsoluteTempo(uint8_t *ram, uint32_t context, uint32_t port, uint32_t tempo, uint32_t stackPointer)
    {
        const PortState state = lookupPort(ram, context, port, stackPointer);
        if (state.lookup == Lookup::NullContext)
            return {true, 0xFFFFFFFFu, 18};
        if (state.lookup == Lookup::MissingPort)
            return {true, 0xFFFFFFFFu, 25};
        if (state.lookup != Lookup::Found)
            return {};
        const uint32_t system = state.environment + kPrivateState;
        if (!wordInRam(system + kTimingDivisor, stackPointer) || !wordInRam(system + kTempoScale, stackPointer) ||
            !wordInRam(system + kQuarterNotePeriod, stackPointer) || !wordInRam(system + kTickStep, stackPointer))
            return {};
        const uint32_t bpm = tempo & 0xFFu;
        const uint32_t divisor = readWord(ram, system + kTimingDivisor);
        const uint32_t scale = readWord(ram, system + kTempoScale);
        if (bpm == 0 || divisor == 0 || scale == 0)
            return {}; // Let the original raise its division exception, including partial writes.
        const uint32_t period = kMicrosecondsPerMinute / bpm;
        // These are fixed-width stages, not a single wide rational expression.
        const uint32_t tickStep = (((period << 7) / divisor) << 8) / scale;
        writeWord(ram, system + kQuarterNotePeriod, period);
        writeWord(ram, system + kTickStep, tickStep);
        return {true, 0, 54};
    }
}
