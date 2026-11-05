// Graphics helpers: DMA lists and the packet buffer, model accessors, render layer and light state.

#include "game_functions.h"
#include "game_globals.h"
#include "lifetime.h"
#include "render.h"
#include "sdk.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace mm
{
    using packet::addressOf;
    using packet::putFloats;
    using packet::putHalvesStq;
    using packet::putWords;
    using packet::tagAddress;

    // The length of animation `animation` of the model: its first track's length word.
    ps2f A3D_GetMeshField0C(GuestPtr<A3DObject> model, int animation)
    {
        const GuestPtr<A3DTrack> tracks{model->animations[animation]};
        return tracks->length;
    }

    // A DMA chain reader starts at `chain`. The ID is set to END so that the first DmaChain_Next stays on the first tag.
    int DmaChain_Begin(GuestPtr<DmaChainReader> reader, int chain)
    {
        reader->tag = static_cast<uint32_t>(chain);
        reader->id = packet::kEnd;
        return static_cast<int>(reader.address);
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
            packet::putCallThenEnd(ptr, target);
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
    int DmaChain_Next(GuestPtr<DmaChainReader> r)
    {
        const uint32_t id = r->id;
        if (id != packet::kEnd)
        {
            if (id == packet::kCnt)
                r->tag = r->tag + (r->qwc + 1u) * 16u; // past its data
            else if (id == packet::kRet)
                return 0;
            else
                r->tag = r->tag + 16u;
        }
        const GuestPtr<uint32_t> tag{r->tag};
        r->id = tag[0] & 0x70000000u;
        r->qwc = port::load<uint16_t>(tag.address);
        r->address = tag[1];
        r->vifCode0 = tag[2];
        r->vifCode1 = tag[3];
        r->vifCommand0 = tag[2] & 0x7F000000u;
        r->vifCommand1 = tag[3] & 0x7F000000u;
        r->data = tag.address + 16u;
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
        layer->marker = GuestPtr<A3DObject>{port::load<uint32_t>(0x001F3B1Cu)};
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
    // 0x3D (FOGCOL, the fog colour: mm_LightSet_SetField20 sets it), a ref to the camera's 0x12 quadwords from its
    // first screen matrix (both screen matrices, the light matrices, the fog vectors) unpacked to VU1 address 0, and a ret.
    void Camera_BuildPacket(GuestPtr<Camera> camera)
    {
        camera->packet = static_cast<uint32_t>(PacketBuf_Tell());
        const GuestPtr<uint32_t> p{camera->packet};
        const uint32_t words[20] = {
            0x10000002u, 0, 0x11000000u, 0x50000002u,                       // cnt 2; VIF flush, direct 2
            0x00008001u, 0x10000000u, 0x0000000Eu, 0,                       // GIF tag: 1 loop, eop, A+D
            0, 0, 0x0000003Du, 0,                                            // register 0x3D = 0
            0x30000012u, tagAddress(camera->worldScreenA), 0x01000101u, 0x6C120000u,   // ref 0x12; stcycl, unpack v4-32
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
        Dma_AppendCall(static_cast<int>(addressOf(camera->skyBands)));
        Dma_AppendCall(static_cast<int>(addressOf(camera->skyGlow)));
        Dma_AppendCall(static_cast<int>(addressOf(camera->skyPicture)));
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
        LayerC_DrawBuildings(map->placement, static_cast<int>(map->chunkPackets[parity]), map, pass);
    }

    void Map_SetPlacementHighlight(GuestPtr<CityMap> map, int mode, GuestPtr<int32_t> rect, GuestPtr<uint32_t> colour)
    {
        Terrain_SetVec(map->placement, mode, rect, colour);
    }

    // The terrain: the base ground pass over this frame's chunk packet, the cliff sides, then layers B and F over the
    // same chunk packet.
    void CityRender_DrawOverlays(GuestPtr<CityMap> map, GuestPtr<CityScene>)
    {
        const uint32_t parity = port::load<uint32_t>(0x001BDCD0u) & 1u;
        const int packet = static_cast<int>(map->chunkPackets[parity]);
        Dma_AppendCall(static_cast<int>(map->groundPacket));
        Dma_AppendCall(packet);
        Dma_AppendCall(static_cast<int>(map->sidePackets[parity]));
        LayerB_Draw(map->layerB, packet);
        LayerF_Draw(map->layerF, packet);
    }

    // ---- model instances

    namespace
    {
        // A node's matrices; the world matrix's last word doubles as the "up to date" mark, cleared before an update.
        GuestPtr<Vec4> localMatrix(GuestPtr<A3DNode> node) { return GuestPtr<Vec4>::of(node->local[0]); }
        bool worldMatrixDone(GuestPtr<A3DNode> node) { return !(node->slot.matrix[3].w == ps2f(0.0f)); }
        void clearWorldMatrixDone(GuestPtr<A3DNode> node) { port::store<int32_t>(addressOf(node->slot.matrix[3].w), 0); }
        // A node's DMA call target: its draw packet, or its mesh's chain.
        int packetOf(GuestPtr<A3DNode> node) { return static_cast<int>(addressOf(node->slot)); }
        int chainOf(GuestPtr<A3DMesh> mesh) { return static_cast<int>(addressOf(mesh->chain)); }
    }

    // A node's world matrix: its local matrix times its parent's world matrix, or the model's for a root. A parent whose
    // world matrix is not done yet is done first, its own parent recursively. (The original multiplies inline with the
    // same VU0 registers as Mat4_MulVu0.)
    void A3DObject_UpdateNodeMatrix(GuestPtr<A3DObject> model, GuestPtr<A3DNode> node)
    {
        const GuestPtr<A3DNode> parent = node->parent;
        if (!parent)
        {
            Mat4_MulVu0(worldMatrix(node), localMatrix(node), GuestPtr<Vec4>::of(model->matrix[0]));
            return;
        }
        if (!worldMatrixDone(parent))
        {
            const GuestPtr<A3DNode> grandparent = parent->parent;
            if (!grandparent)
                Mat4_MulVu0(worldMatrix(parent), localMatrix(parent), GuestPtr<Vec4>::of(model->matrix[0]));
            else
            {
                if (!worldMatrixDone(grandparent))
                    A3DObject_UpdateNodeMatrix(model, grandparent);
                Mat4_MulVu0(worldMatrix(parent), localMatrix(parent), worldMatrix(grandparent));
            }
        }
        Mat4_MulVu0(worldMatrix(node), localMatrix(node), worldMatrix(parent));
    }

    // Every node's world matrix: all marked not done, then each brought up to date after its parent.
    void A3DObject_UpdateMatrices(GuestPtr<A3DObject> model)
    {
        for (int i = 0; i < model->model->nodeCount; ++i)
            clearWorldMatrixDone(model->model->nodes + i);
        for (int i = 0; i < model->model->nodeCount; ++i)
        {
            const GuestPtr<A3DNode> node = model->model->nodes + i;
            const GuestPtr<A3DNode> parent = node->parent;
            if (!parent)
            {
                Mat4_MulVu0(worldMatrix(node), localMatrix(node), GuestPtr<Vec4>::of(model->matrix[0]));
                continue;
            }
            if (parent->slot.matrix[3].w == ps2f(0.0f))
                A3DObject_UpdateNodeMatrix(model, parent);
            Mat4_MulVu0(worldMatrix(node), localMatrix(node), worldMatrix(parent));
        }
    }

    // A whole model: every node's world matrix is updated, then every node with a mesh is queued: its draw packet, then
    // its mesh. The node count is read again on every step, as in the original.
    void A3DObject_Draw(GuestPtr<A3DObject> model)
    {
        for (int i = 0; i < model->model->nodeCount; ++i)
            clearWorldMatrixDone(model->model->nodes + i);
        for (int i = 0; i < model->model->nodeCount; ++i)
            A3DObject_UpdateNodeMatrix(model, model->model->nodes + i);
        const GuestPtr<A3DNode> nodes = model->model->nodes;
        for (int i = 0; i < model->model->nodeCount; ++i)
        {
            const GuestPtr<A3DNode> node = nodes + i;
            if (node->mesh)
            {
                Dma_AppendCall(packetOf(node));
                Dma_AppendCall(chainOf(node->mesh));
            }
        }
    }

    // One node of a model: its draw packet, then its mesh if it has one.
    void A3DObject_DrawNode(GuestPtr<A3DObject> model, int index)
    {
        Dma_AppendCall(packetOf(model->model->nodes + index));
        const GuestPtr<A3DMesh> mesh = model->model->nodes[index].mesh;
        if (mesh)
            Dma_AppendCall(chainOf(mesh));
    }

    GuestPtr<A3DObject> InstanceBase_ctor(GuestPtr<A3DObject> model)
    {
        BaseObject_ctor(static_cast<int>(model.address));
        port::store<uint32_t>(0x001BDA10u, 0);
        return model;
    }

    // Keyframe animation `anim` of a model at `time`: in each node track, the pair of keys around the time sets the
    // node's local matrix (rotation rows, then the position in the last row). The quaternions are taken the short way
    // round (the later key is negated in place when the dot product is below 0) and slerped, or blended linearly when
    // nearly equal (1 - dot <= 0.001); the position is blended linearly. Instance_DrawSwaying2 is the same code, only
    // skipping tracks without keys a step earlier.
    void Instance_DrawSwaying(GuestPtr<A3DObject> model, int anim, ps2f time)
    {
        GuestPtr<A3DTrack> track{model->animations[anim]};
        for (int i = 0; i < model->trackCount; ++i, ++track)
        {
            const int keys = track->keyCount;
            const GuestPtr<ps2f> node{track->node.address}; // words 4..19: the node's local matrix
            GuestPtr<A3DKey> key = track->keys;
            for (int k = 0; k < keys - 1; ++k, ++key)
            {
                // a[0..2] the position, a[3] the time, a[4..7] the rotation
                const GuestPtr<ps2f> a{key.address}, b{(key + 1).address};
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
        const int16_t nodes = model->model->nodeCount;
        if (nodes < 0x708 - globals::instanceCount.get())
            Instance_DrawSwaying(model, a1, phase);
    }

    namespace
    {
        constexpr int kMaxInstances = 0x708;

        // Fills the next instance slot with a matrix, flags and colour and queues it.
        void queueSlot(GuestPtr<Vec4> matrix, GuestPtr<Vec4> colour, int flags)
        {
            const uint32_t buffer = port::load<uint32_t>(globals::kInstanceBuffers + 4u * static_cast<uint32_t>(globals::instanceBuffer.get()));
            const GuestPtr<InstanceSlot> slot = GuestPtr<InstanceSlot>{buffer} + globals::instanceCount.get();
            for (int row = 0; row < 4; ++row)
                slot->matrix[row] = matrix[row];
            port::store<int32_t>(addressOf(slot->matrix[3].w), flags);
            slot->colour = *colour;
            Dma_AppendCall(static_cast<int>(slot.address));
        }

        // An instance slot, then the mesh of node `node`, when it has one (models whose nodes hold mesh addresses).
        void queueInstance(GuestPtr<A3DObject> model, GuestPtr<Vec4> matrix, GuestPtr<Vec4> colour, int node, int flags)
        {
            queueSlot(matrix, colour, flags);
            const GuestPtr<A3DMesh> mesh = model->model->nodes[node].mesh;
            if (mesh)
                Dma_AppendCall(chainOf(mesh));
            globals::instanceCount.set(globals::instanceCount.get() + 1);
        }

        // For models whose nodes hold mesh indexes (-1 = none): an instance slot, then mesh `index` from the mesh table.
        void queueTableMesh(GuestPtr<A3DObject> model, GuestPtr<Vec4> matrix, GuestPtr<Vec4> colour, int index, int flags)
        {
            queueSlot(matrix, colour, flags);
            Dma_AppendCall(chainOf(GuestPtr<A3DMesh>{model->model->meshes[index]}));
            globals::instanceCount.set(globals::instanceCount.get() + 1);
        }

        // A node's world matrix by index.
        GuestPtr<Vec4> nodeMatrix(GuestPtr<A3DObject> model, int node) { return worldMatrix(modelNode(model, node)); }

        // The mesh index a node of a table model holds in its mesh word.
        int32_t meshIndex(GuestPtr<A3DNode> node) { return static_cast<int32_t>(node->mesh.address); }
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
        if (model->model->nodeCount + reserve >= kMaxInstances - globals::instanceCount.get())
            return;
        A3DObject_UpdateMatrices(model);
        const GuestPtr<A3DNode> nodes = model->model->nodes;
        for (int i = 0; i < model->model->nodeCount; ++i)
        {
            const GuestPtr<A3DNode> node = nodes + i;
            if (!node->mesh)
                continue;
            queueInstance(model, worldMatrix(node), colour, i, flags);
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
        const GuestPtr<A3DNode> nodes = model->model->nodes;
        const auto matrix = [&](int i) { return worldMatrix(nodes + i); };
        const auto meshOf = [&](int i) { return meshIndex(nodes + i); };
        const auto count = [&] { return model->model->nodeCount; };
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
    // has a mesh, with mesh (its place from node 4) + group x the model's group size (an InstanceModel's).
    // No room check; the second count rises by the node count.
    void Instance_DrawModelEx(GuestPtr<A3DObject> model, GuestPtr<Vec4> colour, int flags, int offset0, int group, int offset3,
                              int attach, int offset1)
    {
        const GuestPtr<A3DNode> nodes = model->model->nodes;
        const auto matrix = [&](int i) { return worldMatrix(nodes + i); };
        const auto meshOf = [&](int i) { return meshIndex(nodes + i); };
        const auto count = [&] { return model->model->nodeCount; };
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
            queueTableMesh(model, matrix(4 + i), colour, i + group * GuestPtr<InstanceModel>{model.address}->groupSize, flags);
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

    // The light set's two VU1 packets, one per model program: each uploads the colour table and what follows (26
    // quadwords) to VU1 address 0x12, calls the program's upload chain and sets the VIF double-buffer base and offset.
    void LightSet_BuildPackets(GuestPtr<LightSet> lights)
    {
        const uint32_t data = tagAddress(lights->groupA);
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

    // Light set base setup: the two programs' output offsets (0x110 and 0xE0), every colour of both groups and the
    // instance colour set to 1, then its packets. Returns the set.
    int LightSet_Init(GuestPtr<LightSet> lights)
    {
        BaseObject_ctor(static_cast<int>(lights.address));
        lights->outputOffsets[0] = 0x110;
        lights->outputOffsets[1] = 0xE0;
        for (int i = 0; i < 12; ++i)
            for (Vec4 *colour : {&lights->groupA[i], &lights->groupB[i]})
            {
                colour->w = 1.0f;
                colour->z = 1.0f;
                colour->y = 1.0f;
                colour->x = 1.0f;
            }
        Vec4 &last = lights->instanceColour;
        last.w = 1.0f;
        last.z = 1.0f;
        last.y = 1.0f;
        last.x = 1.0f;
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

    // Ends the DMA list being built (a CALL of the GS defaults packet at 0x1B2760, then an END tag, unless it is already 0x4000
    // words long) and starts it on the VIF1 channel: QWC cleared, the tag address set to the list start, the DMA status
    // bit 1 acknowledged and the channel started in chain mode (0x145) after the cache is flushed.
    void DmaList_KickFrame()
    {
        constexpr uint32_t kQwc = 0x10009020u, kTadr = 0x10009030u, kChcr = 0x10009000u, kStat = 0x1000E010u;
        const uint32_t start = port::load<uint32_t>(globals::kDmaLists + 4u * globals::dmaListIndex.get());
        uint32_t cursor = globals::dmaListPtr.get();
        if (static_cast<int32_t>(cursor - start) / 4 < 0x4000)
        {
            packet::putCallThenEnd(cursor, 0x001B2760u & 0x7FFFFFFFu);
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

    // The buffer layer A fills this frame, empty.
    void LayerA_SetSource(GuestPtr<LayerA> layer, int buffer)
    {
        layer->buffer = buffer;
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
        Vec4 &colourA = lights->groupA[1], &colourB = lights->groupB[1];
        colourA.x = blend(globals::kLightColoursA, 0);
        colourA.y = blend(globals::kLightColoursA, 1);
        colourA.z = blend(globals::kLightColoursA, 2);
        colourA.w = 1.0f;
        colourB.x = blend(globals::kLightColoursB, 0);
        colourB.y = blend(globals::kLightColoursB, 1);
        colourB.z = blend(globals::kLightColoursB, 2);
        colourB.w = 1.0f;
        lights->groupB[2] = colourB;
        for (Vec4 *params : {&lights->groupA[3], &lights->groupB[3]})
        {
            params->x = 4.0f;
            params->y = 1.0f;
            params->z = 1.0f;
            params->w = 2.0f;
        }
    }

    // Word 8 of the object's first packet. Called on the camera (whose first word is its packet, mm_Camera_BuildPacket),
    // where word 8 is the data of the GS register 0x3D (FOGCOL): the fog colour.
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

    // The weather: pass 0 the rain, other passes the snow, each with this frame's chunk packet (by the parity of
    // 0x1BDCD0). The city calls it only in rain (2) or snow (3).
    void CityRender_DrawSky(GuestPtr<CityMap> map, GuestPtr<CityScene> city, int pass, GuestPtr<Vec4> a3)
    {
        const uint32_t parity = port::load<uint32_t>(0x001BDCD0u) & 1u;
        const int packet = static_cast<int>(map->chunkPackets[parity]);
        if (pass != 0)
            LayerE_Draw(map->snow, static_cast<int>(parity), packet, city->clock, a3);
        else
            LayerD_DrawSkyByClock(map->rain, static_cast<int>(parity), packet, city->clock);
    }

    PORT_FN(CityRender_DrawSky);

    // ---- batch 38

    // The water: the waves move with the weather, then this frame's packet (one per terrain buffer) is queued.
    void LayerG_Draw(GuestPtr<LayerG> layer, int pass, ps2f weather)
    {
        LayerG_AnimateWaves(layer, weather);
        Dma_AppendCall(static_cast<int>(layer->packets[pass]));
    }

    PORT_FN(LayerG_Draw);

    // ---- layer packets and draw parameter templates (batch 38)

    // Layer A's packets: the start uploads the layer's output offset and colour to VU1 address 0x2A, flushes, calls
    // the chain at 0x1A8330 and sets the VIF base and offset; the end sets ALPHA_1 (0x20_00000044) through the GIF.
    GuestPtr<uint32_t> LayerA_BuildPacket(GuestPtr<LayerA> layer, GuestPtr<uint32_t> packet)
    {
        layer->startPacket = packet.address;
        putWords(packet.address, {
            0x30000002u, tagAddress(layer->outputOffset), 0x01000101u, 0x6C02002Au, // ref 2 quadwords; stcycl, unpack v4-32 to 0x2A
            0x10000000u, 0, 0, 0x11000000u,                                         // cnt 0; VIF flush
            0x50000000u, 0x001A8330u, 0, 0,                                         // call
            0x60000000u, 0, 0x03000042u, 0x020001DFu,                               // ret; VIF base 0x42, offset 0x1DF
        });
        layer->endPacket = packet.address + 0x40u;
        putWords(packet.address + 0x40u, {
            0x10000002u, 0, 0x11000000u, 0x50000002u, // cnt 2 quadwords; flush, direct 2
            0x8001u, 0x10000000u, 0xEu, 0,            // GIF tag: one A+D register
            0x44u, 0x20u, 0x42u, 0,                   // ALPHA_1
            0x60000000u, 0, 0, 0,                     // ret
        });
        return packet + 0x20;
    }

    // Layer A: the start packet, then for each of this frame's instances its packet (ALPHA_1 with its fade) and its
    // two meshes (2n and 2n + 1) under its matrix, then the end packet.
    void LayerA_Draw(GuestPtr<LayerA> layer)
    {
        if (layer->count == 0)
            return;
        Dma_AppendCall(static_cast<int>(layer->startPacket));
        for (int i = 0; i < layer->count; ++i)
        {
            // The buffer and the model are read again for every use, as in the original.
            const auto instance = [&] { return layerAInstance(layer, i); };
            Dma_AppendCall(static_cast<int>(addressOf(instance()->packet)));
            const GuestPtr<Vec4> matrix = GuestPtr<Vec4>::of(instance()->matrix[0]);
            Instance_DrawMesh(layer->model, matrix, GuestPtr<Vec4>{globals::kDefaultColour}, instance()->meshPair * 2, 0);
            Instance_DrawMesh(layer->model, GuestPtr<Vec4>::of(instance()->matrix[0]), GuestPtr<Vec4>{globals::kDefaultColour},
                              instance()->meshPair * 2 + 1, 0);
        }
        Dma_AppendCall(static_cast<int>(layer->endPacket));
    }

    // Layer B's packets: the first calls the chain at 0x1AA340, uploads the layer's parameters (19 quadwords) to VU1
    // 0x1C, sets the VIF base and offset and starts the program; the second uploads the second pass's texture and mask
    // to 0x1D and 0x2D and starts the program again.
    GuestPtr<uint32_t> LayerB_BuildPacket(GuestPtr<LayerB> layer, GuestPtr<uint32_t> packet)
    {
        layer->packetA = packet.address;
        putWords(packet.address, {
            0x10000000u, 0, 0, 0x11000000u,                                  // cnt 0; VIF flush
            0x50000000u, 0x001AA340u, 0, 0,                                  // call
            0x30000013u, tagAddress(layer->params), 0x01000101u, 0x6C13001Cu, // ref 19 quadwords; unpack to 0x1C
            0x10000000u, 0, 0x03000056u, 0x020001D5u,                        // VIF base 0x56, offset 0x1D5
            0x10000000u, 0, 0x14000000u, 0x11000000u,                        // mscal; flush
            0x60000000u, 0, 0, 0,                                            // ret
        });
        layer->packetB = packet.address + 0x60u;
        putWords(packet.address + 0x60u, {
            0x10000000u, 0, 0, 0x11000000u,                                  // cnt 0; flush
            0x30000001u, tagAddress(layer->tex0B), 0x01000101u, 0x6C01001Du, // ref 1 quadword; unpack to 0x1D
            0x30000001u, tagAddress(layer->maskB), 0x01000101u, 0x6C01002Du, // ref 1 quadword; unpack to 0x2D
            0x10000000u, 0, 0x14000000u, 0x11000000u,                        // mscal; flush
            0x60000000u, 0, 0, 0,                                            // ret
        });
        return packet + 0x2C;
    }

    // Layer F's packet: ALPHA_1 (0x30_00000048) through the GIF, then like layer B's first packet with the chain at
    // 0x1A91C0.
    GuestPtr<uint32_t> LayerF_BuildPacket(GuestPtr<LayerF> layer, GuestPtr<uint32_t> packet)
    {
        layer->packet = packet.address;
        putWords(packet.address, {
            0x10000002u, 0, 0x11000000u, 0x50000002u,                        // cnt 2 quadwords; flush, direct 2
            0x8001u, 0x10000000u, 0xEu, 0,                                   // GIF tag: one A+D register
            0x48u, 0x30u, 0x42u, 0,                                          // ALPHA_1
            0x10000000u, 0, 0, 0x11000000u,                                  // cnt 0; flush
            0x50000000u, 0x001A91C0u, 0, 0,                                  // call
            0x30000013u, tagAddress(layer->params), 0x01000101u, 0x6C13001Cu, // ref 19 quadwords; unpack to 0x1C
            0x10000000u, 0, 0x03000056u, 0x020001D5u,                        // VIF base 0x56, offset 0x1D5
            0x10000000u, 0, 0x14000000u, 0x11000000u,                        // mscal; flush
            0x60000000u, 0, 0, 0,                                            // ret
        });
        return packet + 0x24;
    }

    // The draw parameters of the placement highlight (VU1 0x1C..0x22: two colours, the output offset, a GIF tag of 26
    // loops of 14 registers, the fill and outline PRIMs and the mask 4) and of the marker (a colour table of ones, the
    // output offset 0x110 and the instance colour).
    void LayerC_InitParams(GuestPtr<LayerC> layer)
    {
        putFloats(layer->colourA, 255.0f, 0, 0, 128.0f);
        putFloats(layer->colourB, 0, 0, 0, 128.0f);
        putWords(layer->outputOffset, {0x68u, 0, 0, 0});
        putWords(layer->gifTag, {0x801Au, 0xE0000000u, 0x04144410u, 0x00444441u});
        putWords(layer->primA, {0x44u, 0, 0, 0});
        putWords(layer->primB, {0x82u, 0, 0, 0});
        putWords(layer->mask, {0, 0, 0, 4u});
        layer->outputOffsets[0] = 0x110;
        for (Vec4 &colour : layer->colours)
            putFloats(colour, 1.0f, 1.0f, 1.0f, 1.0f);
        putFloats(layer->instanceColour, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    void LayerF_InitParams(GuestPtr<LayerF> layer)
    {
        packet::putPassParams(layer->params, {128.0f, 128.0f, 128.0f, 128.0f}, 0x9930A7A4u, 0x4004F405u, 0x7Cu);
        putWords(layer->params.mask, {0, 0, 0, 0x10u});
    }

    // Layer G (the water) keeps its texture coordinates at rest (waveBase) and copies them to both wave sets.
    void LayerG_InitParams(GuestPtr<LayerG> layer)
    {
        putFloats(layer->colour, 128.0f, 128.0f, 128.0f, 60.0f);
        putWords(layer->outputOffsets, {0x40u, 0x10u, 0, 0});
        putWords(layer->gifTag, {0x8020u, 0xD0000000u, 0x24124120u, 0x00041241u}); // 32 loops of 13
        putWords(layer->primB, {0x27Cu, 0, 0, 0});
        putWords(layer->primA, {0x7Cu, 0, 0, 0});
        putWords(layer->mask, {0, 0, 0, 2u});
        putHalvesStq(layer->waveBase);
        for (int i = 0; i < 12; ++i)
        {
            layer->wavesA[i] = layer->waveBase[i];
            layer->wavesB[i] = layer->waveBase[i];
        }
    }

    // The base ground pass's parameters: mask 1.
    void CityRender_InitParams(GuestPtr<CityMap> map)
    {
        packet::putPassParams(map->groundParams, {128.0f, 128.0f, 128.0f, 128.0f}, 0x99308000u, 0x4004B085u, 0x3Cu);
        putWords(map->groundParams.mask, {0, 0, 0, 1u});
    }

    // The cliff sides' parameters: triangles, their texture coordinates along s only.
    void Terrain_InitParams(GuestPtr<CityMap> map)
    {
        putFloats(map->sideColour, 128.0f, 128.0f, 128.0f, 128.0f);
        putWords(map->sideTex0, {0x99308000u, 0x4004B085u, 0, 0});
        putWords(map->sideOutputOffset, {0x6Cu, 0, 0, 0});
        putWords(map->sideGifTag, {0x8024u, 0xA05DC000u, 0x24124126u, 0x41u}); // 36 loops of 10
        putFloats(map->sideScale, 0.25f, 0.25f, 0, 0);
        static constexpr float kS[12] = {0, 0.5f, 0, 0.5f, 0.5f, 0, 0.5f, 1, 0.5f, 1, 1, 0.5f};
        for (int i = 0; i < 12; ++i)
            putFloats(map->sideStq[i], kS[i], 0, 1.0f, 0);
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

    // Layer B's two passes: the first over the quads with flag 0x20, the second (its own texture) over those with 0x40.
    void LayerB_InitParams(GuestPtr<LayerB> layer)
    {
        packet::putPassParams(layer->params, {128.0f, 128.0f, 128.0f, 128.0f}, 0x9930a698u, 0x4004b085u, 0x3cu);
        putWords(layer->params.mask, {0x0u, 0x0u, 0x0u, 0x20u});
        putFloats(layer->extra, 0.6f, 0.08f, 0.0f, 0.0f);
        putWords(layer->maskB, {0x0u, 0x0u, 0x0u, 0x40u});
        putWords(layer->tex0B, {0x9930a708u, 0x4004b085u, 0x0u, 0x0u});
    }

    // The rain's parameters: the GIF tag of the streaks (96 loops of RGBAQ, XYZF2 twice, blended lines), two colours,
    // 16 drop offsets and twelve streaks as pairs of end points; then the wet ground pass (mask 1).
    void LayerD_InitParams(GuestPtr<LayerD> layer)
    {
        putWords(layer->gifTag, {0x8060u, 0x4034c000u, 0x4141u, 0x0u});
        putFloats(layer->colourA, 765.0f, 765.0f, 765.0f, 40.0f);
        putFloats(layer->colourB, 765.0f, 765.0f, 765.0f, 0.0f);
        layer->unk_030[0] = 8;
        static constexpr float kDropY[16] = {0.1f, 35.4f, 9.6f, 15.8f, 1.3f, 24.6f, 13.3f, 8.7f,
                                             36.8f, 18.1f, 28.0f, 11.6f, 28.8f, 31.5f, 4.2f, 7.9f};
        for (int i = 0; i < 16; ++i)
            putFloats(layer->drops[i], 40.0f, kDropY[i], 0.0f, 0.0f);
        static constexpr float kStreaks[24][3] = {
            {-0.277647f, -36.85f, 0.186816f},     {-0.077647f, -38.449997f, 0.186816f},
            {0.278509f, -33.95f, 1.083412f},      {0.478509f, -35.55f, 1.083412f},
            {-0.702686f, -30.399998f, 0.219903f}, {std::bit_cast<float>(0xbf00b008u), std::bit_cast<float>(0xc1ffffffu), std::bit_cast<float>(0x3e612e41u)},
            {0.823469f, -27.699999f, -0.033502f}, {1.023469f, -29.3f, -0.033502f},
            {-0.437529f, -24.25f, 0.511663f},     {-0.237529f, -25.849998f, 0.511663f},
            {0.617897f, -20.5f, -0.409993f},      {0.817897f, -22.099998f, -0.409993f},
            {0.373105f, -16.600002f, 0.141843f},  {0.573105f, -18.2f, 0.141843f},
            {0.179452f, -11.45f, -0.877998f},     {0.379452f, -13.049998f, -0.877998f},
            {0.8f, -7.15f, 1.0f},                 {1.0f, -8.75f, 1.0f},
            {-0.85f, -14.650001f, 0.7f},          {-0.65f, -16.25f, 0.7f},
            {-0.55f, -4.3f, -0.6f},               {-0.35f, -5.9f, -0.6f},
            {-0.2f, -0.6f, -0.25f},               {0.0f, -2.2f, -0.25f},
        };
        for (int i = 0; i < 24; ++i)
            putFloats(layer->streaks[i], kStreaks[i][0], kStreaks[i][1], kStreaks[i][2], 1.0f);
        packet::putPassParams(layer->ground, {128.0f, 128.0f, 128.0f, 128.0f}, 0x9930a7b8u, 0x4004f685u, 0x7cu);
        putWords(layer->ground.mask, {0x0u, 0x0u, 0x0u, 0x1u});
        putFloats(layer->groundExtra, 0.0f, -0.01f, 0.0f, 1.0f);
    }

    PORT_FN(LayerB_InitParams);
    PORT_FN(LayerD_InitParams);

    // Layer C's three packets, one after the other: the highlight (ALPHA_1 set to 0x30_00000048, the chain at 0x1A7B30
    // and the highlight's seven quadwords to VU1 0x1C with the program started); the end (ALPHA_1 back to
    // 0x20_00000044); the marker (the colour table and what follows, 26 quadwords, to VU1 0x12, ALPHA_1 0x30_00000048
    // and the chain at 0x1A8330). Returns the end of the packets.
    GuestPtr<uint32_t> LayerC_BuildPacket(GuestPtr<LayerC> layer, GuestPtr<uint32_t> packet)
    {
        const uint32_t p = packet.address;
        layer->highlightPacket = p;
        layer->markerPacket = p + 0xc0u;
        layer->endPacket = p + 0x80u;
        putWords(p + 0x0u, {0x10000000u, 0x0u, 0x0u, 0x11000000u});
        putWords(p + 0x10u, {0x10000002u, 0x0u, 0x11000000u, 0x50000002u});
        putWords(p + 0x20u, {0x8001u, 0x10000000u, 0xeu, 0x0u});
        putWords(p + 0x30u, {0x48u, 0x30u, 0x42u, 0x0u});
        putWords(p + 0x40u, {0x50000000u, 0x1a7b30u, 0x0u, 0x0u});
        putWords(p + 0x50u, {0x30000007u, tagAddress(layer->colourA), 0x1000101u, 0x6c07001cu});
        putWords(p + 0x60u, {0x10000000u, 0x0u, 0x3000056u, 0x20001d5u});
        putWords(p + 0x70u, {0x60000000u, 0x0u, 0x14000000u, 0x11000000u});
        putWords(p + 0x80u, {0x10000002u, 0x0u, 0x11000000u, 0x50000002u});
        putWords(p + 0x90u, {0x8001u, 0x10000000u, 0xeu, 0x0u});
        putWords(p + 0xa0u, {0x44u, 0x20u, 0x42u, 0x0u});
        putWords(p + 0xb0u, {0x60000000u, 0x0u, 0x0u, 0x0u});
        putWords(p + 0xc0u, {0x3000001au, tagAddress(layer->colours), 0x1000101u, 0x6c1a0012u});
        putWords(p + 0xd0u, {0x10000000u, 0x0u, 0x0u, 0x11000000u});
        putWords(p + 0xe0u, {0x10000002u, 0x0u, 0x11000000u, 0x50000002u});
        putWords(p + 0xf0u, {0x8001u, 0x10000000u, 0xeu, 0x0u});
        putWords(p + 0x100u, {0x48u, 0x30u, 0x42u, 0x0u});
        putWords(p + 0x110u, {0x50000000u, 0x1a8330u, 0x0u, 0x0u});
        putWords(p + 0x120u, {0x60000000u, 0x0u, 0x3000042u, 0x20001dfu});
        return GuestPtr<uint32_t>{p + 0x130u};
    }

    PORT_FN(LayerC_BuildPacket);

    // Layer D (the rain): for each of the two terrain buffers, a packet that uploads the layer's streak parameters and
    // the first program (0x1A8A00); then the buffer's quads go to VU1 in 78 chunks of 0x200 bytes (9 groups of 8, each
    // chunk a REF tag, the UNPACK and a MSCNT, then 6 singles); then the ground pass: ALPHA_1, the second program
    // (0x1A8E20) with the ground parameters, and the packet ends. The layer keeps the start of each buffer's packet, its
    // ground pass and the shared end packet. Returns the end of the packets.
    GuestPtr<uint32_t> LayerD_BuildPacket(GuestPtr<LayerD> layer, GuestPtr<uint32_t> packet, GuestPtr<int32_t> buffers)
    {
        const uint32_t layerTag = tagAddress(*layer);
        const uint32_t paramsTag = tagAddress(layer->ground);
        const auto chunk = [](uint32_t at, uint32_t source) {
            putWords(at + 0x0u, {0x30000020u, source & 0x7FFFFFFFu, 0x0u, 0x6C208000u});
            putWords(at + 0x10u, {0x10000000u, 0x0u, 0x0u, 0x17000000u});
        };
        uint32_t at = packet.address;
        for (int buffer = 0; buffer < 2; ++buffer)
        {
            layer->startPackets[buffer] = at;
            putWords(at + 0x0u, {0x10000000u, 0x0u, 0x0u, 0x11000000u});
            putWords(at + 0x10u, {0x50000000u, 0x1A8A00u, 0x0u, 0x0u});
            putWords(at + 0x20u, {0x3000002Cu, layerTag, 0x1000101u, 0x6C2C0012u});
            putWords(at + 0x30u, {0x10000000u, 0x0u, 0x30000BEu, 0x20001A1u});
            putWords(at + 0x40u, {0x10000000u, 0x0u, 0x14000000u, 0x11000000u});
            uint32_t source = static_cast<uint32_t>(buffers[buffer]);
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
            layer->groundPackets[buffer] = at + 0x10u;
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
        layer->endPacket = at;
        putWords(at + 0x0u, {0x10000002u, 0x0u, 0x11000000u, 0x50000002u});
        putWords(at + 0x10u, {0x8001u, 0x10000000u, 0xEu, 0x0u});
        putWords(at + 0x20u, {0x44u, 0x20u, 0x42u, 0x0u});
        putWords(at + 0x30u, {0x60000000u, 0x0u, 0x0u, 0x0u});
        return GuestPtr<uint32_t>{at + 0x40u};
    }

    PORT_FN(LayerD_BuildPacket);

    // Layer E (the snow): like layer D, for each of the two terrain buffers a packet that sets the GS state of the flakes
    // (RGBAQ, ALPHA_1 and TEX0_1, the TEX0 of the flake built field by field: base 0x3F70, width 1, format 0x14, 2^5 by
    // 2^5, colour with alpha, its palette at 0x3F72), uploads the layer's flake parameters and the program 0x1A9A90, then
    // the buffer's quads in chunks and the ground pass (the program 0x1A9F10 with the ground parameters). The layer keeps
    // each buffer's start and ground pass and the shared end packet. Returns the end of the packets.
    GuestPtr<uint32_t> LayerE_BuildPacket(GuestPtr<LayerE> layer, GuestPtr<uint32_t> packet, GuestPtr<int32_t> buffers)
    {
        const uint32_t layerTag = tagAddress(*layer);
        const uint32_t paramsTag = tagAddress(layer->ground);
        const auto chunk = [](uint32_t at, uint32_t source) {
            putWords(at + 0x0u, {0x30000020u, source & 0x7FFFFFFFu, 0x0u, 0x6C208000u});
            putWords(at + 0x10u, {0x10000000u, 0x0u, 0x0u, 0x17000000u});
        };
        uint32_t at = packet.address;
        for (int buffer = 0; buffer < 2; ++buffer)
        {
            layer->startPackets[buffer] = at;
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
            uint32_t source = static_cast<uint32_t>(buffers[buffer]);
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
            layer->groundPackets[buffer] = at + 0x10u;
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
        layer->endPacket = at;
        putWords(at + 0x0u, {0x10000002u, 0x0u, 0x11000000u, 0x50000002u});
        putWords(at + 0x10u, {0x8001u, 0x10000000u, 0xEu, 0x0u});
        putWords(at + 0x20u, {0x44u, 0x20u, 0x42u, 0x0u});
        putWords(at + 0x30u, {0x60000000u, 0x0u, 0x0u, 0x0u});
        return GuestPtr<uint32_t>{at + 0x40u};
    }

    PORT_FN(LayerE_BuildPacket);

    // Layer G (the water): two sections, one per terrain buffer. Each starts with the GS state for both contexts (ALPHA_1
    // and ALPHA_2, TEX0_1 and TEX0_2), the program 0x1A6440 and the layer's parameters, then the buffer's quads in 39
    // chunks of 0x400 bytes (4 groups of 8, then 7 singles) and a RET. Returns the end.
    GuestPtr<uint32_t> LayerG_BuildPacket(GuestPtr<LayerG> layer, GuestPtr<uint32_t> packet, GuestPtr<int32_t> buffers)
    {
        const uint32_t layerTag = tagAddress(*layer);
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
        layer->packets[0] = at;
        header(at);
        at += 0xA0u;
        for (int section = 0; section < 2; ++section)
        {
            uint32_t source = static_cast<uint32_t>(buffers[section]);
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
            layer->packets[1] = at + 0x10u;
            header(at + 0x10u);
            at += 0xB0u;
        }
        return GuestPtr<uint32_t>{at};
    }

    PORT_FN(LayerG_BuildPacket);

    // ---- the terrain's VU1 packets (batch 41): the same shape as the layers' packets, two sections of vertex chunks

    // Terrain packet A, the cliff sides (program 0x1A76F0): a header that uploads the side parameters, then the first side
    // buffer in 20 chunks of 0x6C0 bytes (2 groups of 8, 4 singles) and a RET, the header again and the second buffer the
    // same. The map keeps the start in both side packet slots (the second section's start is never kept). Returns the end.
    GuestPtr<uint32_t> Terrain_BuildVu1PacketA(GuestPtr<CityMap> map, GuestPtr<uint32_t> packet)
    {
        const uint32_t tag = tagAddress(map->sideColour);
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
        map->sidePackets[0] = at;
        header(at);
        at += 0x60u;
        for (int section = 0; section < 2; ++section)
        {
            uint32_t source = map->sideBuffers[section].address;
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
            map->sidePackets[1] = packet.address; // the game's: the first section's start again
            header(at + 0x10u);
            at += 0x70u;
        }
        return GuestPtr<uint32_t>{at};
    }

    // Terrain packet B: the base ground pass (program 0x1A5EB0 and the ground parameters, ending in a RET), then for each
    // terrain buffer the chunk packet that every pass CALLs: its quads in 24 chunks of 0x680 bytes (3 groups of 8, each
    // chunk a REF, the UNPACK and an MSCNT) and a RET. Returns the end.
    GuestPtr<uint32_t> Terrain_BuildVu1PacketB(GuestPtr<CityMap> map, GuestPtr<uint32_t> packet)
    {
        const auto chunk = [](uint32_t at, uint32_t source) {
            putWords(at + 0x0u, {0x30000068u, source & 0x7FFFFFFFu, 0x0u, 0x6C688000u});
            putWords(at + 0x10u, {0x10000000u, 0x0u, 0x0u, 0x17000000u});
        };
        uint32_t at = packet.address;
        map->groundPacket = at;
        putWords(at + 0x0u, {0x10000000u, 0x0u, 0x0u, 0x11000000u});
        putWords(at + 0x10u, {0x50000000u, 0x1A5EB0u, 0x0u, 0x0u});
        putWords(at + 0x20u, {0x30000012u, tagAddress(map->groundParams), 0x1000101u, 0x6C12001Cu});
        putWords(at + 0x30u, {0x10000000u, 0x0u, 0x3000056u, 0x20001D5u});
        putWords(at + 0x40u, {0x10000000u, 0x0u, 0x14000000u, 0x11000000u});
        putWords(at + 0x50u, {0x60000000u, 0x0u, 0x0u, 0x0u});
        at += 0x60u;
        map->chunkPackets[0] = at;
        for (int section = 0; section < 2; ++section)
        {
            uint32_t source = map->terrainBuffers[section].address;
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
            map->chunkPackets[1] = at;
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
        if (layer->rectActive == 1 && pass == 0)
        {
            Dma_AppendCall(static_cast<int>(layer->highlightPacket));
            Dma_AppendCall(packet);
            Dma_AppendCall(static_cast<int>(layer->endPacket));
        }
        else if (layer->rectActive == 2 && pass == 1)
        {
            ps2f height = Map_GetHeightAt(map, layer->rectX0, layer->rectZ0) - ps2f(0.8f);
            GuestPtr<MapObject> obj = Map_GetTileObject(map, layer->rectX0, layer->rectZ0);
            const GuestPtr<BuildingType> type{globals::buildingTypeAddress(obj->type)};
            const int node = type->size == 3 ? 1 : 0;
            for (; obj->nextOnTile != kNoObject; obj = Map_GetRecord(map, obj->nextOnTile))
                height = height - ps2f(0.8f);
            const GuestPtr<A3DObject> model = layer->marker;
            const GuestPtr<A3DNode> markerNode = model->model->nodes + node;
            Mat4_Copy(worldMatrix(markerNode), GuestPtr<Vec4>{globals::kIdentityMatrix});
            Vec4 &position = markerNode->slot.matrix[3];
            position.x = fromInt(layer->rectX0) - ps2f(64.0f) + fromInt(type->size) / ps2f(2.0f);
            position.y = height;
            position.z = fromInt(0x80 - layer->rectZ0) - ps2f(64.0f) - fromInt(type->size) / ps2f(2.0f);
            Dma_AppendCall(static_cast<int>(layer->markerPacket));
            A3DObject_DrawNode(model, node);
            Dma_AppendCall(static_cast<int>(layer->endPacket));
        }
    }

    PORT_FN(LayerC_DrawBuildings);

    // ---- the rain by the clock (batch 39)

    // The rain for the time of day (clock in seconds; the seconds up to 5:00, 18000, and after 18:00 the distance from
    // midnight, 0x15180 less the clock; between them the clock held at 18000): the streak colour's three lanes set to
    // 765 and its w to clock / 450, the ground pass's ALPHA_1 FIX to clock / 600; the sixteen drops fall 0.4 a frame
    // within 40; the ground pass's TEX0 steps through six texture pages, one every two frames (the splashes); then the
    // pass's ground packet, the frame's chunk packet, the end packet and the pass's streak packet are queued.
    void LayerD_DrawSkyByClock(GuestPtr<LayerD> layer, int pass, int packet, int clock)
    {
        if (clock < 0x10B30)
        {
            if (clock > 18000)
                clock = 18000;
        }
        else
            clock = std::abs(clock - 0x15180);
        layer->colourA.w = fromInt(clock) / ps2f(450.0f);
        port::store<uint32_t>(addressOf(layer->colourA.z), 0x443F4000u); // 765.0
        port::store<uint32_t>(addressOf(layer->colourA.y), 0x443F4000u);
        port::store<uint32_t>(addressOf(layer->colourA.x), 0x443F4000u);
        port::store<int32_t>(layer->groundPackets[pass] + 0x24u, clock / 600); // ALPHA_1's FIX in the packet
        for (Vec4 &drop : layer->drops)
        {
            ps2f y = drop.y + ps2f(0.4f);
            drop.y = y;
            if (ps2f(40.0f) <= y)
                drop.y = y - ps2f(40.0f);
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
        uint32_t (&tex0)[4] = layer->ground.tex0;
        tex0[3] = 0;
        tex0[2] = 0;
        tex0[1] = 0x4004F685u;
        tex0[0] = static_cast<uint32_t>((cycle >> 2) + 0x27B8) | 0x99308000u; // TBP0 0x27B8 + 0x10 per step
        Dma_AppendCall(static_cast<int>(layer->groundPackets[pass]));
        Dma_AppendCall(packet);
        Dma_AppendCall(static_cast<int>(layer->endPacket));
        Dma_AppendCall(static_cast<int>(layer->startPackets[pass]));
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
                model->model = GuestPtr<A3DModel>{0u};
            }
            if (model->animations)
            {
                sdk::operator_delete(model->animations.address);
                model->animations = GuestPtr<uint32_t>{0u};
            }
            BaseObject_dtor(static_cast<int>(model.address), 0);
            if (0 < deleteFlag)
                sdk::operator_delete(model.address);
        }
        return model;
    }

    // Sets the colour (four words) of node `index`'s draw packet when the node has a mesh; an index past the model's node
    // count is reported with printf. The result is printf's, or all of what v0 held when the call was made.
    int64_t A3DObject_ChangeLuminosity(GuestPtr<A3DObject> model, GuestPtr<uint32_t> colour, int index)
    {
        const int64_t entryV0 = port::guestV0Wide();
        const GuestPtr<A3DModel> data = model->model;
        const GuestPtr<A3DNode> nodes = data->nodes;
        if (!(index < static_cast<int>(data->nodeCount)))
            return port::callGuest<int64_t>(sdk::addr::printf, 0x001B5A90u);
        const GuestPtr<A3DNode> node = nodes + index;
        if (node->mesh)
            node->slot.colour = *GuestPtr<Vec4>{colour.address};
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
        for (int i = 0; i < 0x708; ++i)
            for (const uint32_t base : {first, second})
            {
                const GuestPtr<InstanceSlot> slot = GuestPtr<InstanceSlot>{base} + i;
                putWords(slot->header, {0x10000005u, 0u, 0x1000101u, 0x6C05002Bu}); // CNT 5; STCYCL, UNPACK to 0x2B
                putWords(slot->trailer, {0x60000000u, 0u, 0x14000000u, 0x11000000u}); // RET; MSCAL 0, FLUSH
            }
    }

    // Sends a DMA chain to VIF1 at once: a two-tag chain at 0x1F8100 (a call to the list at `address`, then the end tag), the
    // GS path waited for, the channel started (CHCR 0x145) and the GS path waited for again; returns that last wait.
    int Dma_KickVif1Now(uint32_t address)
    {
        constexpr uint32_t kChain = 0x001F8100u;
        constexpr uint32_t kQwc = 0x10009020u, kTadr = 0x10009030u, kChcr = 0x10009000u, kStat = 0x1000E010u;
        packet::putCallThenEnd(kChain, address & 0x7FFFFFFFu);
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

    // The map with people below it, then the buffers and the draw layers the renderer owns: the two cliff-side buffers
    // (0x8700 bytes each) and the two terrain buffers (0x9C00); the ground and side parameters; the two terrain packets
    // built at the packet buffer's position (which moves on past each); and the layers D, E, F, G, C, B and A, the ones
    // that draw over the terrain quads getting the terrain buffers.
    GuestPtr<CityMap> CityRender_ctor(GuestPtr<CityMap> map)
    {
        MapPeople_ctor(map);
        map->vtable = GuestPtr<void>{0x001BCF00u};
        map->sideBuffers[0] = GuestPtr<void>{sdk::malloc(0x8700u)};
        map->sideBuffers[1] = GuestPtr<void>{sdk::malloc(0x8700u)};
        map->terrainBuffers[0] = GuestPtr<void>{sdk::malloc(0x9C00u)};
        map->terrainBuffers[1] = GuestPtr<void>{sdk::malloc(0x9C00u)};
        CityRender_InitParams(map);
        Terrain_InitParams(map);
        PacketBuf_Seek(static_cast<int>(Terrain_BuildVu1PacketB(map, GuestPtr<uint32_t>{static_cast<uint32_t>(PacketBuf_Tell())}).address));
        PacketBuf_Seek(static_cast<int>(Terrain_BuildVu1PacketA(map, GuestPtr<uint32_t>{static_cast<uint32_t>(PacketBuf_Tell())}).address));
        const int buffers = static_cast<int>(addressOf(map->terrainBuffers));
        const auto make = [&](uint32_t size, auto &field, auto construct) {
            const uint32_t memory = sdk::operator_new(size);
            port::store<uint32_t>(addressOf(field), memory != 0u ? construct(memory) : 0u);
        };
        make(0x410, map->rain, [&](uint32_t o) { return LayerD_ctor(GuestPtr<LayerD>{o}, buffers).address; });
        make(0x6E0, map->snow, [&](uint32_t o) { return LayerE_ctor(GuestPtr<LayerE>{o}, buffers).address; });
        make(0x140, map->layerF, [&](uint32_t o) { return LayerF_ctor(GuestPtr<LayerF>{o}).address; });
        make(0x2E0, map->water, [&](uint32_t o) { return LayerG_ctor(GuestPtr<LayerG>{o}, buffers).address; });
        make(0x240, map->placement, [&](uint32_t o) { return LayerC_ctor(GuestPtr<LayerC>{o}).address; });
        make(0x230, map->layerB, [&](uint32_t o) { return LayerB_ctor(GuestPtr<LayerB>{o}).address; });
        make(0x2440, map->instances, [&](uint32_t o) { return LayerA_ctor(GuestPtr<LayerA>{o}).address; });
        return map;
    }

    // The layers are deleted (A, D, E, F, G, C, B), then the four buffers, then the map below the renderer.
    GuestPtr<CityMap> CityRender_dtor(GuestPtr<CityMap> map, int16_t deleteFlag)
    {
        if (map)
        {
            map->vtable = GuestPtr<void>{0x001BCF00u};
            lifetime::deleteWith(addressOf(map->instances), [](uint32_t o) { LayerA_DeletingDtor(GuestPtr<LayerA>{o}, 1); });
            lifetime::deleteWith(addressOf(map->rain), [](uint32_t o) { LayerD_DeletingDtor(GuestPtr<LayerD>{o}, 1); });
            lifetime::deleteWith(addressOf(map->snow), [](uint32_t o) { LayerE_DeletingDtor(GuestPtr<LayerE>{o}, 1); });
            lifetime::deleteWith(addressOf(map->layerF), [](uint32_t o) { LayerF_DeletingDtor(GuestPtr<LayerF>{o}, 1); });
            lifetime::deleteWith(addressOf(map->water), [](uint32_t o) { LayerG_DeletingDtor(GuestPtr<LayerG>{o}, 1); });
            lifetime::deleteWith(addressOf(map->placement), [](uint32_t o) { LayerC_DeletingDtor(GuestPtr<LayerC>{o}, 1); });
            lifetime::deleteWith(addressOf(map->layerB), [](uint32_t o) { LayerB_DeletingDtor(GuestPtr<LayerB>{o}, 1); });
            for (const uint32_t field : {addressOf(map->terrainBuffers[0]), addressOf(map->terrainBuffers[1]), addressOf(map->sideBuffers[0]),
                                         addressOf(map->sideBuffers[1])})
                lifetime::deleteWith(field, [](uint32_t o) { sdk::free(o); });
            MapPeople_dtor(map, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(map.address);
        }
        return map;
    }

    // The renderer constructor and deleting destructor entered through the map's class (table at 0x1BCED0).
    GuestPtr<CityMap> CityMap_ctor(GuestPtr<CityMap> map)
    {
        CityRender_ctor(map);
        map->vtable = GuestPtr<void>{0x001BCED0u};
        return map;
    }

    GuestPtr<CityMap> CityMap_DeletingDtor(GuestPtr<CityMap> map, int16_t deleteFlag)
    {
        if (map)
        {
            map->vtable = GuestPtr<void>{0x001BCED0u};
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
        putWords(addressOf(camera->base.eye), {0u, 0xC1E80000u, 0x3FCCCCCDu, 0x3F800000u}); // (0, -29, 1.6, 1)
        camera->zoomLevel = 1;
        return camera;
    }

    PORT_FN(Sky_Load);
    PORT_FN(CityCamera_BaseCtor);
    PORT_FN(CityCamera_ctor);

    // The asynchronous model load; when it finished (1) for a model whose nodes keep mesh indexes (a2 is 0), the model's
    // group size is node 0's mesh index divided by 13.
    int InstanceModel_LoadAsync(GuestPtr<A3DObject> model, GuestPtr<GameFile> gamefile, int a2)
    {
        const int result = A3DObject_LoadAsync(model, gamefile, a2);
        if (result == 1 && a2 == 0)
            GuestPtr<InstanceModel>{model.address}->groupSize = divide(meshIndex(model->model->nodes), 13);
        return result;
    }

    PORT_FN(InstanceModel_LoadAsync);

    namespace
    {
        // The tags of the DMA chain at `chain` that point at data (REF and CALL) hold a word offset from `base` in their address
        // word; this turns it into an address. `reader` is the chain reader's working memory.
        void relocateChain(GuestPtr<DmaChainReader> reader, uint32_t chain, uint32_t base)
        {
            DmaChain_Begin(reader, static_cast<int>(chain));
            int more;
            do
            {
                more = DmaChain_Next(reader);
                const uint32_t id = reader->id;
                if (id == packet::kRef || id == packet::kCall)
                {
                    const GuestPtr<uint32_t> tag{reader->tag};
                    tag[1] = base + 4u * tag[1];
                }
            } while (more != 0);
        }
    }

    // Turns the word offsets of a loaded .A3D image into addresses: the four tables of the header, each node's parent, the meshes
    // (when `indexed`, each node's mesh index becomes the mesh's address, null for -1, and the table keeps its offsets; when not,
    // the table's entries become addresses and the nodes keep their indexes), each mesh block's data word and the DMA tags of its
    // chain that point at data, the animations (the model keeps their addresses and the track count; each track's node and keys)
    // and the extra chain blocks. The texture packets (the model's texture block, `textureWords` words) are relocated against
    // that block and sent to the GS as they come.
    void A3D_Relocate(GuestPtr<A3DObject> model, int indexed)
    {
        port::GuestFrame<0xE0> frame;
        const uint32_t sp = frame.ptr().address;
        // The model's data and a word offset in it, both read again for every use, as in the original.
        const auto data = [&]() { return model->model; };
        const auto address = [&](uint32_t words) { return data().address + 4u * words; };
        data()->nodes = GuestPtr<A3DNode>{address(data()->nodes.address)};
        data()->meshes = GuestPtr<uint32_t>{address(data()->meshes.address)};
        data()->animations = GuestPtr<void>{address(data()->animations.address)};
        data()->chains = GuestPtr<void>{address(data()->chains.address)};
        GuestPtr<A3DNode> node = data()->nodes;
        for (int i = 0; i < data()->nodeCount; ++i, ++node)
            if (node->parent)
                node->parent = GuestPtr<A3DNode>{address(node->parent.address)};
        uint32_t block; // the first mesh block
        if (indexed != 0)
        {
            node = data()->nodes;
            for (int i = 0; i < data()->nodeCount; ++i, ++node)
            {
                const int32_t index = meshIndex(node);
                if (index == -1)
                    node->mesh = GuestPtr<A3DMesh>{0u};
                else
                    node->mesh = GuestPtr<A3DMesh>{address(data()->meshes[index])};
            }
            block = address(data()->meshes[0]);
        }
        else
        {
            for (int i = 0; i < data()->meshCount; ++i)
                data()->meshes[i] = address(data()->meshes[i]);
            block = data()->meshes[0];
        }
        // The mesh blocks lie one after the other.
        for (int i = 0; i < data()->meshCount; ++i)
        {
            const GuestPtr<A3DMesh> mesh{block};
            mesh->data = address(mesh->data);
            relocateChain(GuestPtr<DmaChainReader>{sp + 0x50u}, addressOf(mesh->chain), data().address);
            block += 4u * mesh->sizeWords;
        }
        if (data()->animationCount == 0)
            model->trackCount = 0;
        else
        {
            // A 0x10-byte header with the track count, then each animation's tracks, then their keys (0x20 bytes each).
            uint32_t cursor = data()->animations.address;
            model->trackCount = port::load<int32_t>(cursor);
            cursor += 0x10u;
            data()->animations = GuestPtr<void>{cursor};
            for (int animation = 0; animation < data()->animationCount; ++animation)
            {
                model->animations[animation] = cursor;
                uint32_t keys = 0;
                for (int t = 0; t < model->trackCount; ++t)
                {
                    const GuestPtr<A3DTrack> track{cursor};
                    const uint32_t count = static_cast<uint32_t>(track->keyCount);
                    track->node = GuestPtr<A3DNode>{address(track->node.address)};
                    keys += count;
                    track->keys = GuestPtr<A3DKey>{address(track->keys.address)};
                    cursor += 0x10u;
                }
                cursor += keys << 5;
            }
        }
        block = data()->chains.address;
        for (int i = 0; i < data()->chainCount; ++i)
        {
            const GuestPtr<A3DChainBlock> chain{block};
            relocateChain(GuestPtr<DmaChainReader>{sp + 0x80u}, addressOf(chain->chain), data().address);
            block += 4u * chain->sizeWords;
        }
        uint32_t remaining = data()->textureWords;
        if (remaining != 0)
        {
            uint32_t at = model->textures.address;
            do
            {
                const GuestPtr<A3DChainBlock> textures{at};
                relocateChain(GuestPtr<DmaChainReader>{sp + 0xB0u}, addressOf(textures->chain), model->textures.address);
                Dma_KickVif1Now(addressOf(textures->chain));
                const uint32_t size = textures->sizeWords;
                remaining -= size;
                at += 4u * size;
            } while (remaining != 0);
        }
    }

    PORT_FN(A3D_Relocate);

    // Loads a model (.A3D) from an open file, one step per call (the step is in 0x1BDA10, the request being waited for in 0x1BDA14,
    // and the file's 16-byte header lands in 0x1C5100): 0 queue the header read, 1 wait for it (an empty header, a size of 0, is the
    // end of the file: -1), 2 make the data block of the size and queue the rest of the file after a copy of the header, 3 wait, 4
    // make the table of the model's animations and the texture block and queue its read, 5 wait, 6 relocate the data
    // (mm_A3D_Relocate, which also sends the textures), free the texture block, reset the model's matrix and finish (1). 0 means
    // still busy.
    int A3DObject_LoadAsync(GuestPtr<A3DObject> model, GuestPtr<GameFile> file, int indexed)
    {
        const GuestPtr<A3DModel> header{0x001C5100u};
        const GuestVar<int32_t> state{0x001BDA10u}, request{0x001BDA14u};
        const auto done = [&]() { return FileQueue_IsDone(static_cast<int>(file.address), request.get()) != 0; };
        switch (static_cast<uint32_t>(state.get()))
        {
        case 0:
            model->model = GuestPtr<A3DModel>{0u};
            model->textures = GuestPtr<void>{0u};
            model->animations = GuestPtr<uint32_t>{0u};
            request.set(File_Read(file, static_cast<int>(header.address), 1, 0x10, 0));
            state.set(state.get() + 1);
            return 0;
        case 1:
            if (!done())
                return 0;
            if (header->sizeWords == 0)
            {
                state.set(0);
                return -1;
            }
            state.set(state.get() + 1);
            return 0;
        case 2:
            model->model = GuestPtr<A3DModel>{sdk::malloc(header->sizeWords << 2)};
            sdk::memcpy(model->model.address, header.address, 0x10);
            request.set(File_Read(file, static_cast<int>(model->model.address) + 0x10, static_cast<int32_t>(header->sizeWords) - 4, 4, 0));
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
            const int32_t animations = model->model->animationCount;
            if (animations != 0)
                model->animations = GuestPtr<uint32_t>{sdk::mw_NewArray(static_cast<uint32_t>(animations) << 2)};
            model->textures = GuestPtr<void>{sdk::malloc(header->textureWords << 2)};
            request.set(File_Read(file, static_cast<int>(model->textures.address), static_cast<int32_t>(header->textureWords), 4, 0));
            state.set(state.get() + 1);
            return 0;
        }
        case 6:
        {
            A3D_Relocate(model, indexed);
            const uint32_t textures = model->textures.address;
            if (textures != 0u)
            {
                sdk::free(textures);
                model->textures = GuestPtr<void>{0u};
            }
            Mat4_Copy(GuestPtr<Vec4>::of(model->matrix[0]), GuestPtr<Vec4>{0x001B2550u});
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

    // Loads a model (.A3D) from a disc path in one go (the blocking counterpart of A3DObject_LoadAsync): the file's 16-byte header
    // is read and the file read again from its start into a block of its size; the table of the model's animations and the
    // texture block are made and the texture block read; the data is relocated (mm_A3D_Relocate), the texture block freed and the
    // model's matrix reset. The result is the model.
    GuestPtr<int32_t> A3DObject_Load(GuestPtr<A3DObject> model, int path, int indexed)
    {
        port::GuestFrame<0x60> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<GameFile> file{sp + 0x58u};
        const GuestPtr<A3DModel> header{sp + 0x40u};
        BaseObject_ctor(static_cast<int>(model.address));
        model->model = GuestPtr<A3DModel>{0u};
        model->textures = GuestPtr<void>{0u};
        model->animations = GuestPtr<uint32_t>{0u};
        File_ctor(file, 0);
        File_Open(file, GuestPtr<char>{static_cast<uint32_t>(path)});
        File_Read(file, static_cast<int>(header.address), 1, 0x10, 0);
        File_Seek(file, 0, 0, 0);
        model->model = GuestPtr<A3DModel>{sdk::malloc(header->sizeWords << 2)};
        File_Read(file, static_cast<int>(model->model.address), static_cast<int32_t>(header->sizeWords), 4, 0);
        const int32_t animations = model->model->animationCount;
        if (animations != 0)
            model->animations = GuestPtr<uint32_t>{sdk::mw_NewArray(static_cast<uint32_t>(animations) << 2)};
        model->textures = GuestPtr<void>{sdk::malloc(header->textureWords << 2)};
        File_Read(file, static_cast<int>(model->textures.address), static_cast<int32_t>(header->textureWords), 4, 0);
        File_Close(file);
        A3D_Relocate(model, indexed);
        if (model->textures)
        {
            sdk::free(model->textures.address);
            model->textures = GuestPtr<void>{0u};
        }
        Mat4_Copy(GuestPtr<Vec4>::of(model->matrix[0]), GuestPtr<Vec4>{0x001B2550u});
        File_dtor(file, -1);
        return GuestPtr<int32_t>{model.address};
    }

    PORT_FN(A3DObject_Load);
}
