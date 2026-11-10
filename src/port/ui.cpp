// Windows and their widget base class, text boxes, the city cursor and the notice line.

#include "game_functions.h"
#include "game_globals.h"
#include "widgets.h"
#include "render.h"
#include "lifetime.h"
#include "sdk.h"
#include "vu0.h"

#include <bit>
#include <initializer_list>

namespace mm
{
    // Widget states: 0 idle, 1 opening, 2 opened, 3 active, 4 closing, 5 closed.
    GuestPtr<Widget> Widget_ctor(GuestPtr<Widget> widget)
    {
        widget->vtable = globals::kWidgetVtable;
        widget->state = 0;
        widget->timer = 0.0f;
        return widget;
    }

    int Widget_Open(GuestPtr<Widget> widget)
    {
        widget->timer = 0.0f;
        widget->state = 1;
        return 1; // the original leaves the 1 it stored in v0
    }

    void Widget_Close(GuestPtr<Widget> widget)
    {
        widget->timer = 0.0f;
        widget->state = 4;
    }

    int Widget_GetState(GuestPtr<Widget> widget) { return widget->state; }

    // Runs the handler for the widget's state (the vtable's onState slots: idle, opening, opened, active,
    // closing, closed) and returns the state after it.
    int Widget_Update(GuestPtr<Widget> widget)
    {
        const uint32_t state = static_cast<uint32_t>(widget->state);
        if (state < 6)
            port::callGuest<void>(widget->vtable->onState[state], widget.address);
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
    void WaitText_Set(GuestPtr<char> text) { TextBox_SetText(GuestPtr<TextBox>{globals::waitTextBox.get()}, text, 0); }

    void WaitText_Draw()
    {
        if (globals::waitTextMade.get() == 0)
            return;
        if ((static_cast<uint32_t>(globals::waitTextFrames.get()) & 0x30u) != 0)
            TextBox_Draw(GuestPtr<TextBox>{globals::waitTextBox.get()});
        globals::waitTextFrames.set(globals::waitTextFrames.get() + 1);
    }

    void Notice_Draw(GuestPtr<Notice> notice)
    {
        if (notice->id != -1)
            TextBox_Draw(notice->text);
        if (notice->secondLine != 0)
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

    // Places the list's box at x, y (screen pixels) with its texts' depth, and the clipping mask's four sprites around
    // the box (ListMenuMask), to the screen's edges. The box runs from 0x20 left of x (the cursor frame's column) to
    // width right of it, and from a third of a row above the first row to a third of a row below the last. Corners
    // are GS 12.4 fixed point with the screen (640 by 224) at 0x6C0, 0x790.
    void ListMenu_Place(GuestPtr<ListMenu> menu, int x, int y, int depth)
    {
        menu->x = x;
        menu->y = y;
        menu->depth = depth;
        const int third = menu->itemHeight / 3;
        const uint32_t left = static_cast<uint32_t>(menu->x + 0x6A0) << 4;
        const uint32_t right = static_cast<uint32_t>(menu->x + menu->width + 0x6C0) << 4;
        const auto gsY = [](int row) { return static_cast<uint32_t>(row + 0x790) << 20; };
        GsAdWrite *corner = menu->mask.corners;
        corner[0].value[0] = 0x79006C00u;                // left of the box: the screen's top left
        corner[1].value[0] = left | 0x87000000u;         // to the box's left at the bottom
        corner[2].value[0] = left | 0x79000000u;         // above: the box's left at the top
        corner[3].value[0] = gsY(menu->y - third) | right;       // to the box's top right
        corner[4].value[0] = gsY(menu->y + menu->visibleItems * menu->itemHeight + third) | left; // below
        corner[5].value[0] = right | 0x87000000u;
        corner[6].value[0] = right | 0x79000000u;        // right of the box: its right at the top
        corner[7].value[0] = 0x87009400u;                // to the screen's bottom right
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

    // The menu: its clipping mask first (ListMenuMask), then the item texts centred in their rows (one row beyond each
    // end of the box too, for the mask to cut while the list scrolls), then the cursor frame beside the cursor row; Z
    // is cleared afterwards, which takes the mask away again.
    void ListMenu_Draw(GuestPtr<ListMenu> menu)
    {
        if (menu->itemCount == 0)
            return;
        Dma_AppendCall(static_cast<int>(menu.address));
        int y = menu->scroll + menu->y;
        for (int i = 0; i < menu->itemCount; ++i, y += menu->itemHeight)
        {
            const int height = menu->itemHeight;
            const bool visible = menu->y - height <= y && y <= menu->y + height * (menu->visibleItems + 1);
            if (!visible || !listItem(menu, i)->text)
                continue;
            const int centre = (height - listItem(menu, i)->textHeight) / 2;
            TextBox_SetPos(listItem(menu, i)->text, (menu->x + 0x6C0) << 4, (y + centre + 0x790) << 4, menu->depth);
            TextBox_Draw(listItem(menu, i)->text);
        }
        Frame_SetRect(menu->cursorFrame, menu->x - 0x20, (menu->itemHeight - 12) / 2 + menu->y + menu->cursorY, 0x18, 0xC);
        Dma_AppendCall(static_cast<int>(menu->cursorFrame.address));
        DmaList_QueueClearZ();
    }

    // Empties the text of `item`; removing the last filled item shrinks the list. -1 empties every item.
    void ListMenu_ClearItem(GuestPtr<ListMenu> menu, int item)
    {
        const GuestPtr<char> empty{globals::kListMenuEmptyText};
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
                frame->sprite.stA.s = in32nds(cell.s0);
                frame->sprite.stA.t = in32nds(cell.t0);
                frame->sprite.stB.s = in32nds(cell.s1);
                frame->sprite.stB.t = in32nds(cell.t1);
                return;
            }
    }

    // Texture colours (RGBA bytes): the palette, or every pixel of an image without one.
    namespace
    {
        uint32_t texColourCount(GuestPtr<TexFileHeader> image)
        {
            const uint16_t paletteSize = image->paletteSize;
            if (paletteSize != 0)
                return paletteSize;
            return static_cast<uint32_t>(image->tileWidth) * image->tileHeight;
        }
    }

    // Sets every colour's alpha; with `keepClear`, fully transparent colours stay so.
    void TexSprite_SetAlpha(GuestPtr<TexSprite> sprite, uint8_t alpha, int keepClear)
    {
        const GuestPtr<uint8_t> image{sprite->image.address};
        const uint32_t count = texColourCount(sprite->image);
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
        const uint32_t count = texColourCount(sprite->image);
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
        menuwin->base.vtable = globals::kMenuWinVtable;
        menuwin->wideList = 0;
        menuwin->model = globals::talkSceneModel.get();
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

    // The talk session's view and conversation, each through its widget draw slot.
    void Talk_Draw(GuestPtr<TalkSession> talk, int pass)
    {
        const auto draw = [&](uint32_t widget) {
            if (widget != 0)
                port::callGuest<void>(widgetMethod(widget, &WidgetVtable::draw), widget, pass);
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
            port::callGuest<void>(widgetMethod(widget, &WidgetVtable::draw), widget, pass);
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
        const GuestPtr<Vec4> matrix = worldMatrix(modelNode(msgwin->model, msgwin->node));
        Mat4_Copy(matrix, GuestPtr<Vec4>{globals::kIdentityMatrix}); // identity
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
        if (globals::msgWinBlinkStarted.get() == 0)
        {
            globals::msgWinBlinkFrames.set(0);
            globals::msgWinBlinkStarted.set(1);
        }
        globals::msgWinBlinkFrames.set(globals::msgWinBlinkFrames.get() + 1);
        if (msgwin->base.state == 3 && ((globals::msgWinBlinkFrames.get() >> 4) & 1) == 0)
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
        const GuestPtr<Vec4> matrix = worldMatrix(modelNode(menuwin->model, node));
        Mat4_Copy(matrix, GuestPtr<Vec4>{globals::kIdentityMatrix}); // identity
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
        frame->sprite.xyzA.xy = gsXY(x, y);
        frame->sprite.xyzB.xy = gsXY(x + width, y + height);
    }

    // A window frame: the packet template (0xB0 bytes at 0x1B04B0, its sprite and four words over the fields after it),
    // then the style, place and size, and the corners' depth (their Z).
    GuestPtr<Frame> Frame_ctor(GuestPtr<Frame> frame, int style, int x, int y, int width, int height, uint32_t depth)
    {
        BaseObject_ctor(static_cast<int>(frame.address));
        for (uint32_t i = 0; i < 0xB0u; i += 4)
            port::store<uint32_t>(frame.address + i, port::load<uint32_t>(globals::kFrameTemplate + i));
        Frame_SetStyle(frame, style);
        frame->width = width;
        frame->height = height;
        frame->x = x;
        frame->y = y;
        const auto gsXY = [](int gx, int gy)
        { return (static_cast<uint32_t>(gx + 0x6C0) << 4) | (static_cast<uint32_t>(gy + 0x790) << 20); };
        frame->sprite.xyzA.xy = gsXY(x, y);
        frame->sprite.xyzB.xy = gsXY(x + width, y + height);
        frame->sprite.xyzA.z = depth;
        frame->sprite.xyzB.z = depth;
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
        frame->sprite.xyzA.xy = static_cast<uint32_t>(x0) | (static_cast<uint32_t>(y0) << 16);
        frame->sprite.xyzB.xy = static_cast<uint32_t>(x1) | (static_cast<uint32_t>(y1) << 16);
    }

    // The cursor's info window for this frame: its packet and info lines, then either the opening sprite (SpritePacket)
    // over a cleared Z (mode 4) or a dimmed screen with the fade frame (other non-zero modes).
    void CityCursor_Draw(GuestPtr<CityCursor> cursor)
    {
        if (cursor->visible == 0)
            return;
        const uint32_t parity = globals::frameParity.get() & 1u;
        const GuestPtr<CityCursorView> view = GuestPtr<CityCursorView>::of(cursor->views[parity]);
        Dma_AppendCall(static_cast<int>(view->frame.address));
        // lineCount is not checked against 5, so the lines are read as guest addresses like the original.
        const GuestPtr<uint32_t> lines{view.address}, lineFrames = view.field(&CityCursorView::lineFrames).as<uint32_t>();
        for (int i = 0; i < cursor->lineCount; ++i)
        {
            TextBox_Draw(GuestPtr<TextBox>{lines[i]});
            Dma_AppendCall(static_cast<int>(lineFrames[i]));
        }
        if (cursor->mode == 4)
        {
            DmaList_QueueClearZ();
            Dma_AppendCall(static_cast<int>(view.field(&CityCursorView::sprite).address));
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
        const uint32_t parity = globals::frameParity.get() & 1u;
        const GuestPtr<CityCursorView> view = GuestPtr<CityCursorView>::of(cursor->views[parity]);
        // As in CityCursor_Draw, lineCount is not checked against 5.
        const GuestPtr<uint32_t> lineText{view.address}, lineFrames = view.field(&CityCursorView::lineFrames).as<uint32_t>();
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

    void CityCursor_SetTarget(GuestPtr<CityCursor> cursor, uint16_t target, int isPerson, GuestPtr<Vec4> position)
    {
        cursor->targetIsPerson = isPerson;
        cursor->target = target;
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
        sdk::sceVu0RotMatrixZ(matrix.address, globals::kIdentityMatrix, angle); // from the identity
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
        emitWords(minimap.field(&Minimap::packet).as<uint32_t>(),
                  {0x10000006, 0, 0x11000000, 0x50000006,
                   4, 0x10000000, 0xE, 0,
                   0, 0, 0x51, 0,
                   8, 2, 0x52, 0,
                   0, 0x11E00, 0x50, 0,
                   0, 0, 0x53, 0,
                   0x8004, 0x08000000, 0, 0,
                   0x30000004, globals::kMinimapUpload & 0x7FFFFFFFu, 0, 0x50000004,
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
        minimap->model = globals::minimapModel.get();
        minimap->frameModel = globals::minimapFrameModel.get();
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
            port::store<uint32_t>(globals::kMinimapBlinkA, (blink & 0x20u) != 0 ? 0x6000C0C0u : 0x60545AA3u);
        if ((blink & 7u) == 0)
            port::store<uint32_t>(globals::kMinimapBlinkB, (blink & 8u) != 0 ? 0x6000F0F0u : 0x60545AA3u);
        A3DObject_Draw(minimap->frameModel);
        Dma_AppendCall(static_cast<int>(GuestPtr<uint8_t>::of(minimap->packet[0]).address));
        A3DObject_Draw(minimap->model);
    }

    // The pause menu starts cleared, with the default save name from 0x1B73D0 (Shift-JIS).
    GuestPtr<PauseMenu> PauseMenu_ctor(GuestPtr<PauseMenu> pause)
    {
        for (uint32_t offset = 0; offset < 0x14; offset += 4)
            port::store<uint32_t>(pause.address + offset, 0);
        pause->openTime = 0.0f;
        sdk::strcpy(pause.field(&PauseMenu::saveTitle).address, globals::kPauseSaveTitle);
        return pause;
    }

    // The pause menu: the window (node 0 of the shared window model, scaled by 4) slides from its closed to its
    // open position as the menu opens (openTime reaching 120), then the slot list while picking a slot (state 4)
    // or else the main menu, then the message box.
    void PauseMenu_Draw(GuestPtr<PauseMenu> pause)
    {
        const GuestPtr<A3DObject> window = globals::menuWindowModel.get();
        const ps2f t = pause->openTime / ps2f(120.0f);
        const ps2f rest = ps2f(1.0f) - t;
        const GuestPtr<Vec4> open{globals::kPauseWindowOpen}, closed{globals::kPauseWindowClosed};
        const GuestPtr<Vec4> matrix = worldMatrix(modelNode(window, 0));
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
            const GuestPtr<Vec4> matrix = worldMatrix(modelNode(model, node));
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

    // The status panel's packet at `packet` (kept in panelPacket): grey RGBAQ, TEX0 for its 256 x 128 8-bit texture
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
    int Title_DrawCommon(GuestPtr<TitleScene> scene, int layers)
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
    int Title_PressStart(GuestPtr<TitleScene> scene)
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

    // title: the logo intro, one step (pressStep) per frame. 0 starts the intro music (sequence slot 15) and goes
    // straight on to 1: the logo model's animation under the title layers until mm_Title_DrawCommon reports
    // them done. 2 plays the rest of the animation; 3 fades the title sprite in (alpha 0 to 127, one a frame),
    // then plays sound 0x4F; 4 shows it while the logo letters bounce in; 5 holds it 50 frames and returns 1
    // (every other frame 0). In steps 1 and 2 each timed effect plays as the animation reaches its frame.
    int Title_Intro(GuestPtr<TitleScene> scene)
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
            SoundCmd_MidiSetVolume(0xF, 0x1800, 0);
            SoundCmd_MidiPlay(0xF, 7, 0, 0, 0);
            scene->logoTime = 0.0f;
            scene->logoSound = 0;
            scene->pressStep = scene->pressStep + 1;
            [[fallthrough]];
        case 1:
            A3D_Animate(scene->logoModel, 0, scene->logoTime);
            playDueSound();
            scene->logoTime = scene->logoTime + ps2f(1.0f);
            if (Title_DrawCommon(scene, 0x14) == 1)
                scene->pressStep = scene->pressStep + 1;
            return 0;
        case 2:
        {
            A3D_Animate(scene->logoModel, 0, scene->logoTime);
            A3DObject_Draw(scene->logoModel);
            playDueSound();
            scene->logoTime = scene->logoTime + ps2f(1.0f);
            const ps2f length = A3D_GetAnimLength(scene->logoModel, 0);
            if (scene->logoTime < length)
                return 0;
            scene->pressStep = scene->pressStep + 1;
            scene->introFrame = 0;
            return 0;
        }
        case 3:
        {
            A3D_Animate(scene->logoModel, 0, scene->logoTime);
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
    int Title_LogoLetters(GuestPtr<TitleScene> scene)
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
    // mm_Title_DrawChoices; pulseFrame counts the frames. The original's frame is reserved whole and its two
    // vectors and two matrices kept at their offsets in it, as sceVu0RotMatrixZ runs as guest code.
    void Title_DrawChoicesIdle(GuestPtr<TitleScene> scene)
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

    // boot: the memory card check before the title, one step (pressStep) per call; returns 1 when the game may go
    // on. 0: on at once when the card manager reports a save or a slot is fine; waits while a card is still
    // being read; else places the warning box. 1: the warning: no card in either slot, or which slot lacks the
    // space (both, 1 or 2), and "press Cross". 2: moves on when a save or a fine card turns up, goes back to 1
    // when a slot's status changes (and is not being read), and moves on with Cross (sound 1) too. 3: after 40
    // more frames empties the box. 4: done. With `noPolling` set, step 0 waits and step 2 only takes Cross.
    int TitleScene_MemoryCardCheck(GuestPtr<TitleScene> scene, int noPolling)
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
    int Title_MenuInput(GuestPtr<TitleScene> scene)
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
    int Title_DrawChoices(GuestPtr<TitleScene> scene)
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
            Pad_SwallowCross();
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
            notice->secondLine = 0;
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
    PORT_FN(ListMenu_Place);
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
    PORT_FN(TitleScene_MemoryCardCheck);
    PORT_FN(CityCursor_LayoutInfo);
    PORT_FN(Title_DrawChoices);
    PORT_FN(Title_DrawCommon);
    PORT_FN(Title_PressStart);
    PORT_FN(ListMenu_HandleInput);

    // ---- batch 38

    // The frame's corners in GS units (12.4 fixed point with the screen at 0x6C0, 0x790), as mm_Frame_SetCorners takes
    // them: (x0, y0) the top left, (x1, y1) the bottom right (halfwords each); the y values go through -0x8000 and
    // back, as the original does.
    void Frame_GetRect(GuestPtr<Frame> frame, GuestPtr<uint32_t> x0, GuestPtr<uint32_t> y0, GuestPtr<uint32_t> x1, GuestPtr<uint32_t> y1)
    {
        *x0 = frame->sprite.xyzA.xy & 0xFFFFu;
        *y0 = static_cast<uint32_t>(static_cast<int32_t>(frame->sprite.xyzA.xy) >> 16) & 0xFFFFu;
        *x1 = frame->sprite.xyzB.xy & 0xFFFFu;
        *y1 = static_cast<uint32_t>(static_cast<int32_t>(frame->sprite.xyzB.xy) >> 16) & 0xFFFFu;
        *y0 = *y0 - 0x8000u;
        *y1 = *y1 - 0x8000u;
        *y0 = *y0 + 0x8000u;
        *y1 = *y1 + 0x8000u;
    }

    PORT_FN(Frame_GetRect);

    // ---- batch 38

    // ---- the message window: a 0x200-byte text being typed (source), the shown lines (lines, four of 0x24)

    // Starts the next text (in state 3): copied in, typing from its start up to the first delimiter (0x1B5A78);
    // with four lines shown the oldest scrolls away. Returns 1, or 0 in other states.
    int MsgWin_NextLine(GuestPtr<MsgWin> msgwin, GuestPtr<char> text)
    {
        if (msgwin->base.state != 3)
            return 0;
        sdk::strcpy(port::guest(msgwin->source), text.address);
        msgwin->base.state = 6;
        msgwin->readAt = GuestPtr<char>{port::guest(msgwin->source)};
        const int length = sdk::strcspn(msgwin->readAt.address, globals::kMsgWinWordBreaks);
        msgwin->wordEnd = GuestPtr<char>{msgwin->readAt.address + static_cast<uint32_t>(length)};
        if (static_cast<int32_t>(msgwin->writeAt.address - port::guest(msgwin->lines)) / 36 >= 4)
        {
            sdk::memmove(port::guest(msgwin->lines), port::guest(msgwin->lines) + 0x24u, 0x6D);
            TextBox_SetText(msgwin->text, GuestPtr<char>{port::guest(msgwin->lines)}, 1);
            msgwin->writeAt = GuestPtr<char>{msgwin->writeAt.address - 0x24u};
        }
        msgwin->typeDelay = 4;
        return 1;
    }

    // Opens the window for model node `node` with sound `sound`: nothing typed or shown yet, the waiting text
    // (0x1B5A80) in the text box. Returns 1.
    int MsgWin_SetMessage(GuestPtr<MsgWin> msgwin, int node, uint8_t sound)
    {
        const uint32_t w = msgwin.address;
        Widget_Open(GuestPtr<Widget>{w});
        msgwin->node = node;
        msgwin->sound = sound;
        msgwin->bracketed = 0;
        msgwin->typeDelay = 0;
        msgwin->source[0] = static_cast<char>(0);
        msgwin->lines[0] = static_cast<char>(0);
        msgwin->readAt = GuestPtr<char>{port::guest(msgwin->source)};
        msgwin->wordEnd = GuestPtr<char>{msgwin->readAt.address};
        msgwin->writeAt = GuestPtr<char>{port::guest(msgwin->lines)};
        msgwin->exactFit = 0;
        TextBox_SetText(msgwin->text, GuestPtr<char>{globals::kMsgWinEmptyText}, 1);
        return 1;
    }

    // ---- the home view (a TalkView: its building model, city and the home object as its target)

    namespace
    {
        GuestPtr<CityScene> viewCity(GuestPtr<TalkView> view) { return view->city; }
        uint32_t viewObject(GuestPtr<TalkView> view) { return static_cast<uint32_t>(view->targetObject); }
    }

    // The tiles before the home entrance, by the facing: the footprint widened by one more than its size on three
    // sides (the facing side kept), clamped to the map (0..128).
    void MyHome_EntranceRect(GuestPtr<TalkView>, int facing, int x, int z, int size, GuestPtr<TileRect> rect)
    {
        const int e = size + 1;
        switch (facing)
        {
        case 0:
            *rect = TileRect{x - e, z, e + (x + size), e + (z + size)};
            break;
        case 1:
            *rect = TileRect{x - e, z - e, x + size, e + (z + size)};
            break;
        case 2:
            *rect = TileRect{x - e, z - e, e + (x + size), z + size};
            break;
        case 3:
            *rect = TileRect{x, z - e, e + (x + size), e + (z + size)};
            break;
        default:
            break;
        }
        for (int32_t *corner : {&rect->x0, &rect->z0, &rect->x1, &rect->z1})
            *corner = *corner < 0 ? 0 : (*corner < 129 ? *corner : 128);
    }

    // The top objects before the home entrance get or lose flag 0x80.
    void MyHome_MarkEntrance(GuestPtr<TalkView> view, int set)
    {
        struct Locals
        {
            uint8_t gap_00[0x50];
            TileRect entrance;         // +0x50 the tiles before the entrance (x1 and z1 past them)
        };
        static_assert(offsetof(Locals, entrance) == 0x50 && sizeof(Locals) == 0x60);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<CityMap> map = viewCity(view)->map;
        const GuestPtr<MapObject> home = Map_GetRecord(map, static_cast<int>(viewObject(view)));
        const GuestPtr<BuildingType> type{globals::buildingTypeAddress(home->type)};
        const int facing = Map_GetTileFacing(map, home->tile);
        const GuestPtr<TileRect> rect = frame.at(&Locals::entrance);
        MyHome_EntranceRect(view, facing, home->tile & 0x7F, home->tile >> 7, type->size, rect);
        for (int z = rect->z0; z < rect->z1; ++z)
        {
            for (int x = rect->x0; x < rect->x1; ++x)
            {
                const GuestPtr<MapObject> object = Map_GetTileObject(viewCity(view)->map, x, z);
                if (set != 0)
                    object->flags |= 0x80u;
                else
                    object->flags &= 0xFF7Fu;
            }
        }
    }

    // Leaving home: stamina below 399999 is refilled, the entrance marks are cleared, the view closes.
    void MyHome_Close(GuestPtr<TalkView> view)
    {
        const GuestPtr<Stamina> stamina = viewCity(view)->stamina;
        if (stamina->value < 0x61A7F)
            Stamina_Init(stamina);
        if ((Map_GetRecord(viewCity(view)->map, static_cast<int>(viewObject(view)))->flags & 0x20u) != 0)
            MyHome_MarkEntrance(view, 0);
        Widget_Close(GuestPtr<Widget>{view.address});
    }

    // Pass 1, while open (state 3): the light packets and, for a built home (flag 0x20), its model node (the type
    // node + 0x19) placed by the object matrix (w set to 1) and drawn.
    void MyHome_Draw(GuestPtr<TalkView> view, int pass)
    {
        if (pass != 1 || view->base.state != 3)
            return;
        LightSet_SelectProgram(viewCity(view)->lights, 1);
        const GuestPtr<MapObject> home = Map_GetRecord(viewCity(view)->map, static_cast<int>(viewObject(view)));
        if ((home->flags & 0x20u) == 0)
            return;
        const GuestPtr<BuildingType> type{globals::buildingTypeAddress(home->type)};
        const GuestPtr<A3DObject> model = view->buildingModel;
        const int node = type->modelNode + 0x19;
        const GuestPtr<Vec4> matrix = worldMatrix(modelNode(model, node));
        MapObj_GetMatrix(viewCity(view)->map, static_cast<int>(viewObject(view)), matrix);
        port::store<int32_t>(packet::addressOf(matrix[3].w), 1); // the matrix is up to date
        A3DObject_DrawNode(model, node);
    }

    // ---- the name entry (state, slideFrames, restHeight, boardMatrix)

    void NameEntry_Close(GuestPtr<NameEntry> entry)
    {
        if (entry->state == 0)
            return;
        entry->slideFrames = 25;
        entry->state = entry->noSlide != 0 ? 0 : 4;
    }

    // States 3 and 4 slide the panel in (26 frames, then input) and out (to closed); state 1 takes input. Returns
    // the state.
    int NameEntry_Update(GuestPtr<NameEntry> entry)
    {
        const int32_t state = entry->state;
        if (state == 1)
        {
            NameEntry_HandleInput(entry);
            return entry->state;
        }
        if (state != 3 && state != 4)
            return entry->state;
        const ps2f height = ps2f(0x1.333334p-2f) + ps2f(static_cast<float>(entry->restHeight)) -  // 0.3
                            ps2f(0x1.89374cp-7f) * fromInt(entry->slideFrames);    // 0.012 per frame
        entry->boardMatrix[3].y = static_cast<float>(height);
        if (entry->state == 3)
        {
            entry->slideFrames = entry->slideFrames + 1;
            if (entry->slideFrames >= 26)
                entry->state = 1;
        }
        else
        {
            entry->slideFrames = entry->slideFrames - 1;
            if (entry->slideFrames < 0)
                entry->state = 0;
        }
        return entry->state;
    }

    // ---- notices: one line (text) centred by its length for 300 frames, an optional second line

    namespace
    {
        // The x that centres `length` characters (13 units each, in 1/16 pixel from 0x800).
        int noticeX(int length, bool unsignedShift)
        {
            const int32_t width = length * 26;
            const int32_t half = unsignedShift ? static_cast<int32_t>(static_cast<uint32_t>(width) >> 2) : width / 4;
            return (0x800 - half) << 4;
        }
    }

    void Notice_Clear(GuestPtr<Notice> notice)
    {
        notice->id = -1;
        notice->secondLine = 0;
    }

    // Notice `id` from the notice texts (48 bytes each from 0x1B81E0).
    void Notice_Show(GuestPtr<Notice> notice, int id)
    {
        notice->id = id;
        TextBox_SetText(notice->text, GuestPtr<char>{globals::kNoticeTexts + 48u * static_cast<uint32_t>(notice->id)}, 0);
        notice->timer = 300;
        const int length = sdk::strlen(globals::kNoticeTexts + 48u * static_cast<uint32_t>(notice->id));
        TextBox_SetPos(notice->text, noticeX(length, false), 0x84E0, -1);
    }

    void Notice_ShowText(GuestPtr<Notice> notice, GuestPtr<char> text)
    {
        notice->id = -2;
        TextBox_SetText(notice->text, text, 0);
        notice->timer = 300;
        TextBox_SetPos(notice->text, noticeX(sdk::strlen(text.address), false), 0x84E0, -1);
    }

    // A higher line and a second text below it.
    void Notice_ShowCentered(GuestPtr<Notice> notice, GuestPtr<char> text, int second)
    {
        notice->id = -2;
        TextBox_SetText(notice->text, text, 0);
        notice->timer = 300;
        TextBox_SetPos(notice->text, noticeX(sdk::strlen(text.address), true), 0x8440, -1);
        TextBox_SetText(notice->text2, GuestPtr<char>{static_cast<uint32_t>(second)}, 0);
        notice->secondLine = 1;
    }

    PORT_FN(MsgWin_NextLine);
    PORT_FN(MsgWin_SetMessage);
    PORT_FN(MyHome_EntranceRect);
    PORT_FN(MyHome_MarkEntrance);
    PORT_FN(MyHome_Close);
    PORT_FN(MyHome_Draw);
    PORT_FN(NameEntry_Close);
    PORT_FN(NameEntry_Update);
    PORT_FN(Notice_Clear);
    PORT_FN(Notice_Show);
    PORT_FN(Notice_ShowText);
    PORT_FN(Notice_ShowCentered);

    // ---- widget base methods, wait and loading texts (batch 38)

    void Widget_Active(GuestPtr<Widget>) {}
    void Widget_Draw(GuestPtr<Widget>, int) {}
    int Widget_IsActive(GuestPtr<Widget> widget) { return widget->state == 3 ? 1 : 0; }
    int Widget_IsIdle(GuestPtr<Widget> widget) { return widget->state == 0 ? 1 : 0; }

    // The opening and closing states run on the timer: dt is added, and at 1 the timer starts over and the state
    // moves on (past 5, closed, back to 0, idle). Returns 1 when the state moved.
    int Widget_Step(GuestPtr<Widget> widget, ps2f dt)
    {
        const ps2f timer = widget->timer + dt;
        widget->timer = timer;
        if (timer < ps2f(1.0f))
            return 0;
        widget->timer = 0.0f;
        widget->state = widget->state + 1;
        if (widget->state >= 6)
            widget->state = 0;
        return 1;
    }

    // Both forward to the text box; the result is whatever v0 the text box call left.
    int WaitText_Append(GuestPtr<char> text)
    {
        TextBox_AppendText(GuestPtr<TextBox>{globals::waitTextBox.get()}, text, 0);
        return port::guestV0();
    }

    int LoadingScreen_SetText(GuestPtr<LoadingScreen> loading, GuestPtr<char> text)
    {
        TextBox_SetText(loading->text, text, 0);
        return port::guestV0();
    }

    PORT_FN(Widget_Active);
    PORT_FN(Widget_Draw);
    PORT_FN(Widget_IsActive);
    PORT_FN(Widget_IsIdle);
    PORT_FN(Widget_Step);
    PORT_FN(WaitText_Append);
    PORT_FN(LoadingScreen_SetText);

    // ---- batch 38

    // The intro's character: while none is shown (previewModel), one of the loaded character parts (the 48 at 0x1F3B90,
    // up to the last one loaded) at random, in phase 0 (previewState), turned -1.4 (previewTurn), its time (previewTime)
    // cleared, its animation's length in previewEnd and the part's turn step and text from 0x1B1870 (a float) and 0x1B1930. 0 when no
    // part is loaded.
    int Title_RandomCharParts(GuestPtr<TitleScene> scene)
    {
        if (scene->previewModel.address != 0)
            return 1;
        int last = -1;
        for (int i = 0; i < 0x30; ++i)
            if (port::load<uint32_t>(globals::kCharParts + 4u * static_cast<uint32_t>(i)) != 0)
                last = i;
        if (last < 0)
            return 0;
        const uint32_t pick = static_cast<uint32_t>(Random_TimerMix()) % static_cast<uint32_t>(last + 1);
        scene->previewModel = GuestPtr<A3DObject>{port::load<uint32_t>(globals::kCharParts + 4u * pick)};
        scene->previewState = 0;
        scene->previewTurn = -1.4f;
        scene->previewTime = 0.0f;
        const ps2f meshValue = A3D_GetAnimLength(scene->previewModel, scene->previewState);
        scene->previewEnd = static_cast<float>(meshValue);
        scene->previewTurnStep = port::load<float>(globals::kPreviewTurnSteps + 4u * pick);
        scene->previewText = GuestPtr<char>{port::load<uint32_t>(globals::kPreviewTexts + 4u * pick)};
        return 1;
    }

    // The BGM page of the sound test (`test`, its text box hintText): the controls, then the volume (5
    // wide), the tempo (signed, 3 wide) and the song number (song, 2 wide).
    void SoundTest_BgmText(GuestPtr<TitleScene> scene)
    {
        const auto box = [scene] { return scene->hintText; };
        port::GuestFrame<0x30> frame;
        const GuestPtr<char> number{frame.ptr().address + 0x20u};
        TextBox_SetText(box(), GuestPtr<char>{globals::kSoundTestBgmText}, 0); // "*** BGM Test ***", the controls, "Volume(+/-) "
        Text_FormatPadded(number, scene->volume, 5);
        TextBox_AppendText(box(), number, 0);
        TextBox_AppendText(box(), GuestPtr<char>{globals::kSoundTestTempo}, 0); // "Tempo(+/-)"
        const int tempo = scene->tempo;
        TextBox_AppendText(box(), GuestPtr<char>{tempo >= 0 ? globals::kSoundTestPlus : globals::kSoundTestMinus}, 0); // " " or "-"
        Text_FormatPadded(number, tempo >= 0 ? tempo : -tempo, 3);
        TextBox_AppendText(box(), number, 0);
        TextBox_AppendText(box(), GuestPtr<char>{globals::kSoundTestBgmNumber}, 0); // "BGM No:"
        Text_FormatPadded(number, scene->song, 2);
        TextBox_AppendText(box(), number, 0);
    }

    // The new-game screen's character preview (picked by mm_Title_RandomCharParts), drawn every frame at (turn, 0.94, 0.36)
    // in three phases (previewState). 0: it slides in from the left, turned a quarter away and swinging round as turn passes
    // -0.1, until turn reaches 0, when it waits (8) with the part's own loading text. 8: its animation plays once
    // through (time +2 a frame), then it turns out (1) under the empty text: the quarter turn comes back while turn
    // (stepping twice as fast) passes 0.2, and at 1.4 the model is dropped. Phases 0 and 1 keep the animation looping
    // from a fifth of its length. The turn is a Y rotation of -90 degrees times a ramp, multiplied into an identity matrix on
    // VU0 (vf1 to vf12); the original returns the last call's v0, which its caller overwrites.
    void Title_NewGameIntro(GuestPtr<TitleScene> scene)
    {
        using namespace vu0;
        if (Title_RandomCharParts(scene) == 0)
            return;
        port::GuestFrame<0xB0> frame;
        const uint32_t sp = frame.ptr().address;
        const auto F = [](uint32_t bits) { return ps2f(std::bit_cast<float>(bits)); };
        const GuestPtr<Vec4> colour{sp + 0x20u}, matrix{sp + 0x30u}, turnMatrix{sp + 0x70u};
        const auto rowOf = [](GuestPtr<Vec4> base, int i) { return base + i; };
        *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
        Mat4_Copy(matrix, GuestPtr<Vec4>{globals::kIdentityMatrix});
        const int state = scene->previewState;
        if (state != 0 && state != 1 && state != 8)
            return;
        if (state != 8)
        {
            ps2f angle = F(0xBFC90FDBu);
            if (state == 1)
            {
                if (scene->previewTurn < F(0x3E4CCCCDu))
                    angle = angle * (F(0x40A00000u) * ps2f(scene->previewTurn));
            }
            else if (!(scene->previewTurn < F(0xBDCCCCCDu)))
                angle = angle * (F(0xC1200000u) * ps2f(scene->previewTurn));
            sdk::sceVu0RotMatrixY(turnMatrix.address, globals::kIdentityMatrix, angle);
            for (int i = 0; i < 4; ++i)
                vf(1 + i) = load(rowOf(matrix, i));
            for (int i = 0; i < 4; ++i)
                vf(5 + i) = load(rowOf(turnMatrix, i));
            for (int i = 0; i < 4; ++i)
            {
                set(acc(), mul(vf(5), lane(vf(1 + i), 0)), XYZW);
                set(acc(), add(acc(), mul(vf(6), lane(vf(1 + i), 1))), XYZW);
                set(acc(), add(acc(), mul(vf(7), lane(vf(1 + i), 2))), XYZW);
                set(vf(9 + i), add(acc(), mul(vf(8), lane(vf(1 + i), 3))), XYZW);
            }
            for (int i = 0; i < 4; ++i)
                store(rowOf(matrix, i), vf(9 + i));
        }
        const GuestPtr<Vec4> position = rowOf(matrix, 3);
        position->x = scene->previewTurn;
        position->y = F(0x3F70A3D7u);
        position->z = F(0x3EB851ECu);
        position->w = F(0x3F800000u);
        const GuestPtr<A3DObject> model = scene->previewModel;
        for (int i = 0; i < 4; ++i)
            model->matrix[i] = *rowOf(matrix, i);
        A3D_AnimateIfRoom(model, scene->previewState, scene->previewTime);
        A3DObject_UpdateMatrices(model);
        Instance_DrawModelEx(model, colour, 0, 0, 0, 0, 0, 4);
        scene->previewTime = ps2f(scene->previewTime) + F(0x40000000u);
        if (state == 8)
        {
            if (ps2f(scene->previewTime) <= ps2f(scene->previewEnd))
                return;
            scene->previewState = 1;
            scene->previewTime = ps2f(scene->previewEnd) / F(0x40A00000u);
            scene->previewEnd = A3D_GetAnimLength(model, scene->previewState);
            LoadingScreen_SetText(scene->loading, GuestPtr<char>{globals::kEmptyText});
            return;
        }
        if (!(ps2f(scene->previewTime) <= ps2f(scene->previewEnd)))
            scene->previewTime = ps2f(scene->previewEnd) / F(0x40A00000u);
        if (state == 1)
        {
            scene->previewTurn = ps2f(scene->previewTurn) + F(0x40000000u) * ps2f(scene->previewTurnStep);
            if (!(scene->previewTurn < F(0x3FB33333u)))
                scene->previewModel = GuestPtr<A3DObject>{0};
            return;
        }
        scene->previewTurn = ps2f(scene->previewTurn) + ps2f(scene->previewTurnStep);
        if (scene->previewTurn < F(0u))
            return;
        scene->previewState = 8;
        scene->previewTime = F(0u);
        scene->previewEnd = A3D_GetAnimLength(model, scene->previewState);
        LoadingScreen_SetText(scene->loading, GuestPtr<char>{scene->previewText.address});
    }

    PORT_FN(Title_NewGameIntro);
    PORT_FN(Title_RandomCharParts);
    PORT_FN(SoundTest_BgmText);

    // ---- the city cursor (batch 39)

    namespace
    {
        // A box on the screen in GS window units (1/16 pixel).
        struct ScreenBox
        {
            int32_t x0, y0, x1, y1;
        };

        // A picked building's opening box: about the first corner of the cursor view's frame (mm_Frame_GetRect), 100
        // units across and 25 down for each step of the open timer run so far (30 less the timer, 15 at most) to every
        // side; a side with an end outside 0..0xFFFF is cleared. The view's sprite takes the corners (xyzA left and
        // top, xyzB right and bottom).
        void openPickBox(GuestPtr<CityCursor> cursor, GuestPtr<CityCursorView> view, GuestPtr<ScreenBox> box)
        {
            Frame_GetRect(view->frame, box.field(&ScreenBox::x0).as<uint32_t>(), box.field(&ScreenBox::y0).as<uint32_t>(),
                          box.field(&ScreenBox::x1).as<uint32_t>(), box.field(&ScreenBox::y1).as<uint32_t>());
            int32_t steps = 30 - cursor->openTimer;
            if (steps >= 16)
                steps = 15;
            const int32_t down = 25 * steps, across = 4 * down;
            const int32_t x = box->x0, y = box->y0;
            *box = ScreenBox{x - across, y - down, x + across, y + down};
            const auto outside = [](int32_t a, int32_t b) { return a < 0 || b < 0 || a >= 0x10000 || b >= 0x10000; };
            if (outside(box->x0, box->x1))
            {
                box->x1 = 0;
                box->x0 = 0;
            }
            if (outside(box->y0, box->y1))
            {
                box->y1 = 0;
                box->y0 = 0;
            }
            view->sprite.xyzA.xy = static_cast<uint32_t>(box->x0) | (static_cast<uint32_t>(box->y0) << 16);
            view->sprite.xyzB.xy = static_cast<uint32_t>(box->x1) | (static_cast<uint32_t>(box->y1) << 16);
        }
    }

    // Picking a building: one marked by an event (flag 0x200) queues event 0x106; otherwise the building menu opens
    // (mode 0xA) for it, the cursor in mode 4 with its open timer at 30, and this frame's view gets its opening box
    // (openPickBox).
    void CityCursor_SelectTile(GuestPtr<CityCursor> cursor, int object, GuestPtr<CityScene> city)
    {
        if ((Map_GetRecord(city->map, object)->flags & 0x200u) != 0)
        {
            Event_Trigger(city->events, 0x106, city, GuestPtr<MapPerson>{0});
            return;
        }
        cursor->picked = object;
        CityScene_PushMode(city, 0xA);
        cursor->mode = 4;
        cursor->openTimer = 30;
        struct Locals
        {
            uint8_t gap_00[0x4C];
            ScreenBox box;             // +0x4C
            uint8_t gap_5C[4];
        };
        static_assert(offsetof(Locals, box) == 0x4C && sizeof(Locals) == 0x60);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<CityCursorView> view = cursor.field(&CityCursor::views) + static_cast<int32_t>(globals::frameParity.get() & 1u);
        openPickBox(cursor, view, frame.at(&Locals::box));
    }

    PORT_FN(CityCursor_SelectTile);

    // ---- the sound test's effect page (batch 39)

    // The effect page's text: the controls, then the pan (signed: "L" or "R" and 2 digits, or "Center"), the
    // distance (2 wide), the note (3 wide), the pitch (signed, 5 wide) and the effect number
    // (effect, 3 wide).
    void SoundTest_SeText(GuestPtr<TitleScene> scene)
    {
        const auto box = [scene] { return scene->hintText; };
        const auto append = [&](uint32_t text) { TextBox_AppendText(box(), GuestPtr<char>{text}, 0); };
        port::GuestFrame<0x20> frame;
        const GuestPtr<char> number{frame.ptr().address + 0x10u};
        TextBox_SetText(box(), GuestPtr<char>{globals::kSoundTestSeText}, 0); // "*** SE Test ***" and the controls
        const int pan = scene->pan;
        if (pan > 0)
        {
            Text_FormatPadded(number, pan, 2);
            append(globals::kSoundTestRight); // "R"
            append(number.address);
        }
        else if (pan < 0)
        {
            Text_FormatPadded(number, -pan, 2);
            append(globals::kSoundTestLeft); // "L"
            append(number.address);
        }
        else
            append(globals::kSoundTestCentre); // "Center"
        append(globals::kSoundTestDistance); // "R1/R2 Distance(+/-)"
        Text_FormatPadded(number, scene->distance, 2);
        append(number.address);
        append(globals::kSoundTestNote); // "Note(+/-)"
        Text_FormatPadded(number, scene->note, 3);
        append(number.address);
        append(globals::kSoundTestPitch); // "Pitch(+/-)"
        const int pitch = scene->pitch;
        append(pitch < 0 ? globals::kSoundTestMinus : globals::kSoundTestPlus); // "-" or " "
        Text_FormatPadded(number, pitch < 0 ? -pitch : pitch, 5);
        append(number.address);
        append(globals::kSoundTestSeNumber); // "SE No:"
        Text_FormatPadded(number, scene->effect, 3);
        append(number.address);
    }

    // The effect page's input (pads repeated, pressed, held): up and down pick the effect (0 to 0x47, wrapping),
    // the note moves within 0..0x7F (reset to 0x3C), the pitch by 10 within +-0x4000 (reset to 0), the pan within
    // -10..10 and the distance within 0..16 (both reset with their two keys held). Pressing 0x40 plays the effect
    // (with the key held instead for effects whose flag 0x10 is clear at 0x1B2EE0), 0x10 stops it. Returns 1 when
    // the page changed.
    int SoundTest_SePage(GuestPtr<TitleScene> scene, uint32_t repeated, uint32_t pressed, uint32_t held)
    {
        int changed = 0;
        if (repeated == 0x1000)
        {
            scene->effect = static_cast<uint8_t>(scene->effect + 1);
            changed = 1;
            if (scene->effect > 0x47)
                scene->effect = static_cast<uint8_t>(0);
        }
        if (repeated == 0x4000)
        {
            scene->effect = static_cast<uint8_t>(scene->effect == 0 ? 0x47 : scene->effect - 1);
            changed = 1;
        }
        if ((repeated & 0x200000u) != 0)
        {
            changed = 1;
            if (scene->note < 0x7F)
                scene->note = static_cast<uint8_t>(scene->note + 1);
        }
        else if ((repeated & 0x800000u) != 0)
        {
            changed = 1;
            if (scene->note != 0)
                scene->note = static_cast<uint8_t>(scene->note - 1);
        }
        if ((pressed & 0x400u) != 0)
        {
            scene->note = static_cast<uint8_t>(0x3C);
            changed = 1;
        }
        const int pitch = scene->pitch;
        if ((repeated & 0x2000u) != 0)
        {
            changed = 1;
            if (pitch + 10 < 0x4000)
                scene->pitch = static_cast<int16_t>(pitch + 10);
        }
        else if ((repeated & 0x8000u) != 0)
        {
            changed = 1;
            if (-0x4000 < pitch + 10)
                scene->pitch = static_cast<int16_t>(pitch - 10);
        }
        if ((pressed & 0x200u) != 0)
        {
            scene->pitch = 0;
            changed = 1;
        }
        const int pan = scene->pan;
        if ((held & 4u) != 0 && (held & 1u) != 0)
        {
            scene->pan = static_cast<int8_t>(0);
            changed = 1;
        }
        else if ((repeated & 4u) != 0)
        {
            changed = 1;
            if (-10 < pan)
                scene->pan = static_cast<int8_t>(pan - 1);
        }
        else if ((repeated & 1u) != 0)
        {
            changed = 1;
            if (pan < 10)
                scene->pan = static_cast<int8_t>(pan + 1);
        }
        if ((held & 8u) != 0 && (held & 2u) != 0)
        {
            scene->distance = static_cast<uint8_t>(0);
            changed = 1;
        }
        else if ((repeated & 8u) != 0)
        {
            changed = 1;
            if (scene->distance != 0)
                scene->distance = static_cast<uint8_t>(scene->distance - 1);
        }
        else if ((repeated & 2u) != 0)
        {
            changed = 1;
            if (scene->distance < 0x10)
                scene->distance = static_cast<uint8_t>(scene->distance + 1);
        }
        const ps2f panScale = fromInt(scene->pan) / ps2f(10.0f);
        const ps2f distance = fromInt(scene->distance);
        const uint8_t effect = scene->effect;
        const bool onPress = (port::load<uint8_t>(globals::kSoundTable + 0x10u * effect) & 0x10u) != 0;
        if (onPress ? pressed == 0x40 : (held & 0x40u) != 0)
            Sound_PlaySe(effect, scene->pitch, scene->note, panScale, distance);
        if (pressed == 0x10)
            Se_StopOwner(static_cast<char>(scene->effect));
        return changed;
    }

    PORT_FN(SoundTest_SeText);
    PORT_FN(SoundTest_SePage);

    // ---- the pause menu's memory card states (batch 39)

    // Shows a memory card message: the card manager and its slots are reset, the wait counter cleared, the main menu
    // emptied and the message box placed (and for 6 to 8, "Memory card ... in slot 1/2 does not have enough space to
    // save" moved left). Messages 0 and 2 to 5 pick their text (5 names a card by cardPort, then "."), 1 and anything
    // above 8 have none. The state becomes 10.
    void PauseMenu_ShowCardMessage(GuestPtr<PauseMenu> pause, int message)
    {
        McManager_ResetState(pause->mc);
        McManager_ClearSlots(pause->mc);
        pause->waitFrames = 0;
        ListMenu_ClearItem(pause->mainMenu, -1);
        TextBox_SetPos(pause->message, 0x74C0, 0x7E00, -0x100000);
        TextBox_SetCharSize(pause->message, 0, 0xD0, 0, 0xE0);
        pause->cardMessage = message;
        const auto set = [&](uint32_t text) { TextBox_SetText(pause->message, GuestPtr<char>{text}, 0); };
        const auto append = [&](uint32_t text) { TextBox_AppendText(pause->message, GuestPtr<char>{text}, 0); };
        switch (message)
        {
        case 0:
            set(globals::kPauseTextNoCard);
            break;
        case 2:
            set(globals::kPauseTextFormatted);
            break;
        case 3:
            set(globals::kPauseTextSaveFailed); // "Failed to save."
            break;
        case 4:
            set(globals::kPauseTextFormatFailed); // "Failed to format."
            break;
        case 5:
            set(globals::kPauseTextNoCardAgain);
            append(pause->cardPort == 0 ? globals::kPauseTextDigit1 : globals::kPauseTextDigit2); // "1" or "2"
            append(globals::kPauseTextPeriod);                                                    // "."
            break;
        case 6:
        case 7:
        case 8:
            TextBox_SetPos(pause->message, 0x7240, 0x7E00, -0x100000);
            set(globals::kPauseTextCardInSlot);
            append(message == 6 ? globals::kPauseTextSlot1 : message == 7 ? globals::kPauseTextSlot2 : globals::kPauseTextPlural);
            append(globals::kPauseTextNoSpace); // "does not have enough space to save"
            break;
        default:
            break;
        }
        pause->state = 10;
    }

    // Waiting out a card message: after a pad press or 210 frames, or when the card status (mm_McManager_GetCardStatus)
    // suits the message, the main menu comes back (Resume, Save, Quit, the cursor on Save) and the state becomes 2.
    // Message 6 waits for card 0 to leave statuses 4 and 1, 7 the same for card 1, 0 for neither card to be at 4
    // and not both at 2, 8 for neither at 4 and not both at 1.
    void PauseMenu_StateMain(GuestPtr<PauseMenu> pause)
    {
        pause->waitFrames = pause->waitFrames + 1;
        bool done = Pad_Pressed(0) != 0 || pause->waitFrames > 0xD2;
        if (!done)
        {
            McManagerUpdate(pause->mc);
            const uint8_t card0 = McManager_GetCardStatus(pause->mc, 0), card1 = McManager_GetCardStatus(pause->mc, 1);
            switch (pause->cardMessage)
            {
            case 8:
                done = card0 != 4 && card1 != 4 && (card0 != 1 || card1 != 1);
                break;
            case 0:
                done = card0 != 4 && card1 != 4 && (card0 != 2 || card1 != 2);
                break;
            case 7:
                done = card1 != 4 && card1 != 1;
                break;
            case 6:
                done = card0 != 4 && card0 != 1;
                break;
            default:
                break;
            }
        }
        if (!done)
            return;
        TextBox_SetText(pause->message, GuestPtr<char>{globals::kPauseEmptyText}, 0);
        ListMenu_ClearItem(pause->mainMenu, -1);
        ListMenu_Place(pause->mainMenu, 0x104, 0x3C, 0xFF0000);
        ListMenu_SetItemText(pause->mainMenu, GuestPtr<char>{globals::kPauseTextResume}, -1); // "Resume"
        ListMenu_SetItemText(pause->mainMenu, GuestPtr<char>{globals::kPauseTextSave}, -1);   // "Save"
        ListMenu_SetItemText(pause->mainMenu, GuestPtr<char>{globals::kPauseTextQuit}, -1);   // "Quit"
        ListMenu_ScrollTo(pause->mainMenu, 1);
        McManager_ResetState(pause->mc);
        McManager_ClearSlots(pause->mc);
        pause->state = 2;
    }

    PORT_FN(PauseMenu_ShowCardMessage);
    PORT_FN(PauseMenu_StateMain);

    // ---- the sound test (batch 39)

    namespace
    {
        // The sound test screen's text for its current page (0 effects, 1 music); beyond that, the waiting text.
        void soundTestText(GuestPtr<TitleScene> scene)
        {
            const auto box = [scene] { return scene->hintText; };
            const uint32_t page = static_cast<uint32_t>(scene->testPage);
            if (page < 100)
            {
                if (page == 1)
                    SoundTest_BgmText(scene);
                else if (page == 0)
                    SoundTest_SeText(scene);
                return;
            }
            TextBox_SetText(box(), GuestPtr<char>{globals::kSoundTestHeading}, 0); // "*** Sound Test ***"
            TextBox_AppendText(box(), GuestPtr<char>{globals::kSoundTestWait}, 0); // "Just a moment..."
        }
    }

    // The music page's input: up and down pick the song (0 to 0x13, wrapping), left and right the tempo (within
    // +-0xFF, reset on 0x200), the two keys 4 and 1 the volume (by 100 within 0..0x3FFF, both held 0x27FF). The
    // song plays on 0x40 (the same song again just plays; another first fades out and loads its bank once it is
    // out) and stops on 0x10. While a bank loads (bankLoading) the page waits for the driver. Returns 1 when the page
    // changed (the text is then rebuilt and drawn here, so 0 is returned when it drew).
    int SoundTest_BgmPage(GuestPtr<TitleScene> scene, uint32_t repeated, uint32_t pressed, uint32_t held)
    {
        int changed = 0;
        const auto setTempo = [&](int tempo) {
            scene->tempo = static_cast<int16_t>(tempo);
            SoundCmd_MidiSetTempo(0xF, static_cast<uint16_t>(tempo), 0);
            changed = 1;
        };
        const auto setVolume = [&](int volume) {
            scene->volume = volume;
            SoundCmd_MidiSetVolume(0xF, volume, 0);
            changed = 1;
        };
        if (repeated == 0x1000)
        {
            scene->song = static_cast<uint8_t>(scene->song + 1);
            changed = 1;
            if (scene->song > 0x13)
                scene->song = 0;
        }
        if (repeated == 0x4000)
        {
            scene->song = scene->song == 0 ? 0x13 : static_cast<uint8_t>(scene->song - 1);
            changed = 1;
        }
        const int tempo = scene->tempo;
        if ((repeated & 0x2000u) != 0)
        {
            if (tempo < 0xFF)
                setTempo(tempo + 1);
        }
        else if ((repeated & 0x8000u) != 0 && -0xFF < tempo)
            setTempo(tempo - 1);
        if ((pressed & 0x200u) != 0)
            setTempo(0);
        const int volume = scene->volume;
        if ((held & 4u) != 0 && (held & 1u) != 0)
            setVolume(0x27FF);
        else if ((held & 4u) != 0)
            setVolume(volume + 100 > 0x3FFF ? 0x3FFF : volume + 100);
        else if ((held & 1u) != 0)
            setVolume(volume - 100 < 0 ? 0 : volume - 100);
        const auto play = [&] {
            if (pressed == 0x40 && Sound_IsBusy() == 0)
            {
                if (static_cast<uint32_t>(scene->playingSong) == scene->song)
                {
                    SoundCmd_MidiSetVolume(0xF, scene->volume, 0);
                    SoundCmd_MidiSetTempo(0xF, static_cast<uint16_t>(scene->tempo), 0);
                    SoundCmd_MidiPlay(0xF, 7, 0, 0, 0);
                }
                else
                {
                    SoundCmd_MidiFade(0xF, 0, 100, 0);
                    scene->playingSong = static_cast<int32_t>(scene->song);
                    scene->bankLoading = 1;
                }
            }
            if (pressed == 0x10 && Sound_IsBusy() == 0)
                SoundCmd_MidiStop(0xF, 0);
        };
        if (scene->bankLoading == 0)
        {
            play();
            return changed;
        }
        if ((port::load<uint32_t>(static_cast<uint32_t>(Sound_GetStatus()) + 4u) & 0x8000u) == 0)
        {
            scene->bankLoading = 0;
            SoundCmd_MidiClose(7, 0);
            const int offset = 0x30 * static_cast<uint8_t>(scene->playingSong);
            SoundCmd_MidiOpen(7, static_cast<int>(globals::kBgmBanks) + offset,
                              static_cast<int>(globals::kBgmBanks + 0x10u) + offset,
                              static_cast<int>(globals::kBgmBanks + 0x20u) + offset, 0);
            SoundCmd_MidiSetVolume(0xF, scene->volume, 0);
            SoundCmd_MidiSetTempo(0xF, static_cast<uint16_t>(scene->tempo), 0);
            SoundCmd_MidiPlay(0xF, 7, 0, 0, 0);
            play();
            return changed;
        }
        if (changed != 0)
            soundTestText(scene);
        TextBox_Draw(scene->hintText);
        return 0;
    }

    // The sound test screen, every frame: the backdrop and model, then the page (the pad is read here). 0x800
    // starts leaving (page 200: the music and sequences stop and the menu banks load, 201 waits for them to be
    // out and sets the play-land banks up again at 0x65, 0x66 for the screen back); 0x100 swaps between the
    // effects and music pages unless a bank is loading; 100 resets the controls. Returns 1 when leaving is done (page
    // 300).
    int SoundTest_Update(GuestPtr<TitleScene> scene)
    {
        if (globals::soundTestStarted.get() == 0)
        {
            globals::soundTestStatic.set(0);
            globals::soundTestStarted.set(1);
        }
        int changed = 0;
        TexSprite_Draw(scene->titleSprite);
        A3DObject_Draw(scene->menuModel);
        const uint32_t pressed = Pad_Pressed(0);
        const uint32_t repeated = static_cast<uint32_t>(Pad_GetRepeat());
        const uint32_t held = Pad_Held(0);
        const auto box = [scene] { return scene->hintText; };
        const auto page = [scene] { return static_cast<uint32_t>(scene->testPage); };
        const auto setPage = [scene](uint32_t value) { scene->testPage = static_cast<int32_t>(value); };
        if (pressed == 0x800 && page() < 100 && scene->bankLoading == 0)
            setPage(200);
        switch (page())
        {
        case 0xC9:
            if (Sound_IsBusy() == 0)
                setPage(300);
            break;
        case 200:
            TextBox_SetText(box(), GuestPtr<char>{globals::kSoundTestHeading}, 0);
            TextBox_AppendText(box(), GuestPtr<char>{globals::kSoundTestWait}, 0);
            SoundCmd_MidiStop(0xF, 0);
            SoundCmd_MidiClose(7, 0);
            Sound_StopAllSequences();
            SoundCmd_WaveCloseBankSet();
            Sound_ResetVoices(1);
            SoundCmd_MidiOpen(7, static_cast<int>(globals::kTitleBankSq), static_cast<int>(globals::kTitleBankHd),
                              static_cast<int>(globals::kTitleBankBd), 0);
            setPage(page() + 1);
            break;
        case 0x66:
            if (Sound_IsBusy() == 0)
            {
                setPage(0);
                soundTestText(scene);
            }
            break;
        case 0x65:
            if ((port::load<uint32_t>(static_cast<uint32_t>(Sound_GetStatus()) + 4u) & 0x8000u) == 0)
            {
                SoundCmd_MidiClose(7, 0);
                SoundCmd_WaveCloseBankSet();
                Sound_ResetVoices(0);
                Sound_LoadPlaylandBanks();
                setPage(page() + 1);
            }
            break;
        case 100:
            scene->bankLoading = 0;
            soundTestText(scene);
            SoundCmd_MidiFade(0xF, 0, 0x10, 0);
            scene->effect = 0;
            scene->song = 0;
            scene->pitch = 0;
            scene->tempo = 0;
            scene->volume = 0x27FF;
            scene->pan = static_cast<int8_t>(0);
            scene->distance = 0;
            scene->playingSong = static_cast<int32_t>(0xFFFFFFFFu);
            setPage(page() + 1);
            break;
        default:
            break;
        }
        if (page() > 99)
        {
            TextBox_Draw(box());
            return page() == 300 ? 1 : 0;
        }
        bool swap = false;
        if (pressed == 0x100 && scene->bankLoading == 0)
        {
            swap = true;
            changed = 1;
        }
        if (page() == 1)
        {
            if (swap)
                setPage(0);
            else
                changed = SoundTest_BgmPage(scene, repeated, pressed, held);
        }
        else if (page() == 0)
        {
            if (swap)
                setPage(1);
            else
                changed = SoundTest_SePage(scene, repeated, pressed, held);
        }
        if (changed != 0)
            soundTestText(scene);
        TextBox_Draw(box());
        return 0;
    }

    PORT_FN(SoundTest_BgmPage);
    PORT_FN(SoundTest_Update);

    // ---- the name entry screen (batch 39)

    // Draws the name entry (state; 0 draws nothing, 1 is typing, else only the board model): the typed name
    // padded to its length (maxLength) with filler glyphs (0x1B8038), the character under the cursor (length)
    // blinking between a space and '*' at the 16th frame of its 32-frame cycle (blink); every ten frames the scrolling
    // line (position hintScroll, text hint, shown ten characters wide) moves on a character; then the text boxes, the board
    // model's meshes (the cursor too while the cycle is before frame 16) and the name box.
    void NameEntry_Draw(GuestPtr<NameEntry> entry)
    {
        if (entry->state == 0)
            return;
        port::GuestFrame<0x150> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<char> name{sp + 0x30u}, scroll{sp + 0x50u};
        DmaList_QueueClearZ();
        const auto model = [entry] { return entry->model; };
        const auto mesh = [&](GuestPtr<Vec4> matrix, int index) {
            Instance_DrawMesh(model(), matrix, GuestPtr<Vec4>{globals::kDefaultColour}, index, 0);
        };
        if (entry->state == 1)
        {
            if ((static_cast<uint32_t>(entry->blink) & 0xFu) == 0)
            {
                sdk::strcpy(name.address, port::guest(entry->name));
                for (int length = sdk::strlen(port::guest(entry->name)); length < static_cast<int>(entry->maxLength); ++length)
                    sdk::strcat(name.address, globals::kNameEntryMark);
                if (entry->blink == 0x10)
                {
                    const uint32_t cursor = name.address + static_cast<uint32_t>(entry->length);
                    if (port::load<int8_t>(cursor) != 0)
                        port::store<int8_t>(cursor, port::load<int8_t>(cursor) == ' ' ? '*' : ' ');
                }
                TextBox_SetText(entry->nameText, name, 1);
            }
            if (entry->hintScroll >= 0)
            {
                entry->hintStep = static_cast<uint8_t>(entry->hintStep + 1);
                if (entry->hintStep > 9)
                {
                    entry->hintStep = 0;
                    sdk::strcpy(scroll.address, port::guest(entry->hint) + static_cast<uint32_t>(entry->hintScroll));
                    sdk::strcat(scroll.address, port::guest(entry->hint));
                    port::store<uint8_t>(scroll.address + 10u, 0);
                    TextBox_SetText(entry->hintText, scroll, 1);
                    entry->hintScroll = entry->hintScroll + 1;
                    if (static_cast<uint32_t>(sdk::strlen(port::guest(entry->hint))) <= static_cast<uint32_t>(entry->hintScroll))
                        entry->hintScroll = 0;
                }
            }
            TextBox_Draw(entry->hintText);
            TextBox_Draw(entry->lineText);
            mesh(entry->boardMatrix, 0);
            TextBox_Draw(entry->keysText);
            if (entry->blink < 0x10)
                mesh(entry->markerMatrix, 1);
            if (entry->moreAbove != 0)
                mesh(entry->boardMatrix, 3);
            if (entry->moreBelow != 0)
                mesh(entry->boardMatrix, 2);
            mesh(entry->boardMatrix, 4);
            TextBox_Draw(entry->nameText);
        }
        else
            mesh(entry->boardMatrix, 0);
        entry->blink = entry->blink + 1;
        if (entry->blink >= 0x20)
            entry->blink = 0;
    }

    PORT_FN(NameEntry_Draw);

    // ---- typing a message (batch 39)

    // Types the next character of the message into the lines (36 columns, 4 lines): every `typeDelay` frames (a counter
    // at 0x1BDA08) a character moves from the text (read at readAt) to the line buffer (lines, written at writeAt). A
    // message outside brackets (the bracket glyph pairs at 0x1B5A70 and 0x1B5A72) plays its sound for the character.
    // At the end of a word (up to the next delimiter, 0x1B5A78) that does not fit the line, the rest of the line is
    // padded with spaces first (exactFit marks a word that exactly fills it); a line break pads the same. At the end of
    // the text the state becomes 3 and the last line is padded. With four lines full, the oldest scrolls away.
    void MsgWin_TypeNext(GuestPtr<MsgWin> msgwin)
    {
        uint32_t cursor = msgwin->readAt.address;
        const auto typeAt = [](uint32_t at) { return port::load<int8_t>(at); };
        if (typeAt(cursor) != 0)
        {
            if (msgwin->bracketed == 0)
            {
                if (typeAt(cursor) == typeAt(globals::kMsgWinOpenAngle) || typeAt(cursor) == typeAt(globals::kMsgWinOpenParen))
                    msgwin->bracketed = 1;
            }
            else if (typeAt(cursor) == typeAt(globals::kMsgWinCloseAngle) || typeAt(cursor) == typeAt(globals::kMsgWinCloseParen))
                msgwin->bracketed = 0;
            if (msgwin->bracketed == 0)
                Se_PlayMessageSound(static_cast<char>(static_cast<int8_t>(msgwin->sound)));
        }
        if (globals::msgWinTypeStarted.get() == 0)
        {
            globals::msgWinTypeFrames.set(0);
            globals::msgWinTypeStarted.set(1);
        }
        globals::msgWinTypeFrames.set(globals::msgWinTypeFrames.get() + 1);
        if (msgwin->typeDelay > globals::msgWinTypeFrames.get())
            return;
        globals::msgWinTypeFrames.set(0);
        cursor = msgwin->readAt.address;
        const uint32_t text = msgwin->writeAt.address;
        const uint32_t lines = port::guest(msgwin->lines);
        bool newLine = false;
        if (cursor < msgwin->wordEnd.address)
        {
            msgwin->writeAt = GuestPtr<char>{text + 1u};
            port::store<int8_t>(text, typeAt(cursor));
        }
        else if (typeAt(cursor) == 0)
        {
            msgwin->bracketed = 0;
            msgwin->base.state = 3;
            const int32_t column = static_cast<int32_t>(msgwin->writeAt.address - lines) % 0x24;
            if (column != 0)
            {
                const int32_t pad = 0x24 - column;
                sdk::memset(msgwin->writeAt.address, 0x20, pad);
                msgwin->writeAt = GuestPtr<char>{msgwin->writeAt.address + static_cast<uint32_t>(pad)};
            }
            msgwin->exactFit = 0;
        }
        else
        {
            int word = sdk::strcspn(cursor + 1u, globals::kMsgWinWordBreaks) + 1;
            if (word > 0x23)
                word = 0x23;
            if (msgwin->exactFit == 0)
            {
                const uint32_t at = msgwin->writeAt.address;
                const int8_t c = typeAt(msgwin->readAt.address);
                const int32_t room = 0x24 - static_cast<int32_t>(at - lines) % 0x24;
                if (c == '\n' || c == '\r' || room - word < 0)
                {
                    sdk::memset(at, 0x20, room);
                    newLine = true;
                    msgwin->writeAt = GuestPtr<char>{msgwin->writeAt.address + static_cast<uint32_t>(room)};
                }
                else if (room == word)
                {
                    msgwin->exactFit = 1;
                    const uint32_t write = msgwin->writeAt.address;
                    const int8_t next = typeAt(msgwin->readAt.address);
                    msgwin->writeAt = GuestPtr<char>{write + 1u};
                    port::store<int8_t>(write, next);
                }
                else
                {
                    msgwin->writeAt = GuestPtr<char>{at + 1u};
                    port::store<int8_t>(at, c);
                }
            }
            else
            {
                msgwin->exactFit = 0;
                newLine = true;
            }
            msgwin->wordEnd = GuestPtr<char>{msgwin->wordEnd.address + static_cast<uint32_t>(word)};
        }
        msgwin->readAt = GuestPtr<char>{msgwin->readAt.address + 1u};
        port::store<int8_t>(msgwin->writeAt.address, 0);
        if (newLine && static_cast<int32_t>(msgwin->writeAt.address - lines) / 0x24 > 3 && typeAt(msgwin->readAt.address) != 0)
        {
            sdk::memmove(lines, port::guest(msgwin->lines) + 0x24u, 0x6D);
            TextBox_SetText(msgwin->text, GuestPtr<char>{lines}, 1);
            msgwin->writeAt = GuestPtr<char>{msgwin->writeAt.address - 0x24u};
        }
        else
            TextBox_AppendText(msgwin->text, GuestPtr<char>{text}, 1);
    }

    PORT_FN(MsgWin_TypeNext);

    // ---- starting the name entry (batch 39)

    // Opens the name entry: the keyboard set (`mode`, the text pointer table at 0x1B13E0), the name length (up to 24,
    // maxLength) with the name cleared or taken from `text`, the scrolling hint (`hint`, else empty; scrolled when
    // longer than 10 characters), the cursor at the end of the name, the state (3 when `noSlide` is 0, else 1) with the
    // board model placed (the y of its matrix, 0.3 lower in state 3), the keyboard text from its first line (6
    // characters a line) and the arrows for more lines above and below (moreAbove, moreBelow).
    void NameEntry_Setup(GuestPtr<NameEntry> entry, uint32_t mode, uint8_t maxLength, int noSlide, int hint, GuestPtr<char> text)
    {
        const auto empty = [] { return GuestPtr<char>{globals::kNameEntryEmptyText}; };
        entry->keySet = static_cast<int32_t>(mode & 0xFFu);
        entry->blink = 0;
        if (maxLength > 0x18)
            maxLength = 0x18;
        entry->maxLength = maxLength;
        for (int i = 0; i < maxLength + 1; ++i)
            entry->name[i] = static_cast<char>(0);
        entry->noSlide = noSlide;
        entry->slideFrames = 0;
        entry->moreAbove = 0;
        entry->moreBelow = 0;
        sdk::strcpy(port::guest(entry->hint), hint == 0 ? globals::kNameEntryEmptyText : static_cast<uint32_t>(hint));
        entry->hintStep = 0;
        GuestPtr<TextBox> shown;
        if (static_cast<uint32_t>(sdk::strlen(port::guest(entry->hint))) < 0xB)
        {
            entry->hintScroll = -1;
            TextBox_SetText(entry->hintText, GuestPtr<char>{port::guest(entry->hint)}, 1);
            shown = entry->nameText;
        }
        else
        {
            entry->hintScroll = 0;
            entry->hintStep = 1; // the first scroll step only counts (it moves every tenth frame)
            shown = entry->nameText;
        }
        TextBox_SetText(shown, empty(), 0);
        if (!text)
            TextBox_SetText(entry->nameText, empty(), 0);
        else
        {
            sdk::strcpy(port::guest(entry->name), text.address);
            TextBox_SetText(entry->nameText, GuestPtr<char>{port::guest(entry->name)}, 1);
        }
        entry->length = sdk::strlen(port::guest(entry->name));
        if (static_cast<int32_t>(entry->maxLength) <= entry->length)
            entry->length = entry->length - 1;
        for (int length = sdk::strlen(port::guest(entry->name)); length < static_cast<int>(entry->maxLength); ++length)
            TextBox_AppendText(entry->nameText, GuestPtr<char>{globals::kNameEntryMark}, 0);
        if (entry->noSlide == 0)
        {
            entry->boardMatrix[3].y = static_cast<float>(ps2f(static_cast<float>(entry->restHeight)) + ps2f(0.3f));
            entry->state = 3;
        }
        else
        {
            entry->boardMatrix[3].y = entry->restHeight;
            entry->state = 1;
        }
        TextBox_SetText(entry->lineText, empty(), 0);
        entry->column = 0;
        entry->row = 0;
        entry->firstRow = 0;
        const uint32_t keys = port::load<uint32_t>(globals::kNameEntryKeySets + 4u * static_cast<uint32_t>(entry->keySet));
        entry->keyCount = sdk::strlen(keys);
        entry->rows = entry->keyCount / 6;
        if (entry->keyCount % 6 != 0)
            entry->rows = entry->rows + 1;
        TextBox_SetText(entry->keysText, GuestPtr<char>{keys + 6u * static_cast<uint32_t>(entry->firstRow)}, 1);
        entry->markerMatrix[3].x = entry->markerX;
        entry->markerMatrix[3].y = entry->markerY;
        entry->moreAbove = entry->firstRow != 0 ? 1 : 0;
        entry->moreBelow = entry->firstRow + 4 < entry->rows ? 1 : 0;
    }

    PORT_FN(NameEntry_Setup);

    // ---- the name entry's input (batch 40)

    // One frame of the name entry. The keyboard is a grid of 6 columns (column the cursor column, row its row, firstRow
    // the first row shown of the `rows`, 4 shown) over the keys of the set at 0x1B13E0 (keySet; keyCount keys; a key
    // byte of 0xFF is "done", 0xFE a space, 0xFD a delete). Held directions (the pad's repeat bits) move the cursor
    // (up and down scroll the key text and set the arrows moreAbove / moreBelow), the square-like bits 8 and 2 step over or put
    // a space, 4 and 1 delete, and then the pressed buttons: 0x40 types the key under the cursor, 0x10 finishes
    // (state becomes 2). The name is in name with its length in length (at most maxLength); blink restarts
    // the cursor blink. The marker (markerMatrix) follows the cursor column (0.016 a column) and row (0.0227 a row)
    // from the bases markerX / markerY.
    void NameEntry_HandleInput(GuestPtr<NameEntry> entry)
    {
        bool moved = false, stayed = false;
        const uint32_t pressed = Pad_Pressed(0);
        const uint32_t repeat = static_cast<uint32_t>(Pad_GetRepeat());
        const auto click = [&](int sound) { Sound_PlaySe(sound, 0, 0x3C, 0.0f, 0.0f); };
        const auto keys = [&] { return port::load<uint32_t>(globals::kNameEntryKeySets + 4u * static_cast<uint32_t>(entry->keySet)); };
        const auto arrows = [&] {
            entry->moreAbove = entry->firstRow != 0 ? 1 : 0;
            entry->moreBelow = entry->firstRow + 4 < entry->rows ? 1 : 0;
        };
        const auto keysText = [&] { TextBox_SetText(entry->keysText, GuestPtr<char>{keys() + 6u * static_cast<uint32_t>(entry->firstRow)}, 1); };
        // A space: step over the character under the cursor, or put a space at the end if there is room.
        const auto space = [&] {
            if (entry->name[entry->length] != 0)
            {
                entry->blink = 0;
                entry->length = entry->length + 1;
            }
            else if (entry->length < entry->maxLength)
            {
                entry->blink = 0;
                const int32_t at = entry->length;
                entry->length = at + 1;
                entry->name[at] = static_cast<char>(' ');
            }
            if (!(entry->length < entry->maxLength))
                entry->length = entry->length - 1;
            click(0);
        };
        const auto erase = [&] {
            entry->length = sdk::strlen(port::guest(entry->name));
            const int32_t at = entry->length;
            if (at == 0)
                return;
            entry->length = at - 1;
            entry->name[at - 1] = static_cast<char>(0);
            entry->blink = 0;
            click(0);
        };
        if ((repeat & 0x1000u) != 0)
        {
            if (entry->row != 0)
            {
                entry->blink = 0;
                entry->row = entry->row - 1;
                if (entry->row < entry->firstRow)
                {
                    entry->firstRow = entry->firstRow - 1;
                    keysText();
                }
                else
                    stayed = true;
                arrows();
                click(0);
            }
        }
        else if ((repeat & 0x4000u) != 0)
        {
            if (entry->row + 1 < entry->rows)
            {
                entry->blink = 0;
                entry->row = entry->row + 1;
                if (!(entry->column + entry->row * 6 < entry->keyCount))
                {
                    moved = true;
                    entry->column = remainder(entry->keyCount - 1, 6);
                }
                if (entry->row < entry->firstRow + 4)
                    stayed = true;
                else
                {
                    entry->firstRow = entry->firstRow + 1;
                    keysText();
                }
                arrows();
                click(0);
            }
        }
        else if ((repeat & 0x8000u) != 0)
        {
            entry->blink = 0;
            entry->column = entry->column - 1;
            if (entry->column < 0)
            {
                entry->column = 5;
                if (!(entry->column + entry->row * 6 < entry->keyCount))
                    entry->column = remainder(entry->keyCount - 1, 6);
            }
            click(0);
            moved = true;
        }
        else if ((repeat & 0x2000u) != 0)
        {
            entry->blink = 0;
            entry->column = entry->column + 1;
            if (!(entry->column < 6))
                entry->column = 0;
            if (!(entry->column + entry->row * 6 < entry->keyCount))
                entry->column = 0;
            click(0);
            moved = true;
        }
        else if ((repeat & 8u) != 0 || (repeat & 2u) != 0)
            space();
        else if ((repeat & 4u) != 0 || (repeat & 1u) != 0)
            erase();
        else if ((pressed & 0x40u) != 0)
        {
            const uint32_t key = keys() + static_cast<uint32_t>(entry->column + entry->row * 6);
            const uint8_t code = port::load<uint8_t>(key);
            if (code == 0xFF)
            {
                entry->state = 2;
                click(1);
            }
            else if (code == 0xFE)
                space();
            else if (code == 0xFD)
                erase();
            else if (entry->length < entry->maxLength)
            {
                entry->blink = 0;
                const int32_t at = entry->length;
                entry->length = at + 1;
                entry->name[at] = static_cast<char>(port::load<uint8_t>(key));
                if (!(entry->length < entry->maxLength))
                    entry->length = entry->length - 1;
                click(0);
            }
        }
        else if ((pressed & 0x10u) != 0)
        {
            click(1);
            entry->state = 2;
        }
        if (moved)
        {
            entry->markerMatrix[3].x = static_cast<float>(ps2f(static_cast<float>(entry->markerX)) + ps2f(0.016f) * fromInt(entry->column));
        }
        if (stayed)
        {
            entry->markerMatrix[3].y = static_cast<float>(ps2f(static_cast<float>(entry->markerY)) + ps2f(0.0227f) * fromInt(entry->row - entry->firstRow));
        }
    }

    PORT_FN(NameEntry_HandleInput);

    // ---- Destructors (plan P6 batch 1) ----

    GuestPtr<PhoneCalls> PhoneCall_DeletingDtor(GuestPtr<PhoneCalls> phone, int16_t deleteFlag)
    {
        if (phone && 0 < deleteFlag)
            sdk::operator_delete(phone.address);
        return phone;
    }

    // The vtable goes back to the base class's before the object is deleted.
    GuestPtr<Widget> Widget_dtor(GuestPtr<Widget> widget, int16_t deleteFlag)
    {
        if (widget)
        {
            widget->vtable = globals::kWidgetVtable;
            if (0 < deleteFlag)
                sdk::operator_delete(widget.address);
        }
        return widget;
    }

    GuestPtr<Minimap> Minimap_DeletingDtor(GuestPtr<Minimap> minimap, int16_t deleteFlag)
    {
        if (minimap)
        {
            BaseObject_dtor(static_cast<int>(minimap.address), 0);
            if (0 < deleteFlag)
                sdk::operator_delete(minimap.address);
        }
        return minimap;
    }

    GuestPtr<Frame> Frame_DeletingDtor(GuestPtr<Frame> frame, int16_t deleteFlag)
    {
        if (frame)
        {
            BaseObject_dtor(static_cast<int>(frame.address), 0);
            if (0 < deleteFlag)
                sdk::operator_delete(frame.address);
        }
        return frame;
    }

    // Waits for the GS (the box's packet may still be in flight), frees the packet and deletes the box when the flag is
    // positive.
    GuestPtr<TextBox> TextBox_dtor(GuestPtr<TextBox> textbox, int16_t deleteFlag)
    {
        if (textbox)
        {
            sdk::sceGsSyncPath(0, 0);
            if (textbox->packet != 0u)
            {
                sdk::free(textbox->packet);
                textbox->packet = 0u;
            }
            if (0 < deleteFlag)
                sdk::operator_delete(textbox.address);
        }
        return textbox;
    }

    PORT_FN(PhoneCall_DeletingDtor);
    PORT_FN(Widget_dtor);
    PORT_FN(Minimap_DeletingDtor);
    PORT_FN(Frame_DeletingDtor);
    PORT_FN(TextBox_dtor);

    // ---- Destructors (plan P6 batch 2): each deletes the text boxes and frames it owns, in field order ----

    GuestPtr<MsgWin> MsgWin_dtor(GuestPtr<MsgWin> msgwin, int16_t deleteFlag)
    {
        if (msgwin)
        {
            msgwin->base.vtable = globals::kMsgWinVtable;
            lifetime::deleteFrame(msgwin.field(&MsgWin::arrowPacket).address);
            lifetime::deleteTextBox(msgwin.field(&MsgWin::text).address);
            Widget_dtor(GuestPtr<Widget>{msgwin.address}, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(msgwin.address);
        }
        return msgwin;
    }

    // The 32 item texts and the cursor frame.
    GuestPtr<ListMenu> ListMenu_dtor(GuestPtr<ListMenu> menu, int16_t deleteFlag)
    {
        if (menu)
        {
            for (uint32_t i = 0; i < 32u; ++i)
                lifetime::deleteTextBox(packet::addressOf((GuestPtr<ListMenuItem>::of(menu->items[0]) + static_cast<int32_t>(i))->text));
            lifetime::deleteFrame(menu.field(&ListMenu::cursorFrame).address);
            if (0 < deleteFlag)
                sdk::operator_delete(menu.address);
        }
        return menu;
    }

    GuestPtr<WantsWin> WantsWin_dtor(GuestPtr<WantsWin> wants, int16_t deleteFlag)
    {
        if (wants)
        {
            for (uint32_t offset = 0x508u; offset <= 0x514u; offset += 4u)
                lifetime::deleteTextBox(wants.address + offset);
            if (0 < deleteFlag)
                sdk::operator_delete(wants.address);
        }
        return wants;
    }

    // The text box at +8 and the two objects at +0 and +4, which delete themselves through their vtables.
    GuestPtr<LoadingScreen> LoadingScreen_dtor(GuestPtr<LoadingScreen> loading, int16_t deleteFlag)
    {
        if (loading)
        {
            lifetime::deleteTextBox(loading.address + 8u);
            lifetime::deleteVirtual(loading.address + 0u, 0);
            lifetime::deleteVirtual(loading.address + 4u, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(loading.address);
        }
        return loading;
    }

    GuestPtr<StatusPanel> StatusPanel_dtor(GuestPtr<StatusPanel> panel, int16_t deleteFlag)
    {
        if (panel)
        {
            for (uint32_t offset = 0xACu; offset >= 0xA0u; offset -= 4u)
                lifetime::deleteTextBox(panel.address + offset);
            BaseObject_dtor(static_cast<int>(panel.address), 0);
            if (0 < deleteFlag)
                sdk::operator_delete(panel.address);
        }
        return panel;
    }

    GuestPtr<NameEntry> NameEntry_dtor(GuestPtr<NameEntry> entry, int16_t deleteFlag)
    {
        if (entry)
        {
            entry->vtable = globals::kNameEntryVtable;
            for (uint32_t offset = 4u; offset <= 0x10u; offset += 4u)
                lifetime::deleteTextBox(entry.address + offset);
            if (0 < deleteFlag)
                sdk::operator_delete(entry.address);
        }
        return entry;
    }

    // Two views of 0xD0 bytes: the fade and info frames, then five lines each with the frame behind it.
    GuestPtr<CityCursor> CityCursor_dtor(GuestPtr<CityCursor> cursor, int16_t deleteFlag)
    {
        if (cursor)
        {
            for (int32_t parity = 0; parity < 2; ++parity)
            {
                CityCursorView &view = cursor->views[parity];
                lifetime::deleteFrame(port::guest(&view.fadeFrame));
                lifetime::deleteFrame(port::guest(&view.frame));
                for (int32_t line = 0; line < 5; ++line)
                {
                    lifetime::deleteTextBox(port::guest(&view.lines[line]));
                    lifetime::deleteFrame(port::guest(&view.lineFrames[line]));
                }
            }
            BaseObject_dtor(static_cast<int>(cursor.address), 0);
            if (0 < deleteFlag)
                sdk::operator_delete(cursor.address);
        }
        return cursor;
    }

    GuestPtr<Laptop> Laptop_dtor(GuestPtr<Laptop> laptop, int16_t deleteFlag)
    {
        if (laptop)
        {
            lifetime::deleteTextBox(laptop.field(&Laptop::text).address);
            if (0 < deleteFlag)
                sdk::operator_delete(laptop.address);
        }
        return laptop;
    }

    // The applicants window embeds an empty Stage object at the odd offset +0x289, whose destructor is called with flag
    // 0.
    GuestPtr<Applicants> ApplicantsBase_dtor(GuestPtr<Applicants> applicants, int16_t deleteFlag)
    {
        if (applicants)
        {
            applicants->vtable = globals::kApplicantsBaseVtable;
            lifetime::deleteTextBox(port::guest(&applicants->cardText));
            Stage_DeletingDtor(static_cast<int>(port::guest(&applicants->stage)), 0);
            if (0 < deleteFlag)
                sdk::operator_delete(applicants.address);
        }
        return applicants;
    }

    GuestPtr<Notice> Notice_dtor(GuestPtr<Notice> notice, int16_t deleteFlag)
    {
        if (notice)
        {
            lifetime::deleteTextBox(notice.address + 8u);
            lifetime::deleteTextBox(notice.field(&Notice::text2).address);
            if (0 < deleteFlag)
                sdk::operator_delete(notice.address);
        }
        return notice;
    }

    // The cursor frame, the contents index, then the six page texts.
    GuestPtr<TownPages> TownPages_dtor(GuestPtr<TownPages> pages, int16_t deleteFlag)
    {
        if (pages)
        {
            lifetime::deleteFrame(pages.field(&TownPages::cursorFrame).address);
            const uint32_t index = pages->index.address;
            if (index != 0u)
            {
                sdk::operator_delete(index);
                pages->index = GuestPtr<TownPagesIndex>{0u};
            }
            for (uint32_t offset = 4u; offset <= 0x18u; offset += 4u)
                lifetime::deleteTextBox(pages.address + offset);
            if (0 < deleteFlag)
                sdk::operator_delete(pages.address);
        }
        return pages;
    }

    // The "please wait" text box (global at 0x1BDAC4).
    void WaitText_Delete()
    {
        lifetime::deleteTextBox(globals::waitTextBox.address);
    }

    PORT_FN(MsgWin_dtor);
    PORT_FN(ListMenu_dtor);
    PORT_FN(WantsWin_dtor);
    PORT_FN(LoadingScreen_dtor);
    PORT_FN(StatusPanel_dtor);
    PORT_FN(NameEntry_dtor);
    PORT_FN(CityCursor_dtor);
    PORT_FN(Laptop_dtor);
    PORT_FN(ApplicantsBase_dtor);
    PORT_FN(Notice_dtor);
    PORT_FN(TownPages_dtor);
    PORT_FN(WaitText_Delete);

    // ---- Town's Pages and text box set-up (plan P6 batch 2) ----

    // The book of the Town's Pages: a cursor frame, the book model (0x1F3B20) with its matrix moved to the reading position
    // (0x1B1A60), nothing loaded yet.
    GuestPtr<TownPages> TownPages_Init(GuestPtr<TownPages> pages)
    {
        const uint32_t memory = sdk::operator_new(0xC0u);
        uint32_t frame = 0u;
        if (memory != 0u)
            frame = Frame_ctor(GuestPtr<Frame>{memory}, 0x1E, 0x64, 0x64, 0x18, 0xC, 0x10000000u).address;
        pages->cursorFrame = GuestPtr<Frame>{frame};
        pages->book = globals::townPagesModel.get();
        pages->mode = 0;
        pages->bookMatrix = pages->book.field(&A3DObject::matrix);
        const GuestPtr<Vec4> bookPlace{globals::kTownPagesClosed};
        pages->bookMatrix[3].x = bookPlace->x;
        pages->bookMatrix[3].y = bookPlace->y;
        pages->bookMatrix[3].z = bookPlace->z;
        pages->openTime = 0.0f;
        for (uint32_t offset = 4u; offset <= 0x18u; offset += 4u)
            port::store<uint32_t>(pages.address + offset, 0u);
        pages->index = GuestPtr<TownPagesIndex>{0u};
        pages->page = 0;
        pages->targetRow = 0;
        pages->row = 0;
        pages->savedPage = 0;
        pages->savedRow = 0;
        return pages;
    }

    // Sets up a text box of `columns` (twice that many cells wide) by `rows`: its place on screen (0x6C00, 0x7900, depth
    // 16000000), the cell size and pitch (0xC0 by 0xB0), the palette (style -1 and -2 are the two built-in ones at 0x1B24A0 and
    // 0x1B24E0), and the packet buffer (11 quadwords a cell, plus 12 and 8 for a palette) it is then built into; the cells' words
    // at +4 are set to the global at 0x1BDC98.
    GuestPtr<TextBox> TextBox_Init(GuestPtr<TextBox> textbox, int columns, int rows, int style)
    {
        textbox->columns = columns << 1;
        textbox->rows = rows;
        textbox->x = 0x6C00;
        textbox->y = 0x7900;
        textbox->z = 16000000;
        textbox->charWidth = 0xC0;
        textbox->charHeight = 0xB0;
        textbox->pitchX = textbox->charWidth;
        textbox->pitchY = textbox->charHeight;
        textbox->cursor = 0;
        const int32_t cells = textbox->columns * textbox->rows;
        if (style == -1)
            style = static_cast<int>(globals::kTextBoxStyleDefault);
        else if (style == -2)
            style = static_cast<int>(globals::kTextBoxStyleAlternate);
        const int32_t paletteQuads = style != 0 ? 8 : 0;
        textbox->clut = style;
        textbox->packet = sdk::malloc(static_cast<uint32_t>(paletteQuads + 0xC + cells * 11) << 4);
        TextBox_Build(textbox, textbox->clut);
        textbox->cursor = 0;
        const int32_t count = textbox->columns * textbox->rows;
        uint32_t cell = textbox->cells + 0x50u;
        for (int32_t i = 0; i < count; ++i, cell += 0x60u)
            port::store<uint32_t>(cell + 4u, globals::fontData.get());
        return textbox;
    }

    PORT_FN(TownPages_Init);
    PORT_FN(TextBox_Init);

    // Builds the book's index: one 8-byte entry per family type (count of known families, the first of them, the page the type
    // starts on), pages of 10 families each. The contents pages (10 types a page, at least one) come first, so every start page
    // moves up by their number, and the total page count includes them.
    void TownPages_BuildIndex(GuestPtr<TownPages> pages)
    {
        const int32_t types = globals::familyTypeCount.get();
        pages->index = GuestPtr<TownPagesIndex>{sdk::mw_NewArray(static_cast<uint32_t>(types) << 3)};
        const auto entry = [&](int32_t type) { return pages->index + type; };
        for (int32_t type = 0; type < types; ++type)
            entry(type)->count = 0;
        for (uint32_t family = 1; family < 10000u; ++family)
        {
            if (Family_IsKnown(family) == 0)
                continue;
            const int32_t type = Family_GetTypeIndex(static_cast<int>(family & 0xFFFFu));
            if (entry(type)->count == 0)
                entry(type)->firstFamily = static_cast<uint16_t>(family);
            entry(type)->count = static_cast<uint16_t>(entry(type)->count + 1u);
        }
        int32_t knownTypes = 0, pageCount = 0;
        for (int32_t type = 0; type < types; ++type)
        {
            const GuestPtr<TownPagesIndex> e = entry(type);
            if (e->count == 0)
            {
                e->startPage = 0;
                continue;
            }
            e->startPage = static_cast<uint16_t>(pageCount);
            ++knownTypes;
            pageCount += (static_cast<int32_t>(e->count) - 1) / 10 + 1;
        }
        pages->pageCount = pageCount;
        pages->contentsPages = (knownTypes + 9) / 10;
        if (pages->contentsPages == 0)
            pages->contentsPages = 1;
        for (int32_t type = 0; type < types; ++type)
        {
            const GuestPtr<TownPagesIndex> e = entry(type);
            const uint16_t start = e->startPage;
            if (start != 0xFFFFu)
                e->startPage = static_cast<uint16_t>(start + static_cast<uint16_t>(pages->contentsPages));
        }
        pages->pageCount = pages->pageCount + pages->contentsPages;
    }

    PORT_FN(TownPages_BuildIndex);

    // One frame of a logo screen (steps in pressStep): 0 loads the logo sprite (logo 0, 1 or any other: three files) and places
    // it, 1 draws the title layers until they are done (then the logo shows for 140 frames), 2 draws the logo and counts the
    // frames down, 3 draws the layers again until done, 4 deletes the sprite and reports the screen finished (1); 0 until then.
    int TitleScene_LogoStep(GuestPtr<TitleScene> scene, char logo)
    {
        const int step = scene->pressStep;
        if (step == 4)
        {
            const uint32_t sprite = scene->logoSprite.address;
            if (sprite != 0u)
            {
                port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(sprite) + 8u), sprite, 1);
                scene->logoSprite = GuestPtr<TexSprite>{0u};
            }
            return 1;
        }
        if (step == 3)
        {
            if (Title_DrawCommon(scene, 0xB) == 1)
                scene->pressStep = scene->pressStep + 1;
        }
        else if (step == 2)
        {
            TexSprite_Draw(scene->logoSprite);
            scene->introFrame = scene->introFrame - 1;
            if (!(scene->introFrame > 0))
                scene->pressStep = scene->pressStep + 1;
        }
        else if (step == 1)
        {
            if (Title_DrawCommon(scene, 10) == 1)
            {
                scene->pressStep = scene->pressStep + 1;
                scene->introFrame = 0x8C;
            }
        }
        else if (step == 0)
        {
            const uint32_t path = logo == 1 ? globals::kPathMfUsTex : (logo == 0 ? globals::kPathNatsumeTex : globals::kPathLogoTex);
            const uint32_t memory = sdk::operator_new(0x38u);
            uint32_t sprite = 0u;
            if (memory != 0u)
                sprite = static_cast<uint32_t>(TexSprite_Load(GuestPtr<TexSprite>{memory}, static_cast<int>(path), 0));
            scene->logoSprite = GuestPtr<TexSprite>{sprite};
            TexSprite_SetPosScale(scene->logoSprite, 0, 0, 0x3E00, 1.0f, 1.0f);
            scene->pressStep = scene->pressStep + 1;
        }
        return 0;
    }

    PORT_FN(TitleScene_LogoStep);

    // The stage briefing scene: the stage object base and a widget (widget), the scene's own vtables (vtable and the widget's),
    // the fields cleared, and with `loadStage` the next stage read (stage number + 1 kept in stage); then its light set (lights)
    // and camera, the camera placed with the eye and target vectors at 0x1B08D0 and 0x1B08E0.
    GuestPtr<StageScene> StageScene_ctor(GuestPtr<StageScene> scene, int loadStage)
    {
        struct Locals
        {
            uint8_t gap_00[0x30];
            Vec4 target;               // +0x30 the camera's target
            Vec4 eye;                  // +0x40 and eye
        };
        static_assert(offsetof(Locals, target) == 0x30 && offsetof(Locals, eye) == 0x40 && sizeof(Locals) == 0x50);
        port::GuestFrameOf<Locals> frame;
        const uint32_t s = scene.address;
        Stage_ctor(static_cast<int>(s));
        Widget_ctor(GuestPtr<Widget>::of(scene->widget));
        scene->vtable = globals::kStageSceneVtable;
        scene->widget.vtable = globals::kStageSceneWidgetVtable;
        scene->stage = 0;
        scene->step = 0;
        scene->phase = 0;
        scene->unk_30 = 0;
        scene->fadeMode = 0;
        scene->picture = GuestPtr<TexSprite>{0u};
        if (loadStage != 0)
        {
            Stage_Load();
            scene->stage = static_cast<int32_t>(static_cast<uint32_t>(globals::stageState()->stage) + 1u);
        }
        uint32_t lights = sdk::operator_new(0x1B0u);
        if (lights != 0u)
            lights = LightSet_ctor(GuestPtr<LightSet>{lights}).address;
        scene->lights = GuestPtr<LightSet>{lights};
        uint32_t camera = sdk::operator_new(0x310u);
        if (camera != 0u)
            camera = Camera_ctor(GuestPtr<Camera>{camera}).address;
        scene->camera = GuestPtr<Camera>{camera};
        frame->eye = *GuestPtr<Vec4>{globals::kStageSceneEye};
        frame->target = *GuestPtr<Vec4>{globals::kStageSceneTarget};
        Camera_SetEyeTarget(scene->camera, frame.at(&Locals::eye), frame.at(&Locals::target));
        Camera_UpdateMatrices(scene->camera);
        return scene;
    }

    PORT_FN(StageScene_ctor);

    // Builds the stage briefing's next text into the scene's buffer (text) and returns 1 while there is more: phase 0 looks the
    // stage number up in the list at 0x1B71F0 (a stage can have several special messages, one per call), then phase 1 gives the
    // stage's general text, phase 2 the goal in two parts (the heading at 0x1B72A0 with the goal text, then 0x1B72D0); the last part
    // ends it with 0 and clears the phase.
    int StageScene_BuildBriefingText(GuestPtr<StageScene> scene)
    {
        port::GuestFrame<0x120> frame;
        const uint32_t s = scene.address;
        if (scene->briefingPhase == 2)
        {
            bool more;
            if (scene->briefingIndex == 1)
            {
                sdk::strcpy(port::guest(scene->text), globals::kBriefingTextGoodLuck);
                more = true;
                scene->briefingIndex = scene->briefingIndex + 1;
            }
            else if (scene->briefingIndex == 0)
            {
                const uint32_t goal = frame.ptr().address + 0x20u;
                sdk::strcpy(port::guest(scene->text), globals::kBriefingTextTarget);
                Stage_FormatGoalText(static_cast<int>(s), GuestPtr<char>{goal});
                sdk::strcat(port::guest(scene->text), goal);
                more = true;
                scene->briefingIndex = scene->briefingIndex + 1;
            }
            else
                more = false;
            if (!more)
            {
                scene->briefingPhase = 0;
                scene->briefingIndex = 0;
                return 0;
            }
            return 1;
        }
        if (scene->briefingPhase != 1)
        {
            if (scene->briefingPhase != 0)
                return 1;
            bool found = false;
            for (;;)
            {
                const int32_t at = scene->briefingIndex;
                const uint32_t stageId = port::load<uint16_t>(globals::kBriefingExtraStages + 2u * static_cast<uint32_t>(at));
                if (stageId == 0)
                {
                    scene->briefingIndex = 0;
                    break;
                }
                if (static_cast<uint16_t>(scene->stage) == stageId)
                {
                    scene->briefingIndex = at + 1;
                    sdk::strcpy(port::guest(scene->text),
                                port::load<uint32_t>(globals::kBriefingExtraTexts + 4u * static_cast<uint32_t>(at)));
                    found = true;
                    break;
                }
                scene->briefingIndex = at + 1;
            }
            if (found)
                return 1;
            scene->briefingPhase = scene->briefingPhase + 1;
            scene->briefingIndex = 0;
        }
        const int32_t stage = scene->stage;
        sdk::sprintf(port::guest(scene->text), globals::kBriefingTextMission, static_cast<uint32_t>(stage),
                     port::load<uint32_t>(globals::kBriefingStageTexts + 4u * static_cast<uint32_t>(remainder(stage - 1, 0x19))));
        scene->briefingPhase = scene->briefingPhase + 1;
        scene->briefingIndex = 0;
        scene->nextAnimation = 2;
        return 1;
    }

    PORT_FN(StageScene_BuildBriefingText);

    // ---- Drawing the laptop and the phone call icon (plan P6 batch 2) ----

    // Draws the laptop: the fade and depth clear, its title text, and for each entry (count at 0x1BDAB8) the model node (list at
    // 0x1F36F0) with that entry's matrix (entryMatrices) copied into the node's draw slot first; the selected
    // entry (while the laptop is in state 2) is lit with the colour at 0x1AF690 (alpha 12), the others with the one at 0x1AF6A0.
    void Laptop_Draw(GuestPtr<Laptop> laptop)
    {
        struct Locals
        {
            uint8_t gap_00[0x70];
            Vec4 selected;             // +0x70 the selected entry's colour (w the colour table entry, 12)
            Vec4 other;                // +0x80 the others' (entry 0)
        };
        static_assert(offsetof(Locals, selected) == 0x70 && offsetof(Locals, other) == 0x80 && sizeof(Locals) == 0x90);
        port::GuestFrameOf<Locals> frame;
        DmaList_QueueFade(0x50, 0);
        DmaList_QueueClearZ();
        if (laptop->text.address != 0u)
            TextBox_Draw(laptop->text);
        uint32_t node = globals::kLaptopEntries;
        for (int32_t i = 0; i < globals::laptopEntryCount.get(); ++i, node += 4u)
        {
            const uint32_t model = laptop->model.address;
            const int32_t nodeIndex = port::load<int32_t>(node);
            const GuestPtr<A3DNode> target = laptop->model->model->nodes + nodeIndex;
            const GuestPtr<Vec4> rows = laptop.field(&Laptop::entryMatrices) + 4 * i;
            for (int32_t row = 0; row < 4; ++row)
                target->slot.matrix[row] = rows[row];
            A3DObject_DrawNode(GuestPtr<A3DObject>{model}, nodeIndex);
            if (laptop->state == 2 && i == laptop->selected)
            {
                frame->selected = *GuestPtr<Vec4>{globals::kLaptopSelectedColour};
                port::store<int32_t>(packet::addressOf(frame->selected.w), 0xC);
                A3DObject_ChangeLuminosity(laptop->model, frame.at(&Locals::selected), port::load<int32_t>(node));
            }
            else
            {
                frame->other = *GuestPtr<Vec4>{globals::kLaptopOtherColour};
                port::store<int32_t>(packet::addressOf(frame->other.w), 0);
                A3DObject_ChangeLuminosity(laptop->model, frame.at(&Locals::other), port::load<int32_t>(node));
            }
        }
    }

    // The ringing phone icon: while a call shows (its slide not 0), the ring frame counter steps; every other 84 frames a
    // ring sound plays and the icon's colour alpha flips between 0 and 12 every other frame; the icon model's node 1 is placed
    // by the call counter's distance from 10 (sliding in over ten frames) and drawn.
    void PhoneCall_Draw(GuestPtr<PhoneCalls> phone)
    {
        port::GuestFrame<0x50> frame;
        const uint32_t sp = frame.ptr().address;
        if (phone->iconSlide == 0)
            return;
        const GuestPtr<Vec4> colour{sp + 0x40u}; // its fourth word an integer, as the luminosity change takes it
        colour->x = 1.0f;
        colour->y = 1.0f;
        colour->z = 1.0f;
        port::store<int32_t>(packet::addressOf(colour->w), 0);
        phone->ringFrames = phone->ringFrames + 1;
        const int32_t frames = phone->ringFrames;
        if ((divide(frames, 0x54) & 1) == 0)
        {
            Sound_PlaySe(0x27, 0, 0x3C, 0.0f, 0.0f);
            port::store<int32_t>(packet::addressOf(colour->w), ((frames >> 1) & 1) != 0 ? 0xC : 0);
        }
        const int distance = sdk::abs(phone->iconSlide);
        const ps2f slide = fromInt(10 - distance) / ps2f(10.0f);
        const GuestPtr<Vec4> matrix = worldMatrix(modelNode(phone->iconModel, 1));
        Mat4_Copy(matrix, GuestPtr<Vec4>{globals::kIdentityMatrix});
        matrix[3].x = std::bit_cast<float>(0xBF428F5Cu); // -0.76
        matrix[3].y = static_cast<float>(ps2f(0.62f) + slide);
        matrix[3].z = std::bit_cast<float>(0xC027AE14u); // -2.62
        A3DObject_ChangeLuminosity(phone->iconModel, colour, 1);
        A3DObject_DrawNode(phone->iconModel, 1);
        if (phone->iconSlide < 10)
            phone->iconSlide = phone->iconSlide + 1;
    }

    PORT_FN(Laptop_Draw);
    PORT_FN(PhoneCall_Draw);

    // Builds a list menu: `visible` rows of `rowHeight` in a box reaching `width` right of x, a cursor frame, 32 empty
    // items (3 words each from +0xE4) and the clipping mask the menu draws first (ListMenuMask: its corners all 0 until
    // placed), then places the list at x, y with its texts' depth (mm_ListMenu_Place).
    GuestPtr<ListMenu> ListMenu_ctor(GuestPtr<ListMenu> menu, int visible, int rowHeight, int width, int x, int y, int depth)
    {
        const uint32_t m = menu.address;
        menu->visibleItems = visible;
        menu->itemHeight = rowHeight;
        menu->width = width;
        menu->cursorY = 0;
        menu->scroll = 0;
        menu->boxedItems = 0;
        menu->nextItem = 0;
        menu->itemCount = 0;
        menu->canCancel = 1;
        menu->canSelect = 1;
        menu->scrollStep = 0;
        const uint32_t memory = sdk::operator_new(0xC0u);
        uint32_t frame = 0u;
        if (memory != 0u)
            frame = Frame_ctor(GuestPtr<Frame>{memory}, 0x1E, 0, 0, 0x18, 0xC, 0x10000000u).address;
        menu->cursorFrame = GuestPtr<Frame>{frame};
        for (uint32_t offset = 0xE4u; offset < 0xE4u + 32u * 0xCu; offset += 4u)
            port::store<uint32_t>(m + offset, 0u);
        ListMenuMask &mask = menu->mask;
        packet::putWords(mask.dmaTag, {packet::kCnt | 0xCu, 0u});
        packet::putWords(mask.vifCodes, {0x11000000u, 0x5000000Cu});           // FLUSH, DIRECT 12
        packet::putWords(mask.gifTag, {0x800Bu, 0x10000000u, 0xEu, 0u});       // 11 loops (EOP) of A+D
        packet::putAd(mask.maskTest, 0x32001u, 0u, packet::kGsTest1);
        packet::putAd(mask.prim, 6u, 0u, packet::kGsPrim);
        for (GsAdWrite &corner : mask.corners)
            packet::putAd(corner, 0u, 0xFFFFFFFFu, packet::kGsXyzf2);          // Z 0xFFFFFF, the greatest
        packet::putAd(mask.drawTest, 0x5000Fu, 0u, packet::kGsTest1);
        packet::putWords(mask.ret, {packet::kRet, 0u, 0u, 0u});
        ListMenu_Place(menu, x, y, depth);
        return menu;
    }

    PORT_FN(ListMenu_ctor);

    // ---- Window and scene destructors (plan P6 batch 2) ----

    // The two list menus (mainMenu, slotMenu), the save manager (mc) and the text box (message).
    GuestPtr<PauseMenu> PauseMenu_dtor(GuestPtr<PauseMenu> pause, int16_t deleteFlag)
    {
        if (pause)
        {
            lifetime::deleteWith(pause.address + 4u, [](uint32_t o) { ListMenu_dtor(GuestPtr<ListMenu>{o}, 1); });
            lifetime::deleteWith(pause.address + 8u, [](uint32_t o) { ListMenu_dtor(GuestPtr<ListMenu>{o}, 1); });
            lifetime::deleteWith(pause.field(&PauseMenu::mc).address, [](uint32_t o) { McManager_dtor(GuestPtr<McManager>{o}, 1); });
            lifetime::deleteTextBox(pause.field(&PauseMenu::message).address);
            if (0 < deleteFlag)
                sdk::operator_delete(pause.address);
        }
        return pause;
    }

    GuestPtr<Applicants> Applicants_dtor(GuestPtr<Applicants> applicants, int16_t deleteFlag)
    {
        if (applicants)
        {
            applicants->vtable = globals::kApplicantsVtable;
            ApplicantsBase_dtor(applicants, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(applicants.address);
        }
        return applicants;
    }

    // The list menu (list), then the widget below.
    GuestPtr<MenuWin> MenuWin_dtor(GuestPtr<MenuWin> menuwin, int16_t deleteFlag)
    {
        if (menuwin)
        {
            menuwin->base.vtable = globals::kMenuWinVtable;
            lifetime::deleteWith(menuwin.field(&MenuWin::list).address, [](uint32_t o) { ListMenu_dtor(GuestPtr<ListMenu>{o}, 1); });
            Widget_dtor(GuestPtr<Widget>{menuwin.address}, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(menuwin.address);
        }
        return menuwin;
    }

    // Steps the window's closing by 0.1 (its Step slot); once it is done, the list is deleted and the state goes to 0.
    void MenuWin_Closing(GuestPtr<MenuWin> menuwin)
    {
        if (port::callGuest<int64_t>(menuwin->base.vtable->step, menuwin, ps2f(0.1f)) != 0)
        {
            lifetime::deleteWith(menuwin.field(&MenuWin::list).address, [](uint32_t o) { ListMenu_dtor(GuestPtr<ListMenu>{o}, 1); });
            menuwin->base.state = 0;
        }
    }

    // The lights and the camera, then the widget and the stage object base.
    GuestPtr<StageScene> StageScene_dtor(GuestPtr<StageScene> scene, int16_t deleteFlag)
    {
        if (scene)
        {
            scene->vtable = globals::kStageSceneVtable;
            scene->widget.vtable = globals::kStageSceneWidgetVtable;
            lifetime::deleteWith(port::guest(&scene->lights), [](uint32_t o) { LightSet_DeletingDtor(GuestPtr<LightSet>{o}, 1); });
            lifetime::deleteWith(port::guest(&scene->camera), [](uint32_t o) { Camera_DeletingDtor(GuestPtr<Camera>{o}, 1); });
            Widget_dtor(GuestPtr<Widget>::of(scene->widget), 0);
            Stage_DeletingDtor(static_cast<int>(scene.address), 0);
            if (0 < deleteFlag)
                sdk::operator_delete(scene.address);
        }
        return scene;
    }

    // The logo scene's children: the text boxes (hintText, messageBox), the sprites logoLetters, titleSprite, choices,
    // loadingFrame and loadingBar that delete themselves through their vtables, the models (menuModel, logoModel) and the
    // light set, the list menu (loadMenu), the save manager (mc), the camera and the loading screen; then the stage object base.
    GuestPtr<TitleScene> TitleScene_dtor(GuestPtr<TitleScene> scene, int16_t deleteFlag)
    {
        if (!scene)
            return scene;
        const uint32_t at = scene.address;
        lifetime::deleteTextBox(at + 0xB8u);
        for (uint32_t i = 0; i < 3u; ++i)
            lifetime::deleteVirtual(at + 0x10u + 4u * i, 0);
        lifetime::deleteVirtual(at + 0xCu, 0);
        lifetime::deleteWith(at + 0x24u, [](uint32_t o) { A3DObject_dtor(GuestPtr<A3DObject>{o}, 1); });
        lifetime::deleteWith(at + 0x28u, [](uint32_t o) { A3DObject_dtor(GuestPtr<A3DObject>{o}, 1); });
        lifetime::deleteWith(at + 0xB0u, [](uint32_t o) { ListMenu_dtor(GuestPtr<ListMenu>{o}, 1); });
        lifetime::deleteWith(at + 0xB4u, [](uint32_t o) { McManager_dtor(GuestPtr<McManager>{o}, 1); });
        lifetime::deleteWith(at + 4u, [](uint32_t o) { A3DObjectBase_DeletingDtor(GuestPtr<A3DObject>{o}, 1); });
        lifetime::deleteWith(at, [](uint32_t o) { Camera_DeletingDtor(GuestPtr<Camera>{o}, 1); });
        lifetime::deleteVirtual(at + 0x34u, 0);
        lifetime::deleteVirtual(at + 0x30u, 0);
        lifetime::deleteWith(at + 0x2Cu, [](uint32_t o) { LoadingScreen_dtor(GuestPtr<LoadingScreen>{o}, 1); });
        lifetime::deleteVirtual(at + 0x1Cu, 0);
        lifetime::deleteVirtual(at + 0x20u, 0);
        lifetime::deleteTextBox(at + 0xBCu);
        Stage_DeletingDtor(static_cast<int>(at), 0);
        if (0 < deleteFlag)
            sdk::operator_delete(at);
        return scene;
    }

    PORT_FN(PauseMenu_dtor);
    PORT_FN(Applicants_dtor);
    PORT_FN(MenuWin_dtor);
    PORT_FN(MenuWin_Closing);
    PORT_FN(StageScene_dtor);
    PORT_FN(TitleScene_dtor);

    // ---- The briefing scene's captions (plan P6 batch 2) ----
    //
    // Each caption is a text box (caption) over the scene's lights and camera, with the phase (phase; 0 makes the
    // box) and a fade (fade; 0x80 black, 0 clear) that is drawn over everything while it is at most 0x80.

    namespace
    {
        // The frame the caption draws every call: the GS defaults, the lights, the camera, the clear of Z and the text.
        void drawCaptionScene(uint32_t scene)
        {
            DmaList_QueueGsDefaults();
            const GuestPtr<StageScene> stage{scene};
            LightSet_SelectProgram(stage->lights, 0);
            Packet_Draw(GuestPtr<uint32_t>{stage->camera.address});
            DmaList_QueueClearZ();
            TextBox_Draw(stage->caption);
        }

        // The caption's text box (caption), style -1; none when the allocation fails.
        GuestPtr<TextBox> makeCaptionBox(GuestPtr<StageScene> scene, int columns, int rows)
        {
            uint32_t box = sdk::operator_new(0x40u);
            if (box != 0u)
                box = TextBox_Init(GuestPtr<TextBox>{box}, columns, rows, -1).address;
            scene->caption = GuestPtr<TextBox>{box};
            return GuestPtr<TextBox>{box};
        }

        // The phase counts up by one and the fade is set to black.
        void nextCaptionPhase(uint32_t scene)
        {
            GuestPtr<StageScene>{scene}->phase = GuestPtr<StageScene>{scene}->phase + 1;
            GuestPtr<StageScene>{scene}->fade = 0x80;
        }
    }

    // The stage number (three digits from stage) under the stage title text.
    void StageScene_ShowStageTitle(GuestPtr<StageScene> scene)
    {
        port::GuestFrame<0x30> frame;
        const uint32_t s = scene.address;
        if (scene->phase == 0)
        {
            const uint32_t number = frame.ptr().address + 0x2Cu;
            Text_FormatPadded(GuestPtr<char>{number}, scene->stage, 3);
            makeCaptionBox(scene, 0xD, 1);
            TextBox_SetCharSize(scene->caption, 0x120, 0xD0, 0x150, 0x140);
            TextBox_SetPos(scene->caption, 0x7880, 0x7F40, -0x100000);
            TextBox_SetText(scene->caption, GuestPtr<char>{globals::kStageTextStage}, 0);
            TextBox_AppendText(scene->caption, GuestPtr<char>{number}, 0);
            nextCaptionPhase(s);
        }
        drawCaptionScene(s);
    }

    // The city's name (up to its first line break, from 0x1C5680) on two rows.
    void StageScene_ShowCityName(GuestPtr<StageScene> scene)
    {
        port::GuestFrame<0x120> frame;
        const uint32_t s = scene.address;
        if (scene->phase == 0)
        {
            const uint32_t name = frame.ptr().address + 0x20u;
            makeCaptionBox(scene, 0xD, 2);
            TextBox_SetCharSize(scene->caption, 0x120, 0xD0, 0x150, 0x140);
            TextBox_SetPos(scene->caption, 0x7880, 0x7F40, -0x100000);
            sdk::strcpy(name, globals::kLoadedSaveInfo);
            const uint32_t lineBreak = sdk::strchr(name, 0xA);
            if (lineBreak != 0u)
                port::store<uint8_t>(lineBreak, 0);
            TextBox_SetText(scene->caption, GuestPtr<char>{name}, 0);
            nextCaptionPhase(s);
        }
        drawCaptionScene(s);
    }

    // The approval table: four rows of a name (padded to 16 characters, from the table at 0x1B0790 + 4) and a five-digit value
    // (from 0x1C5780). Phase 1 fades in (one step a frame), the phases up to 0x12C hold, and at 0x12C it fades out; when it is
    // dark the box goes, the phase restarts and the count of captions shown (step) grows. The fade is drawn while it is at
    // most 0x80.
    void StageScene_ShowApprovalRate(GuestPtr<StageScene> scene)
    {
        port::GuestFrame<0x80> frame;
        const uint32_t s = scene.address;
        const uint32_t line = frame.ptr().address + 0x60u;
        const auto phase = [&] { return scene->phase; };
        const auto fade = [&] { return scene->fade; };
        const auto text = [&] { return scene->caption; };
        const int32_t current = phase();
        if (current == 0x12C)
        {
            scene->fade = fade() + 1;
            if (fade() > 0x80)
            {
                lifetime::deleteTextBox(port::guest(&scene->caption));
                scene->phase = 0;
                scene->step = static_cast<uint8_t>(scene->step + 1u);
            }
        }
        else if (current == 1)
        {
            scene->fade = fade() - 1;
            if (fade() == 0)
                scene->phase = phase() + 1;
        }
        else if (current == 0)
        {
            makeCaptionBox(scene, 0xD, 6);
            TextBox_SetCharSize(text(), 0, 0xD0, 0, 0x140);
            TextBox_SetPos(text(), 0x7600, 0x7C20, -0x100000);
            TextBox_SetText(text(), GuestPtr<char>{globals::kStageTextApproval}, 0);
            for (uint32_t i = 0; i < 4u; ++i)
            {
                TextBox_AppendText(text(), GuestPtr<char>{globals::kStageTextIndent}, 0);
                sdk::strcpy(line, port::load<uint32_t>(globals::kFriendshipNames + 4u + 4u * i));
                uint32_t end = line + static_cast<uint32_t>(sdk::strlen(line));
                for (; end < line + 0x10u; ++end)
                    port::store<uint8_t>(end, 0x20);
                port::store<uint8_t>(end, 0);
                TextBox_AppendText(text(), GuestPtr<char>{line}, 1);
                Text_FormatPadded(GuestPtr<char>{line}, port::load<int32_t>(globals::kFriendshipCounts + 4u * i), 5);
                TextBox_AppendText(text(), GuestPtr<char>{line}, 1);
            }
            nextCaptionPhase(s);
        }
        else
            scene->phase = current + 1;
        if (fade() <= 0x80)
        {
            drawCaptionScene(s);
            DmaList_QueueClearZ();
            DmaList_QueueFade(fade(), 0);
        }
    }

    PORT_FN(StageScene_ShowStageTitle);
    PORT_FN(StageScene_ShowCityName);
    PORT_FN(StageScene_ShowApprovalRate);

    // The scrolling telop of the briefing. The kind of telop follows the stage: 0 for stage 1, 1 for stage 5, 2 for the
    // others (which also name the city and its mayor). Phase 0 loads the sound bank and writes the text into a box of 17 by 20,
    // 1 starts the music once the sound driver is idle, 2 scrolls the box up (scroll grows by 2 a frame; on stage 6 by 1, and by 2
    // every seventh frame) until Start or Cross is pressed (not on stage 6) or the scroll has gone far enough, then the fade
    // (fade, counting up from 1) takes the picture out, the music is stopped, the box and the picture are deleted and
    // the phase restarts with the step counted on (step).
    void StageScene_ShowTelop(GuestPtr<StageScene> scene)
    {
        const uint32_t s = scene.address;
        const int32_t stage = scene->stage;
        const uint32_t kind = stage == 1 ? 0u : (stage == 5 ? 1u : 2u);
        const auto text = [&] { return scene->caption; };
        const auto append = [&](uint32_t string) { TextBox_AppendText(text(), GuestPtr<char>{string}, 0); };
        const auto nextPhase = [&] { scene->phase = scene->phase + 1; };
        switch (scene->phase)
        {
        case 0:
        {
            sdk::sprintf(globals::kTelopBankSqName, globals::kTelopBankName, kind + 1u);
            sdk::strcpy(globals::kTelopBankHdName, globals::kTelopBankSqName);
            sdk::strcpy(globals::kTelopBankBdName, globals::kTelopBankSqName);
            sdk::strcat(globals::kTelopBankSqName, globals::kTelopBankSq);
            sdk::strcat(globals::kTelopBankHdName, globals::kTelopBankHd);
            sdk::strcat(globals::kTelopBankBdName, globals::kTelopBankBd);
            SoundCmd_MidiOpen(7, static_cast<int>(globals::kTelopBankSqName),
                              static_cast<int>(globals::kTelopBankHdName), static_cast<int>(globals::kTelopBankBdName),
                              0);
            makeCaptionBox(scene, 0x11, 0x14);
            TextBox_SetCharSize(text(), 0, 0xD0, 0, 0x140);
            if (kind != 2)
                TextBox_SetText(text(), GuestPtr<char>{port::load<uint32_t>(globals::kTelopTexts + 4u * kind)}, 0);
            else
            {
                TextBox_SetText(text(), GuestPtr<char>{globals::kTelopTextThanks}, 0);
                if (port::load<int8_t>(globals::kMayorGivenName) == 0 && port::load<int8_t>(globals::kMayorName) == 0)
                    append(globals::kTelopTextYour);
                else
                {
                    append(globals::kTelopTextMister);
                    append(globals::kMayorGivenName);
                    append(globals::kMayorName);
                    append(globals::kTelopTextApostrophe);
                    if (port::load<int8_t>(globals::kMayorName) == 0 ||
                        port::load<int8_t>(globals::kMayorName + static_cast<uint32_t>(sdk::strlen(globals::kMayorName)) - 1u) != 0x73)
                        append(globals::kTelopTextS);
                }
                append(port::load<uint32_t>(globals::kTelopTexts + 0x8u));
            }
            scene->scroll = 0;
            scene->fade = 0;
            nextPhase();
            break;
        }
        case 1:
            if (Sound_IsBusy() == 0)
            {
                SoundCmd_MidiSetVolume(0, port::load<int32_t>(globals::kTelopVolumes + 4u * kind), 0);
                SoundCmd_MidiPlay(0, 7, 0, 0, 0);
                nextPhase();
            }
            break;
        case 2:
        {
            const int32_t position = scene->scroll;
            scene->scroll = position + (stage == 6 && remainder(position, 7) != 0 ? 1 : 2);
            if (scene->fade != 0)
                scene->fade = scene->fade + 1;
            else
            {
                uint32_t buttons = Pad_Held(0);
                if (stage == 6)
                    buttons = 0;
                if (buttons == 0x800u || buttons == 0x40u || port::load<int32_t>(globals::kTelopScrollEnds + 4u * kind) < scene->scroll)
                {
                    scene->fade = 1;
                    SoundCmd_MidiFade(0, 0, 0x20, 0);
                }
            }
            if (scene->fade > 0x80)
            {
                SoundCmd_MidiClose(7, 0);
                lifetime::deleteTextBox(port::guest(&scene->caption));
                lifetime::deleteVirtual(port::guest(&scene->picture), 0);
                scene->phase = 0;
                scene->step = static_cast<uint8_t>(scene->step + 1u);
            }
            break;
        }
        default:
            break;
        }
        if (scene->fade <= 0x80)
        {
            DmaList_QueueGsDefaults();
            LightSet_SelectProgram(scene->lights, 0);
            Packet_Draw(GuestPtr<uint32_t>{scene->camera.address});
            DmaList_QueueClearZ();
            if (scene->picture.address != 0u)
                TexSprite_Draw(scene->picture);
            TextBox_SetPos(text(), 0x7380, 0x8700 - scene->scroll, 0xFF0000);
            TextBox_Draw(text());
            DmaList_QueueClearZ();
            DmaList_QueueFade(scene->fade, 0);
        }
    }

    PORT_FN(StageScene_ShowTelop);

    // ---- Window constructors (plan P6 batch 2) ----

    namespace
    {
        // A text box of `columns` by `rows` with the style given, or 0 when the allocation fails.
        uint32_t newTextBox(int columns, int rows, int style)
        {
            const uint32_t memory = sdk::operator_new(0x40u);
            return memory != 0u ? TextBox_Init(GuestPtr<TextBox>{memory}, columns, rows, style).address : 0u;
        }
    }

    // The message window: a text box of 18 by 4 (style -2) in the window, a frame for the "more" arrow (arrowPacket), and the shared
    // building model (0x1F3B2C, model).
    GuestPtr<MsgWin> MsgWin_ctor(GuestPtr<MsgWin> msgwin)
    {
        Widget_ctor(GuestPtr<Widget>{msgwin.address});
        msgwin->base.vtable = globals::kMsgWinVtable;
        msgwin->text = GuestPtr<TextBox>{newTextBox(0x12, 4, -2)};
        TextBox_SetPos(msgwin->text, 0x7240, 0x8240, -0x100000);
        const uint32_t memory = sdk::operator_new(0xC0u);
        uint32_t arrow = 0u;
        if (memory != 0u)
            arrow = Frame_ctor(GuestPtr<Frame>{memory}, 0x14, 0x21C, 0xBA, 0x18, 0xC, 0x10000000u).address;
        msgwin->arrowPacket = arrow;
        msgwin->model = globals::talkSceneModel.get();
        return msgwin;
    }

    // The notice line: two single-row text boxes of 25 columns (text and text2), nothing shown (id -1, no second line).
    GuestPtr<Notice> Notice_ctor(GuestPtr<Notice> notice)
    {
        notice->text = GuestPtr<TextBox>{newTextBox(0x19, 1, -1)};
        TextBox_SetPos(notice->text, 0x7740, 0x84E0, -1);
        TextBox_SetCharSize(notice->text, 0, 0xD0, 0, 0xE0);
        notice->id = -1;
        notice->text2 = GuestPtr<TextBox>{newTextBox(0x19, 1, -1)};
        TextBox_SetPos(notice->text2, 0x72E0, 0x8540, -1);
        TextBox_SetCharSize(notice->text2, 0, 0xB0, 0, 0xC0);
        notice->secondLine = 0;
        return notice;
    }

    // The loading screen: the background sprite (file at 0x1B7F70) and the sprite of file `file`, placed at (20, 20, 1) at full
    // scale, a text box of 24 by 4, a fade of 0x80 and the animation `style` of the sprite.
    GuestPtr<LoadingScreen> LoadingScreen_ctor(GuestPtr<LoadingScreen> loading, int file, uint8_t style)
    {
        uint32_t sprite = sdk::operator_new(0x38u);
        if (sprite != 0u)
            sprite = static_cast<uint32_t>(TexSprite_Load(GuestPtr<TexSprite>{sprite},
                                                          static_cast<int>(globals::kPathLoadingTTex), 1));
        loading->background = GuestPtr<TexSprite>{sprite};
        sprite = sdk::operator_new(0x38u);
        if (sprite != 0u)
            sprite = static_cast<uint32_t>(TexSprite_Load(GuestPtr<TexSprite>{sprite}, file, 1));
        loading->sprite = GuestPtr<TexSprite>{sprite};
        TexSprite_SetPosScale(loading->background, 0x14, 0x14, 1, ps2f(1.0f), ps2f(1.0f));
        loading->text = GuestPtr<TextBox>{newTextBox(0x18, 4, -1)};
        loading->frame = 0;
        loading->fade = 0x80;
        loading->spriteX = 0;
        loading->spriteY = 0;
        loading->spriteZ = 1;
        loading->style = style;
        loading->showText = 0;
        return loading;
    }

    // The "please wait" text box (global at 0x1BDAC4, 20 by 5 characters) with `text`, or the default text (0x1B65F0) when
    // there is none; its frame counters (0x1BDAC8, 0x1BDACC) restart.
    void WaitText_Create(GuestPtr<char> text)
    {
        globals::waitTextBox.set(newTextBox(0x14, 5, -1));
        TextBox_SetPos(GuestPtr<TextBox>{globals::waitTextBox.get()}, 0x7380, 0x8080, 0x4A000);
        TextBox_SetCharSize(GuestPtr<TextBox>{globals::waitTextBox.get()}, 0, 0xB0, 0, 0xC0);
        if (text)
            TextBox_SetText(GuestPtr<TextBox>{globals::waitTextBox.get()}, text, 0);
        else
            TextBox_SetText(GuestPtr<TextBox>{globals::waitTextBox.get()}, GuestPtr<char>{globals::kWaitText}, 0);
        globals::waitTextFrames.set(0);
        globals::waitTextMade.set(1);
    }

    // Opens the menu window: a list menu of `count` items (at most 4 except for menu -0x7D1, which keeps its count and a wider
    // list) and the window open (wideList: 1 for -0x7D1, else 0; except for menus -0x7D5 and -0x7D2 the list's canCancel is
    // cleared).
    void MenuWin_Open(GuestPtr<MenuWin> menuwin, int menuId, int count)
    {
        const uint32_t w = menuwin.address;
        Widget_Open(GuestPtr<Widget>{w});
        if (menuId == -0x7D1)
        {
            const uint32_t memory = sdk::operator_new(0x2A0u);
            uint32_t list = 0u;
            if (memory != 0u)
                list = ListMenu_ctor(GuestPtr<ListMenu>{memory}, count, 0xC, 0x136, 0xB4, 0x8A, 0xFF0000).address;
            menuwin->list = GuestPtr<ListMenu>{list};
            menuwin->size = count;
            menuwin->wideList = 1;
            return;
        }
        if (count >= 5)
            count = 4;
        menuwin->size = count;
        const uint32_t memory = sdk::operator_new(0x2A0u);
        uint32_t list = 0u;
        if (memory != 0u)
            list = ListMenu_ctor(GuestPtr<ListMenu>{memory}, menuwin->size, 0xC, 0x11D, 0xC8, 0x8A, 0xFF0000).address;
        menuwin->list = GuestPtr<ListMenu>{list};
        if (menuId != -0x7D5 && menuId != -0x7D2)
            menuwin->list->canCancel = 0;
        menuwin->wideList = 0;
    }

    PORT_FN(MsgWin_ctor);
    PORT_FN(Notice_ctor);
    PORT_FN(LoadingScreen_ctor);
    PORT_FN(WaitText_Create);
    PORT_FN(MenuWin_Open);

    // The applicants window base: the list cleared, its stage object (at +0x289), a text box of 18 by 4 (cardText) for the card,
    // the two card models (frameModel from 0x1F3B10 and cardModel from 0x1F3B68) with their matrices (frameMatrix, cardMatrix: the
    // models' own, and cardNodeA, cardNodeB: the second model's data pointers at +0x70 and +0x1F0), and the cursor state (-1 for
    // none in stateColour, enabled in enabled).
    GuestPtr<Applicants> ApplicantsBase_ctor(GuestPtr<Applicants> applicants)
    {
        Applicants_Clear(applicants);
        Stage_ctor(static_cast<int>(port::guest(&applicants->stage)));
        applicants->vtable = globals::kApplicantsBaseVtable;
        applicants->cardShown = 0;
        applicants->cardText = GuestPtr<TextBox>{newTextBox(0x12, 4, -2)};
        TextBox_SetPos(applicants->cardText, 0x79C0, 0x8080, -0x100000);
        applicants->frameModel = globals::applicantFrameModel.get();
        applicants->cardModel = globals::applicantCardModel.get();
        applicants->frameMatrix = GuestPtr<Vec4>::of(applicants->frameModel->matrix[0]);
        applicants->cardMatrix = GuestPtr<Vec4>::of(applicants->cardModel->matrix[0]);
        applicants->cardNodeA = worldMatrix(modelNode(applicants->cardModel, 0));
        applicants->cardNodeB = worldMatrix(modelNode(applicants->cardModel, 2));
        applicants->top = 0;
        applicants->cursor = 0;
        applicants->rollPhase = 0.0f;
        applicants->rollDir = 0.0f;
        applicants->sliding = 0;
        applicants->relight = 0;
        applicants->stateColour = 0xFF;
        applicants->enabled = 1;
        return applicants;
    }

    PORT_FN(ApplicantsBase_ctor);

    // Gives item `item` of the list (the next free one when -1, up to 32) its text box of `columns` by `rows` characters in
    // `style`, unless it has one. `mode` picks the character size: -1 gives (0, 0xB0, 0xB0, 0xC0), 0 gives (0, 0xB0, 0, 0xC0)
    // and any other (0, 0xD0, 0, 0xE0). The item's height (the second word of its 12-byte entry) grows to 12 a row (14 for the other modes)
    // and the boxed item count when this was the next item.
    void ListMenu_AddItems(GuestPtr<ListMenu> menu, int columns, int rows, int mode, int style, int, int item)
    {
        if (item == -1)
            item = menu->boxedItems;
        if (item >= 0x20)
            return;
        const GuestPtr<ListMenuItem> entry = GuestPtr<ListMenuItem>::of(menu->items[0]) + item; // up to 31, as the original
        if (entry->text)
            return;
        const uint32_t memory = sdk::operator_new(0x40u);
        const uint32_t box = memory != 0u ? TextBox_Init(GuestPtr<TextBox>{memory}, columns, rows, style).address : 0u;
        entry->text = GuestPtr<TextBox>{box};
        int height;
        if (mode == -1)
        {
            TextBox_SetCharSize(GuestPtr<TextBox>{box}, 0, 0xB0, 0xB0, 0xC0);
            height = rows * 12;
        }
        else if (mode == 0)
        {
            TextBox_SetCharSize(GuestPtr<TextBox>{box}, 0, 0xB0, 0, 0xC0);
            height = rows * 12;
        }
        else
        {
            TextBox_SetCharSize(GuestPtr<TextBox>{box}, 0, 0xD0, 0, 0xE0);
            height = rows * 14;
        }
        if (entry->textHeight < height)
            entry->textHeight = height;
        if (item == menu->boxedItems)
            menu->boxedItems = item + 1;
    }

    PORT_FN(ListMenu_AddItems);

    // The four text boxes of the name entry window (hintText 16 by 1, keysText 3 by 4 with larger glyphs, nameText 12 by 1 and
    // lineText 16 by 1, all at depth 0x10001000), and the model it shows: the shared model (0x1F3B6C, model), its matrix
    // (boardMatrix) and the world matrix of its node 1 (markerMatrix), set to the one at 0x1B2550 moved to markerX, markerY and
    // the board's z.
    void NameEntry_CreateTexts(GuestPtr<NameEntry> entry)
    {
        const auto box = [&](GuestPtr<TextBox> &field, int columns, int rows, int style, int x, int y) {
            field = GuestPtr<TextBox>{newTextBox(columns, rows, style)};
            TextBox_SetPos(field, x, y, 0x10001000);
        };
        box(entry->hintText, 0x10, 1, -1, 0x71E0, 0x7CB0);
        box(entry->keysText, 3, 4, -2, 0x7200, 0x7E20);
        TextBox_SetCharSize(entry->keysText, 0x150, 0xD0, 0x150, 0xE0);
        box(entry->nameText, 0xC, 1, -1, 0x7DA0, 0x8120);
        box(entry->lineText, 0x10, 1, -1, 0x7D30, 0x7FD0);
        entry->model = globals::nameEntryModel.get();
        entry->boardMatrix = entry->model.field(&A3DObject::matrix);
        entry->markerMatrix = worldMatrix(modelNode(entry->model, 1));
        entry->restHeight = 0.0f;
        entry->markerX = std::bit_cast<float>(0x3C03126Fu); // 0.008
        entry->markerY = std::bit_cast<float>(0x3B449BA6u); // 0.003
        Mat4_Copy(entry->markerMatrix, GuestPtr<Vec4>{globals::kIdentityMatrix});
        entry->markerMatrix[3].x = entry->markerX;
        entry->markerMatrix[3].y = entry->markerY;
        entry->markerMatrix[3].z = entry->boardMatrix[3].z;
    }

    PORT_FN(NameEntry_CreateTexts);

    // The card text of the applicant under the cursor (top), built in the buffer at 0x1F3790: the family's names (or, for the
    // home types 5 to 8, the mayor's and the city's, or a fixed text when there are none) padded to 30 characters; for the other
    // types two numbers of the family; the month and the year (the entry's month count split by 12), the name of the home type
    // and its mark by the type's rule; and, 30 characters further on, a line of 26 bytes of the building's description (picked by the
    // applicant's claim section) and the size of the type. The state's colour (the table at 0x1BD998 for states below 8) is put
    // in stateColour and the text goes into the card's text box (cardText). Nothing without applicants.
    void Applicants_BuildInfo(GuestPtr<Applicants> applicants)
    {
        port::GuestFrame<0x90> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t seat = sp + 0x68u, month = sp + 0x78u, year = sp + 0x70u, members = sp + 0x80u, size = sp + 0x88u;
        if (applicants->count == 0)
            return;
        const GuestPtr<ApplicantEntry> entry = applicants.as<ApplicantEntry>() + applicants->top;
        const GuestPtr<Family> family = Family_Get(entry->family, 0);
        const GuestPtr<BuildingType> type = globals::buildingType(family->homeType);
        Text_FormatPadded(GuestPtr<char>{seat}, family->members[0].age, 2);
        const int32_t months = entry->month;
        Text_FormatPadded(GuestPtr<char>{month}, remainder(months, 12) + 1, 2);
        Text_FormatPadded(GuestPtr<char>{year}, divide(months, 12) + 1, 3);
        Text_FormatPadded(GuestPtr<char>{members}, family->memberCount, 3);
        Text_FormatPadded(GuestPtr<char>{size}, type->size, 1);
        const uint32_t homeType = family->homeType;
        const bool official = homeType == 8 || homeType == 7 || homeType == 6 || homeType == 5;
        if (official)
        {
            if (port::load<int8_t>(globals::kMayorGivenName) == 0 && port::load<int8_t>(globals::kMayorName) == 0)
                sdk::strcpy(globals::kApplicantText, globals::kApplicantTextPlayer);
            else
                sdk::sprintf(globals::kApplicantText, globals::kApplicantTextName, globals::kMayorGivenName, globals::kMayorName);
        }
        else
            // the first member's names
            sdk::sprintf(globals::kApplicantText, globals::kApplicantTextName,
                         port::guest(family->members[0].givenName), port::guest(family->members[0].surname));
        port::store<uint8_t>(globals::kApplicantText + 0x19u, 0);
        int length = sdk::strlen(globals::kApplicantText);
        sdk::memset(globals::kApplicantText + static_cast<uint32_t>(length), 0x20, 0x1E - length);
        port::store<uint8_t>(globals::kApplicantText + 0x1Eu, 0);
        if (official)
            sdk::strcat(globals::kApplicantText, globals::kApplicantTextUnknown);
        else
            sdk::sprintf(globals::kApplicantText + static_cast<uint32_t>(sdk::strlen(globals::kApplicantText)),
                         globals::kApplicantTextLines, seat, members);
        sdk::sprintf(globals::kApplicantText + static_cast<uint32_t>(sdk::strlen(globals::kApplicantText)),
                     globals::kApplicantTextDate, month, year, port::guest(type->name));
        switch (type->rule)
        {
        case 4:
            sdk::strcat(globals::kApplicantText, globals::kApplicantTextUnit);
            break;
        case 3:
        case 2:
        case 1:
            sdk::strcat(globals::kApplicantText, globals::kApplicantTextBuilding);
            break;
        default:
            break;
        }
        sdk::strcat(globals::kApplicantText, globals::kApplicantTextNewline);
        const int base = sdk::strlen(globals::kApplicantText);
        const uint32_t line = globals::kApplicantText + static_cast<uint32_t>(base);
        const uint32_t table = globals::claimTexts.get();
        const uint32_t claim = applicants->claims[applicants->top];
        sdk::memcpy(line, table + port::load<uint16_t>(table + 2u * port::load<uint16_t>(table + 4u) + 2u * claim), 0x1A);
        port::store<uint8_t>(globals::kApplicantText + 0x1Au + static_cast<uint32_t>(base), 0);
        const int total = sdk::strlen(globals::kApplicantText);
        sdk::memset(globals::kApplicantText + static_cast<uint32_t>(total), 0x20, base + 0x1E - total);
        port::store<uint8_t>(globals::kApplicantText + 0x1Eu + static_cast<uint32_t>(base), 0);
        sdk::sprintf(line + 0x1Eu, globals::kApplicantTextMetres, size);
        const uint32_t state = static_cast<uint16_t>(entry->state);
        applicants->stateColour = state < 8 ? port::load<uint8_t>(globals::kApplicantStateColours + state) : 0;
        TextBox_SetText(applicants->cardText, GuestPtr<char>{globals::kApplicantText}, 0);
    }

    PORT_FN(Applicants_BuildInfo);

    // The status panel: the panel model (0x1F3B0C, model) with its light moved (0xBF47AE14, 0xBF051EB8, 0xC02CCCCD at the
    // model's +0x40), the panel's draw packet built at the packet buffer's position, the alert model (alertModel from 0x1F3B00),
    // the 0x90-byte image transfer packet of the alert (packet, data at 0x1AF370), four text boxes of the detail view (text[0],
    // text[2] and text[1] of 12 columns and text[3] of 5, the last with the text at 0x1B6550) and the detail model (detailModel
    // from 0x1F3B34).
    GuestPtr<StatusPanel> StatusPanel_ctor(GuestPtr<StatusPanel> panel)
    {
        const uint32_t p = panel.address;
        BaseObject_ctor(static_cast<int>(p));
        panel->model = globals::statusPanelModel.get();
        const GuestPtr<A3DObject> model = panel->model;
        model->matrix[3].x = std::bit_cast<float>(0xBF47AE14u); // -0.78
        model->matrix[3].y = std::bit_cast<float>(0xBF051EB8u); // -0.52
        model->matrix[3].z = std::bit_cast<float>(0xC02CCCCDu); // -2.7
        PacketBuf_Seek(static_cast<int>(StatusPanel_BuildPacket(panel, GuestPtr<uint32_t>{static_cast<uint32_t>(PacketBuf_Tell())}).address));
        panel->alertModel = globals::statusAlertModel.get();
        const uint32_t words[] = {0x10000006u, 0u, 0x11000000u, 0x50000006u,
                                  4u, 0x10000000u, 0xEu, 0u,
                                  0u, 0u, 0x51u, 0u,
                                  8u, 2u, 0x52u, 0u,
                                  0u, 0x13F7Bu, 0x50u, 0u,
                                  0u, 0u, 0x53u, 0u,
                                  0x8004u, 0x08000000u, 0u, 0u,
                                  0x30000004u, globals::kAlertColours, 0u, 0x50000004u,
                                  0x60000000u, 0u, 0u, 0u};
        for (uint32_t i = 0; i < 36u; ++i)
            port::store<uint32_t>(p + 4u * i, words[i]);
        const auto box = [&](uint32_t field, int columns, int rows, int x, int y) {
            port::store<uint32_t>(p + field, newTextBox(columns, rows, -2));
            const GuestPtr<TextBox> text{port::load<uint32_t>(p + field)};
            TextBox_SetPos(text, x, y, -0x100000);
            TextBox_SetCharSize(text, 0, 0, 0xB0, 0);
        };
        box(0xA0, 0xC, 8, 0x8140, 0x7CC0);
        box(0xA8, 0xC, 1, 0x8140, 0x8280);
        box(0xA4, 0xC, 1, 0x8140, 0x8350);
        box(0xAC, 5, 1, 0x8140, 0x8420);
        TextBox_SetText(panel->text[3], GuestPtr<char>{globals::kStatusTextStrength}, 0);
        panel->detailModel = globals::statusDetailModel.get();
        panel->detail = 0;
        panel->alert = 0;
        panel->infoFrames = 0;
        return panel;
    }

    PORT_FN(StatusPanel_ctor);

    // ---- Small window functions (plan P6 batch 2) ----

    // Sets the applicants window mode (cardShown); mode 1 builds the card text of the applicant under the cursor.
    void Applicants_SetMode(GuestPtr<Applicants> applicants, int mode)
    {
        applicants->cardShown = mode;
        if (mode == 1)
            Applicants_BuildInfo(applicants);
    }

    // Adds a line to the menu window's list: the list gets an item of 13 columns (14 when the window is open) by 1 row, style -2.
    void MenuWin_FillItems(GuestPtr<MenuWin> menuwin, GuestPtr<char> item)
    {
        const GuestPtr<ListMenu> list{menuwin->list.address};
        ListMenu_AddItems(list, menuwin->wideList == 0 ? 0xD : 0xE, 1, -1, -2, 0, -1);
        ListMenu_SetItemText(menuwin->list, item, -1);
    }

    // Once the opening (its Step slot, by 0.1) is done, the conversation goes to state 3 and its first line.
    void TalkConv_Opening(GuestPtr<TalkConv> conv)
    {
        if (port::callGuest<int64_t>(conv->base.base.vtable->step, conv, ps2f(0.1f)) != 0)
        {
            conv->base.base.state = 3;
            TalkConv_NextLine(conv);
        }
    }

    PORT_FN(Applicants_SetMode);
    PORT_FN(MenuWin_FillItems);
    PORT_FN(TalkConv_Opening);

    // The name entry window: its texts, then the board placed at restHeight and z -4.4 (the marker takes the same z) and the
    // place kept in boardPlace.
    GuestPtr<NameEntry> NameEntry_ctor(GuestPtr<NameEntry> entry)
    {
        entry->vtable = globals::kNameEntryVtable;
        NameEntry_CreateTexts(entry);
        entry->boardMatrix[3].y = entry->restHeight;
        entry->boardMatrix[3].z = std::bit_cast<float>(0xC08CCCCDu); // -4.4
        entry->markerMatrix[3].z = entry->boardMatrix[3].z;
        entry->boardPlace[0] = entry->boardMatrix[3].x;
        entry->boardPlace[1] = entry->boardMatrix[3].y;
        entry->boardPlace[2] = entry->boardMatrix[3].z;
        entry->state = 0;
        return entry;
    }

    // The applicants window: the base, its table (0x1BCF70), the pointer state from mode to cardPoint (the point (0, 2.3, 0, 1) in
    // cardPoint is what the cards are placed at), mode 0 and nothing selected.
    GuestPtr<Applicants> Applicants_ctor(GuestPtr<Applicants> applicants)
    {
        ApplicantsBase_ctor(applicants);
        applicants->vtable = globals::kApplicantsVtable;
        applicants->assignIndex = 0;
        applicants->assignTimer = 0;
        applicants->mode = 0;
        applicants->cardPoint.x = 0.0f;
        applicants->cardPoint.y = std::bit_cast<float>(0x40133333u); // 2.3
        applicants->cardPoint.z = 0.0f;
        applicants->cardPoint.w = 1.0f;
        Applicants_PlaceAt(applicants, GuestPtr<ps2f>::of(applicants->cardPoint.x));
        Applicants_SetMode(applicants, 0);
        Applicants_ScrollTo(applicants, -1);
        return applicants;
    }

    // Puts the cursor on applicant `index` (the top one before is kept in savedTop): the card model's matrix is the standard one moved
    // 3 and 2.5 units from the first model's place, the view scrolls to nothing, the card text is built, and the mode goes to 1.
    void Applicants_Select(GuestPtr<Applicants> applicants, int index)
    {
        applicants->sliding = 1;
        applicants->slideFrame = 0;
        applicants->savedTop = static_cast<int32_t>(static_cast<uint32_t>(applicants->top));
        applicants->top = index;
        applicants->cursor = index;
        Mat4_Copy(applicants->cardNodeA, GuestPtr<Vec4>{globals::kIdentityMatrix});
        const Vec4 &frame = applicants->frameMatrix[3];
        Vec4 &card = applicants->cardNodeA[3];
        card.x = (ps2f(frame.x) - ps2f(3.0f)).v;
        card.y = (ps2f(frame.y) - ps2f(2.5f)).v;
        card.z = frame.z;
        Applicants_ScrollTo(applicants, -1);
        Applicants_BuildInfo(applicants);
        applicants->cardShown = 1;
        Applicants_BuildInfo(applicants);
    }

    // Back to the pause menu's first page: the list cleared and its cursor put at (0x58, 0x31), the message text restored (0x1B73C0),
    // the Cross press consumed, the slot select state run once, the list scrolled to the item that was waiting (pendingChoice), and the
    // 16 slot marks (one byte in each of slotTexts) cleared.
    void PauseMenu_Reset(GuestPtr<PauseMenu> pause)
    {
        const auto list = [&] { return pause->slotMenu; };
        ListMenu_ClearItem(list(), -1);
        ListMenu_Place(list(), 0x58, 0x31, 0xFF0000);
        pause->itemsListed = 0;
        TextBox_SetText(pause->message, GuestPtr<char>{globals::kPauseEmptyText}, 0);
        Pad_SwallowCross();
        PauseMenu_StateSlotSelect(pause);
        const int32_t waiting = pause->pendingChoice;
        pause->pendingChoice = -1;
        if (waiting != -1)
            ListMenu_ScrollTo(list(), waiting);
        for (uint32_t i = 0; i < 0x10u; ++i)
            pause->slotTexts[i].text[0] = 0;
    }

    PORT_FN(NameEntry_ctor);
    PORT_FN(Applicants_ctor);
    PORT_FN(Applicants_Select);
    PORT_FN(PauseMenu_Reset);

    // The text of line `index` of the wants list into `out`: a want an event holds gets its line from that event's wants
    // script (mm_Words_LoadForWants); otherwise the want's family member and what is wanted (the claim line of the family's
    // home type for the want's claim slot) are written as "%s %s ..." from the format at 0x1B73B0.
    void WantsWin_FormatLine(GuestPtr<WantsWin> wants, GuestPtr<CityScene> city, int index, GuestPtr<char> out)
    {
        const GuestPtr<WantsIndex> entry = GuestPtr<WantsIndex>::of(wants->index[0]) + index;
        const GuestPtr<Want> want{(GuestPtr<uint32_t>::of(wants->data[0]) + entry->offset).address};
        if (entry->held != 0)
        {
            Words_LoadForWants(city, static_cast<int16_t>(entry->held), port::load<int32_t>(want.address), out); // its first word
            return;
        }
        const GuestPtr<Family> family = Family_Get(want->family, 0);
        const int claimLine = Claim_GetLine(globals::buildingType(family->homeType)->claimB, static_cast<int32_t>(want->claimSlot));
        const GuestPtr<FamilyMember> member = GuestPtr<FamilyMember>::of(family->members[0]) + want->member;
        sdk::sprintf(out.address, globals::kWantsTextLine, GuestPtr<char>::of(member->givenName[0]).address,
                     GuestPtr<char>::of(member->surname[0]).address, claimLine);
    }

    PORT_FN(WantsWin_FormatLine);

    // The title scene: its stage object (`afterGame` kept), the stage reset, the menu list of 3 by 48 items (loadMenu)
    // with sixteen items of 19 columns, the memory card manager (mc), the lights and the camera, set to the eye and target at
    // 0x1B17C0 and 0x1B17D0; the scene's state cleared (layers -1, loadStep 1), a message box of 21 by 12 (messageBox) and a
    // line box of 30 by 15 (hintText) with their places.
    GuestPtr<TitleScene> TitleScene_ctor(GuestPtr<TitleScene> scene, int afterGame)
    {
        struct Locals
        {
            uint8_t gap_00[0x30];
            Vec4 eye;                  // +0x30 the camera's eye
            Vec4 target;               // +0x40 and target
        };
        static_assert(offsetof(Locals, eye) == 0x30 && offsetof(Locals, target) == 0x40 && sizeof(Locals) == 0x50);
        port::GuestFrameOf<Locals> frame;
        const uint32_t s = scene.address;
        Stage_ctor(static_cast<int>(s));
        scene->afterGame = afterGame;
        Stage_Reset();
        uint32_t list = sdk::operator_new(0x2A0u);
        if (list != 0u)
            list = ListMenu_ctor(GuestPtr<ListMenu>{list}, 3, 0x30, 0x1C2, 0x1A4, 0x3C, 0xFF0000).address;
        scene->loadMenu = GuestPtr<ListMenu>{list};
        for (int i = 0; i < 0x10; ++i)
            ListMenu_AddItems(scene->loadMenu, 0x13, 3, 0, -1, 0, -1);
        uint32_t mc = sdk::operator_new(0x28u);
        if (mc != 0u)
            mc = McManager_ctor(GuestPtr<McManager>{mc}).address;
        scene->mc = GuestPtr<McManager>{mc};
        uint32_t lights = sdk::operator_new(0x1B0u);
        if (lights != 0u)
            lights = static_cast<uint32_t>(LightSet_Init(GuestPtr<LightSet>{lights}));
        scene->lights = GuestPtr<LightSet>{lights};
        uint32_t camera = sdk::operator_new(0x310u);
        if (camera != 0u)
            camera = Camera_ctor(GuestPtr<Camera>{camera}).address;
        scene->camera = GuestPtr<Camera>{camera};
        frame->eye = *GuestPtr<Vec4>{globals::kTitleEye};
        frame->target = *GuestPtr<Vec4>{globals::kTitleTarget};
        Camera_SetEyeTarget(scene->camera, frame.at(&Locals::eye), frame.at(&Locals::target));
        Camera_UpdateMatrices(scene->camera);
        scene->mode = 0;
        scene->pressStep = 0;
        scene->waitFrames = 0;
        scene->introFrame = 0;
        scene->pulseFrame = 0;
        scene->choice = 0;
        scene->choicePulsing = 0;
        scene->loadProgress = 0;
        globals::loadGameFromTitle.set(0);
        scene->layers = -1;
        scene->loadStep = 1;
        scene->messageBox = GuestPtr<TextBox>{newTextBox(0x15, 0xC, -1)};
        TextBox_SetPos(scene->messageBox, 0x72E0, 0x7C20, -0x100000);
        scene->logoSound = 0;
        scene->showHint = 0;
        scene->effect = 0;
        scene->song = 0;
        scene->pitch = static_cast<int16_t>(0);
        scene->note = 0x3C;
        scene->tempo = static_cast<int16_t>(0);
        scene->codeStep = 0;
        scene->hintText = GuestPtr<TextBox>{newTextBox(0x1E, 0xF, -1)};
        TextBox_SetPos(scene->hintText, 0x74C0, 0x7AE0, -0x100000);
        scene->unk_AE = 0x3C;
        scene->previewModel = GuestPtr<A3DObject>{0u};
        return scene;
    }

    PORT_FN(TitleScene_ctor);

    // The pause menu's card check state: the card manager is stepped; when the card in the slot (cardPort) reports 2 the menu shows
    // card message 5. Otherwise the main menu (+4) takes its input: choice 1 or the cancel (-1) gives the pad focus back, resets
    // the menu and goes to state 4 (the slot menu); any other choice resets the manager, clears the list and shows the message box
    // at its first place, and goes to state 6.
    void PauseMenu_StateWait(GuestPtr<PauseMenu> pause)
    {
        port::GuestFrame<0x30> frame;
        const uint32_t choiceAt = frame.ptr().address + 0x2Cu;
        const GuestPtr<McManager> mc{pause->mc.address};
        McManagerUpdate(mc);
        if (McManager_GetCardStatus(pause->mc, pause->cardPort) == 2)
        {
            PauseMenu_ShowCardMessage(pause, 5);
            return;
        }
        if (ListMenu_HandleInput(pause->mainMenu, GuestPtr<int32_t>{choiceAt}) == 0)
            return;
        const int32_t choice = port::load<int32_t>(choiceAt);
        const GuestPtr<TextBox> message{pause->message.address};
        if (choice == 1 || choice == -1)
        {
            Pad_TakeFocus(1, 0x10);
            PauseMenu_Reset(pause);
            Pad_ReleaseFocus(1);
            TextBox_SetText(message, GuestPtr<char>{globals::kPauseEmptyText}, 0);
            TextBox_SetPos(message, 0x7600, 0x7E00, -0x100000);
            pause->state = 4;
            return;
        }
        McManager_ResetState(pause->mc);
        pause->waitFrames = 0;
        pause->blink = 0;
        ListMenu_ClearItem(pause->mainMenu, -1);
        TextBox_SetPos(message, 0x7600, 0x7CC0, -0x100000);
        TextBox_SetCharSize(message, 0, 0xD0, 0, 0xE0);
        TextBox_SetText(message, GuestPtr<char>{globals::kPauseEmptyText}, 0);
        TextBox_SetPos(message, 0x7600, 0x7E00, -0x100000);
        pause->state = 6;
    }

    PORT_FN(PauseMenu_StateWait);

    // The Cross button on the thing under the cursor (target; 0xFFFF for nothing), when the city is in mode 1 and the Square-side
    // button 0x20 is not held. A cursor on a building (targetIsPerson 0) either picks the tile (the player is in mode 3) or
    // starts a talk about the object (mode view 1), or asks for mode 0xA with its marks reset; with a person view, unless the
    // player is in mode 3, a talk starts about the building at the person's tile (x and z on the 128-tile map, from the map's
    // person record).
    void CityCursor_Move(GuestPtr<CityCursor> cursor, GuestPtr<CityScene> city, int enabled)
    {
        if (cursor->target == 0xFFFFu || enabled == 0)
            return;
        if (CityScene_GetMode(city) != 1)
            return;
        if ((Pad_Pressed(0) & 0x40u) == 0)
            return;
        if ((Pad_Held(0) & 0x20u) != 0)
            return;
        const auto player = [&] { return city->player; };
        if (cursor->targetIsPerson == 0)
        {
            if (cursor->lineCount != 1)
            {
                CityScene_PushMode(city, 0xA);
                cursor->mode = 1;
                cursor->frameStep = 0;
                cursor->openTimer = 0;
            }
            else if (Player_GetMode(player()) == 3)
                CityCursor_SelectTile(cursor, cursor->target, city);
            else
                CityScene_StartTalk(city, 0, cursor->target, 0x1770, 0);
            return;
        }
        if (Player_GetMode(player()) == 3)
            return;
        const GuestPtr<CityMap> map{city->map.address};
        const uint32_t person = cursor->target;
        const uint32_t record = (map->people + static_cast<int32_t>(person)).address;
        const int x = sdk::fptosi(ps2f(64.0f) + ps2f(std::bit_cast<float>(port::load<uint32_t>(record))));
        const int z = sdk::fptosi(ps2f(64.0f) - ps2f(std::bit_cast<float>(port::load<uint32_t>(record + 8u))));
        const GuestPtr<MapObject> object = Map_FindPersonObjectOnTile(map, person, static_cast<uint16_t>(x + (z << 7)));
        CityScene_StartTalk(city, 0, object->id, cursor->target, 0);
    }

    PORT_FN(CityCursor_Move);

    // The pause menu's save state, one step a frame on its step counter (waitFrames): 0 wait for the card manager to settle, 1 make the
    // slot's save folder (mm_SaveFile_PrepareDir), 2 queue the delete of its icon.sys, 3 wait for that, 4 write the city
    // (mm_CityScene_SaveStep, which answers 2 to give up), 5 write icon.sys with the save name (saveTitle), 6 wait for that, 7 step
    // back out of the folder (the ".." at 0x1B7688), 8 show the main menu again and go to state 2. Giving up (a result of 2) puts the
    // counter back to 0 and returns 1. In the steps before 8 the message alternates between the saving text and the do-not-remove
    // text every 16 frames (the blink counter).
    int PauseMenu_StateSave(GuestPtr<PauseMenu> pause, GuestPtr<CityScene> city)
    {
        const auto mc = [pause]() { return pause->mc; };
        const auto nextStep = [pause]() { pause->waitFrames = pause->waitFrames + 1; };
        // Steps 1 and 7: the folder is ready (1, on to the next step) or the attempt failed (2, start over and answer 1).
        const auto folderStep = [&](int name, int create) {
            const int result = SaveFile_PrepareDir(mc(), pause->cardPort, name, create);
            if (result == 2)
            {
                pause->waitFrames = 0;
                return true;
            }
            if (result == 1)
                nextStep();
            return false;
        };
        switch (static_cast<uint32_t>(pause->waitFrames))
        {
        case 0:
            if (McManagerUpdate(mc()) < 2)
                nextStep();
            break;
        case 1:
            if (folderStep(pause->saveSlot, 1))
                return 1;
            break;
        case 2:
            SaveFile_DeleteIconSys(mc(), pause->cardPort);
            nextStep();
            break;
        case 3:
        case 6:
            if (SaveFile_PollDone(mc()) != 0)
                nextStep();
            break;
        case 4:
        {
            const int result = CityScene_SaveStep(city, mc());
            if (result == 1)
                nextStep();
            else if (result == 2)
            {
                pause->waitFrames = 0;
                return 1;
            }
            break;
        }
        case 5:
            SaveFile_WriteIconSys(mc(), pause->cardPort, static_cast<int>(GuestPtr<SaveTitle>::of(pause->saveTitle).address), 0);
            nextStep();
            break;
        case 7:
            if (folderStep(static_cast<int>(globals::kPauseTextDots), 0))
                return 1;
            break;
        case 8:
        {
            const GuestPtr<ListMenu> menu{pause->mainMenu.address};
            TextBox_SetText(pause->message, GuestPtr<char>{globals::kPauseEmptyText}, 0);
            ListMenu_ClearItem(menu, -1);
            ListMenu_Place(menu, 0x104, 0x3C, 0xFF0000);
            ListMenu_SetItemText(menu, GuestPtr<char>{globals::kPauseTextResume}, -1); // "Resume"
            ListMenu_SetItemText(menu, GuestPtr<char>{globals::kPauseTextSave}, -1);   // "Save"
            ListMenu_SetItemText(menu, GuestPtr<char>{globals::kPauseTextQuit}, -1);   // "Quit"
            McManager_ClearSlots(mc());
            pause->state = 2;
            break;
        }
        default:
            break;
        }
        if (pause->waitFrames == 8)
            return 0;
        const int32_t blink = pause->blink + 1;
        pause->blink = blink;
        if ((blink & 0xF) == 0)
            TextBox_SetText(pause->message,
                            GuestPtr<char>{(blink & 0x10) != 0 ? globals::kPauseTextSaving : globals::kPauseTextDoNotRemove}, 0);
        return 0;
    }

    PORT_FN(PauseMenu_StateSave);

    // The pause menu's format state, a step machine on its step counter (waitFrames): 0 the card check (the same as in the wait state: a
    // card that reports 2 shows message 5, choice 1 or the cancel hands the pad focus back and returns to the slot menu at state 4,
    // any other choice goes on), 1 look at the card's status (4 still being checked, so the "Now checking memory card" text; 3
    // goes on after resetting the save file; 2 shows message 5; anything else message 2), 2 wait for the card manager to settle,
    // 3 format the card (mm_SaveFile_Format: 2 failed and shows message 4; 1 done, back to the save state 6 with the manager
    // reset; 0 still running, and the "Formatting memory card" text alternates with the do-not-remove text every 16 frames on the
    // blink counter).
    void PauseMenu_StateFormat(GuestPtr<PauseMenu> pause)
    {
        port::GuestFrame<0x30> frame;
        const uint32_t choiceAt = frame.ptr().address + 0x2Cu;
        const auto mc = [pause]() { return pause->mc; };
        const auto menu = [pause]() { return pause->mainMenu; };
        const auto message = [pause]() { return pause->message; };
        const auto nextStep = [pause]() { pause->waitFrames = pause->waitFrames + 1; };
        switch (pause->waitFrames)
        {
        case 0:
        {
            McManagerUpdate(mc());
            if (McManager_GetCardStatus(mc(), pause->cardPort) == 2)
            {
                PauseMenu_ShowCardMessage(pause, 5);
                return;
            }
            if (ListMenu_HandleInput(menu(), GuestPtr<int32_t>{choiceAt}) == 0)
                return;
            const int32_t choice = port::load<int32_t>(choiceAt);
            if (choice == 1 || choice == -1)
            {
                Pad_TakeFocus(1, 0x10);
                PauseMenu_Reset(pause);
                Pad_ReleaseFocus(1);
                pause->state = 4;
                return;
            }
            nextStep();
            return;
        }
        case 1:
            McManagerUpdate(mc());
            switch (McManager_GetCardStatus(mc(), pause->cardPort))
            {
            case 4:
                ListMenu_ClearItem(menu(), -1);
                TextBox_SetPos(message(), 0x7600, 0x7E00, -0x100000);
                TextBox_SetCharSize(message(), 0, 0xD0, 0, 0xE0);
                TextBox_SetText(message(), GuestPtr<char>{globals::kPauseTextChecking}, 0);
                return;
            case 3:
                SaveFile_ResetState(mc());
                ListMenu_ClearItem(menu(), -1);
                nextStep();
                return;
            case 2:
                PauseMenu_ShowCardMessage(pause, 5);
                return;
            default:
                PauseMenu_ShowCardMessage(pause, 2);
                return;
            }
        case 2:
            if (McManagerUpdate(mc()) < 2)
                nextStep();
            return;
        case 3:
        {
            const int result = SaveFile_Format(mc(), pause->cardPort);
            if (result == 2)
            {
                PauseMenu_ShowCardMessage(pause, 4);
                return;
            }
            if (result == 1)
            {
                McManager_ResetState(mc());
                pause->waitFrames = 0;
                pause->blink = 0;
                ListMenu_ClearItem(menu(), -1);
                TextBox_SetPos(message(), 0x7600, 0x7CC0, -0x100000);
                TextBox_SetCharSize(message(), 0, 0xD0, 0, 0xE0);
                pause->state = 6;
                return;
            }
            if (result != 0)
                return;
            const int32_t blink = pause->blink + 1;
            pause->blink = blink;
            if ((blink & 0xF) != 0)
                return;
            TextBox_SetPos(message(), 0x7600, 0x7CC0, -0x100000);
            TextBox_SetText(message(),
                            GuestPtr<char>{(blink & 0x10) != 0 ? globals::kPauseTextFormatting : globals::kPauseTextDoNotRemove},
                            0);
            return;
        }
        default:
            return;
        }
    }

    PORT_FN(PauseMenu_StateFormat);

    // A page of the Town's Pages: all six texts are cleared, the entry count of the page reset and the two page numbers written
    // (2 * page + 1 and + 2). A page before the contents pages (mm_TownPages_FillContents) is the contents. Otherwise the type whose
    // listing holds the page is found in the index (the last type with families that starts at or before the page): its name
    // (cut at 18 bytes, or followed by a sign) goes in the heading, the INDEX label in the corner, and up to ten of its known
    // families are listed from the page's first one (ten a page): "name" and the phone number in the form 1234-5678, five in the
    // left text and five in the right.
    void TownPages_FillPage(GuestPtr<TownPages> pages)
    {
        port::GuestFrame<0xF0> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t text = sp + 0x70u, number = sp + 0xB0u, name = sp + 0xD0u;
        const auto clear = [&]() {
            for (int i = 0; i < 6; ++i)
                TextBox_SetText(pages->text[i], GuestPtr<char>{globals::kTownPagesEmptyText}, 0);
        };
        clear();
        pages->pageEntries = 0;
        Text_FormatPadded(GuestPtr<char>{text}, pages->page * 2 + 1, 4);
        TextBox_SetText(pages->text[3], GuestPtr<char>{text}, 0);
        Text_FormatPadded(GuestPtr<char>{text}, pages->page * 2 + 2, 4);
        TextBox_SetText(pages->text[4], GuestPtr<char>{text}, 0);
        if (pages->page < pages->contentsPages)
        {
            TownPages_FillContents(pages);
            return;
        }
        const int32_t types = globals::familyTypeCount.get();
        const auto entryAt = [&](int32_t type) { return pages->index + type; };
        int32_t type = 0;
        if (types > 0)
        {
            do
            {
                const GuestPtr<TownPagesIndex> entry = entryAt(type);
                if (entry->count != 0 && pages->page < static_cast<int32_t>(entry->startPage))
                    break;
                ++type;
            } while (type < types);
        }
        uint16_t before;
        do
        {
            before = entryAt(type - 1)->count; // the type before this one (index -1 reads before the table, as the original)
            --type;
        } while (before == 0);
        if (type == types)
        {
            clear();
            return;
        }
        sdk::sprintf(text, globals::kTownPagesMarkName,
                     GuestPtr<char>::of(globals::buildingType(static_cast<uint32_t>(type))->name[0]).address);
        if (sdk::strlen(text) >= 0x13)
            port::store<uint8_t>(text + 0x12u, 0xCD);
        else
            sdk::strcat(text, globals::kTownPagesMark);
        port::store<uint8_t>(text + 0x13u, 0);
        TextBox_SetText(pages->text[2], GuestPtr<char>{text}, 0);
        TextBox_SetText(pages->text[5], GuestPtr<char>{globals::kTownPagesIndex}, 0);
        const GuestPtr<TownPagesIndex> entry = entryAt(type);
        uint32_t family = entry->firstFamily;
        const int32_t skip = (pages->page - static_cast<int32_t>(entry->startPage)) * 10;
        int32_t counted = 0;
        if (skip > 0)
        {
            do
            {
                if (Family_IsKnown(family) != 0)
                    ++counted;
                ++family;
            } while (counted < skip);
        }
        int32_t rows = 0;
        while (counted < static_cast<int32_t>(entryAt(type)->count))
        {
            if (Family_IsKnown(family) != 0)
            {
                const GuestPtr<Family> record = Family_Get(static_cast<int>(family), 1);
                Text_FormatPadded(GuestPtr<char>{number}, Family_MakePhoneNumber(static_cast<int>(family)), 8);
                for (uint32_t k = 7; k >= 4; --k)
                    port::store<uint8_t>(number + k + 1u, port::load<uint8_t>(number + k));
                port::store<uint8_t>(number + 4u, '-');
                port::store<uint8_t>(number + 9u, 0);
                sdk::strcpy(name, record.address);
                if (sdk::strlen(record.address) >= 0x13)
                {
                    port::store<uint8_t>(name + 0x12u, 0xCD);
                    port::store<uint8_t>(name + 0x13u, 0);
                }
                sdk::sprintf(text, globals::kTownPagesEntry, name, number);
                TextBox_AppendText(pages->text[rows < 5 ? 0 : 1], GuestPtr<char>{text}, 0);
                ++rows;
                ++counted;
            }
            ++family;
            if (rows >= 10)
                break;
        }
        pages->pageEntries = rows;
    }

    PORT_FN(TownPages_FillPage);

    // One frame of the laptop (city mode 2). The entries sit on a carousel (count at 0x1BDAB8, `selected`, the turn moving
    // toward turnTarget); the states (state): 0 sets the carousel up (the entry angles, scale 0.1) and sound 10, taking the
    // applicants' flag away and the HUD off; 1 grows it (scale +0.1, lift -0.25) until the scale reaches 1, then makes the text box
    // (text) and shows the selected entry's text; 2 waits for Triangle (leave, sound 11, nothing run), Cross (leave and run the
    // entry) or left and right (the next entry the other way, sound 12, state 3); 3 turns the carousel by 0.1 a frame (direction at
    // turnDir) until it is within 0.2 of the wanted angle, snaps and shows the text; 4 shrinks it (scale -0.2, lift +0.2) until
    // the scale is below 0.1, then deletes the text box, gives the HUD and the applicants' flag back and runs the entry (mm_Laptop_RunEntry)
    // unless left with Triangle (cancelled). The result is the city mode wanted next: 1 leaves the mode (popped), 2 stays, any other
    // mode is pushed.
    int Laptop_Update(GuestPtr<Laptop> laptop, GuestPtr<CityScene> city)
    {
        const auto loadF = [](uint32_t at) { return ps2f(std::bit_cast<float>(port::load<uint32_t>(at))); };
        const auto storeF = [](uint32_t at, ps2f value) { port::store<uint32_t>(at, std::bit_cast<uint32_t>(value.v)); };
        // The angle of an entry: a full turn over the entries, folded into -pi..pi.
        const auto angleOf = [&](int32_t index) {
            ps2f angle = (ps2f(6.2831855f) / fromInt(globals::laptopEntryCount.get())) * fromInt(index);
            if (!(angle < ps2f(3.1415927f)))
                angle = (angle - ps2f(3.1415927f)) - ps2f(3.1415927f);
            return angle;
        };
        const auto box = [&]() { return laptop->text; };
        const auto showSelected = [&]() {
            Laptop_SetItemText(laptop, city, port::load<uint32_t>(globals::kLaptopEntries + 4u * static_cast<uint32_t>(laptop->selected)));
        };
        int result = CityScene_GetMode(city);
        switch (laptop->state)
        {
        case 0:
        {
            laptop->radius.x = 0.0f;
            laptop->radius.y = 0.0f;
            laptop->radius.z = 0.0f;
            laptop->radius.w = std::bit_cast<float>(0x3F800000u);
            laptop->centre.x = std::bit_cast<float>(0xC019999Au);
            laptop->centre.y = std::bit_cast<float>(0xBF51EB85u);
            laptop->centre.z = std::bit_cast<float>(0x40E28F5Cu);
            laptop->centre.w = 0.0f;
            laptop->scale = std::bit_cast<float>(0x3DCCCCCDu);
            const ps2f angle = angleOf(laptop->selected);
            laptop->turnTarget = angle;
            laptop->turn = angle;
            laptop->state = 1;
            Sound_PlaySe(0xA, 0, 0x3C, 0.0f, 0.0f);
            city->hudVisible[1] = 0;
            city->hudVisible[2] = 0;
            city->applicants->enabled = 0;
            break;
        }
        case 1:
            laptop->scale = laptop->scale + ps2f(0.1f);
            laptop->radius.z = laptop->radius.z - ps2f(0.25f);
            if (laptop->scale < ps2f(1.0f))
                break;
            laptop->scale = ps2f(1.0f);
            laptop->state = 2;
            laptop->radius.z = std::bit_cast<float>(0xC0000000u);
            laptop->text = GuestPtr<TextBox>{newTextBox(0xB, 5, -1)};
            TextBox_SetPos(box(), 0x6E80, 0x81C0, 100000);
            showSelected();
            break;
        case 2:
            if ((Pad_Pressed(0) & 0x10u) != 0)
            {
                TextBox_SetText(box(), GuestPtr<char>{globals::kLaptopEmptyText}, 0);
                laptop->state = 4;
                Sound_PlaySe(0xB, 0, 0x3C, 0.0f, 0.0f);
                laptop->cancelled = 1;
            }
            else if ((Pad_Pressed(0) & 0x40u) != 0)
            {
                TextBox_SetText(box(), GuestPtr<char>{globals::kLaptopEmptyText}, 0);
                laptop->state = 4;
                laptop->cancelled = 0;
            }
            else if ((Pad_GetRepeat() & 0x8000) != 0)
            {
                TextBox_SetText(box(), GuestPtr<char>{globals::kLaptopEmptyText}, 0);
                laptop->selected = laptop->selected - 1;
                if (laptop->selected < 0)
                    laptop->selected = globals::laptopEntryCount.get() - 1;
                laptop->turnTarget = angleOf(laptop->selected);
                laptop->turnDir = 0;
                Sound_PlaySe(0xC, 0, 0x3C, 0.0f, 0.0f);
                laptop->state = 3;
            }
            else if ((Pad_GetRepeat() & 0x2000) != 0)
            {
                TextBox_SetText(box(), GuestPtr<char>{globals::kLaptopEmptyText}, 0);
                laptop->selected = laptop->selected + 1;
                if (!(laptop->selected < globals::laptopEntryCount.get()))
                    laptop->selected = 0;
                laptop->turnTarget = angleOf(laptop->selected);
                laptop->turnDir = 1;
                Sound_PlaySe(0xC, 0, 0x3C, 0.0f, 0.0f);
                laptop->state = 3;
            }
            break;
        case 3:
        {
            const int32_t direction = laptop->turnDir;
            if (direction == 0)
            {
                const ps2f turn = laptop->turn - ps2f(0.1f);
                laptop->turn = turn;
                if (turn < ps2f(-3.1415927f))
                    laptop->turn = turn + ps2f(6.2831855f);
            }
            else if (direction == 1)
            {
                const ps2f turn = laptop->turn + ps2f(0.1f);
                laptop->turn = turn;
                if (!(turn < ps2f(3.1415927f)))
                    laptop->turn = (turn - ps2f(3.1415927f)) - ps2f(3.1415927f);
            }
            else
                break;
            const ps2f target = laptop->turnTarget, current = laptop->turn;
            if (current < target - ps2f(0.2f) || !(current <= ps2f(0.2f) + target))
                break;
            laptop->turn = target;
            laptop->state = 2;
            showSelected();
            break;
        }
        case 4:
        {
            laptop->scale = laptop->scale - ps2f(0.2f);
            laptop->radius.z = laptop->radius.z + ps2f(0.2f);
            if (!(laptop->scale < ps2f(0.1f)))
                break;
            laptop->scale = ps2f(0.1f);
            laptop->state = 0;
            laptop->radius.z = 0.0f;
            if (laptop->text.address != 0u)
            {
                TextBox_dtor(box(), 1);
                laptop->text = GuestPtr<TextBox>{0u};
            }
            result = 1;
            city->hudVisible[1] = 1;
            city->hudVisible[2] = 1;
            city->applicants->enabled = 1;
            if (laptop->cancelled == 0)
                result = Laptop_RunEntry(laptop, city);
            break;
        }
        default:
            break;
        }
        Laptop_Animate(laptop);
        if (result == 1)
            CityScene_PopMode(city);
        else if (result != 2)
            CityScene_PushMode(city, result);
        return result;
    }

    PORT_FN(Laptop_Update);

    // The info window of the city cursor: the view for this frame's parity (0x1BDCD0 & 1) has its five line frames
    // reset to style 0 and the lines refilled from what the cursor points at (target, 0xFFFF for nothing; a building
    // when targetIsPerson is 0, otherwise a person). A building lists the families of all the objects on its tile,
    // starting with the pointed one (the chain of the objects' nextOnTile, ended by 0x3F80): the family name (a family
    // not in the cache gets the text at 0x1B5ED8) and a frame style 9 plus the best friendship band of its members. A
    // person gives one line, "name" over "phone" from the formats at 0x1B5EE0, with the frame style of its band. The
    // window's own frame is style 0x32 with no line, 0x35 when the player is in mode 3, else 0x34. The anchor is the
    // pointed place (a building's centre, a person's feet) with its height from the terrain, 0.6 above it for anything
    // pointed at (heights grow downwards), and the window is laid out next to its projection
    // (mm_CityCursor_LayoutInfo).
    void CityCursor_UpdateInfo(GuestPtr<CityCursor> cursor, GuestPtr<CityScene> city)
    {
        port::GuestFrame<0xF0> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t point = sp + 0xC0u, text = sp + 0xD0u;
        const uint32_t matrix = city->cityCamera.field(&CityCamera::base).field(&Camera::worldScreenA).address;
        const GuestPtr<Vec4> place{point};
        *place = cursor->targetPos;
        ps2f lift(0.0f);
        // The view of this frame's parity; its lines are indexed by guest address, any count, as the original does.
        const GuestPtr<CityCursorView> view = GuestPtr<CityCursorView>::of(cursor->views[globals::frameParity.get() & 1u]);
        const auto lineBox = [&](int32_t index) { return (GuestPtr<GuestPtr<TextBox>>::of(view->lines[0]) + index)[0]; };
        const auto lineFrame = [&](int32_t index) { return (GuestPtr<GuestPtr<Frame>>::of(view->lineFrames[0]) + index)[0]; };
        const auto count = [&]() { return cursor->lineCount; };
        for (int i = 0; i < 5; ++i)
            Frame_SetStyle(lineFrame(i), 0);
        cursor->lineCount = 0;
        const uint32_t target = cursor->target;
        if (target != 0xFFFFu)
        {
            if (cursor->targetIsPerson == 0)
            {
                const GuestPtr<MapObject> object = Map_GetRecord(city->map, static_cast<int>(target));
                const ps2f half = fromInt(globals::buildingType(object->type)->size) / ps2f(2.0f);
                place->x = (fromInt(object->tile & 0x7F) - ps2f(64.0f)) + half;
                place->z = (fromInt(0x80 - (object->tile >> 7)) - ps2f(64.0f)) - half;
                lift = ps2f(0.6f);
                uint32_t next = GuestPtr<uint16_t>::of(cursor->target).address; // then each object's next on the tile
                while (port::load<uint16_t>(next) != 0x3F80)
                {
                    const GuestPtr<MapObject> here = Map_GetRecord(city->map, static_cast<int>(port::load<uint16_t>(next)));
                    const uint32_t record = static_cast<uint32_t>(Family_Prefetch(here->family, 1));
                    if (record == 0u)
                        TextBox_SetText(lineBox(count()), GuestPtr<char>{globals::kCursorEmptyText}, 1);
                    else
                    {
                        TextBox_SetText(lineBox(count()), GuestPtr<char>{record}, 1);
                        const uint32_t family = here->family;
                        int band = 0;
                        const uint32_t members = static_cast<uint32_t>(Family_Prefetch(static_cast<int>(family), 1));
                        if (members != 0u)
                            for (int member = 0; member < GuestPtr<Family>{members}->memberCount; ++member)
                            {
                                const int found = Family_GetFriendshipBand(static_cast<int>(family), member);
                                if (band < found)
                                    band = found;
                            }
                        if (band != 0)
                            Frame_SetStyle(lineFrame(count()), band + 9);
                    }
                    cursor->lineCount = count() + 1;
                    next = here.field(&MapObject::nextOnTile).address;
                }
            }
            else
            {
                lift = ps2f(0.6f);
                const GuestPtr<MapPerson> who = city->map->people + static_cast<int>(target);
                const uint32_t record = static_cast<uint32_t>(Family_Prefetch(who->family, 1));
                if (record == 0u)
                    TextBox_SetText(lineBox(0), GuestPtr<char>{globals::kCursorEmptyText}, 1);
                else
                {
                    place->x = who->x;
                    place->z = who->z;
                    const uint32_t member = record + 0x48u * who->member;
                    sdk::sprintf(text, globals::kCursorTextName, member + 0x5Cu, member + 0x4Cu);
                    TextBox_SetText(lineBox(count()), GuestPtr<char>{text}, 1);
                    const int band = Family_GetFriendshipBand(who->family, who->member);
                    if (band != 0)
                        Frame_SetStyle(lineFrame(count()), band + 9);
                    cursor->lineCount = count() + 1;
                }
            }
        }
        const GuestPtr<Frame> windowFrame = view->frame;
        if (count() == 0)
            Frame_SetStyle(windowFrame, 0x32);
        else if (Player_GetMode(city->player) == 3)
            Frame_SetStyle(windowFrame, 0x35);
        else
            Frame_SetStyle(windowFrame, 0x34);
        place->y = Map_SampleHeight(city->map, place->x, place->z) - lift;
        place->w = 1.0f;
        CityCursor_LayoutInfo(cursor, GuestPtr<Vec4>{point}, GuestPtr<Vec4>{matrix});
    }

    PORT_FN(CityCursor_UpdateInfo);

    // The title scene's steps around the opening movie (the step, pressStep; the title is drawn with all five layers first on every
    // call): 0 fades voice 15 out; 1 (once the draw reports 1) stops sound sequence 7; 2 and 3 wait for the sound driver (2 also
    // sends command 2); 4 plays the opening movie; 5 (the disc ready, 2) resets the voices; 6 waits for the driver; 7 (the disc
    // ready) loads the title sound bank into slot 7; 8 returns 1 once the driver is idle. 0 until then.
    int Title_OpeningMovieStep(GuestPtr<TitleScene> scene)
    {
        const int drawn = Title_DrawCommon(scene, 0x1F);
        const auto next = [&]() { scene->pressStep = scene->pressStep + 1; };
        switch (static_cast<uint32_t>(scene->pressStep))
        {
        case 0:
            SoundCmd_MidiFade(0xF, 0, 0x10, 0);
            next();
            return 0;
        case 1:
            if (drawn != 1)
                return 0;
            SoundCmd_MidiClose(7, 0);
            next();
            return 0;
        case 2:
            if (Sound_IsBusy() != 0)
                return 0;
            SoundCmd_WaveCloseBankSet();
            next();
            return 0;
        case 3:
        case 6:
            if (Sound_IsBusy() != 0)
                return 0;
            next();
            return 0;
        case 4:
            PlayOpeningMovie();
            next();
            return 0;
        case 5:
            if (sdk::sceCdDiskReady(0) != 2)
                return 0;
            Sound_ResetVoices(1);
            next();
            return 0;
        case 7:
            if (sdk::sceCdDiskReady(0) != 2)
                return 0;
            SoundCmd_MidiOpen(7, static_cast<int>(globals::kTitleBankSq), static_cast<int>(globals::kTitleBankHd),
                              static_cast<int>(globals::kTitleBankBd), 0);
            next();
            return 0;
        case 8:
            return Sound_IsBusy() != 0 ? 0 : 1;
        default:
            return 0;
        }
    }

    PORT_FN(Title_OpeningMovieStep);

    // The title scene's start of a game (the step, pressStep; every call also steps the city's asset loader, whose result is kept
    // in loadStep and the progress in loadProgress, copied from assetLoadCount). `loaded` is 1 when the load menu chose a save
    // (title mode 0xA), 0 for a new game (mode 9). 0 fades voice 15 out and draws the title (layer set 0x29 when `loaded` is set,
    // else 0x21); 1 (once the draw reports 1) puts the camera at kNewGameEye and kNewGameTarget; 2 shows the loading bar (the
    // progress in 17ths of 10, at most 16 bars) and the intro text while the loading screen is up, and goes on when the loader is
    // done (loadStep 0); 3 fades the loading screen out; 4 and 5 stop the title music and wait for the sound driver; 6 sets the
    // game up: the first time since start-up (afterGame clear) the character types, families, claims, words, goods, stamina, bag
    // and event types are loaded, after a game (afterGame set) the stage, families, stamina and bag are reset, then the laptop
    // entries are made; the result is 1 for a new game (loadGameFromTitle = 0) or 2 for a loaded one (loadGameFromTitle = 1). 0
    // until then.
    int TitleScene_StartGame(GuestPtr<TitleScene> scene, int loaded)
    {
        struct Locals
        {
            uint8_t gap_00[0x30];
            Vec4 eye;                  // +0x30 the camera's eye
            Vec4 target;               // +0x40 and target
        };
        static_assert(offsetof(Locals, eye) == 0x30 && offsetof(Locals, target) == 0x40 && sizeof(Locals) == 0x50);
        port::GuestFrameOf<Locals> frame;
        const uint32_t s = scene.address;
        scene->loadProgress = globals::assetLoadCount.get();
        scene->loadStep = CityAssets_LoadStep();
        const auto next = [&]() { scene->pressStep = scene->pressStep + 1; };
        const auto draw = [&]() { return Title_DrawCommon(scene, loaded != 0 ? 0x29 : 0x21); };
        switch (static_cast<uint32_t>(scene->pressStep))
        {
        case 0:
            SoundCmd_MidiFade(0xF, 0, 0x10, 0);
            draw();
            next();
            return 0;
        case 1:
        {
            if (draw() != 1)
                return 0;
            next();
            frame->eye = *GuestPtr<Vec4>{globals::kNewGameEye};
            frame->target = *GuestPtr<Vec4>{globals::kNewGameTarget};
            const GuestPtr<Camera> camera{scene->camera.address};
            Camera_SetEyeTarget(camera, frame.at(&Locals::eye), frame.at(&Locals::target));
            Camera_UpdateMatrices(camera);
            return 0;
        }
        case 2:
            if (LoadingScreen_Update(scene->loading, 0) == 1)
            {
                int bars = (10 * scene->loadProgress) / 70;
                if (bars > 16)
                    bars = 16;
                TexSprite_Draw(scene->loadingFrame);
                if (bars != 0)
                {
                    const GuestPtr<TexSprite> bar{scene->loadingBar.address};
                    TexSprite_SetFrame(bar, 0, 0);
                    TexSprite_SetClip(bar, static_cast<uint32_t>(bars), 1);
                    TexSprite_Draw(bar);
                }
                Title_NewGameIntro(scene);
            }
            if (scene->loadStep == 0)
                next();
            return 0;
        case 3:
            if (LoadingScreen_Update(scene->loading, 1) == 0)
                next();
            return 0;
        case 4:
            SoundCmd_MidiClose(7, 0);
            SoundCmd_WaveCloseBankSet();
            next();
            return 0;
        case 5:
            if (Sound_IsBusy() == 0)
                next();
            return 0;
        case 6:
            sdk::sceGsSyncPath(0, 0);
            if (scene->afterGame == 0)
            {
                CharType_LoadAll();
                Family_Init();
                Claim_Load();
                Words_Init();
                Goods_Load();
                Stamina_CreateGlobal();
                Bag_CreateGlobal();
                Event_LoadTypes();
            }
            else
            {
                Stage_Reset();
                Family_ResetState();
                Stamina_Init(GuestPtr<Stamina>{globals::staminaObject.get()});
                Bag_Clear(GuestPtr<Bag>{globals::bagObject.get()});
            }
            Laptop_InitEntries();
            if (loaded == 0)
            {
                globals::loadGameFromTitle.set(0);
                return 1;
            }
            globals::loadGameFromTitle.set(1);
            return 2;
        default:
            return 0;
        }
    }

    PORT_FN(TitleScene_StartGame);

    // One frame of the title scene (by its mode): clears the screen and Z, draws the scene's packets and camera, steps the
    // memory card manager and then runs the mode. 0 loads the scene's assets (TitleScene_Load), then on to 1 (or 5 when afterGame is set);
    // 1 is the memory card check; 2 to 4 the three logos; 5 the title intro (Cross, 0x800, skips into the title at once); 6 the
    // title screen, whose answer is 0 new game (9), 1 load (8), 2 the opening movie (7, after the music fades out) or 10 the sound
    // test (0xFF, which goes back to 5 when done); 8 the load menu (1 starts a loaded game: 0xA, 0 goes back: 6); 9 and 0xA start
    // a new or loaded game (TitleScene_StartGame: its 1 is mode 0xB, its 2 mode 0xC); 0xB and 0xC answer 1 or 2 once the card is idle.
    // The asset loader is stepped in modes 1 to 6 and 8. The result is 0 until a game starts.
    int TitleScene_Update(GuestPtr<TitleScene> scene)
    {
        const uint32_t s = scene.address;
        DmaList_QueueClearScreen();
        DmaList_QueueClearZ();
        LightSet_SelectProgram(scene->lights, 0);
        Packet_Draw(GuestPtr<uint32_t>{scene->camera.address});
        const int card = McManagerUpdate(scene->mc);
        const uint32_t mode = static_cast<uint32_t>(scene->mode);
        const auto setMode = [&](uint32_t value) { scene->mode = static_cast<int32_t>(value); };
        const auto next = [&]() { setMode(static_cast<uint32_t>(scene->mode) + 1u); };
        const auto stepLoader = [&]() {
            scene->loadProgress = globals::assetLoadCount.get();
            scene->loadStep = CityAssets_LoadStep();
        };
        switch (mode)
        {
        case 0xFF:
            if (SoundTest_Update(scene) != 0)
            {
                setMode(5);
                scene->pressStep = static_cast<int32_t>(0);
            }
            return 0;
        case 0xC:
        case 0xB:
            if (card >= 2)
                return 0;
            return mode == 0xB ? 1 : 2;
        case 0xA:
        case 9:
        {
            const int result = TitleScene_StartGame(scene, mode == 0xA ? 1 : 0);
            if (result == 1)
                setMode(0xB);
            else if (result == 2)
                setMode(0xC);
            return 0;
        }
        case 8:
        {
            stepLoader();
            const int result = Title_LoadMenu(scene);
            if (result == 1)
            {
                setMode(0xA);
                scene->pressStep = static_cast<int32_t>(0);
            }
            else if (result == 0)
            {
                setMode(6);
                scene->pressStep = static_cast<int32_t>(0);
            }
            return 0;
        }
        case 7:
            if (Title_OpeningMovieStep(scene) != 0)
            {
                setMode(5);
                scene->pressStep = static_cast<int32_t>(0);
                scene->logoSound = static_cast<int32_t>(0);
            }
            return 0;
        case 6:
        {
            stepLoader();
            const int result = Title_PressStart(scene);
            if (result == 0xA)
            {
                scene->testPage = static_cast<int32_t>(0x64);
                setMode(0xFF);
            }
            else if (result == 2)
            {
                SoundCmd_WaveCloseBankSet();
                SoundCmd_MidiFade(0xF, 0, 0x10, 0);
                setMode(7);
                scene->pressStep = static_cast<int32_t>(0);
            }
            else if (result == 1)
            {
                setMode(8);
                scene->pressStep = static_cast<int32_t>(0);
            }
            else if (result == 0)
            {
                setMode(9);
                scene->pressStep = static_cast<int32_t>(0);
            }
            return 0;
        }
        case 5:
            stepLoader();
            if (Title_Intro(scene) != 0)
            {
                scene->choicePulsing = static_cast<int32_t>(0);
                scene->pressStep = static_cast<int32_t>(0);
                next();
            }
            if (Pad_Pressed(0) == 0x800u)
            {
                TexSprite_SetAlpha(scene->titleSprite, 0x80, 0);
                scene->choicePulsing = static_cast<int32_t>(0);
                scene->pressStep = static_cast<int32_t>(0);
                next();
            }
            return 0;
        case 4:
        case 3:
        case 2:
            stepLoader();
            if (TitleScene_LogoStep(scene, static_cast<char>(mode - 2u)) != 0)
            {
                scene->pressStep = static_cast<int32_t>(0);
                next();
            }
            return 0;
        case 1:
            stepLoader();
            if (TitleScene_MemoryCardCheck(scene, card) != 0)
            {
                scene->pressStep = static_cast<int32_t>(0);
                scene->waitFrames = static_cast<int32_t>(0);
                next();
            }
            return 0;
        case 0:
            if (TitleScene_Load(scene, 0) == 0)
                return 0;
            scene->pressStep = static_cast<int32_t>(0);
            scene->waitFrames = static_cast<int32_t>(0);
            if (scene->afterGame != 0)
                setMode(5);
            else
                next();
            return 0;
        default:
            return 0;
        }
    }

    PORT_FN(TitleScene_Update);

    // The pause menu's save slot state (4): the two cards' states are read; while either is still being checked the "checking memory
    // card" text shows and nothing is listed. Otherwise the slot menu (slotMenu) lists the saves on the cards (their texts copied to
    // slotTexts, 0x92 bytes each; the count goes to savesListed, the newest save's index is the result) and, for a card that is unformatted
    // or has room (states 0 and 3 and a new slot to make), a "new save" line each; items left from a longer list are emptied. With
    // nothing listed, card message 0 (both cards in state 2), 6 (the second), 7 (the first) or 8 shows. The menu's choice: the
    // cancel (-1) goes back to the main menu (state 2); a new slot makes the save slot (saveSlot) and, if the card is usable, asks to
    // confirm (state 5), else resets the manager (state 6); an existing save is looked up (saveSlot, the card in cardPort) and shown with
    // the overwrite question (state 7, the choice kept in pendingChoice).
    int PauseMenu_StateSlotSelect(GuestPtr<PauseMenu> pause)
    {
        port::GuestFrame<0x110> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t cursorAt = sp + 0x108u, newestAt = sp + 0x10Cu, textAt = sp + 0x70u;
        const uint32_t p = pause.address;
        const auto mc = [pause]() { return pause->mc; };
        const auto slots = [pause]() { return pause->slotMenu; };
        const auto main = [pause]() { return pause->mainMenu; };
        const auto message = [pause]() { return pause->message; };
        port::store<int32_t>(cursorAt, 0);
        int newest = 0, count = 0;
        const int first = McManager_GetCardStatus(mc(), 0);
        const int second = McManager_GetCardStatus(mc(), 1);
        if (first == 4 || second == 4)
        {
            TextBox_SetPos(message(), 0x7600, 0x7E00, -0x100000);
            TextBox_SetCharSize(message(), 0, 0xD0, 0, 0xE0);
            TextBox_SetText(message(), GuestPtr<char>{globals::kPauseTextChecking}, 0);
            ListMenu_ClearItem(slots(), -1);
            return 0;
        }
        TextBox_SetText(message(), GuestPtr<char>{globals::kPauseEmptyText}, 0);
        for (uint32_t name = p; McManager_FormatSlotText(mc(), GuestPtr<int32_t>{cursorAt}, GuestPtr<char>{textAt}, GuestPtr<uint32_t>{newestAt}) != 0;
             name += 0x92u, ++count)
        {
            if (port::load<int32_t>(newestAt) != 0)
                newest = count;
            ListMenu_SetItemText(slots(), GuestPtr<char>{textAt}, count);
            sdk::strcpy(name + 0x78u, textAt);
        }
        pause->savesListed = count;
        if ((first == 0 || first == 3) && McManager_PrepareNewSlot(mc(), 0) != 0)
            ListMenu_SetItemText(slots(), GuestPtr<char>{globals::kPauseTextNewSave}, count++);
        if ((second == 0 || second == 3) && McManager_PrepareNewSlot(mc(), 1) != 0)
            ListMenu_SetItemText(slots(), GuestPtr<char>{globals::kPauseTextNewSaveAgain}, count++);
        if (pause->itemsListed != 0)
            for (int stale = pause->itemsListed - 1; !(stale < count); --stale)
                ListMenu_ClearItem(slots(), stale);
        pause->itemsListed = count;
        if (pause->itemsListed == 0)
        {
            int number;
            if (first == 2 && second == 2)
                number = 0;
            else if (second == 2)
                number = 6;
            else if (first == 2)
                number = 7;
            else
                number = 8;
            PauseMenu_ShowCardMessage(pause, number);
            return newest;
        }
        if (ListMenu_HandleInput(slots(), GuestPtr<int32_t>{cursorAt}) == 0)
            return newest;
        const int32_t choice = port::load<int32_t>(cursorAt);
        if (choice == -1)
        {
            ListMenu_ClearItem(main(), -1);
            ListMenu_Place(main(), 0x104, 0x3C, 0xFF0000);
            ListMenu_SetItemText(main(), GuestPtr<char>{globals::kPauseTextResume}, -1);
            ListMenu_SetItemText(main(), GuestPtr<char>{globals::kPauseTextSave}, -1);
            ListMenu_SetItemText(main(), GuestPtr<char>{globals::kPauseTextQuit}, -1);
            pause->state = 2;
            return newest;
        }
        const int32_t listed = pause->savesListed;
        if (!(choice < listed))
        {
            pause->cardPort = choice - listed;
            if (first != 0 && first != 3)
                pause->cardPort = 1;
            pause->saveSlot = McManager_PrepareNewSlot(mc(), pause->cardPort);
            if (McManager_IsCardUsable(mc(), pause->cardPort) != 0)
            {
                ListMenu_ClearItem(main(), -1);
                ListMenu_Place(main(), 0x118, 0x8C, 0xFF0000);
                ListMenu_SetItemText(main(), GuestPtr<char>{globals::kPauseTextYes}, -1);
                ListMenu_SetItemText(main(), GuestPtr<char>{globals::kPauseTextNo}, -1);
                pause->waitFrames = 0;
                TextBox_SetText(message(), GuestPtr<char>{globals::kPauseTextSlotNeeds}, 0);
                TextBox_AppendText(message(),
                                   GuestPtr<char>{pause->cardPort != 0 ? globals::kPauseTextDigit2 : globals::kPauseTextDigit1}, 0);
                TextBox_AppendText(message(), GuestPtr<char>{globals::kPauseTextNeedsFormat}, 0);
                TextBox_SetPos(message(), 0x72E0, 0x7CC0, -0x100000);
                TextBox_SetCharSize(message(), 0, 0xD0, 0, 0xE0);
                pause->blink = 0;
                pause->state = 5;
            }
            else
            {
                McManager_ResetState(mc());
                pause->waitFrames = 0;
                pause->blink = 0;
                ListMenu_ClearItem(main(), -1);
                TextBox_SetPos(message(), 0x7600, 0x7CC0, -0x100000);
                TextBox_SetCharSize(message(), 0, 0xD0, 0, 0xE0);
                pause->state = 6;
            }
        }
        else
        {
            pause->saveSlot = McManager_GetSaveFolder(mc(), choice, pause.field(&PauseMenu::cardPort));
            TextBox_SetPos(message(), 0x71C0, 0x7C20, -0x100000);
            TextBox_SetText(message(), GuestPtr<char>{(GuestPtr<PauseSlotText>::of(pause->slotTexts[0]) + choice).address}, 0);
            TextBox_AppendText(message(), GuestPtr<char>{globals::kPauseTextOverwrite}, 0);
            ListMenu_ClearItem(main(), -1);
            ListMenu_ScrollTo(main(), 0);
            ListMenu_Place(main(), 0x118, 0x8C, 0xFF0000);
            ListMenu_SetItemText(main(), GuestPtr<char>{globals::kPauseTextYes}, -1);
            ListMenu_SetItemText(main(), GuestPtr<char>{globals::kPauseTextNo}, -1);
            pause->pendingChoice = choice;
            pause->state = 7;
        }
        return newest;
    }

    PORT_FN(PauseMenu_StateSlotSelect);

    // One frame of the pause menu (by its state): 0 makes its parts (the main list of three items, mainMenu, the slot list of sixteen
    // (slotMenu), the memory card manager (mc), the message box (message) and the save header (saveTitle)), hides the city's HUD and
    // goes to 1; 1 slides in (openTime up by 10 to 120) and fills the main list (state 2); 2 takes the main list's choice (0 or the
    // cancel closes with 9 and the sound 8, 1 goes to the card check 3, 2 shows the settings question: state 8); 3 shows the "checking
    // memory card" text and, with the manager idle, goes to 4 (the slot menu) with the choice reset (pendingChoice -1); 4 to 7 and 10 are
    // the slot, format, save, wait and main states; 8 closes the pause (state 0xB) on its first choice or goes back to 2; 9 slides
    // out (openTime down by 10) and, with the manager idle, frees the parts, shows the HUD again and pops the city mode; 11 counts
    // the city's fadeLevel up by 2 to 128 and then does the same, but leaves the state at 11 (the result 1). 0 otherwise.
    int PauseMenu_Update(GuestPtr<PauseMenu> pause, GuestPtr<CityScene> city)
    {
        struct Locals
        {
            uint8_t gap_00[0x48];
            int32_t mainChoice;        // +0x48 the main list's choice (state 2)
            int32_t closeChoice;       // +0x4C the closing question's (state 8)
        };
        static_assert(offsetof(Locals, mainChoice) == 0x48 && offsetof(Locals, closeChoice) == 0x4C && sizeof(Locals) == 0x50);
        port::GuestFrameOf<Locals> frame;
        const auto mc = [pause]() { return pause->mc; };
        const auto main = [pause]() { return pause->mainMenu; };
        const auto slots = [pause]() { return pause->slotMenu; };
        const auto message = [pause]() { return pause->message; };
        const auto applicantsEnabled = [city](uint8_t value) { city->applicants->enabled = value; };
        const auto mainItems = [&]() {
            ListMenu_ClearItem(main(), -1);
            ListMenu_Place(main(), 0x104, 0x3C, 0xFF0000);
            ListMenu_SetItemText(main(), GuestPtr<char>{globals::kPauseTextResume}, -1);
            ListMenu_SetItemText(main(), GuestPtr<char>{globals::kPauseTextSave}, -1);
            ListMenu_SetItemText(main(), GuestPtr<char>{globals::kPauseTextQuit}, -1);
        };
        // Frees the four parts, shows the HUD again and pops the city's mode (state 9 also puts the pause state back to 0; state 11
        // leaves it at 0xB).
        const auto close = [&](bool resetState) {
            if (main())
            {
                ListMenu_dtor(main(), 1);
                pause->mainMenu = GuestPtr<ListMenu>{0u};
            }
            if (slots())
            {
                ListMenu_dtor(slots(), 1);
                pause->slotMenu = GuestPtr<ListMenu>{0u};
            }
            if (mc())
            {
                McManager_dtor(mc(), 1);
                pause->mc = GuestPtr<McManager>{0u};
            }
            if (message())
            {
                TextBox_dtor(message(), 1);
                pause->message = GuestPtr<TextBox>{0u};
            }
            city->hudVisible[1] = 1;
            city->hudVisible[2] = 1;
            applicantsEnabled(1);
            if (resetState)
                pause->state = 0;
            CityScene_PopMode(city);
        };
        switch (static_cast<uint32_t>(pause->state))
        {
        case 0:
        {
            uint32_t list = sdk::operator_new(0x2A0u);
            if (list != 0u)
                list = ListMenu_ctor(GuestPtr<ListMenu>{list}, 3, 0x10, 0xFA, 0xF0, 0x3C, 0xFF0000).address;
            pause->mainMenu = GuestPtr<ListMenu>{list};
            for (int i = 0; i < 3; ++i)
                ListMenu_AddItems(main(), 0xA, 1, 1, -1, 0, -1);
            list = sdk::operator_new(0x2A0u);
            if (list != 0u)
                list = ListMenu_ctor(GuestPtr<ListMenu>{list}, 3, 0x2C, 0x21C, 0x1A4, 0x3C, 0xFF0000).address;
            pause->slotMenu = GuestPtr<ListMenu>{list};
            for (int i = 0; i < 0x10; ++i)
                ListMenu_AddItems(slots(), 0x13, 3, 0, -1, 0, -1);
            uint32_t manager = sdk::operator_new(0x28u);
            if (manager != 0u)
                manager = McManager_ctor(GuestPtr<McManager>{manager}).address;
            pause->mc = GuestPtr<McManager>{manager};
            McManager_ClearSlots(mc());
            pause->message = GuestPtr<TextBox>{newTextBox(0x14, 5, -1)};
            city->hudVisible[1] = 0;
            city->hudVisible[2] = 0;
            applicantsEnabled(0);
            McManager_BuildSaveTitle(mc(), city, GuestPtr<SaveTitle>::of(pause->saveTitle));
            pause->state = 1;
            pause->openTime = 0.0f;
            Sound_PlaySe(7, 0, 0x3C, 0.0f, 0.0f);
            return 0;
        }
        case 1:
        {
            McManagerUpdate(mc());
            const ps2f time = ps2f(static_cast<float>(pause->openTime)) + ps2f(10.0f);
            pause->openTime = static_cast<float>(time);
            if (time < ps2f(120.0f))
                return 0;
            mainItems();
            applicantsEnabled(0);
            pause->state = 2;
            return 0;
        }
        case 2:
        {
            McManagerUpdate(mc());
            if (ListMenu_HandleInput(main(), frame.at(&Locals::mainChoice)) == 0)
                return 0;
            const int32_t choice = frame->mainChoice;
            if (choice == 2)
            {
                TextBox_SetPos(message(), 0x7240, 0x7CC0, -0x100000);
                TextBox_SetCharSize(message(), 0, 0xD0, 0, 0xE0);
                TextBox_SetText(message(), GuestPtr<char>{globals::kPauseTextQuitSure}, 0);
                ListMenu_ClearItem(main(), -1);
                ListMenu_Place(main(), 0x118, 0x50, 0xFF0000);
                ListMenu_SetItemText(main(), GuestPtr<char>{globals::kPauseTextYes}, -1);
                ListMenu_SetItemText(main(), GuestPtr<char>{globals::kPauseTextNo}, -1);
                ListMenu_ScrollTo(main(), 1);
                pause->state = 8;
            }
            else if (choice == 1)
                pause->state = 3;
            else if (choice == 0 || choice == -1)
            {
                ListMenu_ClearItem(main(), -1);
                pause->state = 9;
                pause->openTime = 120.0f;
                Sound_PlaySe(8, 0, 0x3C, 0.0f, 0.0f);
            }
            return 0;
        }
        case 3:
            ListMenu_ClearItem(main(), -1);
            TextBox_SetText(message(), GuestPtr<char>{globals::kPauseTextChecking}, 0);
            TextBox_SetPos(message(), 0x7600, 0x7E00, -0x100000);
            TextBox_SetCharSize(message(), 0, 0xD0, 0, 0xE0);
            if (McManagerUpdate(mc()) == 0)
            {
                pause->state = 4;
                PauseMenu_Reset(pause);
            }
            pause->pendingChoice = -1;
            return 0;
        case 4:
            McManagerUpdate(mc());
            PauseMenu_StateSlotSelect(pause);
            return 0;
        case 5:
            PauseMenu_StateFormat(pause);
            return 0;
        case 6:
            if (PauseMenu_StateSave(pause, city) != 0)
                PauseMenu_ShowCardMessage(pause, 3);
            return 0;
        case 7:
            PauseMenu_StateWait(pause);
            return 0;
        case 8:
        {
            if (ListMenu_HandleInput(main(), frame.at(&Locals::closeChoice)) != 0)
            {
                if (frame->closeChoice == 0)
                {
                    city->fadeOverAll = 1;
                    pause->state = 0xB;
                    Bgm_FadeOut(0x20);
                    TextBox_SetText(message(), GuestPtr<char>{globals::kPauseEmptyText}, 0);
                    ListMenu_ClearItem(main(), -1);
                }
                else
                {
                    ListMenu_ScrollTo(main(), 0);
                    TextBox_SetText(message(), GuestPtr<char>{globals::kPauseEmptyText}, 0);
                    mainItems();
                    pause->state = 2;
                }
            }
            McManagerUpdate(mc());
            return 0;
        }
        case 9:
        {
            const ps2f time = ps2f(static_cast<float>(pause->openTime)) - ps2f(10.0f);
            pause->openTime = static_cast<float>(time);
            const int busy = McManagerUpdate(mc());
            if (!(time <= ps2f(0.0f)))
                return 0;
            if (busy == 2)
            {
                pause->openTime = 0.0f;
                return 0;
            }
            close(true);
            return 0;
        }
        case 10:
            PauseMenu_StateMain(pause);
            return 0;
        case 11:
        {
            const int busy = McManagerUpdate(mc());
            city->fadeLevel = city->fadeLevel + 2;
            bool done = false;
            if (!(city->fadeLevel < 0x81))
            {
                city->fadeLevel = 0x80;
                done = busy < 2;
            }
            if (!done)
                return 0;
            city->fadeLevel = 0;
            city->fadeOverAll = 0;
            close(false);
            return 1;
        }
        default:
            return 0;
        }
    }

    PORT_FN(PauseMenu_Update);

    // The title's load game screen (mode 8) for one frame: the title sprite and the menu model are drawn, then by the
    // step (pressStep): 0 empties the slot list (loadMenu), puts its cursor at (0x62, 0x2C) and goes on to 1. While
    // either memory card is still being checked (status 4) the "checking" text shows, the list is emptied and the
    // answer is -1. Otherwise the saves on the cards are listed (one line each, from McManager_FormatSlotText; the
    // newest save's line is scrolled to once, in step 1, which goes on to 2), lines left from a longer list are emptied
    // and the count is kept in savesListed. With saves the "choose" text shows and the list takes input: Triangle
    // answers 0 (back), Cross copies the chosen save's 0x40-byte header to 0x1C5640 and its description
    // (McManager_FormatSaveInfo) to 0x1C5680 and answers 1. With no save the "no data" text shows for 300 frames
    // (introFrame, which the cursor restarts) and the answer is 0. -1 otherwise.
    int Title_LoadMenu(GuestPtr<TitleScene> scene)
    {
        port::GuestFrame<0x100> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t cursorAt = sp + 0xF8u, newestAt = sp + 0xFCu, textAt = sp + 0x60u;
        const uint32_t s = scene.address;
        const auto menu = [scene]() { return scene->loadMenu; };
        const auto mc = [scene]() { return scene->mc; };
        const auto message = [scene]() { return scene->messageBox; };
        TexSprite_Draw(scene->titleSprite);
        A3DObject_Draw(scene->menuModel);
        const int32_t step = scene->pressStep;
        if (step == 0)
        {
            scene->savesListed = 0;
            ListMenu_ClearItem(menu(), -1);
            ListMenu_Place(menu(), 0x62, 0x2C, 0xFF0000);
            scene->pressStep = scene->pressStep + 1;
        }
        else if (step != 1 && step != 2)
            return -1;
        port::store<int32_t>(cursorAt, 0);
        int count = 0, newest = 0;
        const int first = McManager_GetCardStatus(mc(), 0);
        const int second = McManager_GetCardStatus(mc(), 1);
        if (first == 4 || second == 4)
        {
            TextBox_SetPos(message(), 0x7600, 0x7F40, -0x100000);
            TextBox_SetCharSize(message(), 0, 0xD0, 0, 0xE0);
            TextBox_SetText(message(), GuestPtr<char>{globals::kLoadMenuTextChecking}, 0);
            TextBox_Draw(message());
            ListMenu_ClearItem(menu(), -1);
            return -1;
        }
        while (McManager_FormatSlotText(mc(), GuestPtr<int32_t>{cursorAt}, GuestPtr<char>{textAt}, GuestPtr<uint32_t>{newestAt}) != 0)
        {
            if (port::load<int32_t>(newestAt) != 0)
                newest = count;
            ListMenu_SetItemText(menu(), GuestPtr<char>{textAt}, count);
            ++count;
        }
        if (scene->pressStep == 1)
        {
            ListMenu_ScrollTo(menu(), newest);
            scene->pressStep = scene->pressStep + 1;
        }
        const int32_t listed = scene->savesListed;
        if (listed != 0)
            for (int stale = listed - 1; !(stale < count); --stale)
                ListMenu_ClearItem(menu(), stale);
        scene->savesListed = count;
        if (count == 0)
        {
            if (scene->introFrame == 0)
                ListMenu_ClearItem(menu(), -1);
            TextBox_SetPos(message(), 0x7560, 0x7F40, -0x100000);
            TextBox_SetCharSize(message(), 0, 0xD0, 0, 0xE0);
            TextBox_SetText(message(), GuestPtr<char>{globals::kLoadMenuTextNoSave}, 0);
            TextBox_Draw(message());
            const int32_t frames = scene->introFrame + 1;
            scene->introFrame = frames;
            return frames < 0x12D ? -1 : 0;
        }
        TextBox_SetText(message(), GuestPtr<char>{globals::kEmptyText}, 0);
        ListMenu_Draw(menu());
        scene->introFrame = 0;
        if (ListMenu_HandleInput(menu(), GuestPtr<int32_t>{cursorAt}) == 0)
            return -1;
        const int32_t choice = port::load<int32_t>(cursorAt);
        if (choice == -1)
            return 0;
        const GuestPtr<CardDirEntry> entry = McManager_GetSaveEntry(mc(), choice, GuestPtr<int32_t>{globals::loadGameCard.address});
        if (entry.address == 0u)
            return -1;
        *globals::loadedSaveEntry() = *entry;
        McManager_FormatSaveInfo(mc(), choice, GuestPtr<char>{globals::kLoadedSaveInfo});
        return 1;
    }

    PORT_FN(Title_LoadMenu);

    // The stage briefing scene for one frame (nothing once fadeMode is set). The step (phase): 0 makes the message window (window) and
    // keeps the shared building model (0x1F3B2C) in office and the model at 0x1F3C68 in boss, with the fade at 128; 1 fades in (4 a
    // frame); 2 sets the window's first message (sound 0x3C); 3 and 8 wait for the window's slots 0x28 and 0x24 to say they are done;
    // 4 builds the next briefing text (StageScene_BuildBriefingText): with text it goes into the window (MsgWin_NextLine) and on to 5,
    // without the scene goes to 7; 5 waits out the typing (window state 6); 6 waits for Cross alone (sound 1, back to 4); 7 calls slot
    // 0x10 once; 9 fades out (4 a frame) and, at 128, deletes the window and starts over with the stage count (step) one up, drawing
    // nothing more this frame. Every other frame draws: the window (slot 0x14), the light set and camera, the building model with its
    // matrix moved to (0, -0.16, -3.84) and node 0x12, and the boss model put in place (translation row 0, 0.4, -3.65, 1) and
    // played (animTime grows by 2 a frame and wraps at the animation's length, where the next animation, if any (nextAnimation),
    // takes over),
    // then the window's slot 0x20 and the fade.
    void StageScene_MissionBriefing(GuestPtr<StageScene> scene)
    {
        struct Locals
        {
            uint8_t gap_00[0x40];
            Vec4 colour;               // +0x40 the boss's colour (that of the colour at 0x1B26C0)
            Vec4 matrix[4];            // +0x50 the boss's placement
        };
        static_assert(offsetof(Locals, colour) == 0x40 && offsetof(Locals, matrix) == 0x50 && sizeof(Locals) == 0x90);
        port::GuestFrameOf<Locals> frame;
        if (scene->fadeMode != 0)
            return;
        const auto window = [scene]() { return scene->window.address; };
        const auto vtable = [scene]() { return scene->window->base.vtable; };
        const auto nextStep = [scene]() { scene->phase = scene->phase + 1; };
        const uint32_t step = static_cast<uint32_t>(scene->phase);
        bool draw = true;
        if (step < 10u)
        {
            switch (step)
            {
            case 0:
            {
                scene->office = globals::talkSceneModel.get();
                uint32_t win = sdk::operator_new(0x2CCu);
                if (win != 0u)
                    win = MsgWin_ctor(GuestPtr<MsgWin>{win}).address;
                scene->window = GuestPtr<MsgWin>{win};
                scene->boss = globals::bossModel.get();
                scene->animTime = 0.0f;
                scene->animation = 7;
                scene->nextAnimation = -1;
                scene->fade = 0x80;
                scene->briefingIndex = 0;
                scene->briefingPhase = 0;
                nextStep();
                break;
            }
            case 1:
                scene->fade = scene->fade - 4;
                if (!(scene->fade > 0))
                    nextStep();
                break;
            case 2:
                MsgWin_SetMessage(GuestPtr<MsgWin>{window()}, 1, 0x3C);
                nextStep();
                break;
            case 3:
                if (port::callGuest<int64_t>(vtable()->isActive, window()) != 0)
                    nextStep();
                break;
            case 4:
                if (StageScene_BuildBriefingText(scene) != 0)
                {
                    MsgWin_NextLine(GuestPtr<MsgWin>{window()}, GuestPtr<char>{port::guest(scene->text)});
                    nextStep();
                }
                else
                    scene->phase = 7;
                break;
            case 5:
                if (port::load<int32_t>(window()) != 6)
                    scene->phase = static_cast<int32_t>(step) + 1;
                break;
            case 6:
                if (Pad_Pressed(0) == 0x40u)
                {
                    Sound_PlaySe(1, 0, 0x3C, 0.0f, 0.0f);
                    scene->phase = scene->phase - 2;
                }
                break;
            case 7:
                port::callGuest<int64_t>(vtable()->close, window());
                nextStep();
                break;
            case 8:
                if (port::callGuest<int64_t>(vtable()->isIdle, window()) != 0)
                    nextStep();
                break;
            case 9:
            {
                scene->fade = scene->fade + 4;
                if (scene->fade < 0x80)
                    break;
                const uint32_t win = window();
                if (win != 0u)
                    port::callGuest<void>(GuestPtr<MsgWin>{win}->base.vtable->dtor, win, 1);
                scene->window = GuestPtr<MsgWin>{0u};
                scene->phase = 0;
                scene->step = static_cast<uint8_t>(scene->step + 1);
                draw = false;
                break;
            }
            }
        }
        if (!draw)
            return;
        port::callGuest<int64_t>(vtable()->update, window());
        DmaList_QueueGsDefaults();
        LightSet_SelectProgram(scene->lights, 0);
        Packet_Draw(GuestPtr<uint32_t>{scene->camera.address});
        DmaList_QueueClearZ();
        const GuestPtr<A3DObject> building{scene->office.address};
        const GuestPtr<Vec4> matrix = worldMatrix(modelNode(building, 0x12));
        Mat4_Copy(matrix, GuestPtr<Vec4>{globals::kIdentityMatrix});
        matrix[3].x = 0.0f;
        matrix[3].y = std::bit_cast<float>(0xBE23D70Au); // -0.16
        matrix[3].z = std::bit_cast<float>(0xC075C28Fu); // -3.84
        A3DObject_ChangeLuminosity(building, GuestPtr<Vec4>{globals::kDefaultColour}, 0);
        A3DObject_DrawNode(building, 0x12);
        frame->colour = *GuestPtr<Vec4>{globals::kDefaultColour};
        const GuestPtr<A3DObject> actor{scene->boss.address};
        const ps2f length = A3D_GetAnimLength(actor, scene->animation);
        Mat4_Copy(frame.at(&Locals::matrix), GuestPtr<Vec4>{globals::kIdentityMatrix});
        frame->matrix[3] = Vec4{0.0f, std::bit_cast<float>(0x3ECCCCCDu), std::bit_cast<float>(0xC069999Au), 1.0f}; // 0.4, -3.65
        for (uint32_t row = 0; row < 4u; ++row)
            actor->matrix[row] = frame->matrix[row];
        A3D_AnimateIfRoom(actor, scene->animation, ps2f(static_cast<float>(scene->animTime)));
        A3DObject_UpdateMatrices(actor);
        Instance_DrawModelEx(actor, frame.at(&Locals::colour), 0, 0, 0, 0, 0, 4);
        const ps2f time = ps2f(static_cast<float>(scene->animTime)) + ps2f(2.0f);
        scene->animTime = static_cast<float>(time);
        if (!(time <= length))
        {
            scene->animTime = 0.0f;
            const int32_t following = scene->nextAnimation;
            if (!(following < 0))
            {
                scene->animation = following;
                scene->nextAnimation = -1;
            }
        }
        port::callGuest<int64_t>(vtable()->draw, window(), 2);
        DmaList_QueueClearZ();
        DmaList_QueueFade(scene->fade, 0);
    }

    PORT_FN(StageScene_MissionBriefing);

    // The stage briefing scene for one frame. What happens is looked up by the stage (7 at most) and the phase (step, a byte) in the
    // table at 0x1B07C0 (eight bytes a stage): 0 answers 1 (the scene is over) unless fadeMode is set; 1 starts the sound change
    // (fadeMode 2, fadeFrame 0) and the next phase; 2 plays the stage's mission movie and goes on when it has played; 3 the telop, 4 the briefing,
    // 10 the approval rate; 5 starts the sound change the other way (fadeMode 1); 6 and 7 go to the next phase; 8 stops the sounds and goes
    // on; 9 plays the movie and, when it has played, puts up the full-screen picture (picture, a sprite of 0x1B7350); 11 and 12 show the
    // stage title or the city name (or, with fadeMode clear, go on and delete the caption box); other codes do nothing. Then the
    // sound change runs: with fadeMode 2 the sequences are stopped (fadeFrame 0 to 1), then the scene waits for the sounds to stop (back to
    // 0); with fadeMode 1 it waits for the disc, resets the voices and loads the playland banks (fadeFrame 0 to 1), then waits for the sounds
    // (back to 0). The result is 0 while the scene goes on.
    int StageScene_Update(GuestPtr<StageScene> scene)
    {
        DmaList_QueueClearScreen();
        const int32_t stage = scene->stage;
        const uint32_t row = static_cast<uint32_t>(stage < 8 ? stage : 7);
        const uint32_t action = port::load<uint8_t>(globals::kStageSceneActions + (row << 3) + scene->step);
        const auto nextPhase = [scene]() { scene->step = static_cast<uint8_t>(scene->step + 1); };
        // The caption box (caption) is deleted once the scene moves on.
        const auto goOn = [&]() {
            scene->phase = 0;
            nextPhase();
            const uint32_t text = scene->caption.address;
            if (text != 0u)
            {
                TextBox_dtor(GuestPtr<TextBox>{text}, 1);
                scene->caption = GuestPtr<TextBox>{0u};
            }
        };
        switch (action)
        {
        case 0:
            if (scene->fadeMode == 0)
                return 1;
            break;
        case 1:
        case 5:
            scene->fadeMode = action == 1 ? 2 : 1;
            scene->fadeFrame = 0;
            nextPhase();
            break;
        case 2:
            if (scene->fadeMode == 0 && PlayMissionMovie(static_cast<char>(stage & 0xFF)) != 0)
            {
                DmaList_Flip();
                scene->phase = 0;
                nextPhase();
            }
            break;
        case 3:
            StageScene_ShowTelop(scene);
            break;
        case 4:
            StageScene_MissionBriefing(scene);
            break;
        case 6:
        case 7:
            nextPhase();
            break;
        case 8:
            Sound_StopAllPlaying();
            scene->phase = 0;
            nextPhase();
            break;
        case 9:
        {
            bool played = false;
            if (scene->fadeMode == 0 && PlayMissionMovie(static_cast<char>(stage & 0xFF)) != 0)
            {
                DmaList_Flip();
                scene->phase = 0;
                nextPhase();
                played = true;
            }
            if (played && scene->picture.address == 0u)
            {
                uint32_t sprite = sdk::operator_new(0x38u);
                if (sprite != 0u)
                    sprite = static_cast<uint32_t>(TexSprite_Load(GuestPtr<TexSprite>{sprite},
                                                                  static_cast<int>(globals::kPathEndRollTex), 0));
                scene->picture = GuestPtr<TexSprite>{sprite};
                TexSprite_SetPosScale(scene->picture, 0, 0, 0x1000, ps2f(1.0f), ps2f(1.0f));
                TexSprite_Draw(scene->picture);
            }
            break;
        }
        case 10:
            StageScene_ShowApprovalRate(scene);
            break;
        case 11:
            if (scene->fadeMode == 0)
                goOn();
            else
                StageScene_ShowStageTitle(scene);
            break;
        case 12:
            if (scene->fadeMode == 0)
                goOn();
            else
                StageScene_ShowCityName(scene);
            break;
        default:
            break;
        }
        if (scene->fadeMode == 2)
        {
            const int32_t phase = scene->fadeFrame;
            if (phase == 1)
            {
                if (Sound_IsBusy() == 0)
                {
                    scene->fadeFrame = 0;
                    scene->fadeMode = 0;
                }
            }
            else if (phase == 0)
            {
                Sound_StopAllSequences();
                SoundCmd_WaveCloseBankSet();
                scene->fadeFrame = scene->fadeFrame + 1;
            }
        }
        if (scene->fadeMode != 1)
            return 0;
        const int32_t phase = scene->fadeFrame;
        if (phase == 1)
        {
            if (Sound_IsBusy() == 0)
            {
                scene->fadeMode = 0;
                scene->fadeFrame = 0;
            }
        }
        else if (phase == 0 && sdk::sceCdDiskReady(0) == 2)
        {
            Sound_ResetVoices(0);
            Sound_LoadPlaylandBanks();
            scene->fadeFrame = scene->fadeFrame + 1;
        }
        return 0;
    }

    PORT_FN(StageScene_Update);

    // The status panel for one frame. The clock hand matrices of the panel model (model, at 0xD0 and 0x190 in its node table) turn
    // with the hour (pi per 6, kept between -6 and 5) and the seconds into the hour (pi per 1800, kept between -1800 and 1799), the
    // first one lowered by 0.01. The digit glyph quads in the panel packet (panelPacket) are set from the glyph tables at 0x1AF610 (two floats
    // a digit, 10 blank): the month (the city's monthCount mod 12, plus 1), the year (the count over 12, plus 1, with the frame words
    // beside it moved 0xC0 outward or inward by the digits), the four digit population (the map's population), and the signed three digit
    // approval (the colour word by its size, 50 and 100 the limits, and a minus sign when negative). The alert (alert) is the city
    // flag 4; the detail view (detail) is on in the city mode 2. With the detail view on, the timer (infoFrames, 960 long) shows the stage
    // target text for 480 frames and then the goal's remainder and the mission progress text, and the two money lines go into the other
    // text boxes. The marks shown (markCount) are the count of the city's stamina object times 20 over two million.
    void StatusPanel_Update(GuestPtr<StatusPanel> panel, GuestPtr<CityScene> city)
    {
        struct Locals
        {
            uint8_t gap_00[0x60];
            uint8_t digits[0x10];      // +0x60 a number's digits (mm_Text_SplitDigits)
            char text[0x100];          // +0x70 the line made for the detail text boxes
            char money[0x20];          // +0x170 an amount (mm_Text_FormatMoney)
        };
        static_assert(offsetof(Locals, digits) == 0x60 && offsetof(Locals, text) == 0x70 && offsetof(Locals, money) == 0x170 &&
                      sizeof(Locals) == 0x190);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<uint8_t> digits = frame.at(&Locals::digits);
        const uint32_t p = panel.address, c = city.address;
        const auto word = [](uint32_t at) { return port::load<uint32_t>(at); };
        panel->detail = CityScene_GetMode(city) == 2 ? 1 : 0;
        panel->alert = (city->flags & 4u) != 0u ? 1 : 0;
        const int32_t seconds = city->clock;
        int32_t hour = (seconds / 3600) % 12;
        int32_t minuteSeconds = seconds % 3600;
        if (!(hour + 6 < 12))
            hour -= 12;
        if (!(minuteSeconds + 0x708 < 0xE10))
            minuteSeconds -= 0xE10;
        const GuestPtr<A3DNode> hourHand = modelNode(panel->model, 1), minuteHand = modelNode(panel->model, 2);
        const ps2f pi = ps2f(std::bit_cast<float>(0x40490FDBu));
        sdk::sceVu0RotMatrixZ(GuestPtr<Vec4>::of(hourHand->local[0]).address, globals::kIdentityMatrix,
                              (pi * ps2f(static_cast<float>(hour))) / ps2f(6.0f));
        sdk::sceVu0RotMatrixZ(GuestPtr<Vec4>::of(minuteHand->local[0]).address, globals::kIdentityMatrix,
                              (pi * ps2f(static_cast<float>(minuteSeconds))) / ps2f(std::bit_cast<float>(0x44E10000u)));
        hourHand->local[3].z = std::bit_cast<float>(0xBC23D70Au); // -0.01, over the minute hand
        const uint32_t quads = panel->panelPacket;
        // The two glyph floats of `digit` go to the quad words at `at` and at + 0x20.
        const auto glyph = [&](uint32_t at, int digit) {
            port::store<uint32_t>(quads + at, word(globals::kStatusPanelGlyphs + 4u * static_cast<uint32_t>(digit)));
            port::store<uint32_t>(quads + at + 0x20u, word(globals::kStatusPanelGlyphs + 4u + 4u * static_cast<uint32_t>(digit)));
        };
        const auto blank = [&](uint32_t at) {
            port::store<uint32_t>(quads + at, word(globals::kStatusPanelGlyphs + 0x28u));
            port::store<uint32_t>(quads + at + 0x20u, word(globals::kStatusPanelGlyphs + 0x2Cu));
        };
        // Two table words as one place word: the first plus `offset`, the second in the high half.
        const auto place = [&](uint32_t at, uint32_t low, uint32_t high, uint32_t offset) {
            port::store<uint32_t>(quads + at, (word(low) + offset) | (word(high) << 16));
        };
        const auto digitAt = [&](uint32_t index) { return static_cast<int>(static_cast<int8_t>(frame->digits[index])); };
        const int32_t month = city->monthCount % 12 + 1;
        Text_SplitDigits(digits, month, 2);
        if (!(month < 10))
        {
            glyph(0xD0u, digitAt(0));
            glyph(0x110u, digitAt(1));
        }
        else
        {
            glyph(0xD0u, digitAt(1));
            blank(0x110u);
        }
        const int32_t year = city->monthCount / 12 + 1;
        Text_SplitDigits(digits, year, 2);
        if (!(year < 10))
        {
            glyph(0x1D0u, digitAt(0));
            glyph(0x210u, digitAt(1));
            place(0x260u, globals::kStatusPanelSprites + 0x110u, globals::kStatusPanelSprites + 0x114u, 0xC0u);
            place(0x280u, globals::kStatusPanelSprites + 0x118u, globals::kStatusPanelSprites + 0x11Cu, 0xC0u);
        }
        else
        {
            glyph(0x1D0u, digitAt(1));
            blank(0x210u);
            place(0x260u, globals::kStatusPanelSprites + 0x110u, globals::kStatusPanelSprites + 0x114u, 0u - 0xC0u);
            place(0x280u, globals::kStatusPanelSprites + 0x118u, globals::kStatusPanelSprites + 0x11Cu, 0u - 0xC0u);
        }
        if ((city->flags & 2u) == 0u)
        {
            port::store<uint32_t>(quads + 0x250u, 0x3F100000u);
            port::store<uint32_t>(quads + 0x270u, 0x3F280000u);
        }
        else
        {
            port::store<uint32_t>(quads + 0x250u, 0x3F280000u);
            port::store<uint32_t>(quads + 0x270u, 0x3F400000u);
        }
        Text_SplitDigits(digits, city->map->population, 4);
        glyph(0x2D0u, digitAt(0));
        glyph(0x310u, digitAt(1));
        glyph(0x350u, digitAt(2));
        glyph(0x390u, digitAt(3));
        if (digitAt(0) == 10)
        {
            place(0x2A0u, globals::kStatusPanelSprites + 0x150u, globals::kStatusPanelSprites + 0x154u, 0u);
            place(0x2C0u, globals::kStatusPanelSprites + 0x158u, globals::kStatusPanelSprites + 0x15Cu, 0u);
        }
        else
        {
            place(0x2A0u, globals::kStatusPanelSprites + 0x130u, globals::kStatusPanelSprites + 0x134u, 0u);
            place(0x2C0u, globals::kStatusPanelSprites + 0x138u, globals::kStatusPanelSprites + 0x13Cu, 0u);
        }
        const int32_t balance = city->approval;
        Text_SplitDigits(digits, std::abs(balance), 3);
        if (balance < 0)
        {
            port::store<uint32_t>(quads + 0x410u, 0x80B48C00u);
            port::store<uint32_t>(quads + 0x420u, 0x3F700000u);
            port::store<uint32_t>(quads + 0x440u, 0x3F800000u);
            frame->digits[0] = 0;
        }
        else
        {
            port::store<uint32_t>(quads + 0x410u, balance < 0x32 ? 0x80808058u : (balance < 0x64 ? 0x80808080u : 0x808080C0u));
            glyph(0x420u, digitAt(0));
        }
        glyph(0x460u, digitAt(1));
        glyph(0x4A0u, digitAt(2));
        if (panel->detail != 0)
        {
            panel->infoFrames = panel->infoFrames + 1;
            if (!(static_cast<uint32_t>(panel->infoFrames) < 0x3C0u))
                panel->infoFrames = 0;
            const uint32_t text = frame.at(&Locals::text).address, money = frame.at(&Locals::money).address;
            if (static_cast<uint32_t>(panel->infoFrames) < 0x1E0u)
                Stage_FormatTargetText(static_cast<int>(c), GuestPtr<char>{text});
            else
            {
                sdk::strcpy(text, globals::kStatusTextHeading);
                const int32_t goal = globals::stageState()->quota;
                const int32_t count = city->map->population;
                if (count < goal)
                    sdk::sprintf(text + static_cast<uint32_t>(sdk::strlen(text)), globals::kStatusTextPeopleNeeded, goal - count);
                else
                    sdk::strcat(text, globals::kStatusTextReached);
                const int32_t level = globals::stageState()->missionKind;
                if (level != 0)
                {
                    const uint32_t end = static_cast<uint32_t>(sdk::strlen(text));
                    const int progress = CityScene_MissionProgress(city);
                    sdk::sprintf(text + end, globals::kStatusTextThemeShare, progress,
                                 port::load<uint32_t>(globals::kThemeNames + 4u * static_cast<uint32_t>(level)));
                }
            }
            TextBox_SetText(panel->text[0], GuestPtr<char>{text}, 0);
            Text_FormatMoney(GuestPtr<char>{money}, globals::stageState()->salary * 2, 0x12, 0);
            sdk::sprintf(text, globals::kStatusTextSalary, money);
            TextBox_SetText(panel->text[2], GuestPtr<char>{text}, 0);
            Text_FormatMoney(GuestPtr<char>{money}, globals::stageState()->money, 0x13, 0);
            sdk::sprintf(text, globals::kStatusTextMoney, money);
            TextBox_SetText(panel->text[1], GuestPtr<char>{text}, 0);
        }
        const int32_t marks = city->stamina->value;
        const int32_t scaled = static_cast<int32_t>(static_cast<uint32_t>(marks) * 20u);
        panel->markCount = scaled / 2000000;
    }

    PORT_FN(StatusPanel_Update);

    // The residents' wants window for one frame. Outside the city mode 8 the wants are pruned (Wants_Prune) and that is all. In mode 8 the
    // state (state): 0 starts (the window time animTime and the active flag textsShown cleared, the city's two HUD flags hudVisible[1], [2]
    // cleared, the applicants panel switched off, sound 7) and goes to 1; 1 slides the window model's matrix (matrix, translation from
    // the point at 0x1B08F0 to the one at 0x1B0900 over 10 frames, then the model played at 8 a frame), and after 25 frames makes the
    // four text boxes (two lines of the wants page, two page numbers) and fills them (state 2); 2 waits for Triangle or Cross (the
    // boxes are deleted, sound 8, the time set to 25: state 4) or for left (page back, if not the first: state 3 with the direction 1 and
    // the time at 120) or right (page forward, if there is one: state 3 with the direction 0); 3 turns the page (the time moves by 6 a
    // frame; when its whole part is 60 the page moves and the four boxes are refilled, sound 9) and ends back in state 2; 4 plays the
    // model back and slides the matrix back, and at the end gives the city its update flags back, switches the applicants panel on and
    // pops the city mode (state 0). The active flag is set while the window waits for input.
    void WantsWin_Update(GuestPtr<WantsWin> wants, GuestPtr<CityScene> city)
    {
        struct Locals
        {
            uint8_t gap_00[0x40];
            char opened[0x100];        // +0x40 the page's texts as the window opens
            char forward[0x100];       // +0x140 after turning forward
            char back[0x100];          // +0x240 after turning back
        };
        static_assert(offsetof(Locals, opened) == 0x40 && offsetof(Locals, forward) == 0x140 && offsetof(Locals, back) == 0x240 &&
                      sizeof(Locals) == 0x340);
        port::GuestFrameOf<Locals> frame;
        const auto box = [wants](int i) { return wants->texts[i]; };
        const auto model = [wants]() { return wants->model; };
        if (CityScene_GetMode(city) != 8)
        {
            Wants_Prune(wants, city);
            return;
        }
        // The window model's matrix moved along the path between the two points at 0x1B08F0 and 0x1B0900 (the time over ten).
        const auto slide = [&](ps2f time) {
            const ps2f t = time / ps2f(10.0f);
            const ps2f rest = ps2f(1.0f) - t;
            const GuestPtr<Vec4> to{globals::kWantsWinOpen}, from{globals::kWantsWinClosed};
            wants->matrix[3].x = rest * from->x + t * to->x;
            wants->matrix[3].y = rest * from->y + t * to->y;
            wants->matrix[3].z = rest * from->z + t * to->z;
        };
        // The page's lines and numbers: the two wants lines in texts[0] and texts[1] (after emptying them) and the two page numbers.
        const auto fillPage = [&](GuestPtr<char> buffer) {
            TextBox_SetText(box(0), GuestPtr<char>{globals::kWantsEmptyText}, 0);
            TextBox_SetText(box(1), GuestPtr<char>{globals::kWantsEmptyText}, 0);
            const int32_t first = wants->page << 1;
            Text_FormatPadded(buffer, first + 1, 4);
            TextBox_SetText(box(2), buffer, 0);
            Text_FormatPadded(buffer, (wants->page << 1) + 2, 4);
            TextBox_SetText(box(3), buffer, 0);
            if (first < wants->count)
            {
                WantsWin_FormatLine(wants, city, first, buffer);
                TextBox_SetText(box(0), buffer, 0);
            }
            if (first + 1 < wants->count)
            {
                WantsWin_FormatLine(wants, city, first + 1, buffer);
                TextBox_SetText(box(1), buffer, 0);
            }
        };
        const auto applicantsEnabled = [city](uint8_t value) { city->applicants->enabled = value; };
        switch (wants->state)
        {
        case 0:
            wants->animTime = 0.0f;
            wants->state = 1;
            wants->textsShown = 0;
            city->hudVisible[1] = 0;
            city->hudVisible[2] = 0;
            applicantsEnabled(0);
            Sound_PlaySe(7, 0, 0x3C, 0.0f, 0.0f);
            return;
        case 1:
        {
            const ps2f time = wants->animTime + ps2f(1.0f);
            wants->animTime = time;
            if (time <= ps2f(10.0f))
            {
                slide(time);
                wants->textsShown = 0;
                return;
            }
            A3D_Animate(model(), 0, ps2f(8.0f) * (time - ps2f(10.0f)));
            if (!(wants->animTime < ps2f(25.0f)))
            {
                wants->state = 2;
                wants->texts[0] = GuestPtr<TextBox>{newTextBox(8, 0xA, -2)};
                TextBox_SetPos(box(0), 0x7240, 0x7DE0, -0x100000);
                TextBox_SetCharSize(box(0), 0, 0, 0xB0, 0);
                wants->texts[1] = GuestPtr<TextBox>{newTextBox(8, 0xA, -2)};
                TextBox_SetPos(box(1), 0x8260, 0x7DE0, -0x100000);
                TextBox_SetCharSize(box(1), 0, 0, 0xB0, 0);
                wants->texts[2] = GuestPtr<TextBox>{newTextBox(4, 1, -2)};
                TextBox_SetPos(box(2), 0x7600, 0x8440, -0x100000);
                TextBox_SetCharSize(box(2), 0, 0x90, 0, 0xA0);
                wants->texts[3] = GuestPtr<TextBox>{newTextBox(4, 1, -2)};
                TextBox_SetPos(box(3), 0x8540, 0x8440, -0x100000);
                TextBox_SetCharSize(box(3), 0, 0x90, 0, 0xA0);
                fillPage(frame.at(&Locals::opened));
            }
            wants->textsShown = 0;
            return;
        }
        case 2:
        {
            if ((Pad_Pressed(0) & 0x10u) != 0u || (Pad_Pressed(0) & 0x40u) != 0u)
            {
                wants->state = 4;
                wants->animTime = std::bit_cast<float>(0x41C80000u);
                if (box(1))
                {
                    TextBox_dtor(box(1), 1);
                    wants->texts[1] = GuestPtr<TextBox>{0u};
                }
                if (box(0))
                {
                    TextBox_dtor(box(0), 1);
                    wants->texts[0] = GuestPtr<TextBox>{0u};
                }
                Sound_PlaySe(8, 0, 0x3C, 0.0f, 0.0f);
            }
            else if ((Pad_Held(0) & 0x8000u) != 0u)
            {
                if (wants->page > 0)
                {
                    wants->state = 3;
                    wants->turnBack = 1;
                    wants->animTime = std::bit_cast<float>(0x42F00000u);
                }
            }
            else if ((Pad_Held(0) & 0x2000u) != 0u)
            {
                if (wants->page < wants->pageCount - 1)
                {
                    wants->state = 3;
                    wants->turnBack = 0;
                    wants->animTime = 0.0f;
                }
            }
            wants->textsShown = 1;
            return;
        }
        case 3:
        {
            const bool back = wants->turnBack != 0;
            const ps2f time = back ? wants->animTime - ps2f(6.0f) : wants->animTime + ps2f(6.0f);
            wants->animTime = time;
            if (sdk::fptosi(time) == 60)
            {
                wants->page = wants->page + (back ? -1 : 1);
                fillPage(back ? frame.at(&Locals::back) : frame.at(&Locals::forward));
                Sound_PlaySe(9, 0, 0x3C, 0.0f, 0.0f);
            }
            A3D_Animate(model(), 0, ps2f(120.0f) + wants->animTime);
            if (back)
            {
                if (wants->animTime <= ps2f(0.0f))
                {
                    wants->animTime = 0.0f;
                    wants->state = 2;
                }
            }
            else if (!(wants->animTime <= ps2f(120.0f)))
            {
                wants->animTime = 0.0f;
                wants->state = 2;
            }
            wants->textsShown = 0;
            return;
        }
        case 4:
        {
            const ps2f time = wants->animTime - ps2f(1.0f);
            wants->animTime = time;
            if (!(time < ps2f(10.0f)))
            {
                A3D_Animate(model(), 0, ps2f(4.0f) * (time - ps2f(10.0f)));
                wants->textsShown = 0;
                return;
            }
            slide(time);
            if (wants->animTime <= ps2f(0.0f))
            {
                wants->state = 0;
                city->hudVisible[1] = 1;
                city->hudVisible[2] = 1;
                applicantsEnabled(1);
                CityScene_PopMode(city);
            }
            wants->textsShown = 0;
            return;
        }
        default:
            return;
        }
    }

    PORT_FN(WantsWin_Update);

    // The city cursor's info window for one frame (nothing while visible is clear). The info text is refreshed
    // (CityCursor_UpdateInfo), then by the mode: 0 moves the cursor (CityCursor_Move, with the enabled flag); 1 is the
    // info list: Cross (with Circle not held) takes the entry the frame step (frameStep) is on (twelve to a line, six
    // to an entry: the entries are the chain of ids from target, each record's next in nextOnTile, ending at 0x3F80),
    // picking the tile in the placing mode (3) and starting a talk otherwise; Triangle closes it (mode 0, the city mode
    // popped); up (when there is a line above) and down (when the step is above 0) start the step scrolling (modes 2
    // and 3, sound 0). 2 and 3 move the step by one a frame (up, down) for twelve frames (openTimer), then back to 1. 4
    // clears the picked building: this frame's view's sprite (SpritePacket) opens out around the info frame's position
    // as the timer runs down (25 by 100 units a step, at most 15 steps, kept in the 16 bit range), and when the timer
    // (30 at the pick) runs out the picked building is plotted on the minimap, its family removed from the applicants,
    // the approval lowered by 20, the tile area turned to rubble and the mode popped (twice if the city is then in mode
    // 10); the cursor goes back to mode 0.
    void CityCursor_Update(GuestPtr<CityCursor> cursor, GuestPtr<CityScene> city, int enabled)
    {
        struct Locals
        {
            uint8_t gap_00[0x70];
            ScreenBox box;             // +0x70
        };
        static_assert(offsetof(Locals, box) == 0x70 && sizeof(Locals) == 0x80);
        port::GuestFrameOf<Locals> frame;
        const uint32_t c = city.address;
        if (cursor->visible == 0)
            return;
        const uint32_t parity = globals::frameParity.get() & 1u;
        CityCursor_UpdateInfo(cursor, city);
        const auto map = [city]() { return city->map; };
        switch (cursor->mode)
        {
        case 0:
            CityCursor_Move(cursor, city, enabled);
            return;
        case 1:
        {
            if ((Pad_Pressed(0) & 0x40u) != 0u && (Pad_Held(0) & 0x20u) == 0u)
            {
                Sound_PlaySe(1, 0, 0x3C, 0.0f, 0.0f);
                const int32_t wanted = cursor->frameStep / 6;
                int32_t index = 0;
                uint32_t at = cursor.field(&CityCursor::target).address;
                for (;;)
                {
                    const uint32_t id = port::load<uint16_t>(at);
                    if (id == 0x3F80u)
                        return;
                    if (index == wanted)
                    {
                        if (Player_GetMode(city->player) == 3)
                            CityCursor_SelectTile(cursor, static_cast<int>(id), city);
                        else
                            CityScene_StartTalk(city, 0, static_cast<int>(id), 0x1770, 0);
                        return;
                    }
                    at = Map_GetRecord(map(), static_cast<int>(id)).field(&MapObject::nextOnTile).address;
                    ++index;
                }
            }
            if ((Pad_Pressed(0) & 0x10u) != 0u)
            {
                Sound_PlaySe(2, 0, 0x3C, 0.0f, 0.0f);
                cursor->mode = 0;
                CityScene_PopMode(city);
                return;
            }
            if ((Pad_Held(0) & 0x1000u) != 0u)
            {
                if (cursor->frameStep < (cursor->lineCount - 1) * 12)
                {
                    Sound_PlaySe(0, 0, 0x3C, 0.0f, 0.0f);
                    cursor->mode = 2;
                }
                return;
            }
            if ((Pad_Held(0) & 0x4000u) != 0u && cursor->frameStep > 0)
            {
                Sound_PlaySe(0, 0, 0x3C, 0.0f, 0.0f);
                cursor->mode = 3;
            }
            return;
        }
        case 2:
        case 3:
        {
            cursor->frameStep = cursor->frameStep + (cursor->mode == 2 ? 1 : -1);
            const int32_t timer = cursor->openTimer + 1;
            cursor->openTimer = timer;
            if (timer == 12)
            {
                cursor->mode = 1;
                cursor->openTimer = 0;
            }
            return;
        }
        case 4:
        {
            openPickBox(cursor, GuestPtr<CityCursorView>::of(cursor->views[parity]), frame.at(&Locals::box));
            const int32_t left = cursor->openTimer - 1;
            cursor->openTimer = left;
            if (left != 0)
                return;
            const int32_t picked = cursor->picked;
            const GuestPtr<MapObject> record = Map_GetRecord(map(), picked);
            const GuestPtr<BuildingType> type{port::load<uint32_t>(globals::buildingTypeTable.get() + 4u * record->type)};
            if ((type->flags & 0x80u) == 0u)
                Sound_PlaySe(0x44, 0, 0x3C, 0.0f, 0.0f);
            Minimap_Plot(city->minimap, record->tile, type->size, 4, 0);
            const uint32_t tile = record->tile;
            const int size = type->size;
            CityScene_AddApproval(city, -0x14);
            Applicants_RemoveFamily(city->applicants, record->family);
            port::callGuest<void>(map()->vtable->clearObject, map(), picked);
            Map_PlaceRubbleArea(map(), static_cast<int>(tile & 0x7Fu), static_cast<int>(tile >> 7), size, GuestPtr<CityScene>{c});
            CityScene_PopMode(city);
            if (CityScene_GetMode(city) == 0xA)
                CityScene_PopMode(city);
            cursor->mode = 0;
            return;
        }
        default:
            return;
        }
    }

    PORT_FN(CityCursor_Update);

    // The city cursor: the base object, a model loaded (0x1B5EF0) and deleted again at once, the lines and the fade
    // counters cleared, and for each of the two views (0xD0 bytes, one per frame parity) five text boxes (15 by 1,
    // style -1, lines) with five frames (lineFrames, a 24 by 12 box), the info frame (style 0x32) and the fade frame
    // (0x1E), then the view's opening sprite (SpritePacket): a 128 by 64 texture of 8-bit colours (the sizes as log2 in
    // TEX0), corners in 12.4 fixed point from (0x6C00, 0x7900) to (0x9400, 0x8000) until mm_CityCursor_Update opens it
    // out.
    GuestPtr<CityCursor> CityCursor_ctor(GuestPtr<CityCursor> cursor)
    {
        port::GuestFrame<0x60> frame;
        const uint32_t k = cursor.address;
        BaseObject_ctor(static_cast<int>(k));
        uint32_t model = sdk::operator_new(0x60u);
        if (model != 0u)
            model = A3DObject_Load(GuestPtr<A3DObject>{model}, static_cast<int>(globals::kPathDestructA3d), 1).address;
        A3DObject_dtor(GuestPtr<A3DObject>{model}, 1);
        cursor->lineCount = 0;
        cursor->mode = 0;
        const auto newFrame = [](int style, int x, int y) {
            uint32_t object = sdk::operator_new(0xC0u);
            if (object != 0u)
                object = Frame_ctor(GuestPtr<Frame>{object}, style, x, y, 0x18, 0xC, 0xFFF00000u).address;
            return object;
        };
        for (int32_t parity = 0; parity < 2; ++parity)
        {
            const GuestPtr<CityCursorView> view = GuestPtr<CityCursorView>::of(cursor->views[parity]);
            for (int32_t i = 0; i < 5; ++i)
            {
                uint32_t text = sdk::operator_new(0x40u);
                if (text != 0u)
                    text = TextBox_Init(GuestPtr<TextBox>{text}, 0xF, 1, -1).address;
                view->lines[i] = GuestPtr<TextBox>{text};
                view->lineFrames[i] = GuestPtr<Frame>{newFrame(0, 0, 0)};
            }
            view->frame = GuestPtr<Frame>{newFrame(0x32, 0x64, 0x64)};
            view->fadeFrame = GuestPtr<Frame>{newFrame(0x1E, 0x64, 0x64)};
            SpritePacket &sprite = view->sprite;
            packet::putWords(sprite.dmaTag, {packet::kCnt | 8u, 0u});
            packet::putWords(sprite.vifCodes, {0x11000000u, 0x50000008u});         // FLUSH, DIRECT 8
            packet::putWords(sprite.gifTag, {0x8007u, 0x10000000u, 0xEu, 0u});     // 7 loops (EOP) of A+D
            packet::putAd(sprite.rgbaq, 0x80808080u, 0x3F800000u, packet::kGsRgbaq);
            const uint32_t width = static_cast<uint32_t>(Math_Log2(0x80)), height = static_cast<uint32_t>(Math_Log2(0x40));
            packet::putAd(sprite.tex0,
                          packet::packTex0({.tbp0 = 0x3E8Au, .tbw = 2u, .psm = packet::kPsmT8, .tw = width, .th = height, .tcc = 1u,
                                            .tfx = 0u, .cbp = 0x3EAAu, .cpsm = packet::kPsmCt32, .csm = 0u, .csa = 0u, .cld = 1u}),
                          packet::kGsTex0_1);
            packet::putAd(sprite.prim, 0x16u, 0u, packet::kGsPrim);
            packet::putSt(sprite.stA, 0.0f, 0.0f);
            packet::putXyz(sprite.xyzA, 0x79006C00u, 0x0FFF0000u, packet::kGsXyzf2);
            packet::putSt(sprite.stB, 1.0f, 1.0f);
            packet::putXyz(sprite.xyzB, 0x80009400u, 0x0FFF0000u, packet::kGsXyzf2);
            packet::putWords(sprite.ret, {packet::kRet, 0u, 0u, 0u});
        }
        return cursor;
    }

    PORT_FN(CityCursor_ctor);

    // The title scene's asset loader, one step a call (the step, waitFrames; the loading text box, messageBox, is drawn every call). 0 shows the
    // loading text; 1 loads a model (0x1B8D00) and deletes it at once; 2 to 5 load the four texture images (0x1B8D20 to 0x1B8D50, each
    // through a 0x1010-byte loader on the stack); 6 and 7 load the two models (menuModel, which is also scaled, and logoModel); 8 to 0xB load
    // the sprites titleSprite and logoLetters; 0xC and 0xD load the two choices (green made alpha); 0xE makes the
    // loading screen (loading); 0xF and 0x10 load loadingFrame and loadingBar (the last goes to step 100). 100 skips to 104 unless afterGame
    // is set, which stops the sequences; 101 to 106 wait for the sounds to be idle and the disc ready, reset the voices (bank set 1),
    // zero a sound command and load the bank 7; 107 answers 1 once the sounds are idle (without drawing). 0 otherwise.
    int TitleScene_Load(GuestPtr<TitleScene> scene, int)
    {
        struct Locals
        {
            uint8_t gap_00[0x20];
            TexImage images[4];        // +0x20 the loaders of steps 2 to 5 (0x1010 bytes each)
        };
        static_assert(offsetof(Locals, images) == 0x20 && sizeof(Locals) == 0x4060);
        port::GuestFrameOf<Locals> frame;
        const GuestPtr<TexImage> images = frame.at(&Locals::images);
        const auto nextStep = [scene]() { scene->waitFrames = scene->waitFrames + 1; };
        // A sprite from the file `name` kept in the scene's field `field`.
        const auto loadSprite = [](GuestPtr<TexSprite> &field, uint32_t name) {
            uint32_t sprite = sdk::operator_new(0x38u);
            if (sprite != 0u)
                sprite = static_cast<uint32_t>(TexSprite_Load(GuestPtr<TexSprite>{sprite}, static_cast<int>(name), 1));
            field = GuestPtr<TexSprite>{sprite};
            return sprite;
        };
        const auto loadModel = [](uint32_t name) {
            uint32_t model = sdk::operator_new(0x60u);
            if (model != 0u)
                model = A3DObject_Load(GuestPtr<A3DObject>{model}, static_cast<int>(name), 1).address;
            return model;
        };
        // A texture image read, uploaded and freed again through the loader at `buffer`.
        const auto loadImage = [&](GuestPtr<TexImage> buffer, uint32_t name, int textureBlock, int clutBlock) {
            TexImage_Load(buffer, GuestPtr<char>{name}, textureBlock, clutBlock);
            TexImage_Upload(buffer);
            nextStep();
            TexImage_dtor(buffer, -1);
        };
        const auto place = [](uint32_t sprite, int x, int y, int z) {
            TexSprite_SetPosScale(GuestPtr<TexSprite>{sprite}, x, y, z, ps2f(1.0f), ps2f(1.0f));
        };
        const auto message = [scene]() { return scene->messageBox; };
        const int32_t step = scene->waitFrames;
        switch (step)
        {
        case 0:
            TextBox_SetText(message(), GuestPtr<char>{globals::kTitleTextLoading}, 0);
            nextStep();
            break;
        case 1:
            A3DObject_dtor(GuestPtr<A3DObject>{loadModel(globals::kPathJunkA3d)}, 1);
            nextStep();
            break;
        case 2:
            loadImage(images + 0, globals::kPathGroundTex, 0x2588, 0x2584);
            break;
        case 3:
            loadImage(images + 1, globals::kPathWaterTex, 0x278C, 0x2788);
            break;
        case 4:
            loadImage(images + 2, globals::kPathSpotTex, 0x27A4, 0x27A0);
            break;
        case 5:
            loadImage(images + 3, globals::kPathSplashTex, 0x27B8, 0x27B4);
            break;
        case 6:
        {
            const uint32_t model = loadModel(globals::kPathLoadA3d);
            scene->menuModel = GuestPtr<A3DObject>{model};
            A3DObject_UpdateMatrices(scene->menuModel);
            scene->menuModel->matrix[3].x = std::bit_cast<float>(0x40133333u); // 2.3
            scene->menuModel->matrix[3].y = std::bit_cast<float>(0x3E99999Au); // 0.3
            scene->menuModel->matrix[3].z = std::bit_cast<float>(0x40533333u); // 3.3
            nextStep();
            break;
        }
        case 7:
            scene->logoModel = GuestPtr<A3DObject>{loadModel(globals::kPathTitleA3d)};
            A3DObject_UpdateMatrices(scene->logoModel);
            nextStep();
            break;
        case 8:
            place(loadSprite(scene->titleSprite, globals::kPathTitleTex), 0, 0, 0x3E00);
            nextStep();
            break;
        case 9:
            place(loadSprite(scene->logoLetters[0], globals::kPathTwinkleTex), 0, 0, 0x3F00);
            nextStep();
            break;
        case 10:
            place(loadSprite(scene->logoLetters[1], globals::kPathTwinkleTex), 0, 0, 0x3F00);
            nextStep();
            break;
        case 11:
            place(loadSprite(scene->logoLetters[2], globals::kPathTwinkleTex), 0, 0, 0x3F00);
            nextStep();
            break;
        case 12:
            TexSprite_GreenToAlpha(GuestPtr<TexSprite>{loadSprite(scene->choices[0], globals::kPathHajimeTex)});
            nextStep();
            break;
        case 13:
            TexSprite_GreenToAlpha(GuestPtr<TexSprite>{loadSprite(scene->choices[1], globals::kPathTuzukiTex)});
            nextStep();
            break;
        case 14:
        {
            uint32_t loading = sdk::operator_new(0x28u);
            if (loading != 0u)
                loading = LoadingScreen_ctor(GuestPtr<LoadingScreen>{loading},
                                             static_cast<int>(globals::kPathLoadingTex), 1).address;
            scene->loading = GuestPtr<LoadingScreen>{loading};
            const GuestPtr<LoadingScreen> screen{scene->loading.address};
            LoadingScreen_SetSpritePos(screen, 0x154, 0x168, 0x3F00);
            LoadingScreen_SetTextPos(screen, 0x108, 0x32);
            scene->loading->showText = 1;
            nextStep();
            break;
        }
        case 15:
            place(loadSprite(scene->loadingFrame, globals::kPathGaugeOffTex), 0x40, 0x154, 1);
            nextStep();
            break;
        case 16:
            place(loadSprite(scene->loadingBar, globals::kPathGaugeOnTex), 0x40, 0x154, 1);
            scene->waitFrames = 100;
            break;
        case 100:
            if (scene->afterGame == 0)
                scene->waitFrames = 0x68;
            else
            {
                Sound_StopAllSequences();
                nextStep();
            }
            break;
        case 101:
        case 103:
        case 105:
            if (Sound_IsBusy() == 0)
                nextStep();
            break;
        case 102:
            SoundCmd_WaveCloseBankSet();
            nextStep();
            break;
        case 104:
            if (sdk::sceCdDiskReady(0) == 2)
            {
                Sound_ResetVoices(1);
                nextStep();
            }
            break;
        case 106:
            if (sdk::sceCdDiskReady(0) == 2)
            {
                SoundCmd_MidiOpen(7, static_cast<int>(globals::kTitleBankSq), static_cast<int>(globals::kTitleBankHd),
                                  static_cast<int>(globals::kTitleBankBd), 0);
                nextStep();
            }
            break;
        case 107:
            if (Sound_IsBusy() == 0)
                return 1;
            break;
        default:
            break;
        }
        TextBox_Draw(message());
        return 0;
    }

    PORT_FN(TitleScene_Load);

    // The Town's Pages book for one frame. The state (mode): 0 starts (the open time openTime at 0, the city's update flags off, the applicants
    // panel off, sound 7); 1 slides the book model's matrix (bookMatrix) in over 10 frames, plays it, and after 25 frames makes the six text boxes
    // (the three lines and the page numbers), builds the index, fills the page and puts the cursor on its row; 2 waits for input: Triangle
    // closes the book (state 4, time 25, the boxes and the index freed, sound 8), Start goes back from a family page to the contents (state 6
    // with the page and row it came from), Cross picks the entry under the cursor (a contents entry opens its first page, a family page entry
    // closes the book with the family kept in chosenFamily; the last row, 10, closes it from the contents or goes back from a family page), and the
    // d-pad moves the cursor (state 5) or turns the page (state 3, direction in turnBack); 3 turns the page (the time moves by 6; when its whole
    // part is 60 the page changes and is filled) and then puts the cursor on its row; 4 slides the book out and, at the end, gives the city
    // its update flags and the applicants panel back, then starts the talk with the family kept (a family type's home, or the family itself
    // for the debug flag 2) or, with none, pops the city mode; 5 moves the cursor sprite (cursorX and cursorY) half the way to the row's place
    // (the table at 0x1B1A70) a frame; 6 flips to the page in targetPage (the model played out and back, the page changed at 23).
    void TownPages_Update(GuestPtr<TownPages> pages, GuestPtr<CityScene> city)
    {
        const uint32_t p = pages.address, c = city.address;
        const auto loadF = [](uint32_t at) { return ps2f(std::bit_cast<float>(port::load<uint32_t>(at))); };
        const auto storeF = [](uint32_t at, ps2f value) { port::store<uint32_t>(at, std::bit_cast<uint32_t>(value.v)); };
        const auto box = [p](uint32_t at) { return GuestPtr<TextBox>{port::load<uint32_t>(p + at)}; };
        const auto book = [pages]() { return pages->book; };
        const auto state = [pages](int32_t value) { pages->mode = value; };
        const auto place = [&](int32_t row, uint32_t axis) {
            return port::load<int32_t>(globals::kTownPagesCursor + 8u * static_cast<uint32_t>(row) + axis);
        };
        const auto clearBoxes = [&]() {
            for (const uint32_t at : {0x4u, 0x8u, 0xCu, 0x10u, 0x14u, 0x18u})
                TextBox_SetText(box(at), GuestPtr<char>{globals::kTownPagesEmptyText}, 0);
        };
        // The book closes: the state 4 with the time 25, the boxes freed in their order, the index freed, sound 8.
        const auto closeBook = [&]() {
            state(4);
            pages->openTime = std::bit_cast<float>(0x41C80000u);
            for (const uint32_t at : {0x8u, 0x4u, 0x14u, 0x10u, 0xCu, 0x18u})
                if (box(at))
                {
                    TextBox_dtor(box(at), 1);
                    port::store<uint32_t>(p + at, 0u);
                }
            if (pages->index.address != 0u)
            {
                sdk::operator_delete(pages->index.address);
                pages->index = GuestPtr<TownPagesIndex>{0u};
            }
            Sound_PlaySe(8, 0, 0x3C, 0.0f, 0.0f);
        };
        // The book model's matrix moved along the path between the points at 0x1B1A50 and 0x1B1A60 (the time over ten).
        const auto slide = [&](ps2f time) {
            const ps2f t = time / ps2f(10.0f);
            const ps2f rest = ps2f(1.0f) - t;
            const auto lerp = [&](uint32_t to, uint32_t from) { return rest * loadF(from) + t * loadF(to); };
            storeF((pages->bookMatrix + 3).field(&Vec4::x).address, lerp(globals::kTownPagesOpen, globals::kTownPagesClosed));
            storeF((pages->bookMatrix + 3).field(&Vec4::y).address,
                   lerp(globals::kTownPagesOpen + 0x4u, globals::kTownPagesClosed + 0x4u));
            storeF((pages->bookMatrix + 3).field(&Vec4::z).address,
                   lerp(globals::kTownPagesOpen + 0x8u, globals::kTownPagesClosed + 0x8u));
        };
        // Goes back to the page and row the book came from (state 6, the flip).
        const auto goBack = [&]() {
            pages->targetPage = pages->savedPage;
            pages->targetRow = pages->savedRow;
            state(6);
            pages->openTime = 0.0f;
            clearBoxes();
        };
        const auto applicantsEnabled = [city](uint8_t value) { city->applicants->enabled = value; };
        const uint32_t current = static_cast<uint32_t>(pages->mode);
        if (current >= 7u)
            return;
        switch (current)
        {
        case 0:
            pages->openTime = 0.0f;
            state(1);
            city->hudVisible[1] = 0;
            city->hudVisible[2] = 0;
            applicantsEnabled(0);
            Sound_PlaySe(7, 0, 0x3C, 0.0f, 0.0f);
            return;
        case 1:
        {
            const ps2f time = pages->openTime + ps2f(1.0f);
            pages->openTime = time;
            if (time <= ps2f(10.0f))
            {
                slide(time);
                return;
            }
            A3D_Animate(book(), 0, ps2f(8.0f) * (time - ps2f(10.0f)));
            if (pages->openTime < ps2f(25.0f))
                return;
            state(2);
            struct Layout { uint32_t at; int columns, rows, x, y, a, b, c, d; };
            const Layout layout[] = {
                {0x4u, 0xA, 0xA, 0x71E0, 0x7DE0, 0, 0, 0xB0, 0}, {0x8u, 0xA, 0xA, 0x82B0, 0x7D60, 0, 0, 0xB0, 0}, {0xCu, 0xA, 1, 0x71E0, 0x7CC0, 0, 0, 0xB0, 0},
                {0x10u, 4, 1, 0x7620, 0x8520, 0, 0x90, 0, 0xA0}, {0x14u, 4, 1, 0x8710, 0x8520, 0, 0x90, 0, 0xA0}, {0x18u, 4, 1, 0x8B40, 0x8480, 0, 0x90, 0, 0xA0}};
            for (const Layout &l : layout)
            {
                port::store<uint32_t>(p + l.at, newTextBox(l.columns, l.rows, -2));
                TextBox_SetPos(box(l.at), l.x, l.y, -0x100000);
                TextBox_SetCharSize(box(l.at), l.a, l.b, l.c, l.d);
            }
            TownPages_BuildIndex(pages);
            TownPages_FillPage(pages);
            const int row = TownPages_ClampCursor(pages, pages->row, 0);
            pages->targetRow = row;
            pages->row = row;
            pages->cursorX = place(pages->row, 0);
            pages->cursorY = place(pages->row, 4);
            return;
        }
        case 2:
        {
            const uint32_t pad = Pad_Pressed(0);
            if ((pad & 0x10u) != 0u)
            {
                pages->chosenFamily = 0;
                closeBook();
                return;
            }
            const bool contents = pages->page < pages->contentsPages;
            if ((pad & 0x80u) != 0u)
            {
                if (contents)
                    return;
                Sound_PlaySe(2, 0, 0x3C, 0.0f, 0.0f);
                goBack();
                return;
            }
            if ((pad & 0x40u) != 0u)
            {
                const int32_t row = pages->row;
                if (contents)
                {
                    if (row == 10)
                    {
                        pages->chosenFamily = 0;
                        closeBook();
                        return;
                    }
                    const int entry = TownPages_SelectedEntry(pages);
                    pages->savedPage = pages->page;
                    pages->savedRow = pages->row;
                    pages->targetPage = pages->index[entry].startPage;
                    state(6);
                    pages->openTime = 0.0f;
                    clearBoxes();
                    Sound_PlaySe(1, 0, 0x3C, 0.0f, 0.0f);
                    return;
                }
                Sound_PlaySe(1, 0, 0x3C, 0.0f, 0.0f);
                if (row == 10)
                {
                    goBack();
                    return;
                }
                pages->chosenFamily = TownPages_SelectedEntry(pages);
                closeBook();
                return;
            }
            const auto cursorSound = [](bool moved) {
                if (moved)
                    Sound_PlaySe(0, 0, 0x3C, 0.0f, 0.0f);
            };
            if ((Pad_Held(0) & 0x8000u) != 0u)
            {
                const int32_t row = pages->row;
                if (place(row, 0) != 0)
                {
                    const int target = TownPages_ClampCursor(pages, row == 10 ? 4 : row - 5, 3);
                    pages->targetRow = target;
                    cursorSound(target != pages->row);
                    state(5);
                    return;
                }
                if (pages->page <= 0)
                    return;
                state(3);
                pages->turnBack = 1;
                pages->openTime = std::bit_cast<float>(0x42F00000u);
                Sound_PlaySe(9, 0, 0x3C, 0.0f, 0.0f);
                return;
            }
            if ((Pad_Held(0) & 0x2000u) != 0u)
            {
                const int32_t row = pages->row;
                if (place(row, 0) != 0)
                {
                    if (!(pages->page < pages->pageCount - 1))
                        return;
                    state(3);
                    pages->turnBack = 0;
                    pages->openTime = 0.0f;
                    Sound_PlaySe(9, 0, 0x3C, 0.0f, 0.0f);
                    return;
                }
                Sound_PlaySe(0, 0, 0x3C, 0.0f, 0.0f);
                pages->targetRow = TownPages_ClampCursor(pages, row + 5, 1);
                state(5);
                return;
            }
            if ((Pad_Held(0) & 0x1000u) != 0u)
            {
                const int32_t before = pages->targetRow;
                pages->targetRow = TownPages_ClampCursor(pages, pages->row - 1, 0);
                cursorSound(before != pages->targetRow);
                state(5);
                return;
            }
            if ((Pad_Held(0) & 0x4000u) != 0u)
            {
                const int32_t before = pages->targetRow;
                pages->targetRow = TownPages_ClampCursor(pages, pages->row + 1, 2);
                cursorSound(before != pages->targetRow);
                state(5);
            }
            return;
        }
        case 3:
        {
            const bool back = pages->turnBack != 0;
            const ps2f time = back ? pages->openTime - ps2f(6.0f) : pages->openTime + ps2f(6.0f);
            pages->openTime = time;
            if (sdk::fptosi(time) == 60)
            {
                pages->page = pages->page + (back ? -1 : 1);
                TownPages_FillPage(pages);
            }
            A3D_Animate(book(), 0, ps2f(120.0f) + pages->openTime);
            const auto settle = [&](int direction) {
                const int row = TownPages_ClampCursor(pages, pages->row, direction);
                pages->targetRow = row;
                state(row == pages->row ? 2 : 5);
            };
            if (back)
            {
                if (pages->openTime <= ps2f(0.0f))
                {
                    pages->openTime = 0.0f;
                    settle(3);
                }
            }
            else if (!(pages->openTime <= ps2f(120.0f)))
            {
                pages->openTime = 0.0f;
                settle(1);
            }
            return;
        }
        case 4:
        {
            const ps2f time = pages->openTime - ps2f(1.0f);
            pages->openTime = time;
            if (!(time < ps2f(10.0f)))
            {
                A3D_Animate(book(), 0, ps2f(4.0f) * (time - ps2f(10.0f)));
                return;
            }
            slide(time);
            if (!(pages->openTime <= ps2f(0.0f)))
                return;
            city->hudVisible[1] = 1;
            city->hudVisible[2] = 1;
            applicantsEnabled(1);
            state(0);
            const uint32_t family = static_cast<uint32_t>(pages->chosenFamily);
            if (family == 0u)
            {
                CityScene_PopMode(city);
                return;
            }
            const GuestPtr<MapObject> home = Map_FindFamilyHome(city->map, static_cast<uint16_t>(family));
            if (home.address != 0u)
                CityScene_StartTalk(city, 0, home->id, 0x1770, 1);
            else if ((globals::stageState()->flags & 2u) != 0u)
                CityScene_StartTalk(city, 1, static_cast<int>(family), 6, 3);
            else
                CityScene_StartTalk(city, 1, static_cast<int>(family), 6, 1);
            return;
        }
        case 5:
        {
            const int32_t row = pages->targetRow;
            const int32_t targetX = place(row, 0);
            const int32_t x = pages->cursorX;
            int32_t xDiff = targetX - x;
            if (x == targetX)
            {
                if (pages->cursorY == place(row, 4))
                {
                    pages->row = row;
                    state(2);
                    return;
                }
                xDiff = 0;
            }
            const int32_t stepX = xDiff / 2;
            const int32_t yDiff = place(row, 4) - pages->cursorY;
            const int32_t stepY = yDiff / 2;
            if (stepX == 0)
                pages->cursorX = targetX;
            else
                pages->cursorX = pages->cursorX + stepX;
            if (stepY == 0)
                pages->cursorY = place(pages->targetRow, 4);
            else
                pages->cursorY = pages->cursorY + stepY;
            return;
        }
        case 6:
        {
            const ps2f time = pages->openTime;
            if (time <= ps2f(10.0f))
            {
                A3D_Animate(book(), 0, ps2f(120.0f) - ps2f(10.0f) * time);
                pages->openTime = pages->openTime + ps2f(1.0f);
            }
            else if (time == ps2f(11.0f))
                pages->openTime = time + ps2f(1.0f);
            else if (time <= ps2f(22.0f))
            {
                A3D_Animate(book(), 0, ps2f(10.0f) * (time - ps2f(10.0f)));
                pages->openTime = pages->openTime + ps2f(1.0f);
            }
            else if (time == ps2f(23.0f))
            {
                pages->openTime = 0.0f;
                pages->page = pages->targetPage;
                if (pages->page < pages->contentsPages)
                    pages->targetRow = pages->savedRow;
                else
                    pages->targetRow = 0;
                TownPages_FillPage(pages);
                state(5);
            }
            return;
        }
        default:
            return;
        }
    }

    PORT_FN(TownPages_Update);
}
