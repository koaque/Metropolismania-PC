// The city map: 128 x 128 tiles, the placed objects and the per-tile object lists, and the people.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

namespace mm
{
    namespace
    {
        constexpr int kMapSize = 128;

        bool onMap(int x, int z) { return x >= 0 && x < kMapSize && z >= 0 && z < kMapSize; }
        int clampToMap(int v) { return v < 0 ? 0 : v >= kMapSize ? kMapSize - 1 : v; }
        MapTile &tileAt(GuestPtr<CityMap> map, int x, int z) { return map->tiles[x + z * kMapSize]; }

        uint16_t tileFlag(GuestPtr<CityMap> map, int x, int z, uint16_t flag)
        {
            return onMap(x, z) ? static_cast<uint16_t>(tileAt(map, x, z).flags & flag) : 0;
        }
    }

    // ---- tiles

    uint16_t Map_TileFlag01(GuestPtr<CityMap> map, int x, int z) { return tileFlag(map, x, z, 0x01); }
    uint16_t Map_TileFlag08(GuestPtr<CityMap> map, int x, int z) { return tileFlag(map, x, z, 0x08); }
    uint16_t Map_TileFlag10(GuestPtr<CityMap> map, int x, int z) { return tileFlag(map, x, z, 0x10); }
    uint16_t Map_TileFlag20(GuestPtr<CityMap> map, int x, int z) { return tileFlag(map, x, z, 0x20); }

    // Walkable flag (0x10) of a tile, 0 for indices off the map.
    uint16_t Map_TileIndexFlag10(GuestPtr<CityMap> map, uint32_t tile)
    {
        return tile < 0x4000u ? static_cast<uint16_t>(map->tiles[tile].flags & 0x10u) : 0;
    }

    // Tile kind 0..3 in flags 4 (high bit) and 2 (low bit).
    int Map_GetTileKind(GuestPtr<CityMap> map, int tile)
    {
        const uint16_t flags = map->tiles[tile].flags;
        return ((flags & 4) ? 2 : 0) + ((flags & 2) ? 1 : 0);
    }

    // Roads go on even tiles only (2x2 blocks), owned by nobody, type 4.
    void Map_PlaceRoad(GuestPtr<CityMap> map, uint32_t x, uint32_t z, int arg)
    {
        Map_PlaceObject(map, 0, 4, static_cast<int>(x & ~1u), static_cast<int>(z & ~1u), arg);
    }

    void Map_SetTileHeight(GuestPtr<CityMap> map, uint32_t tile, ps2f height)
    {
        map->tiles[tile].height = height;
        const int index = static_cast<int>(tile);
        Map_UpdateTileGeometry(map, index % kMapSize, index / kMapSize);
    }

    void Map_SetTileKind(GuestPtr<CityMap> map, int tile, uint32_t kind)
    {
        uint16_t flags = map->tiles[tile].flags & 0xFFF9u;
        if (kind & 2)
            flags |= 4;
        if (kind & 1)
            flags |= 2;
        map->tiles[tile].flags = flags;
    }

    ps2f Map_GetTileHeightWord(GuestPtr<CityMap> map, int tile) { return map->tiles[tile].height; }

    ps2f Map_GetHeightAt(GuestPtr<CityMap> map, int x, int z) { return tileAt(map, clampToMap(x), clampToMap(z)).height; }

    ps2f Map_HeightScale(ps2f height) { return ps2f(0.08f) + ps2f(0.6f) * height; }

    // Clears a size x size square at (x, z): each tile loses its first object (the map's ClearObject slot, +0xC)
    // until a type-2 object (wasteland) comes first.
    void Map_ClearArea(GuestPtr<CityMap> map, int x, int z, int size)
    {
        for (int tz = z; tz < z + size; ++tz)
            for (int tx = x; tx < x + size; ++tx)
            {
                const int tile = tx + tz * kMapSize;
                do
                    port::callGuest<void>(port::load<uint32_t>(map->vtable.address + 0xCu), map.address, map->tiles[tile].firstObject);
                while (map->objects[map->tiles[tile].firstObject].type != 2);
            }
    }

    // The ground height at world (x, z): the tile's four corner heights make two triangles split along a
    // diagonal, and the plane of the one the point is in gives the height (scaled by Map_HeightScale when
    // above 0). Off the map, the edge tile and edge coordinate are used. The corners and the plane go through
    // stack vectors as in the original: their w lanes are never written and VU0 loads them.
    ps2f Map_SampleHeight(GuestPtr<CityMap> map, ps2f x, ps2f z)
    {
        // Declared in this order, they take the original's slots, entry sp - 0x10 down to - 0x50.
        port::GuestLocal<Vec4> plane;
        port::GuestLocal<Vec4> d;
        port::GuestLocal<Vec4> c;
        port::GuestLocal<Vec4> b;
        port::GuestLocal<Vec4> a;
        ps2f pz = z + ps2f(0.001f);
        ps2f px = x + ps2f(0.001f);
        int ix = sdk::fptosi(ps2f(64.0f) + px);
        int iz = sdk::fptosi(ps2f(64.0f) - pz);
        if (ix < 0)
        {
            px = -64.0f;
            ix = 0;
        }
        else if (ix >= kMapSize)
        {
            px = 64.0f;
            ix = kMapSize - 1;
        }
        if (iz < 0)
        {
            pz = 64.0f;
            iz = 0;
        }
        else if (iz >= kMapSize)
        {
            pz = -64.0f;
            iz = kMapSize - 1;
        }
        const ps2f half = 0.5f;
        const ps2f xMid = half + (fromInt(ix) - ps2f(64.0f));
        const ps2f x0 = xMid - half;
        const ps2f zMid = (fromInt(kMapSize - iz) - ps2f(64.0f)) - half;
        const ps2f z0 = half + zMid;
        const ps2f x1 = half + xMid;
        const ps2f z1 = zMid - half;
        a->x = x0;
        a->z = z0;
        b->x = x1;
        b->z = z0;
        c->x = x1;
        c->z = z1;
        d->x = x0;
        d->z = z1;
        const MapTile &tile = map->tiles[ix + iz * kMapSize];
        a->y = tile.verts[0];
        b->y = tile.verts[1];
        c->y = tile.verts[2];
        d->y = tile.verts[3];
        GuestPtr<Vec4> corner;
        if (pz <= d->z + (px - d->x))
        {
            Math_TriangleNormal(plane, b, c, d);
            corner = b;
        }
        else
        {
            Math_TriangleNormal(plane, a, b, d);
            corner = a;
        }
        plane->w = (-plane->x * corner->x - plane->y * corner->y) - plane->z * corner->z;
        const ps2f height = -(plane->w + (plane->x * px + plane->z * pz)) / plane->y;
        if (height <= ps2f(0.0f))
            return height;
        return Map_HeightScale(height);
    }

    // The tile's four vertex values; returns the last one.
    ps2f Map_GetTileVertex(GuestPtr<CityMap> map, int tile, GuestPtr<ps2f> out)
    {
        for (int i = 0; i < 4; ++i)
            out[i] = map->tiles[tile].verts[i];
        return out[3];
    }

    void Map_GetTileVertexClamped(GuestPtr<CityMap> map, int x, int z, GuestPtr<ps2f> out)
    {
        const MapTile &tile = tileAt(map, clampToMap(x), clampToMap(z));
        for (int i = 0; i < 4; ++i)
            out[i] = tile.verts[i];
    }

    // ---- objects

    GuestPtr<MapObject> Map_GetRecord(GuestPtr<CityMap> map, int object) { return map->objects + object; }

    // The tile's first object; for a tile without one (firstObject 0x3F80) this is the record past the end.
    GuestPtr<MapObject> Map_GetTileFirstObject(GuestPtr<CityMap> map, int tile) { return map->objects + map->tiles[tile].firstObject; }

    // As Map_GetTileFirstObject by coordinates; off the map it is record 0.
    GuestPtr<MapObject> Map_GetTileObject(GuestPtr<CityMap> map, int x, int z)
    {
        if (!onMap(x, z))
            return map->objects;
        return map->objects + tileAt(map, x, z).firstObject;
    }

    GuestPtr<uint16_t> Map_GetTileList(GuestPtr<CityMap> map, int list, GuestPtr<uint32_t> count) { return TileLists_Get(map->tileLists, list, count); }

    // Position of `object` in the tile's object list (it must be on the tile).
    int Map_CountObjectsBefore(GuestPtr<CityMap> map, int x, int z, uint16_t object)
    {
        int count = 0;
        for (const uint16_t *link = &tileAt(map, x, z).firstObject; *link != object; link = &map->objects[*link].nextOnTile)
            ++count;
        return count;
    }

    // The home of a family: an object of a type that has people inside (type flag 0x40) whose family is
    // `family`. Record 0 is not searched.
    GuestPtr<MapObject> Map_FindFamilyHome(GuestPtr<CityMap> map, uint16_t family)
    {
        for (int i = 1; i < 0x3F80; ++i)
        {
            const MapObject &obj = map->objects[i];
            if (obj.type != 0 && obj.family == family && (globals::buildingTypeFlags(obj.type) & 0x40) != 0)
                return map->objects + i;
        }
        return GuestPtr<MapObject>{0};
    }

    // ---- people lists (6000 ends a list)

    // The person at the cursor, advancing the cursor along the list; null at the end.
    GuestPtr<MapPerson> People_Next(GuestPtr<CityMap> map, GuestPtr<uint16_t> cursor)
    {
        if (*cursor == kNoPerson)
            return GuestPtr<MapPerson>{0};
        const GuestPtr<MapPerson> person = map->people + *cursor;
        *cursor = person->nextOnTile;
        return person;
    }

    // Objects on one tile stack: each one sits 0.8 below the one before it in the tile's list. 0 when the
    // object is not in its own tile's list.
    ps2f MapObj_GetHeight(GuestPtr<CityMap> map, uint32_t object)
    {
        const uint16_t tile = map->objects[object].tile;
        ps2f height = Map_GetTileHeightWord(map, tile);
        for (uint16_t id = map->tiles[tile].firstObject; id != kNoObject; id = map->objects[id].nextOnTile)
        {
            if (id == object)
                return height;
            height = height - ps2f(0.8f);
        }
        return 0.0f;
    }

    // Takes `person` out of the person list of whichever object on `tile` holds it, and flags it 0x800.
    // Returns that object's id, or 0x3F80 when no object on the tile has the person.
    uint16_t Map_UnlinkPersonFromTile(GuestPtr<CityMap> map, int tile, uint32_t person)
    {
        for (GuestPtr<MapObject> obj = Map_GetTileFirstObject(map, tile);; obj = map->objects + obj->nextOnTile)
        {
            for (uint16_t *link = &obj->firstPerson; *link != kNoPerson; link = &map->people[*link].nextOnTile)
                if (*link == person)
                {
                    MapPerson &entry = map->people[*link];
                    *link = entry.nextOnTile;
                    entry.nextOnTile = kNoPerson;
                    entry.flags |= 0x800u;
                    return obj->id;
                }
            if (obj->nextOnTile == kNoObject)
                return kNoObject;
        }
    }

    void MapObj_AddPerson(GuestPtr<CityMap> map, int object, int person)
    {
        const uint16_t first = map->objects[object].firstPerson;
        map->objects[object].firstPerson = static_cast<uint16_t>(person);
        map->people[person].nextOnTile = first;
        map->people[person].flags &= 0xF7FFu;
    }

    // Marks every person in the list removed (flag 0x800) and empties the list.
    void People_RemoveChain(GuestPtr<CityMap> map, GuestPtr<uint16_t> head)
    {
        while (*head != kNoPerson)
        {
            MapPerson &person = map->people[*head];
            person.flags |= 0x800u;
            *head = kNoPerson;
            head = GuestPtr<uint16_t>::of(person.nextOnTile);
        }
    }

    // The object on a tile whose people list holds `person`; none for a person marked removed (0x800).
    GuestPtr<MapObject> Map_FindPersonObjectOnTile(GuestPtr<CityMap> map, uint32_t person, uint32_t tile)
    {
        const uint16_t id = static_cast<uint16_t>(person);
        if ((map->people[id].flags & 0x800u) == 0)
            for (uint16_t o = map->tiles[tile & 0xFFFFu].firstObject; o != 0x3F80; o = map->objects[o].nextOnTile)
                for (uint16_t p = map->objects[o].firstPerson; p != kNoPerson; p = map->people[p].nextOnTile)
                    if (map->people[p].index == id)
                        return map->objects + o;
        return GuestPtr<MapObject>{0};
    }

    // A person on the tile working at `work` whose character class has flag 1 (at class +0x18).
    GuestPtr<MapPerson> Map_FindWorkerOnTile(GuestPtr<CityMap> map, uint16_t work, uint32_t tile)
    {
        for (uint16_t o = map->tiles[tile & 0xFFFFu].firstObject; o != 0x3F80; o = map->objects[o].nextOnTile)
            for (uint16_t p = map->objects[o].firstPerson; p != kNoPerson; p = map->people[p].nextOnTile)
            {
                const MapPerson &person = map->people[p];
                const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person.charType)};
                if (work == person.work && (cls->flags & 1u) != 0)
                    return map->people + p;
            }
        return GuestPtr<MapPerson>{0};
    }

    // Removes object record `object` (1..0x3F7F): out of its claim section's tile lists, type cleared, and the
    // map's object and category counts lowered.
    void MapObj_Delete(GuestPtr<CityMap> map, int object)
    {
        if (object <= 0 || object >= kNoObject)
            return;
        const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(map->objects[object].type)};
        TileLists_RemoveAll(map->tileLists, object, GuestPtr<int16_t>{static_cast<uint32_t>(Claim_GetSectionC(btype->claimC))});
        map->objects[object].type = 0;
        --map->objectCount;
        const GuestPtr<int32_t> categoryCount = GuestPtr<int32_t>::of(map->categoryCount[0]) + btype->category;
        if (btype->category == 4)
        {
            if ((btype->flags & 0x80u) == 0)
                --*categoryCount;
            return;
        }
        if (btype->category == 5 && btype->size >= 3)
            --map->bigCategory5Count;
        --*categoryCount;
    }

    // Sets the height of a size x size block of tiles from (x, z), skipping tiles off the map, and rebuilds
    // each one's geometry.
    void Map_SetAreaHeight(GuestPtr<CityMap> map, int x, int z, int size, ps2f height)
    {
        for (int row = 0; row < size; ++row)
            for (int col = 0; col < size; ++col)
            {
                const int tz = z + row, tx = x + col;
                if (!onMap(tx, tz))
                    continue;
                tileAt(map, tx, tz).height = height;
                Map_UpdateTileGeometry(map, tx, tz);
            }
    }

    // Takes `item` out of the tile lists named by a zero-terminated list of (list, unused) halfword pairs.
    void TileLists_RemoveAll(GuestPtr<TileLists> lists, int item, GuestPtr<int16_t> pairs)
    {
        for (; *pairs != 0; pairs = pairs + 2)
            TileLists_Remove(lists, static_cast<uint32_t>(item), static_cast<uint16_t>(*pairs));
    }

    // Recomputes the geometry of every tile in rect = (x0, z0, x1, z1), inclusive.
    void Map_UpdateAreaGeometry(GuestPtr<CityMap> map, GuestPtr<int32_t> rect)
    {
        for (int z = rect[1]; z <= rect[3]; ++z)
            for (int x = rect[0]; x <= rect[2]; ++x)
                Map_UpdateTileGeometry(map, x, z);
    }

    // Before placing over the size x size block at (x, z): the box around every object standing there
    // (box = min x, min z, max x, max z, widened by two tiles within the map) and whether the cells those
    // objects cover stay below the free object records plus the objects found. A tile whose first object is
    // record 0 counts as empty, and an object is counted once per tile of the block it covers, as in the
    // original.
    int Placement_CheckNeighbours(GuestPtr<CityMap> map, int x, int z, int size, GuestPtr<int32_t> box)
    {
        box[3] = 0;
        box[2] = 0;
        box[1] = 0x7F;
        box[0] = 0x7F;
        int cells = 0, objects = 0;
        for (int row = z; row < z + size; ++row)
            for (int col = x, tile = x + row * kMapSize; col < x + size; ++col, ++tile)
            {
                const uint16_t id = map->tiles[tile].firstObject;
                if (id == 0)
                    continue;
                ++objects;
                const MapObject &obj = map->objects[id];
                const int objSize = port::load<uint8_t>(globals::buildingTypeAddress(obj.type) + 0x142u);
                const int ox = obj.tile & 0x7F, oz = obj.tile >> 7;
                for (int cz = oz; cz < oz + objSize; ++cz)
                    for (int cx = ox; cx < ox + objSize; ++cx)
                    {
                        if (cx < box[0])
                            box[0] = cx;
                        if (cz < box[1])
                            box[1] = cz;
                        if (box[2] < cx)
                            box[2] = cx;
                        if (box[3] < cz)
                            box[3] = cz;
                        ++cells;
                    }
            }
        if (box[0] >= 2)
            box[0] -= 2;
        if (box[1] >= 2)
            box[1] -= 2;
        if (box[2] < 0x7F)
            box[2] += 2;
        if (box[3] < 0x7F)
            box[3] += 2;
        return cells < (0x3F80 - map->objectCount) + objects;
    }

    // Placement rule (type +0x144) for stacking: the tile's top object must be an entrance (0x20), not
    // highlighted (1), of a type with the same rule, and the stack below the type's limit (+0x138) - 1.
    int Placement_CheckTypeRule(GuestPtr<CityMap> map, int type, int tile)
    {
        const uint32_t btype = globals::buildingTypeAddress(static_cast<uint32_t>(type));
        const int8_t rule = port::load<int8_t>(btype + 0x144u);
        if (rule == 0 || (map->tiles[tile].flags & 1u) == 0)
            return 0;
        uint16_t top = map->tiles[tile].firstObject;
        if (top == 0x3F80)
            return 0;
        int below = 0;
        for (; map->objects[top].nextOnTile != 0x3F80; top = map->objects[top].nextOnTile)
            ++below;
        const MapObject &obj = map->objects[top];
        return (obj.flags & 0x20u) != 0 && below < static_cast<int>(port::load<uint16_t>(btype + 0x138u)) - 1 && (obj.flags & 1u) == 0 &&
               port::load<int8_t>(globals::buildingTypeAddress(obj.type) + 0x144u) == rule;
    }

    // ---- terrain drawing

    // Appends a quad (four vertices) to the terrain buffer while there is room (0x9BD vertices). The w
    // words carry draw data: the first a texture entry from t1 (with the base at 0x1BD990), the second an
    // entry of the table at 0x1AF360, the third t3; the fourth vertex is copied whole.
    void Terrain_AddQuad(GuestPtr<CityMap> map, GuestPtr<Vec4> a, GuestPtr<Vec4> b, GuestPtr<Vec4> c, GuestPtr<Vec4> d, int t1, int t2, int t3)
    {
        if (map->terrainQuads >= 0x9BD)
            return;
        auto put = [&](GuestPtr<Vec4> src, const uint32_t *w)
        {
            const GuestPtr<Vec4> dst{map->terrainWrite.address};
            *dst = *src;
            if (w)
                port::store<uint32_t>(dst.address + 0xCu, *w);
            map->terrainWrite.address += 16u;
        };
        const uint32_t w0 = port::load<uint32_t>(0x001BD990u) | static_cast<uint32_t>((t1 - 1) * 0x40 / 4 + 0x2588);
        const uint32_t w1 = port::load<uint32_t>(0x001AF360u + 4u * static_cast<uint32_t>(t2)) + 0x21u;
        const uint32_t w2 = static_cast<uint32_t>(t3);
        put(a, &w0);
        put(b, &w1);
        put(c, &w2);
        put(d, nullptr);
        map->terrainQuads += 4;
    }

    // ---- tile lists

    // Inserts `item` at the start of each list in `pairs` (list ids at even positions, 0 ends): the items
    // after it move up one place and the later lists start one item later.
    void TileLists_InsertAll(GuestPtr<TileLists> lists, uint16_t item, GuestPtr<uint16_t> pairs)
    {
        const GuestPtr<uint16_t> items = GuestPtr<uint16_t>::of(lists->items[0]);
        const GuestPtr<uint16_t> start = GuestPtr<uint16_t>::of(lists->start[0]);
        const GuestPtr<uint16_t> count = GuestPtr<uint16_t>::of(lists->count[0]);
        for (uint16_t list = *pairs; list != 0; list = *pairs)
        {
            const uint16_t at = start[list];
            for (int i = 0x3F7F; i > at; --i)
                items[i] = items[i - 1];
            for (int l = list + 1; l < 0x50; ++l)
                ++start[l];
            pairs = pairs + 2;
            ++count[list];
            items[at] = item;
        }
    }

    GuestPtr<TileLists> TileLists_Clear(GuestPtr<TileLists> lists)
    {
        for (uint16_t &item : lists->items)
            item = 0;
        for (int i = 0; i < 0x50; ++i)
        {
            lists->start[i] = 0;
            lists->count[i] = 0;
        }
        return lists;
    }

    PORT_FN(Map_TileFlag01);
    PORT_FN(Map_TileFlag08);
    PORT_FN(Map_TileFlag10);
    PORT_FN(Map_TileFlag20);
    PORT_FN(Map_TileIndexFlag10);
    PORT_FN(Map_GetTileKind);
    PORT_FN(Map_SetTileKind);
    PORT_FN(Map_PlaceRoad);
    PORT_FN(Map_SetTileHeight);
    PORT_FN(Map_GetTileHeightWord);
    PORT_FN(Map_GetHeightAt);
    PORT_FN(Map_HeightScale);
    PORT_FN(Map_SampleHeight);
    PORT_FN(Map_ClearArea);
    PORT_FN(Map_GetTileVertex);
    PORT_FN(Map_GetTileVertexClamped);
    PORT_FN(Map_GetRecord);
    PORT_FN(Map_GetTileFirstObject);
    PORT_FN(Map_GetTileObject);
    PORT_FN(Map_GetTileList);
    PORT_FN(Map_CountObjectsBefore);
    PORT_FN(Map_FindFamilyHome);
    PORT_FN(People_Next);
    PORT_FN(MapObj_AddPerson);
    PORT_FN(MapObj_GetHeight);
    PORT_FN(Map_UnlinkPersonFromTile);
    PORT_FN(People_RemoveChain);
    PORT_FN(TileLists_Clear);
    PORT_FN(TileLists_InsertAll);
    PORT_FN(Map_FindPersonObjectOnTile);
    PORT_FN(Map_FindWorkerOnTile);
    PORT_FN(Placement_CheckTypeRule);
    PORT_FN(Placement_CheckNeighbours);
    PORT_FN(TileLists_RemoveAll);
    PORT_FN(Map_SetAreaHeight);
    PORT_FN(MapObj_Delete);
    PORT_FN(Map_UpdateAreaGeometry);
    PORT_FN(Terrain_AddQuad);
}
