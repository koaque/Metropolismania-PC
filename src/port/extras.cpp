// The extras pass (an enhancement, off by default): what a wider (MMPC_WIDESCREEN) or longer (MMPC_VIEW_DISTANCE) view
// shows beyond the PS2's own, drawn with the game's own draw code, then undone, so the game's logic stays the PS2's.
//
// The game moves a person only while drawing them, advances their animation, ticks claim-met counters and more inside its
// draw code, so drawing more the plain way would change the city's simulation. Instead, when frame N's list is about to be
// sent (DmaList_KickFrame), and the city drew its world in it:
//   1. guest memory is snapshotted (PS2Memory::beginGuestSnapshot: each RDRAM page saved at its first write, the scratchpad
//      and timer 0 state kept), with the R5900 context; no time is charged and no thread switch happens meanwhile;
//   2. the tiles the wider or longer view sees are found by the same test as Terrain_Draw's with the larger screen, reach and
//      far distance, less the tiles the game's own walk drew this frame;
//   3. they are drawn in batches through the game's own passes (terrain patches, objects, people, water, weather, claim-met)
//      into the other DMA list and instance buffer, each batch closed and handed to the native renderer, which copies it out
//      (render::takeExtras) and later draws it in frame N's stream where the HUD starts;
//   4. everything is put back. People outside the PS2's view are thus drawn where the game has them, standing: their step,
//      pose and flags are undone each frame.

#include "abi.h"
#include "enhance.h"
#include "game_functions.h"
#include "game_globals.h"
#include "render.h"
#include "render_native.h"
#include "sdk.h"
#include "vu0.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace mm
{
    namespace
    {
        constexpr int kMapSize = 128;
        constexpr int kBatchTiles = 96;    // tiles a batch: their objects and people stay well within the 1800 instance slots
        constexpr int kQuadsRoom = 0x9C0 - 64, kSidesRoom = 0x870 - 96;

        struct ExtraTile
        {
            int col, row;
            uint32_t object;
            float distance, x, z;
        };

        bool onMap(int x, int z) { return x >= 0 && x < kMapSize && z >= 0 && z < kMapSize; }

        // The wider or longer view's tiles that the game's own walk did not draw this frame, near to far. Terrain_Draw's
        // projection and test (map.cpp), with the screen widened by `wide` and the reach and far distance times `distance`.
        void collectTiles(GuestPtr<CityMap> map, GuestPtr<Vec4> view, GuestPtr<Vec4> camera, int zoom, int variant, float wide,
                          float distance, std::vector<ExtraTile> &out)
        {
            using namespace vu0;
            port::GuestFrame<0x1A0> frame;
            const uint32_t sp = frame.ptr().address;
            const auto F = [](uint32_t bits) { return ps2f(std::bit_cast<float>(bits)); };
            const uint32_t farTable = variant == 0 ? globals::kTerrainFarA : globals::kTerrainFarB;
            const uint32_t heights = variant == 0 ? globals::kTerrainZoomHeightsA : globals::kTerrainZoomHeightsB;
            const GuestPtr<Vec4> screen{sp + 0x80u}, offset{sp + 0x90u}, result{sp + 0xA0u};
            const GuestPtr<Vec4> projection{sp + 0xB0u}, projectionWide{sp + 0xF0u}, inverse{sp + 0x140u};
            const GuestPtr<Vec4> here{sp + 0x180u}, centre{sp + 0x190u};
            screen->z = F(0x3F800000u);
            screen->w = F(0x3F800000u);
            offset->x = F(0u);
            offset->y = F(0u);
            offset->z = F(0u);
            offset->w = F(0x3F800000u);
            ps2f farDistance;
            int reach;
            if (zoom < 2)
            {
                const uint32_t i = 4u * static_cast<uint32_t>(zoom);
                screen->x = port::load<float>(globals::kTerrainScreenX + i);
                screen->y = port::load<float>(globals::kTerrainScreenY + i);
                offset->z = port::load<float>(globals::kTerrainCameraZ + i);
                farDistance = port::load<float>(farTable + i);
                reach = port::load<int32_t>(globals::kTerrainReach + i);
            }
            else
            {
                int k = 1;
                while (camera->y < port::load<float>(heights + 4u * static_cast<uint32_t>(k + 1)))
                    ++k;
                const uint32_t i = 4u * static_cast<uint32_t>(k);
                const ps2f low = port::load<float>(heights + i), high = port::load<float>(heights + i + 4u);
                const ps2f t = (ps2f(camera->y) - low) / (high - low);
                const ps2f rest = F(0x3F800000u) - t;
                const auto blend = [&](uint32_t table) {
                    return (rest * port::load<float>(table + i)) + t * port::load<float>(table + i + 4u);
                };
                screen->x = blend(globals::kTerrainScreenX);
                screen->y = blend(globals::kTerrainScreenY);
                farDistance = blend(farTable);
                offset->z = blend(globals::kTerrainCameraZ);
                reach = t < F(0x3F000000u) ? port::load<int32_t>(globals::kTerrainReach + i)
                                           : port::load<int32_t>(globals::kTerrainReach + i + 4u);
            }
            screen->x = ps2f(screen->x) * ps2f(wide);
            farDistance = farDistance * ps2f(distance);
            reach = std::min(static_cast<int>(std::ceil(static_cast<float>(reach) * distance)), kMapSize + 32);
            Math_ProjectionMatrix(GuestPtr<ps2f>{projection.address}, GuestPtr<ps2f>{screen.address}, F(0x44400000u), F(0x3DCCCCCDu),
                                  farDistance);
            sdk::sceVu0MulMatrix(projection.address, projection.address, view.address);
            screen->x = ps2f(screen->x) + F(0x43480000u);
            Math_ProjectionMatrix(GuestPtr<ps2f>{projectionWide.address}, GuestPtr<ps2f>{screen.address}, F(0x44400000u),
                                  F(0x3DCCCCCDu), farDistance);
            sdk::sceVu0MulMatrix(projectionWide.address, projectionWide.address, view.address);
            std::memcpy(inverse.get(), view.get(), 3 * sizeof(Vec4));
            store(inverse + 3, vf(0));
            sdk::sceVu0InversMatrix(inverse.address, inverse.address);
            applyMatrix(inverse, offset, offset);
            offset->x = ps2f(offset->x) + ps2f(camera->x);
            offset->z = ps2f(offset->z) + ps2f(camera->z);
            const ps2f mapHalf = F(0x42800000u), half = F(0x3F000000u), one = F(0x3F800000u);
            const int tileX = sdk::fptosi(mapHalf + ps2f(offset->x));
            const int tileZ = sdk::fptosi(mapHalf - ps2f(offset->z));
            const int left = tileX - reach, top = tileZ - reach, right = tileX + reach, bottom = tileZ + reach;
            vf(1) = load(projection);
            vf(2) = load(projection + 1);
            vf(3) = load(projection + 2);
            vf(4) = load(projection + 3);
            vf(5) = load(projectionWide);
            vf(6) = load(projectionWide + 1);
            vf(7) = load(projectionWide + 2);
            vf(8) = load(projectionWide + 3);
            vf(9) = load(camera);
            here->w = one;
            for (int row = top; row < bottom; ++row)
            {
                here->z = half + (fromInt(0x80 - row) - mapHalf) - one;
                for (int col = left; col < right; ++col)
                {
                    if (port::enhance::isPs2Tile(col, row))
                        continue;
                    here->x = (fromInt(col) - mapHalf) - half + one;
                    const ps2f height = Map_GetHeightAt(map, col, row);
                    here->y = height;
                    uint32_t object = 0;
                    if (onMap(col, row))
                    {
                        object = map->tiles[col + row * kMapSize].firstObject;
                        const GuestPtr<MapObject> placed = map->objects + static_cast<int>(object);
                        const GuestPtr<BuildingType> type{globals::buildingTypeAddress(placed->type)};
                        if ((placed->flags & 0x80u) != 0)
                            continue;
                        const ps2f size = fromInt(type->size);
                        centre->x = ((fromInt(placed->tile & 0x7F) - mapHalf) + size / F(0x40000000u));
                        centre->y = height;
                        centre->z = (fromInt(0x80 - (placed->tile >> 7)) - mapHalf) - size / F(0x40000000u);
                    }
                    else
                    {
                        centre->x = here->x;
                        centre->y = here->y;
                        centre->z = here->z;
                    }
                    vf(10) = load(centre);
                    vf(11) = load(here);
                    set(vf(10), sub(vf(10), vf(9)), XYZW);
                    set(acc(), mul(vf(1), lane(vf(11), 0)), XYZW);
                    set(acc(), add(acc(), mul(vf(2), lane(vf(11), 1))), XYZW);
                    set(acc(), add(acc(), mul(vf(3), lane(vf(11), 2))), XYZW);
                    set(vf(10), mul(vf(10), vf(10)), X | Z);
                    set(vf(12), add(acc(), mul(vf(4), lane(vf(11), 3))), XYZW);
                    set(acc(), mul(vf(5), lane(vf(11), 0)), XYZW);
                    set(acc(), add(acc(), mul(vf(6), lane(vf(11), 1))), XYZW);
                    set(vf(10), add(vf(10), lane(vf(10), 2)), X);
                    set(acc(), add(acc(), mul(vf(7), lane(vf(11), 2))), XYZW);
                    set(vf(13), add(acc(), mul(vf(8), lane(vf(11), 3))), XYZW);
                    clipw(12);
                    q() = sqrtQ(vu0::x(vf(10)));
                    clipw(13);
                    set(vf(10), add(vf(0), _mm_set1_ps(q())), XYZW);
                    const uint32_t clip = clipFlags();
                    store(result, vf(10));
                    const ps2f dist = result->x;
                    bool draw;
                    if (!(ps2f(camera->y) <= F(0xC1200000u)))
                    {
                        if (dist < F(0x41100000u))
                            draw = (clip & 0x3Bu) == 0 || dist <= F(0x40000000u);
                        else
                            draw = dist <= farDistance && (clip & 0xFC0u) == 0;
                    }
                    else
                        draw = (clip & 0xFC0u) == 0;
                    if (draw)
                        out.push_back(ExtraTile{col, row, object, static_cast<float>(dist), static_cast<float>(ps2f(here->x)),
                                                static_cast<float>(ps2f(here->z))});
                }
            }
            std::stable_sort(out.begin(), out.end(), [](const ExtraTile &a, const ExtraTile &b) { return a.distance < b.distance; });
        }

        // The cliff-side buffer this frame's side packet draws. Terrain_Draw writes the buffer of the frame's parity, but the
        // game keeps the first section's packet in both side packet slots (Terrain_BuildSidePackets), so buffer 0 is drawn on
        // every frame: on odd frames the game shows the sides it found the frame before. A batch is drawn as soon as it is
        // made, so its sides go where the packet reads (the first chunk's REF), else odd frames would draw no far sides and
        // repeat the game's own in every batch.
        uint32_t sideBufferDrawn(GuestPtr<CityMap> map)
        {
            const uint32_t parity = globals::frameParity.get() & 1u;
            const uint32_t source = port::load<uint32_t>(map->sidePackets[parity] + 0x64u);
            for (uint32_t b = 0; b < 2; ++b)
                if (source == (map->sideBuffers[b].address & 0x7FFFFFFFu))
                    return b;
            return parity;
        }

        // Starts a batch: the other DMA list and the terrain walk's state as Terrain_Draw starts them.
        void startBatch(GuestPtr<CityMap> map, uint32_t list)
        {
            globals::dmaListPtr.set(list);
            globals::instanceCount.set(0);
            globals::instanceLimitedCount.set(0);
            globals::instanceLimit.set(0x708); // the PS2's adaptive limit (Perf_AdaptDrawRange) was for its own draws
            for (int i = 0; i < 150; ++i)
            {
                TerrainNode &node = map->buckets[i];
                const uint32_t at = GuestPtr<TerrainNode>::of(node).address;
                node.next = i == 149 ? 0u : at + sizeof(TerrainNode);
                node.prev = i == 0 ? 0u : at - sizeof(TerrainNode);
                node.object = 0;
            }
            map->sortedCount = 0;
            map->terrainQuads = 0;
            const uint32_t parity = globals::frameParity.get() & 1u;
            map->terrainWrite = map->terrainBuffers[parity];
            map->sideVertices = 0;
            map->sideWrite = map->sideBuffers[sideBufferDrawn(map)];
        }

        // One tile into the batch, as Terrain_Draw adds one; `drawObject` false leaves its object out of the object and
        // people passes (drawn already, by the game's walk or an earlier batch), keeping its ground.
        void addTile(GuestPtr<CityMap> map, const ExtraTile &t, bool drawObject, ps2f level)
        {
            const int entry = map->sortedCount;
            TerrainEntry &node = map->sorted[entry];
            node.object = drawObject ? t.object : 0u;
            node.distance = t.distance;
            int bucket = sdk::fptosi(ps2f(4.0f) * ps2f(t.distance));
            bucket = bucket < 0 ? 0 : bucket >= 150 ? 149 : bucket;
            TerrainNode &head = map->buckets[bucket];
            const uint32_t at = GuestPtr<TerrainEntry>::of(node).address;
            node.next = head.next;
            head.next = at;
            node.prev = head.prev;
            head.prev = at;
            Terrain_DrawPatch(map, t.col, t.row, static_cast<int>(t.object), ps2f(t.x), ps2f(t.z), level);
            map->sortedCount = entry + 1;
        }

        // A batch fills a few of the terrain buffer's 624 quad slots, but the game's chunk lists send every slot (the empty
        // ones flagged off) through every pass that runs them: the chunk packet that the ground, the sea and lake bottoms,
        // the lit roads and the weather's ground pass CALL, and the water's and the weather's own lists; the same for the
        // cliff-side buffer's 720 triangle slots. Each list gets a RET in place of its first chunk past the batch's last quad
        // or triangle, put back once the renderer has taken the batch; the chunks left out hold only flagged-off slots, which
        // draw nothing (checked: every frame's draw list the same). MMPC_EXTRAS_TRIM=0 sends them all, as the game does; a
        // number picks the lists (1 the chunk packet, 2 the water's, 4 the rain's, 8 the snow's, 16 the cliff sides').
        struct CutChunk
        {
            uint32_t at;
            uint32_t words[4];
        };
        std::vector<CutChunk> s_cuts;

        uint32_t trimChunks()
        {
            static const uint32_t lists = [] {
                const char *v = std::getenv("MMPC_EXTRAS_TRIM");
                return v == nullptr ? 0x1Fu : static_cast<uint32_t>(std::strtoul(v, nullptr, 0));
            }();
            return lists;
        }

        // The list whose first chunk is at `first` (`chunks` of them, each a REF tag `tag` of `chunkBytes` from `source`
        // on, its UNPACK and an MSCNT): a RET in place of chunk `used`. Left as it is when that chunk is not the one expected.
        void cutChunks(uint32_t first, uint32_t chunks, uint32_t tag, uint32_t source, uint32_t chunkBytes, uint32_t used)
        {
            if (used >= chunks)
                return;
            const uint32_t at = first + 0x20u * used;
            if (port::load<uint32_t>(at) != tag || port::load<uint32_t>(at + 4u) != ((source + chunkBytes * used) & 0x7FFFFFFFu))
            {
                static bool s_warned = false;
                if (!s_warned)
                {
                    s_warned = true;
                    std::cerr << "[extras] a chunk list is not laid out as expected at 0x" << std::hex << at << std::dec
                              << ": sent whole" << std::endl;
                }
                return;
            }
            CutChunk cut{at, {}};
            for (uint32_t i = 0; i < 4; ++i)
                cut.words[i] = port::load<uint32_t>(at + 4u * i);
            s_cuts.push_back(cut);
            port::store<uint32_t>(at, 0x60000000u); // RET, no data, no VIF codes
            port::store<uint32_t>(at + 4u, 0u);
            port::store<uint32_t>(at + 8u, 0u);
            port::store<uint32_t>(at + 12u, 0u);
        }

        void cutBatchLists(GuestPtr<CityMap> map)
        {
            const uint32_t parity = globals::frameParity.get() & 1u;
            const uint32_t quads = static_cast<uint32_t>(map->terrainQuads) / 4u;
            const uint32_t buffer = map->terrainBuffers[parity].address;
            const auto chunksFor = [&](uint32_t per) { return (quads + per - 1u) / per; };
            const uint32_t lists = trimChunks();
            if (lists & 1u)
                cutChunks(map->chunkPackets[parity], 24, 0x30000068u, buffer, 0x680u, chunksFor(26));
            if ((lists & 2u) && map->water.address != 0)
                cutChunks(map->water->packets[parity] + 0xA0u, 39, 0x30000040u, buffer, 0x400u, chunksFor(16));
            if ((lists & 4u) && map->rain.address != 0)
                cutChunks(map->rain->startPackets[parity] + 0x50u, 78, 0x30000020u, buffer, 0x200u, chunksFor(8));
            if ((lists & 8u) && map->snow.address != 0)
                cutChunks(map->snow->startPackets[parity] + 0x90u, 78, 0x30000020u, buffer, 0x200u, chunksFor(8));
            if (lists & 16u) // the cliff sides: 36 triangles a chunk, in the buffer the side packet draws
            {
                const uint32_t triangles = static_cast<uint32_t>(map->sideVertices) / 3u;
                cutChunks(map->sidePackets[parity] + 0x60u, 20, 0x3000006Cu, map->sideBuffers[sideBufferDrawn(map)].address, 0x6C0u,
                          (triangles + 35u) / 36u);
            }
        }

        void restoreBatchLists()
        {
            for (auto cut = s_cuts.rbegin(); cut != s_cuts.rend(); ++cut)
                for (uint32_t i = 0; i < 4; ++i)
                    port::store<uint32_t>(cut->at + 4u * i, cut->words[i]);
            s_cuts.clear();
        }

        // Ends the batch's terrain as Terrain_Draw does (the unfilled quads and side triangles are not drawn), then the
        // world passes the city runs after its walk (city.cpp, CityScene's draw), and an END tag.
        void drawBatch(GuestPtr<CityMap> map, GuestPtr<CityScene> city)
        {
            for (int quads = map->terrainQuads; quads < 0x9C0; quads += 4)
            {
                port::store<uint32_t>(packet::addressOf(GuestPtr<Vec4>{map->terrainWrite.address}[2].w), 0u);
                map->terrainWrite = GuestPtr<void>{map->terrainWrite.address + 0x40u};
            }
            for (int sides = map->sideVertices; sides < 0x870; sides += 3)
            {
                port::store<uint32_t>(packet::addressOf(GuestPtr<Vec4>{map->sideWrite.address}[2].w), 0u);
                map->sideWrite = GuestPtr<void>{map->sideWrite.address + 0x30u};
            }
            if (trimChunks() != 0)
                cutBatchLists(map);
            port::enhance::markFrameList(port::enhance::Span::WorldCamera);
            Packet_Draw(GuestPtr<uint32_t>::of(city->cityCamera->base.packet));
            CityRender_DrawTerrain(map, city);
            DmaList_QueueGsDefaults();
            if (city->hudVisible[0] != 0)
            {
                LightSet_SelectProgram(city->lights, 1);
                CityRender_CollectObjects(map, city);
            }
            LightSet_SelectProgram(city->lights, 0);
            if (city->hudVisible[1] != 0)
                CityRender_UpdatePeople(map, city);
            CityRender_DrawWater(map, city);
            const int32_t weather = city->weatherToday;
            if (weather == 2 || weather == 3)
                CityRender_DrawWeather(map, city, weather == 2 ? 0 : 1, GuestPtr<Vec4>::of(city->cityCamera->base.view[0]));
            CityRender_DrawClaimMet(map);
            DmaList_QueueGsDefaults();
            LightSet_SelectProgram(city->lights, 0);
            packet::putCallThenEnd(globals::dmaListPtr.get(), globals::kGsDefaultsPacket & 0x7FFFFFFFu);
        }

        struct Stats
        {
            uint32_t passes = 0, tiles = 0, batches = 0, pages = 0, sides = 0;
            double seconds = 0.0, worst = 0.0;
        };
    }

    // DmaList_KickFrame calls this before it sends the frame's list (gfx.cpp).
    void Extras_Run()
    {
        const uint32_t cityAddress = port::enhance::takeCityWorld();
        if (cityAddress == 0 || !render::extrasWanted())
            return;
        const auto start = std::chrono::steady_clock::now();
        PS2Memory &memory = port::g_call.runtime->memory();
        R5900Context *ctx = port::g_call.ctx;
        const R5900Context saved = *ctx;
        if (!memory.beginGuestSnapshot())
            return;
        port::g_sidePass = true;
        port::enhance::setSidePass(true);

        const GuestPtr<CityScene> city{cityAddress};
        const GuestPtr<CityMap> map = city->map;
        const GuestPtr<CityCamera> camera = city->cityCamera;
        static std::vector<ExtraTile> tiles;
        tiles.clear();
        collectTiles(map, GuestPtr<Vec4>::of(camera->base.view[0]), GuestPtr<Vec4>::of(camera->base.eye),
                     CityCamera_GetZoomLevel(camera, city), city->weatherToday, port::enhance::widescreen(),
                     port::enhance::viewDistance(), tiles);
        // objects the game's walk drew this frame (its sorted entries, still in memory) or an earlier batch: not again
        static std::vector<uint8_t> drawn;
        drawn.assign(0x10000, 0);
        for (int i = 0; i < map->sortedCount; ++i)
            drawn[map->sorted[i].object & 0xFFFFu] = 1;
        // The other DMA list, made the current one as the game's next frame makes it (DmaList_Flip): the appends measure the
        // room left from the current list's start (appendDmaCall), so with the index left as it is every append into the list
        // above the current one counted as overflow and was dropped (every other frame lost its extras). Put back with the rest.
        globals::dmaListIndex.set(globals::dmaListIndex.get() ^ 1u);
        const uint32_t list = port::load<uint32_t>(globals::kDmaLists + 4u * globals::dmaListIndex.get());
        globals::instanceBuffer.set(globals::instanceBuffer.get() ^ 1);
        const ps2f level = city->waterLevel;
        uint32_t batches = 0, sides = 0;
        static bool s_warned = false;
        for (size_t next = 0; next < tiles.size();)
        {
            startBatch(map, list);
            int count = 0;
            while (next < tiles.size() && count < kBatchTiles && map->terrainQuads < kQuadsRoom && map->sideVertices < kSidesRoom)
            {
                const ExtraTile &t = tiles[next++];
                const bool fresh = t.object != 0 && drawn[t.object & 0xFFFFu] == 0;
                if (fresh)
                    drawn[t.object & 0xFFFFu] = 1;
                addTile(map, t, fresh, level);
                ++count;
            }
            sides += static_cast<uint32_t>(map->sideVertices) / 3u;
            drawBatch(map, city);
            if (!s_warned && (globals::instanceCount.get() >= 0x708 - 64 || globals::dmaListPtr.get() - list > 0xF000u))
            {
                s_warned = true;
                std::cerr << "[extras] a batch came close to the instance slots or the DMA list's end (" << globals::instanceCount.get()
                          << " instances, " << (globals::dmaListPtr.get() - list) << " bytes)" << std::endl;
            }
            render::takeExtras(list);
            restoreBatchLists();
            ++batches;
        }

        port::enhance::setSidePass(false);
        port::g_sidePass = false;
        const size_t pages = memory.endGuestSnapshot();
        *ctx = saved;

        // MMPC_EXTRAS_STATS=1: every 120 passes, the tiles, batches, pages put back and the time on the game thread
        static const bool s_stats = [] {
            const char *v = std::getenv("MMPC_EXTRAS_STATS");
            return v != nullptr && v[0] == '1';
        }();
        if (s_stats)
        {
            static Stats stats;
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            ++stats.passes;
            stats.tiles += static_cast<uint32_t>(tiles.size());
            stats.batches += batches;
            stats.sides += sides;
            stats.pages += static_cast<uint32_t>(pages);
            stats.seconds += seconds;
            stats.worst = std::max(stats.worst, seconds);
            if (stats.passes == 120)
            {
                const double n = 120.0;
                std::cerr << "[extras] a pass: " << stats.tiles / n << " tiles in " << stats.batches / n << " batches, "
                          << stats.sides / n << " cliff-side triangles, " << stats.pages / n << " pages put back, "
                          << stats.seconds * 1000.0 / n << " ms (" << stats.worst * 1000.0 << " worst)" << std::endl;
                stats = Stats{};
            }
        }
    }
}
