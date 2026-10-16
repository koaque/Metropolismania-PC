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
#include <map>
#include <mutex>
#include <string>
#include <utility>

#if defined(_WIN32)
// Keep the runtime's PS2X_PROFILE sampling profiler (patches/0108) in the link.
#pragma comment(linker, "/include:ps2x_sampling_profiler_anchor")
#endif

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

    // ------------------------------------------------------------------ MMPC_TRACE_GS=1
    // Logs the first double-buffer swaps: which buffer is displayed, where each context draws and
    // what the clear packet targets (sceGsDBuffDc layout: disp[2] 0x00, giftag0 0x50, draw01 0x60,
    // draw02 0xE0, clear0 0x160, giftag1 0x1C0, draw11 0x1D0, draw12 0x250, clear1 0x2D0).
    constexpr uint32_t kGsSwapDBuffDc = 0x00103D08u;
    PS2Runtime::RecompiledFunction g_originalSwapDBuffDc = nullptr;

    uint64_t guestRead64(const uint8_t *rdram, uint32_t address)
    {
        uint64_t value = 0;
        std::memcpy(&value, rdram + (address & 0x01FFFFF8u), sizeof(value));
        return value;
    }

    void tracedSwapDBuffDc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t calls = 0;
        const uint32_t db = getRegU32(ctx, 4);
        const uint32_t which = getRegU32(ctx, 5) & 1u;
        if (calls++ < 6u)
        {
            const uint32_t set = which ? 0x1D0u : 0x60u;
            const uint32_t clear = which ? 0x2D0u : 0x160u;
            const uint64_t dispfb = guestRead64(rdram, db + which * 40u + 16u);
            const uint64_t otherDispfb = guestRead64(rdram, db + (which ^ 1u) * 40u + 16u);
            const uint64_t frame1 = guestRead64(rdram, db + set);
            const uint64_t frame2 = guestRead64(rdram, db + set + 0x80u);
            const uint64_t xyoff1 = guestRead64(rdram, db + set + 0x20u);
            const uint64_t xyoff2 = guestRead64(rdram, db + set + 0x80u + 0x20u);
            const uint64_t prim = guestRead64(rdram, db + clear + 0x10u);
            const uint64_t xyzA = guestRead64(rdram, db + clear + 0x30u);
            const uint64_t xyzB = guestRead64(rdram, db + clear + 0x40u);
            std::cerr << std::hex << "[mm:gs] swap which=" << which << " display.fbp=0x" << (dispfb & 0x1FF)
                      << " (other disp.fbp=0x" << (otherDispfb & 0x1FF) << ") frame1.fbp=0x" << (frame1 & 0x1FF)
                      << " frame2.fbp=0x" << (frame2 & 0x1FF) << " xyoff1=0x" << xyoff1 << " xyoff2=0x" << xyoff2
                      << " clear.prim=0x" << prim << " ctxt=" << ((prim >> 9) & 1) << " xyzA=0x" << xyzA << " xyzB=0x" << xyzB
                      << std::dec << std::endl;
        }
        g_originalSwapDBuffDc(rdram, ctx, runtime);
    }

    void installGsTrace(PS2Runtime &runtime)
    {
        const char *flag = std::getenv("MMPC_TRACE_GS");
        if (!flag || std::strcmp(flag, "1") != 0)
            return;
        g_originalSwapDBuffDc = runtime.lookupFunction(kGsSwapDBuffDc);
        runtime.registerFunction(kGsSwapDBuffDc, tracedSwapDBuffDc);
    }

    // ------------------------------------------------------------------ MMPC_TRACE_CALLS=1
    // Every 2 s, logs how often a few frame-loop functions ran, by caller ($ra), and the host time
    // spent inside them. The runtime processes DMA kicks (VIF1, VU1, GIF) synchronously, so the
    // time inside the VIF1 kick and sceGsSyncPath shows where a game frame goes.
    struct TracedCall
    {
        uint32_t address;
        const char *name;
        PS2Runtime::RecompiledFunction original;
    };
    TracedCall g_tracedCalls[] = {
        {0x00187290u, "mm_KickFrameVif1", nullptr},
        {0x0012ABA0u, "mm_GameUpdate", nullptr},
        {0x00103418u, "sceGsSyncPath", nullptr},
        {0x00103380u, "sceGsSyncV", nullptr},
        {0x00103D08u, "sceGsSwapDBuffDc", nullptr},
        {0x0019C4C0u, "mm_VSyncHandler", nullptr},
        {0x001040F0u, "sceDmaSend", nullptr},
        {0x0019C480u, "mm_MovieDmacRelease", nullptr},
        {0x001A0960u, "mm_MovieBufferFree", nullptr},
        {0x0010BF90u, "sceMpegGetPicture", nullptr},
        {0x0019E060u, "mm_MovieYield", nullptr},
        {0x0010DC10u, "RotateThreadReadyQueue", nullptr},
    };
    constexpr size_t kTracedCallCount = sizeof(g_tracedCalls) / sizeof(g_tracedCalls[0]);

    struct CallStats
    {
        uint64_t calls = 0;
        double seconds = 0.0;
    };

    void reportCalls(size_t index, uint32_t caller, double seconds)
    {
        static std::mutex mutex;
        static std::map<std::pair<size_t, uint32_t>, CallStats> stats;
        static auto lastReport = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex);
        CallStats &entry = stats[{index, caller}];
        ++entry.calls;
        entry.seconds += seconds;
        const auto now = std::chrono::steady_clock::now();
        if (now - lastReport < std::chrono::seconds(2))
            return;
        std::cerr << "[mm:calls] last " << std::fixed << std::setprecision(1)
                  << std::chrono::duration<double>(now - lastReport).count() << " s:";
        for (const auto &[key, value] : stats)
            std::cerr << " " << g_tracedCalls[key.first].name << "<-0x" << std::hex << key.second << std::dec
                      << " n=" << value.calls << " ms=" << static_cast<uint64_t>(value.seconds * 1000.0) << ";";
        std::cerr << std::endl;
        stats.clear();
        lastReport = now;
    }

    template <size_t Index>
    void tracedCall(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t caller = getRegU32(ctx, 31);
        const auto start = std::chrono::steady_clock::now();
        g_tracedCalls[Index].original(rdram, ctx, runtime);
        reportCalls(Index, caller, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    }

    template <size_t... Indices>
    void installTracedCalls(PS2Runtime &runtime, std::index_sequence<Indices...>)
    {
        ((g_tracedCalls[Indices].original = runtime.lookupFunction(g_tracedCalls[Indices].address)), ...);
        ((g_tracedCalls[Indices].original ? static_cast<void>(runtime.registerFunction(g_tracedCalls[Indices].address, tracedCall<Indices>)) : void()), ...);
    }

    void installCallTrace(PS2Runtime &runtime)
    {
        const char *flag = std::getenv("MMPC_TRACE_CALLS");
        if (!flag || std::strcmp(flag, "1") != 0)
            return;
        installTracedCalls(runtime, std::make_index_sequence<kTracedCallCount>{});
        std::cerr << "[mm] call trace on (MMPC_TRACE_CALLS=1)" << std::endl;
    }

    void applyMetropolismaniaOverrides(PS2Runtime &runtime)
    {
        installTrace(runtime);
        installMovieSkip(runtime);
        installGsTrace(runtime);
        installCallTrace(runtime);
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "metropolismania-us",
    "SLUS_203.77",
    kEntryPoint,
    kElfCrc32,
    applyMetropolismaniaOverrides);
