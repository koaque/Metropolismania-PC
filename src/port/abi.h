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
// to 64 bits) or f0. The runtime's preemption checkpoints are off while ported code runs (patches/0129), so a
// thread switch only happens where the code waits. A ported function that can reach one (mm::blocking::at in
// game_functions.h) runs on a fiber (patches/0137, plan P6): callGuest parks the fiber when a switch unwinds the
// guest code it called, and the thread carries on in the same C++ frames when it is resumed.

#pragma once

#include "guest.h"
#include "port.h"
#include "runtime/ee_scheduler.h"

#include <array>
#include <cstdio>
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
        uint32_t frameBottom = 0; // sp of the GuestFrame in force, so callGuest puts stack arguments inside it
    };
    inline CallState g_call;

    // g_call belongs to the code running now; a fiber has its own, swapped in and out as the scheduler switches
    // (Scheduler hooks, port.cpp). GuestFiber::user holds the fiber's state and then the dispatcher's.
    static_assert(2 * sizeof(CallState) <= sizeof(GuestFiber::user), "CallState does not fit in GuestFiber::user");
    void fiberEnter(GuestFiber &fiber);
    void fiberLeave(GuestFiber &fiber);

    // A 32-bit read of a hardware register (a timer, a DMA channel), made through the runtime as the original
    // makes it.
    inline uint32_t readHardware32(uint32_t address) { return g_call.runtime->Load32(g_call.rdram, g_call.ctx, address); }
    // The 64-bit read (the GS privileged registers, such as CSR, are read this way).
    inline uint64_t readHardware64(uint32_t address) { return g_call.runtime->Load64(g_call.rdram, g_call.ctx, address); }
    // The matching 32-bit write (a DMA channel's registers start the transfer).
    inline void writeHardware32(uint32_t address, uint32_t value) { g_call.runtime->Store32(g_call.rdram, g_call.ctx, address, value); }
    // The time a loop of the original takes, in EE cycles, charged to the scheduler for a loop the port runs natively. The scheduler
    // drives the timers and the IOP (a module starting, an RPC server registering) from the cycles it is told of, so a polling or
    // delay loop that never accounts any waits for ever on what only time can bring. The generated code accounts 32 cycles at
    // every checkpoint (a loop's back branch).
    inline void accountCycles(uint32_t cycles) { g_call.runtime->eeScheduler().accountCycles(cycles); }

    void registerPorted(uint32_t address, const char *name, GuestFn wrapper, ResultKind result, MutateFn mutate);
    void enterPorted(PS2Runtime *runtime);
    void leavePorted(PS2Runtime *runtime);
    [[noreturn]] void reachedThreadSwitch(uint32_t address);

    // Active for the duration of a ported function called from the guest.
    class PortedScope
    {
    public:
        PortedScope(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t function)
            : m_outer(g_call), m_preemptible(runtime->eeScheduler().onFiber())
        {
            g_call = {rdram, ctx, runtime, function};
            // On a fiber the function may be switched out, so the no-preempt rule (which keeps checkpoints from firing
            // while ported code runs) does not apply: checkpoints fire in the guest code it calls and callGuest yields.
            if (!m_preemptible)
                enterPorted(runtime);
        }
        ~PortedScope()
        {
            if (!m_preemptible)
                leavePorted(g_call.runtime);
            g_call = m_outer;
        }
        PortedScope(const PortedScope &) = delete;
        PortedScope &operator=(const PortedScope &) = delete;

    private:
        CallState m_outer;
        bool m_preemptible;
    };

    struct AutoRegister
    {
        AutoRegister(uint32_t address, const char *name, GuestFn wrapper, ResultKind result, MutateFn mutate)
        {
            registerPorted(address, name, wrapper, result, mutate);
        }
    };

    // Difftest fuzzing (port.cpp): new values for one float argument, or for one integer argument within
    // its type's range (bytes 1, 2, 4 or 8, signed or not). Zero comes up often, to reach the special cases.
    void fuzzFloat(float &value, uint32_t &state);
    int64_t fuzzInt(int64_t value, int bytes, bool isSigned, uint32_t &state);

    // A 128-bit argument (the GIF tag a packet builder passes whole): both halves of the guest register.
    struct Quad
    {
        uint64_t low;
        uint64_t high;
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

    // Sets the guest v0 as a 32-bit instruction would (sign-extended into the low 64 bits, upper half kept).
    // Some game functions leave v0 as they found it on some paths and their callers pass it on: ported code
    // that calls one sets v0 first to what the original had in it at that point.
    inline void setGuestV0(int32_t value)
    {
        const int64_t low = value;
        std::memcpy(&g_call.ctx->r[2], &low, sizeof(low));
    }
    // The guest v0 as the function found it on entry (for paths that return it unchanged).
    inline int32_t guestV0() { return static_cast<int32_t>(gpr64(g_call.ctx, 2)); }
    // All 64 bits of it, for a function whose result is declared 64-bit.
    inline int64_t guestV0Wide() { return static_cast<int64_t>(gpr64(g_call.ctx, 2)); }
    // The caller's s0, which a few game functions hand back as their result on paths that never set v0.
    inline int32_t guestS0() { return static_cast<int32_t>(gpr64(g_call.ctx, 16)); }
    // Sets s0 for a ported callee that hands the caller's s0 back (mm_MsgWin_Update); the caller restores its own afterwards.
    inline void setGuestS0(int32_t value)
    {
        const int64_t low = value;
        std::memcpy(&g_call.ctx->r[16], &low, sizeof(low));
    }
    // The caller's s2, handed on by a path where the function never sets it.
    inline uint32_t guestS2() { return static_cast<uint32_t>(gpr64(g_call.ctx, 18)); }
    // The caller's s1, for a path where the function leaves it unset (the decompiler shows it as unaffected).
    inline int32_t guestS1() { return static_cast<int32_t>(gpr64(g_call.ctx, 17)); }
    // The caller's a3 (for a function with fewer arguments whose callee is handed a3 as it came in).
    inline uint32_t guestA3() { return static_cast<uint32_t>(gpr64(g_call.ctx, 7)); }

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

    template <auto Fn, bool Blocking>
    struct Wrapper;

    template <typename R, typename... Args, R (*Fn)(Args...), bool Blocking>
    struct Wrapper<Fn, Blocking>
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

        // Fuzzing touches float and plain integer arguments passed in registers; pointers keep their
        // recorded values, so the call still works on real game data.
        static constexpr bool kFuzzable =
            ((kFloatArg<Args> || (std::is_integral_v<Args> && !mm::kIsGuestPtr<Args>)) || ... || false);

        template <typename T>
        static void mutateArg(R5900Context *ctx, int slot, uint32_t &state)
        {
            if constexpr (kFloatArg<T>)
                fuzzFloat(ctx->f[12 + slot], state);
            else if constexpr (!mm::kIsGuestPtr<T>)
            {
                if (slot >= 8)
                    return;
                const int64_t value = fuzzInt(static_cast<int64_t>(gpr64(ctx, 4 + slot)), static_cast<int>(sizeof(T)),
                                              std::is_signed_v<T>, state);
                setGpr64(ctx, 4 + slot, toGuestInt(static_cast<T>(value)));
            }
        }

        template <size_t... I>
        static void mutateAll(R5900Context *ctx, uint32_t &state, std::index_sequence<I...>)
        {
            constexpr auto slots = argSlots<Args...>();
            (mutateArg<Args>(ctx, slots[I], state), ...);
        }

        static void mutate(R5900Context *ctx, uint32_t seed)
        {
            uint32_t state = seed ? seed : 1u;
            mutateAll(ctx, state, std::index_sequence_for<Args...>{});
        }

        static void run(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t address = ctx->pc;
            PortedScope scope(rdram, ctx, runtime, address);
            const bool traced = traceRequested(address);
            if (traced)
                std::fprintf(stderr, "[port:trace] enter %s\n", nameOf(address));
            try
            {
                if constexpr (std::is_void_v<R>)
                    invoke(ctx, std::index_sequence_for<Args...>{});
                else
                    setResult<R>(ctx, invoke(ctx, std::index_sequence_for<Args...>{}));
                if (traced)
                    std::fprintf(stderr, "[port:trace] leave %s\n", nameOf(address));
            }
            catch (const EeDispatcherTransfer &)
            {
                // A thread switch would drop this C++ frame and with it the rest of the function.
                reachedThreadSwitch(address);
            }
            ctx->pc = static_cast<uint32_t>(gpr64(ctx, 31));
        }

        static void entry(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            if constexpr (Blocking)
            {
                EeScheduler &ee = runtime->eeScheduler();
                if (ee.fibersAvailable() && !ee.onFiber())
                {
                    // A thread switch under this function would drop its C++ frame: it runs on a fiber of the thread.
                    // When the fiber parks this unwinds the generated frames above (EeDispatcherTransfer), as a switch does.
                    ee.runOnFiber([rdram, ctx, runtime] { run(rdram, ctx, runtime); });
                    return;
                }
            }
            run(rdram, ctx, runtime);
        }
    };

    // A local the code hands to a guest function by pointer, where the original kept it in its stack frame:
    // guest memory reserved below the guest stack pointer for the scope, with the stack pointer moved below it
    // so callees do not overwrite it. That area is free stack, which the difftest leaves out.
    template <typename T>
    class GuestLocal
    {
    public:
        GuestLocal()
            : ctx_(g_call.ctx), savedSp_(gpr64(g_call.ctx, 29)),
              address_((static_cast<uint32_t>(savedSp_) - static_cast<uint32_t>((sizeof(T) + 15u) & ~size_t{15})) & ~15u)
        {
            setGpr64(ctx_, 29, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(address_))));
        }
        ~GuestLocal() { setGpr64(ctx_, 29, savedSp_); }
        GuestLocal(const GuestLocal &) = delete;
        GuestLocal &operator=(const GuestLocal &) = delete;

        mm::GuestPtr<T> ptr() const { return mm::GuestPtr<T>{address_}; }
        operator mm::GuestPtr<T>() const { return ptr(); }
        T &operator*() const { return *ptr(); }
        T *operator->() const { return &*ptr(); }

    private:
        R5900Context *ctx_;
        uint64_t savedSp_;
        uint32_t address_;
    };

    // The original function's own stack frame (`Size` bytes), reserved for a scope. Needed before calling a
    // function that reads stack memory it never wrote (VU0 loads of half-filled stack vectors): its frame then
    // lands where it did under the original, over the same leftover data.
    // While it is the innermost reservation, callGuest passes stack arguments at its bottom (sp + 0), where the
    // original's compiler put them, so a callee's own frame starts at the same address as under the original.
    template <uint32_t Size>
    class GuestFrame : public GuestLocal<std::array<uint8_t, Size>>
    {
    public:
        GuestFrame() : outerBottom_(g_call.frameBottom) { g_call.frameBottom = this->ptr().address; }
        ~GuestFrame() { g_call.frameBottom = outerBottom_; }
        GuestFrame(const GuestFrame &) = delete;
        GuestFrame &operator=(const GuestFrame &) = delete;

    private:
        uint32_t outerBottom_;
    };

    // Return address given to guest callees; never a real code address.
    inline constexpr uint32_t kReturnSentinel = 0x00000FF0u;

    [[noreturn]] void calleeDidNotReturn(uint32_t address, uint32_t pc);

    // After a guest function at `entry` was entered: recompiled code sometimes hands the call back to the
    // dispatcher before it is finished (a recursive call is compiled as a jump inside the function, so its
    // return lands on a resume point). Like the runtime's dispatcher, keep entering the code at ctx->pc until
    // it returns to `until`. Runtime handlers that do not advance pc count as returned. False when ctx->pc
    // stops at an address without code.
    inline bool finishGuestCall(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t entry, uint32_t until)
    {
        if (ctx->pc == entry)
            return true;
        while (ctx->pc != until)
        {
            if (!runtime->hasFunction(ctx->pc))
                return false;
            // The callee handed the call back with a checkpoint due (recompiled code does that); on a fiber the thread
            // gives the processor up here instead of the checkpoint being ignored.
            runtime->eeScheduler().yieldIfDue();
            runtime->lookupFunction(ctx->pc)(rdram, ctx, runtime);
        }
        return true;
    }

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
        // Stack arguments go at the bottom of the GuestFrame in force when the stack pointer is still at its bottom (the
        // original keeps them in its own frame), else in a new 16-byte aligned area below the guest stack pointer.
        constexpr uint32_t stackBytes = ints > 8 ? ((8u * static_cast<uint32_t>(ints - 8)) + 15u) & ~15u : 0u;
        const bool inFrame = stackBytes != 0 && g_call.frameBottom == static_cast<uint32_t>(savedSp);
        const uint32_t sp = inFrame ? static_cast<uint32_t>(savedSp) : static_cast<uint32_t>(savedSp) - stackBytes;
        if (stackBytes != 0 && !inFrame)
            setGpr64(ctx, 29, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(sp))));
        int k = 0;
        (
            [&](auto value)
            {
                using T = decltype(value);
                const int slot = slots[k++];
                if constexpr (kFloatArg<T>)
                    ctx->f[12 + slot] = static_cast<float>(value);
                else if constexpr (std::is_same_v<T, Quad>)
                {
                    if (slot >= 8)
                        fatal("a 128-bit argument on the stack", address);
                    ctx->r[4 + slot] = _mm_set_epi64x(static_cast<int64_t>(value.high), static_cast<int64_t>(value.low));
                }
                else if (slot < 8)
                    setGpr64(ctx, 4 + slot, toGuestInt(value));
                else
                    store<uint64_t>(sp + 8u * static_cast<uint32_t>(slot - 8), toGuestInt(value));
            }(args),
            ...);
        setGpr64(ctx, 31, kReturnSentinel);
        ctx->pc = address;
        uint8_t *const rdram = g_call.rdram;
        PS2Runtime *const runtime = g_call.runtime;
        const GuestFn fn = runtime->lookupFunction(address);
        // On a fiber a thread switch inside the callee parks the fiber here and the callee goes on from ctx->pc when the
        // thread is resumed; elsewhere the switch propagates as before (to the fatal error in Wrapper::run).
        const bool completed = runtime->eeScheduler().callOnFiber(
            [&] { fn(rdram, ctx, runtime); },
            [&] { return finishGuestCall(rdram, ctx, runtime, address, kReturnSentinel); });
        if (!completed)
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
        mm::addr::NAME, #NAME, &port::Wrapper<&mm::NAME, mm::blocking::at<mm::addr::NAME>>::entry, port::Wrapper<&mm::NAME, mm::blocking::at<mm::addr::NAME>>::kResult,  \
        port::Wrapper<&mm::NAME, mm::blocking::at<mm::addr::NAME>>::kFuzzable ? &port::Wrapper<&mm::NAME, mm::blocking::at<mm::addr::NAME>>::mutate : nullptr)
