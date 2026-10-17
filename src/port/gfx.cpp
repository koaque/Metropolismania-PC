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
}
