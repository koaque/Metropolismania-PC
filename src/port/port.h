// Entry points of the readable port for the override module and the difftest harness.

#pragma once

#include "ps2_runtime.h"

#include <cstdint>
#include <vector>

namespace port
{
    // What a function leaves for its caller: nothing, v0 or f0 (the difftest compares only that).
    enum class ResultKind
    {
        None,
        Int,
        Float,
    };

    struct Ported
    {
        uint32_t address;
        const char *name;
        PS2Runtime::RecompiledFunction wrapper;
        ResultKind result;
        PS2Runtime::RecompiledFunction original; // the generated function, set by install()
    };

    // Installs every PORT_FN function in place of its generated counterpart (unless listed in
    // MMPC_PORT_DISABLE). Called from the override module once the ELF is loaded.
    void install(PS2Runtime &runtime);

    const std::vector<Ported> &ported();
    const char *nameOf(uint32_t address);
}
