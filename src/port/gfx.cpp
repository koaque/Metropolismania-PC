// Graphics helpers: DMA lists and the packet buffer, model accessors, render layer and light state.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

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

    // ---- drawing: queue an object's prebuilt packet on the DMA list

    void Packet_Draw(GuestPtr<uint32_t> packets) { Dma_AppendCall(static_cast<int>(*packets)); }
    void Packet2_Draw(GuestPtr<uint32_t> packets, int second) { Dma_AppendCall(static_cast<int>(packets[second == 0 ? 0 : 1])); }
    void TexSprite_Draw(GuestPtr<TexSprite> sprite) { Dma_AppendCall(static_cast<int>(sprite->packet)); }

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
        LayerG_Draw(map->water, static_cast<int>(port::load<uint32_t>(0x001BDCD0u) & 1u), city->unk_B0);
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

        // Fills the next instance slot with a matrix, flags and colour and queues it, then the mesh's packet.
        void queueInstance(GuestPtr<A3DObject> model, GuestPtr<Vec4> matrix, GuestPtr<Vec4> colour, int mesh, int flags)
        {
            const uint32_t buffer = port::load<uint32_t>(globals::kInstanceBuffers + 4u * static_cast<uint32_t>(globals::instanceBuffer.get()));
            const GuestPtr<InstanceSlot> slot = GuestPtr<InstanceSlot>{buffer} + globals::instanceCount.get();
            for (int row = 0; row < 4; ++row)
                slot->matrix[row] = matrix[row];
            port::store<int32_t>(slot.address + 0x5Cu, flags);
            slot->colour = *colour;
            Dma_AppendCall(static_cast<int>(slot.address));
            const uint32_t meshData = port::load<uint32_t>(modelNodes(model) + static_cast<uint32_t>(mesh) * 0xC0u + 4u);
            if (meshData != 0)
                Dma_AppendCall(static_cast<int>(meshData + 0x10u));
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

    // As Instance_DrawNode, also within a second count and limit.
    void Instance_DrawNodeLimited(GuestPtr<A3DObject> model, GuestPtr<Vec4> colour, int node, int flags)
    {
        if (globals::instanceCount.get() >= kMaxInstances || globals::instanceLimitedCount.get() >= globals::instanceLimit.get())
            return;
        queueInstance(model, nodeMatrix(model, node), colour, node, flags);
        globals::instanceLimitedCount.set(globals::instanceLimitedCount.get() + 1);
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

    void PacketBuf_Seek(int address) { globals::packetBufPos.set(address); }
    int PacketBuf_Tell() { return globals::packetBufPos.get(); }

    void LayerA_SetSource(GuestPtr<LayerA> layer, int source)
    {
        layer->source = source;
        layer->count = 0;
    }

    void LightSet_SetField20(GuestPtr<LightSet> lights, int value) { port::store<int32_t>(lights->unk_00.address + 0x20u, value); }

    PORT_FN(DmaList_BeginScene);
    PORT_FN(LayerB_ctor);
    PORT_FN(LayerC_ctor);
    PORT_FN(LayerD_ctor);
    PORT_FN(LayerE_ctor);
    PORT_FN(LayerF_ctor);
    PORT_FN(LayerG_ctor);
    PORT_FN(CityRender_DrawOverlays);
    PORT_FN(A3DObject_DrawNode);
    PORT_FN(A3DObject_Draw);
    PORT_FN(A3DObject_UpdateMatrices);
    PORT_FN(Packet_Draw);
    PORT_FN(Packet2_Draw);
    PORT_FN(TexSprite_Draw);
    PORT_FN(LayerF_Draw);
    PORT_FN(LayerB_Draw);
    PORT_FN(Sky_Draw);
    PORT_FN(CityRender_DrawInstanceLayer);
    PORT_FN(CityRender_DrawWater);
    PORT_FN(CityRender_DrawBuildings);
    PORT_FN(Map_SetPlacementHighlight);
    PORT_FN(InstanceBase_ctor);
    PORT_FN(Instance_DrawSwayingIfRoom);
    PORT_FN(Instance_DrawMesh);
    PORT_FN(Instance_DrawNode);
    PORT_FN(Instance_DrawNodeLimited);
    PORT_FN(Instance_DrawModel);
    PORT_FN(Perf_AdaptDrawRange);
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
}
