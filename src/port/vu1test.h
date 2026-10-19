// Replay of captured VU1 program runs (plan P4). See vu1test.cpp.

#pragma once

#include <cstddef>
#include <vector>

namespace port
{
    struct Vu1Record;

    // With MMPC_VU1_AOT=<mode> set: checks the pre-timed programs (vu1aot.h) against the interpreter on the records; returns
    // the number of failures.
    size_t runVu1AotChecks(const std::vector<Vu1Record> &records);

    // With MMPC_VU1_TEST=<dir> set: replays every run captured there (PS2X_VU1_CAPTURE), prints the results and the
    // interpreter's speed, and exits the process with the number of failing records.
    void runVu1TestIfRequested();
}
