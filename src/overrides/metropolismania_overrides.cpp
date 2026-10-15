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

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace
{
    constexpr uint32_t kEntryPoint = 0x00100008u;
    constexpr uint32_t kElfCrc32 = 0x4FA3F727u;

    // Addresses in SLUS_203.77 (see docs/GAME_NOTES.md).
    constexpr uint32_t kGameUpdate = 0x0012ABA0u;   // game object vtable +0xC, called once per frame
    constexpr uint32_t kOpenDiscFile = 0x0019ED60u; // mm_OpenDiscFile(path, ...)

    // ------------------------------------------------------------------ trace mode (MMPC_TRACE=1)
    // Logs top-level game state changes and every file the game opens. Debug aid for boot triage;
    // off by default.

    uint32_t guestRead32(const uint8_t *rdram, uint32_t address)
    {
        uint32_t value = 0;
        std::memcpy(&value, rdram + (address & 0x01FFFFFCu), sizeof(value));
        return value;
    }

    std::string guestString(const uint8_t *rdram, uint32_t address, size_t maxLength = 128)
    {
        std::string out;
        for (size_t i = 0; i < maxLength; ++i)
        {
            const char ch = static_cast<char>(rdram[(address + i) & 0x01FFFFFFu]);
            if (ch == '\0')
                break;
            out.push_back(ch);
        }
        return out;
    }

    PS2Runtime::RecompiledFunction g_originalGameUpdate = nullptr;
    PS2Runtime::RecompiledFunction g_originalOpenDiscFile = nullptr;

    void tracedGameUpdate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // Game object layout (from FUN_0012aba0): +4 state, +8 scene C, +0xC scene A, +0x14 scene B.
        // Scene A keeps its sub-state at +0xC0 and a counter at +0xC4.
        static uint32_t lastState = 0xFFFFFFFFu, lastSub = 0xFFFFFFFFu;
        static uint32_t frames = 0;
        const uint32_t self = getRegU32(ctx, 4);
        const uint32_t state = guestRead32(rdram, self + 4u);
        const uint32_t sceneA = guestRead32(rdram, self + 0xCu);
        const uint32_t sub = (state == 2u && sceneA != 0u) ? guestRead32(rdram, sceneA + 0xC0u) : 0xFFFFFFFFu;
        if (state != lastState || sub != lastSub || (++frames % 600u) == 0u)
        {
            std::cerr << "[mm:scene] frame=" << std::dec << frames << " state=" << state
                      << " sceneA=0x" << std::hex << sceneA << " sceneA.sub=" << std::dec
                      << static_cast<int32_t>(sub) << std::endl;
            lastState = state;
            lastSub = sub;
        }
        g_originalGameUpdate(rdram, ctx, runtime);
    }

    void tracedOpenDiscFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::cerr << "[mm:file] open \"" << guestString(rdram, getRegU32(ctx, 4)) << "\"" << std::endl;
        g_originalOpenDiscFile(rdram, ctx, runtime);
    }

    void installTrace(PS2Runtime &runtime)
    {
        const char *flag = std::getenv("MMPC_TRACE");
        if (!flag || std::strcmp(flag, "1") != 0)
            return;
        g_originalGameUpdate = runtime.lookupFunction(kGameUpdate);
        g_originalOpenDiscFile = runtime.lookupFunction(kOpenDiscFile);
        runtime.registerFunction(kGameUpdate, tracedGameUpdate);
        runtime.registerFunction(kOpenDiscFile, tracedOpenDiscFile);
        std::cerr << "[mm] trace mode on (MMPC_TRACE=1)" << std::endl;
    }

    void applyMetropolismaniaOverrides(PS2Runtime &runtime)
    {
        installTrace(runtime);
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "metropolismania-us",
    "SLUS_203.77",
    kEntryPoint,
    kElfCrc32,
    applyMetropolismaniaOverrides);
