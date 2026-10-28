// The runtime of the readable port of the IOP modules (plan P5): native replacements of module functions registered with the IOP emulator.
//
// A port function is written as plain C++ with the original's name and parameters (`int CmdMidiOpen(IopPtr<ST_MIDI_OPEN> data)`) and registered
// with IOP_FN. When the IOP reaches the original's address the emulator hands the function a NativeContext; the trampoline reads the arguments
// from a0 to a3, calls the function and writes the result to v0. Imports and functions of the module that are not ported yet are guest code:
// call() runs them (an import is its stub in the module's import table).

#pragma once

#include "iop_ptr.h"

#include "ps2_runtime.h"
#include "ps2x/iop/iop_native.h"

#include <cstdint>
#include <tuple>
#include <type_traits>
#include <vector>

namespace iop
{
    // The context of the native function that is running.
    inline ps2x::iop::NativeContext *g_context = nullptr;

    [[noreturn]] void fatal(const char *what, uint32_t value);

    // Guest code at an absolute IOP address, run to its return: v0.
    inline uint32_t call(uint32_t address, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0)
    {
        return g_context->callGuest(address, a0, a1, a2, a3);
    }

    // An import: its stub, run as guest code; `arguments` is how many of a0 to a3 the import takes (the original leaves leftovers in the rest).
    inline uint32_t callImport(uint32_t address, unsigned arguments, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0)
    {
        g_context->argumentCount(arguments);
        return g_context->callGuest(address, a0, a1, a2, a3);
    }

    // Ends the function by continuing in guest code (a tail call).
    inline void jump(uint32_t address)
    {
        g_context->jump(address);
    }

    inline uint32_t reg(unsigned index) { return g_context->reg(index); }
    inline void setReg(unsigned index, uint32_t value) { g_context->setReg(index, value); }

    // A stack frame like the original function's: the stack pointer moves down by `size` for the call, so locals whose address goes to guest
    // code sit where the original's would (the same stale bytes, which the original's code may read, are there).
    class Frame
    {
    public:
        explicit Frame(uint32_t size) : saved(g_context->reg(29))
        {
            g_context->setReg(29, saved - ((size + 7u) & ~7u));
        }
        ~Frame() { g_context->setReg(29, saved); }
        Frame(const Frame &) = delete;
        Frame &operator=(const Frame &) = delete;
        uint32_t address(uint32_t offset = 0) const { return g_context->reg(29) + offset; }

    private:
        uint32_t saved;
    };

    struct Registration
    {
        const char *module;
        uint32_t offset;
        ps2x::iop::NativeFunction function;
        const char *name;
        uint32_t cost;
        bool hasResult;
    };
    std::vector<Registration> &registry();

    struct Registrar
    {
        Registrar(const char *module, uint32_t offset, ps2x::iop::NativeFunction function, const char *name, bool hasResult, uint32_t cost = 0)
        {
            registry().push_back({module, offset, function, name, cost, hasResult});
        }
    };

    // Registers every IOP_FN function with the emulator (called once from the override module).
    void install(PS2Runtime &runtime);

    namespace detail
    {
        template <typename A>
        A argument(ps2x::iop::NativeContext &context, unsigned &index)
        {
            static_assert(!std::is_pointer_v<A>, "port functions take IopPtr<T>, not host pointers");
            const uint32_t value = context.reg(index++);
            if constexpr (std::is_integral_v<A> || std::is_enum_v<A>)
                return static_cast<A>(value);
            else
                return A{value}; // IopPtr<T>
        }

        template <typename R>
        uint32_t result(R value)
        {
            if constexpr (std::is_enum_v<R>)
                return static_cast<uint32_t>(static_cast<int32_t>(value));
            else if constexpr (std::is_integral_v<R>)
            {
                if constexpr (std::is_signed_v<R>)
                    return static_cast<uint32_t>(static_cast<int32_t>(value));
                else
                    return static_cast<uint32_t>(value);
            }
            else
                return value.address; // IopPtr<T>
        }

        template <auto Function>
        struct Trampoline;

        template <typename R, typename... Args, R (*Function)(Args...)>
        struct Trampoline<Function>
        {
            static constexpr bool kHasResult = !std::is_void_v<R>;

            static void entry(ps2x::iop::NativeContext &context)
            {
                g_context = &context;
                g_ram = context.ram();
                unsigned index = 4;
                std::tuple<Args...> arguments{argument<Args>(context, index)...};
                if constexpr (std::is_void_v<R>)
                    std::apply(Function, arguments);
                else
                    context.setReg(2, result<R>(std::apply(Function, arguments)));
            }
        };
    }
}

// Registers `name` (a function of namespace iop::<module>) as the replacement of the module function of the same name.
#define IOP_FN(module, name) \
    namespace             \
    {                     \
        const ::iop::Registrar iop_registrar_##name{::iop::module::kModule, ::iop::module::offset::name, &::iop::detail::Trampoline<&::iop::module::name>::entry, #name, ::iop::detail::Trampoline<&::iop::module::name>::kHasResult}; \
    }
