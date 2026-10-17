// Windows and their widget base class, text boxes, the city cursor and the notice line.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

#include <initializer_list>

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

    // Runs the handler for the widget's state (vtable +0x2C idle, +0x30 opening, +0x34 opened, +0x38 active,
    // +0x3C closing, +0x40 closed) and returns the state after it.
    int Widget_Update(GuestPtr<Widget> widget)
    {
        const uint32_t state = static_cast<uint32_t>(widget->state);
        if (state < 6)
            port::callGuest<void>(port::load<uint32_t>(widget->vtable.address + 0x2Cu + 4u * state), widget.address);
        return widget->state;
    }
    void Widget_Idle(GuestPtr<Widget>) {}

    void TextBox_SetText(GuestPtr<TextBox> textbox, GuestPtr<char> text, int raw)
    {
        textbox->cursor = 0;
        TextBox_AppendText(textbox, text, raw);
    }

    // Lays `text` out from the cursor on, then blanks the cells after it. `raw` puts it character by
    // character; otherwise it wraps: a word (up to the next delimiter) that would run past the line's end
    // starts the next line, the rest of the line blanked and the character the word starts with (its space)
    // dropped; a word that ends exactly at the line's end drops the character after it; CR and LF end a line.
    void TextBox_AppendText(GuestPtr<TextBox> textbox, GuestPtr<char> text, int raw)
    {
        const auto cellAt = [&](int index) { return textbox->cells + 0x60u * static_cast<uint32_t>(index) + 0x50u; };
        const auto capacity = [&] { return textbox->columns * textbox->rows; };
        const auto blankFrom = [&](int from, int to) {
            uint32_t cell = cellAt(from);
            for (int i = from; i < to; ++i, cell += 0x60u)
                port::store<uint32_t>(cell + 4u, globals::fontData.get());
        };
        uint32_t cell = cellAt(textbox->cursor);
        const auto put = [&](uint8_t c) {
            port::store<uint32_t>(cell + 4u, static_cast<uint32_t>(Font_GetGlyph(c)));
            cell += 0x60u;
            ++textbox->cursor;
        };
        uint32_t p = text.address;
        if (raw != 0)
        {
            while (port::load<int8_t>(p) != 0 && textbox->cursor < capacity())
            {
                const uint8_t c = port::load<uint8_t>(p);
                ++p;
                put(c);
            }
        }
        else
        {
            bool skipNext = false;
            uint32_t wordEnd = p;
            for (; port::load<int8_t>(p) != 0 && textbox->cursor < capacity(); ++p)
            {
                if (p != wordEnd)
                {
                    put(port::load<uint8_t>(p));
                    continue;
                }
                if (skipNext)
                {
                    skipNext = false;
                    ++wordEnd;
                    continue;
                }
                const int8_t c = port::load<int8_t>(p);
                bool lineBreak = true;
                if (c == '\n' || c == '\r')
                    ++wordEnd;
                else
                {
                    wordEnd += static_cast<uint32_t>(sdk::strcspn(p + 1u, globals::kWordDelimiters)) + 1u;
                    const int reach = static_cast<int32_t>(wordEnd + static_cast<uint32_t>(remainder(textbox->cursor, textbox->columns)) - p);
                    lineBreak = textbox->columns < reach;
                    if (!lineBreak && reach == textbox->columns)
                        skipNext = true;
                }
                if (!lineBreak)
                {
                    put(port::load<uint8_t>(p));
                    continue;
                }
                const int start = textbox->cursor;
                const int fill = textbox->columns - remainder(start, textbox->columns);
                int end = start + fill;
                textbox->cursor = start;
                if (end == -1)
                    end = capacity();
                blankFrom(start, end);
                cell += 0x60u * static_cast<uint32_t>(fill);
                textbox->cursor = textbox->cursor + fill;
            }
        }
        const int last = textbox->cursor;
        textbox->cursor = last;
        blankFrom(last, capacity());
    }

    void TextBox_Draw(GuestPtr<TextBox> textbox) { Dma_AppendCall(static_cast<int>(textbox->packet)); }

    namespace
    {
        // Writes `words` from p on and returns the address after them.
        GuestPtr<uint32_t> emitWords(GuestPtr<uint32_t> p, std::initializer_list<uint32_t> words)
        {
            int32_t i = 0;
            for (const uint32_t word : words)
                p[i++] = word;
            return p + i;
        }

        // A cell position on the GS's 16-bit screen axis: one that is negative or whose far edge leaves it
        // restarts at 0.
        int keepOnAxis(int position, int size)
        {
            const int32_t end = static_cast<int32_t>(static_cast<uint32_t>(position) + static_cast<uint32_t>(size));
            return position < 0 || end >= 0x10000 ? 0 : position;
        }
    }

    // The box's DMA packet. With a palette (`clut`, 16 entries as 8 x 2) an image transfer of it to GS block 0x1E00
    // comes first. Then come the glyph uploads: one transfer per cell of a 32 x 32 4-bit glyph to block 0x1E01 + 2
    // per cell. Each cell's entry is 0x60 bytes, starting at `cells`; the glyph's address goes in at +0x54
    // (mm_TextBox_AppendText). Then the draw: a texture flush, register 8, grey, and a packed GIF tag over the
    // cells (at `sprites`). Each cell is a TEX0 and two (ST, XYZ2) corners of a sprite, row by row at the pitches.
    void TextBox_Build(GuestPtr<TextBox> textbox, int clut)
    {
        GuestPtr<uint32_t> p{textbox->packet};
        const int cells = textbox->columns * textbox->rows;
        if (clut != 0)
            p = emitWords(p, {0x10000006, 0, 0x11000000, 0x50000006,
                              4, 0x10000000, 0xE, 0,
                              0, 0, 0x51, 0,
                              8, 2, 0x52, 0,
                              0, 0x11E00, 0x50, 0,
                              0, 0, 0x53, 0,
                              0x8004, 0x08000000, 0, 0,
                              0x30000004, static_cast<uint32_t>(clut) & 0x7FFFFFFFu, 0, 0x50000004});
        p = emitWords(p, {0x10000002, 0, 0x11000000, 0x50000002,
                          0x8001, 0x10000000, 0xE, 0,
                          0xC, 0x16, 0x52, 0});
        textbox->cells = p.address;
        for (int i = 0; i < cells; ++i)
        {
            emitWords(p, {0x10000004, 0, 0x11000000, 0x50000004,
                          2, 0x10000000, 0xE, 0,
                          0, 0x14011E01u + 2u * static_cast<uint32_t>(i), 0x50, 0,
                          0, 0, 0x53, 0,
                          0x8009, 0x08000000, 0, 0,
                          0x30000009});
            p[22] = 0;
            p[23] = 0x50000009;
            p = p + 24;
        }
        const uint32_t qwc = static_cast<uint32_t>(cells * 5 + 7);
        p = emitWords(p, {qwc | 0x10000000u, 0, 0x11000000, qwc | 0x50000000u,
                          3, 0x10000000, 0xE, 0,
                          0, 0, 0x3F, 0,
                          0x4C00A, 0x54, 8, 0,
                          0x80808080u, 0x3F800000, 1, 0,
                          static_cast<uint32_t>(cells), 0x500B4000, 0x52526, 0});
        textbox->sprites = p.address;

        // TEX0: TBW 1, PSMT4 (0x14), 32 x 32, TCC 1, CLUT at block 0x1E00 loaded (CLD 2); TBP0 per cell.
        constexpr uint64_t kTex0 = (1ull << 14) | (0x14ull << 20) | (5ull << 26) | (5ull << 30) | (1ull << 34) |
                                   (0x1E00ull << 37) | (2ull << 61);
        int y = textbox->y;
        for (int row = 0; row < textbox->rows; ++row, y += textbox->pitchY)
        {
            y = keepOnAxis(y, textbox->charHeight);
            int x = textbox->x;
            for (int column = 0; column < textbox->columns; ++column, x += textbox->pitchX)
            {
                x = keepOnAxis(x, textbox->charWidth);
                const uint32_t block = static_cast<uint32_t>((column + row * textbox->columns) * 2 + 0x1E01) & 0x3FFFu;
                const uint64_t tex0 = kTex0 | block;
                const uint32_t z = static_cast<uint32_t>(textbox->z);
                p = emitWords(p, {static_cast<uint32_t>(tex0), static_cast<uint32_t>(tex0 >> 32), 6, 0,
                                  0, 0, 0, 0,
                                  static_cast<uint32_t>(x), static_cast<uint32_t>(y), z, 0xFF,
                                  0x3EC00000, 0x3F300000, 0, 0,
                                  static_cast<uint32_t>(x) + static_cast<uint32_t>(textbox->charWidth),
                                  static_cast<uint32_t>(y) + static_cast<uint32_t>(textbox->charHeight), z, 0xFF});
            }
        }
        emitWords(p, {0x8001, 0x10000000, 0xE, 0,
                      0, 0, 8, 0,
                      0x60000000, 0, 0, 0});
    }

    // Each layout setter rebuilds the box's packet.
    void TextBox_SetPos(GuestPtr<TextBox> textbox, int x, int y, int z)
    {
        textbox->x = x;
        textbox->y = y;
        textbox->z = z;
        TextBox_Build(textbox, textbox->clut);
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
        TextBox_Build(textbox, textbox->clut);
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
        TextBox_Build(textbox, textbox->clut);
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

    void LoadingScreen_SetSpritePos(GuestPtr<LoadingScreen> loading, int x, int y, int z)
    {
        loading->spriteX = x;
        loading->spriteY = y;
        loading->spriteZ = z;
        TexSprite_SetPosScale(loading->sprite, loading->spriteX, loading->spriteY, loading->spriteZ, 1.0f, 1.0f);
    }

    // The loading screen each frame; for the first 20 frames nothing (0). Then the sprite moves by its style:
    // circling (the vector (0, -10) turned about z once per 188 frames, x tripled, around its place), pulsing
    // (alpha from 128 down to 64 and back up over 128 frames), or still; background and sprite are drawn.
    // With `fadeIn` the screen fade rises by 4 a frame to 128, else it falls by 4 to 0; while it is not 0 the
    // fade (after a z clear) is queued, else the text box is drawn when shown. Returns 2 while the fade moves,
    // otherwise 0 after fading in and 1 after fading out. The original's frame is reserved whole.
    int LoadingScreen_Update(GuestPtr<LoadingScreen> loading, int fadeIn)
    {
        port::GuestFrame<0xA0> frame;
        const uint32_t sp = frame.ptr().address;
        loading->frame = loading->frame + 1;
        if (loading->frame < 20)
            return 0;
        const int style = loading->style;
        if (style == 1)
        {
            int alpha = (loading->frame % 128) & 0xFF;
            if (alpha < 0x40)
                alpha = (0x80 - alpha) & 0xFF;
            TexSprite_SetAlpha(loading->sprite, static_cast<uint8_t>(alpha), 1);
        }
        else if (style == 0)
        {
            const ps2f pi = 0x1.921FB6p+1f;
            const GuestPtr<Vec4> offset{sp + 0x50u}, turn{sp + 0x60u};
            *offset = *GuestPtr<Vec4>{globals::kLoadingCircle};
            sdk::sceVu0RotMatrixZ(turn.address, globals::kIdentityMatrix, fromInt(loading->frame % 188) / ps2f(30.0f) - pi);
            Math_MulMatVecVu0(offset, turn, offset);
            const int x = sdk::fptosi(ps2f(3.0f) * offset->x);
            const int placeX = loading->spriteX + x;
            const int y = sdk::fptosi(offset->y);
            TexSprite_SetPosScale(loading->sprite, placeX, loading->spriteY + y, loading->spriteZ, 1.0f, 1.0f);
        }
        TexSprite_Draw(loading->background);
        TexSprite_Draw(loading->sprite);
        int result = 1;
        if (fadeIn != 0)
        {
            result = 0;
            if (loading->fade < 128)
            {
                loading->fade = loading->fade + 4;
                if (loading->fade < 129)
                    result = 2;
                else
                    loading->fade = 128;
            }
        }
        else if (loading->fade > 0)
        {
            loading->fade = loading->fade - 4;
            if (loading->fade >= 0)
                result = 2;
            else
                loading->fade = 0;
        }
        if (loading->fade != 0)
        {
            DmaList_QueueClearZ();
            DmaList_QueueFade(loading->fade, 0);
        }
        else if (loading->showText != 0)
            TextBox_Draw(loading->text);
        return result;
    }

    // The blinking "please wait" line: its text box (0x1BDAC4), shown flag (0x1BDACC) and frame counter (0x1BDAC8).
    void WaitText_Set(GuestPtr<char> text) { TextBox_SetText(GuestPtr<TextBox>{port::load<uint32_t>(0x001BDAC4u)}, text, 0); }

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
    // The message window's update (vtable slot 0x14). While typing (state 6), Cross skips the typing delay and
    // the next character goes out; the result is then whatever the caller had in s0, which the original hands
    // back without setting v0 (its caller compares it with 7). Otherwise the widget update and its state.
    int MsgWin_Update(GuestPtr<MsgWin> msgwin)
    {
        const int callerS0 = port::guestS0();
        if (msgwin->base.state == 6)
        {
            if ((Pad_Pressed(0) & 0x40u) != 0)
                msgwin->typeDelay = 0;
            MsgWin_TypeNext(msgwin);
            return callerS0;
        }
        return Widget_Update(GuestPtr<Widget>::of(msgwin->base));
    }

    // The talk session's view and conversation, each through its widget draw slot (+0x20).
    void Talk_Draw(GuestPtr<TalkSession> talk, int pass)
    {
        const auto draw = [&](uint32_t widget) {
            if (widget != 0)
                port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(widget + 8u) + 0x20u), widget, pass);
        };
        draw(talk->view.address);
        draw(talk->conversation.address);
    }

    // A conversation: its message window, the menu, the card, the panel while set and, in pass 2 while a name is
    // entered (state 8), the name entry.
    void TalkConv_Draw(GuestPtr<TalkConv> conv, int pass)
    {
        MsgWin_Draw(GuestPtr<MsgWin>::of(conv->base), pass);
        const auto draw = [&](uint32_t widget) {
            port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(widget + 8u) + 0x20u), widget, pass);
        };
        draw(conv->menu.address);
        draw(conv->card.address);
        if (conv->panel)
            draw(conv->panel.address);
        if (pass == 2 && conv->nameEntry && conv->base.base.state == 8)
            NameEntry_Draw(conv->nameEntry);
    }

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

    // The minimap's DMA packet: the 16-colour palette at 0x1AF180 (8 x 2) to GS block 0x1E00, the cells as a
    // 128 x 128 4-bit image to block 0x1E01, a texture flush, then the map drawn as one sprite: GS state (A+D
    // 0x56), grey, TEX0 (PSMT4 128 x 128, CLUT at 0x1E00) and two (ST, XYZ2) corners.
    void Minimap_BuildPacket(GuestPtr<Minimap> minimap)
    {
        constexpr uint64_t kTex0 = 0x1E01ull | (2ull << 14) | (0x14ull << 20) | (7ull << 26) | (7ull << 30) | (1ull << 34) |
                                   (0x1E00ull << 37) | (2ull << 61);
        emitWords(GuestPtr<uint32_t>{minimap.address + 0x2000u},
                  {0x10000006, 0, 0x11000000, 0x50000006,
                   4, 0x10000000, 0xE, 0,
                   0, 0, 0x51, 0,
                   8, 2, 0x52, 0,
                   0, 0x11E00, 0x50, 0,
                   0, 0, 0x53, 0,
                   0x8004, 0x08000000, 0, 0,
                   0x30000004, 0x001AF180u & 0x7FFFFFFFu, 0, 0x50000004,
                   0x10000006, 0, 0x11000000, 0x50000006,
                   4, 0x10000000, 0xE, 0,
                   0, 0, 0x51, 0,
                   0x80, 0x80, 0x52, 0,
                   0, 0x14021E01, 0x50, 0,
                   0, 0, 0x53, 0,
                   0x8200, 0x08000000, 0, 0,
                   0x30000200, minimap.address & 0x7FFFFFFFu, 0, 0x50000200,
                   0x10000002, 0, 0, 0x50000002,
                   0x8001, 0x10000000, 0xE, 0,
                   0, 0, 0x3F, 0,
                   0x10000008, 0, 0x11000000, 0x50000008,
                   0x8007, 0x10000000, 0xE, 0,
                   0x56, 0, 0, 0,
                   0x80808080u, 0x3F800000, 1, 0,
                   static_cast<uint32_t>(kTex0), static_cast<uint32_t>(kTex0 >> 32), 6, 0,
                   0x3F800000, 0x3F800000, 2, 0,
                   0x812087A0u, 0x10, 5, 0,
                   0, 0, 2, 0,
                   0x85809060u, 0x10, 5, 0,
                   0x60000000, 0, 0, 0});
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

    // The status panel's packet at `packet` (kept at +0x9C): grey RGBAQ, TEX0 for its 256 x 128 8-bit texture
    // at block 0x3F7C (palette at 0x3FFC, CLUT load 1), PRIM textured sprite, then the table's 19 sprites at
    // depth 0xFFF000, with the colour set again before the 16th and 19th; a ret ends it. Returns the address
    // after it.
    GuestPtr<uint32_t> StatusPanel_BuildPacket(GuestPtr<StatusPanel> panel, GuestPtr<uint32_t> packet)
    {
        panel->panelPacket = packet.address;
        const uint64_t tex0 = 0x3F7Cu | (4ull << 14) | (0x13ull << 20) | (static_cast<uint64_t>(Math_Log2(0x100) & 0xF) << 26) |
                              (static_cast<uint64_t>(Math_Log2(0x80) & 0xF) << 30) | (1ull << 34) | (0x3FFCull << 37) | (1ull << 61);
        const uint32_t header[20] = {
            0x10000053u, 0, 0x11000000u, 0x50000053u,                                 // cnt 0x53; VIF flush, direct 0x53
            0x00008052u, 0x10000000u, 0x0000000Eu, 0,                                 // GIF tag: 0x52 loops, eop, A+D
            0x80808080u, 0x3F800000u, 0x01u, 0,                                       // RGBAQ: grey, Q 1
            static_cast<uint32_t>(tex0), static_cast<uint32_t>(tex0 >> 32), 0x06u, 0, // TEX0_1
            0x16u, 0, 0, 0,                                                           // PRIM: textured sprite
        };
        GuestPtr<uint32_t> p = packet;
        for (int32_t i = 0; i < 20; ++i)
            p[i] = header[i];
        p = p + 20;
        const auto put = [&](uint32_t a, uint32_t b, uint32_t reg) {
            p[0] = a;
            p[1] = b;
            p[2] = reg;
            p[3] = 0;
            p = p + 4;
        };
        for (uint32_t i = 0; i < 19; ++i)
        {
            if (i == 15 || i == 18)
                put(0x80808080u, 0x3F800000u, 0x01u);
            const GuestPtr<uint32_t> sprite{globals::kStatusPanelSprites + 0x20u * i};
            put(sprite[0], sprite[1], 0x02u);                               // ST
            put(sprite[4] | sprite[5] << 16, 0x00FFF000u, 0x05u);            // XYZ2
            put(sprite[2], sprite[3], 0x02u);
            put(sprite[6] | sprite[7] << 16, 0x00FFF000u, 0x05u);
        }
        put(0x60000000u, 0, 0);                                             // ret
        return p;
    }

    // title: draws a layer set (0x0A/0x0B logo sprite, 0x14/0x15 logo model, 0x1E/0x1F title, 0x20/0x21 title and
    // menu choices, 0x28/0x29 title, model and load menu) and runs the screen fade. A new layer set restarts
    // the fade at -0x80 (even sets) or 0 (odd), then it moves by the set's step each frame (1, 2 or 4). Returns
    // 1 when the fade is through (parked at -1 or 0x7F), 2 in its first step from 0x40 on, else 0.
    int Title_DrawCommon(GuestPtr<SceneA> scene, int layers)
    {
        if (scene->layers != layers)
        {
            scene->layers = layers;
            scene->fade = (scene->layers & 1) == 0 ? -0x80 : 0;
        }
        int step = 4;
        switch (scene->layers)
        {
        case 0x29:
        case 0x28:
            step = 2;
            TexSprite_Draw(scene->titleSprite);
            A3DObject_Draw(scene->menuModel);
            ListMenu_Draw(scene->loadMenu);
            break;
        case 0x21:
        case 0x20:
            step = 2;
            TexSprite_Draw(scene->titleSprite);
            Title_DrawChoices(scene);
            break;
        case 0x1F:
        case 0x1E:
            TexSprite_Draw(scene->titleSprite);
            break;
        case 0x15:
        case 0x14:
            step = 1;
            A3DObject_Draw(scene->logoModel);
            break;
        case 0x0B:
        case 0x0A:
            TexSprite_Draw(scene->logoSprite);
            break;
        default:
            break;
        }
        if (layers != 0)
            scene->fade += step;
        DmaList_QueueClearZ();
        DmaList_QueueFade(scene->fade < 0 ? -scene->fade : scene->fade, 0);
        const int fade = scene->fade;
        if (fade == 0)
        {
            scene->fade = -1;
            return 1;
        }
        if (fade >= 0x80)
        {
            scene->fade = 0x7F;
            return 1;
        }
        if (fade < 0x40)
            return 0;
        return fade < step + 0x40 ? 2 : 0;
    }

    // title: the title screen waiting for input. The first frame sets up the "[SELECT]SoundTest" hint and the idle
    // timer; then the menu runs, with two choices once a save exists (the cursor moving to the second if only one
    // was shown before). Returns the menu input's result, its idle restart (-2) as -1 with the timer restarted.
    int Title_PressStart(GuestPtr<SceneA> scene)
    {
        if (port::load<int8_t>(globals::kPressStartStatic + 4u) == 0)
        {
            port::store<int32_t>(globals::kPressStartStatic, 0);
            port::store<int8_t>(globals::kPressStartStatic + 4u, 1);
        }
        TexSprite_Draw(scene->titleSprite);
        if (scene->showHint != 0)
            TextBox_Draw(scene->hintText);
        if (scene->pressStep == 1)
        {
            if (McManager_HasAnySave() == 0)
                scene->choiceCount = 1;
            else
            {
                if (scene->choiceCount == 1)
                    scene->choice = 1;
                scene->choiceCount = 2;
            }
            int result = Title_MenuInput(scene);
            if (result == -2)
            {
                scene->idleTimer = 1800;
                result = -1;
            }
            return result;
        }
        if (scene->pressStep == 0)
        {
            scene->idleTimer = 1800;
            scene->choiceCount = 1;
            ++scene->pressStep;
            TextBox_SetText(scene->hintText, GuestPtr<char>{globals::kSoundTestHint}, 0);
        }
        return -1;
    }

    // title: the logo intro, one step (+0xC4) per frame. 0 starts the intro music (sequence slot 15) and goes
    // straight on to 1: the logo model's animation under the title layers until mm_Title_DrawCommon reports
    // them done. 2 plays the rest of the animation; 3 fades the title sprite in (alpha 0 to 127, one a frame),
    // then plays sound 0x4F; 4 shows it while the logo letters bounce in; 5 holds it 50 frames and returns 1
    // (every other frame 0). In steps 1 and 2 each timed effect plays as the animation reaches its frame.
    int Title_Intro(GuestPtr<SceneA> scene)
    {
        const auto playDueSound = [&] {
            const int next = scene->logoSound;
            if (scene->logoTime < ps2f(port::load<float>(globals::kIntroSoundFrames + 4u * static_cast<uint32_t>(next))))
                return;
            scene->logoSound = next + 1;
            Sound_PlaySe(port::load<uint8_t>(globals::kIntroSounds + static_cast<uint32_t>(next)), 0, 0x3C, 0.0f, 0.0f);
        };
        switch (scene->pressStep)
        {
        case 0:
            SoundCmd_0F(0xF, 0x1800, 0);
            SoundCmd_PlaySequence(0xF, 7, 0, 0, 0);
            scene->logoTime = 0.0f;
            scene->logoSound = 0;
            scene->pressStep = scene->pressStep + 1;
            [[fallthrough]];
        case 1:
            Instance_DrawSwaying(scene->logoModel, 0, scene->logoTime);
            playDueSound();
            scene->logoTime = scene->logoTime + ps2f(1.0f);
            if (Title_DrawCommon(scene, 0x14) == 1)
                scene->pressStep = scene->pressStep + 1;
            return 0;
        case 2:
        {
            Instance_DrawSwaying(scene->logoModel, 0, scene->logoTime);
            A3DObject_Draw(scene->logoModel);
            playDueSound();
            scene->logoTime = scene->logoTime + ps2f(1.0f);
            const ps2f length = A3D_GetMeshField0C(scene->logoModel, 0);
            if (scene->logoTime < length)
                return 0;
            scene->pressStep = scene->pressStep + 1;
            scene->introFrame = 0;
            return 0;
        }
        case 3:
        {
            Instance_DrawSwaying(scene->logoModel, 0, scene->logoTime);
            A3DObject_Draw(scene->logoModel);
            const int alpha = scene->introFrame;
            scene->introFrame = alpha + 1;
            TexSprite_SetAlpha(scene->titleSprite, static_cast<uint8_t>(alpha), 0);
            TexSprite_Draw(scene->titleSprite);
            if (scene->introFrame < 0x80)
                return 0;
            scene->pressStep = scene->pressStep + 1;
            scene->introFrame = 5;
            Sound_PlaySe(0x4F, 0, 0x3C, 0.0f, 0.0f);
            return 0;
        }
        case 4:
            TexSprite_Draw(scene->titleSprite);
            if (Title_LogoLetters(scene) == 0)
                return 0;
            scene->pressStep = scene->pressStep + 1;
            scene->introFrame = 50;
            return 0;
        case 5:
            TexSprite_Draw(scene->titleSprite);
            scene->introFrame = scene->introFrame - 1;
            return scene->introFrame > 0 ? 0 : 1;
        default:
            return 0;
        }
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

    // title: the menu choices at rest. The selected one, at full size, wobbles: its offset is the vectors
    // (0, -5) and (0, -10) turned about z (one turn per 62 and per 188 frames), the first giving y and both
    // adding up for x. The other is drawn at 0.6, and with a single choice only the first is shown, as in
    // mm_Title_DrawChoices; +0x48 counts the frames. The original's frame is reserved whole and its two
    // vectors and two matrices kept at their offsets in it, as sceVu0RotMatrixZ runs as guest code.
    void Title_DrawChoicesIdle(GuestPtr<SceneA> scene)
    {
        port::GuestFrame<0xF0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<Vec4> small{sp + 0x50u}, large{sp + 0x60u}, turnSmall{sp + 0x70u}, turnLarge{sp + 0xB0u};
        scene->pulseFrame = scene->pulseFrame + 1;
        *small = *GuestPtr<Vec4>{globals::kChoiceWobbleSmall};
        *large = *GuestPtr<Vec4>{globals::kChoiceWobbleLarge};
        const int count = scene->pulseFrame;
        const ps2f pi = 3.14159265f;
        const ps2f angleSmall = fromInt(count % 62) / ps2f(10.0f) - pi;
        const ps2f angleLarge = fromInt(count % 188) / ps2f(30.0f) - pi;
        sdk::sceVu0RotMatrixZ(turnSmall.address, globals::kIdentityMatrix, angleSmall);
        sdk::sceVu0RotMatrixZ(turnLarge.address, globals::kIdentityMatrix, angleLarge);
        Math_MulMatVecVu0(small, turnSmall, small);
        Math_MulMatVecVu0(large, turnLarge, large);
        const int dx = sdk::fptosi(fromInt(sdk::fptosi(small->x)) + large->x);
        const int dy = sdk::fptosi(small->y);
        if (scene->choiceCount == 1)
        {
            TexSprite_SetPosScale(scene->choices[0], dx + 0xBE, dy + 0x154, 0x3F00, 1.0f, 1.0f);
            TexSprite_Draw(scene->choices[0]);
        }
        else if (scene->choice == 0)
        {
            TexSprite_SetPosScale(scene->choices[0], dx + 0xBE, dy + 0x13C, 0x3F00, 1.0f, 1.0f);
            TexSprite_SetPosScale(scene->choices[1], 0xF0, 0x178, 0x3F00, 0.6f, 0.6f);
            TexSprite_Draw(scene->choices[0]);
            TexSprite_Draw(scene->choices[1]);
        }
        else
        {
            TexSprite_SetPosScale(scene->choices[0], 0xF0, 0x146, 0x3F00, 0.6f, 0.6f);
            TexSprite_SetPosScale(scene->choices[1], dx + 0xBE, dy + 0x168, 0x3F00, 1.0f, 1.0f);
            TexSprite_Draw(scene->choices[0]);
            TexSprite_Draw(scene->choices[1]);
        }
    }

    // boot: the memory card check before the title, one step (+0xC4) per call; returns 1 when the game may go
    // on. 0: on at once when the card manager reports a save or a slot is fine; waits while a card is still
    // being read; else places the warning box. 1: the warning: no card in either slot, or which slot lacks the
    // space (both, 1 or 2), and "press Cross". 2: moves on when a save or a fine card turns up, goes back to 1
    // when a slot's status changes (and is not being read), and moves on with Cross (sound 1) too. 3: after 40
    // more frames empties the box. 4: done. With `noPolling` set, step 0 waits and step 2 only takes Cross.
    int SceneA_MemoryCardCheck(GuestPtr<SceneA> scene, int noPolling)
    {
        const auto fine = [](int status) { return status == 0 || status == 3; };
        const auto text = [](uint32_t address) { return GuestPtr<char>{address}; };
        switch (scene->pressStep)
        {
        case 0:
            if (noPolling != 0)
                return 0;
            if (McManager_HasAnySave() != 0)
                return 1;
            globals::cardStatus0.set(McManager_GetCardStatus(scene->mc, 0));
            if (fine(globals::cardStatus0.get()))
                return 1;
            globals::cardStatus1.set(McManager_GetCardStatus(scene->mc, 1));
            if (fine(globals::cardStatus1.get()))
                return 1;
            if (globals::cardStatus0.get() == 4 || globals::cardStatus1.get() == 4)
                return 0;
            TextBox_SetPos(scene->hintText, 0x6FC0, 0x7C20, static_cast<int>(0xFFF00000u));
            scene->pressStep = scene->pressStep + 1;
            return 0;
        case 1:
            TextBox_SetText(scene->hintText, text(globals::kCardTextHeading), 0);
            if (globals::cardStatus0.get() == 2 && globals::cardStatus1.get() == 2)
                TextBox_AppendText(scene->hintText, text(globals::kCardTextNoCard), 0);
            else
            {
                TextBox_AppendText(scene->hintText, text(globals::kCardTextSlot), 0);
                const int first = globals::cardStatus0.get();
                if (first == 1 && globals::cardStatus1.get() == 1)
                    TextBox_AppendText(scene->hintText, text(globals::kCardTextBoth), 0);
                else if (first == 1)
                    TextBox_AppendText(scene->hintText, text(globals::kCardTextSlot1), 0);
                else
                    TextBox_AppendText(scene->hintText, text(globals::kCardTextSlot2), 0);
                TextBox_AppendText(scene->hintText, text(globals::kCardTextNoSpace), 0);
            }
            TextBox_AppendText(scene->hintText, text(globals::kCardTextEnd), 0);
            TextBox_Draw(scene->hintText);
            scene->pressStep = scene->pressStep + 1;
            return 0;
        case 2:
            if (noPolling == 0)
            {
                const int first = McManager_GetCardStatus(scene->mc, 0);
                const int second = McManager_GetCardStatus(scene->mc, 1);
                if (McManager_HasAnySave() != 0 || fine(first) || fine(second))
                    scene->pressStep = scene->pressStep + 1;
                else if ((globals::cardStatus0.get() != first || globals::cardStatus1.get() != second) && first != 4 && second != 4)
                {
                    globals::cardStatus0.set(first);
                    globals::cardStatus1.set(second);
                    scene->pressStep = scene->pressStep - 1;
                }
            }
            if (Pad_Pressed(0) == 0x40) // cross
            {
                Sound_PlaySe(1, 0, 0x3C, 0.0f, 0.0f);
                scene->pressStep = scene->pressStep + 1;
                scene->waitFrames = 0;
            }
            TextBox_Draw(scene->hintText);
            return 0;
        case 3:
            scene->waitFrames = scene->waitFrames + 1;
            if (scene->waitFrames < 41)
                return 0;
            TextBox_SetText(scene->hintText, text(globals::kEmptyText), 0);
            TextBox_Draw(scene->hintText);
            scene->pressStep = scene->pressStep + 1;
            return 0;
        case 4:
            scene->waitFrames = 0;
            return 1;
        default:
            return 0;
        }
    }

    // title: the menu's input. Entering the sound test code (kSoundTestCode; a wrong button starts it over)
    // shows the "[SELECT]SoundTest" line, with sound 0x50. While the selected choice pulses: -2, then the choice
    // once the pulse is over. Otherwise Up and Down move the cursor (sound 0) when there is more than one choice,
    // the choices are drawn at rest, and Cross or Start start the pulse (sound 1). Returns -2 after Up, Down,
    // Cross or Start, 10 for Select while the sound test line shows, else -1.
    int Title_MenuInput(GuestPtr<SceneA> scene)
    {
        const uint32_t pressed = Pad_Pressed(0);
        const auto codeButton = [](int step) { return port::load<uint32_t>(globals::kSoundTestCode + 4u * static_cast<uint32_t>(step)); };
        int result = -1;
        if (pressed != 0)
        {
            const int step = scene->codeStep;
            if (pressed != codeButton(step))
                scene->codeStep = 0;
            else
            {
                scene->codeStep = static_cast<uint8_t>(step + 1);
                if (codeButton(scene->codeStep) == 0)
                {
                    scene->showHint = 1;
                    Sound_PlaySe(0x50, 0, 0x3C, 0.0f, 0.0f);
                }
            }
        }
        if (scene->choicePulsing != 0)
            return Title_DrawChoices(scene) == 0 ? -2 : scene->choice;
        const int count = scene->choiceCount;
        if (count >= 2)
        {
            if (pressed == 0x1000) // up
            {
                if (scene->choice > 0)
                {
                    scene->choice = scene->choice - 1;
                    Sound_PlaySe(0, 0, 0x3C, 0.0f, 0.0f);
                }
            }
            else if (pressed == 0x4000) // down
            {
                if (scene->choice + 1 < count)
                {
                    scene->choice = scene->choice + 1;
                    Sound_PlaySe(0, 0, 0x3C, 0.0f, 0.0f);
                }
            }
        }
        if (pressed == 0x1000 || pressed == 0x4000)
            result = -2;
        if (!(scene->choice < scene->choiceCount))
            scene->choice = scene->choiceCount - 1;
        Title_DrawChoicesIdle(scene);
        if (pressed == 0x40 || pressed == 0x800) // cross, start
        {
            Sound_PlaySe(1, 0, 0x3C, 0.0f, 0.0f);
            scene->choicePulsing = 1;
            scene->pulseFrame = 0;
            result = -2;
        }
        if (scene->showHint != 0 && pressed == 0x100) // select
            result = 10;
        return result;
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
    PORT_FN(Widget_Update);
    PORT_FN(MsgWin_Update);
    PORT_FN(Talk_Draw);
    PORT_FN(TalkConv_Draw);
    PORT_FN(Widget_Idle);
    PORT_FN(TextBox_SetText);
    PORT_FN(TextBox_AppendText);
    PORT_FN(TextBox_Draw);
    PORT_FN(TextBox_Build);
    PORT_FN(TextBox_SetPos);
    PORT_FN(TextBox_SetPosAndSize);
    PORT_FN(TextBox_SetCharSize);
    PORT_FN(TexSprite_SetPosScale);
    PORT_FN(LoadingScreen_SetTextPos);
    PORT_FN(LoadingScreen_SetSpritePos);
    PORT_FN(LoadingScreen_Update);
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
    PORT_FN(Minimap_BuildPacket);
    PORT_FN(Minimap_Draw);
    PORT_FN(PauseMenu_ctor);
    PORT_FN(PauseMenu_Draw);
    PORT_FN(StatusPanel_Draw);
    PORT_FN(StatusPanel_BuildPacket);
    PORT_FN(Title_LogoLetters);
    PORT_FN(Title_Intro);
    PORT_FN(Title_DrawChoicesIdle);
    PORT_FN(Title_MenuInput);
    PORT_FN(SceneA_MemoryCardCheck);
    PORT_FN(CityCursor_LayoutInfo);
    PORT_FN(Title_DrawChoices);
    PORT_FN(Title_DrawCommon);
    PORT_FN(Title_PressStart);
    PORT_FN(ListMenu_HandleInput);
}
