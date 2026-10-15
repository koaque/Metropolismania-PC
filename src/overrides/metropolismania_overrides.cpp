// Game-specific runtime overrides for Metropolismania (USA), SLUS-20377, disc VER 1.02.
//
// tools/pipeline.py stage copies this file into the runtime's src/runner/ folder, where the
// upstream CMake glob compiles it into ps2EntryRunner. The descriptor below only matches the
// supported build (ELF name, entry point and file CRC32), so it never touches other games.
//
// Prefer config/metropolismania.template.toml (handler@0xADDR) for simple bindings. Use this
// file when a binding needs custom logic. Remember that many handlers do not advance ctx->pc:
// wrap them with registerFunction and fall back to $ra, as in the upstream walkthrough.

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"
#include "ps2_syscalls.h"

namespace
{
    constexpr uint32_t kEntryPoint = 0x00100008u;
    constexpr uint32_t kElfCrc32 = 0x4FA3F727u;

    void applyMetropolismaniaOverrides(PS2Runtime &runtime)
    {
        (void)runtime;
        // No overrides yet. Boot triage (milestone M4) adds them here.
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "metropolismania-us",
    "SLUS_203.77",
    kEntryPoint,
    kElfCrc32,
    applyMetropolismaniaOverrides);
