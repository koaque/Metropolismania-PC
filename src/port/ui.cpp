// Windows and their widget base class, text boxes, the city cursor and the notice line.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

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

    void TextBox_Draw(GuestPtr<TextBox> textbox) { Dma_AppendCall(static_cast<int>(textbox->packet)); }

    // Each layout setter rebuilds the box's packet.
    void TextBox_SetPos(GuestPtr<TextBox> textbox, int x, int y, int z)
    {
        textbox->x = x;
        textbox->y = y;
        textbox->z = z;
        TextBox_Build(textbox, textbox->style);
    }

    void TextBox_SetPosAndSize(GuestPtr<TextBox> textbox, int x, int y, int charWidth, int charHeight, int pitchX,
                               int pitchY, int z)
    {
        textbox->x = x;
        textbox->y = y;
        textbox->z = z;
        textbox->charWidth = charWidth;
        textbox->charHeight = charHeight;
        textbox->pitchX = pitchX;
        textbox->pitchY = pitchY;
        TextBox_Build(textbox, textbox->style);
    }

    // Values of 0 or less keep the current setting.
    void TextBox_SetCharSize(GuestPtr<TextBox> textbox, int width, int height, int pitchX, int pitchY)
    {
        if (width > 0)
            textbox->charWidth = width;
        if (height > 0)
            textbox->charHeight = height;
        if (pitchX > 0)
            textbox->pitchX = pitchX;
        if (pitchY > 0)
            textbox->pitchY = pitchY;
        TextBox_Build(textbox, textbox->style);
    }

    int TexSprite_SetPosScale(GuestPtr<TexSprite> sprite, int x, int y, int z, ps2f scaleX, ps2f scaleY)
    {
        sprite->x = x;
        sprite->y = y;
        sprite->z = z;
        sprite->scaleX = scaleX;
        sprite->scaleY = scaleY;
        return TexSprite_BuildPacket(sprite);
    }

    // Loading screen positions are in pixels; the text box takes GS units (offset 0x6C0, 0x790, 4 fraction bits).
    void LoadingScreen_SetTextPos(GuestPtr<LoadingScreen> loading, int x, int y)
    {
        TextBox_SetPos(loading->text, (x + 0x6C0) << 4, (y + 0x790) << 4, -0x100000);
    }

    void LoadingScreen_SetSpritePos(GuestPtr<LoadingScreen> loading, int sprite, int x, int y)
    {
        loading->spriteId = sprite;
        loading->spriteX = x;
        loading->spriteY = y;
        TexSprite_SetPosScale(loading->sprite, loading->spriteId, loading->spriteX, loading->spriteY, 1.0f, 1.0f);
    }

    // The blinking "please wait" line: its text box (0x1BDAC4), shown flag (0x1BDACC) and frame counter (0x1BDAC8).
    int WaitText_Set(GuestPtr<char> text) { return TextBox_SetText(GuestPtr<TextBox>{port::load<uint32_t>(0x001BDAC4u)}, text, 0); }

    void WaitText_Draw()
    {
        if (port::load<int32_t>(0x001BDACCu) == 0)
            return;
        if ((port::load<uint32_t>(0x001BDAC8u) & 0x30u) != 0)
            TextBox_Draw(GuestPtr<TextBox>{port::load<uint32_t>(0x001BDAC4u)});
        port::store<int32_t>(0x001BDAC8u, port::load<int32_t>(0x001BDAC8u) + 1);
    }

    void Notice_Draw(GuestPtr<Notice> notice)
    {
        if (notice->id != -1)
            TextBox_Draw(notice->text);
        if (notice->unk_10 != 0)
            TextBox_Draw(notice->text2);
    }

    // Scrolls so `item` is visible: the cursor moves down the first page, after that the list scrolls under it.
    void ListMenu_ScrollTo(GuestPtr<ListMenu> menu, int item)
    {
        if (item < menu->visibleItems)
        {
            menu->scroll = 0;
            menu->cursorY = item * menu->itemHeight;
        }
        else
        {
            menu->scroll = menu->itemHeight * (menu->visibleItems - (item + 1));
            menu->cursorY = menu->itemHeight * (menu->visibleItems - 1);
        }
    }

    // The cursor bar's packet words: GS XY corners (12.4 fixed point, offset 0x6C0 and 0x790) around the cursor
    // offset, a third of an item above and below the list, and fixed texture words.
    void ListMenu_SetCursor(GuestPtr<ListMenu> menu, int dx, int dy, int depth)
    {
        menu->cursorDx = dx;
        menu->cursorDy = dy;
        menu->depth = depth;
        const int third = menu->itemHeight / 3;
        const uint32_t left = static_cast<uint32_t>(menu->cursorDx + 0x6A0) << 4;
        const uint32_t right = static_cast<uint32_t>(menu->cursorDx + menu->unk_26C + 0x6C0) << 4;
        const auto gsY = [](int y) { return static_cast<uint32_t>(y + 0x790) << 20; };
        uint32_t *packet = menu->cursorPacket;
        packet[0x00] = 0x79006C00u;
        packet[0x04] = left | 0x87000000u;
        packet[0x08] = left | 0x79000000u;
        packet[0x0C] = gsY(menu->cursorDy - third) | right;
        packet[0x10] = gsY(menu->cursorDy + menu->visibleItems * menu->itemHeight + third) | left;
        packet[0x14] = right | 0x87000000u;
        packet[0x18] = right | 0x79000000u;
        packet[0x1C] = 0x87009400u;
    }

    // List items: a text box each. Item numbers are checked against 32 only (the list holds 31), so they are
    // looked up as guest addresses like the original.
    namespace
    {
        GuestPtr<ListMenuItem> listItem(GuestPtr<ListMenu> menu, int item)
        {
            return GuestPtr<ListMenuItem>::of(menu->items[0]) + item;
        }
    }

    // Sets the text of `item` (-1: the next unfilled one); filling the next item advances the fill position
    // and grows the item count.
    void ListMenu_SetItemText(GuestPtr<ListMenu> menu, GuestPtr<char> text, int item)
    {
        if (item == -1)
            item = menu->nextItem;
        if (item >= 0x20 || !listItem(menu, item)->text)
            return;
        TextBox_SetText(listItem(menu, item)->text, text, 0);
        if (item == menu->nextItem)
            ++menu->nextItem;
        if (menu->itemCount < menu->nextItem)
            menu->itemCount = menu->nextItem;
    }

    // The menu (its own packet first), the item texts on screen centred in their rows, then the cursor frame
    // beside the cursor row; Z is cleared afterwards.
    void ListMenu_Draw(GuestPtr<ListMenu> menu)
    {
        if (menu->itemCount == 0)
            return;
        Dma_AppendCall(static_cast<int>(menu.address));
        int y = menu->scroll + menu->cursorDy;
        for (int i = 0; i < menu->itemCount; ++i, y += menu->itemHeight)
        {
            const int height = menu->itemHeight;
            const bool visible = menu->cursorDy - height <= y && y <= menu->cursorDy + height * (menu->visibleItems + 1);
            if (!visible || !listItem(menu, i)->text)
                continue;
            const int centre = (height - listItem(menu, i)->textHeight) / 2;
            TextBox_SetPos(listItem(menu, i)->text, (menu->cursorDx + 0x6C0) << 4, (y + centre + 0x790) << 4, menu->depth);
            TextBox_Draw(listItem(menu, i)->text);
        }
        Frame_SetRect(menu->cursorFrame, menu->cursorDx - 0x20, (menu->itemHeight - 12) / 2 + menu->cursorDy + menu->cursorY, 0x18, 0xC);
        Dma_AppendCall(static_cast<int>(menu->cursorFrame.address));
        DmaList_QueueClearZ();
    }

    // Empties the text of `item`; removing the last filled item shrinks the list. -1 empties every item.
    void ListMenu_ClearItem(GuestPtr<ListMenu> menu, int item)
    {
        const GuestPtr<char> empty{0x001B6600u};
        if (item == -1)
        {
            for (int i = 0; i < menu->nextItem; ++i)
                if (listItem(menu, i)->text)
                    TextBox_SetText(listItem(menu, i)->text, empty, 1);
            menu->nextItem = 0;
            menu->itemCount = 0;
            return;
        }
        if (listItem(menu, item)->text)
            TextBox_SetText(listItem(menu, item)->text, empty, 1);
        const int last = menu->nextItem - 1;
        if (item == last)
        {
            menu->nextItem = last;
            --menu->itemCount;
        }
    }

    // Window frame styles pick a cell of the frame texture, in 32nds of its size: three wide, six high, in two
    // rows. Unknown styles keep the previous texture coordinates.
    namespace
    {
        struct FrameCell
        {
            int style, s0, t0, s1, t1;
        };

        constexpr FrameCell kFrameCells[] = {
            {0, 0, 0, 0, 0},
            {10, 0, 16, 3, 22}, {11, 3, 16, 6, 22}, {12, 6, 16, 9, 22}, {13, 9, 16, 12, 22},
            {20, 0, 22, 3, 28}, {21, 3, 22, 6, 28}, {22, 6, 22, 9, 28}, {23, 9, 22, 12, 28},
            {30, 12, 22, 15, 28}, {31, 12, 22, 15, 28},
            {50, 15, 22, 18, 28}, {51, 18, 22, 21, 28}, {52, 21, 22, 24, 28}, {53, 24, 22, 27, 28},
        };
    }

    void Frame_SetStyle(GuestPtr<Frame> frame, int style)
    {
        frame->style = style;
        for (const FrameCell &cell : kFrameCells)
            if (cell.style == frame->style)
            {
                const auto in32nds = [](int n) { return fromInt(n) / ps2f(32.0f); };
                frame->stA[0] = in32nds(cell.s0);
                frame->stA[1] = in32nds(cell.t0);
                frame->stB[0] = in32nds(cell.s1);
                frame->stB[1] = in32nds(cell.t1);
                return;
            }
    }

    // Texture colours (RGBA bytes): the palette, or every pixel of an image without one.
    namespace
    {
        uint32_t texColourCount(GuestPtr<uint8_t> image)
        {
            const uint16_t paletteSize = port::load<uint16_t>(image.address + 4u);
            if (paletteSize != 0)
                return paletteSize;
            return static_cast<uint32_t>(port::load<uint16_t>(image.address + 0x10u)) * port::load<uint16_t>(image.address + 0x12u);
        }
    }

    // Sets every colour's alpha; with `keepClear`, fully transparent colours stay so.
    void TexSprite_SetAlpha(GuestPtr<TexSprite> sprite, uint8_t alpha, int keepClear)
    {
        const GuestPtr<uint8_t> image{sprite->image.address};
        const uint32_t count = texColourCount(image);
        GuestPtr<uint8_t> colour = image + 0x20;
        for (uint32_t i = 0; i < count; ++i, colour = colour + 4)
            if (keepClear == 0 || colour[3] != 0)
                colour[3] = alpha;
    }

    // Turns the green-tinted grey colours (red = blue != green) translucent: alpha 0x80 - green / 2, then green
    // set to red. Transparent colours are left alone.
    void TexSprite_GreenToAlpha(GuestPtr<TexSprite> sprite)
    {
        const GuestPtr<uint8_t> image{sprite->image.address};
        const uint32_t count = texColourCount(image);
        GuestPtr<uint8_t> colour = image + 0x20;
        for (uint32_t i = 0; i < count; ++i, colour = colour + 4)
            if (colour[0] == colour[2] && colour[0] != colour[1] && colour[3] != 0)
            {
                colour[3] = static_cast<uint8_t>(0x80 - (colour[1] >> 1));
                colour[1] = colour[0];
            }
    }

    GuestPtr<MenuWin> MenuWin_ctor(GuestPtr<MenuWin> menuwin)
    {
        Widget_ctor(GuestPtr<Widget>::of(menuwin->base));
        menuwin->base.vtable = GuestPtr<void>{0x001BCF80u};
        menuwin->unk_1C = 0;
        menuwin->model = GuestPtr<A3DObject>{port::load<uint32_t>(0x001F3B2Cu)};
        menuwin->list = GuestPtr<ListMenu>{0};
        menuwin->size = 0;
        return menuwin;
    }

    // Message windows draw in pass 2: the window node, scaled in x by the open (state 1) or close (state 4)
    // timer, then while shown the text and, in state 3, the "more" arrow blinking every 16 frames (a frame
    // counter at 0x1BDA00, set up on first use like a static local, flag 0x1BDA04).
    void MsgWin_Draw(GuestPtr<MsgWin> msgwin, int pass)
    {
        if (pass != 2 || msgwin->base.state == 0)
            return;
        const GuestPtr<Vec4> matrix{port::load<uint32_t>(msgwin->model->model.address + 8u) + static_cast<uint32_t>(msgwin->node) * 0xC0u + 0x70u};
        Mat4_Copy(matrix, GuestPtr<Vec4>{0x001B2550u}); // identity
        matrix[3].y = 0.01f;
        matrix[3].z = -4.4f;
        if (msgwin->base.state == 1)
            matrix[0].x = msgwin->base.timer;
        else if (msgwin->base.state == 4)
            matrix[0].x = ps2f(1.0f) - msgwin->base.timer;
        A3DObject_DrawNode(msgwin->model, msgwin->node);
        const int state = msgwin->base.state;
        if (state == 0 || state == 1 || state == 2 || state == 4 || state == 5)
            return;
        if (port::load<int8_t>(0x001BDA04u) == 0)
        {
            port::store<int32_t>(0x001BDA00u, 0);
            port::store<int8_t>(0x001BDA04u, 1);
        }
        port::store<int32_t>(0x001BDA00u, port::load<int32_t>(0x001BDA00u) + 1);
        if (msgwin->base.state == 3 && ((port::load<int32_t>(0x001BDA00u) >> 4) & 1) == 0)
            Dma_AppendCall(static_cast<int>(msgwin->arrowPacket));
        TextBox_Draw(msgwin->text);
    }

    // Menu windows draw in pass 2 over a cleared Z: the window node for their size, scaled in y by the open
    // (state 1) or close (state 4) timer, and their list once active (state 3).
    void MenuWin_Draw(GuestPtr<MenuWin> menuwin, int pass)
    {
        if (pass != 2 || menuwin->base.state == 0)
            return;
        DmaList_QueueClearZ();
        const int node = (menuwin->size - 2 < 0 ? 0 : menuwin->size - 2) + 3;
        const GuestPtr<Vec4> matrix{port::load<uint32_t>(menuwin->model->model.address + 8u) + static_cast<uint32_t>(node) * 0xC0u + 0x70u};
        Mat4_Copy(matrix, GuestPtr<Vec4>{0x001B2550u}); // identity
        matrix[3].z = -4.4f;
        if (menuwin->base.state == 1)
            matrix[1].y = menuwin->base.timer;
        else if (menuwin->base.state == 4)
            matrix[1].y = ps2f(1.0f) - menuwin->base.timer;
        A3DObject_DrawNode(menuwin->model, node);
        if (menuwin->list && menuwin->base.state == 3)
            ListMenu_Draw(menuwin->list);
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

    // A window frame: the packet template (0xB0 bytes at 0x1B04B0), then style, position, size and colour.
    GuestPtr<Frame> Frame_ctor(GuestPtr<Frame> frame, int style, int x, int y, int width, int height, uint32_t colour)
    {
        BaseObject_ctor(static_cast<int>(frame.address));
        for (uint32_t i = 0; i < 0xB0u; i += 4)
            port::store<uint32_t>(frame.address + i, port::load<uint32_t>(0x001B04B0u + i));
        Frame_SetStyle(frame, style);
        frame->width = width;
        frame->height = height;
        frame->x = x;
        frame->y = y;
        const auto gsXY = [](int gx, int gy)
        { return (static_cast<uint32_t>(gx + 0x6C0) << 4) | (static_cast<uint32_t>(gy + 0x790) << 20); };
        frame->cornerA = gsXY(x, y);
        frame->cornerB = gsXY(x + width, y + height);
        frame->colourA = colour;
        frame->colourB = colour;
        return frame;
    }

    // The frame's corners in GS units, (x0, y0) top left and (x1, y1) bottom right. When either x (or either
    // y) is outside 0..0xFFFF, both become 0.
    void Frame_SetCorners(GuestPtr<Frame> frame, int x0, int y0, int x1, int y1)
    {
        if (x0 < 0 || x1 < 0 || x0 > 0xFFFF || x1 > 0xFFFF)
            x0 = x1 = 0;
        if (y0 < 0 || y1 < 0 || y0 > 0xFFFF || y1 > 0xFFFF)
            y0 = y1 = 0;
        frame->cornerA = static_cast<uint32_t>(x0) | (static_cast<uint32_t>(y0) << 16);
        frame->cornerB = static_cast<uint32_t>(x1) | (static_cast<uint32_t>(y1) << 16);
    }

    // The cursor's info window for this frame: its packet and info lines, then either the extra packet over
    // a cleared Z (mode 4) or a dimmed screen with the fade frame (other non-zero modes).
    void CityCursor_Draw(GuestPtr<CityCursor> cursor)
    {
        if (cursor->visible == 0)
            return;
        const uint32_t parity = port::load<uint32_t>(0x001BDCD0u) & 1u;
        const GuestPtr<CityCursorView> view = GuestPtr<CityCursorView>::of(cursor->views[parity]);
        Dma_AppendCall(static_cast<int>(view->frame.address));
        // lineCount is not checked against 5, so the lines are read as guest addresses like the original.
        const GuestPtr<uint32_t> lines{view.address}, lineFrames{view.address + 0x14u};
        for (int i = 0; i < cursor->lineCount; ++i)
        {
            TextBox_Draw(GuestPtr<TextBox>{lines[i]});
            Dma_AppendCall(static_cast<int>(lineFrames[i]));
        }
        if (cursor->mode == 4)
        {
            DmaList_QueueClearZ();
            Dma_AppendCall(static_cast<int>(view.address + 0x30u));
        }
        else if (cursor->mode != 0)
        {
            DmaList_QueueFade(0x40, 0);
            const int top = cursor->frameY - cursor->frameStep * 16;
            Frame_SetCorners(view->fadeFrame, cursor->frameX, top, cursor->frameX + 0x180, top + 0xC0);
            Dma_AppendCall(static_cast<int>(view->fadeFrame.address));
        }
    }

    // Places the info window next to the projected cursor point (GS units, 4 fraction bits): x kept within
    // 0x7080..0x9140, the lines stacked upwards from 24 pixels above it, or from 12 pixels below it when they
    // would run off the top (y 0x7A60 at least), and kept above y 0x8700 at the bottom. The fade frame goes 0x6C0
    // to the left, each line's frame behind its text, and the window frame from the point 0x180 to the left.
    void CityCursor_LayoutInfo(GuestPtr<CityCursor> cursor, GuestPtr<Vec4> point, GuestPtr<Vec4> matrix)
    {
        port::GuestLocal<Vec4> screen;
        const uint32_t sx = screen.ptr().address, sy = sx + 4u;
        Math_ProjectPoint(static_cast<int>(cursor.address), screen, point, matrix);
        int x = port::load<int32_t>(sx);
        x = x < 0x7080 ? 0x7080 : x > 0x9140 ? 0x9140 : x;
        port::store<int32_t>(sx, x);
        const int column = (x & 0xFFF0);
        cursor->frameX = column - 0x6C0;
        const int pixelY = port::load<int32_t>(sy) / 16;
        cursor->frameY = (pixelY - 24) * 16;
        int windowTop = pixelY - 12;
        int lineY = pixelY - 24;
        const int lines = cursor->lineCount;
        if (windowTop - lines * 12 < 0x7A6)
        {
            if (windowTop < 0x7A6)
                port::store<int32_t>(sy, 0x7A60);
            const int below = port::load<int32_t>(sy) / 16;
            windowTop = below + 12;
            lineY = below + lines * 12;
            cursor->frameY = lineY * 16;
        }
        else if (windowTop >= 0x865)
        {
            port::store<int32_t>(sy, 0x8700);
            windowTop = 0x864;
            lineY = 0x864 - lines * 12;
            cursor->frameY = lineY * 16;
        }
        const uint32_t parity = port::load<uint32_t>(0x001BDCD0u) & 1u;
        const GuestPtr<CityCursorView> view = GuestPtr<CityCursorView>::of(cursor->views[parity]);
        // As in CityCursor_Draw, lineCount is not checked against 5.
        const GuestPtr<uint32_t> lineText{view.address}, lineFrames{view.address + 0x14u};
        int y = lineY << 4;
        for (int i = 0; i < cursor->lineCount; ++i)
        {
            TextBox_SetPos(GuestPtr<TextBox>{lineText[i]}, column - 0x400, y, -0x100000);
            Frame_SetCorners(GuestPtr<Frame>{lineFrames[i]}, column - 0x580, y, column - 0x400, y + 0xC0);
            y -= 0xC0;
        }
        x = port::load<int32_t>(sx);
        Frame_SetCorners(view->frame, x - 0x180, windowTop << 4, x, port::load<int32_t>(sy));
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

    // The laptop's minimap model follows the camera: turned by `angle`, moved against the camera's x and z.
    void Minimap_SetView(GuestPtr<Minimap> minimap, GuestPtr<ps2f> camera, ps2f angle)
    {
        const GuestPtr<Vec4> matrix = GuestPtr<Vec4>::of(minimap->model->matrix[0]);
        sdk::sceVu0RotMatrixZ(matrix.address, 0x001B2550u, angle); // from the identity
        matrix[3].z = 2.0f;
        matrix[3].x = ps2f(1.75f) - ps2f(0.0105f) * camera[0];
        matrix[3].y = ps2f(1.02f) + ps2f(0.0105f) * camera[2];
    }

    // The laptop minimap: its frame and map models (0x1F3B18, 0x1F3B14), every cell set to colour 1 (both
    // nibbles), and the packet of the map image built.
    GuestPtr<Minimap> Minimap_ctor(GuestPtr<Minimap> minimap)
    {
        BaseObject_ctor(static_cast<int>(minimap.address));
        minimap->model = GuestPtr<A3DObject>{port::load<uint32_t>(0x001F3B14u)};
        minimap->frameModel = GuestPtr<A3DObject>{port::load<uint32_t>(0x001F3B18u)};
        minimap->frameModel->matrix[3].x = 1.8f;
        minimap->frameModel->matrix[3].y = 1.0f;
        minimap->frameModel->matrix[3].z = 2.2f;
        minimap->blink = 0;
        for (uint8_t &cell : minimap->cells)
            cell = 0x11;
        Minimap_BuildPacket(minimap);
        return minimap;
    }

    // Two colours of the map image's palette (0x1AF1A4, 0x1AF1A8) blink, every 32 and every 8 frames.
    void Minimap_Draw(GuestPtr<Minimap> minimap)
    {
        const uint32_t blink = static_cast<uint32_t>(++minimap->blink);
        if ((blink & 0x1Fu) == 0)
            port::store<uint32_t>(0x001AF1A4u, (blink & 0x20u) != 0 ? 0x6000C0C0u : 0x60545AA3u);
        if ((blink & 7u) == 0)
            port::store<uint32_t>(0x001AF1A8u, (blink & 8u) != 0 ? 0x6000F0F0u : 0x60545AA3u);
        A3DObject_Draw(minimap->frameModel);
        Dma_AppendCall(static_cast<int>(GuestPtr<uint8_t>::of(minimap->packet[0]).address));
        A3DObject_Draw(minimap->model);
    }

    // The pause menu starts cleared, with the default save name from 0x1B73D0 (Shift-JIS).
    GuestPtr<PauseMenu> PauseMenu_ctor(GuestPtr<PauseMenu> pause)
    {
        for (uint32_t offset = 0; offset < 0x14; offset += 4)
            port::store<uint32_t>(pause.address + offset, 0);
        port::store<uint32_t>(pause.address + 0x2Cu, 0);
        sdk::strcpy(pause.address + 0x30u, 0x001B73D0u);
        return pause;
    }

    // The pause menu: the window (node 0 of the shared window model, scaled by 4) slides from its closed to its
    // open position as the menu opens (+0x2C reaching 120), then the slot list while picking a slot (state 4)
    // or else the main menu, then the message box.
    void PauseMenu_Draw(GuestPtr<PauseMenu> pause)
    {
        const GuestPtr<A3DObject> window = globals::menuWindowModel.get();
        const ps2f t = pause->openTime / ps2f(120.0f);
        const ps2f rest = ps2f(1.0f) - t;
        const GuestPtr<Vec4> open{globals::kPauseWindowOpen}, closed{globals::kPauseWindowClosed};
        const GuestPtr<Vec4> matrix{port::load<uint32_t>(window->model.address + 8u) + 0x70u};
        matrix[3].x = rest * closed->x + t * open->x;
        matrix[3].y = rest * closed->y + t * open->y;
        matrix[3].z = rest * closed->z + t * open->z;
        matrix[3].w = 1.0f;
        matrix[2].z = 4.0f;
        matrix[1].y = 4.0f;
        matrix[0].x = 4.0f;
        A3DObject_DrawNode(window, 0);
        if (pause->state == 4 && pause->slotMenu)
            ListMenu_Draw(pause->slotMenu);
        else if (pause->mainMenu)
            ListMenu_Draw(pause->mainMenu);
        if (pause->message)
            TextBox_Draw(pause->message);
    }

    namespace
    {
        // Node `node` of the model, unrotated and unscaled at (x, y, z).
        void placeNode(GuestPtr<A3DObject> model, int node, ps2f x, ps2f y, ps2f z)
        {
            const GuestPtr<Vec4> matrix{port::load<uint32_t>(model->model.address + 8u) + static_cast<uint32_t>(node) * 0xC0u + 0x70u};
            Mat4_Copy(matrix, GuestPtr<Vec4>{globals::kIdentityMatrix});
            matrix[3].x = x;
            matrix[3].y = y;
            matrix[3].z = z;
        }
    }

    // The status panel: in the detail view, node 0x15 of the detail model with the four text boxes, then node 0
    // and one more node per mark; then the panel model after a depth clear and, during an alert, the flashing
    // packet and the alert model.
    void StatusPanel_Draw(GuestPtr<StatusPanel> panel)
    {
        if (panel->detail != 0)
        {
            placeNode(panel->detailModel, 0x15, 0.18f, -0.04f, -4.28f);
            A3DObject_DrawNode(panel->detailModel, 0x15);
            for (int i = 0; i < 4; ++i)
                TextBox_Draw(panel->text[i]);
            placeNode(panel->detailModel, 0, 0.145f, 0.14f, -4.4f);
            A3DObject_DrawNode(panel->detailModel, 0);
            for (int i = 0; i < panel->markCount; ++i)
            {
                placeNode(panel->detailModel, i + 1, 0.145f, 0.14f, -4.4f);
                A3DObject_DrawNode(panel->detailModel, i + 1);
            }
        }
        DmaList_QueueClearZ();
        Dma_AppendCall(static_cast<int>(panel->panelPacket));
        A3DObject_Draw(panel->model);
        if (panel->alert == 0)
            return;
        // The colours move one entry along. The copy runs downwards through the table it reads from, so, as in
        // the original, entry 0's colour drops out and entries 0 and 1 both take entry 14's.
        int dest = 0;
        for (int source = 14; source >= 0; --source)
        {
            port::store<uint32_t>(globals::kAlertColours + 4u * static_cast<uint32_t>(dest),
                                  port::load<uint32_t>(globals::kAlertColours + 4u * static_cast<uint32_t>(source)));
            if (--dest < 0)
                dest = 14;
        }
        Dma_AppendCall(static_cast<int>(panel.address));
        A3DObject_Draw(panel->alertModel);
    }

    // title: the logo's three letters bounce in turn (a size step every 5 frames from the 8-step table) and hop
    // to the next of five places every 40 frames. The third letter waits for frame 11 and stops at 185. Returns
    // 1 once 200 frames have passed.
    int Title_LogoLetters(GuestPtr<SceneA> scene)
    {
        ++scene->introFrame;
        if (scene->introFrame >= 200)
            return 1;
        const int step = scene->introFrame % 40 / 5;
        for (uint32_t i = 0; i < 3; ++i)
        {
            if (i == 2 && scene->introFrame < 11)
                return 0;
            if (i == 2 && scene->introFrame >= 185)
                break;
            const uint16_t size = port::load<uint16_t>(globals::kLogoLetterSteps + 0x10u * i + 2u * static_cast<uint32_t>(step));
            if (size == 0)
                continue;
            const uint32_t place = globals::kLogoLetterPlaces + 0x14u * i + 4u * static_cast<uint32_t>(scene->introFrame / 40 % 5);
            int x = port::load<uint16_t>(place), y = port::load<uint16_t>(place + 2u);
            ps2f scale = 1.0f;
            if (size == 1)
            {
                scale = 0.6f;
                x += 4;
                y += 4;
            }
            else if (size == 2)
            {
                scale = 0.8f;
                x += 2;
                y += 2;
            }
            TexSprite_SetPosScale(scene->logoLetters[i], x, y, 0x3F00, scale, scale);
            TexSprite_Draw(scene->logoLetters[i]);
        }
        return 0;
    }

    // title: the menu choices. The selected one pulses (scale up to 1.09 over 10 frames and back, moving left
    // and up with it) and the other is drawn at 0.6; with a single choice only the first is shown. Returns 1
    // once the 20-frame pulse is over (then it rests at scale 1).
    int Title_DrawChoices(GuestPtr<SceneA> scene)
    {
        const int frame = scene->pulseFrame;
        int dx;
        ps2f scale;
        if (frame < 10)
        {
            dx = -frame;
            scale = ps2f(1.0f) + fromInt(frame) / ps2f(100.0f);
        }
        else
        {
            dx = frame % 10 - 9;
            scale = ps2f(1.09f) - fromInt(frame % 10) / ps2f(100.0f);
        }
        const int dy = dx / 2;
        if (scene->choiceCount == 1 && scene->choice == 0)
        {
            TexSprite_SetPosScale(scene->choices[0], dx + 0xBE, dy + 0x154, 0x3F00, scale, scale);
            TexSprite_Draw(scene->choices[0]);
        }
        else if (scene->choice == 0)
        {
            TexSprite_SetPosScale(scene->choices[0], dx + 0xBE, dy + 0x13C, 0x3F00, scale, scale);
            TexSprite_SetPosScale(scene->choices[1], 0xF0, 0x178, 0x3F00, 0.6f, 0.6f);
            TexSprite_Draw(scene->choices[0]);
            TexSprite_Draw(scene->choices[1]);
        }
        else
        {
            TexSprite_SetPosScale(scene->choices[0], 0xF0, 0x146, 0x3F00, 0.6f, 0.6f);
            TexSprite_SetPosScale(scene->choices[1], dx + 0xBE, dy + 0x168, 0x3F00, scale, scale);
            TexSprite_Draw(scene->choices[0]);
            TexSprite_Draw(scene->choices[1]);
        }
        ++scene->pulseFrame;
        if (scene->pulseFrame < 20)
            return 0;
        scene->choicePulsing = 0;
        --scene->pulseFrame;
        return 1;
    }

    namespace
    {
        // One frame of a list move: the cursor bar moves 4 pixels, or, at the top or bottom of the shown
        // items, the list scrolls; the last frame takes what is left of the step.
        void stepListMove(GuestPtr<ListMenu> menu)
        {
            if (menu->scrollStep > 0) // up
            {
                menu->scrollStep -= 4;
                if (menu->cursorY > 0)
                {
                    menu->cursorY -= 4;
                    if (menu->scrollStep < 0)
                    {
                        menu->cursorY -= menu->scrollStep;
                        menu->scrollStep = 0;
                    }
                }
                else
                {
                    menu->cursorY = 0;
                    if (menu->scroll < 0)
                    {
                        menu->scroll += 4;
                        if (menu->scrollStep < 0)
                        {
                            menu->scroll += menu->scrollStep;
                            menu->scrollStep = 0;
                        }
                    }
                    else
                    {
                        menu->scroll = 0;
                        menu->scrollStep = 0;
                    }
                }
                return;
            }
            menu->scrollStep += 4; // down
            const int shown = menu->itemCount < menu->visibleItems ? menu->itemCount : menu->visibleItems;
            const int lastY = (shown - 1) * menu->itemHeight;
            if (menu->cursorY < lastY)
            {
                menu->cursorY += 4;
                if (menu->scrollStep > 0)
                {
                    menu->cursorY += menu->scrollStep;
                    menu->scrollStep = 0;
                }
                return;
            }
            menu->cursorY = lastY;
            const int lowest = menu->itemHeight * (menu->visibleItems - menu->itemCount);
            if (lowest < menu->scroll)
            {
                menu->scroll -= 4;
                if (menu->scrollStep > 0)
                {
                    menu->scroll -= menu->scrollStep;
                    menu->scrollStep = 0;
                }
            }
            else
            {
                menu->scroll = lowest;
                menu->scrollStep = 0;
            }
        }
    }

    // A list's input for one frame. *choice is the item under the cursor. While a move is under way it goes on;
    // otherwise a cursor past the end goes to the last item, then Cross selects (returns 1, if allowed),
    // Triangle cancels (*choice = -1, returns 1, if allowed), and up or down (with key repeat) start a move of
    // one item, with a sound.
    int ListMenu_HandleInput(GuestPtr<ListMenu> menu, GuestPtr<int32_t> choice)
    {
        if (menu->itemCount == 0)
            return 0;
        *choice = divide(menu->cursorY - menu->scroll, menu->itemHeight);
        if (menu->scrollStep != 0)
        {
            stepListMove(menu);
            return 0;
        }
        const int count = menu->itemCount;
        if (*choice >= count)
        {
            if (count - 1 < menu->visibleItems)
            {
                menu->scroll = 0;
                menu->cursorY = (menu->itemCount - 1) * menu->itemHeight;
            }
            else
            {
                menu->scroll = menu->itemHeight * (menu->visibleItems - count);
                menu->cursorY = (menu->visibleItems - 1) * menu->itemHeight;
            }
        }
        if ((Pad_Pressed(0) & 0x40u) != 0) // Cross
        {
            if (menu->canSelect == 0)
                return 0;
            Pad_ClearBit40();
            Sound_PlaySe(1, 0, 0x3C, 0.0f, 0.0f);
            return 1;
        }
        if ((Pad_Pressed(0) & 0x10u) != 0) // Triangle
        {
            if (menu->canCancel == 0)
                return 0;
            Sound_PlaySe(2, 0, 0x3C, 0.0f, 0.0f);
            *choice = -1;
            return 1;
        }
        if ((Pad_GetRepeat() & 0x1000) != 0) // up
        {
            if (menu->cursorY > 0 || menu->scroll < 0)
            {
                menu->scrollStep = menu->itemHeight;
                Sound_PlaySe(0, 0, 0x3C, 0.0f, 0.0f);
            }
            return 0;
        }
        if ((Pad_GetRepeat() & 0x4000) != 0) // down
        {
            const int shown = menu->itemCount < menu->visibleItems ? menu->itemCount : menu->visibleItems;
            if (menu->cursorY < (shown - 1) * menu->itemHeight || menu->itemHeight * (menu->visibleItems - menu->itemCount) < menu->scroll)
            {
                menu->scrollStep = -menu->itemHeight;
                Sound_PlaySe(0, 0, 0x3C, 0.0f, 0.0f);
            }
        }
        return 0;
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
    PORT_FN(TextBox_Draw);
    PORT_FN(TextBox_SetPos);
    PORT_FN(TextBox_SetPosAndSize);
    PORT_FN(TextBox_SetCharSize);
    PORT_FN(TexSprite_SetPosScale);
    PORT_FN(LoadingScreen_SetTextPos);
    PORT_FN(LoadingScreen_SetSpritePos);
    PORT_FN(WaitText_Set);
    PORT_FN(WaitText_Draw);
    PORT_FN(Notice_Draw);
    PORT_FN(ListMenu_ScrollTo);
    PORT_FN(MenuWin_ctor);
    PORT_FN(ListMenu_SetCursor);
    PORT_FN(ListMenu_SetItemText);
    PORT_FN(ListMenu_ClearItem);
    PORT_FN(ListMenu_Draw);
    PORT_FN(Frame_SetStyle);
    PORT_FN(TexSprite_SetAlpha);
    PORT_FN(TexSprite_GreenToAlpha);
    PORT_FN(Frame_SetRect);
    PORT_FN(Frame_SetCorners);
    PORT_FN(Frame_ctor);
    PORT_FN(CityCursor_SetTarget);
    PORT_FN(CityCursor_Draw);
    PORT_FN(MsgWin_Draw);
    PORT_FN(MenuWin_Draw);
    PORT_FN(Notice_Tick);
    PORT_FN(Minimap_Plot);
    PORT_FN(Minimap_SetView);
    PORT_FN(Minimap_ctor);
    PORT_FN(Minimap_Draw);
    PORT_FN(PauseMenu_ctor);
    PORT_FN(PauseMenu_Draw);
    PORT_FN(StatusPanel_Draw);
    PORT_FN(Title_LogoLetters);
    PORT_FN(CityCursor_LayoutInfo);
    PORT_FN(Title_DrawChoices);
    PORT_FN(ListMenu_HandleInput);
}
