// The platform layer, libpkt, the GIF packet builder, and the half pixel offset of libgraph (plan P7), written from what the
// functions do and proven against the recompiled functions by the difftest. Prototypes are in config/platform_signatures.txt.
//
// A packet writes 32 bit words at its cursor. A DMA tag (cnt, ref or end) is four words: the first holds the quadword count of what
// follows it and the tag id, the second an address, the other two are VIF codes. Moving on (Terminate) pads to a quadword and adds
// the count of the quadwords written after the open tag to its first word.

#include "game_functions.h"
#include "sdk.h"

namespace mm
{
    namespace
    {
        constexpr uint32_t kTagCnt = 0x10000000u;  // DMA tag ids in the top bits of the first word
        constexpr uint32_t kTagRef = 0x30000000u;
        constexpr uint32_t kTagEnd = 0x70000000u;
        constexpr uint32_t kPhysicalMask = 0x9FFFFFFFu; // a ref tag's address loses the uncached bit

        // Writes a DMA tag (four words) at the cursor, after closing the open one, and makes it the open tag.
        void putTag(GuestPtr<GifPacket> packet, uint32_t first, uint32_t address, uint32_t third, uint32_t fourth, bool open)
        {
            sdk_sceGifPkTerminate(packet);
            GuestPtr<uint32_t> cursor = packet->cursor;
            if (open)
                packet->dmaTag = cursor;
            cursor[0] = first;
            cursor[1] = address;
            cursor[2] = third;
            cursor[3] = fourth;
            packet->cursor = cursor + 4;
        }
    }

    // Starts a packet in the buffer at `base`.
    void sdk_sceGifPkInit(GuestPtr<GifPacket> packet, GuestPtr<uint32_t> base)
    {
        packet->cursor = base;
        packet->dmaTag = GuestPtr<uint32_t>{};
        packet->base = base;
    }

    // Starts again from the beginning of the buffer.
    void sdk_sceGifPkReset(GuestPtr<GifPacket> packet)
    {
        packet->cursor = packet->base;
        packet->dmaTag = GuestPtr<uint32_t>{};
    }

    // Pads the packet to a quadword with zero words and closes the open DMA tag: the quadwords after it are added to its first word.
    // Returns the cursor.
    GuestPtr<uint32_t> sdk_sceGifPkTerminate(GuestPtr<GifPacket> packet)
    {
        GuestPtr<uint32_t> cursor = packet->cursor;
        const GuestPtr<uint32_t> tag = packet->dmaTag;
        while ((cursor.address & 0xCu) != 0)
        {
            *cursor = 0;
            ++cursor;
        }
        if (tag)
            *tag += static_cast<uint32_t>((static_cast<int32_t>(cursor.address - tag.address) >> 4) - 1);
        packet->dmaTag = GuestPtr<uint32_t>{};
        packet->cursor = cursor;
        return cursor;
    }

    // A cnt tag (the data follows it), with the two VIF codes `third` and `fourth`; `first` is added to its first word.
    void sdk_sceGifPkCnt(GuestPtr<GifPacket> packet, uint32_t third, uint32_t fourth, uint32_t first)
    {
        putTag(packet, first | kTagCnt, 0, third, fourth, true);
    }

    // A ref tag: `quadwords` quadwords at `address` are sent, then the chain goes on.
    void sdk_sceGifPkRef(GuestPtr<GifPacket> packet, uint32_t address, uint32_t quadwords, uint32_t third, uint32_t fourth, uint32_t first)
    {
        putTag(packet, first | (quadwords | kTagRef), address & kPhysicalMask, third, fourth, false);
    }

    // An end tag (the data follows it and the chain stops).
    void sdk_sceGifPkEnd(GuestPtr<GifPacket> packet, uint32_t third, uint32_t fourth, uint32_t first)
    {
        putTag(packet, first | kTagEnd, 0, third, fourth, true);
    }

    // Reserves `words` words at the cursor; returns where they start.
    GuestPtr<uint32_t> sdk_sceGifPkReserve(GuestPtr<GifPacket> packet, int words)
    {
        const GuestPtr<uint32_t> start = packet->cursor;
        packet->cursor = GuestPtr<uint32_t>{start.address + (static_cast<uint32_t>(words) << 2)};
        return start;
    }

    // Writes a GIF tag (a 128 bit value) at the cursor and notes where it is. Returns the address.
    GuestPtr<uint32_t> sdk_sceGifPkOpenGifTag(GuestPtr<GifPacket> packet, Quad tag)
    {
        const GuestPtr<uint32_t> at = packet->cursor;
        port::store<uint64_t>(at.address, tag.low);
        port::store<uint64_t>(at.address + 8u, tag.high);
        packet->cursor = at + 4;
        packet->gifTag = at;
        return at;
    }

    // Sets the drawing offset of an environment so that (centreX, centreY) is the middle of its scissor area (the area's width and
    // height are its x1 and y1 plus one); with a non-zero `field` the y offset is half a pixel lower (the odd field). Both versions
    // do the same.
    namespace
    {
        void setHalfOffset(GuestPtr<GsDrawEnv> environment, int centreX, int centreY, int field)
        {
            const uint64_t scissor = environment->scissor;
            const int64_t halfWidth = static_cast<int64_t>(((scissor >> 16) & 0x7FFu) + 1u) >> 1;
            const int64_t halfHeight = static_cast<int64_t>(((scissor >> 48) & 0x7FFu) + 1u) >> 1;
            const int64_t x = static_cast<int16_t>(centreX) - halfWidth;
            const int64_t y = static_cast<int16_t>(centreY) - halfHeight;
            const uint64_t xOffset = static_cast<uint64_t>(x * 16);
            const uint64_t yOffset = static_cast<uint64_t>((((field & 0xFFFF) != 0) ? y * 16 + 8 : y * 16)) << 32;
            environment->xyoffset = xOffset | yOffset;
        }
    }

    void sdk_sceGsSetHalfOffset(GuestPtr<GsDrawEnv> environment, int centreX, int centreY, int field) { setHalfOffset(environment, centreX, centreY, field); }
    void sdk_sceGsSetHalfOffset2(GuestPtr<GsDrawEnv> environment, int centreX, int centreY, int field) { setHalfOffset(environment, centreX, centreY, field); }

    PORT_FN(sdk_sceGifPkInit);
    PORT_FN(sdk_sceGifPkReset);
    PORT_FN(sdk_sceGifPkTerminate);
    PORT_FN(sdk_sceGifPkCnt);
    PORT_FN(sdk_sceGifPkRef);
    PORT_FN(sdk_sceGifPkEnd);
    PORT_FN(sdk_sceGifPkReserve);
    PORT_FN(sdk_sceGifPkOpenGifTag);
    PORT_FN(sdk_sceGsSetHalfOffset);
    PORT_FN(sdk_sceGsSetHalfOffset2);
}
