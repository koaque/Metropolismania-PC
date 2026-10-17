// Controller input as the game sees it: held and newly pressed buttons, and input focus. While an owner
// holds the focus, every other owner gets the buttons without the focus mask.

#include "game_functions.h"
#include "game_globals.h"

namespace mm
{
    namespace
    {
        uint32_t withoutFocus(uint32_t buttons, int owner)
        {
            const int32_t focus = globals::padFocusOwner.get();
            return focus != 0 && focus != owner ? buttons & ~globals::padFocusMask.get() : buttons;
        }
    }

    uint32_t Pad_Pressed(int owner) { return withoutFocus(globals::padPressed.get(), owner); }
    uint32_t Pad_Held(int owner) { return withoutFocus(globals::padHeld.get(), owner); }

    // Drops the focus if `owner` holds it (-1: whoever holds it).
    void Pad_ReleaseFocus(int owner)
    {
        if (owner == -1 || globals::padFocusOwner.get() == owner)
        {
            globals::padFocusOwner.set(0);
            globals::padFocusMask.set(0);
        }
    }

    // Held buttons with auto-repeat, for menus: on a new press at once (and the next repeat 30 frames later),
    // then every 5 frames while held; 0 in between.
    int Pad_GetRepeat()
    {
        globals::padRepeat.set(0);
        if (globals::padPressed.get() != 0)
        {
            const uint32_t held = globals::padHeld.get();
            if (held != 0)
                globals::padRepeatTimer.set(30);
            globals::padRepeat.set(held);
        }
        else
        {
            if (globals::padRepeatTimer.get() != 0)
                globals::padRepeatTimer.set(globals::padRepeatTimer.get() - 1);
            if (globals::padRepeatTimer.get() == 0)
            {
                globals::padRepeatTimer.set(5);
                globals::padRepeat.set(globals::padHeld.get());
            }
        }
        return static_cast<int>(globals::padRepeat.get());
    }

    // Consumes a Cross press (0x40): it disappears from this frame's buttons and stays out of the held ones
    // until it is released, so the next screen does not see it again.
    void Pad_ClearBit40()
    {
        globals::padSwallowCross.set(1);
        globals::padHeld.set(globals::padHeld.get() & ~0x40u);
        globals::padPressed.set(globals::padPressed.get() & ~0x40u);
    }

    // The port's pad buffer once the pad is ready (state 100), else 0.
    int Pad_GetData(int padPort)
    {
        const uint32_t index = static_cast<uint32_t>(padPort);
        if (port::load<int32_t>(globals::kPadPortStates + 4u * index) != 100)
            return 0;
        return static_cast<int>(globals::kPadBuffers + 0x20u * index);
    }

    PORT_FN(Pad_ClearBit40);
    PORT_FN(Pad_Pressed);
    PORT_FN(Pad_Held);
    PORT_FN(Pad_ReleaseFocus);
    PORT_FN(Pad_GetRepeat);
    PORT_FN(Pad_GetData);
}
