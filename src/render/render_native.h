#pragma once

// The native renderer (plan N, docs/RENDERER.md). MMPC_RENDERER=native shows its picture; MMPC_RENDERER=side runs it beside
// the emulated GS and F2 switches between the two. Unset, nothing changes.

#include <string>

class PS2Runtime;

namespace mm::render
{
    void install(PS2Runtime &runtime);
    // The next native frame the game finishes, written at its full resolution to `path` (binary PPM) once drawn. For
    // comparisons with the emulated picture at scripted shots; does nothing while the renderer is off.
    void requestShot(const std::string &path);
}
