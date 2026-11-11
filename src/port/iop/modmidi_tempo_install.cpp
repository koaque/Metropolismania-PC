#include "modmidi_tempo.h"
#include "ps2x/iop/iop_subsystem.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace iop::midi
{
    void installTempoPilot(ps2x::iop::IopSubsystem &subsystem)
    {
        const char *enabled = std::getenv("MMPC_IOP_MIDI_TEMPO");
        if (enabled == nullptr || std::strcmp(enabled, "1") != 0)
            return;
        subsystem.registerNativeModule("MODMIDI.IRX", kModuleImageHash, kModuleImageSize);
        // Costs are path-dependent and charged by the entry points themselves.
        subsystem.registerNative("MODMIDI.IRX", kGetTempoOffset, getAbsoluteTempoNative,
                                 "Midi_GetAbsoluteTempo", 0, true);
        subsystem.registerNative("MODMIDI.IRX", kSetTempoOffset, setAbsoluteTempoNative,
                                 "Midi_SetAbsoluteTempo", 0, true);
        std::fprintf(stderr, "[iop:midi] optional tempo pilot registered; original module fingerprint required\n");
    }
}
