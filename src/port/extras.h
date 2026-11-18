// The extras pass (extras.cpp): what a wider or longer view shows beyond the PS2's, drawn with the game's own code and
// undone, so the game's logic stays the PS2's.
#pragma once

namespace mm
{
    // Called by DmaList_KickFrame before it sends the frame's list; does nothing unless widescreen or a longer view
    // distance is on, the native renderer runs and the city drew its world into this list.
    void Extras_Run();
}
