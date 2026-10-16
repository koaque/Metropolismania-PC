// Entry points of the readable port for the override module and the difftest harness.

#pragma once

#include "ps2_runtime.h"

#include <cstdint>
#include <stdexcept>
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

    // A port error (a guest pointer into hardware registers, a guest callee that did not return, a thread
    // switch inside ported code). In a game run it stops the process; during a difftest replay
    // (faultsThrow) it is thrown and reported against the record instead.
    struct Fault : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };
    void setFaultsThrow(bool on);
    const char *nameOf(uint32_t address);
}
