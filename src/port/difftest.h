// Differential tests for ported functions: capture (MMPC_CAPTURE) and replay (MMPC_DIFFTEST).
// See difftest.cpp.

#pragma once

#include "ps2_runtime.h"

namespace port
{
    bool captureRequested();
    // Wraps the functions listed in MMPC_CAPTURE so their first calls are recorded.
    void installCapture(PS2Runtime &runtime);
    // With MMPC_CALL_COUNTS set: counts the calls of every game function (see difftest.cpp).
    void installCallCounts(PS2Runtime &runtime);
    // With MMPC_GENERATED_ENTRIES set: counts the entries of every function that is not ported (see difftest.cpp).
    void installGeneratedAudit(PS2Runtime &runtime);
    // With MMPC_DIFFTEST set: replays the records, prints the results and exits the process.
    void runDifftestIfRequested(PS2Runtime &runtime);
}
