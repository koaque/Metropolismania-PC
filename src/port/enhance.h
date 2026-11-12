// Enhancements beyond the original game, each off unless asked for, so the faithful build stays as the original draws and
// plays. (The planned "our code" tree; for now its few switches live here, read by the port where they apply.)

#pragma once

#include <atomic>
#include <cstdint>

namespace port::enhance
{
    // MMPC_WIDESCREEN=1 (16:9) or W:H (21:9, say): how many times wider than 4:3 the city's view opens; 1 when off, and
    // without the native renderer (MMPC_RENDERER), which is what shows the wider picture. The city's visibility tests (and
    // the snow's) widen by it. The game's camera is left as it is, since saves keep it: the native renderer narrows the
    // world camera's screen and clip matrices by it where they reach VU1 (Span::WorldCamera), so the wider view fits the
    // 640 pixels drawn, and shows those frames that much wider.
    float widescreen();

    // Counts the city frames drawn with the wider view (the native renderer shows a frame wide when this moved since the
    // frame before).
    std::atomic<uint64_t> &wideFrames();

    // What the frame list holds from a point on, for the native renderer on a widened frame.
    enum class Span : uint8_t
    {
        Hud = 1,      // the HUD (minimap, phone, markers, windows, text): kept at 4:3 proportions in the middle
        Backdrop = 2, // a HUD piece that covers the 4:3 screen (a menu's frame): the same, its edges out to the wide edges
        WorldCamera = 3, // the world camera's packet: its screen and clip matrices, narrowed as they reach VU1
    };

    // Marks the current end of the frame list (the next DMA tag the game appends) as the start of `span`, until the next
    // mark or the frame's end. Game thread; only while widescreen() is on.
    void markFrameList(Span span);

    // The chain walker (game thread, at the list's transfer): the mark at this tag address, taken (0 when none).
    uint8_t takeFrameListMark(uint32_t tagAddress);
    bool frameListMarked();
}
