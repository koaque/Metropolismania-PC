// Helpers shared by the render code (gfx.cpp, layers.cpp): writers for the DMA packets and VU1 parameter blocks it builds
// in guest memory (docs/RENDER_PIPELINE.md describes what they hold), and layer A's instance lookup.

#pragma once

#include "game_types.h"

#include <cstddef>
#include <initializer_list>

namespace mm::packet
{
    // DMA tag IDs, the bits 28-30 of a tag's first word.
    inline constexpr uint32_t kCnt = 0x10000000u;
    inline constexpr uint32_t kRef = 0x30000000u;
    inline constexpr uint32_t kCall = 0x50000000u;
    inline constexpr uint32_t kRet = 0x60000000u;
    inline constexpr uint32_t kEnd = 0x70000000u;

    // The guest address of an object in guest memory (a struct field the packet points the DMA at).
    template <typename T>
    uint32_t addressOf(const T &object)
    {
        return port::guest(&object);
    }

    // A DMA tag's address word: the guest address without the KSEG bit.
    template <typename T>
    uint32_t tagAddress(const T &object)
    {
        return addressOf(object) & 0x7FFFFFFFu;
    }

    // Words or floats one after the other from `at`.
    inline void putWords(uint32_t at, std::initializer_list<uint32_t> words)
    {
        for (const uint32_t word : words)
        {
            port::store<uint32_t>(at, word);
            at += 4u;
        }
    }

    inline void putFloats(uint32_t at, std::initializer_list<float> values)
    {
        for (const float value : values)
        {
            port::store<float>(at, value);
            at += 4u;
        }
    }

    template <std::size_t N>
    void putWords(uint32_t (&field)[N], std::initializer_list<uint32_t> words)
    {
        putWords(addressOf(field), words);
    }

    inline void putFloats(Vec4 &field, float x, float y, float z, float w) { putFloats(addressOf(field), {x, y, z, w}); }

    // A CALL tag to `target` and, in the next quadword, an END tag (which the next call written over it replaces).
    inline void putCallThenEnd(uint32_t at, uint32_t target) { putWords(at, {kCall, target, 0u, 0u, kEnd, 0u, 0u, 0u}); }

    // The texture coordinates (s, t, q 1, 0) of a quad drawn as two strips: the left half (s 0 to 0.5) top to bottom, then
    // the right half. Every terrain pass but the cliff sides uses them.
    inline void putHalvesStq(Vec4 (&stq)[12])
    {
        static constexpr float kST[12][2] = {{0, 0},    {0.5f, 0}, {0, 0.5f},    {0.5f, 0.5f}, {0, 1},    {0.5f, 1},
                                             {0.5f, 0}, {1, 0},    {0.5f, 0.5f}, {1, 0.5f},    {0.5f, 1}, {1, 1}};
        for (int i = 0; i < 12; ++i)
            putFloats(stq[i], kST[i][0], kST[i][1], 1.0f, 0.0f);
    }

    // A terrain pass's parameters as the layers set them up: the colour scale, TEX0 (two words), the output offset 0x68,
    // the GIF tag (26 loops of TEX0_1, PRIM, then ST, RGBAQ, XYZF2 four times), PRIM and the two-strip texture
    // coordinates. The mask is the caller's.
    inline void putPassParams(TerrainPassParams &params, std::initializer_list<float> colour, uint32_t tex0Low, uint32_t tex0High,
                              uint32_t prim)
    {
        putFloats(addressOf(params.colour), colour);
        putWords(params.tex0, {tex0Low, tex0High, 0u, 0u});
        putWords(params.outputOffset, {0x68u, 0u, 0u, 0u});
        putWords(params.gifTag, {0x801Au, 0xE0000000u, 0x41241206u, 0x00412412u});
        putWords(params.prim, {prim, 0u, 0u, 0u});
        putHalvesStq(params.stq);
    }
}

namespace mm
{
    // Node `index` of a model, and a node's world matrix (in its draw packet; the last word, +0xAC, is the "up to date" mark).
    inline GuestPtr<A3DNode> modelNode(GuestPtr<A3DObject> model, int index) { return model->model->nodes + index; }
    inline GuestPtr<Vec4> worldMatrix(GuestPtr<A3DNode> node) { return GuestPtr<Vec4>::of(node->slot.matrix[0]); }

    // Instance `index` of the buffer layer A fills this frame. The buffer is read on every call, and the arithmetic is on
    // the 32-bit guest address, as in the original.
    inline GuestPtr<LayerAInstance> layerAInstance(GuestPtr<LayerA> layer, int index)
    {
        const uint32_t slot = 32u * static_cast<uint32_t>(layer->buffer) + static_cast<uint32_t>(index);
        return GuestPtr<LayerAInstance>{packet::addressOf(layer->instances[0]) + static_cast<uint32_t>(sizeof(LayerAInstance)) * slot};
    }
}
