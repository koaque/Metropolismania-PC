// Calling convention glue between ported C++ functions and the guest (recompiled) code.
//
// PORT_FN(Name) registers mm::Name, a ported function, in place of the generated function at the
// same address (game_functions.h has the address and the prototype). Every caller, generated or
// ported, direct or through a function pointer, then reaches the C++ version.
//
// port::callGuest<R>(address, args...) calls a function that is not ported yet through the
// runtime's function table. game_functions.h wraps it in an inline function per unported game
// function, so ported code just calls Family_InCity(family) and the like.
//
// Guest ABI (R5900, as used by this game): integer and pointer arguments in a0-a3, t0-t3, then in
// 8-byte stack slots from sp+0; float arguments in f12-f19; results in v0 (32-bit values sign-extended
// to 64 bits) or f0. Ported code never blocks or yields: the runtime's preemption checkpoints are off
// while it runs (patches/0129), until guest threads get their own host stacks (plan P6).

#pragma once

#include "guest.h"
#include "port.h"
#include "runtime/ee_scheduler.h"

#include <array>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

namespace port
{
    using GuestFn = PS2Runtime::RecompiledFunction;

    // The guest call that the innermost ported function was entered with.
    struct CallState
    {
        uint8_t *rdram = nullptr;
        R5900Context *ctx = nullptr;
        PS2Runtime *runtime = nullptr;
        uint32_t function = 0; // address of the innermost ported function, for messages
    };
    inline CallState g_call;

    void registerPorted(uint32_t address, const char *name, GuestFn wrapper, ResultKind result);
    void enterPorted(PS2Runtime *runtime);
    void leavePorted(PS2Runtime *runtime);
    [[noreturn]] void reachedThreadSwitch(uint32_t address);

    // Active for the duration of a ported function called from the guest.
    class PortedScope
    {
    public:
        PortedScope(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t function) : m_outer(g_call)
        {
            g_call = {rdram, ctx, runtime, function};
            enterPorted(runtime);
        }
        ~PortedScope()
        {
            leavePorted(g_call.runtime);
            g_call = m_outer;
        }
        PortedScope(const PortedScope &) = delete;
        PortedScope &operator=(const PortedScope &) = delete;

    private:
        CallState m_outer;
    };

    struct AutoRegister
    {
        AutoRegister(uint32_t address, const char *name, GuestFn wrapper, ResultKind result)
        {
            registerPorted(address, name, wrapper, result);
        }
    };

    template <typename T>
    inline constexpr bool kFloatArg = std::is_same_v<T, float> || std::is_same_v<T, mm::ps2f>;

    // For each parameter, its index among the integer or among the float parameters.
    template <typename... Args>
    constexpr std::array<int, sizeof...(Args)> argSlots()
    {
        std::array<int, sizeof...(Args)> slots{};
        int ints = 0, floats = 0, k = 0;
        ((slots[k++] = kFloatArg<Args> ? floats++ : ints++), ...);
        return slots;
    }

    template <typename... Args>
    constexpr int intArgCount() { return ((kFloatArg<Args> ? 0 : 1) + ... + 0); }

    inline uint64_t gpr64(const R5900Context *ctx, int reg)
    {
        return static_cast<uint64_t>(_mm_cvtsi128_si64(ctx->r[reg]));
    }

    inline void setGpr64(R5900Context *ctx, int reg, uint64_t value)
    {
        ctx->r[reg] = _mm_set_epi64x(0, static_cast<int64_t>(value));
    }

    // Integer-class argument k: a0-a3, t0-t3 (GPR 4-11), then the caller's stack slots.
    inline uint64_t intArg(const R5900Context *ctx, int k)
    {
        if (k < 8)
            return gpr64(ctx, 4 + k);
        return load<uint64_t>(static_cast<uint32_t>(gpr64(ctx, 29)) + 8u * static_cast<uint32_t>(k - 8));
    }

    template <typename T>
    T fromGuest(const R5900Context *ctx, int slot)
    {
        if constexpr (kFloatArg<T>)
        {
            // f12-f19; no function in this game takes more than eight float arguments.
            return T(ctx->f[12 + slot]);
        }
        else if constexpr (mm::kIsGuestPtr<T>)
            return T{static_cast<uint32_t>(intArg(ctx, slot))};
        else
        {
            static_assert(std::is_integral_v<T>, "unsupported parameter type (pointers are mm::GuestPtr)");
            return static_cast<T>(intArg(ctx, slot));
        }
    }

    template <typename T>
    uint64_t toGuestInt(T value)
    {
        if constexpr (mm::kIsGuestPtr<T>)
            return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(value.address)));
        else if constexpr (sizeof(T) <= 4)
            return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(value)));
        else
            return static_cast<uint64_t>(value);
    }

    template <typename R>
    void setResult(R5900Context *ctx, R value)
    {
        if constexpr (kFloatArg<R>)
            ctx->f[0] = static_cast<float>(value);
        else
            setGpr64(ctx, 2, toGuestInt(value));
    }

    template <typename R>
    R getResult(const R5900Context *ctx)
    {
        if constexpr (std::is_void_v<R>)
            return;
        else if constexpr (kFloatArg<R>)
            return R(ctx->f[0]);
        else if constexpr (mm::kIsGuestPtr<R>)
            return R{static_cast<uint32_t>(gpr64(ctx, 2))};
        else
            return static_cast<R>(gpr64(ctx, 2));
    }

    template <auto Fn>
    struct Wrapper;

    template <typename R, typename... Args, R (*Fn)(Args...)>
    struct Wrapper<Fn>
    {
        static constexpr ResultKind kResult = std::is_void_v<R> ? ResultKind::None
                                              : kFloatArg<R>    ? ResultKind::Float
                                                                : ResultKind::Int;

        template <size_t... I>
        static R invoke(const R5900Context *ctx, std::index_sequence<I...>)
        {
            constexpr auto slots = argSlots<Args...>();
            return Fn(fromGuest<Args>(ctx, slots[I])...);
        }

        static void entry(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t address = ctx->pc;
            PortedScope scope(rdram, ctx, runtime, address);
            try
            {
                if constexpr (std::is_void_v<R>)
                    invoke(ctx, std::index_sequence_for<Args...>{});
                else
                    setResult<R>(ctx, invoke(ctx, std::index_sequence_for<Args...>{}));
            }
            catch (const EeDispatcherTransfer &)
            {
                // A thread switch would drop this C++ frame and with it the rest of the function.
                reachedThreadSwitch(address);
            }
            ctx->pc = static_cast<uint32_t>(gpr64(ctx, 31));
        }
    };

    // Return address given to guest callees; never a real code address.
    inline constexpr uint32_t kReturnSentinel = 0x00000FF0u;

    [[noreturn]] void calleeDidNotReturn(uint32_t address, uint32_t pc);

    template <typename R, typename... Args>
    R callGuest(uint32_t address, Args... args)
    {
        R5900Context *ctx = g_call.ctx;
        if (ctx == nullptr)
            fatal("callGuest outside a ported function", address);
        constexpr int ints = intArgCount<Args...>();
        constexpr auto slots = argSlots<Args...>();
        const uint64_t savedRa = gpr64(ctx, 31);
        const uint64_t savedSp = gpr64(ctx, 29);
        // Stack arguments go in a new 16-byte aligned area below the current guest stack pointer.
        constexpr uint32_t stackBytes = ints > 8 ? ((8u * static_cast<uint32_t>(ints - 8)) + 15u) & ~15u : 0u;
        const uint32_t sp = static_cast<uint32_t>(savedSp) - stackBytes;
        if constexpr (stackBytes != 0)
            setGpr64(ctx, 29, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(sp))));
        int k = 0;
        (
            [&](auto value)
            {
                using T = decltype(value);
                const int slot = slots[k++];
                if constexpr (kFloatArg<T>)
                    ctx->f[12 + slot] = static_cast<float>(value);
                else if (slot < 8)
                    setGpr64(ctx, 4 + slot, toGuestInt(value));
                else
                    store<uint64_t>(sp + 8u * static_cast<uint32_t>(slot - 8), toGuestInt(value));
            }(args),
            ...);
        setGpr64(ctx, 31, kReturnSentinel);
        ctx->pc = address;
        GuestFn fn = g_call.runtime->lookupFunction(address);
        fn(g_call.rdram, ctx, g_call.runtime);
        // Runtime handlers that do not advance pc count as returned, as in dispatchGuestBranch.
        if (ctx->pc != kReturnSentinel && ctx->pc != address)
            calleeDidNotReturn(address, ctx->pc);
        setGpr64(ctx, 31, savedRa);
        setGpr64(ctx, 29, savedSp);
        return getResult<R>(ctx);
    }
}

#define PORT_JOIN2(a, b) a##b
#define PORT_JOIN(a, b) PORT_JOIN2(a, b)

// PORT_FN(Name): mm::Name replaces the generated function at mm::addr::Name.
#define PORT_FN(NAME)                                                                           \
    static const port::AutoRegister PORT_JOIN(kPortRegistration_, __COUNTER__)(                    \
        mm::addr::NAME, #NAME, &port::Wrapper<&mm::NAME>::entry, port::Wrapper<&mm::NAME>::kResult)
