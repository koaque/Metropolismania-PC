// Graphics helpers: DMA lists and the packet buffer, model accessors, render layer and light state.

#include "game_functions.h"
#include "game_globals.h"

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

    void PacketBuf_Seek(int address) { globals::packetBufPos.set(address); }
    int PacketBuf_Tell() { return globals::packetBufPos.get(); }

    void LayerA_SetSource(GuestPtr<LayerA> layer, int source)
    {
        layer->source = source;
        layer->count = 0;
    }

    void LightSet_SetField20(GuestPtr<LightSet> lights, int value) { port::store<int32_t>(lights->unk_00.address + 0x20u, value); }

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
}
