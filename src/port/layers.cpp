// The first render layer (the instance layer the city draws trees and the like into).

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"
#include "vu0.h"

#include <bit>
#include <initializer_list>

namespace mm
{
    namespace
    {
        constexpr ps2f F(uint32_t bits) { return ps2f(std::bit_cast<float>(bits)); }

        void putWords(uint32_t at, std::initializer_list<uint32_t> words)
        {
            for (const uint32_t word : words)
            {
                port::store<uint32_t>(at, word);
                at += 4u;
            }
        }
    }

    // The layer: its base object, the VU1 packet at the packet buffer (which then moves past it), its model from
    // 0x1F3B70, and two buffers of 32 instance records of 0x90 bytes (from +0x60 in 0x1200 steps), each starting with the
    // tags of its DMA packet: a REF/CALL tag, the VIF codes (UNPACK to 0x80, a GS A+D giftag) and the rest zero.
    GuestPtr<LayerA> LayerA_ctor(GuestPtr<LayerA> layer)
    {
        BaseObject_ctor(static_cast<int>(layer.address));
        const int packet = PacketBuf_Tell();
        PacketBuf_Seek(static_cast<int>(LayerA_BuildPacket(layer, GuestPtr<uint32_t>{static_cast<uint32_t>(packet)}).address));
        port::store<uint32_t>(layer.address, port::load<uint32_t>(0x001F3B70u));
        for (uint32_t buffer = 0; buffer < 2; ++buffer)
            for (uint32_t entry = 0; entry < 32; ++entry)
            {
                const uint32_t at = layer.address + 0x1200u * buffer + 0x90u * entry;
                port::store<uint32_t>(at + 0x60u, 0x10000002u);
                port::store<uint32_t>(at + 0x64u, 0);
                port::store<uint32_t>(at + 0x68u, 0);
                port::store<uint32_t>(at + 0x6Cu, 0);
                port::store<uint32_t>(at + 0x68u, 0x11000000u);
                port::store<uint32_t>(at + 0x6Cu, 0x50000002u);
                port::store<uint32_t>(at + 0x7Cu, 0);
                port::store<uint32_t>(at + 0x78u, 0xE);
                port::store<uint32_t>(at + 0x74u, 0x10000000u);
                port::store<uint32_t>(at + 0x70u, 0x8001u);
                port::store<uint32_t>(at + 0x8Cu, 0);
                port::store<uint32_t>(at + 0x88u, 0x42);
                port::store<uint32_t>(at + 0x84u, 0x80);
                port::store<uint32_t>(at + 0x80u, 0x68);
                port::store<uint32_t>(at + 0x90u, 0x60000000u);
                port::store<uint32_t>(at + 0x94u, 0);
                port::store<uint32_t>(at + 0x98u, 0);
                port::store<uint32_t>(at + 0x9Cu, 0);
            }
        port::store<uint32_t>(layer.address + 0x2410u, 0x110);
        port::store<uint32_t>(layer.address + 0x242Cu, 0x3F800000u);
        port::store<uint32_t>(layer.address + 0x2428u, 0x3F800000u);
        port::store<uint32_t>(layer.address + 0x2424u, 0x3F800000u);
        port::store<uint32_t>(layer.address + 0x2420u, 0x3F800000u);
        return layer;
    }

    // One more instance in this frame's buffer (at most 32): the record at +0x10 gets a1 - 1; its matrix (+0x20) is a
    // turn about y of (a2 - 1) / 100 times a scale that squashes y by 0.08 per step up to 6 steps, then x, y and z follow
    // (the height lowered past step 25, the rest of the record's fade at +0x84 from 0x40 less 1.5 per step).
    void LayerA_AddInstance(GuestPtr<LayerA> layer, int a1, int a2, ps2f x, ps2f y, ps2f z)
    {
        using namespace vu0;
        port::GuestFrame<0x90> frame; // the scale matrix at +0x50
        const uint32_t base = frame.ptr().address;
        if (!(layer->count < 0x20))
            return;
        const auto record = [&](uint32_t offset) {
            return layer.address + 0x1200u * static_cast<uint32_t>(layer->source) + 0x90u * static_cast<uint32_t>(layer->count) + offset;
        };
        const ps2f step = fromInt(a2 - 1);
        port::store<int32_t>(record(0x10u), a1 - 1);
        sdk::sceVu0RotMatrixY(record(0x20u), globals::kIdentityMatrix, F(0x3C23D70Au) * step);
        ps2f capped = F(0x41C00000u);
        if (step <= capped)
            capped = step;
        Mat4_Copy(GuestPtr<Vec4>{base + 0x50u}, GuestPtr<Vec4>{globals::kIdentityMatrix});
        port::store<float>(base + 0x64u, static_cast<float>(F(0x3DA3D70Au) * capped));
        const GuestPtr<Vec4> turn{record(0x20u)};
        for (int row = 0; row < 4; ++row)
            vf(1 + row) = load(GuestPtr<Vec4>{base + 0x50u + 0x10u * static_cast<uint32_t>(row)});
        for (int row = 0; row < 4; ++row)
            vf(5 + row) = load(GuestPtr<Vec4>{turn.address + 0x10u * static_cast<uint32_t>(row)});
        for (int row = 0; row < 4; ++row)
        {
            set(acc(), mul(vf(5), lane(vf(1 + row), 0)), XYZW);
            set(acc(), add(acc(), mul(vf(6), lane(vf(1 + row), 1))), XYZW);
            set(acc(), add(acc(), mul(vf(7), lane(vf(1 + row), 2))), XYZW);
            set(vf(9 + row), add(acc(), mul(vf(8), lane(vf(1 + row), 3))), XYZW);
        }
        for (int row = 0; row < 4; ++row)
            store(GuestPtr<Vec4>{turn.address + 0x10u * static_cast<uint32_t>(row)}, vf(9 + row));
        port::store<float>(record(0x50u), static_cast<float>(x));
        const int32_t tens = static_cast<int32_t>(15 * static_cast<uint32_t>(a2 - 1)) / 10;
        port::store<float>(record(0x54u), static_cast<float>(y));
        port::store<float>(record(0x58u), static_cast<float>(z));
        int32_t fade = 0x40 - tens;
        port::store<uint32_t>(record(0x5Cu), 0);
        if (fade < 0)
            fade = 0;
        port::store<int32_t>(record(0x84u), fade);
        if (!(a2 - 1 < 0x19))
            port::store<float>(record(0x54u), static_cast<float>(ps2f(port::load<float>(record(0x54u))) - F(0x3DF5C28Fu) * (step - F(0x41C00000u))));
        layer->count = layer->count + 1;
    }


    // The layer of clouds and stars: its parameter table (eight cloud shapes of two vectors each), eight turns about y
    // (an angle each, random, with the rotation matrices at +0x110) and 32 random offsets (x within half, y up to 40
    // down, z within half) at +0x350, then the packet constants (colour 255, the microcode's GS state words).
    void LayerE_InitParams(GuestPtr<LayerE> layer)
    {
        port::GuestFrame<0x50> frame; // spills only: the callees' frames start where they did
        const uint32_t l = layer.address;
        const auto randomScale = [] { return fromInt(sdk::abs(static_cast<int32_t>(Random_Next()))) / F(0x461C4000u); };
        putWords(l + 0x10u, {0x0u, 0x0u, 0x3F800000u, 0x0u});
        putWords(l + 0x20u, {0x3F800000u, 0x3F800000u, 0x3F800000u, 0x0u});
        putWords(l + 0x30u, {0x41200000u, 0x40A00000u, 0x0u, 0x0u});
        putWords(l + 0x40u, {0xBF75C28Fu, 0x42080000u, 0xBDD2F1AAu, 0x3F800000u});
        putWords(l + 0x50u, {0x0u, 0x42180000u, 0x3ED0624Eu, 0x3F800000u});
        putWords(l + 0x60u, {0xBEF5C28Fu, 0x41D00000u, 0xBF5E353Fu, 0x3F800000u});
        putWords(l + 0x70u, {0x3EF5C28Fu, 0x41E80000u, 0xBDD2F1AAu, 0x3F800000u});
        putWords(l + 0x80u, {0xBEF5C28Fu, 0x418C0000u, 0xBEB74BC7u, 0x3F800000u});
        putWords(l + 0x90u, {0x3F75C28Fu, 0x41C40000u, 0x3E1BA5E3u, 0x3F800000u});
        putWords(l + 0xA0u, {0x0u, 0x41600000u, 0xBF1CED91u, 0x3F800000u});
        putWords(l + 0xB0u, {0xBE75C28Fu, 0x41A00000u, 0x3F6AC083u, 0x3F800000u});
        putWords(l + 0xC0u, {0xBEF5C28Fu, 0x40F00000u, 0x3E1BA5E3u, 0x3F800000u});
        putWords(l + 0xD0u, {0x3EF5C28Fu, 0x41300000u, 0x3F2978D5u, 0x3F800000u});
        putWords(l + 0xE0u, {0x0u, 0x3F800000u, 0xBEB74BC7u, 0x3F800000u});
        putWords(l + 0xF0u, {0x3F3851ECu, 0x40800000u, 0xBEB74BC7u, 0x3F800000u});
        putWords(l + 0x100u, {0x3EF5C28Fu, 0x41B00000u, 0xBF5E353Fu, 0x3F800000u});
        port::store<uint32_t>(l + 0x554u, 8);
        for (uint32_t i = 0; i < 8; ++i)
        {
            const ps2f angle = sdk::fmodf(randomScale(), F(0x40C90FDBu)) - F(0x40490FDBu);
            port::store<float>(l + 0x6C0u + 4u * i, static_cast<float>(angle));
            sdk::sceVu0RotMatrixY(l + 0x110u + 0x40u * i, globals::kIdentityMatrix, angle);
        }
        for (uint32_t i = 0; i < 0x20; ++i)
        {
            const uint32_t at = l + 0x10u * i;
            port::store<float>(at + 0x350u, static_cast<float>(sdk::fmodf(randomScale(), F(0x3F800000u)) - F(0x3F000000u)));
            port::store<float>(at + 0x354u, static_cast<float>(-sdk::fmodf(randomScale(), F(0x42200000u))));
            port::store<float>(at + 0x358u, static_cast<float>(sdk::fmodf(randomScale(), F(0x3F800000u)) - F(0x3F000000u)));
        }
        putWords(l + 0x570u, {0x437F0000u, 0x437F0000u, 0x437F0000u, 0x42800000u});
        putWords(l + 0x580u, {0x9930A8F8u, 0x4004F685u, 0x0u, 0x0u});
        putWords(l + 0x590u, {0x68u, 0x0u, 0x0u, 0x0u});
        putWords(l + 0x5A0u, {0x801Au, 0xE0000000u, 0x41241206u, 0x412412u});
        putWords(l + 0x5B0u, {0x6Cu, 0x0u, 0x0u, 0x0u});
        putWords(l + 0x5C0u, {0x0u, 0x0u, 0x3F800000u, 0x0u});
        port::store<uint32_t>(l + 0x5D4u, 0x0u);
        port::store<uint32_t>(l + 0x5D8u, 0x3F800000u);
        port::store<uint32_t>(l + 0x5DCu, 0x0u);
        port::store<uint32_t>(l + 0x5E0u, 0x0u);
        port::store<uint32_t>(l + 0x5E8u, 0x3F800000u);
        port::store<uint32_t>(l + 0x5ECu, 0x0u);
        port::store<uint32_t>(l + 0x5F8u, 0x3F800000u);
        port::store<uint32_t>(l + 0x5FCu, 0x0u);
        putWords(l + 0x600u, {0x0u, 0x3F800000u, 0x3F800000u, 0x0u});
        port::store<uint32_t>(l + 0x614u, 0x3F800000u);
        port::store<uint32_t>(l + 0x618u, 0x3F800000u);
        port::store<uint32_t>(l + 0x61Cu, 0x0u);
        port::store<uint32_t>(l + 0x624u, 0x0u);
        port::store<uint32_t>(l + 0x628u, 0x3F800000u);
        port::store<uint32_t>(l + 0x62Cu, 0x0u);
        putWords(l + 0x630u, {0x3F800000u, 0x0u, 0x3F800000u, 0x0u});
        port::store<uint32_t>(l + 0x648u, 0x3F800000u);
        port::store<uint32_t>(l + 0x64Cu, 0x0u);
        port::store<uint32_t>(l + 0x650u, 0x3F800000u);
        port::store<uint32_t>(l + 0x658u, 0x3F800000u);
        port::store<uint32_t>(l + 0x65Cu, 0x0u);
        port::store<uint32_t>(l + 0x664u, 0x3F800000u);
        port::store<uint32_t>(l + 0x668u, 0x3F800000u);
        port::store<uint32_t>(l + 0x66Cu, 0x0u);
        putWords(l + 0x670u, {0x3F800000u, 0x3F800000u, 0x3F800000u, 0x0u});
        port::store<uint32_t>(l + 0x680u, 0x8u);
        putWords(l + 0x690u, {0x0u, 0xBC23D70Au, 0x0u, 0x3F800000u});
        // the five vector slots the loop above left 0.5 in f1 for
        for (const uint32_t offset : {0x5D0u, 0x5E4u, 0x5F0u, 0x5F4u, 0x610u, 0x620u, 0x640u, 0x644u, 0x654u, 0x660u})
            port::store<uint32_t>(l + offset, 0x3F000000u);
    }

    PORT_FN(LayerE_InitParams);

    // Layer E drawn for the clock (a3, seconds): the night length t (the clock held at 5 hours before 18:00, else the distance
    // from midnight) sets the layer's step count (+0x550, t / 1500 + 1) and packet words (alpha t / 144 in the first
    // packet's +0x20, t / 180 in the second's +0x24); the eight turn angles advance by 0.03 (wrapping above pi) with their
    // matrices rebuilt, the 32 offsets sink 0.03 each (back to -40 once they pass 0); the projection matrix (800 by 800, 768
    // away, 1 to 50) is multiplied by the view matrix and the pass's two packets, the packet and the end are queued.
    void LayerE_Draw(GuestPtr<LayerE> layer, int pass, int packet, int clock, GuestPtr<Vec4> view)
    {
        using namespace vu0;
        port::GuestFrame<0xA0> frame; // the screen parameters at +0x90
        const uint32_t sp = frame.ptr().address;
        const uint32_t l = layer.address;
        const uint32_t slot = l + 4u * static_cast<uint32_t>(pass);
        int32_t t;
        if (clock < 0x10B30)
            t = clock < 0x4651 ? clock : 0x4650;
        else
            t = sdk::abs(clock - 0x15180);
        port::store<int32_t>(l + 0x550u, t / 1500 + 1);
        const uint32_t first = port::load<uint32_t>(slot + 0x560u);
        port::store<uint32_t>(first + 0x20u, (port::load<uint32_t>(first + 0x20u) & 0x00FFFFFFu) | (static_cast<uint32_t>(t / 144) << 24));
        port::store<int32_t>(port::load<uint32_t>(slot + 0x6A0u) + 0x24u, t / 180);
        port::store<uint32_t>(l + 0xCu, 0);
        port::store<uint32_t>(l + 0x8u, 0x4242);
        port::store<uint32_t>(l + 0x4u, 0x403B4000u);
        port::store<uint32_t>(l + 0x0u, (static_cast<uint32_t>(port::load<int32_t>(l + 0x550u)) << 3) | 0x8000u);
        for (uint32_t i = 0; i < 8; ++i)
        {
            ps2f angle = ps2f(port::load<float>(l + 0x6C0u + 4u * i)) + F(0x3CF5C28Fu);
            port::store<float>(l + 0x6C0u + 4u * i, static_cast<float>(angle));
            if (!(angle <= F(0x40490FDBu)))
                port::store<float>(l + 0x6C0u + 4u * i, static_cast<float>(angle - F(0x40C90FDBu)));
            sdk::sceVu0RotMatrixY(l + 0x110u + 0x40u * i, globals::kIdentityMatrix, ps2f(port::load<float>(l + 0x6C0u + 4u * i)));
        }
        for (uint32_t j = 0; j < 0x20; ++j)
        {
            ps2f y = ps2f(port::load<float>(l + 0x354u + 0x10u * j)) + F(0x3CF5C28Fu);
            if (!(y < ps2f(0.0f)))
                y = F(0xC2200000u);
            port::store<float>(l + 0x354u + 0x10u * j, static_cast<float>(y));
        }
        port::store<uint32_t>(sp + 0x90u, 0x44480000u);
        port::store<uint32_t>(sp + 0x94u, 0x44480000u);
        port::store<uint32_t>(sp + 0x98u, 0x3F800000u);
        port::store<uint32_t>(sp + 0x9Cu, 0x41200000u);
        Math_ProjectionMatrix(GuestPtr<ps2f>{l + 0x310u}, GuestPtr<ps2f>{sp + 0x90u}, F(0x44400000u), F(0x3F800000u), F(0x42480000u));
        for (int row = 0; row < 4; ++row)
            vf(1 + row) = load(GuestPtr<Vec4>{view.address + 0x10u * static_cast<uint32_t>(row)});
        for (int row = 0; row < 4; ++row)
            vf(5 + row) = load(GuestPtr<Vec4>{l + 0x310u + 0x10u * static_cast<uint32_t>(row)});
        for (int row = 0; row < 4; ++row)
        {
            set(acc(), mul(vf(5), lane(vf(1 + row), 0)), XYZW);
            set(acc(), add(acc(), mul(vf(6), lane(vf(1 + row), 1))), XYZW);
            set(acc(), add(acc(), mul(vf(7), lane(vf(1 + row), 2))), XYZW);
            set(vf(9 + row), add(acc(), mul(vf(8), lane(vf(1 + row), 3))), XYZW);
        }
        for (int row = 0; row < 4; ++row)
            store(GuestPtr<Vec4>{l + 0x310u + 0x10u * static_cast<uint32_t>(row)}, vf(9 + row));
        Dma_AppendCall(port::load<int32_t>(slot + 0x6A0u));
        Dma_AppendCall(packet);
        Dma_AppendCall(port::load<int32_t>(slot + 0x560u));
        Dma_AppendCall(port::load<int32_t>(l + 0x6A8u));
    }

    PORT_FN(LayerE_Draw);

    PORT_FN(LayerA_ctor);
    PORT_FN(LayerA_AddInstance);

    namespace
    {
        // The deleting destructor shared by the render layers: the base destructor, then the delete when the flag is positive.
        uint32_t layerDeletingDtor(uint32_t layer, int16_t deleteFlag)
        {
            if (layer != 0u)
            {
                BaseObject_dtor(static_cast<int>(layer), 0);
                if (0 < deleteFlag)
                    sdk::operator_delete(layer);
            }
            return layer;
        }
    }

    GuestPtr<LayerA> LayerA_DeletingDtor(GuestPtr<LayerA> layer, int16_t deleteFlag)
    {
        return GuestPtr<LayerA>{layerDeletingDtor(layer.address, deleteFlag)};
    }

    GuestPtr<LayerB> LayerB_DeletingDtor(GuestPtr<LayerB> layer, int16_t deleteFlag)
    {
        return GuestPtr<LayerB>{layerDeletingDtor(layer.address, deleteFlag)};
    }

    GuestPtr<LayerC> LayerC_DeletingDtor(GuestPtr<LayerC> layer, int16_t deleteFlag)
    {
        return GuestPtr<LayerC>{layerDeletingDtor(layer.address, deleteFlag)};
    }

    GuestPtr<LayerD> LayerD_DeletingDtor(GuestPtr<LayerD> layer, int16_t deleteFlag)
    {
        return GuestPtr<LayerD>{layerDeletingDtor(layer.address, deleteFlag)};
    }

    GuestPtr<LayerE> LayerE_DeletingDtor(GuestPtr<LayerE> layer, int16_t deleteFlag)
    {
        return GuestPtr<LayerE>{layerDeletingDtor(layer.address, deleteFlag)};
    }

    GuestPtr<LayerF> LayerF_DeletingDtor(GuestPtr<LayerF> layer, int16_t deleteFlag)
    {
        return GuestPtr<LayerF>{layerDeletingDtor(layer.address, deleteFlag)};
    }

    GuestPtr<LayerG> LayerG_DeletingDtor(GuestPtr<LayerG> layer, int16_t deleteFlag)
    {
        return GuestPtr<LayerG>{layerDeletingDtor(layer.address, deleteFlag)};
    }

    PORT_FN(LayerA_DeletingDtor);
    PORT_FN(LayerB_DeletingDtor);
    PORT_FN(LayerC_DeletingDtor);
    PORT_FN(LayerD_DeletingDtor);
    PORT_FN(LayerE_DeletingDtor);
    PORT_FN(LayerF_DeletingDtor);
    PORT_FN(LayerG_DeletingDtor);
}
