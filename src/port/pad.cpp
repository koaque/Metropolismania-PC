// Controller input as the game sees it: held and newly pressed buttons, and input focus. While an owner
// holds the focus, every other owner gets the buttons without the focus mask.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

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

    // Takes the pad focus for `owner` (buttons in `mask` hidden from others) unless someone holds it.
    void Pad_TakeFocus(int owner, int mask)
    {
        if (globals::padFocusOwner.get() != 0)
            return;
        globals::padFocusOwner.set(owner);
        globals::padFocusMask.set(static_cast<uint32_t>(mask));
    }

    PORT_FN(Pad_TakeFocus);

    // ---- Controller set-up and polling (plan P6 batch 2) ----

    namespace
    {
        // Per-port words (two ports, 4 bytes each): the state machine's state (kPadPortStates), the frame count the pad
        // library last reported and the one before (the pad is dropped when it changes without being asked to), the fault
        // code of a pad that could not be set up, and one more word that only start-up clears.
        constexpr uint32_t kPadLastFrame = 0x001BDB10u, kPadFrame = 0x001BDB18u, kPadFault = 0x001BDB20u, kPadUnused = 0x001BDB30u;
        constexpr uint32_t kPadActAlignData = 0x001BDB28u;  // the 6 bytes handed to scePadSetActAlign
        constexpr uint32_t kPadPortBuffers = 0x001F38C0u;   // 0x100 bytes per port for the library's DMA

        // The pad library's request states: waiting for a request to finish steps the state back one, so the same call
        // is made again until the request is done.
        enum PadState : int32_t
        {
            None = 0,
            SetModeWait = 0x2A,    // set main mode requested: finished (0), or still pending (1)
            SetMainMode = 0x29,
            QueryDualShock = 0x28,
            ActAlign = 0x46,
            InfoPressModeWait = 0x47,
            EnterPressMode = 0x4C,
            EnterPressModeWait = 0x4D,
            Failed = 99,
            Ready = 100,
        };
    }

    void PadInit()
    {
        sdk::scePadInit(0);
        for (uint32_t port = 0; port < 2u; ++port)
        {
            port::store<int32_t>(globals::kPadPortStates + 4u * port, 0);
            port::store<int32_t>(kPadFrame + 4u * port, 0);
            port::store<int32_t>(kPadLastFrame + 4u * port, 0);
            port::store<int32_t>(kPadFault + 4u * port, 0);
            port::store<int32_t>(kPadUnused + 4u * port, 0);
            sdk::scePadPortOpen(static_cast<int>(port), 0, kPadPortBuffers + 0x100u * port);
        }
        sdk::sceGsSyncPath(0, 0);
    }

    // Steps each port's set-up state machine (finding out what controller it is, switching a DualShock to analog mode and
    // pressure sensitivity), and once a pad is ready (100) reads its buttons; a pad whose frame counter changed unasked is
    // set up again from state 0.
    void Pad_Poll()
    {
        for (int port = 0; port < 2; ++port)
        {
            const uint32_t at = 4u * static_cast<uint32_t>(port);
            const uint32_t stateWord = globals::kPadPortStates + at;
            const uint32_t faultWord = kPadFault + at;
            const int64_t status = sdk::scePadGetState(port, 0);
            if (status == 0)
                port::store<int32_t>(stateWord, 0);
            switch (port::load<int32_t>(stateWord))
            {
            case EnterPressModeWait:
                if (sdk::scePadGetReqState(port, 0) == 1)
                    port::store<int32_t>(stateWord, port::load<int32_t>(stateWord) - 1);
                if (sdk::scePadGetReqState(port, 0) == 0)
                {
                    port::store<int32_t>(stateWord, Failed);
                    port::store<int32_t>(faultWord, 0x71);
                }
                break;
            case EnterPressMode:
                if (sdk::scePadEnterPressMode(port, 0) == 1)
                    port::store<int32_t>(stateWord, port::load<int32_t>(stateWord) + 1);
                break;
            case InfoPressModeWait:
                if (sdk::scePadGetReqState(port, 0) == 1)
                    port::store<int32_t>(stateWord, port::load<int32_t>(stateWord) - 1);
                if (sdk::scePadGetReqState(port, 0) == 0)
                {
                    if (sdk::scePadInfoPressMode(port, 0) == 1)
                        port::store<int32_t>(stateWord, EnterPressMode);
                    else
                    {
                        port::store<int32_t>(stateWord, Failed);
                        port::store<int32_t>(faultWord, 0x70);
                    }
                }
                break;
            case ActAlign:
                port::store<uint8_t>(kPadActAlignData + 0, 0);
                port::store<uint8_t>(kPadActAlignData + 1, 1);
                for (uint32_t i = 2; i < 6u; ++i)
                    port::store<uint8_t>(kPadActAlignData + i, 0xFF);
                if (sdk::scePadSetActAlign(0, 0, kPadActAlignData) != 0)
                    port::store<int32_t>(stateWord, port::load<int32_t>(stateWord) + 1);
                break;
            case SetModeWait:
                if (sdk::scePadGetReqState(port, 0) == 1)
                    port::store<int32_t>(stateWord, port::load<int32_t>(stateWord) - 1);
                if (sdk::scePadGetReqState(port, 0) == 0)
                    port::store<int32_t>(stateWord, 0);
                break;
            case SetMainMode:
                if (sdk::scePadSetMainMode(port, 0, 1, 2) == 1)
                    port::store<int32_t>(stateWord, port::load<int32_t>(stateWord) + 1);
                break;
            case QueryDualShock:
                if (sdk::scePadInfoMode(port, 0, 4, -1) != 0)
                {
                    port::store<int32_t>(stateWord, port::load<int32_t>(stateWord) + 1);
                    if (sdk::scePadSetMainMode(port, 0, 1, 2) == 1)
                        port::store<int32_t>(stateWord, port::load<int32_t>(stateWord) + 1);
                }
                else
                {
                    port::store<int32_t>(stateWord, Failed);
                    port::store<int32_t>(faultWord, 0x40);
                }
                break;
            case None:
                if (status == 6 || status == 2)
                {
                    int64_t mode = sdk::scePadInfoMode(port, 0, 1, 0);
                    if (mode != 0)
                    {
                        const int64_t term = sdk::scePadInfoMode(port, 0, 2, 0);
                        if (0 < term)
                            mode = term;
                        if (mode == 7)
                            port::store<int32_t>(stateWord, ActAlign);
                        else if (mode == 4)
                            port::store<int32_t>(stateWord, QueryDualShock);
                        else
                        {
                            port::store<int32_t>(stateWord, Failed);
                            port::store<int32_t>(faultWord, 0x20);
                        }
                    }
                }
                break;
            default:
                if (status == 6 || status == 2)
                {
                    port::store<int32_t>(stateWord, Ready);
                    sdk::scePadRead(port, 0, globals::kPadBuffers + 0x20u * static_cast<uint32_t>(port));
                    const int32_t last = port::load<int32_t>(kPadLastFrame + at);
                    if (last != 0 && port::load<int32_t>(kPadFrame + at) != last)
                    {
                        port::store<int32_t>(kPadLastFrame + at, 0);
                        port::store<int32_t>(stateWord, 0);
                    }
                    port::store<int32_t>(kPadLastFrame + at, port::load<int32_t>(kPadFrame + at));
                }
                break;
            }
        }
    }

    PORT_FN(PadInit);
    PORT_FN(Pad_Poll);

    // Once a frame: polls the pads and builds the button words from the first one's buffer. The two button bytes (+2, +3) are
    // active low and are turned round; on an analog pad (mode 7 in the high nibble of +1) each stick axis (+4..+7) beyond a
    // quarter of its travel either way adds a button bit (right stick: left 0x800000, right 0x200000, up 0x100000, down
    // 0x400000; left stick: left 0x8000, right 0x2000, up 0x1000, down 0x4000). The newly pressed buttons are those not held
    // the frame before; Cross (0x40) is kept out of both while it is being swallowed (mm_Pad_ClearBit40) and the swallow ends
    // when Cross is let go. The result is the buffer's address (0 when the pad is not ready).
    int Pad_Update()
    {
        Pad_Poll();
        const int data = Pad_GetData(0);
        globals::padData.set(static_cast<uint32_t>(data));
        uint32_t held = 0;
        if (data != 0)
        {
            const uint32_t at = static_cast<uint32_t>(data);
            held = ((static_cast<uint32_t>(port::load<uint8_t>(at + 2u)) << 8) | port::load<uint8_t>(at + 3u)) ^ 0xFFFFu;
            if ((port::load<uint8_t>(at + 1u) & 0xF0u) == 0x70u)
            {
                const auto axis = [&](uint32_t offset, uint32_t low, uint32_t high) {
                    const uint32_t value = port::load<uint8_t>(at + offset);
                    if (value < 0x40u)
                        held |= low;
                    else if (value >= 0xC1u)
                        held |= high;
                };
                axis(4, 0x800000u, 0x200000u);
                axis(5, 0x100000u, 0x400000u);
                axis(6, 0x8000u, 0x2000u);
                axis(7, 0x1000u, 0x4000u);
            }
        }
        globals::padHeld.set(held);
        globals::padPressed.set(held & ~globals::padHeldBefore.get());
        globals::padHeldBefore.set(held);
        if ((held & 0x40u) == 0)
            globals::padSwallowCross.set(0);
        if (globals::padSwallowCross.get() != 0)
            globals::padHeld.set(globals::padHeld.get() & ~0x40u);
        return data;
    }

    PORT_FN(Pad_Update);
}
