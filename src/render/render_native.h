#pragma once

// The native renderer (plan N, docs/RENDERER.md). MMPC_RENDERER=native shows its picture; MMPC_RENDERER=side runs it beside
// the emulated GS and F2 switches between the two. Unset, nothing changes.

class PS2Runtime;

namespace mm::render
{
    void install(PS2Runtime &runtime);
}
