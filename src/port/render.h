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

    // The GS TEX0 register from its fields, each cut to its width. The game sets them one by one through a bitfield
    // struct; together they cover all 64 bits, so the packed value is what those stores leave.
    struct Tex0
    {
        uint32_t tbp0, tbw, psm, tw, th, tcc, tfx, cbp, cpsm, csm, csa, cld;
    };

    constexpr uint64_t packTex0(const Tex0 &t)
    {
        return (uint64_t{t.tbp0 & 0x3FFFu}) | (uint64_t{t.tbw & 0x3Fu} << 14) | (uint64_t{t.psm & 0x3Fu} << 20) |
               (uint64_t{t.tw & 0xFu} << 26) | (uint64_t{t.th & 0xFu} << 30) | (uint64_t{t.tcc & 1u} << 34) |
               (uint64_t{t.tfx & 3u} << 35) | (uint64_t{t.cbp & 0x3FFFu} << 37) | (uint64_t{t.cpsm & 0xFu} << 51) |
               (uint64_t{t.csm & 1u} << 55) | (uint64_t{t.csa & 0x1Fu} << 56) | (uint64_t{t.cld & 7u} << 61);
    }

    // The pixel formats the game's textures use (TEX0 PSM).
    inline constexpr uint32_t kPsmCt32 = 0x00u, kPsmT8 = 0x13u, kPsmT4 = 0x14u;

    // GS register addresses, as A+D writes name them.
    inline constexpr uint32_t kGsPrim = 0x00u, kGsRgbaq = 0x01u, kGsSt = 0x02u, kGsXyzf2 = 0x04u, kGsXyz2 = 0x05u,
                              kGsTex0_1 = 0x06u, kGsTest1 = 0x47u;

    // One A+D write: the register's value (low word, high word), then its address.
    inline void putAd(GsAdWrite &write, uint32_t low, uint32_t high, uint32_t reg)
    {
        putWords(write.value, {low, high});
        putWords(write.reg, {reg, 0u});
    }

    inline void putAd(GsAdWrite &write, uint64_t value, uint32_t reg)
    {
        putAd(write, static_cast<uint32_t>(value), static_cast<uint32_t>(value >> 32), reg);
    }

    // An ST write: the texture coordinates, then the register.
    inline void putSt(GsStWrite &write, float s, float t)
    {
        write.s = s;
        write.t = t;
        putWords(write.reg, {kGsSt, 0u});
    }

    // A vertex write (XYZ2 or XYZF2): X and Y, Z, then the register.
    inline void putXyz(GsXyzWrite &write, uint32_t xy, uint32_t z, uint32_t reg)
    {
        write.xy = xy;
        write.z = z;
        putWords(write.reg, {reg, 0u});
    }

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
    inline GuestPtr<ClaimMetInstance> claimMetInstance(GuestPtr<ClaimMetLayer> layer, int index)
    {
        const uint32_t slot = 32u * static_cast<uint32_t>(layer->buffer) + static_cast<uint32_t>(index);
        return GuestPtr<ClaimMetInstance>{packet::addressOf(layer->instances[0]) + static_cast<uint32_t>(sizeof(ClaimMetInstance)) * slot};
    }
}
