// The claim-met layer (layer A: instances over built objects whose claim was met) and the snow layer; the rest are in
// gfx.cpp.

#include "game_functions.h"
#include "game_globals.h"
#include "render.h"
#include "sdk.h"
#include "vu0.h"

#include <bit>
#include <initializer_list>

namespace mm
{
    using packet::addressOf;
    using packet::putFloats;
    using packet::putWords;

    namespace
    {
        constexpr ps2f F(uint32_t bits) { return ps2f(std::bit_cast<float>(bits)); }
    }

    // The layer: its base object, its packets at the packet buffer (which then moves past them), its model (0x1F3B70), and
    // the two buffers of 32 instances, each instance's packet set up (a CNT tag with FLUSH and DIRECT 2, a GIF tag of one
    // A+D register, ALPHA_1 0x80_00000068, a RET); then the output offset and the colour the program gets.
    GuestPtr<ClaimMetLayer> ClaimMetLayer_ctor(GuestPtr<ClaimMetLayer> layer)
    {
        BaseObject_ctor(static_cast<int>(layer.address));
        const int packet = PacketBuf_Tell();
        PacketBuf_Seek(static_cast<int>(ClaimMetLayer_BuildPacket(layer, GuestPtr<uint32_t>{static_cast<uint32_t>(packet)}).address));
        layer->model = globals::claimMetModel.get();
        for (ClaimMetInstance &instance : layer->instances)
        {
            putWords(instance.packet, {0x10000002u, 0, 0x11000000u, 0x50000002u, 0x8001u, 0x10000000u, 0xEu, 0});
            instance.alpha = 0x68;
            instance.fade = 0x80;
            putWords(instance.alphaRegister, {0x42, 0});
            putWords(instance.ret, {0x60000000u, 0, 0, 0});
        }
        layer->outputOffset[0] = 0x110;
        putFloats(layer->colour, 1.0f, 1.0f, 1.0f, 1.0f);
        return layer;
    }

    // One more instance in this frame's buffer (at most 32) for a building of `size`, `age` frames into its effect (1 to
    // 45, mm_CityRender_CollectObjects), its step being age - 1: meshes 2(size - 1) and the next; its matrix a turn
    // about y of step / 100 times a scale in y of 0.08 per step (growing from nothing, up to 24 steps), then x, y and z,
    // 0.12 higher for each step past 24 (heights grow downwards); its fade 0x40 less 1.5 per step, not below 0.
    void ClaimMetLayer_AddInstance(GuestPtr<ClaimMetLayer> layer, int size, int age, ps2f x, ps2f y, ps2f z)
    {
        using namespace vu0;
        port::GuestFrame<0x90> frame; // the scale matrix at +0x50
        const GuestPtr<Vec4> scale{frame.ptr().address + 0x50u};
        if (!(layer->count < 0x20))
            return;
        // The instance is looked up again for every use, as in the original.
        const auto instance = [&] { return claimMetInstance(layer, layer->count); };
        const ps2f step = fromInt(age - 1);
        instance()->meshPair = size - 1;
        sdk::sceVu0RotMatrixY(addressOf(instance()->matrix), globals::kIdentityMatrix, F(0x3C23D70Au) * step);
        ps2f capped = F(0x41C00000u);
        if (step <= capped)
            capped = step;
        Mat4_Copy(scale, GuestPtr<Vec4>{globals::kIdentityMatrix});
        scale[1].y = F(0x3DA3D70Au) * capped;
        const GuestPtr<Vec4> turn = GuestPtr<Vec4>::of(instance()->matrix[0]);
        for (int row = 0; row < 4; ++row)
            vf(1 + row) = load(scale + row);
        for (int row = 0; row < 4; ++row)
            vf(5 + row) = load(turn + row);
        for (int row = 0; row < 4; ++row)
        {
            set(acc(), mul(vf(5), lane(vf(1 + row), 0)), XYZW);
            set(acc(), add(acc(), mul(vf(6), lane(vf(1 + row), 1))), XYZW);
            set(acc(), add(acc(), mul(vf(7), lane(vf(1 + row), 2))), XYZW);
            set(vf(9 + row), add(acc(), mul(vf(8), lane(vf(1 + row), 3))), XYZW);
        }
        for (int row = 0; row < 4; ++row)
            store(turn + row, vf(9 + row));
        instance()->matrix[3].x = x;
        const int32_t tens = static_cast<int32_t>(15 * static_cast<uint32_t>(age - 1)) / 10;
        instance()->matrix[3].y = y;
        instance()->matrix[3].z = z;
        int32_t fade = 0x40 - tens;
        port::store<uint32_t>(addressOf(instance()->matrix[3].w), 0);
        if (fade < 0)
            fade = 0;
        instance()->fade = fade;
        if (!(age - 1 < 0x19))
            instance()->matrix[3].y = instance()->matrix[3].y - F(0x3DF5C28Fu) * (step - F(0x41C00000u));
        layer->count = layer->count + 1;
    }


    // The snow's parameters: the flake sprite's corner ST and size, thirteen points (the flakes of a quad), eight turns
    // about y (an angle each, random, with its rotation matrix), 32 random flake offsets (x and z within half, y up to 40
    // higher) and the quad flag mask (8: every quad); then the snowy ground pass (mask x 8). Its program (0x1A9A90) draws
    // the first flakesPerQuad points on every quad, turned by one turn and moved by one offset (both picked by a hash of
    // the quad's position), wrapped into the 40 above the ground.
    void SnowLayer_InitParams(GuestPtr<SnowLayer> layer)
    {
        port::GuestFrame<0x50> frame; // spills only: the callees' frames start where they did
        const auto randomScale = [] { return fromInt(sdk::abs(static_cast<int32_t>(Random_Next()))) / F(0x461C4000u); };
        putWords(addressOf(layer->spriteST[0]), {0x0u, 0x0u, 0x3F800000u, 0x0u});
        putWords(addressOf(layer->spriteST[1]), {0x3F800000u, 0x3F800000u, 0x3F800000u, 0x0u});
        putWords(addressOf(layer->spriteSize), {0x41200000u, 0x40A00000u, 0x0u, 0x0u});
        static constexpr uint32_t kPoints[13][4] = {
            {0xBF75C28Fu, 0x42080000u, 0xBDD2F1AAu, 0x3F800000u}, {0x0u, 0x42180000u, 0x3ED0624Eu, 0x3F800000u},
            {0xBEF5C28Fu, 0x41D00000u, 0xBF5E353Fu, 0x3F800000u}, {0x3EF5C28Fu, 0x41E80000u, 0xBDD2F1AAu, 0x3F800000u},
            {0xBEF5C28Fu, 0x418C0000u, 0xBEB74BC7u, 0x3F800000u}, {0x3F75C28Fu, 0x41C40000u, 0x3E1BA5E3u, 0x3F800000u},
            {0x0u, 0x41600000u, 0xBF1CED91u, 0x3F800000u},        {0xBE75C28Fu, 0x41A00000u, 0x3F6AC083u, 0x3F800000u},
            {0xBEF5C28Fu, 0x40F00000u, 0x3E1BA5E3u, 0x3F800000u}, {0x3EF5C28Fu, 0x41300000u, 0x3F2978D5u, 0x3F800000u},
            {0x0u, 0x3F800000u, 0xBEB74BC7u, 0x3F800000u},        {0x3F3851ECu, 0x40800000u, 0xBEB74BC7u, 0x3F800000u},
            {0x3EF5C28Fu, 0x41B00000u, 0xBF5E353Fu, 0x3F800000u},
        };
        for (int i = 0; i < 13; ++i)
            putWords(addressOf(layer->points[i]), {kPoints[i][0], kPoints[i][1], kPoints[i][2], kPoints[i][3]});
        layer->mask = 8;
        for (int i = 0; i < 8; ++i)
        {
            const ps2f angle = sdk::fmodf(randomScale(), F(0x40C90FDBu)) - F(0x40490FDBu);
            layer->angles[i] = angle;
            sdk::sceVu0RotMatrixY(addressOf(layer->turns[4 * i]), globals::kIdentityMatrix, angle);
        }
        for (Vec4 &flake : layer->flakes)
        {
            flake.x = sdk::fmodf(randomScale(), F(0x3F800000u)) - F(0x3F000000u);
            flake.y = -sdk::fmodf(randomScale(), F(0x42200000u));
            flake.z = sdk::fmodf(randomScale(), F(0x3F800000u)) - F(0x3F000000u);
        }
        packet::putPassParams(layer->ground, {255.0f, 255.0f, 255.0f, 64.0f}, 0x9930A8F8u, 0x4004F685u, 0x6Cu);
        layer->ground.mask[0] = 0x8u;
        putWords(addressOf(layer->groundExtra), {0x0u, 0xBC23D70Au, 0x0u, 0x3F800000u}); // (0, -0.01, 0, 1)
    }

    PORT_FN(SnowLayer_InitParams);

    // The snow drawn for the clock (seconds): the time t from midnight (the clock, at most 5 hours, before 19:00
    // (0x10B30), else the distance to midnight) sets the flakes per quad (t / 1500 + 1, so 1 to 13; the GIF tag loops 8
    // per flake, one per quad of a chunk's eight), the flakes' alpha (t / 144, in the RGBAQ of the pass's start packet)
    // and the ground pass's ALPHA_1 FIX (t / 180); the eight turn angles advance by 0.03 (wrapping above pi) with their
    // matrices rebuilt, the 32 flakes move 0.03 in y (back to -40 once they reach 0); the projection (800 by 800, 768
    // away, 1 to 50) is multiplied by the view matrix; then the pass's ground packet, the frame's chunk packet, the
    // pass's flake packet and the end packet are queued.
    void SnowLayer_Draw(GuestPtr<SnowLayer> layer, int pass, int packet, int clock, GuestPtr<Vec4> view)
    {
        using namespace vu0;
        struct Locals
        {
            uint8_t gap_00[0x90];
            Vec4 screen;               // +0x90 mm_Math_ProjectionMatrix's screen: half-size 800 by 800 (then 1 and 10)
        };
        static_assert(offsetof(Locals, screen) == 0x90 && sizeof(Locals) == 0xA0);
        port::GuestFrameOf<Locals> frame;
        int32_t t;
        if (clock < 0x10B30)
            t = clock < 0x4651 ? clock : 0x4650;
        else
            t = sdk::abs(clock - 0x15180);
        layer->flakesPerQuad = t / 1500 + 1;
        const uint32_t start = layer->startPackets[pass];
        port::store<uint32_t>(start + 0x20u, (port::load<uint32_t>(start + 0x20u) & 0x00FFFFFFu) | (static_cast<uint32_t>(t / 144) << 24)); // RGBAQ's A
        port::store<int32_t>(layer->groundPackets[pass] + 0x24u, t / 180); // ALPHA_1's FIX
        layer->gifTag[3] = 0;
        layer->gifTag[2] = 0x4242;
        layer->gifTag[1] = 0x403B4000u;
        layer->gifTag[0] = (static_cast<uint32_t>(layer->flakesPerQuad) << 3) | 0x8000u;
        for (int i = 0; i < 8; ++i)
        {
            ps2f angle = layer->angles[i] + F(0x3CF5C28Fu);
            layer->angles[i] = angle;
            if (!(angle <= F(0x40490FDBu)))
                layer->angles[i] = angle - F(0x40C90FDBu);
            sdk::sceVu0RotMatrixY(addressOf(layer->turns[4 * i]), globals::kIdentityMatrix, layer->angles[i]);
        }
        for (Vec4 &flake : layer->flakes)
        {
            ps2f y = flake.y + F(0x3CF5C28Fu);
            if (!(y < ps2f(0.0f)))
                y = F(0xC2200000u);
            flake.y = y;
        }
        frame->screen = Vec4{800.0f, 800.0f, 1.0f, 10.0f};
        const GuestPtr<Vec4> projection = GuestPtr<Vec4>::of(layer->projection[0]);
        Math_ProjectionMatrix(GuestPtr<ps2f>{projection.address}, frame.at(&Locals::screen).as<ps2f>(), F(0x44400000u),
                              F(0x3F800000u), F(0x42480000u));
        for (int row = 0; row < 4; ++row)
            vf(1 + row) = load(view + row);
        for (int row = 0; row < 4; ++row)
            vf(5 + row) = load(projection + row);
        for (int row = 0; row < 4; ++row)
        {
            set(acc(), mul(vf(5), lane(vf(1 + row), 0)), XYZW);
            set(acc(), add(acc(), mul(vf(6), lane(vf(1 + row), 1))), XYZW);
            set(acc(), add(acc(), mul(vf(7), lane(vf(1 + row), 2))), XYZW);
            set(vf(9 + row), add(acc(), mul(vf(8), lane(vf(1 + row), 3))), XYZW);
        }
        for (int row = 0; row < 4; ++row)
            store(projection + row, vf(9 + row));
        Dma_AppendCall(static_cast<int>(layer->groundPackets[pass]));
        Dma_AppendCall(packet);
        Dma_AppendCall(static_cast<int>(layer->startPackets[pass]));
        Dma_AppendCall(static_cast<int>(layer->endPacket));
    }

    PORT_FN(SnowLayer_Draw);

    PORT_FN(ClaimMetLayer_ctor);
    PORT_FN(ClaimMetLayer_AddInstance);

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

    GuestPtr<ClaimMetLayer> ClaimMetLayer_DeletingDtor(GuestPtr<ClaimMetLayer> layer, int16_t deleteFlag)
    {
        return GuestPtr<ClaimMetLayer>{layerDeletingDtor(layer.address, deleteFlag)};
    }

    GuestPtr<WaterBottomLayer> WaterBottomLayer_DeletingDtor(GuestPtr<WaterBottomLayer> layer, int16_t deleteFlag)
    {
        return GuestPtr<WaterBottomLayer>{layerDeletingDtor(layer.address, deleteFlag)};
    }

    GuestPtr<PlacementLayer> PlacementLayer_DeletingDtor(GuestPtr<PlacementLayer> layer, int16_t deleteFlag)
    {
        return GuestPtr<PlacementLayer>{layerDeletingDtor(layer.address, deleteFlag)};
    }

    GuestPtr<RainLayer> RainLayer_DeletingDtor(GuestPtr<RainLayer> layer, int16_t deleteFlag)
    {
        return GuestPtr<RainLayer>{layerDeletingDtor(layer.address, deleteFlag)};
    }

    GuestPtr<SnowLayer> SnowLayer_DeletingDtor(GuestPtr<SnowLayer> layer, int16_t deleteFlag)
    {
        return GuestPtr<SnowLayer>{layerDeletingDtor(layer.address, deleteFlag)};
    }

    GuestPtr<LitRoadLayer> LitRoadLayer_DeletingDtor(GuestPtr<LitRoadLayer> layer, int16_t deleteFlag)
    {
        return GuestPtr<LitRoadLayer>{layerDeletingDtor(layer.address, deleteFlag)};
    }

    GuestPtr<WaterLayer> WaterLayer_DeletingDtor(GuestPtr<WaterLayer> layer, int16_t deleteFlag)
    {
        return GuestPtr<WaterLayer>{layerDeletingDtor(layer.address, deleteFlag)};
    }

    PORT_FN(ClaimMetLayer_DeletingDtor);
    PORT_FN(WaterBottomLayer_DeletingDtor);
    PORT_FN(PlacementLayer_DeletingDtor);
    PORT_FN(RainLayer_DeletingDtor);
    PORT_FN(SnowLayer_DeletingDtor);
    PORT_FN(LitRoadLayer_DeletingDtor);
    PORT_FN(WaterLayer_DeletingDtor);
}
