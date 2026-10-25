// Graphics helpers: DMA lists and the packet buffer, model accessors, render layer and light state.

#include "game_functions.h"
#include "game_globals.h"
#include "lifetime.h"
#include "sdk.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace mm
{
    // Word +0xC of mesh `mesh` in the model's mesh table, as a float.
    ps2f A3D_GetMeshField0C(GuestPtr<A3DObject> model, int mesh)
    {
        const uint32_t entry = model->meshes.address + 4u * static_cast<uint32_t>(mesh);
        return port::load<float>(port::load<uint32_t>(entry) + 0xCu);
    }

    // A DMA chain reader starts at `chain`, with the current tag pointing at the scratchpad (0x70000000).
    int DmaChain_Begin(int reader, int chain)
    {
        port::store<int32_t>(static_cast<uint32_t>(reader) + 4u, chain);
        port::store<uint32_t>(static_cast<uint32_t>(reader) + 8u, 0x70000000u);
        return reader;
    }

    namespace
    {
        // Appends a DMA CALL tag to `target` to the current list when there is room (64 KB): the tag, then a
        // placeholder END tag in the next quadword, which the next append overwrites.
        void appendDmaCall(uint32_t target)
        {
            const uint32_t ptr = globals::dmaListPtr.get();
            const uint32_t base = port::load<uint32_t>(globals::kDmaLists + 4u * globals::dmaListIndex.get());
            int32_t used = static_cast<int32_t>(ptr - base);
            if (used < 0)
                used += 3;
            if ((used >> 2) >= 0x4000)
                return;
            port::store<uint32_t>(ptr + 0x0, 0x50000000u); // CALL, no data of its own
            port::store<uint32_t>(ptr + 0x4, target);
            port::store<uint32_t>(ptr + 0x8, 0u);
            port::store<uint32_t>(ptr + 0xC, 0u);
            port::store<uint32_t>(ptr + 0x10, 0x70000000u); // END
            port::store<uint32_t>(ptr + 0x14, 0u);
            port::store<uint32_t>(ptr + 0x18, 0u);
            port::store<uint32_t>(ptr + 0x1C, 0u);
            globals::dmaListPtr.set(ptr + 16u);
        }
    }

    void Dma_AppendCall(int address) { appendDmaCall(static_cast<uint32_t>(address) & 0x7FFFFFFFu); }
    void Dma_AppendCall_14C200(int address) { Dma_AppendCall(address); } // a thunk to the one above

    // Full-screen black sprite with the given alpha (written into the static packet's colour first), on
    // the first (a1 == 0) or the second fade packet.
    void DmaList_QueueFade(int alpha, int a1)
    {
        const uint32_t colour = static_cast<uint32_t>(alpha) << 24;
        if (a1 == 0)
        {
            port::store<uint32_t>(0x001B2830u, colour);
            appendDmaCall(0x001B2800u);
        }
        else
        {
            port::store<uint32_t>(0x001B28A0u, colour);
            appendDmaCall(0x001B2870u);
        }
    }

    // Steps a DMA chain reader to the next tag: past a CNT tag's data, onto the next tag otherwise; stays
    // on END; returns 0 at a RET tag (0x6...). The reader keeps the tag's id, size, words and data address.
    int DmaChain_Next(int reader)
    {
        const uint32_t r = static_cast<uint32_t>(reader);
        const uint32_t id = port::load<uint32_t>(r + 8);
        if (id != 0x70000000u)
        {
            if (id == 0x10000000u)
                port::store<uint32_t>(r + 4, port::load<uint32_t>(r + 4) + (port::load<uint32_t>(r + 0xC) + 1u) * 16u);
            else if (id == 0x60000000u)
                return 0;
            else
                port::store<uint32_t>(r + 4, port::load<uint32_t>(r + 4) + 16u);
        }
        const uint32_t tag = port::load<uint32_t>(r + 4);
        port::store<uint32_t>(r + 0x8, port::load<uint32_t>(tag) & 0x70000000u);
        port::store<uint32_t>(r + 0xC, port::load<uint16_t>(tag));
        port::store<uint32_t>(r + 0x10, port::load<uint32_t>(tag + 4));
        port::store<uint32_t>(r + 0x18, port::load<uint32_t>(tag + 8));
        port::store<uint32_t>(r + 0x20, port::load<uint32_t>(tag + 0xC));
        port::store<uint32_t>(r + 0x14, port::load<uint32_t>(tag + 8) & 0x7F000000u);
        port::store<uint32_t>(r + 0x1C, port::load<uint32_t>(tag + 0xC) & 0x7F000000u);
        port::store<uint32_t>(r + 0x24, tag + 16u);
        return 1;
    }

    // Static GS packets queued as calls.
    void DmaList_QueueGsDefaults() { appendDmaCall(0x001B2760u); }  // standard alpha blend, texture and test state
    void DmaList_QueueClearScreen() { appendDmaCall(0x001B28E0u); } // full-screen sprite writing colour 0
    void DmaList_QueueClearZ() { appendDmaCall(0x001B26D0u); }      // full-screen sprite writing only Z

    // Start of a frame's drawing: the packet buffer rewinds and the list begins with the GS defaults.
    void DmaList_BeginScene()
    {
        globals::packetBufPos.set(static_cast<int32_t>(globals::packetBufBase.get()));
        DmaList_QueueGsDefaults();
    }

    // ---- city render layers: each constructor sets its parameters and builds its VU1 packet in the packet
    // buffer, which then moves past it. D, E and G also get the city map's vertex buffers.

    GuestPtr<LayerB> LayerB_ctor(GuestPtr<LayerB> layer)
    {
        BaseObject_ctor(static_cast<int>(layer.address));
        LayerB_InitParams(layer);
        PacketBuf_Seek(static_cast<int>(LayerB_BuildPacket(layer, GuestPtr<uint32_t>{static_cast<uint32_t>(PacketBuf_Tell())}).address));
        return layer;
    }

    GuestPtr<LayerC> LayerC_ctor(GuestPtr<LayerC> layer)
    {
        BaseObject_ctor(static_cast<int>(layer.address));
        LayerC_InitParams(layer);
        PacketBuf_Seek(static_cast<int>(LayerC_BuildPacket(layer, GuestPtr<uint32_t>{static_cast<uint32_t>(PacketBuf_Tell())}).address));
        layer->unk_230 = GuestPtr<void>{port::load<uint32_t>(0x001F3B1Cu)};
        return layer;
    }

    GuestPtr<LayerD> LayerD_ctor(GuestPtr<LayerD> layer, int buffers)
    {
        BaseObject_ctor(static_cast<int>(layer.address));
        LayerD_InitParams(layer);
        const GuestPtr<uint32_t> packet{static_cast<uint32_t>(PacketBuf_Tell())};
        PacketBuf_Seek(static_cast<int>(LayerD_BuildPacket(layer, packet, GuestPtr<int32_t>{static_cast<uint32_t>(buffers)}).address));
        return layer;
    }

    GuestPtr<LayerE> LayerE_ctor(GuestPtr<LayerE> layer, int buffers)
    {
        BaseObject_ctor(static_cast<int>(layer.address));
        LayerE_InitParams(layer);
        const GuestPtr<uint32_t> packet{static_cast<uint32_t>(PacketBuf_Tell())};
        PacketBuf_Seek(static_cast<int>(LayerE_BuildPacket(layer, packet, GuestPtr<int32_t>{static_cast<uint32_t>(buffers)}).address));
        return layer;
    }

    GuestPtr<LayerF> LayerF_ctor(GuestPtr<LayerF> layer)
    {
        BaseObject_ctor(static_cast<int>(layer.address));
        LayerF_InitParams(layer);
        PacketBuf_Seek(static_cast<int>(LayerF_BuildPacket(layer, GuestPtr<uint32_t>{static_cast<uint32_t>(PacketBuf_Tell())}).address));
        return layer;
    }

    GuestPtr<LayerG> LayerG_ctor(GuestPtr<LayerG> layer, int buffers)
    {
        BaseObject_ctor(static_cast<int>(layer.address));
        LayerG_InitParams(layer);
        const GuestPtr<uint32_t> packet{static_cast<uint32_t>(PacketBuf_Tell())};
        PacketBuf_Seek(static_cast<int>(LayerG_BuildPacket(layer, packet, GuestPtr<int32_t>{static_cast<uint32_t>(buffers)}).address));
        return layer;
    }

    // The water waves (layer G). A phase runs from -pi/2 to pi/2 in steps of 0.05, then starts over with the
    // sway direction flipped; sin(phase) and sin(phase + pi/2) (wrapped, flipping its own direction) give two
    // sway offsets of up to 0.05. The first point set sways by one and scrolls forward 0.002 a frame, the
    // second (1.2 times the sway, 0.7 to the side) by the other and scrolls back 0.001; points at x 0.5 take
    // the other offset. Both scrolls wrap at 1.
    void LayerG_AnimateWaves(GuestPtr<LayerG> layer, ps2f weather)
    {
        const auto initOnce = [](uint32_t address, uint32_t bits) {
            if (port::load<int8_t>(address + 4u) == 0)
            {
                port::store<uint32_t>(address, bits);
                port::store<int8_t>(address + 4u, 1);
            }
        };
        initOnce(globals::kWavePhase, 0xBFC90FDBu); // -pi/2
        initOnce(globals::kWaveDirection, 0x3F800000u); // 1
        initOnce(globals::kWaveScrollA, 0);
        initOnce(globals::kWaveScrollB, 0);
        ps2f &phase = *GuestPtr<ps2f>{globals::kWavePhase};
        ps2f &direction = *GuestPtr<ps2f>{globals::kWaveDirection};
        ps2f &scrollA = *GuestPtr<ps2f>{globals::kWaveScrollA};
        ps2f &scrollB = *GuestPtr<ps2f>{globals::kWaveScrollB};
        const ps2f halfPi = 1.57079637050628662109375f, pi = 3.1415927410125732421875f;
        phase = phase + ps2f(0.05f);
        if (!(phase < halfPi))
        {
            direction = direction * ps2f(-1.0f);
            phase = -halfPi;
        }
        layer->waveParamsA.w = pi;
        layer->waveParamsA.z = 0.3183098733425140380859375f; // 1/pi
        layer->waveParamsA.y = phase - halfPi;
        layer->waveParamsA.x = 1.0f;
        layer->waveParamsB.w = 0.0f;
        layer->waveParamsB.z = phase;
        layer->waveParamsB.y = weather;
        layer->waveParamsB.x = ps2f(0.06f) * direction;
        ps2f angleB = halfPi + phase;
        ps2f directionB = direction;
        if (!(angleB < halfPi))
        {
            angleB = angleB - pi;
            directionB = directionB * ps2f(-1.0f);
        }
        const ps2f swayA = direction * (ps2f(0.05f) * sdk::sinf(phase));
        const ps2f swayB = directionB * (ps2f(0.05f) * sdk::sinf(angleB));
        scrollA = scrollA + ps2f(0.002f);
        if (!(scrollA < ps2f(1.0f)))
            scrollA = 0.0f;
        scrollB = scrollB - ps2f(0.001f);
        if (scrollB < ps2f(0.0f))
            scrollB = scrollB + ps2f(1.0f);
        for (int i = 0; i < 12; ++i)
        {
            layer->wavesA[i].y = layer->waveBase[i].y + scrollA;
            const ps2f x = layer->waveBase[i].x;
            layer->wavesA[i].x = ps2f(0.5f) == x ? x + swayB : x + swayA;
        }
        const ps2f swayA2 = ps2f(1.2f) * swayA, swayB2 = ps2f(1.2f) * swayB;
        for (int i = 0; i < 12; ++i)
        {
            layer->wavesB[i].y = layer->waveBase[i].y + scrollB;
            const ps2f x = layer->waveBase[i].x;
            layer->wavesB[i].x = ps2f(0.7f) + (x + (ps2f(0.5f) == x ? swayA2 : swayB2));
        }
    }

    // The camera's VU1 packet, built in the packet buffer: a DMA cnt with a GIF A+D write of 0 to GS register
    // 0x3D, a ref to the camera's 0x12 quadwords from +0x10 (both screen matrices, the light matrices, the fog
    // vectors) unpacked to VU1 address 0, and a ret.
    void Camera_BuildPacket(GuestPtr<Camera> camera)
    {
        camera->packet = static_cast<uint32_t>(PacketBuf_Tell());
        const GuestPtr<uint32_t> p{camera->packet};
        const uint32_t words[20] = {
            0x10000002u, 0, 0x11000000u, 0x50000002u,                       // cnt 2; VIF flush, direct 2
            0x00008001u, 0x10000000u, 0x0000000Eu, 0,                       // GIF tag: 1 loop, eop, A+D
            0, 0, 0x0000003Du, 0,                                            // register 0x3D = 0
            0x30000012u, (camera.address + 0x10u) & 0x7FFFFFFFu, 0x01000101u, 0x6C120000u, // ref 0x12; stcycl, unpack v4-32
            0x60000000u, 0, 0, 0,                                            // ret
        };
        for (uint32_t i = 0; i < 20; ++i)
            p[i] = words[i];
        PacketBuf_Seek(static_cast<int>((p + 20).address));
    }

    // ---- drawing: queue an object's prebuilt packet on the DMA list

    void Packet_Draw(GuestPtr<uint32_t> packets) { Dma_AppendCall(static_cast<int>(*packets)); }
    void Packet2_Draw(GuestPtr<uint32_t> packets, int second) { Dma_AppendCall(static_cast<int>(packets[second == 0 ? 0 : 1])); }
    void TexSprite_Draw(GuestPtr<TexSprite> sprite) { Dma_AppendCall(static_cast<int>(sprite->packet)); }

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
    }

    // Two static packets (at 0x1F69C0) that each draw a 640 x 224 picture held in the GS as 5 x 7 textured
    // sprites of 128 x 32: a GS state header (A+D: 0x47, 0x14, 0x42, colour 0x60808080, 0x56), the 35 sprites
    // (TEX0 from block 0x8000, the second picture 0x8C0 blocks on, then ST and XYZ2 of two corners) and a footer
    // restoring the test and alpha state.
    void DmaList_InitPackets()
    {
        GuestPtr<uint32_t> p{globals::kFramePictures};
        for (uint32_t picture = 0; picture < 2; ++picture)
        {
            p = emitWords(p, {0x100000B8, 0, 0x11000000, 0x500000B8,
                              0x80B7, 0x10000000, 0xE, 0,
                              0x5000F, 0, 0x47, 0,
                              0, 0, 0x14, 0,
                              100, 0x50, 0x42, 0,
                              0x60808080, 0x3F800000, 1, 0,
                              0x56, 0, 0, 0});
            for (uint32_t row = 0; row < 7; ++row)
                for (uint32_t column = 0; column < 5; ++column)
                {
                    const uint32_t block = picture * 0x8C0u + (row * 5 + column) * 0x40u;
                    const uint32_t top = 0x78FCu + row * 0x200u, left = 0x6BF8u + column * 0x800u;
                    p = emitWords(p, {block + 0x5C008000u, 5, 6, 0,
                                      0, 0, 2, 0,
                                      (top << 16) | left, 0, 4, 0,
                                      0x3F800000, 0x3F800000, 2, 0,
                                      ((top + 0x200u) << 16) | (left + 0x800u), 0, 4, 0});
                }
            p = emitWords(p, {0x60, 0, 0x14, 0,
                              0x5000F, 0, 0x47, 0,
                              0x44, 0x20, 0x42, 0,
                              0x60000000, 0, 0, 0});
        }
    }

    // The DMA packet (written over the start of `image`) that uploads a loaded texture file to the GS: its palette
    // (8 x 2 for 4-bit texels, else 16 x 16) to block `clutBlock`, then the texels (PSMT8 0x13, PSMT4 0x14,
    // else PSMCT32) as square images of the file's side, one transfer per part, to `textureBlock` onwards, and
    // a texture flush.
    void TexImage_Convert(GuestPtr<TexImage> image, uint32_t textureBlock, uint32_t clutBlock)
    {
        const GuestPtr<TexImageFile> file = image->file;
        const uint32_t palette = file.address + 0x20u;
        uint32_t texels = palette + 4u * static_cast<uint32_t>(file->paletteEntries);
        GuestPtr<uint32_t> p{image.address};
        if (file->paletteEntries != 0)
        {
            const bool small = file->bitsPerPixel == 4;
            const uint32_t width = small ? 8 : 16, height = small ? 2 : 16, qwc = small ? 4 : 0x40;
            p = emitWords(p, {0x10000005, 0, 0x11000000, 0x50000005,
                              3, 0x10000000, 0xE, 0,
                              width, height, 0x52, 0,
                              0, clutBlock | 0x10000u, 0x50, 0,
                              0, 0, 0x53, 0,
                              qwc + 0x8000u, 0x08000000, 0, 0,
                              qwc | 0x30000000u, palette & 0x7FFFFFFFu, 0, qwc | 0x50000000u});
        }
        if (file->hasTexels != 0)
        {
            const uint32_t side = file->side;
            const uint32_t psm = file->bitsPerPixel == 8 ? 0x13 : (file->bitsPerPixel == 4 ? 0x14 : 0);
            p = emitWords(p, {0x10000002, 0, 0x11000000, 0x50000002,
                              0x8001, 0x10000000, 0xE, 0,
                              side, side, 0x52, 0});
            uint32_t words = side * side;
            if (file->bitsPerPixel == 8)
                words >>= 2;
            else if (file->bitsPerPixel == 4)
                words >>= 3;
            const uint32_t qwc = words >> 2;
            for (int32_t done = 0; done < file->uploadSpan; done += 0x40)
            {
                p = emitWords(p, {0x10000004, 0, 0, 0x50000004,
                                  2, 0x10000000, 0xE, 0,
                                  0, (psm << 24) | textureBlock | ((side >> 6) << 16), 0x50, 0,
                                  0, 0, 0x53, 0,
                                  qwc | 0x8000u, 0x08000000, 0, 0,
                                  qwc | 0x30000000u, texels & 0x7FFFFFFFu, 0, qwc | 0x50000000u});
                texels += words * 4u;
                textureBlock += words >> 6;
            }
            p = emitWords(p, {0x10000002, 0, 0, 0x50000002,
                              0x8001, 0x10000000, 0xE, 0,
                              0, 0, 0x3F, 0});
        }
        emitWords(p, {0x60000000, 0, 0, 0});
    }

    // The sprite's DMA packet, rebuilt after each change. With a palette (16 entries as 8 x 2, 256 as 16 x 16)
    // an image transfer of it to GS block 0x1E00 comes first, and the texels are 4- or 8-bit, else 32-bit. Then
    // for each tile from the first frame tile up to the clip tile (used as an end here), row by row: an image
    // transfer of its texels above the palette, a texture flush, and a sprite with TEX0 (the tile's size as
    // powers of two, the palette, CLUT load 2), ST from 0 to 1 and the tile's place: (x, y, z) plus the tile's
    // index times its scaled size (x and y in 1/16 and 1/8 units, from 0x6C00 and 0x7900). Tiles are clamped
    // while drawn and textures repeat again after; the sprite blends when +0x24 is set. The texel source
    // (+0x20) walks through the file as it goes. Returns the scaled tile height.
    int TexSprite_BuildPacket(GuestPtr<TexSprite> sprite)
    {
        GuestPtr<uint32_t> p{sprite->packet};
        const GuestPtr<TexFileHeader> image = sprite->image;
        uint32_t texelBase = 0x1E00;
        uint32_t tileQwords = static_cast<uint32_t>(static_cast<int32_t>(static_cast<uint32_t>(image->tileWidth) * image->tileHeight) / 4);
        const int log2Width = Math_Log2(image->tileWidth);
        const int log2Height = Math_Log2(image->tileHeight);
        const uint32_t palette = image.address + 0x20u;
        sprite->upload = palette;
        int bufferWidth = image->tileWidth / 64;
        if (bufferWidth == 0)
            bufferWidth = 1;
        const uint32_t drawWidth = sdk::fptoui(fromInt(image->tileWidth) * sprite->scaleX);
        const uint32_t drawHeight = sdk::fptoui(fromInt(image->tileHeight) * sprite->scaleY);

        uint32_t psm = 0;
        if (image->paletteSize != 0)
        {
            const uint32_t entries = image->paletteSize, quarter = entries / 4u;
            const bool small = entries == 16;
            p = emitWords(p, {
                0x10000006u, 0, 0x11000000u, 0x50000006u,                     // cnt 6; VIF flush, direct 6
                0x00000004u, 0x10000000u, 0x0000000Eu, 0,                     // GIF tag: 4 loops, A+D
                0, 0, 0x51u, 0,                                                // TRXPOS 0
                small ? 8u : 16u, small ? 2u : 16u, 0x52u, 0,                  // TRXREG: the palette's size
                0, 0x00011E00u, 0x50u, 0,                                      // BITBLTBUF: block 0x1E00, width 1, 32-bit
                0, 0, 0x53u, 0,                                                // TRXDIR: host to local
                quarter + 0x8000u, 0x08000000u, 0, 0,                          // GIF tag: image, eop
                (entries >> 2) | 0x30000000u, palette & 0x7FFFFFFFu, 0, quarter | 0x50000000u, // ref the entries; direct
            });
            sprite->upload = sprite->upload + 4u * entries;
            texelBase += quarter;
            psm = small ? 0x14u : 0x13u;
            tileQwords >>= small ? 3 : 2;
        }
        p = emitWords(p, {
            0x10000004u, 0, 0, 0x50000004u,                                    // cnt 4; VIF direct 4
            0x00008003u, 0x10000000u, 0x0000000Eu, 0,                          // GIF tag: 3 loops, eop, A+D
            image->tileWidth, image->tileHeight, 0x52u, 0,                     // TRXREG: one tile
            5, 0, 0x08u, 0,                                                    // CLAMP_1: clamp
            sprite->blend != 0 ? 0x56u : 0x16u, 0, 0, 0,                       // PRIM: textured sprite (blended)
        });
        sprite->upload = sprite->upload + image->tilesAcross * (image->tileWords * sprite->frameY) * 4u;

        const uint32_t bitblt = texelBase + (psm << 24) + (static_cast<uint32_t>(bufferWidth) << 16);
        const uint64_t tex0 = (texelBase & 0x3FFFu) | (static_cast<uint64_t>(bufferWidth & 0x3F) << 14) | (static_cast<uint64_t>(psm & 0x3Fu) << 20) |
                              (static_cast<uint64_t>(log2Width & 0xF) << 26) | (static_cast<uint64_t>(log2Height & 0xF) << 30) |
                              (1ull << 34) | (0x1E00ull << 37) | (2ull << 61); // TCC 1, CBP 0x1E00, CLD 2
        for (uint32_t row = sprite->frameY; row < sprite->clipHeight; ++row)
        {
            const uint32_t top = row * drawHeight;
            sprite->upload = sprite->upload + image->tileWords * sprite->frameX * 4u;
            for (uint32_t column = sprite->frameX; column < sprite->clipWidth; ++column)
            {
                const uint32_t left = static_cast<uint32_t>(sprite->x) + column * drawWidth;
                const uint32_t y0 = (static_cast<uint32_t>(sprite->y) + top) << 3, x0 = left << 4;
                const uint32_t y1 = y0 + (drawHeight << 3), x1 = x0 + (drawWidth << 4);
                p = emitWords(p, {
                    0x10000004u, 0, 0x11000000u, 0x50000004u,                  // cnt 4; flush, direct 4
                    0x00000002u, 0x10000000u, 0x0000000Eu, 0,                  // GIF tag: 2 loops, A+D
                    0, bitblt, 0x50u, 0,                                        // BITBLTBUF: above the palette
                    0, 0, 0x53u, 0,                                             // TRXDIR
                    tileQwords + 0x8000u, 0x08000000u, 0, 0,                    // GIF tag: image, eop
                    tileQwords | 0x30000000u, sprite->upload & 0x7FFFFFFFu, 0, tileQwords | 0x50000000u, // ref the texels
                    0x10000002u, 0, 0, 0x50000002u,                             // cnt 2; direct 2
                    0x00008001u, 0x10000000u, 0x0000000Eu, 0,                   // GIF tag: 1 loop, eop, A+D
                    0, 0, 0x3Fu, 0,                                             // TEXFLUSH
                    0x10000008u, 0, 0x11000000u, 0x50000008u,                   // cnt 8; flush, direct 8
                    0x00008007u, 0x10000000u, 0x0000000Eu, 0,                   // GIF tag: 7 loops, eop, A+D
                    0, 0, 0x3Fu, 0,                                             // TEXFLUSH
                    0x80808080u, 0x3F800000u, 0x01u, 0,                         // RGBAQ: grey, Q 1
                    static_cast<uint32_t>(tex0), static_cast<uint32_t>(tex0 >> 32), 0x06u, 0, // TEX0_1
                    0, 0, 0x02u, 0,                                             // ST 0, 0
                    ((y0 + 0x7900u) << 16) | (x0 + 0x6C00u), static_cast<uint32_t>(sprite->z), 0x05u, 0, // XYZ2
                    0x3F800000u, 0x3F800000u, 0x02u, 0,                         // ST 1, 1
                    ((y1 + 0x7900u) << 16) | (x1 + 0x6C00u), static_cast<uint32_t>(sprite->z), 0x05u, 0, // XYZ2
                });
                sprite->upload = sprite->upload + image->tileWords * 4u;
            }
            sprite->upload = sprite->upload + image->tileWords * (image->tilesAcross - sprite->clipWidth) * 4u;
        }
        emitWords(p, {
            0x10000002u, 0, 0x11000000u, 0x50000002u,                          // cnt 2; flush, direct 2
            0x00008001u, 0x10000000u, 0x0000000Eu, 0,                          // GIF tag: 1 loop, eop, A+D
            0, 0, 0x08u, 0,                                                    // CLAMP_1: repeat
            0x60000000u, 0, 0, 0,                                              // ret
        });
        return static_cast<int>(drawHeight);
    }

    // The first tile shown, kept within the image, with the shown tiles trimmed to fit past it; then the packet
    // is rebuilt (its result passed on).
    int TexSprite_SetFrame(GuestPtr<TexSprite> sprite, int frameX, int frameY)
    {
        const GuestPtr<TexFileHeader> image = sprite->image;
        sprite->frameX = static_cast<uint32_t>(frameX);
        sprite->frameY = static_cast<uint32_t>(frameY);
        if (!(sprite->frameX < image->tilesAcross))
            sprite->frameX = image->tilesAcross - 1u;
        if (!(sprite->frameY < image->tilesDown))
            sprite->frameY = image->tilesDown - 1u;
        if (image->tilesAcross < sprite->frameX + sprite->clipWidth)
            sprite->clipWidth = image->tilesAcross - sprite->frameX;
        if (image->tilesDown < sprite->frameY + sprite->clipHeight)
            sprite->clipHeight = image->tilesDown - sprite->frameY;
        return TexSprite_BuildPacket(sprite);
    }

    // The tiles shown: all past the first tile, or `width` and `height` when non-zero and fewer; then the packet
    // is rebuilt (its result passed on).
    int TexSprite_SetClip(GuestPtr<TexSprite> sprite, uint32_t width, uint32_t height)
    {
        const GuestPtr<TexFileHeader> image = sprite->image;
        sprite->clipWidth = image->tilesAcross - sprite->frameX;
        sprite->clipHeight = image->tilesDown - sprite->frameY;
        if (width != 0 && width < image->tilesAcross - sprite->frameX)
            sprite->clipWidth = width;
        if (height != 0 && height < image->tilesDown - sprite->frameY)
            sprite->clipHeight = height;
        return TexSprite_BuildPacket(sprite);
    }

    void LayerF_Draw(GuestPtr<LayerF> layer, int packet)
    {
        Dma_AppendCall(static_cast<int>(layer->packet));
        Dma_AppendCall(packet);
    }

    void LayerB_Draw(GuestPtr<LayerB> layer, int packet)
    {
        Dma_AppendCall(static_cast<int>(layer->packetA));
        Dma_AppendCall(packet);
        Dma_AppendCall(static_cast<int>(layer->packetB));
        Dma_AppendCall(packet);
    }

    // The three sky packets built into the city camera.
    void Sky_Draw(GuestPtr<CityCamera> camera)
    {
        Dma_AppendCall(static_cast<int>(camera.address + 0x310u));
        Dma_AppendCall(static_cast<int>(camera.address + 0x470u));
        Dma_AppendCall(static_cast<int>(camera.address + 0x3D0u));
    }

    // City render passes; the frame parity (0x1BDCD0) picks one of two packet sets.
    void CityRender_DrawInstanceLayer(GuestPtr<CityMap> map) { LayerA_Draw(map->instances); }

    void CityRender_DrawWater(GuestPtr<CityMap> map, GuestPtr<CityScene> city)
    {
        LayerG_Draw(map->water, static_cast<int>(port::load<uint32_t>(0x001BDCD0u) & 1u), city->waterLevel);
    }

    void CityRender_DrawBuildings(GuestPtr<CityMap> map, int pass)
    {
        const uint32_t parity = port::load<uint32_t>(0x001BDCD0u) & 1u;
        LayerC_DrawBuildings(map->buildings, static_cast<int>(map->packets[parity].address), map, pass);
    }

    void Map_SetPlacementHighlight(GuestPtr<CityMap> map, int mode, GuestPtr<int32_t> rect, GuestPtr<uint32_t> colour)
    {
        Terrain_SetVec(GuestPtr<uint32_t>{map->buildings.address}, mode, GuestPtr<uint32_t>{rect.address}, colour);
    }

    // Static overlay packets, then the two-packet layers B and F with this frame's packet.
    void CityRender_DrawOverlays(GuestPtr<CityMap> map, GuestPtr<CityScene>)
    {
        const uint32_t parity = port::load<uint32_t>(0x001BDCD0u) & 1u;
        const int packet = static_cast<int>(map->packets[parity].address);
        Dma_AppendCall(static_cast<int>(map->packets[2].address));
        Dma_AppendCall(packet);
        Dma_AppendCall(static_cast<int>(map->packets[parity + 3].address));
        LayerB_Draw(map->layerB, packet);
        LayerF_Draw(map->layerF, packet);
    }

    // ---- model instances

    // A model's node matrices: each node's local matrix (+0x10) times its parent's world matrix (+0x70), or
    // the model's matrix for a root node, into the node's world matrix. A parent whose last word (+0xAC) is
    // still 0 is brought up to date first (A3DObject_UpdateNodeMatrix); every node's last word is cleared
    // before.
    // A node's world matrix (+0x70): its local matrix (+0x10) times its parent's world matrix, or the model's for a
    // root. A parent whose world matrix is not done yet (+0xAC still 0) is done first, its own parent recursively.
    // (The original multiplies inline with the same VU0 registers as Mat4_MulVu0.)
    void A3DObject_UpdateNodeMatrix(GuestPtr<A3DObject> model, GuestPtr<int32_t> node)
    {
        const auto world = [](uint32_t of) { return GuestPtr<Vec4>{of + 0x70u}; };
        const auto local = [](uint32_t of) { return GuestPtr<Vec4>{of + 0x10u}; };
        const auto done = [](uint32_t of) { return !(ps2f(port::load<float>(of + 0xACu)) == ps2f(0.0f)); };
        const uint32_t self = node.address;
        const uint32_t parent = port::load<uint32_t>(self);
        if (parent == 0)
        {
            Mat4_MulVu0(world(self), local(self), GuestPtr<Vec4>::of(model->matrix[0]));
            return;
        }
        if (!done(parent))
        {
            const uint32_t grandparent = port::load<uint32_t>(parent);
            if (grandparent == 0)
                Mat4_MulVu0(world(parent), local(parent), GuestPtr<Vec4>::of(model->matrix[0]));
            else
            {
                if (!done(grandparent))
                    A3DObject_UpdateNodeMatrix(model, GuestPtr<int32_t>{grandparent});
                Mat4_MulVu0(world(parent), local(parent), world(grandparent));
            }
        }
        Mat4_MulVu0(world(self), local(self), world(parent));
    }

    void A3DObject_UpdateMatrices(GuestPtr<A3DObject> model)
    {
        const auto count = [&] { return port::load<int16_t>(model->model.address + 0x18u); };
        const auto nodes = [&] { return port::load<uint32_t>(model->model.address + 8u); };
        for (int i = 0; i < count(); ++i)
            port::store<int32_t>(nodes() + static_cast<uint32_t>(i) * 0xC0u + 0xACu, 0);
        for (int i = 0; i < count(); ++i)
        {
            const uint32_t node = nodes() + static_cast<uint32_t>(i) * 0xC0u;
            const uint32_t parent = port::load<uint32_t>(node);
            if (parent == 0)
            {
                Mat4_MulVu0(GuestPtr<Vec4>{node + 0x70u}, GuestPtr<Vec4>{node + 0x10u}, GuestPtr<Vec4>::of(model->matrix[0]));
                continue;
            }
            if (ps2f(port::load<float>(parent + 0xACu)) == ps2f(0.0f))
                A3DObject_UpdateNodeMatrix(model, GuestPtr<int32_t>{parent});
            Mat4_MulVu0(GuestPtr<Vec4>{node + 0x70u}, GuestPtr<Vec4>{node + 0x10u}, GuestPtr<Vec4>{parent + 0x70u});
        }
    }

    // A whole model: every node's matrix is cleared in its last word (+0xAC) and updated, then every node
    // with a mesh is queued with its mesh. The node count is read again on every step, as in the original.
    void A3DObject_Draw(GuestPtr<A3DObject> model)
    {
        const auto count = [&] { return port::load<int16_t>(model->model.address + 0x18u); };
        const auto nodes = [&] { return port::load<uint32_t>(model->model.address + 8u); };
        for (int i = 0; i < count(); ++i)
            port::store<int32_t>(nodes() + static_cast<uint32_t>(i) * 0xC0u + 0xACu, 0);
        for (int i = 0; i < count(); ++i)
            A3DObject_UpdateNodeMatrix(model, GuestPtr<int32_t>{nodes() + static_cast<uint32_t>(i) * 0xC0u});
        const uint32_t first = nodes();
        for (int i = 0; i < count(); ++i)
        {
            const uint32_t node = first + static_cast<uint32_t>(i) * 0xC0u;
            const uint32_t mesh = port::load<uint32_t>(node + 4u);
            if (mesh != 0)
            {
                Dma_AppendCall(static_cast<int>(node + 0x50u));
                Dma_AppendCall(static_cast<int>(mesh + 0x10u));
            }
        }
    }

    // One node of a model: the node's own packet (nodes are 0xC0 bytes, packet at +0x50), then its mesh's
    // packet (+0x10) if it has a mesh.
    void A3DObject_DrawNode(GuestPtr<A3DObject> model, int node)
    {
        const uint32_t offset = static_cast<uint32_t>(node) * 0xC0u;
        Dma_AppendCall(static_cast<int>(port::load<uint32_t>(model->model.address + 8u) + offset + 0x50u));
        const uint32_t mesh = port::load<uint32_t>(port::load<uint32_t>(model->model.address + 8u) + offset + 4u);
        if (mesh != 0)
            Dma_AppendCall(static_cast<int>(mesh + 0x10u));
    }

    GuestPtr<A3DObject> InstanceBase_ctor(GuestPtr<A3DObject> model)
    {
        BaseObject_ctor(static_cast<int>(model.address));
        port::store<uint32_t>(0x001BDA10u, 0);
        return model;
    }

    // Keyframe animation `anim` of a model at `time`. Each node track (0x10 bytes: key count, node, keys) holds keys
    // of 0x20 bytes (position xyz, time, rotation quaternion xyzw); the pair around the time sets the node's
    // matrix (rotation rows at +0x10, position at +0x40). The quaternions are taken the short way round (the later
    // key is negated in place when the dot product is below 0) and slerped, or blended linearly when nearly
    // equal (1 - dot <= 0.001); the position is blended linearly. Instance_DrawSwaying2 is the same code, only
    // skipping tracks without keys a step earlier.
    void Instance_DrawSwaying(GuestPtr<A3DObject> model, int anim, ps2f time)
    {
        uint32_t track = port::load<uint32_t>(model->meshes.address + 4u * static_cast<uint32_t>(anim));
        for (int i = 0; i < port::load<int32_t>(model.address + 8u); ++i, track += 0x10u)
        {
            const int keys = port::load<int32_t>(track);
            const GuestPtr<ps2f> node{port::load<uint32_t>(track + 4u)};
            uint32_t key = port::load<uint32_t>(track + 8u);
            for (int k = 0; k < keys - 1; ++k, key += 0x20u)
            {
                const GuestPtr<ps2f> a{key}, b{key + 0x20u};
                if (!(time <= b[3]))
                    continue;
                const ps2f t = (time - a[3]) / (b[3] - a[3]);
                ps2f dot = (a[6] * b[6] + (a[4] * b[4] + a[5] * b[5])) + a[7] * b[7];
                if (dot < ps2f(0.0f))
                {
                    b[4] = -b[4];
                    dot = -dot;
                    b[5] = -b[5];
                    b[6] = -b[6];
                    b[7] = -b[7];
                }
                const ps2f rest = ps2f(1.0f) - t;
                ps2f weightA = rest, weightB = t;
                if (!(ps2f(1.0f) - dot <= ps2f(0.001f)))
                {
                    const ps2f angle = sdk::acosf(dot);
                    const ps2f partA = sdk::sinf(angle * weightA);
                    weightA = partA / sdk::sinf(angle);
                    const ps2f partB = sdk::sinf(angle * t);
                    weightB = partB / sdk::sinf(angle);
                }
                const ps2f x = weightA * a[4] + weightB * b[4];
                const ps2f y = weightA * a[5] + weightB * b[5];
                const ps2f z = weightA * a[6] + weightB * b[6];
                const ps2f w = weightA * a[7] + weightB * b[7];
                const ps2f yy = y * y, xy = x * y, yz = y * z, wy = w * y, zz = z * z, wx = w * x, wz = w * z, xx = x * x, xz = x * z;
                const ps2f two = 2.0f, one = 1.0f;
                node[4] = one - two * (yy + zz);
                node[5] = two * (xy - wz);
                node[6] = two * (xz + wy);
                node[8] = two * (xy + wz);
                node[9] = one - two * (xx + zz);
                node[10] = two * (yz - wx);
                node[12] = two * (xz - wy);
                node[13] = two * (yz + wx);
                node[14] = one - two * (xx + yy);
                node[15] = 0.0f;
                node[11] = 0.0f;
                node[7] = 0.0f;
                node[16] = rest * a[0] + t * b[0];
                node[17] = rest * a[1] + t * b[1];
                node[18] = rest * a[2] + t * b[2];
                node[19] = one;
                break;
            }
        }
    }

    void Instance_DrawSwaying2(GuestPtr<A3DObject> model, int anim, ps2f time) { Instance_DrawSwaying(model, anim, time); }

    // Draws a swaying model only while its nodes still fit in this frame's instance slots.
    void Instance_DrawSwayingIfRoom(GuestPtr<A3DObject> model, int a1, ps2f phase)
    {
        const int16_t nodes = port::load<int16_t>(model->model.address + 0x18u);
        if (nodes < 0x708 - globals::instanceCount.get())
            Instance_DrawSwaying(model, a1, phase);
    }

    namespace
    {
        constexpr int kMaxInstances = 0x708;

        uint32_t modelNodes(GuestPtr<A3DObject> model) { return port::load<uint32_t>(model->model.address + 8u); }

        // Fills the next instance slot with a matrix, flags and colour and queues it.
        void queueSlot(GuestPtr<Vec4> matrix, GuestPtr<Vec4> colour, int flags)
        {
            const uint32_t buffer = port::load<uint32_t>(globals::kInstanceBuffers + 4u * static_cast<uint32_t>(globals::instanceBuffer.get()));
            const GuestPtr<InstanceSlot> slot = GuestPtr<InstanceSlot>{buffer} + globals::instanceCount.get();
            for (int row = 0; row < 4; ++row)
                slot->matrix[row] = matrix[row];
            port::store<int32_t>(slot.address + 0x5Cu, flags);
            slot->colour = *colour;
            Dma_AppendCall(static_cast<int>(slot.address));
        }

        // An instance slot, then the mesh's packet (the node's +4, when set).
        void queueInstance(GuestPtr<A3DObject> model, GuestPtr<Vec4> matrix, GuestPtr<Vec4> colour, int mesh, int flags)
        {
            queueSlot(matrix, colour, flags);
            const uint32_t meshData = port::load<uint32_t>(modelNodes(model) + static_cast<uint32_t>(mesh) * 0xC0u + 4u);
            if (meshData != 0)
                Dma_AppendCall(static_cast<int>(meshData + 0x10u));
            globals::instanceCount.set(globals::instanceCount.get() + 1);
        }

        // For models whose nodes hold mesh indexes at +4 (-1 = none): an instance slot, then the packet of mesh
        // `index` from the model data's mesh table (+0xC).
        void queueTableMesh(GuestPtr<A3DObject> model, GuestPtr<Vec4> matrix, GuestPtr<Vec4> colour, int index, int flags)
        {
            queueSlot(matrix, colour, flags);
            const uint32_t table = port::load<uint32_t>(model->model.address + 0xCu);
            Dma_AppendCall(static_cast<int>(port::load<uint32_t>(table + 4u * static_cast<uint32_t>(index)) + 0x10u));
            globals::instanceCount.set(globals::instanceCount.get() + 1);
        }

        // A node's own matrix (nodes are 0xC0 bytes, matrix at +0x70).
        GuestPtr<Vec4> nodeMatrix(GuestPtr<A3DObject> model, int node)
        {
            return GuestPtr<Vec4>{modelNodes(model) + static_cast<uint32_t>(node) * 0xC0u + 0x70u};
        }
    }

    void Instance_DrawMesh(GuestPtr<A3DObject> model, GuestPtr<Vec4> matrix, GuestPtr<Vec4> colour, int mesh, int flags)
    {
        if (globals::instanceCount.get() < kMaxInstances)
            queueInstance(model, matrix, colour, mesh, flags);
    }

    void Instance_DrawNode(GuestPtr<A3DObject> model, GuestPtr<Vec4> colour, int node, int flags)
    {
        if (globals::instanceCount.get() < kMaxInstances)
            queueInstance(model, nodeMatrix(model, node), colour, node, flags);
    }

    // Once a frame: the second instance limit follows the frame load, dropping by 12 per percent above 95,
    // else rising by 1, within 255..935 (the second count restarts).
    void Perf_AdaptDrawRange()
    {
        globals::instanceLimitedCount.set(0);
        const ps2f load = globals::frameLoad.get();
        if (load <= ps2f(95.0f))
            globals::instanceLimit.set(globals::instanceLimit.get() + 1);
        else
            globals::instanceLimit.set(sdk::fptosi(fromInt(globals::instanceLimit.get()) - ps2f(12.0f) * (load - ps2f(95.0f))));
        const int limit = globals::instanceLimit.get();
        globals::instanceLimit.set(limit < 0xFF ? 0xFF : (limit < 0x3A8 ? limit : 0x3A7));
    }

    // A whole model as instances, when its nodes plus `reserve` still fit in this frame's slots: matrices
    // updated, then every node with a mesh queued with its world matrix, until the slots run out.
    void Instance_DrawModel(GuestPtr<A3DObject> model, GuestPtr<Vec4> colour, int reserve, int flags)
    {
        const auto count = [&] { return port::load<int16_t>(model->model.address + 0x18u); };
        if (count() + reserve >= kMaxInstances - globals::instanceCount.get())
            return;
        A3DObject_UpdateMatrices(model);
        const uint32_t nodes = modelNodes(model);
        for (int i = 0; i < count(); ++i)
        {
            const uint32_t node = nodes + static_cast<uint32_t>(i) * 0xC0u;
            if (port::load<uint32_t>(node + 4u) == 0)
                continue;
            queueInstance(model, GuestPtr<Vec4>{node + 0x70u}, colour, i, flags);
            if (globals::instanceCount.get() >= kMaxInstances)
                return;
        }
    }

    // A model whose nodes hold mesh indexes, with no room check (the callers make it): nodes 0 and 1, and node 2
    // for style 0, each with its own mesh, node 0's offset by `variant`; then each later node that has a mesh,
    // with the meshes of the style's group of 13 in turn. Flags 0. The second count rises by the node count.
    // Used for the player (mm_Player_Draw: variant = its mode, style = the bag's item).
    void Instance_DrawModelLit(GuestPtr<A3DObject> model, GuestPtr<Vec4> colour, int variant, int style)
    {
        const uint32_t nodes = modelNodes(model);
        const auto node = [&](int i) { return nodes + static_cast<uint32_t>(i) * 0xC0u; };
        const auto matrix = [&](int i) { return GuestPtr<Vec4>{node(i) + 0x70u}; };
        const auto meshOf = [&](int i) { return port::load<int32_t>(node(i) + 4u); };
        const auto count = [&] { return port::load<int16_t>(model->model.address + 0x18u); };
        queueTableMesh(model, matrix(0), colour, meshOf(0) + variant, 0);
        queueTableMesh(model, matrix(1), colour, meshOf(1), 0);
        if (style == 0)
            queueTableMesh(model, matrix(2), colour, meshOf(2), 0);
        int mesh = style * 13;
        for (int i = 0; i < count() - 3; ++i)
        {
            if (meshOf(3 + i) == -1)
                continue;
            queueTableMesh(model, matrix(3 + i), colour, mesh, 0);
            ++mesh;
        }
        globals::instanceLimitedCount.set(globals::instanceLimitedCount.get() + count());
    }

    // The same kind of model with the offsets passed in, all with `flags`: node 0 with its mesh + `offset0`;
    // node 1 with its mesh + `offset1` unless that is 4 or more; when `attach` is set, mesh 0 of the part model
    // at 0x1F3B80 (also kept at 0x1BDA38) at node 2; node 3 with its mesh + `offset3`; then each later node that
    // has a mesh, with mesh (its place from node 4) + group x the group size at +0x60 (past the A3DObject base).
    // No room check; the second count rises by the node count.
    void Instance_DrawModelEx(GuestPtr<A3DObject> model, GuestPtr<Vec4> colour, int flags, int offset0, int group, int offset3,
                              int attach, int offset1)
    {
        const uint32_t nodes = modelNodes(model);
        const auto node = [&](int i) { return nodes + static_cast<uint32_t>(i) * 0xC0u; };
        const auto matrix = [&](int i) { return GuestPtr<Vec4>{node(i) + 0x70u}; };
        const auto meshOf = [&](int i) { return port::load<int32_t>(node(i) + 4u); };
        const auto count = [&] { return port::load<int16_t>(model->model.address + 0x18u); };
        queueTableMesh(model, matrix(0), colour, meshOf(0) + offset0, flags);
        if (offset1 < 4)
            queueTableMesh(model, matrix(1), colour, meshOf(1) + offset1, flags);
        if (attach != 0)
        {
            const uint32_t part = port::load<uint32_t>(0x001F3B80u);
            port::store<uint32_t>(0x001BDA38u, part);
            Instance_DrawMesh(GuestPtr<A3DObject>{part}, matrix(2), colour, 0, flags);
        }
        queueTableMesh(model, matrix(3), colour, meshOf(3) + offset3, flags);
        for (int i = 0; i < count() - 4; ++i)
        {
            if (meshOf(4 + i) == -1)
                continue;
            queueTableMesh(model, matrix(4 + i), colour, i + group * port::load<int32_t>(model.address + 0x60u), flags);
        }
        globals::instanceLimitedCount.set(globals::instanceLimitedCount.get() + count());
    }

    // As Instance_DrawNode, also within a second count and limit.
    void Instance_DrawNodeLimited(GuestPtr<A3DObject> model, GuestPtr<Vec4> colour, int node, int flags)
    {
        if (globals::instanceCount.get() >= kMaxInstances || globals::instanceLimitedCount.get() >= globals::instanceLimit.get())
            return;
        queueInstance(model, nodeMatrix(model, node), colour, node, flags);
        globals::instanceLimitedCount.set(globals::instanceLimitedCount.get() + 1);
    }

    // The light set's two VU1 packets, one per render program: each uploads the set's 26 quadwords from +0x10 to
    // VU1 address 0x12, calls the program's DMA chain and sets the VIF double-buffer base and offset.
    void LightSet_BuildPackets(GuestPtr<LightSet> lights)
    {
        const uint32_t data = (lights.address + 0x10u) & 0x7FFFFFFFu;
        const auto build = [&](uint32_t chain, uint32_t base, uint32_t offset) {
            const GuestPtr<uint32_t> p{static_cast<uint32_t>(PacketBuf_Tell())};
            const uint32_t words[20] = {
                0x10000000u, 0, 0, 0x11000000u,                   // cnt 0; VIF flush
                0x3000001Au, data, 0x01000101u, 0x6C1A0012u,      // ref 26 quadwords; stcycl, unpack v4-32 to 0x12
                0x10000000u, 0, 0, 0x11000000u,                   // cnt 0; flush
                0x50000000u, chain, 0, 0,                         // call the program's chain
                0x60000000u, 0, 0x03000000u | base, 0x02000000u | offset, // ret; VIF base, offset
            };
            for (uint32_t i = 0; i < 20; ++i)
                p[i] = words[i];
            PacketBuf_Seek(static_cast<int>((p + 20).address));
            return p.address;
        };
        lights->packetA = build(0x001A3C80u, 0x42, 0x1DF);
        lights->packetB = build(0x001A4780u, 0xEA, 0x18B);
    }

    // Light set base setup: 0x110 and 0xE0 at +0x190/+0x194, every colour of both groups (twelve vectors each from
    // +0x10 and +0xD0) and the vector at +0x1A0 set to 1, then its image-upload packet. Returns the set.
    int LightSet_Init(GuestPtr<LightSet> lights)
    {
        BaseObject_ctor(static_cast<int>(lights.address));
        port::store<int32_t>(lights.address + 0x190u, 0x110);
        port::store<int32_t>(lights.address + 0x194u, 0xE0);
        const GuestPtr<Vec4> groupA{lights.address + 0x10u}, groupB{lights.address + 0xD0u};
        for (int i = 0; i < 12; ++i)
            for (const GuestPtr<Vec4> colour : {groupA + i, groupB + i})
            {
                colour->w = 1.0f;
                colour->z = 1.0f;
                colour->y = 1.0f;
                colour->x = 1.0f;
            }
        const GuestPtr<Vec4> last{lights.address + 0x1A0u};
        last->w = 1.0f;
        last->z = 1.0f;
        last->y = 1.0f;
        last->x = 1.0f;
        LightSet_BuildPackets(lights);
        return static_cast<int>(lights.address);
    }

    GuestPtr<LightSet> LightSet_ctor(GuestPtr<LightSet> lights)
    {
        LightSet_Init(lights);
        return lights;
    }

    // Building layer: whether a tile lies in the layer's active rectangle.
    int LayerC_InTileRect(GuestPtr<LayerC> layer, int x, int z)
    {
        if (layer->rectActive != 1)
            return 0;
        return x >= layer->rectX0 && x <= layer->rectX1 && z >= layer->rectZ0 && z <= layer->rectZ1;
    }

    // Switches to the other of the two DMA lists.
    void DmaList_Flip()
    {
        const uint32_t index = globals::dmaListIndex.get() ^ 1u;
        globals::dmaListIndex.set(index);
        globals::dmaListPtr.set(port::load<uint32_t>(globals::kDmaLists + 4u * index));
    }

    // Ends the DMA list being built (a REF tag to the static end packet, then an END tag, unless it is already 0x4000
    // words long) and starts it on the VIF1 channel: QWC cleared, the tag address set to the list start, the DMA status
    // bit 1 acknowledged and the channel started in chain mode (0x145) after the cache is flushed.
    void DmaList_KickFrame()
    {
        constexpr uint32_t kQwc = 0x10009020u, kTadr = 0x10009030u, kChcr = 0x10009000u, kStat = 0x1000E010u;
        const uint32_t start = port::load<uint32_t>(globals::kDmaLists + 4u * globals::dmaListIndex.get());
        uint32_t cursor = globals::dmaListPtr.get();
        if (static_cast<int32_t>(cursor - start) / 4 < 0x4000)
        {
            port::store<uint32_t>(cursor + 0xCu, 0u);
            port::store<uint32_t>(cursor + 8u, 0u);
            port::store<uint32_t>(cursor + 4u, 0x001B2760u & 0x7FFFFFFFu);
            port::store<uint32_t>(cursor, 0x50000000u);
            port::store<uint32_t>(cursor + 0x1Cu, 0u);
            port::store<uint32_t>(cursor + 0x18u, 0u);
            port::store<uint32_t>(cursor + 0x14u, 0u);
            port::store<uint32_t>(cursor + 0x10u, 0x70000000u);
            cursor += 0x10u;
            globals::dmaListPtr.set(cursor);
        }
        sdk::FlushCache(0);
        port::writeHardware32(kQwc, 0u);
        port::writeHardware32(kTadr, start & 0x7FFFFFFFu);
        port::writeHardware32(kStat, 2u);
        // MMPC_FRAME_STATS=1: every 2 seconds, frames drawn, the time the kick took (the runtime runs the whole VIF1 chain, VU1 and
        // GS submission inside this store), and how many frames came 33 ms, 50 ms or later after the one before.
        static const bool statsOn = [] {
            const char *value = std::getenv("MMPC_FRAME_STATS");
            return value != nullptr && value[0] == '1';
        }();
        if (!statsOn)
        {
            port::writeHardware32(kChcr, 0x145u);
            return;
        }
        struct FrameStats
        {
            std::chrono::steady_clock::time_point windowStart = std::chrono::steady_clock::now(), lastKick{};
            uint32_t frames = 0, within34 = 0, within51 = 0, later = 0;
            double kickSeconds = 0.0, worstKick = 0.0;
        };
        static FrameStats stats;
        const auto begin = std::chrono::steady_clock::now();
        if (stats.lastKick.time_since_epoch().count() != 0)
        {
            const double interval = std::chrono::duration<double>(begin - stats.lastKick).count();
            (interval <= 0.0345 ? stats.within34 : interval <= 0.0512 ? stats.within51 : stats.later)++;
        }
        stats.lastKick = begin;
        port::writeHardware32(kChcr, 0x145u);
        const auto end = std::chrono::steady_clock::now();
        const double kick = std::chrono::duration<double>(end - begin).count();
        ++stats.frames;
        stats.kickSeconds += kick;
        stats.worstKick = std::max(stats.worstKick, kick);
        if (end - stats.windowStart >= std::chrono::seconds(2))
        {
            std::fprintf(stderr, "[frames] %u in %.1f s (%.1f fps), kick %.1f ms average / %.1f ms worst; intervals <=34 ms: %u, <=51 ms: %u, later: %u\n",
                         stats.frames, std::chrono::duration<double>(end - stats.windowStart).count(),
                         stats.frames / std::chrono::duration<double>(end - stats.windowStart).count(), 1000.0 * stats.kickSeconds / stats.frames,
                         1000.0 * stats.worstKick, stats.within34, stats.within51, stats.later);
            stats = FrameStats{};
            stats.windowStart = end;
            stats.lastKick = end;
        }
    }

    void PacketBuf_Seek(int address) { globals::packetBufPos.set(address); }
    int PacketBuf_Tell() { return globals::packetBufPos.get(); }

    void LayerA_SetSource(GuestPtr<LayerA> layer, int source)
    {
        layer->source = source;
        layer->count = 0;
    }

    // The light colours for the time of day: blended between the colours at the start of the current 2-hour
    // segment and the next (the second table set in rain or snow). Both groups get fixed parameters.
    void LightSet_UpdateByClock(GuestPtr<LightSet> lights, int clock, int weather)
    {
        const uint32_t set = weather != 0 ? 0x90u : 0u;
        const ps2f t = sdk::fmodf(fromInt(clock), 7200.0f);
        const int segment = clock / 7200;
        int next = segment + 1;
        if (next >= 12)
            next -= 12;
        const ps2f rest = ps2f(7200.0f) - t;
        const auto blend = [&](uint32_t table, uint32_t component) {
            const ps2f now = GuestPtr<ps2f>{table + set + 12u * static_cast<uint32_t>(segment) + 4u * component}[0];
            const ps2f then = GuestPtr<ps2f>{table + set + 12u * static_cast<uint32_t>(next) + 4u * component}[0];
            return (rest * now + t * then) / ps2f(7200.0f);
        };
        lights->colourA.x = blend(globals::kLightColoursA, 0);
        lights->colourA.y = blend(globals::kLightColoursA, 1);
        lights->colourA.z = blend(globals::kLightColoursA, 2);
        lights->colourA.w = 1.0f;
        lights->colourB.x = blend(globals::kLightColoursB, 0);
        lights->colourB.y = blend(globals::kLightColoursB, 1);
        lights->colourB.z = blend(globals::kLightColoursB, 2);
        lights->colourB.w = 1.0f;
        lights->colourB2 = lights->colourB;
        for (Vec4 *params : {&lights->paramsA, &lights->paramsB})
        {
            params->x = 4.0f;
            params->y = 1.0f;
            params->z = 1.0f;
            params->w = 2.0f;
        }
    }

    void LightSet_SetField20(GuestPtr<LightSet> lights, int value) { port::store<int32_t>(lights->packetA + 0x20u, value); }

    PORT_FN(DmaList_BeginScene);
    PORT_FN(DmaList_KickFrame);
    PORT_FN(Camera_BuildPacket);
    PORT_FN(LayerB_ctor);
    PORT_FN(LayerC_ctor);
    PORT_FN(LayerD_ctor);
    PORT_FN(LayerE_ctor);
    PORT_FN(LayerF_ctor);
    PORT_FN(LayerG_ctor);
    PORT_FN(LayerG_AnimateWaves);
    PORT_FN(CityRender_DrawOverlays);
    PORT_FN(A3DObject_DrawNode);
    PORT_FN(A3DObject_Draw);
    PORT_FN(A3DObject_UpdateNodeMatrix);
    PORT_FN(A3DObject_UpdateMatrices);
    PORT_FN(Packet_Draw);
    PORT_FN(Packet2_Draw);
    PORT_FN(TexSprite_Draw);
    PORT_FN(TexSprite_BuildPacket);
    PORT_FN(TexImage_Convert);
    PORT_FN(DmaList_InitPackets);
    PORT_FN(TexSprite_SetFrame);
    PORT_FN(TexSprite_SetClip);
    PORT_FN(LayerF_Draw);
    PORT_FN(LayerB_Draw);
    PORT_FN(Sky_Draw);
    PORT_FN(CityRender_DrawInstanceLayer);
    PORT_FN(CityRender_DrawWater);
    PORT_FN(CityRender_DrawBuildings);
    PORT_FN(Map_SetPlacementHighlight);
    PORT_FN(InstanceBase_ctor);
    PORT_FN(Instance_DrawSwaying);
    PORT_FN(Instance_DrawSwaying2);
    PORT_FN(Instance_DrawSwayingIfRoom);
    PORT_FN(Instance_DrawMesh);
    PORT_FN(Instance_DrawNode);
    PORT_FN(Instance_DrawNodeLimited);
    PORT_FN(Instance_DrawModelLit);
    PORT_FN(Instance_DrawModelEx);
    PORT_FN(Instance_DrawModel);
    PORT_FN(Perf_AdaptDrawRange);
    PORT_FN(LightSet_BuildPackets);
    PORT_FN(LightSet_Init);
    PORT_FN(LightSet_ctor);
    PORT_FN(DmaList_QueueFade);
    PORT_FN(DmaChain_Next);
    PORT_FN(Dma_AppendCall);
    PORT_FN(Dma_AppendCall_14C200);
    PORT_FN(DmaList_QueueGsDefaults);
    PORT_FN(DmaList_QueueClearScreen);
    PORT_FN(DmaList_QueueClearZ);
    PORT_FN(LayerC_InTileRect);
    PORT_FN(A3D_GetMeshField0C);
    PORT_FN(DmaChain_Begin);
    PORT_FN(DmaList_Flip);
    PORT_FN(PacketBuf_Seek);
    PORT_FN(PacketBuf_Tell);
    PORT_FN(LayerA_SetSource);
    PORT_FN(LightSet_SetField20);
    PORT_FN(LightSet_UpdateByClock);

    // The other frame's 640 x 224 picture packet (the one not drawn into this frame).
    void DmaList_QueueOtherOverlay() { appendDmaCall(globals::kFramePictures + 0xBA0u * (globals::dmaListIndex.get() ^ 1u)); }

    PORT_FN(DmaList_QueueOtherOverlay);

    // ---- batch 38

    // The sky by the clock: pass 0 the sky layer, other passes layer E, each with this frame's packet (by the
    // parity of 0x1BDCD0).
    void CityRender_DrawSky(GuestPtr<CityMap> map, GuestPtr<CityScene> city, int pass, GuestPtr<Vec4> a3)
    {
        const uint32_t parity = port::load<uint32_t>(0x001BDCD0u) & 1u;
        const int packet = static_cast<int>(map->packets[parity].address);
        if (pass != 0)
            LayerE_Draw(map->layerE, static_cast<int>(parity), packet, city->clock, a3);
        else
            LayerD_DrawSkyByClock(map->sky, static_cast<int>(parity), packet, city->clock);
    }

    PORT_FN(CityRender_DrawSky);

    // ---- batch 38

    // The water: the waves move with the weather, then this frame's packet (+0x210, one per pass) is queued.
    void LayerG_Draw(GuestPtr<LayerG> layer, int pass, ps2f weather)
    {
        LayerG_AnimateWaves(layer, weather);
        Dma_AppendCall(static_cast<int>(port::load<uint32_t>(layer.address + 0x210u + 4u * static_cast<uint32_t>(pass))));
    }

    PORT_FN(LayerG_Draw);

    // ---- layer packets and draw parameter templates (batch 38)

    namespace
    {
        void putWords(uint32_t at, std::initializer_list<uint32_t> words)
        {
            for (const uint32_t word : words)
            {
                port::store<uint32_t>(at, word);
                at += 4u;
            }
        }

        void putFloats(uint32_t at, std::initializer_list<float> values)
        {
            for (const float value : values)
            {
                port::store<float>(at, value);
                at += 4u;
            }
        }

        // Texture coordinates (s, t, q 1, 0) for a quad drawn as two strips: the left half (s 0 to 0.5) top to
        // bottom, then the right half.
        void putHalvesStq(uint32_t at)
        {
            static constexpr float kST[12][2] = {{0, 0},    {0.5f, 0}, {0, 0.5f},    {0.5f, 0.5f}, {0, 1},    {0.5f, 1},
                                                 {0.5f, 0}, {1, 0},    {0.5f, 0.5f}, {1, 0.5f},    {0.5f, 1}, {1, 1}};
            for (const auto &st : kST)
            {
                putFloats(at, {st[0], st[1], 1.0f, 0.0f});
                at += 16u;
            }
        }
    }

    // Layer A's packets: the start (+0x2434) uploads the layer's two quadwords at +0x2410 to VU1 address 0x2A,
    // flushes, calls the chain at 0x1A8330 and sets the VIF base and offset; the end (+0x2438) sets ALPHA_1
    // (0x20_00000044) through the GIF.
    GuestPtr<uint32_t> LayerA_BuildPacket(GuestPtr<LayerA> layer, GuestPtr<uint32_t> packet)
    {
        port::store<uint32_t>(layer.address + 0x2434u, packet.address);
        putWords(packet.address, {
            0x30000002u, (layer.address + 0x2410u) & 0x7FFFFFFFu, 0x01000101u, 0x6C02002Au, // ref 2 quadwords; stcycl, unpack v4-32 to 0x2A
            0x10000000u, 0, 0, 0x11000000u,                                                 // cnt 0; VIF flush
            0x50000000u, 0x001A8330u, 0, 0,                                                 // call
            0x60000000u, 0, 0x03000042u, 0x020001DFu,                                       // ret; VIF base 0x42, offset 0x1DF
        });
        port::store<uint32_t>(layer.address + 0x2438u, packet.address + 0x40u);
        putWords(packet.address + 0x40u, {
            0x10000002u, 0, 0x11000000u, 0x50000002u, // cnt 2 quadwords; flush, direct 2
            0x8001u, 0x10000000u, 0xEu, 0,            // GIF tag: one A+D register
            0x44u, 0x20u, 0x42u, 0,                   // ALPHA_1
            0x60000000u, 0, 0, 0,                     // ret
        });
        return packet + 0x20;
    }

    // Layer A: the start packet, then for each of its entries (+0x2430; 0x90 bytes each, in banks of 32 picked by
    // +0x243C) the entry's packet (+0x60) and its two meshes (2n and 2n + 1 for n at +0x10) under its matrix
    // (+0x20), then the end packet.
    void LayerA_Draw(GuestPtr<LayerA> layer)
    {
        const uint32_t l = layer.address;
        if (port::load<int32_t>(l + 0x2430u) == 0)
            return;
        Dma_AppendCall(port::load<int32_t>(l + 0x2434u));
        for (int i = 0; i < port::load<int32_t>(l + 0x2430u); ++i)
        {
            const auto entry = [&] { return l + 0x1200u * port::load<uint32_t>(l + 0x243Cu) + 0x90u * static_cast<uint32_t>(i); };
            Dma_AppendCall(static_cast<int>(entry() + 0x60u));
            const auto model = [&] { return GuestPtr<A3DObject>{port::load<uint32_t>(l)}; };
            Instance_DrawMesh(model(), GuestPtr<Vec4>{entry() + 0x20u}, GuestPtr<Vec4>{globals::kDefaultColour}, port::load<int32_t>(entry() + 0x10u) * 2, 0);
            Instance_DrawMesh(model(), GuestPtr<Vec4>{entry() + 0x20u}, GuestPtr<Vec4>{globals::kDefaultColour}, port::load<int32_t>(entry() + 0x10u) * 2 + 1, 0);
        }
        Dma_AppendCall(port::load<int32_t>(l + 0x2438u));
    }

    // Layer B's packets: the first (+0x130) calls the chain at 0x1AA340, uploads the layer's 19 quadwords to VU1
    // 0x1C, sets the VIF base and offset and starts the program; the second (+0x220) uploads the vectors at
    // +0x210 and +0x200 to 0x1D and 0x2D and starts the program again.
    GuestPtr<uint32_t> LayerB_BuildPacket(GuestPtr<LayerB> layer, GuestPtr<uint32_t> packet)
    {
        const uint32_t l = layer.address;
        port::store<uint32_t>(l + 0x130u, packet.address);
        putWords(packet.address, {
            0x10000000u, 0, 0, 0x11000000u,                                  // cnt 0; VIF flush
            0x50000000u, 0x001AA340u, 0, 0,                                  // call
            0x30000013u, l & 0x7FFFFFFFu, 0x01000101u, 0x6C13001Cu,          // ref 19 quadwords; unpack to 0x1C
            0x10000000u, 0, 0x03000056u, 0x020001D5u,                        // VIF base 0x56, offset 0x1D5
            0x10000000u, 0, 0x14000000u, 0x11000000u,                        // mscal; flush
            0x60000000u, 0, 0, 0,                                            // ret
        });
        port::store<uint32_t>(l + 0x220u, packet.address + 0x60u);
        putWords(packet.address + 0x60u, {
            0x10000000u, 0, 0, 0x11000000u,                                  // cnt 0; flush
            0x30000001u, (l + 0x210u) & 0x7FFFFFFFu, 0x01000101u, 0x6C01001Du, // ref 1 quadword; unpack to 0x1D
            0x30000001u, (l + 0x200u) & 0x7FFFFFFFu, 0x01000101u, 0x6C01002Du, // ref 1 quadword; unpack to 0x2D
            0x10000000u, 0, 0x14000000u, 0x11000000u,                        // mscal; flush
            0x60000000u, 0, 0, 0,                                            // ret
        });
        return packet + 0x2C;
    }

    // Layer F's packet (+0x130): ALPHA_1 (0x30_00000048) through the GIF, then like layer B's first packet with
    // the chain at 0x1A91C0.
    GuestPtr<uint32_t> LayerF_BuildPacket(GuestPtr<LayerF> layer, GuestPtr<uint32_t> packet)
    {
        const uint32_t l = layer.address;
        port::store<uint32_t>(l + 0x130u, packet.address);
        putWords(packet.address, {
            0x10000002u, 0, 0x11000000u, 0x50000002u,                        // cnt 2 quadwords; flush, direct 2
            0x8001u, 0x10000000u, 0xEu, 0,                                   // GIF tag: one A+D register
            0x48u, 0x30u, 0x42u, 0,                                          // ALPHA_1
            0x10000000u, 0, 0, 0x11000000u,                                  // cnt 0; flush
            0x50000000u, 0x001A91C0u, 0, 0,                                  // call
            0x30000013u, l & 0x7FFFFFFFu, 0x01000101u, 0x6C13001Cu,          // ref 19 quadwords; unpack to 0x1C
            0x10000000u, 0, 0x03000056u, 0x020001D5u,                        // VIF base 0x56, offset 0x1D5
            0x10000000u, 0, 0x14000000u, 0x11000000u,                        // mscal; flush
            0x60000000u, 0, 0, 0,                                            // ret
        });
        return packet + 0x24;
    }

    // The draw parameters VU1 reads for each layer: a colour, register words, a GIF tag (NLOOP, NREG and the
    // register list), more words and the texture coordinates of its strips.
    void LayerC_InitParams(GuestPtr<LayerC> layer)
    {
        const uint32_t l = layer.address;
        putFloats(l, {255.0f, 0, 0, 128.0f, 0, 0, 0, 128.0f});
        putWords(l + 0x20u, {0x68u, 0, 0, 0, 0x801Au, 0xE0000000u, 0x04144410u, 0x00444441u, // GIF tag: 26 loops of 14
                             0x44u, 0, 0, 0, 0x82u, 0, 0, 0, 0, 0, 0, 4u});
        port::store<int32_t>(l + 0x1F0u, 0x110);
        for (uint32_t at = l + 0x70u; at < l + 0x1F0u; at += 4u)
            port::store<float>(at, 1.0f);
        putFloats(l + 0x200u, {1.0f, 1.0f, 1.0f, 1.0f});
    }

    void LayerF_InitParams(GuestPtr<LayerF> layer)
    {
        const uint32_t l = layer.address;
        putFloats(l, {128.0f, 128.0f, 128.0f, 128.0f});
        putWords(l + 0x10u, {0x9930A7A4u, 0x4004F405u, 0, 0, 0x68u, 0, 0, 0, 0x801Au, 0xE0000000u, 0x41241206u, 0x00412412u, // GIF tag: 26 loops of 14
                             0x7Cu, 0, 0, 0});
        putHalvesStq(l + 0x50u);
        putWords(l + 0x110u, {0, 0, 0, 0x10u});
    }

    // Layer G (the water) keeps its texture coordinates at +0x220 and copies them to both strips' packets (+0x60
    // and +0x120).
    void LayerG_InitParams(GuestPtr<LayerG> layer)
    {
        const uint32_t l = layer.address;
        putFloats(l, {128.0f, 128.0f, 128.0f, 60.0f});
        putWords(l + 0x20u, {0x40u, 0x10u, 0, 0, 0x8020u, 0xD0000000u, 0x24124120u, 0x00041241u, // GIF tag: 32 loops of 13
                             0x27Cu, 0, 0, 0, 0x7Cu, 0, 0, 0});
        putWords(l + 0x1E0u, {0, 0, 0, 2u});
        putHalvesStq(l + 0x220u);
        for (uint32_t i = 0; i < 12; ++i)
        {
            *GuestPtr<Vec4>{l + 0x60u + 16u * i} = *GuestPtr<Vec4>{l + 0x220u + 16u * i};
            *GuestPtr<Vec4>{l + 0x120u + 16u * i} = *GuestPtr<Vec4>{l + 0x220u + 16u * i};
        }
    }

    // The city map's overlay parameters (+0x2D30).
    void CityRender_InitParams(GuestPtr<CityMap> map)
    {
        const uint32_t m = map.address;
        putFloats(m + 0x2D30u, {128.0f, 128.0f, 128.0f, 128.0f});
        putWords(m + 0x2D40u, {0x99308000u, 0x4004B085u, 0, 0, 0x68u, 0, 0, 0, 0x801Au, 0xE0000000u, 0x41241206u, 0x00412412u, // GIF tag: 26 loops of 14
                               0x3Cu, 0, 0, 0});
        putHalvesStq(m + 0x2D80u);
        putWords(m + 0x2E40u, {0, 0, 0, 1u});
    }

    // The terrain's parameters (map +0x2E50): its texture coordinates run along s only.
    void Terrain_InitParams(int map)
    {
        const auto m = static_cast<uint32_t>(map);
        putFloats(m + 0x2E50u, {128.0f, 128.0f, 128.0f, 128.0f});
        putWords(m + 0x2E60u, {0x99308000u, 0x4004B085u, 0, 0, 0x6Cu, 0, 0, 0, 0x8024u, 0xA05DC000u, 0x24124126u, 0x41u}); // GIF tag: 36 loops of 10
        putFloats(m + 0x2E90u, {0.25f, 0.25f, 0, 0});
        static constexpr float kS[12] = {0, 0.5f, 0, 0.5f, 0.5f, 0, 0.5f, 1, 0.5f, 1, 1, 0.5f};
        for (uint32_t i = 0; i < 12; ++i)
            putFloats(m + 0x2EA0u + 16u * i, {kS[i], 0, 1.0f, 0});
    }

    PORT_FN(LayerA_BuildPacket);
    PORT_FN(LayerA_Draw);
    PORT_FN(LayerB_BuildPacket);
    PORT_FN(LayerF_BuildPacket);
    PORT_FN(LayerC_InitParams);
    PORT_FN(LayerF_InitParams);
    PORT_FN(LayerG_InitParams);
    PORT_FN(CityRender_InitParams);
    PORT_FN(Terrain_InitParams);

    // Layer B's draw parameters: a colour, register words, a GIF tag and the texture coordinates of its strips.
    void LayerB_InitParams(GuestPtr<LayerB> layer)
    {
        const uint32_t l = layer.address;
        putFloats(l + 0x0u, {128.0f, 128.0f, 128.0f, 128.0f});
        putWords(l + 0x10u, {0x9930a698u, 0x4004b085u, 0x0u, 0x0u});
        putWords(l + 0x20u, {0x68u, 0x0u, 0x0u, 0x0u});
        putWords(l + 0x30u, {0x801au, 0xe0000000u, 0x41241206u, 0x412412u});
        putWords(l + 0x40u, {0x3cu, 0x0u, 0x0u, 0x0u});
        putWords(l + 0x50u, {0x0u, 0x0u, 0x3f800000u, 0x0u});
        putFloats(l + 0x60u, {0.5f, 0.0f, 1.0f, 0.0f});
        putFloats(l + 0x70u, {0.0f, 0.5f, 1.0f, 0.0f});
        putFloats(l + 0x80u, {0.5f, 0.5f, 1.0f, 0.0f});
        putFloats(l + 0x90u, {0.0f, 1.0f, 1.0f, 0.0f});
        putFloats(l + 0xa0u, {0.5f, 1.0f, 1.0f, 0.0f});
        putFloats(l + 0xb0u, {0.5f, 0.0f, 1.0f, 0.0f});
        putFloats(l + 0xc0u, {1.0f, 0.0f, 1.0f, 0.0f});
        putFloats(l + 0xd0u, {0.5f, 0.5f, 1.0f, 0.0f});
        putFloats(l + 0xe0u, {1.0f, 0.5f, 1.0f, 0.0f});
        putFloats(l + 0xf0u, {0.5f, 1.0f, 1.0f, 0.0f});
        putFloats(l + 0x100u, {1.0f, 1.0f, 1.0f, 0.0f});
        putWords(l + 0x110u, {0x0u, 0x0u, 0x0u, 0x20u});
        putFloats(l + 0x120u, {0.6f, 0.08f, 0.0f, 0.0f});
        putWords(l + 0x200u, {0x0u, 0x0u, 0x0u, 0x40u});
        putWords(l + 0x210u, {0x9930a708u, 0x4004b085u, 0x0u, 0x0u});
    }

    // Layer D's draw parameters, built like layer B's.
    void LayerD_InitParams(GuestPtr<LayerD> layer)
    {
        const uint32_t l = layer.address;
        putWords(l + 0x0u, {0x8060u, 0x4034c000u, 0x4141u, 0x0u});
        putFloats(l + 0x10u, {765.0f, 765.0f, 765.0f, 40.0f});
        putFloats(l + 0x20u, {765.0f, 765.0f, 765.0f, 0.0f});
        port::store<int32_t>(l + 0x30u, 8);
        putFloats(l + 0x40u, {40.0f, 0.1f, 0.0f, 0.0f});
        putFloats(l + 0x50u, {40.0f, 35.4f, 0.0f, 0.0f});
        putFloats(l + 0x60u, {40.0f, 9.6f, 0.0f, 0.0f});
        putFloats(l + 0x70u, {40.0f, 15.8f, 0.0f, 0.0f});
        putFloats(l + 0x80u, {40.0f, 1.3f, 0.0f, 0.0f});
        putFloats(l + 0x90u, {40.0f, 24.6f, 0.0f, 0.0f});
        putFloats(l + 0xa0u, {40.0f, 13.3f, 0.0f, 0.0f});
        putFloats(l + 0xb0u, {40.0f, 8.7f, 0.0f, 0.0f});
        putFloats(l + 0xc0u, {40.0f, 36.8f, 0.0f, 0.0f});
        putFloats(l + 0xd0u, {40.0f, 18.1f, 0.0f, 0.0f});
        putFloats(l + 0xe0u, {40.0f, 28.0f, 0.0f, 0.0f});
        putFloats(l + 0xf0u, {40.0f, 11.6f, 0.0f, 0.0f});
        putFloats(l + 0x100u, {40.0f, 28.8f, 0.0f, 0.0f});
        putFloats(l + 0x110u, {40.0f, 31.5f, 0.0f, 0.0f});
        putFloats(l + 0x120u, {40.0f, 4.2f, 0.0f, 0.0f});
        putFloats(l + 0x130u, {40.0f, 7.9f, 0.0f, 0.0f});
        putFloats(l + 0x140u, {-0.277647f, -36.85f, 0.186816f, 1.0f});
        putFloats(l + 0x150u, {-0.077647f, -38.449997f, 0.186816f, 1.0f});
        putFloats(l + 0x160u, {0.278509f, -33.95f, 1.083412f, 1.0f});
        putFloats(l + 0x170u, {0.478509f, -35.55f, 1.083412f, 1.0f});
        putFloats(l + 0x180u, {-0.702686f, -30.399998f, 0.219903f, 1.0f});
        putWords(l + 0x190u, {0xbf00b008u, 0xc1ffffffu, 0x3e612e41u, 0x3f800000u});
        putFloats(l + 0x1a0u, {0.823469f, -27.699999f, -0.033502f, 1.0f});
        putFloats(l + 0x1b0u, {1.023469f, -29.3f, -0.033502f, 1.0f});
        putFloats(l + 0x1c0u, {-0.437529f, -24.25f, 0.511663f, 1.0f});
        putFloats(l + 0x1d0u, {-0.237529f, -25.849998f, 0.511663f, 1.0f});
        putFloats(l + 0x1e0u, {0.617897f, -20.5f, -0.409993f, 1.0f});
        putFloats(l + 0x1f0u, {0.817897f, -22.099998f, -0.409993f, 1.0f});
        putFloats(l + 0x200u, {0.373105f, -16.600002f, 0.141843f, 1.0f});
        putFloats(l + 0x210u, {0.573105f, -18.2f, 0.141843f, 1.0f});
        putFloats(l + 0x220u, {0.179452f, -11.45f, -0.877998f, 1.0f});
        putFloats(l + 0x230u, {0.379452f, -13.049998f, -0.877998f, 1.0f});
        putFloats(l + 0x240u, {0.8f, -7.15f, 1.0f, 1.0f});
        putFloats(l + 0x250u, {1.0f, -8.75f, 1.0f, 1.0f});
        putFloats(l + 0x260u, {-0.85f, -14.650001f, 0.7f, 1.0f});
        putFloats(l + 0x270u, {-0.65f, -16.25f, 0.7f, 1.0f});
        putFloats(l + 0x280u, {-0.55f, -4.3f, -0.6f, 1.0f});
        putFloats(l + 0x290u, {-0.35f, -5.9f, -0.6f, 1.0f});
        putFloats(l + 0x2a0u, {-0.2f, -0.6f, -0.25f, 1.0f});
        putFloats(l + 0x2b0u, {0.0f, -2.2f, -0.25f, 1.0f});
        putFloats(l + 0x2d0u, {128.0f, 128.0f, 128.0f, 128.0f});
        putWords(l + 0x2e0u, {0x9930a7b8u, 0x4004f685u, 0x0u, 0x0u});
        putWords(l + 0x2f0u, {0x68u, 0x0u, 0x0u, 0x0u});
        putWords(l + 0x300u, {0x801au, 0xe0000000u, 0x41241206u, 0x412412u});
        putWords(l + 0x310u, {0x7cu, 0x0u, 0x0u, 0x0u});
        putWords(l + 0x320u, {0x0u, 0x0u, 0x3f800000u, 0x0u});
        putFloats(l + 0x330u, {0.5f, 0.0f, 1.0f, 0.0f});
        putFloats(l + 0x340u, {0.0f, 0.5f, 1.0f, 0.0f});
        putFloats(l + 0x350u, {0.5f, 0.5f, 1.0f, 0.0f});
        putFloats(l + 0x360u, {0.0f, 1.0f, 1.0f, 0.0f});
        putFloats(l + 0x370u, {0.5f, 1.0f, 1.0f, 0.0f});
        putFloats(l + 0x380u, {0.5f, 0.0f, 1.0f, 0.0f});
        putFloats(l + 0x390u, {1.0f, 0.0f, 1.0f, 0.0f});
        putFloats(l + 0x3a0u, {0.5f, 0.5f, 1.0f, 0.0f});
        putFloats(l + 0x3b0u, {1.0f, 0.5f, 1.0f, 0.0f});
        putFloats(l + 0x3c0u, {0.5f, 1.0f, 1.0f, 0.0f});
        putFloats(l + 0x3d0u, {1.0f, 1.0f, 1.0f, 0.0f});
        putWords(l + 0x3e0u, {0x0u, 0x0u, 0x0u, 0x1u});
        putFloats(l + 0x3f0u, {0.0f, -0.01f, 0.0f, 1.0f});
    }

    PORT_FN(LayerB_InitParams);
    PORT_FN(LayerD_InitParams);

    // Layer C's three packets, one after the other (their addresses at +0x210, +0x218 and +0x214): ALPHA_1 set to
    // 0x30_00000048, the chain at 0x1A7B30 and the layer's first seven quadwords to VU1 0x1C with the program started;
    // ALPHA_1 set to 0x20_00000044; ALPHA_1 back to 0x30_00000048, the layer's 26 quadwords from +0x70 to VU1 0x12 and the
    // chain at 0x1A8330. Returns the end of the packets.
    GuestPtr<uint32_t> LayerC_BuildPacket(GuestPtr<LayerC> layer, GuestPtr<uint32_t> packet)
    {
        const uint32_t l = layer.address, p = packet.address;
        port::store<uint32_t>(l + 0x210u, p);
        port::store<uint32_t>(l + 0x214u, p + 0xc0u);
        port::store<uint32_t>(l + 0x218u, p + 0x80u);
        putWords(p + 0x0u, {0x10000000u, 0x0u, 0x0u, 0x11000000u});
        putWords(p + 0x10u, {0x10000002u, 0x0u, 0x11000000u, 0x50000002u});
        putWords(p + 0x20u, {0x8001u, 0x10000000u, 0xeu, 0x0u});
        putWords(p + 0x30u, {0x48u, 0x30u, 0x42u, 0x0u});
        putWords(p + 0x40u, {0x50000000u, 0x1a7b30u, 0x0u, 0x0u});
        putWords(p + 0x50u, {0x30000007u, (l) & 0x7FFFFFFFu, 0x1000101u, 0x6c07001cu});
        putWords(p + 0x60u, {0x10000000u, 0x0u, 0x3000056u, 0x20001d5u});
        putWords(p + 0x70u, {0x60000000u, 0x0u, 0x14000000u, 0x11000000u});
        putWords(p + 0x80u, {0x10000002u, 0x0u, 0x11000000u, 0x50000002u});
        putWords(p + 0x90u, {0x8001u, 0x10000000u, 0xeu, 0x0u});
        putWords(p + 0xa0u, {0x44u, 0x20u, 0x42u, 0x0u});
        putWords(p + 0xb0u, {0x60000000u, 0x0u, 0x0u, 0x0u});
        putWords(p + 0xc0u, {0x3000001au, (l + 0x70u) & 0x7FFFFFFFu, 0x1000101u, 0x6c1a0012u});
        putWords(p + 0xd0u, {0x10000000u, 0x0u, 0x0u, 0x11000000u});
        putWords(p + 0xe0u, {0x10000002u, 0x0u, 0x11000000u, 0x50000002u});
        putWords(p + 0xf0u, {0x8001u, 0x10000000u, 0xeu, 0x0u});
        putWords(p + 0x100u, {0x48u, 0x30u, 0x42u, 0x0u});
        putWords(p + 0x110u, {0x50000000u, 0x1a8330u, 0x0u, 0x0u});
        putWords(p + 0x120u, {0x60000000u, 0x0u, 0x3000042u, 0x20001dfu});
        return GuestPtr<uint32_t>{p + 0x130u};
    }

    PORT_FN(LayerC_BuildPacket);

    // Layer D (the sky): for each of its two buffers, the packet starts with its layer-parameters upload and the first
    // microcode (0x1A8A00); then the buffer's vertex data goes to VU1 in 78 chunks of 0x200 bytes (9 groups of 8, each
    // chunk a REF tag, the UNPACK and a MSCAL, then 6 singles), the second microcode (0x1A8E20) follows with the
    // parameters (+0x2D0) and the packet ends. The layer keeps the start of each buffer's packet at +0x2C0 and the
    // second tail at +0x400, the end of the last at +0x408. Returns the end of the packets.
    GuestPtr<uint32_t> LayerD_BuildPacket(GuestPtr<LayerD> layer, GuestPtr<uint32_t> packet, GuestPtr<int32_t> buffers)
    {
        const uint32_t layerAddress = layer.address;
        const uint32_t layerTag = layerAddress & 0x7FFFFFFFu;
        const uint32_t paramsTag = (layerAddress + 0x2D0u) & 0x7FFFFFFFu;
        const auto chunk = [](uint32_t at, uint32_t source) {
            putWords(at + 0x0u, {0x30000020u, source & 0x7FFFFFFFu, 0x0u, 0x6C208000u});
            putWords(at + 0x10u, {0x10000000u, 0x0u, 0x0u, 0x17000000u});
        };
        uint32_t at = packet.address;
        for (uint32_t buffer = 0; buffer < 2; ++buffer)
        {
            const uint32_t slot = layerAddress + 4u * buffer;
            port::store<uint32_t>(slot + 0x2C0u, at);
            putWords(at + 0x0u, {0x10000000u, 0x0u, 0x0u, 0x11000000u});
            putWords(at + 0x10u, {0x50000000u, 0x1A8A00u, 0x0u, 0x0u});
            putWords(at + 0x20u, {0x3000002Cu, layerTag, 0x1000101u, 0x6C2C0012u});
            putWords(at + 0x30u, {0x10000000u, 0x0u, 0x30000BEu, 0x20001A1u});
            putWords(at + 0x40u, {0x10000000u, 0x0u, 0x14000000u, 0x11000000u});
            uint32_t source = port::load<uint32_t>(buffers.address + 4u * buffer);
            at += 0x50u;
            uint32_t chunks = 0;
            for (; chunks < 0x46; chunks += 8)
            {
                for (uint32_t k = 0; k < 8; ++k)
                    chunk(at + 0x20u * k, source + 0x200u * k);
                at += 0x100u;
                source += 0x1000u;
            }
            for (; chunks < 0x4E; ++chunks)
            {
                chunk(at, source);
                at += 0x20u;
                source += 0x200u;
            }
            putWords(at + 0x0u, {0x60000000u, 0x0u, 0x0u, 0x0u});
            port::store<uint32_t>(slot + 0x400u, at + 0x10u);
            putWords(at + 0x10u, {0x10000002u, 0x0u, 0x11000000u, 0x50000002u});
            putWords(at + 0x20u, {0x8001u, 0x10000000u, 0xEu, 0x0u});
            putWords(at + 0x30u, {0x64u, 0xFFu, 0x42u, 0x0u});
            putWords(at + 0x40u, {0x10000000u, 0x0u, 0x0u, 0x11000000u});
            putWords(at + 0x50u, {0x50000000u, 0x1A8E20u, 0x0u, 0x0u});
            putWords(at + 0x60u, {0x30000013u, paramsTag, 0x1000101u, 0x6C13001Cu});
            putWords(at + 0x70u, {0x10000000u, 0x0u, 0x3000056u, 0x20001D5u});
            putWords(at + 0x80u, {0x10000000u, 0x0u, 0x14000000u, 0x11000000u});
            putWords(at + 0x90u, {0x60000000u, 0x0u, 0x0u, 0x0u});
            at += 0xA0u;
        }
        port::store<uint32_t>(layerAddress + 0x408u, at);
        putWords(at + 0x0u, {0x10000002u, 0x0u, 0x11000000u, 0x50000002u});
        putWords(at + 0x10u, {0x8001u, 0x10000000u, 0xEu, 0x0u});
        putWords(at + 0x20u, {0x44u, 0x20u, 0x42u, 0x0u});
        putWords(at + 0x30u, {0x60000000u, 0x0u, 0x0u, 0x0u});
        return GuestPtr<uint32_t>{at + 0x40u};
    }

    PORT_FN(LayerD_BuildPacket);

    // Layer E (clouds and stars): like layer D, for each of the two buffers a packet that uploads the layer's parameters
    // (the GS TEX0 of its texture is built field by field: base 0x3F70, width 1, format 0x14, 2^5 by 2^5, colour with
    // alpha, its palette at 0x3F72) and the microcode at 0x1A9A90, then the buffer's vertex data in chunks and the
    // microcode at 0x1A9F10 with the parameters (+0x570). The starts are kept at +0x560, the second tails at +0x6A0 and
    // the end at +0x6A8. Returns the end of the packets.
    GuestPtr<uint32_t> LayerE_BuildPacket(GuestPtr<LayerE> layer, GuestPtr<uint32_t> packet, GuestPtr<int32_t> buffers)
    {
        const uint32_t layerAddress = layer.address;
        const uint32_t layerTag = layerAddress & 0x7FFFFFFFu;
        const uint32_t paramsTag = (layerAddress + 0x570u) & 0x7FFFFFFFu;
        const auto chunk = [](uint32_t at, uint32_t source) {
            putWords(at + 0x0u, {0x30000020u, source & 0x7FFFFFFFu, 0x0u, 0x6C208000u});
            putWords(at + 0x10u, {0x10000000u, 0x0u, 0x0u, 0x17000000u});
        };
        uint32_t at = packet.address;
        for (uint32_t buffer = 0; buffer < 2; ++buffer)
        {
            const uint32_t slot = layerAddress + 4u * buffer;
            port::store<uint32_t>(slot + 0x560u, at);
            putWords(at + 0x0u, {0x10000004u, 0x0u, 0x11000000u, 0x50000004u});
            putWords(at + 0x10u, {0x8003u, 0x10000000u, 0xEu, 0x0u});
            putWords(at + 0x20u, {0x80808080u, 0x3F800000u, 0x1u, 0x0u});
            putWords(at + 0x30u, {0x48u, 0x30u, 0x42u, 0x0u});
            putWords(at + 0x40u, {0x0u, 0x0u, 0x6u, 0x0u});
            const auto field16 = [&](uint32_t offset, uint16_t keep, uint16_t value) {
                port::store<uint16_t>(at + offset, static_cast<uint16_t>((port::load<uint16_t>(at + offset) & keep) | value));
            };
            const auto field8 = [&](uint32_t offset, uint8_t keep, uint8_t value) {
                port::store<uint8_t>(at + offset, static_cast<uint8_t>((port::load<uint8_t>(at + offset) & keep) | value));
            };
            const auto field64 = [&](uint64_t keep, uint64_t value) {
                port::store<uint64_t>(at + 0x40u, (port::load<uint64_t>(at + 0x40u) & keep) | value);
            };
            field16(0x40u, 0xC000u, 0x3F70u);                                      // TBP0
            field64(0xFFFFFFFFFFF03FFFull, 1ull << 14);                            // TBW
            field16(0x42u, 0xFC0Fu, 0x140u);                                       // PSM
            field8(0x43u, 0xC3u, static_cast<uint8_t>((Math_Log2(0x20) & 0xF) << 2)); // TW
            field64(0xFFFFFFFC3FFFFFFFull, static_cast<uint64_t>(Math_Log2(0x20) & 0xF) << 30); // TH
            field8(0x44u, 0xFBu, 0x4u);                                            // TCC
            field8(0x44u, 0xE7u, 0x0u);                                            // TFX
            field64(0xFFF8001FFFFFFFFFull, 0x3F72ull << 37);                       // CBP
            field8(0x46u, 0x87u, 0x0u);                                            // CPSM
            field8(0x46u, 0x7Fu, 0x0u);                                            // CSM
            field8(0x47u, 0xE0u, 0x0u);                                            // CSA
            field8(0x47u, 0x1Fu, 0x20u);                                           // CLD
            putWords(at + 0x50u, {0x50000000u, 0x1A9A90u, 0x0u, 0x0u});
            putWords(at + 0x60u, {0x30000056u, layerTag, 0x1000101u, 0x6C560012u});
            putWords(at + 0x70u, {0x10000000u, 0x0u, 0x300007Cu, 0x20001C2u});
            putWords(at + 0x80u, {0x10000000u, 0x0u, 0x14000000u, 0x11000000u});
            uint32_t source = port::load<uint32_t>(buffers.address + 4u * buffer);
            at += 0x90u;
            uint32_t chunks = 0;
            for (; chunks < 0x46; chunks += 8)
            {
                for (uint32_t k = 0; k < 8; ++k)
                    chunk(at + 0x20u * k, source + 0x200u * k);
                at += 0x100u;
                source += 0x1000u;
            }
            for (; chunks < 0x4E; ++chunks)
            {
                chunk(at, source);
                at += 0x20u;
                source += 0x200u;
            }
            port::store<uint32_t>(slot + 0x6A0u, at + 0x10u);
            putWords(at + 0x0u, {0x60000000u, 0x0u, 0x0u, 0x0u});
            putWords(at + 0x10u, {0x10000002u, 0x0u, 0x11000000u, 0x50000002u});
            putWords(at + 0x20u, {0x8001u, 0x10000000u, 0xEu, 0x0u});
            putWords(at + 0x30u, {0x64u, 0x60u, 0x42u, 0x0u});
            putWords(at + 0x40u, {0x10000000u, 0x0u, 0x0u, 0x11000000u});
            putWords(at + 0x50u, {0x50000000u, 0x1A9F10u, 0x0u, 0x0u});
            putWords(at + 0x60u, {0x30000013u, paramsTag, 0x1000101u, 0x6C13001Cu});
            putWords(at + 0x70u, {0x10000000u, 0x0u, 0x3000056u, 0x20001D5u});
            putWords(at + 0x80u, {0x10000000u, 0x0u, 0x14000000u, 0x11000000u});
            putWords(at + 0x90u, {0x60000000u, 0x0u, 0x0u, 0x0u});
            at += 0xA0u;
        }
        port::store<uint32_t>(layerAddress + 0x6A8u, at);
        putWords(at + 0x0u, {0x10000002u, 0x0u, 0x11000000u, 0x50000002u});
        putWords(at + 0x10u, {0x8001u, 0x10000000u, 0xEu, 0x0u});
        putWords(at + 0x20u, {0x44u, 0x20u, 0x42u, 0x0u});
        putWords(at + 0x30u, {0x60000000u, 0x0u, 0x0u, 0x0u});
        return GuestPtr<uint32_t>{at + 0x40u};
    }

    PORT_FN(LayerE_BuildPacket);

    // Layer G (the water): two sections, one per vertex buffer. Each starts with the layer's parameters upload and the
    // microcode at 0x1A6440 (the packet words of the layer's GS state: two copies of the same register pair, ALPHA and
    // the rest), then the buffer's vertex data in 39 chunks of 0x400 bytes (4 groups of 8, then 7 singles) and an end
    // marker. The layer keeps the start of the first section at +0x210 and of the second at +0x214. Returns the end.
    GuestPtr<uint32_t> LayerG_BuildPacket(GuestPtr<LayerG> layer, GuestPtr<uint32_t> packet, GuestPtr<int32_t> buffers)
    {
        const uint32_t layerAddress = layer.address;
        const uint32_t layerTag = layerAddress & 0x7FFFFFFFu;
        const auto header = [&](uint32_t at) {
            putWords(at + 0x0u, {0x10000005u, 0x0u, 0x11000000u, 0x50000005u});
            putWords(at + 0x10u, {0x8004u, 0x10000000u, 0xEu, 0x0u});
            putWords(at + 0x20u, {0x48u, 0x50u, 0x42u, 0x0u});
            putWords(at + 0x30u, {0x64u, 0x50u, 0x43u, 0x0u});
            putWords(at + 0x40u, {0x9930A78Cu, 0x4004F105u, 0x6u, 0x0u});
            putWords(at + 0x50u, {0x9930A78Cu, 0x4004F105u, 0x7u, 0x0u});
            putWords(at + 0x60u, {0x50000000u, 0x1A6440u, 0x0u, 0x0u});
            putWords(at + 0x70u, {0x30000021u, layerTag, 0x1000101u, 0x6C21001Cu});
            putWords(at + 0x80u, {0x10000000u, 0x0u, 0x300003Eu, 0x20001E1u});
            putWords(at + 0x90u, {0x10000000u, 0x0u, 0x14000000u, 0x11000000u});
        };
        const auto chunk = [](uint32_t at, uint32_t source) {
            putWords(at + 0x0u, {0x30000040u, source & 0x7FFFFFFFu, 0x0u, 0x6C408000u});
            putWords(at + 0x10u, {0x10000000u, 0x0u, 0x0u, 0x17000000u});
        };
        uint32_t at = packet.address;
        port::store<uint32_t>(layerAddress + 0x210u, at);
        header(at);
        at += 0xA0u;
        for (uint32_t section = 0; section < 2; ++section)
        {
            uint32_t source = port::load<uint32_t>(buffers.address + 4u * section);
            uint32_t chunks = 0;
            for (; chunks < 0x1F; chunks += 8)
            {
                for (uint32_t k = 0; k < 8; ++k)
                    chunk(at + 0x20u * k, source + 0x400u * k);
                at += 0x100u;
                source += 0x2000u;
            }
            for (; chunks < 0x27; ++chunks)
            {
                chunk(at, source);
                at += 0x20u;
                source += 0x400u;
            }
            putWords(at + 0x0u, {0x60000000u, 0x0u, 0x0u, 0x0u});
            if (section == 1)
                return GuestPtr<uint32_t>{at + 0x10u};
            port::store<uint32_t>(layerAddress + 0x214u, at + 0x10u);
            header(at + 0x10u);
            at += 0xB0u;
        }
        return GuestPtr<uint32_t>{at};
    }

    PORT_FN(LayerG_BuildPacket);

    // ---- the terrain's VU1 packets (batch 41): the same shape as the layers' packets, two sections of vertex chunks

    // Terrain packet A (microcode 0x1A76F0, parameters at +0x2E50): a header upload, then the first vertex buffer
    // (+0x2D18) in 20 chunks of 0x6C0 bytes (2 groups of 8, 4 singles) and an end marker, the header again and the second
    // buffer (+0x2D1C) the same. The terrain keeps the start at +0x2F6C and, for the second section, the start again at
    // +0x2F70. Returns the end.
    GuestPtr<uint32_t> Terrain_BuildVu1PacketA(int terrainAddress, GuestPtr<uint32_t> packet)
    {
        const uint32_t terrain = static_cast<uint32_t>(terrainAddress);
        const uint32_t tag = (terrain + 0x2E50u) & 0x7FFFFFFFu;
        const auto header = [&](uint32_t at) {
            putWords(at + 0x0u, {0x10000000u, 0x0u, 0x0u, 0x11000000u});
            putWords(at + 0x10u, {0x50000000u, 0x1A76F0u, 0x0u, 0x0u});
            putWords(at + 0x20u, {0x30000011u, tag, 0x1000101u, 0x6C11001Cu});
            putWords(at + 0x30u, {0x10000000u, 0x0u, 0x3000056u, 0x20001D5u});
            putWords(at + 0x40u, {0x10000000u, 0x0u, 0x14000000u, 0x11000000u});
            putWords(at + 0x50u, {0x10000000u, 0x0u, 0x1000101u, 0x0u});
        };
        const auto chunk = [](uint32_t at, uint32_t source) {
            putWords(at + 0x0u, {0x3000006Cu, source & 0x7FFFFFFFu, 0x0u, 0x6C6C8000u});
            putWords(at + 0x10u, {0x10000000u, 0x0u, 0x0u, 0x17000000u});
        };
        uint32_t at = packet.address;
        port::store<uint32_t>(terrain + 0x2F6Cu, at);
        header(at);
        at += 0x60u;
        for (uint32_t section = 0; section < 2; ++section)
        {
            uint32_t source = port::load<uint32_t>(terrain + 0x2D18u + 4u * section);
            uint32_t chunks = 0;
            for (; chunks < 0xC; chunks += 8)
            {
                for (uint32_t k = 0; k < 8; ++k)
                    chunk(at + 0x20u * k, source + 0x6C0u * k);
                at += 0x100u;
                source += 0x3600u;
            }
            for (; chunks < 0x14; ++chunks)
            {
                chunk(at, source);
                at += 0x20u;
                source += 0x6C0u;
            }
            putWords(at + 0x0u, {0x60000000u, 0x0u, 0x0u, 0x0u});
            if (section == 1)
                return GuestPtr<uint32_t>{at + 0x10u};
            port::store<uint32_t>(terrain + 0x2F70u, packet.address);
            header(at + 0x10u);
            at += 0x70u;
        }
        return GuestPtr<uint32_t>{at};
    }

    // Terrain packet B (microcode 0x1A5EB0, parameters at +0x2D30): a header with an end marker, the first buffer (+0x2D0C)
    // in 24 chunks of 0x680 bytes (3 groups of 8) and an end marker, then the second buffer (+0x2D10) the same. The
    // terrain keeps the start at +0x2F68, where the chunks begin at +0x2F60 and where the second section begins at
    // +0x2F64. Returns the end.
    GuestPtr<uint32_t> Terrain_BuildVu1PacketB(int terrainAddress, GuestPtr<uint32_t> packet)
    {
        const uint32_t terrain = static_cast<uint32_t>(terrainAddress);
        const auto chunk = [](uint32_t at, uint32_t source) {
            putWords(at + 0x0u, {0x30000068u, source & 0x7FFFFFFFu, 0x0u, 0x6C688000u});
            putWords(at + 0x10u, {0x10000000u, 0x0u, 0x0u, 0x17000000u});
        };
        uint32_t at = packet.address;
        port::store<uint32_t>(terrain + 0x2F68u, at);
        putWords(at + 0x0u, {0x10000000u, 0x0u, 0x0u, 0x11000000u});
        putWords(at + 0x10u, {0x50000000u, 0x1A5EB0u, 0x0u, 0x0u});
        putWords(at + 0x20u, {0x30000012u, (terrain + 0x2D30u) & 0x7FFFFFFFu, 0x1000101u, 0x6C12001Cu});
        putWords(at + 0x30u, {0x10000000u, 0x0u, 0x3000056u, 0x20001D5u});
        putWords(at + 0x40u, {0x10000000u, 0x0u, 0x14000000u, 0x11000000u});
        putWords(at + 0x50u, {0x60000000u, 0x0u, 0x0u, 0x0u});
        at += 0x60u;
        port::store<uint32_t>(terrain + 0x2F60u, at);
        for (uint32_t section = 0; section < 2; ++section)
        {
            uint32_t source = port::load<uint32_t>(terrain + 0x2D0Cu + 4u * section);
            for (uint32_t chunks = 0; chunks < 0x18; chunks += 8)
            {
                for (uint32_t k = 0; k < 8; ++k)
                    chunk(at + 0x20u * k, source + 0x680u * k);
                at += 0x100u;
                source += 0x3400u;
            }
            putWords(at + 0x0u, {0x60000000u, 0x0u, 0x0u, 0x0u});
            if (section == 1)
                return GuestPtr<uint32_t>{at + 0x10u};
            at += 0x10u;
            port::store<uint32_t>(terrain + 0x2F64u, at);
        }
        return GuestPtr<uint32_t>{at};
    }

    PORT_FN(Terrain_BuildVu1PacketA);
    PORT_FN(Terrain_BuildVu1PacketB);

    // ---- drawing layer C (batch 39)

    // Layer C draws the highlighted rectangle: in mode 1 (pass 0) its three packets around the map's own for this
    // frame; in mode 2 (pass 1) the placement marker, node 0 of the marker model (node 1 for a size 3 building) put
    // at the middle of the footprint at (rectX0, rectZ0), 0.8 below the tile for the ground and below each object
    // stacked on it.
    void LayerC_DrawBuildings(GuestPtr<LayerC> layer, int packet, GuestPtr<CityMap> map, int pass)
    {
        const uint32_t l = layer.address;
        if (layer->rectActive == 1 && pass == 0)
        {
            Dma_AppendCall(port::load<int32_t>(l + 0x210u));
            Dma_AppendCall(packet);
            Dma_AppendCall(port::load<int32_t>(l + 0x218u));
        }
        else if (layer->rectActive == 2 && pass == 1)
        {
            ps2f height = Map_GetHeightAt(map, layer->rectX0, layer->rectZ0) - ps2f(0.8f);
            GuestPtr<MapObject> obj = Map_GetTileObject(map, layer->rectX0, layer->rectZ0);
            const GuestPtr<BuildingType> type{globals::buildingTypeAddress(obj->type)};
            const int node = type->size == 3 ? 1 : 0;
            for (; obj->nextOnTile != kNoObject; obj = Map_GetRecord(map, obj->nextOnTile))
                height = height - ps2f(0.8f);
            const GuestPtr<A3DObject> model{port::load<uint32_t>(l + 0x230u)};
            const uint32_t nodeRecord = port::load<uint32_t>(model->model.address + 8u) + 0xC0u * static_cast<uint32_t>(node);
            Mat4_Copy(GuestPtr<Vec4>{nodeRecord + 0x70u}, GuestPtr<Vec4>{globals::kIdentityMatrix});
            port::store<float>(nodeRecord + 0xA0u, static_cast<float>(fromInt(layer->rectX0) - ps2f(64.0f) + fromInt(type->size) / ps2f(2.0f)));
            port::store<float>(nodeRecord + 0xA4u, static_cast<float>(height));
            port::store<float>(nodeRecord + 0xA8u, static_cast<float>(fromInt(0x80 - layer->rectZ0) - ps2f(64.0f) - fromInt(type->size) / ps2f(2.0f)));
            Dma_AppendCall(port::load<int32_t>(l + 0x214u));
            A3DObject_DrawNode(model, node);
            Dma_AppendCall(port::load<int32_t>(l + 0x218u));
        }
    }

    PORT_FN(LayerC_DrawBuildings);

    // ---- the sky by the clock (batch 39)

    // The sky layer for the time of day (clock in seconds): the seconds up to 5:00 (18000), the distance from midnight
    // after 18:00 (0x15180 less the clock; before that the clock held at 18000) set its three colour scale words
    // (0x443F4000) and the day fraction (clock / 450, +0x1C), and the packet's word +0x24 (clock / 600); the sixteen
    // cloud offsets (+0x44, 0x10 apart) drift 0.4 a frame within 40; the horizon's VIF code (+0x2E0..) is rebuilt with
    // a position that cycles every twelve frames; then the pass's packet, the map's, the shared one (+0x408) and the
    // pass's second packet (+0x2C0) are queued.
    void LayerD_DrawSkyByClock(GuestPtr<LayerD> layer, int pass, int packet, int clock)
    {
        const uint32_t l = layer.address;
        if (clock < 0x10B30)
        {
            if (clock > 18000)
                clock = 18000;
        }
        else
            clock = std::abs(clock - 0x15180);
        const uint32_t slot = 4u * static_cast<uint32_t>(pass);
        port::store<float>(l + 0x1Cu, static_cast<float>(fromInt(clock) / ps2f(450.0f)));
        port::store<uint32_t>(l + 0x18u, 0x443F4000u);
        port::store<uint32_t>(l + 0x14u, 0x443F4000u);
        port::store<uint32_t>(l + 0x10u, 0x443F4000u);
        port::store<int32_t>(port::load<uint32_t>(l + 0x400u + slot) + 0x24u, clock / 600);
        for (uint32_t cloud = l + 0x44u; cloud < l + 0x44u + 0x100u; cloud += 0x10u)
        {
            ps2f offset = ps2f(port::load<float>(cloud)) + ps2f(0.4f);
            port::store<float>(cloud, static_cast<float>(offset));
            if (ps2f(40.0f) <= offset)
                port::store<float>(cloud, static_cast<float>(offset - ps2f(40.0f)));
        }
        if (port::load<int8_t>(0x001BDAD4u) == 0)
        {
            port::store<int32_t>(0x001BDAD0u, 0);
            port::store<int8_t>(0x001BDAD4u, 1);
        }
        port::store<int32_t>(0x001BDAD0u, port::load<int32_t>(0x001BDAD0u) + 1);
        int32_t cycle = ((port::load<int32_t>(0x001BDAD0u) >> 1) % 6) * 0x40;
        if (cycle < 0)
            cycle += 3;
        port::store<int32_t>(l + 0x2ECu, 0);
        port::store<int32_t>(l + 0x2E8u, 0);
        port::store<uint32_t>(l + 0x2E4u, 0x4004F685u);
        port::store<uint32_t>(l + 0x2E0u, static_cast<uint32_t>((cycle >> 2) + 0x27B8) | 0x99308000u);
        Dma_AppendCall(port::load<int32_t>(l + 0x400u + slot));
        Dma_AppendCall(packet);
        Dma_AppendCall(port::load<int32_t>(l + 0x408u));
        Dma_AppendCall(port::load<int32_t>(l + 0x2C0u + slot));
    }

    PORT_FN(LayerD_DrawSkyByClock);

    // ---- Object lifetime and the files and lists graphics need (plan P6 batch 1) ----

    GuestPtr<A3DObject> A3DObjectBase_DeletingDtor(GuestPtr<A3DObject> model, int16_t deleteFlag)
    {
        if (model)
        {
            BaseObject_dtor(static_cast<int>(model.address), 0);
            if (0 < deleteFlag)
                sdk::operator_delete(model.address);
        }
        return model;
    }

    // Frees the model data and the mesh table, then the base object (and the object itself when the flag is positive).
    GuestPtr<A3DObject> A3DObject_dtor(GuestPtr<A3DObject> model, int16_t deleteFlag)
    {
        if (model)
        {
            if (model->model)
            {
                sdk::free(model->model.address);
                model->model = GuestPtr<void>{0u};
            }
            if (model->meshes)
            {
                sdk::operator_delete(model->meshes.address);
                model->meshes = GuestPtr<void>{0u};
            }
            BaseObject_dtor(static_cast<int>(model.address), 0);
            if (0 < deleteFlag)
                sdk::operator_delete(model.address);
        }
        return model;
    }

    // Sets the colour (four words) of mesh `index` when it has a material (+4 nonzero); an index past the model's mesh
    // count is reported with printf. The result is printf's, or all of what v0 held when the call was made.
    int64_t A3DObject_ChangeLuminosity(GuestPtr<A3DObject> model, GuestPtr<uint32_t> colour, int index)
    {
        const int64_t entryV0 = port::guestV0Wide();
        const uint32_t data = model->model.address;
        const uint32_t meshes = port::load<uint32_t>(data + 8u);
        if (!(index < static_cast<int>(port::load<int16_t>(data + 0x18u))))
            return port::callGuest<int64_t>(sdk::addr::printf, 0x001B5A90u);
        const uint32_t mesh = meshes + static_cast<uint32_t>(index) * 0xC0u;
        if (port::load<uint32_t>(mesh + 4u) != 0u)
            *GuestPtr<Vec4>{mesh + 0x60u} = *GuestPtr<Vec4>{colour.address};
        return entryV0;
    }

    GuestPtr<TexImage> TexImage_dtor(GuestPtr<TexImage> image, int16_t deleteFlag)
    {
        if (image)
        {
            if (image->file)
            {
                sdk::free(image->file.address);
                image->file = GuestPtr<TexImageFile>{0u};
            }
            if (0 < deleteFlag)
                sdk::operator_delete(image.address);
        }
        return image;
    }

    // Loads the texture file `name` whole into the image and converts it for upload (a2, a3: texture and palette blocks).
    int TexImage_Load(GuestPtr<TexImage> image, GuestPtr<char> name, int a2, int a3)
    {
        port::GuestLocal<GameFile> file;
        File_ctor(file, 0);
        image->file = GuestPtr<TexImageFile>{static_cast<uint32_t>(File_LoadWhole(file, name, GuestPtr<int32_t>{0u}))};
        TexImage_Convert(image, a2, a3);
        File_dtor(file, -1);
        return static_cast<int>(image.address);
    }

    GuestPtr<TexSprite> TexSprite_dtor(GuestPtr<TexSprite> sprite, int16_t deleteFlag)
    {
        if (sprite)
        {
            port::store<uint32_t>(sprite.address, 0x001BCFD8u);
            if (sprite->packet != 0u)
            {
                sdk::free(sprite->packet);
                sprite->packet = 0u;
            }
            if (sprite->image)
            {
                sdk::free(sprite->image.address);
                sprite->image = GuestPtr<TexFileHeader>{0u};
            }
            if (0 < deleteFlag)
                sdk::operator_delete(sprite.address);
        }
        return sprite;
    }

    // Loads the .TEX file `file` whole, allocates the sprite's packet (18 quadwords per tile plus the palette upload),
    // shows the whole image from its origin and builds the packet; a2 nonzero: alpha blended.
    int TexSprite_Load(GuestPtr<TexSprite> sprite, int file, int blend)
    {
        port::store<uint32_t>(sprite.address, 0x001BCFD8u);
        port::GuestLocal<GameFile> local;
        File_ctor(local, 0);
        sprite->image = GuestPtr<TexFileHeader>{static_cast<uint32_t>(File_LoadWhole(local, GuestPtr<char>{static_cast<uint32_t>(file)}, GuestPtr<int32_t>{0u}))};
        const GuestPtr<TexFileHeader> header = sprite->image;
        const uint32_t tileCount = static_cast<uint32_t>(header->tilesDown) * static_cast<uint32_t>(header->tilesAcross) * 0x12u;
        const uint32_t quadwords = header->paletteSize != 0 ? tileCount + 0x11u : tileCount + 9u;
        sprite->packet = sdk::malloc(quadwords << 4);
        sprite->x = 0;
        sprite->y = 0;
        sprite->z = -1;
        sprite->scaleX = ps2f(1.0f);
        sprite->scaleY = ps2f(1.0f);
        sprite->blend = blend;
        sprite->frameX = 0;
        sprite->frameY = 0;
        sprite->clipWidth = static_cast<uint32_t>(sprite->image->tilesAcross) - sprite->frameX;
        sprite->clipHeight = static_cast<uint32_t>(sprite->image->tilesDown) - sprite->frameY;
        TexSprite_BuildPacket(sprite);
        File_dtor(local, -1);
        return static_cast<int>(sprite.address);
    }

    GuestPtr<Camera> Camera_DeletingDtor(GuestPtr<Camera> camera, int16_t deleteFlag)
    {
        if (camera)
        {
            BaseObject_dtor(static_cast<int>(camera.address), 0);
            if (0 < deleteFlag)
                sdk::operator_delete(camera.address);
        }
        return camera;
    }

    // Waits for the GS and starts the current list again from its beginning.
    void DmaList_Rewind()
    {
        sdk::sceGsSyncPath(0, 0);
        globals::dmaListPtr.set(port::load<uint32_t>(globals::kDmaLists + 4u * static_cast<uint32_t>(globals::dmaListIndex.get())));
    }

    // Frees the two DMA lists and the packet buffer.
    void DmaList_Free()
    {
        for (const uint32_t slot : {globals::kDmaLists, globals::kDmaLists + 4u, globals::packetBufBase.address})
        {
            if (port::load<uint32_t>(slot) != 0u)
            {
                sdk::free(port::load<uint32_t>(slot));
                port::store<uint32_t>(slot, 0u);
            }
        }
    }

    // Allocates the two 64 KB DMA lists and the 32 KB packet buffer when they do not exist yet and starts both from their
    // beginning.
    void DmaList_Init()
    {
        if (port::load<uint32_t>(globals::kDmaLists) == 0u)
        {
            port::store<uint32_t>(globals::kDmaLists, sdk::malloc(0x10010u));
            port::store<uint32_t>(globals::kDmaLists + 4u, sdk::malloc(0x10010u));
        }
        if (globals::packetBufBase.get() == 0u)
            globals::packetBufBase.set(sdk::malloc(0x8000u));
        globals::packetBufPos.set(static_cast<int32_t>(globals::packetBufBase.get()));
        globals::dmaListPtr.set(port::load<uint32_t>(globals::kDmaLists));
        DmaList_InitPackets();
    }

    PORT_FN(A3DObjectBase_DeletingDtor);
    PORT_FN(A3DObject_dtor);
    PORT_FN(A3DObject_ChangeLuminosity);
    PORT_FN(TexImage_dtor);
    PORT_FN(TexImage_Load);
    PORT_FN(TexSprite_dtor);
    PORT_FN(TexSprite_Load);
    PORT_FN(Camera_DeletingDtor);
    PORT_FN(DmaList_Rewind);
    PORT_FN(DmaList_Free);
    PORT_FN(DmaList_Init);

    // ---- Instance buffers and the VIF1 kick (plan P6 batch 2) ----

    // Two buffers of 1800 instance draw packets (0x70 bytes each), the one drawn while the other is filled. Every packet is
    // a 5-quadword DMA tag (0x10000005), a GIF tag with the PRIM state (0x01000101, 0x6C05002B), and at +0x60 the end tag
    // (0x60000000) and a VIF UNPACK header (0x14000000, 0x11000000).
    void InstanceBuf_Init()
    {
        const uint32_t first = sdk::mw_NewArray(0x31380u);
        port::store<uint32_t>(0x001BDA20u, first);
        const uint32_t second = sdk::mw_NewArray(0x31380u);
        port::store<uint32_t>(0x001BDA24u, second);
        for (uint32_t i = 0, at = 0; i < 0x708u; ++i, at += 0x70u)
            for (const uint32_t base : {first, second})
            {
                port::store<uint32_t>(base + at, 0x10000005u);
                port::store<uint32_t>(base + at + 4u, 0u);
                port::store<uint32_t>(base + at + 8u, 0x1000101u);
                port::store<uint32_t>(base + at + 0xCu, 0x6C05002Bu);
                port::store<uint32_t>(base + at + 0x60u, 0x60000000u);
                port::store<uint32_t>(base + at + 0x64u, 0u);
                port::store<uint32_t>(base + at + 0x68u, 0x14000000u);
                port::store<uint32_t>(base + at + 0x6Cu, 0x11000000u);
            }
    }

    // Sends a DMA chain to VIF1 at once: a two-tag chain at 0x1F8100 (a call to the list at `address`, then the end tag), the
    // GS path waited for, the channel started (CHCR 0x145) and the GS path waited for again; returns that last wait.
    int Dma_KickVif1Now(uint32_t address)
    {
        constexpr uint32_t kChain = 0x001F8100u;
        constexpr uint32_t kQwc = 0x10009020u, kTadr = 0x10009030u, kChcr = 0x10009000u, kStat = 0x1000E010u;
        port::store<uint32_t>(kChain + 0xCu, 0u);
        port::store<uint32_t>(kChain + 8u, 0u);
        port::store<uint32_t>(kChain + 4u, address & 0x7FFFFFFFu);
        port::store<uint32_t>(kChain, 0x50000000u);
        port::store<uint32_t>(kChain + 0x1Cu, 0u);
        port::store<uint32_t>(kChain + 0x18u, 0u);
        port::store<uint32_t>(kChain + 0x14u, 0u);
        port::store<uint32_t>(kChain + 0x10u, 0x70000000u);
        sdk::sceGsSyncPath(0, 0);
        port::writeHardware32(kQwc, 0u);
        port::writeHardware32(kTadr, kChain & 0x7FFFFFFFu);
        port::writeHardware32(kStat, 2u);
        sdk::FlushCache(0);
        port::writeHardware32(kChcr, 0x145u);
        return sdk::sceGsSyncPath(0, 0);
    }

    PORT_FN(InstanceBuf_Init);
    PORT_FN(Dma_KickVif1Now);

    // The same kick, entered from the second copy of the code.
    int Dma_KickVif1Now_1490D0(uint32_t address) { return Dma_KickVif1Now(address); }

    // The instance base, then the shared draw buffers once for the first model of the run; the model count (0x1BDA2C) grows.
    GuestPtr<A3DObject> InstanceModel_ctor(GuestPtr<A3DObject> model)
    {
        InstanceBase_ctor(model);
        constexpr uint32_t kModelCount = 0x001BDA2Cu;
        if (port::load<int32_t>(kModelCount) == 0)
            InstanceBuf_Init();
        port::store<int32_t>(kModelCount, port::load<int32_t>(kModelCount) + 1);
        return model;
    }

    PORT_FN(Dma_KickVif1Now_1490D0);
    PORT_FN(InstanceModel_ctor);

    // The constructor of the derived instance model: the base one, nothing more.
    GuestPtr<A3DObject> InstanceModel_ctor2(GuestPtr<A3DObject> model)
    {
        InstanceModel_ctor(model);
        return model;
    }

    PORT_FN(InstanceModel_ctor2);

    // ---- The city renderer (plan P6 batch 2) ----

    // The map with people below it, then the buffers and the draw layers the renderer owns: two buffers of 0x8700 bytes
    // (+0x2D18, +0x2D1C) and two of 0x9C00 (+0x2D0C, +0x2D10); the render and terrain parameters; the two terrain packets
    // built at the packet buffer's position (which moves on past each); and the layers D, E, F, G, C, B and A
    // (+0x2F74, +0x2F78, +0x2F7C, +0x2F80, +0x2F88, +0x2F84, +0x2F8C), the ones that take buffers getting the pair at +0x2D0C.
    GuestPtr<CityMap> CityRender_ctor(GuestPtr<CityMap> map)
    {
        const uint32_t m = map.address;
        MapPeople_ctor(map);
        port::store<uint32_t>(m + 0x64u, 0x001BCF00u);
        port::store<uint32_t>(m + 0x2D18u, sdk::malloc(0x8700u));
        port::store<uint32_t>(m + 0x2D1Cu, sdk::malloc(0x8700u));
        port::store<uint32_t>(m + 0x2D0Cu, sdk::malloc(0x9C00u));
        port::store<uint32_t>(m + 0x2D10u, sdk::malloc(0x9C00u));
        CityRender_InitParams(map);
        Terrain_InitParams(static_cast<int>(m));
        PacketBuf_Seek(static_cast<int>(Terrain_BuildVu1PacketB(static_cast<int>(m), GuestPtr<uint32_t>{static_cast<uint32_t>(PacketBuf_Tell())}).address));
        PacketBuf_Seek(static_cast<int>(Terrain_BuildVu1PacketA(static_cast<int>(m), GuestPtr<uint32_t>{static_cast<uint32_t>(PacketBuf_Tell())}).address));
        const auto make = [&](uint32_t size, uint32_t field, auto construct) {
            const uint32_t memory = sdk::operator_new(size);
            port::store<uint32_t>(m + field, memory != 0u ? construct(memory) : 0u);
        };
        make(0x410, 0x2F74, [&](uint32_t o) { return LayerD_ctor(GuestPtr<LayerD>{o}, static_cast<int>(m + 0x2D0Cu)).address; });
        make(0x6E0, 0x2F78, [&](uint32_t o) { return LayerE_ctor(GuestPtr<LayerE>{o}, static_cast<int>(m + 0x2D0Cu)).address; });
        make(0x140, 0x2F7C, [&](uint32_t o) { return LayerF_ctor(GuestPtr<LayerF>{o}).address; });
        make(0x2E0, 0x2F80, [&](uint32_t o) { return LayerG_ctor(GuestPtr<LayerG>{o}, static_cast<int>(m + 0x2D0Cu)).address; });
        make(0x240, 0x2F88, [&](uint32_t o) { return LayerC_ctor(GuestPtr<LayerC>{o}).address; });
        make(0x230, 0x2F84, [&](uint32_t o) { return LayerB_ctor(GuestPtr<LayerB>{o}).address; });
        make(0x2440, 0x2F8C, [&](uint32_t o) { return LayerA_ctor(GuestPtr<LayerA>{o}).address; });
        return map;
    }

    // The layers are deleted (A, D, E, F, G, C, B), then the four buffers, then the map below the renderer.
    GuestPtr<CityMap> CityRender_dtor(GuestPtr<CityMap> map, int16_t deleteFlag)
    {
        if (map)
        {
            const uint32_t m = map.address;
            port::store<uint32_t>(m + 0x64u, 0x001BCF00u);
            lifetime::deleteWith(m + 0x2F8Cu, [](uint32_t o) { LayerA_DeletingDtor(GuestPtr<LayerA>{o}, 1); });
            lifetime::deleteWith(m + 0x2F74u, [](uint32_t o) { LayerD_DeletingDtor(GuestPtr<LayerD>{o}, 1); });
            lifetime::deleteWith(m + 0x2F78u, [](uint32_t o) { LayerE_DeletingDtor(GuestPtr<LayerE>{o}, 1); });
            lifetime::deleteWith(m + 0x2F7Cu, [](uint32_t o) { LayerF_DeletingDtor(GuestPtr<LayerF>{o}, 1); });
            lifetime::deleteWith(m + 0x2F80u, [](uint32_t o) { LayerG_DeletingDtor(GuestPtr<LayerG>{o}, 1); });
            lifetime::deleteWith(m + 0x2F88u, [](uint32_t o) { LayerC_DeletingDtor(GuestPtr<LayerC>{o}, 1); });
            lifetime::deleteWith(m + 0x2F84u, [](uint32_t o) { LayerB_DeletingDtor(GuestPtr<LayerB>{o}, 1); });
            for (const uint32_t field : {0x2D0Cu, 0x2D10u, 0x2D18u, 0x2D1Cu})
                lifetime::deleteWith(m + field, [](uint32_t o) { sdk::free(o); });
            MapPeople_dtor(map, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(m);
        }
        return map;
    }

    // The renderer constructor and deleting destructor entered through the map's class (table at 0x1BCED0).
    GuestPtr<CityMap> CityMap_ctor(GuestPtr<CityMap> map)
    {
        CityRender_ctor(map);
        port::store<uint32_t>(map.address + 0x64u, 0x001BCED0u);
        return map;
    }

    GuestPtr<CityMap> CityMap_DeletingDtor(GuestPtr<CityMap> map, int16_t deleteFlag)
    {
        if (map)
        {
            port::store<uint32_t>(map.address + 0x64u, 0x001BCED0u);
            CityRender_dtor(map, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(map.address);
        }
        return map;
    }

    PORT_FN(CityRender_ctor);
    PORT_FN(CityRender_dtor);
    PORT_FN(CityMap_ctor);
    PORT_FN(CityMap_DeletingDtor);

    // ---- The city camera and the sky (plan P6 batch 2) ----

    // Loads the sky model (file 0x1B7F50) and drops it again at once, then sends an image transfer to the GS (BITBLTBUF,
    // TRXPOS, TRXREG and TRXDIR set up, then the data at 0x1B0DD0) straight to VIF1.
    void Sky_Load()
    {
        port::GuestFrame<0xA0> frame;
        const uint32_t packet = frame.ptr().address + 0x10u;
        uint32_t model = sdk::operator_new(0x60u);
        if (model != 0u)
            model = A3DObject_Load(GuestPtr<A3DObject>{model}, 0x001B7F50, 1).address;
        A3DObject_dtor(GuestPtr<A3DObject>{model}, 1);
        const uint32_t words[] = {0x10000006u, 0u, 0x11000000u, 0x50000006u,
                                  4u, 0x10000000u, 0xEu, 0u,
                                  0u, 0u, 0x51u, 0u,
                                  8u, 2u, 0x52u, 0u,
                                  0u, 0x13F2Eu, 0x50u, 0u,
                                  0u, 0u, 0x53u, 0u,
                                  0x8004u, 0x08000000u, 0u, 0u,
                                  0x30000004u, 0x001B0DD0u, 0u, 0x50000004u,
                                  0x60000000u, 0u, 0u, 0u};
        for (uint32_t i = 0; i < 36u; ++i)
            port::store<uint32_t>(packet + 4u * i, words[i]);
        Dma_KickVif1Now(packet);
    }

    // The camera, the sky load and the sky packets.
    GuestPtr<CityCamera> CityCamera_BaseCtor(GuestPtr<CityCamera> camera)
    {
        port::GuestFrame<0x20> frame; // the original's frame: the sky packet's address lands in the DMA chain
        Camera_ctor(GuestPtr<Camera>{camera.address});
        Sky_Load();
        Sky_BuildPackets(camera);
        return camera;
    }

    GuestPtr<CityCamera> CityCamera_ctor(GuestPtr<CityCamera> camera)
    {
        port::GuestFrame<0x20> frame;
        CityCamera_BaseCtor(camera);
        port::store<uint32_t>(camera.address + 0x130u, 0u);
        port::store<uint32_t>(camera.address + 0x134u, 0xC1E80000u);
        port::store<uint32_t>(camera.address + 0x138u, 0x3FCCCCCDu);
        port::store<uint32_t>(camera.address + 0x13Cu, 0x3F800000u);
        port::store<uint32_t>(camera.address + 0x530u, 1u);
        return camera;
    }

    PORT_FN(Sky_Load);
    PORT_FN(CityCamera_BaseCtor);
    PORT_FN(CityCamera_ctor);

    // The asynchronous model load; when it succeeded (1) for a synchronous call (a2 is 0), the model keeps its mesh table's count
    // divided by 13 at +0x60 (the count at +4 of the table at +8 of its data).
    int InstanceModel_LoadAsync(GuestPtr<A3DObject> model, GuestPtr<GameFile> gamefile, int a2)
    {
        const int result = A3DObject_LoadAsync(model, gamefile, a2);
        if (result == 1 && a2 == 0)
            port::store<int32_t>(model.address + 0x60u, divide(port::load<int32_t>(port::load<uint32_t>(port::load<uint32_t>(model.address) + 8u) + 4u), 13));
        return result;
    }

    PORT_FN(InstanceModel_LoadAsync);

    namespace
    {
        // The tags of the DMA chain at `chain` that point at data (REF and CALL) hold a word count from `base` in their address word;
        // this turns it into an address. `reader` is the chain reader's 0x30 bytes of working memory.
        void relocateChain(uint32_t reader, uint32_t chain, uint32_t base)
        {
            DmaChain_Begin(static_cast<int>(reader), static_cast<int>(chain));
            int more;
            do
            {
                more = DmaChain_Next(static_cast<int>(reader));
                const uint32_t id = port::load<uint32_t>(reader + 8u);
                if (id == 0x30000000u || id == 0x50000000u)
                {
                    const uint32_t tag = port::load<uint32_t>(reader + 4u);
                    port::store<uint32_t>(tag + 4u, base + 4u * port::load<uint32_t>(tag + 4u));
                }
            } while (more != 0);
        }
    }

    // Turns the word offsets of a loaded .A3D image (the model's data at +0; its header has the offsets of five tables at +4..+0x14
    // and the counts of records A, B, C and the chain blocks as halfwords at +0x18 to +0x1E) into addresses: the four tables, the
    // first word of each 0xC0-byte record A (and, when `indexed`, its second word as an index into table B, with -1 for none; when not,
    // table B's own entries), the chain blocks of B (their size at +4) and of D (their size at +0), whose DMA tags that point at data
    // are relocated as well, and the groups of table C (the model keeps a table of their addresses at +0x50, and the entry count of a
    // group at +8). The texture packets (the second block, at +4 of the model, `D[1]` words) are relocated against that block
    // and sent to the GS as they come.
    void A3D_Relocate(GuestPtr<A3DObject> model, int indexed)
    {
        port::GuestFrame<0xE0> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t self = model.address;
        const auto data = [&]() { return port::load<uint32_t>(self); };
        for (uint32_t at = 8u; at <= 0x14u; at += 4u)
            port::store<uint32_t>(data() + at, data() + 4u * port::load<uint32_t>(data() + at));
        uint32_t record = port::load<uint32_t>(data() + 8u);
        for (int i = 0; i < port::load<int16_t>(data() + 0x18u); ++i, record += 0xC0u)
            if (port::load<uint32_t>(record) != 0)
                port::store<uint32_t>(record, data() + 4u * port::load<uint32_t>(record));
        uint32_t block;
        if (indexed != 0)
        {
            record = port::load<uint32_t>(data() + 8u);
            for (int i = 0; i < port::load<int16_t>(data() + 0x18u); ++i, record += 0xC0u)
            {
                const int32_t index = port::load<int32_t>(record + 4u);
                if (index == -1)
                    port::store<uint32_t>(record + 4u, 0u);
                else
                    port::store<uint32_t>(record + 4u, data() + 4u * port::load<uint32_t>(port::load<uint32_t>(data() + 0xCu) + 4u * static_cast<uint32_t>(index)));
            }
            block = data() + 4u * port::load<uint32_t>(port::load<uint32_t>(data() + 0xCu));
        }
        else
        {
            for (int i = 0; i < port::load<int16_t>(data() + 0x1Au); ++i)
            {
                const uint32_t slot = port::load<uint32_t>(data() + 0xCu) + 4u * static_cast<uint32_t>(i);
                port::store<uint32_t>(slot, data() + 4u * port::load<uint32_t>(slot));
            }
            block = port::load<uint32_t>(port::load<uint32_t>(data() + 0xCu));
        }
        for (int i = 0; i < port::load<int16_t>(data() + 0x1Au); ++i)
        {
            port::store<uint32_t>(block, data() + 4u * port::load<uint32_t>(block));
            relocateChain(sp + 0x50u, block + 0x10u, data());
            block += 4u * port::load<uint32_t>(block + 4u);
        }
        if (port::load<int16_t>(data() + 0x1Cu) == 0)
            port::store<uint32_t>(self + 8u, 0u);
        else
        {
            uint32_t cursor = port::load<uint32_t>(data() + 0x10u);
            port::store<uint32_t>(self + 8u, port::load<uint32_t>(cursor));
            cursor += 0x10u;
            port::store<uint32_t>(data() + 0x10u, cursor);
            for (int group = 0; group < port::load<int16_t>(data() + 0x1Cu); ++group)
            {
                port::store<uint32_t>(port::load<uint32_t>(self + 0x50u) + 4u * static_cast<uint32_t>(group), cursor);
                uint32_t total = 0;
                for (int entry = 0; entry < port::load<int32_t>(self + 8u); ++entry)
                {
                    const uint32_t count = port::load<uint32_t>(cursor);
                    port::store<uint32_t>(cursor + 4u, data() + 4u * port::load<uint32_t>(cursor + 4u));
                    total += count;
                    port::store<uint32_t>(cursor + 8u, data() + 4u * port::load<uint32_t>(cursor + 8u));
                    cursor += 0x10u;
                }
                cursor += total << 5;
            }
        }
        block = port::load<uint32_t>(data() + 0x14u);
        for (int i = 0; i < port::load<int16_t>(data() + 0x1Eu); ++i)
        {
            relocateChain(sp + 0x80u, block + 0x10u, data());
            block += 4u * port::load<uint32_t>(block);
        }
        uint32_t remaining = port::load<uint32_t>(data() + 4u);
        if (remaining != 0)
        {
            uint32_t packet = port::load<uint32_t>(self + 4u);
            do
            {
                relocateChain(sp + 0xB0u, packet + 0x10u, port::load<uint32_t>(self + 4u));
                Dma_KickVif1Now(packet + 0x10u);
                const uint32_t size = port::load<uint32_t>(packet);
                remaining -= size;
                packet += 4u * size;
            } while (remaining != 0);
        }
    }

    PORT_FN(A3D_Relocate);

    // Loads a model (.A3D) from an open file, one step per call (the step is in 0x1BDA10, the request being waited for in 0x1BDA14,
    // and the 16-byte header lands in 0x1C5100: the size of the file in words, then of the texture block in words, then the counts):
    // 0 queue the header read, 1 wait for it (an empty header, a size of 0, is the end of the file: -1), 2 make the data block of the
    // size and queue the rest of the file after a copy of the header, 3 wait, 4 make the table of the model's meshes (+0x50, the
    // count is a halfword at +0x1C of the data) and the texture block (+4) and queue its read, 5 wait, 6 relocate the data
    // (mm_A3D_Relocate, which also sends the textures), free the texture block, reset the model's matrix at +0x10 and finish
    // (1). 0 means still busy.
    int A3DObject_LoadAsync(GuestPtr<A3DObject> model, GuestPtr<GameFile> file, int indexed)
    {
        constexpr uint32_t kHeader = 0x001C5100u;
        const GuestVar<int32_t> state{0x001BDA10u}, request{0x001BDA14u};
        const uint32_t m = model.address;
        const auto done = [&]() { return FileQueue_IsDone(static_cast<int>(file.address), request.get()) != 0; };
        switch (static_cast<uint32_t>(state.get()))
        {
        case 0:
            port::store<uint32_t>(m, 0u);
            port::store<uint32_t>(m + 4u, 0u);
            port::store<uint32_t>(m + 0x50u, 0u);
            request.set(File_Read(file, static_cast<int>(kHeader), 1, 0x10, 0));
            state.set(state.get() + 1);
            return 0;
        case 1:
            if (!done())
                return 0;
            if (port::load<int32_t>(kHeader) == 0)
            {
                state.set(0);
                return -1;
            }
            state.set(state.get() + 1);
            return 0;
        case 2:
            port::store<uint32_t>(m, sdk::malloc(port::load<uint32_t>(kHeader) << 2));
            sdk::memcpy(port::load<uint32_t>(m), kHeader, 0x10);
            request.set(File_Read(file, static_cast<int>(port::load<uint32_t>(m)) + 0x10, port::load<int32_t>(kHeader) - 4, 4, 0));
            state.set(state.get() + 1);
            return 0;
        case 3:
        case 5:
            if (!done())
                return 0;
            state.set(state.get() + 1);
            return 0;
        case 4:
        {
            const int32_t meshes = port::load<int16_t>(port::load<uint32_t>(m) + 0x1Cu);
            if (meshes != 0)
                port::store<uint32_t>(m + 0x50u, sdk::mw_NewArray(static_cast<uint32_t>(meshes) << 2));
            port::store<uint32_t>(m + 4u, sdk::malloc(port::load<uint32_t>(kHeader + 4u) << 2));
            request.set(File_Read(file, static_cast<int>(port::load<uint32_t>(m + 4u)), port::load<int32_t>(kHeader + 4u), 4, 0));
            state.set(state.get() + 1);
            return 0;
        }
        case 6:
        {
            A3D_Relocate(model, indexed);
            const uint32_t textures = port::load<uint32_t>(m + 4u);
            if (textures != 0u)
            {
                sdk::free(textures);
                port::store<uint32_t>(m + 4u, 0u);
            }
            Mat4_Copy(GuestPtr<Vec4>{m + 0x10u}, GuestPtr<Vec4>{0x001B2550u});
            state.set(0);
            return 1;
        }
        default:
            return 0;
        }
    }

    PORT_FN(A3DObject_LoadAsync);

    // Sets up the display: the display environment (0x1F5B10, a frame 8 units on: its first fields as the flag bits below), the draw
    // environment (0x1F5B20) for the given width and a height worked out from the screen's, the double buffer (0x1F5AC0: width by
    // half the height, cleared) and a GIF packet made in a 0x60 byte block that sets five GS registers (the one tag at 0x1B2540
    // opens it; registers 6, 1, 5 and 5 again with 0x96002800) and is sent on DMA channel 2 before the block is freed.
    void Gs_InitDisplay(uint16_t width, int height)
    {
        port::GuestFrame<0x80> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t tag = sp + 0x60u, packet = sp + 0x70u;
        port::store<uint64_t>(tag, port::load<uint64_t>(0x001B2540u));
        port::store<uint64_t>(tag + 8u, port::load<uint64_t>(0x001B2548u));
        const uint32_t block = sdk::memalign(0x10, 0x60);
        const int channel = sdk::sceDmaGetChan(2);
        constexpr uint32_t kDisplay = 0x001F5B10u, kDraw = 0x001F5B20u, kBuffer = 0x001F5AC0u;
        port::store<uint64_t>(kDisplay, 0u);
        port::store<uint64_t>(kDisplay + 8u, 0u);
        port::store<uint16_t>(kDisplay, static_cast<uint16_t>((port::load<uint16_t>(kDisplay) & 0x8000u) | 0x8u));
        port::store<uint8_t>(kDisplay + 1u, static_cast<uint8_t>((port::load<uint8_t>(kDisplay + 1u) & ~0x80u) | 0x80u));
        port::store<uint8_t>(kDisplay + 7u, static_cast<uint8_t>((port::load<uint8_t>(kDisplay + 7u) & 0x0Fu) | 0x10u));
        port::store<uint8_t>(kDisplay + 8u, static_cast<uint8_t>((port::load<uint8_t>(kDisplay + 8u) & 0xF0u) | 0xEu));
        const int16_t wide = static_cast<int16_t>(width);
        const int half = height / 2;
        const int pages = ((half + 0x1F) / 32 + (height + 0x1F) / 32) << 6;
        sdk::sceGsSetDefDrawEnv(kDraw, 0, wide, static_cast<int16_t>(pages), 0, 0);
        port::store<uint64_t>(kDisplay + 0x30u, 0u);
        sdk::FlushCache(0);
        sdk::sceGsSyncPath(0, 0);
        sdk::sceGsPutDispEnv(kDisplay);
        sdk::sceGifPkInit(packet, block);
        sdk::sceGifPkReset(packet);
        sdk::sceGifPkEnd(packet);
        sdk::sceGifPkOpenGifTag(packet, port::Quad{port::load<uint64_t>(tag), port::load<uint64_t>(tag + 8u)});
        sdk::sceGifPkAddGsAD(packet, 0, 6);
        sdk::sceGifPkAddGsAD(packet, 1, 0);
        sdk::sceGifPkAddGsAD(packet, 5, 0);
        sdk::sceGifPkAddGsAD(packet, 5, 0x96002800u);
        sdk::sceGifPkCloseGifTag(packet);
        sdk::sceGifPkTerminate(packet);
        sdk::FlushCache(0);
        sdk::sceGsSyncPath(0, 0);
        sdk::sceDmaSend(channel, port::load<uint32_t>(packet + 4u));
        sdk::sceGsSyncPath(0, 0);
        sdk::free(block);
        sdk::sceGsSetDefDBuff(kBuffer, 0, wide, static_cast<int16_t>(half), 0, 0, 1);
        sdk::FlushCache(0);
    }

    PORT_FN(Gs_InitDisplay);
}
