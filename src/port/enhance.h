// Enhancements beyond the original game, each off unless asked for, so the faithful build stays as the original draws and
// plays. (The planned "our code" tree; for now its few switches live here, read by the port where they apply.)

#pragma once

#include <atomic>
#include <cstdint>

namespace port::enhance
{
    // MMPC_WIDESCREEN=1 (16:9) or W:H (21:9, say): how many times wider than 4:3 the city's view opens; 1 when off, and
    // without the native renderer (MMPC_RENDERER), which is what shows the wider picture. The game's camera and its own
    // visibility tests are left as they are (the game logic stays the PS2's): what the wider view adds at the sides is
    // drawn by the extras pass (extras.cpp). The native renderer narrows the world camera's screen and clip matrices by
    // it where they reach VU1 (Span::WorldCamera), so the wider view fits the 640 pixels drawn, and shows those frames
    // that much wider.
    float widescreen();
    // MMPC_VIEW_DISTANCE=<factor> (1.5, 2, 3, up to 4) or max (4): how many times farther than the PS2 the city is drawn;
    // 1 when off or without the native renderer. The extra distance is drawn by the extras pass, and the native renderer
    // moves the fog out by the same factor.
    float viewDistance();
    // Whether the extras pass runs (widescreen or a longer view distance): the city then records its own visible tiles.
    bool extrasOn();
    // The extras pass (extras.cpp) is running: guest code drawn on the side and undone afterwards.
    bool inSidePass();
    void setSidePass(bool on);
    // The city drew its world into the frame list being built (city.cpp): the scene, taken by the extras pass at the
    // list's kick (0 when the city did not draw this frame).
    void noteCityWorld(uint32_t city);
    uint32_t takeCityWorld();
    // The tiles the game's own terrain walk (Terrain_Draw) drew this frame, by tile coordinates (they may lie off the map),
    // so the extras pass draws only the others. Cleared when the walk starts.
    void clearPs2Tiles();
    void notePs2Tile(int col, int row);
    bool isPs2Tile(int col, int row);

    // Counts the city frames drawn with the wider view (the native renderer shows a frame wide when this moved since the
    // frame before).
    std::atomic<uint64_t> &wideFrames();

    // What the frame list holds from a point on, for the native renderer on a widened frame.
    enum class Span : uint8_t
    {
        Hud = 1,      // the HUD (minimap, phone, markers, windows, text): kept at 4:3 proportions in the middle
        Backdrop = 2, // a HUD piece that covers the 4:3 screen (a menu's frame): the same, its edges out to the wide edges
        WorldCamera = 3, // the world camera's packet: its screen and clip matrices, narrowed as they reach VU1
        Extras = 4,      // the renderer's own mark where the extras pass's batches are spliced in (MMPC_EXTRAS_DEBUG)
    };

    // Marks the current end of the frame list (the next DMA tag the game appends) as the start of `span`, until the next
    // mark or the frame's end. Game thread; only while widescreen() is on or the extras pass runs.
    void markFrameList(Span span);

    // The chain walker (game thread, at the list's transfer): the mark at this tag address, taken (0 when none).
    uint8_t takeFrameListMark(uint32_t tagAddress);
    bool frameListMarked();
}
