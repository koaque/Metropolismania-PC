// Replay of captured VU1 program runs (plan P4). See vu1test.cpp.

#pragma once

namespace port
{
    // With MMPC_VU1_TEST=<dir> set: replays every run captured there (PS2X_VU1_CAPTURE), prints the results and the
    // interpreter's speed, and exits the process with the number of failing records.
    void runVu1TestIfRequested();
}
