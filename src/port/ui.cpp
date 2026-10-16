// Windows and their widget base class, text boxes, the city cursor and the notice line.

#include "game_functions.h"
#include "game_globals.h"

namespace mm
{
    // Widget states: 0 idle, 1 opening, 2 opened, 3 active, 4 closing, 5 closed.
    GuestPtr<Widget> Widget_ctor(GuestPtr<Widget> widget)
    {
        widget->vtable = GuestPtr<void>{0x001BD400u};
        widget->state = 0;
        widget->timer = 0.0f;
        return widget;
    }

    void Widget_Open(GuestPtr<Widget> widget)
    {
        widget->timer = 0.0f;
        widget->state = 1;
    }

    void Widget_Close(GuestPtr<Widget> widget)
    {
        widget->timer = 0.0f;
        widget->state = 4;
    }

    int Widget_GetState(GuestPtr<Widget> widget) { return widget->state; }
    void Widget_Idle(GuestPtr<Widget>) {}

    int TextBox_SetText(GuestPtr<TextBox> textbox, GuestPtr<char> text, int a2)
    {
        textbox->cursor = 0;
        return TextBox_AppendText(textbox, text, a2);
    }

    // Window frames: position and size, and the two corners as GS XY words (offset 0x6C0, 0x790; 4 fraction
    // bits horizontally, 16 bits per coordinate).
    void Frame_SetRect(GuestPtr<Frame> frame, int x, int y, int width, int height)
    {
        frame->width = width;
        frame->height = height;
        frame->x = x;
        frame->y = y;
        const auto gsXY = [](int gx, int gy)
        { return (static_cast<uint32_t>(gx + 0x6C0) << 4) | (static_cast<uint32_t>(gy + 0x790) << 20); };
        frame->cornerA = gsXY(x, y);
        frame->cornerB = gsXY(x + width, y + height);
    }

    // Colour components outside 0..0xFFFF zero their pair (r with b, g with a).
    void Frame_SetColor(GuestPtr<Frame> frame, int r, int g, int b, int a)
    {
        if (r < 0 || b < 0 || r > 0xFFFF || b > 0xFFFF)
            r = b = 0;
        if (g < 0 || a < 0 || g > 0xFFFF || a > 0xFFFF)
            g = a = 0;
        frame->cornerA = static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 16);
        frame->cornerB = static_cast<uint32_t>(b) | (static_cast<uint32_t>(a) << 16);
    }

    void CityCursor_SetTarget(GuestPtr<CityCursor> cursor, uint16_t kind, int id, GuestPtr<Vec4> position)
    {
        cursor->targetId = id;
        cursor->targetKind = kind;
        cursor->targetPos = *position;
    }

    // Marks a size x size block from `tile` on the minimap: one nibble per tile, two tiles per byte (even x
    // in the high nibble). Every 32nd row and column stays a grid line (colour 7, high nibbles only). With
    // `keepHigher` set, a tile only takes a colour above the one it has.
    void Minimap_Plot(GuestPtr<Minimap> minimap, uint32_t tile, int size, int colour, int keepHigher)
    {
        const GuestPtr<uint8_t> nibbles = GuestPtr<uint8_t>{minimap.address};
        for (int row = 0; row < size; ++row)
        {
            const uint32_t z = static_cast<uint32_t>((static_cast<int>(tile & 0xFFFFu) >> 7) + row);
            int linear = static_cast<int>((tile & 0x7Fu) + z * 0x80u);
            for (int col = 0; col < size; ++col, ++linear)
            {
                uint8_t &cell = nibbles[linear / 2];
                const uint32_t x = (tile & 0x7Fu) + static_cast<uint32_t>(col);
                if ((x & 1u) == 0)
                {
                    if ((x & 0x1Fu) == 0 || (z & 0x1Fu) == 0)
                        cell = static_cast<uint8_t>((cell & 0x0F) | 0x70);
                    else if (keepHigher == 0 || (cell & 0xF0) < colour << 4)
                        cell = static_cast<uint8_t>((cell & 0x0F) | static_cast<uint8_t>(colour << 4));
                }
                else if (keepHigher == 0 || (cell & 0x0F) < colour)
                    cell = static_cast<uint8_t>((cell & 0xF0) | static_cast<uint8_t>(colour));
            }
        }
    }

    // Counts the shown notice down and hides it when the time is up.
    void Notice_Tick(GuestPtr<Notice> notice)
    {
        if (notice->id == -1)
            return;
        if (--notice->timer < 1)
        {
            notice->id = -1;
            notice->unk_10 = 0;
        }
    }

    PORT_FN(Widget_ctor);
    PORT_FN(Widget_Open);
    PORT_FN(Widget_Close);
    PORT_FN(Widget_GetState);
    PORT_FN(Widget_Idle);
    PORT_FN(TextBox_SetText);
    PORT_FN(Frame_SetRect);
    PORT_FN(Frame_SetColor);
    PORT_FN(CityCursor_SetTarget);
    PORT_FN(Notice_Tick);
    PORT_FN(Minimap_Plot);
}
