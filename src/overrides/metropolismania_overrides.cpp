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

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
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
        static uint32_t frames = 0, lastLogFrame = 0;
        static const auto start = std::chrono::steady_clock::now();
        static auto lastLog = start;
        const uint32_t self = getRegU32(ctx, 4);
        const uint32_t state = guestRead32(rdram, self + 4u);
        const uint32_t sceneA = guestRead32(rdram, self + 0xCu);
        const uint32_t sub = (state == 2u && sceneA != 0u) ? guestRead32(rdram, sceneA + 0xC0u) : 0xFFFFFFFFu;
        ++frames;
        if (state != lastState || sub != lastSub || (frames % 300u) == 0u)
        {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - start).count();
            const double span = std::chrono::duration<double>(now - lastLog).count();
            const double fps = span > 0.0 ? (frames - lastLogFrame) / span : 0.0;
            std::cerr << "[mm:scene] t=" << std::fixed << std::setprecision(1) << elapsed << "s frame=" << std::dec << frames
                      << " fps=" << fps << " state=" << state << " sceneA=0x" << std::hex << sceneA
                      << " sceneA.sub=" << std::dec << static_cast<int32_t>(sub) << std::endl;
            lastState = state;
            lastSub = sub;
            lastLog = now;
            lastLogFrame = frames;
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

    // ------------------------------------------------------------------ MMPC_SKIP_MOVIES=1
    // Test aid while PSS movie playback is incomplete: the movie functions return "played" at once.
    // mm_PlayMissionMovie(n) and mm_PlayOpeningMovie block until the movie ends and return 1.
    constexpr uint32_t kPlayMissionMovie = 0x0019DE10u;
    constexpr uint32_t kPlayOpeningMovie = 0x0019DFA0u;

    void skipMovie(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        std::cerr << "[mm] skipped movie (MMPC_SKIP_MOVIES=1), arg=" << getRegU32(ctx, 4) << std::endl;
        setReturnS32(ctx, 1);
        ctx->pc = getRegU32(ctx, 31);
    }

    void installMovieSkip(PS2Runtime &runtime)
    {
        const char *flag = std::getenv("MMPC_SKIP_MOVIES");
        if (!flag || std::strcmp(flag, "1") != 0)
            return;
        runtime.registerFunction(kPlayMissionMovie, skipMovie);
        runtime.registerFunction(kPlayOpeningMovie, skipMovie);
        std::cerr << "[mm] movies will be skipped (MMPC_SKIP_MOVIES=1)" << std::endl;
    }

    void applyMetropolismaniaOverrides(PS2Runtime &runtime)
    {
        installTrace(runtime);
        installMovieSkip(runtime);
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "metropolismania-us",
    "SLUS_203.77",
    kEntryPoint,
    kElfCrc32,
    applyMetropolismaniaOverrides);
