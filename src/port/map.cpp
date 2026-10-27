// The city map: 128 x 128 tiles, the placed objects and the per-tile object lists, and the people.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"
#include "vu0.h"

#include <algorithm>
#include <bit>

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

    // Takes `person` out of the list at `head`, ending its link and flagging it removed (0x800); 1 if it was in it.
    int People_UnlinkFromList(GuestPtr<CityMap> map, GuestPtr<uint16_t> head, uint32_t person)
    {
        for (uint16_t *link = &*head; *link != kNoPerson;)
        {
            MapPerson &entry = map->people[*link];
            if (*link == person)
            {
                *link = entry.nextOnTile;
                entry.nextOnTile = kNoPerson;
                entry.flags |= 0x800u;
                return 1;
            }
            link = &entry.nextOnTile;
        }
        return 0;
    }

    // Whether ground lower than 0.5 lies just outside the footprint: one tile beyond each side, at the side's
    // corner, (x, z - 1), (x + size, z), (x, z + size) and (x - 1, z), counting tiles on the map only.
    int Placement_CheckSlope(GuestPtr<CityMap> map, int x, int z, int size)
    {
        port::GuestFrame<0x70> frame;
        const int xs[4] = {x, x + size, x, x - 1};
        const int zs[4] = {z - 1, z, z + size, z};
        for (int i = 0; i < 4; ++i)
        {
            if (xs[i] < 0 || xs[i] >= 0x80 || zs[i] < 0 || zs[i] >= 0x80)
                continue;
            const GuestPtr<MapObject> obj = map->objects + map->tiles[xs[i] + zs[i] * 128].firstObject;
            if (Map_GetTileHeightWord(map, obj->tile) < ps2f(0.5f))
                return 1;
        }
        return 0;
    }

    // Whether `type` can stand at (x, z): 1 yes, 2 when its type rule forbids the tile, 0 when its footprint
    // leaves the map, the map holds 0x3F80 objects, the neighbours forbid it (mm_Placement_CheckNeighbours,
    // which also widens `rect`), or a footprint tile's object is of the same type, locked (flag 1), of a type
    // without flag 4, on ground at or below 0 for a type with flag 0x10, or refused by the type (vtable
    // +0x20). Other than ground (2) and wasteland (3), a type placed above height 0.5 also needs lower ground
    // beside it (mm_Placement_CheckSlope).
    int Map_CheckPlacement(GuestPtr<CityMap> map, uint32_t type, int x, int z, GuestPtr<int32_t> rect)
    {
        port::GuestFrame<0xE0> frame;
        const int tile = x + z * 128;
        const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(type)};
        const int size = btype->size;
        if (x < 0 || z < 0 || x + size >= 0x80 || z + size >= 0x80)
            return 0;
        if (map->objectCount >= 0x3F80)
            return 0;
        if (Placement_CheckTypeRule(map, static_cast<int>(type), tile) != 0)
            return 2;
        if (Placement_CheckNeighbours(map, x, z, size, rect) == 0)
            return 0;
        for (int tz = z; tz < z + size; ++tz)
            for (int tx = x; tx < x + size; ++tx)
            {
                const GuestPtr<MapObject> obj = map->objects + map->tiles[tx + tz * 128].firstObject;
                if (type == obj->type || (obj->flags & 1u) != 0)
                    return 0;
                if ((GuestPtr<BuildingType>{globals::buildingTypeAddress(obj->type)}->flags & 4u) == 0)
                    return 0;
                if ((btype->flags & 0x10u) != 0 && Map_GetHeightAt(map, tx, tz) <= ps2f(0.0f))
                    return 0;
                const uint32_t accepts = port::load<uint32_t>(btype->vtable.address + 0x20u);
                if (port::callGuest<int64_t>(accepts, btype, obj, type, map, tx, tz) == 0)
                    return 0;
            }
        if (type == 2 || type == 3)
            return 1;
        if (Map_GetTileHeightWord(map, tile) <= ps2f(0.5f))
            return 1;
        return Placement_CheckSlope(map, x, z, size) != 0 ? 1 : 0;
    }

    // Links `object` at the end of tile `tile`'s object list.
    void Map_AppendObjectToTile(GuestPtr<CityMap> map, int tile, uint16_t object)
    {
        uint16_t *link = &map->tiles[tile].firstObject;
        while (*link != kNoObject)
            link = &map->objects[*link].nextOnTile;
        *link = object;
    }

    // Moves the next-record index on to a free object record (type 0), wrapping from 0x3F80 to 1, and returns it;
    // 0 when 0x3F81 tries find none.
    int MapObj_FindFreeSlot(GuestPtr<CityMap> map)
    {
        for (int tries = 0; map->objects[map->nextId].type != 0;)
        {
            ++map->nextId;
            if (++tries >= 0x3F81)
                return 0;
            if (map->nextId >= 0x3F80)
                map->nextId = 1;
        }
        return map->nextId;
    }

    // Places an object of `type` at (x, z) for `owner` when the placement test allows it (0x3F80 when not):
    // result 1 clears the footprint and creates the object normally; any other result puts a new record on top
    // of the tile's list. The touched area's geometry is then rebuilt and the type is told (vtable +0x10, with
    // `arg` and the object).
    int Map_PlaceObject(GuestPtr<CityMap> map, int owner, int type, int x, int z, int arg)
    {
        port::GuestFrame<0x80> frame;
        const GuestPtr<int32_t> rect{frame.ptr().address + 0x70u};
        rect[0] = 0x7F;
        rect[1] = 0x7F;
        rect[2] = 0;
        rect[3] = 0;
        const int placement = Map_CheckPlacement(map, static_cast<uint32_t>(type), x, z, rect);
        if (placement == 0)
            return kNoObject;
        int object;
        if (placement == 1)
        {
            Map_ClearArea(map, x, z, GuestPtr<BuildingType>{globals::buildingTypeAddress(static_cast<uint32_t>(type))}->size);
            object = Map_CreateObject(map, owner, type, x, z);
        }
        else
        {
            const int tile = x + z * 128;
            object = MapObj_FindFreeSlot(map);
            MapObj_Init(map, object, type, static_cast<uint16_t>(owner), static_cast<uint16_t>(tile));
            Map_AppendObjectToTile(map, tile, static_cast<uint16_t>(object));
        }
        Map_UpdateAreaGeometry(map, rect);
        if (object != kNoObject)
        {
            const GuestPtr<MapObject> obj = map->objects + object;
            const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(obj->type)};
            port::callGuest<void>(port::load<uint32_t>(btype->vtable.address + 0x10u), btype, arg, obj, 0);
        }
        map->unk_60 = 0;
        return object;
    }

    // Unlinks `object` from the object list of `tile`, ending its own link.
    void Map_RemoveObjectFromTile(GuestPtr<CityMap> map, int tile, uint32_t object)
    {
        for (uint16_t *link = &map->tiles[tile].firstObject; *link != kNoObject; link = &map->objects[*link].nextOnTile)
            if (*link == object)
            {
                MapObject &obj = map->objects[*link];
                const uint16_t next = obj.nextOnTile;
                obj.nextOnTile = kNoObject;
                *link = next;
                return;
            }
    }

    // A building goes. Its workers (work = object) are taken off the tile they stand on and freed (charType 0);
    // for residents (classes without flag 1) the building they were heading for loses one from its +0x16 count,
    // the one they were in (a type with flag 0x40) one from +0x17, and the population drops. Everyone else in it
    // is put outside, and targets and destinations naming it are cleared. If a worker went, the building's
    // family moves away.
    void MapPeople_RemoveForObject(GuestPtr<CityMap> map, uint32_t object)
    {
        bool workerLeft = false;
        for (int i = 0; i < 6000; ++i)
        {
            MapPerson &person = map->people[i];
            if (person.charType == 0)
                continue;
            const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person.charType)};
            if (person.work != object)
            {
                if (person.inside == object)
                {
                    person.inside = kNoObject;
                    person.destination = kNoObject;
                    person.walkTile = 0x4000;
                    person.previousTile = 0x4000;
                    person.flags &= 0xFEFFu;
                }
                if ((cls->flags & 1u) == 0 && person.target == object)
                    person.target = kNoObject;
                if (person.destination == object)
                    person.destination = kNoObject;
                continue;
            }

            const int z = sdk::fptosi(ps2f(64.0f) - person.z);
            const int x = sdk::fptosi(ps2f(64.0f) + person.x);
            for (GuestPtr<MapObject> obj = Map_GetTileFirstObject(map, x + z * 128);; obj = map->objects + obj->nextOnTile)
                if (People_UnlinkFromList(map, GuestPtr<uint16_t>::of(obj->firstPerson), static_cast<uint32_t>(i)) != 0 ||
                    obj->nextOnTile == kNoObject)
                    break;
            person.charType = 0;
            if ((cls->flags & 1u) == 0)
            {
                if (person.target != kNoObject && person.target != object)
                {
                    const GuestPtr<MapObject> heading = Map_GetRecord(map, person.target);
                    if (heading->headingHere != 0)
                        --heading->headingHere;
                }
                const uint16_t inside = person.inside;
                if (inside != kNoObject && inside != object && inside != person.target)
                {
                    const GuestPtr<MapObject> in = Map_GetRecord(map, inside);
                    if ((GuestPtr<BuildingType>{globals::buildingTypeAddress(in->type)}->flags & 0x40u) != 0 && in->occupants != 0)
                        --in->occupants;
                }
                --map->population;
            }
            --map->peopleInUse;
            workerLeft = true;
        }
        if (workerLeft)
            Family_MoveAway(map->objects[object].family);
    }

    // Clears an object off the map: its people (its own list and everyone tied to it), then, unless it is ground
    // (type 2), its tile link and footprint, whose tiles take the first object left on its tile; the record is
    // deleted, and a footprint left empty is filled with ground objects. A type with flag 8 marks the objects
    // changed.
    void CityMap_ClearObject(GuestPtr<CityMap> map, int object)
    {
        const uint16_t tile = map->objects[object].tile;
        const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(map->objects[object].type)};
        if ((btype->flags & 8u) != 0)
            map->objectsChanged = 1;
        People_RemoveChain(map, GuestPtr<uint16_t>::of(map->objects[object].firstPerson));
        MapPeople_RemoveForObject(map, static_cast<uint32_t>(object));
        if (map->objects[object].type == 2)
            return;
        Map_RemoveObjectFromTile(map, tile, static_cast<uint32_t>(object));
        const uint16_t first = map->tiles[tile].firstObject;
        const int size = btype->size;
        const int tileX = tile & 0x7F, tileZ = tile >> 7;
        for (int z = tileZ; z < tileZ + size; ++z)
            for (int x = tileX; x < tileX + size; ++x)
                map->tiles[x + z * 128].firstObject = first;
        MapObj_Delete(map, object);
        if (map->tiles[tile].firstObject != kNoObject)
            return;
        const int refill = btype->size;
        for (int z = tileZ; z < tileZ + refill; ++z)
            for (int x = tileX; x < tileX + refill; ++x)
                Map_CreateObject(map, 0, 2, x, z);
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

    // Fills object record `object` for a new `type` object owned by `owner` (a family) on `tile` and runs the
    // type's InitRecord slot (+0xC), then counts it the way MapObj_Delete uncounts it: a type with flag 8 adds to
    // the flag-8 objects and this period's history entry (up to 255) and marks the objects changed, one with flag
    // 2 starts the refresh delay (0x81 ticks); the object goes into its claim section's tile lists, the object
    // count and its category count.
    void MapObj_Init(GuestPtr<CityMap> map, int object, int type, uint16_t owner, uint16_t tile)
    {
        const auto record = [&] { return map->objects + object; };
        record()->type = static_cast<uint16_t>(type);
        record()->id = static_cast<uint16_t>(map->nextId);
        record()->tile = tile;
        record()->firstPerson = kNoPerson;
        record()->nextOnTile = kNoObject;
        record()->flags = 0;
        port::store<uint32_t>(record().address + 0x10u, 0);
        port::store<uint32_t>(record().address + 0x14u, 0);
        port::store<uint32_t>(record().address + 0x18u, 0);
        record()->family = owner;
        const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(static_cast<uint32_t>(type))};
        port::callGuest<void>(port::load<uint32_t>(btype->vtable.address + 0xCu), btype.address, record().address);
        if ((btype->flags & 8u) != 0)
        {
            ++map->flag8Objects;
            map->objectsChanged = 1;
            if (map->flag8History[0] < 0xFF)
                ++map->flag8History[0];
        }
        else if ((btype->flags & 2u) != 0)
            map->refreshDelay = 0x81;
        TileLists_InsertAll(map->tileLists, static_cast<uint16_t>(object),
                            GuestPtr<uint16_t>{static_cast<uint32_t>(Claim_GetSectionC(btype->claimC))});
        ++map->objectCount;
        const GuestPtr<int32_t> categoryCount = GuestPtr<int32_t>::of(map->categoryCount[0]) + btype->category;
        if (btype->category == 4)
        {
            if ((btype->flags & 0x80u) == 0)
                ++*categoryCount;
            return;
        }
        if (btype->category == 5 && btype->size >= 3)
            ++map->bigCategory5Count;
        ++*categoryCount;
    }

    // Terrain generation: marks the objects of the low tiles (height 0.1 or below) whose centres lie within
    // `radius` of tile (x, z)'s with a scenery kind (the object's +0x14 byte). Kind 2 only where none is set,
    // kind 0 clears; other kinds take each tile with a chance falling off with the distance (a random 0..9 at
    // most ten times the fraction of the radius left). Distances on VU0; the original's frame is reserved
    // whole and its two points kept at their offsets.
    void TerrainGen_ScatterObjects(GuestPtr<CityMap> map, int x, int z, int kind, ps2f radius)
    {
        using namespace vu0;
        port::GuestFrame<0xC0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<Vec4> centre{sp + 0x80u}, point{sp + 0x90u}, distance{sp + 0xB0u};
        const ps2f half = 0.5f, middle = 64.0f;
        const auto toFloat = [](int v) { return ps2f(static_cast<float>(v)); };
        const auto place = [&](GuestPtr<Vec4> at, int tx, int tz) {
            at->x = half + (toFloat(tx) - middle);
            at->y = 0.0f;
            at->z = (toFloat(kMapSize - tz) - middle) - half;
            at->w = 1.0f;
        };
        place(centre, x, z);
        const int reach = sdk::fptosi(ps2f(1.0f) + radius);
        const int x0 = std::max(x - reach - 4, 0);
        const int z0 = std::max(z - reach - 4, 0);
        const int x1 = x + reach + 4 < kMapSize ? x + reach + 4 : kMapSize - 1;
        const int z1 = z + reach + 4 < kMapSize ? z + reach + 4 : kMapSize - 1;
        for (int tz = z0; tz < z1; ++tz)
        {
            if (tz < 0 || tz >= kMapSize)
                continue;
            for (int tx = x0; tx < x1; ++tx)
            {
                if (tx < 0 || tx >= kMapSize)
                    continue;
                if (ps2f(0.1f) < tileAt(map, tx, tz).height)
                    continue;
                place(point, tx, tz);
                vf(4) = load(centre);
                vf(5) = load(point);
                set(vf(6), sub(vf(5), vf(4)), XYZW);
                set(vf(7), mul(vf(6), vf(6)), XYZ);
                set(vf(8), add(vf(7), lane(vf(7), 1)), X);
                set(vf(8), add(vf(8), lane(vf(7), 2)), X);
                q() = sqrtQ(vu0::x(vf(8)));
                set(vf(9), add(vf(0), _mm_set1_ps(q())), XYZW);
                store(distance, vf(9));
                const ps2f d = distance->x;
                if (!(d < radius))
                    continue;
                const GuestPtr<uint8_t> mark{Map_GetTileObject(map, tx, tz).address + 0x14u};
                if (kind == 2)
                {
                    if (*mark == 0)
                        *mark = 2;
                }
                else if (kind == 0)
                    *mark = 0;
                else
                {
                    const int chance = sdk::fptosi(ps2f(10.0f) * (ps2f(1.0f) - d / radius));
                    const int roll = static_cast<int32_t>(Random_Next()) % 10;
                    if (!(chance < roll))
                        *mark = static_cast<uint8_t>(kind);
                }
            }
        }
    }

    // A tile's four corner heights (verts 0..3: the corners towards (x-1, z-1), (x+1, z-1), (x+1, z+1) and
    // (x-1, z+1)), from the 3 x 3 block of heights around it (edges clamped). A tile with flag 8 is flat at its
    // own height. Otherwise each corner is the mean of the four tiles sharing it, or, for a tile with flag 0x10,
    // the mean of those that also have flag 0x10, summed in the vertex itself one tile at a time.
    void Map_UpdateTileGeometry(GuestPtr<CityMap> map, int x, int z)
    {
        x = clampToMap(x);
        z = clampToMap(z);
        MapTile &tile = tileAt(map, x, z);
        if ((tile.flags & 8u) != 0)
        {
            for (int v = 0; v < 4; ++v)
                tile.verts[v] = tile.height;
            return;
        }
        ps2f height[9];
        bool flagged[9];
        for (int r = 0; r < 3; ++r)
        {
            const int row = x + clampToMap(z + r - 1) * kMapSize;
            const int cells[3] = {row - (x != 0 ? 1 : 0), row, row + (x != kMapSize - 1 ? 1 : 0)};
            for (int c = 0; c < 3; ++c)
            {
                height[3 * r + c] = map->tiles[cells[c]].height;
                flagged[3 * r + c] = (map->tiles[cells[c]].flags & 0x10u) != 0;
            }
        }
        if ((tile.flags & 0x10u) == 0)
        {
            const ps2f four = 4.0f;
            tile.verts[0] = (height[4] + (height[3] + (height[0] + height[1]))) / four;
            tile.verts[1] = (height[5] + (height[4] + (height[1] + height[2]))) / four;
            tile.verts[2] = (height[8] + (height[7] + (height[4] + height[5]))) / four;
            tile.verts[3] = (height[7] + (height[6] + (height[3] + height[4]))) / four;
            return;
        }
        static constexpr int kCorner[4][4] = {{0, 1, 3, 4}, {1, 2, 4, 5}, {4, 5, 7, 8}, {3, 4, 6, 7}};
        for (int v = 0; v < 4; ++v)
            tile.verts[v] = 0.0f;
        for (int v = 0; v < 4; ++v)
        {
            ps2f count = 0.0f;
            for (const int i : kCorner[v])
                if (flagged[i])
                {
                    count = count + ps2f(1.0f);
                    tile.verts[v] = tile.verts[v] + height[i];
                }
            tile.verts[v] = tile.verts[v] / count;
        }
    }

    // Places a new `type` object for `owner` at (x, z): the next free object record (the search wraps from 0x3F80
    // to 1 and gives record 0 after 0x3F81 tries) is filled (MapObj_Init) and covers the size x size block. Each
    // tile's old object is deleted, the tile points at the new one, keeps only flag 0x20 and gains 8 (type flag 1)
    // or 0x10 (type flag 2). The block is levelled to the mean over its tiles of each one's average neighbour
    // height (the eight around it that are on the map), and the corner tile gets flag 1. Returns the record.
    int Map_CreateObject(GuestPtr<CityMap> map, int owner, int type, int x, int z)
    {
        const int corner = x + z * kMapSize;
        const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(static_cast<uint32_t>(type))};
        const int size = btype->size;
        int object;
        for (int tries = 0;;)
        {
            object = map->nextId;
            if (map->objects[object].type == 0)
                break;
            ++tries;
            map->nextId = map->nextId + 1;
            if (tries >= 0x3F81)
            {
                object = 0;
                break;
            }
            if (map->nextId >= 0x3F80)
                map->nextId = 1;
        }
        MapObj_Init(map, object, type, static_cast<uint16_t>(owner), static_cast<uint16_t>(corner));
        const int xEnd = x + size, zEnd = z + size;
        for (int tz = z; tz < zEnd; ++tz)
            for (int tx = x; tx < xEnd; ++tx)
            {
                MapTile &tile = map->tiles[tx + tz * kMapSize];
                MapObj_Delete(map, tile.firstObject);
                tile.firstObject = static_cast<uint16_t>(object);
                tile.flags &= 0x20u;
                if ((btype->flags & 1u) != 0)
                    tile.flags |= 8u;
                else if ((btype->flags & 2u) != 0)
                    tile.flags |= 0x10u;
            }
        ps2f total = 0.0f;
        for (int tz = z; tz < zEnd; ++tz)
            for (int tx = x; tx < xEnd; ++tx)
            {
                ps2f sum = 0.0f, count = 0.0f;
                for (uint32_t k = 0; k < 8; ++k)
                {
                    const int nz = tz + port::load<int32_t>(globals::kRingStepZ + 4u * k);
                    if (nz < 0 || nz >= kMapSize)
                        continue;
                    const int nx = tx + port::load<int32_t>(globals::kRingStepX + 4u * k);
                    if (nx < 0 || nx >= kMapSize)
                        continue;
                    count = count + ps2f(1.0f);
                    sum = sum + map->tiles[nx + nz * kMapSize].height;
                }
                total = total + sum / count;
            }
        total = total / fromInt(size * size);
        for (int tz = z; tz < zEnd; ++tz)
            for (int tx = x; tx < xEnd; ++tx)
                map->tiles[tx + tz * kMapSize].height = total;
        map->tiles[corner].flags |= 1u;
        return object;
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

    namespace
    {
        // One side of a terrain tile with corners a and b: where the neighbouring tile's corners on that side stand
        // higher (heightA, heightB), up to two triangles close the gap into the cliff-side buffer (0x86E vertices).
        // The w words carry draw data: the first vertex a texture entry (0x2698 with the base at 0x1BD990), the
        // second 0x21 or 0x24 (+6 on odd rows or columns), the third 1. `raisedFirst` (the +z side) starts each
        // triangle with the raised corner instead of the tile's own.
        void terrainSide(GuestPtr<CityMap> map, GuestPtr<Vec4> a, ps2f heightA, GuestPtr<Vec4> b, ps2f heightB, uint32_t odd,
                         bool raisedFirst)
        {
            const auto raise = [](GuestPtr<Vec4> corner, ps2f height) {
                Vec4 v = *corner;
                v.y = static_cast<float>(height);
                v.w = 1.0f;
                return v;
            };
            const auto triangle = [&](const Vec4 &v0, const Vec4 &v1, uint32_t w1, const Vec4 &v2) {
                if (map->sideVertices >= 0x86E)
                    return;
                const uint32_t w[3] = {port::load<uint32_t>(0x001BD990u) | 0x2698u, w1, 1u};
                const Vec4 *v[3] = {&v0, &v1, &v2};
                for (int i = 0; i < 3; ++i)
                {
                    const GuestPtr<Vec4> dst{map->sideWrite.address};
                    *dst = *v[i];
                    port::store<uint32_t>(dst.address + 0xCu, w[i]);
                    map->sideWrite.address += 16u;
                }
                map->sideVertices += 3;
            };
            const uint32_t near = odd * 6 + 0x21, far = odd * 6 + 0x24;
            if (!(heightA <= a->y))
            {
                const Vec4 upA = raise(a, heightA);
                if (raisedFirst)
                    triangle(upA, *a, far, *b);
                else
                    triangle(*a, upA, far, *b);
                if (!(heightB <= b->y))
                {
                    const Vec4 upB = raise(b, heightB);
                    if (raisedFirst)
                        triangle(upB, upA, near, *b);
                    else
                        triangle(*b, upA, near, upB);
                }
            }
            else if (!(heightB <= b->y))
            {
                const Vec4 upB = raise(b, heightB);
                if (raisedFirst)
                    triangle(upB, *a, far, *b);
                else
                    triangle(*b, *a, near, upB);
            }
        }
    }

    // The cliff sides of terrain tile (x, z), whose corners are corner0 (-x,-z), corner1 (+x,-z), corner2 (+x,+z)
    // and corner3 (-x,+z): towards -z, +z, -x and +x in turn, against the neighbouring tile's facing corners (the
    // tile itself at the map's edge).
    void Terrain_BuildStrip(GuestPtr<CityMap> map, int x, int z, GuestPtr<Vec4> corner0, GuestPtr<Vec4> corner1,
                            GuestPtr<Vec4> corner2, GuestPtr<Vec4> corner3)
    {
        const auto tile = [&](int tx, int tz) -> const MapTile & { return map->tiles[tx + tz * 128]; };
        const uint32_t oddX = static_cast<uint32_t>(x) & 1u, oddZ = static_cast<uint32_t>(z) & 1u;
        const MapTile &back = tile(x, z < 1 ? 0 : z - 1);
        terrainSide(map, corner0, back.verts[3], corner1, back.verts[2], oddX, false);
        const MapTile &front = tile(x, z > 0x7E ? 0x7F : z + 1);
        terrainSide(map, corner3, front.verts[0], corner2, front.verts[1], oddX, true);
        const MapTile &left = tile(x < 1 ? 0 : x - 1, z);
        terrainSide(map, corner3, left.verts[2], corner0, left.verts[1], oddZ, false);
        const MapTile &right = tile(x < 0x7F ? x + 1 : 0x7F, z);
        terrainSide(map, corner1, right.verts[0], corner2, right.verts[3], oddZ, false);
    }

    // One terrain tile's quad around (centreX, centreZ): its corners take the tile's corner heights. With no
    // object (outside the map) the nearest tile's are used, flattened along the edge it lies beyond. The draw
    // flags (t3 of mm_Terrain_AddQuad) start at 8: +1 when the object's type gives a texture (vtable +0x1C,
    // with the object; passed on as the texture), 2 when a corner rises above `level`, 0x20 or 0x40 (tile flag 0x20 or not) when a
    // corner is above 0 on a type without flag 1, 4 inside the buildings layer's tile rectangle, 0x10 when the
    // tile and its first object both have flag 0x10. A tile with an object also gets its cliff sides.
    void Terrain_DrawPatch(GuestPtr<CityMap> map, int x, int z, int object, ps2f centreX, ps2f centreZ, ps2f level)
    {
        port::GuestFrame<0xE0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<Vec4> a{sp + 0xA0u}, b{sp + 0xB0u}, c{sp + 0xC0u}, d{sp + 0xD0u};
        const ps2f half = 0.5f;
        const ps2f left = centreX - half, right = half + centreX, near = half + centreZ, far = centreZ - half;
        *a = Vec4{static_cast<float>(left), 0.0f, static_cast<float>(near), 1.0f};
        *b = Vec4{static_cast<float>(right), 0.0f, static_cast<float>(near), 1.0f};
        *c = Vec4{static_cast<float>(right), 0.0f, static_cast<float>(far), 1.0f};
        *d = Vec4{static_cast<float>(left), 0.0f, static_cast<float>(far), 1.0f};
        int tile;
        if (object == 0)
        {
            tile = std::clamp(x, 0, 0x7F) + std::clamp(z, 0, 0x7F) * 128;
            const MapTile &t = map->tiles[tile];
            a->y = t.verts[0];
            b->y = t.verts[1];
            c->y = t.verts[2];
            d->y = t.verts[3];
            if (x < 0)
            {
                b->y = a->y;
                c->y = d->y;
            }
            else if (x >= 0x80)
            {
                a->y = b->y;
                d->y = c->y;
            }
            if (z < 0)
            {
                d->y = a->y;
                c->y = b->y;
            }
            else if (z >= 0x80)
            {
                a->y = d->y;
                b->y = c->y;
            }
        }
        else
        {
            tile = x + z * 128;
            const MapTile &t = map->tiles[tile];
            a->y = t.verts[0];
            b->y = t.verts[1];
            c->y = t.verts[2];
            d->y = t.verts[3];
        }

        const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(map->objects[object].type)};
        const int64_t texture = port::callGuest<int64_t>(port::load<uint32_t>(btype->vtable.address + 0x1Cu), btype, map->objects + object);
        uint32_t flags = texture != 0 ? 9u : 8u;
        const ps2f heights[4] = {a->y, b->y, c->y, d->y};
        for (const ps2f h : heights)
            if (level < h)
            {
                flags |= 2u;
                break;
            }
        if ((btype->flags & 1u) == 0)
            for (const ps2f h : heights)
                if (ps2f(0.0f) < h)
                {
                    flags |= (map->tiles[tile].flags & 0x20u) != 0 ? 0x20u : 0x40u;
                    break;
                }
        if (LayerC_InTileRect(map->buildings, x, z) != 0)
            flags |= 4u;
        if ((map->tiles[tile].flags & 0x10u) != 0 && (map->objects[map->tiles[tile].firstObject].flags & 0x10u) != 0)
            flags |= 0x10u;
        Terrain_AddQuad(map, a, b, c, d, static_cast<int>(texture), (z & 1) + (x & 1) * 2, static_cast<int>(flags));
        if (object != 0)
            Terrain_BuildStrip(map, x, z, a, b, c, d);
    }

    // Whether a ray hits the first object on `tile` (record 0 off the map): the object's type tests it (vtable
    // +0x30, passed a2, a3, arg and the object's matrix: the building matrix of its terrain kind with the
    // footprint's middle as translation, y 0). 1 or 0.
    int MapObj_RayHit(GuestPtr<CityMap> map, uint32_t tile, int a2, int a3, int arg)
    {
        port::GuestFrame<0x90> frame;
        const GuestPtr<Vec4> matrix{frame.ptr().address + 0x50u};
        const int tx = static_cast<int>(tile & 0x7Fu), tz = static_cast<int>((tile & 0xFFFFu) >> 7);
        const GuestPtr<MapObject> obj = tz < 0x80 && tx < 0x80 ? map->objects + map->tiles[tx + tz * 128].firstObject : map->objects;
        const int kind = Map_GetTileKind(map, obj->tile);
        for (int row = 0; row < 4; ++row)
            matrix[row] = GuestPtr<Vec4>{globals::kBuildingMatrices + 0x40u * static_cast<uint32_t>(kind)}[row];
        const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(obj->type)};
        const ps2f half = fromInt(btype->size) / ps2f(2.0f);
        matrix[3].x = static_cast<float>((fromInt(obj->tile & 0x7F) - ps2f(64.0f)) + half);
        port::store<uint32_t>(matrix.address + 0x34u, 0);
        matrix[3].z = static_cast<float>((fromInt(128 - (obj->tile >> 7)) - ps2f(64.0f)) - half);
        const uint32_t test = port::load<uint32_t>(btype->vtable.address + 0x30u);
        return port::callGuest<int64_t>(test, btype, obj, a2, a3, matrix, arg) != 0 ? 1 : 0;
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
    PORT_FN(MapObj_RayHit);
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
    PORT_FN(MapObj_Init);
    PORT_FN(Map_CreateObject);
    PORT_FN(Map_UpdateTileGeometry);
    // Terrain generation: how far, in tiles, the nearest land (a tile above height 0) lies from tile (x, z) along the four
    // axes, up to 15 (100 when there is none; the distance counts the steps all four directions were clear for).
    ps2f TerrainGen_SampleSmooth(GuestPtr<CityMap> map, int x, int z)
    {
        const auto land = [&](int tx, int tz) {
            if (tx < 0 || tx >= kMapSize || tz < 0 || tz >= kMapSize)
                return false;
            return !(tileAt(map, tx, tz).height <= ps2f(0.0f));
        };
        ps2f steps(0.0f);
        for (int d = 0; d < 16; ++d)
        {
            if (land(x + d, z) || land(x - d, z) || land(x, z + d) || land(x, z - d))
                return steps;
            steps = steps + ps2f(1.0f);
        }
        return ps2f(100.0f);
    }

    // Terrain generation: pulls the tiles within `radius` of tile (x, z)'s centre (the tiles in a window `reach` + 4 wide) down
    // towards the sea: a tile with land `s` steps away (SampleSmooth, above 0) takes strength * (radius - distance) /
    // radius, no lower than -0.022 * s * s and than `floor`, when that is below its height. Distances on VU0, as the
    // scatter; the original's frame is reserved whole and its points kept at their offsets.
    void TerrainGen_Smooth(GuestPtr<CityMap> map, int cx, int cz, ps2f strength, ps2f radius, ps2f floorHeight)
    {
        using namespace vu0;
        port::GuestFrame<0x90> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<Vec4> centre{sp + 0x50u}, point{sp + 0x60u}, distance{sp + 0x80u};
        const ps2f half = 0.5f, middle = 64.0f;
        const auto place = [&](GuestPtr<Vec4> at, int tx, int tz) {
            at->x = half + (fromInt(tx) - middle);
            at->y = 0.0f;
            at->z = (fromInt(kMapSize - tz) - middle) - half;
            at->w = 1.0f;
        };
        place(centre, cx, cz);
        const int reach = sdk::fptosi(ps2f(1.0f) + radius);
        const int x0 = std::max(cx - reach - 4, 0);
        const int z0 = std::max(cz - reach - 4, 0);
        const int x1 = cx + reach + 4 < kMapSize ? cx + reach + 4 : kMapSize;
        const int z1 = cz + reach + 4 < kMapSize ? cz + reach + 4 : kMapSize;
        for (int z = z0; z < z1; ++z)
        {
            if (z < 0 || z >= kMapSize)
                continue;
            for (int x = x0; x < x1; ++x)
            {
                if (x < 0 || x >= kMapSize)
                    continue;
                place(point, x, z);
                vf(4) = load(centre);
                vf(5) = load(point);
                set(vf(6), sub(vf(5), vf(4)), XYZW);
                set(vf(7), mul(vf(6), vf(6)), XYZ);
                set(vf(8), add(vf(7), lane(vf(7), 1)), X);
                set(vf(8), add(vf(8), lane(vf(7), 2)), X);
                q() = sqrtQ(vu0::x(vf(8)));
                set(vf(9), add(vf(0), _mm_set1_ps(q())), XYZW);
                store(distance, vf(9));
                const ps2f d = distance->x;
                if (!(d < radius))
                    continue;
                const ps2f land = TerrainGen_SampleSmooth(map, x, z);
                if (land <= ps2f(0.0f))
                    continue;
                const ps2f squared = land * land;
                ps2f pull = (strength * (radius - d)) / radius;
                const ps2f cap = -(ps2f(std::bit_cast<float>(0x3CB43958u)) * squared);
                if (pull <= cap)
                    pull = cap;
                if (pull <= floorHeight)
                    pull = floorHeight;
                if (pull < tileAt(map, x, z).height)
                    tileAt(map, x, z).height = pull;
            }
        }
    }

    // Terrain generation: a round bump of strength `height` and reach `radius` at tile (x, z)'s centre. Each tile within the
    // radius takes a pull: the height scaled by the fraction of the radius left (at most 3.2), or for mode 3 the fourth power
    // of that fraction cut to 0..1 after scaling by 4, and from then on, for every later tile too, the mode is 0. When the pull is larger in size than the
    // tile's height (soft doubles), mode 0 averages the two, 1 takes the smaller, 2 the larger, other modes leave the tile;
    // every tile is then held to 3.2 at most, and with flag 0x20 a tile above sea level gets its tile flag 0x20. (The
    // original leaves a float in f0, which its callers do not read.)
    void TerrainGen_ApplyBump(GuestPtr<CityMap> map, int x, int z, int mode, uint32_t flags, ps2f height, ps2f radius)
    {
        using namespace vu0;
        port::GuestFrame<0xD0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<Vec4> centre{sp + 0x90u}, point{sp + 0xA0u}, distance{sp + 0xC0u};
        const ps2f half = 0.5f, middle = 64.0f;
        const auto place = [&](GuestPtr<Vec4> at, int tx, int tz) {
            at->x = half + (fromInt(tx) - middle);
            at->y = 0.0f;
            at->z = (fromInt(kMapSize - tz) - middle) - half;
            at->w = 1.0f;
        };
        place(centre, x, z);
        const int reach = sdk::fptosi(ps2f(1.0f) + radius);
        const int x0 = std::max(x - reach - 4, 0);
        const int z0 = std::max(z - reach - 4, 0);
        const int x1 = x + reach + 4 < kMapSize ? x + reach + 4 : kMapSize;
        const int z1 = z + reach + 4 < kMapSize ? z + reach + 4 : kMapSize;
        const ps2f cap(std::bit_cast<float>(0x404CCCCDu));
        for (int tz = z0; tz < z1; ++tz)
        {
            if (tz < 0 || tz >= kMapSize)
                continue;
            for (int tx = x0; tx < x1; ++tx)
            {
                if (tx < 0 || tx >= kMapSize)
                    continue;
                place(point, tx, tz);
                vf(4) = load(centre);
                vf(5) = load(point);
                set(vf(6), sub(vf(5), vf(4)), XYZW);
                set(vf(7), mul(vf(6), vf(6)), XYZ);
                set(vf(8), add(vf(7), lane(vf(7), 1)), X);
                set(vf(8), add(vf(8), lane(vf(7), 2)), X);
                q() = sqrtQ(vu0::x(vf(8)));
                set(vf(9), add(vf(0), _mm_set1_ps(q())), XYZW);
                store(distance, vf(9));
                const ps2f d = distance->x;
                if (!(d < radius))
                    continue;
                ps2f pull;
                if (mode != 3)
                {
                    pull = (height * (radius - d)) / radius;
                    if (!(pull <= cap))
                        pull = cap;
                }
                else
                {
                    ps2f t = ps2f(1.0f) - d / radius;
                    if (ps2f(4.0f) * t < ps2f(0.0f))
                        t = ps2f(0.0f);
                    else if (ps2f(4.0f) * t <= ps2f(1.0f))
                        t = ps2f(4.0f) * t;
                    else
                        t = ps2f(1.0f);
                    t = t * t;
                    mode = 0; // the original's register: every tile after the first one takes the plain formula and the average
                    pull = height * (t * t);
                }
                MapTile &tile = tileAt(map, tx, tz);
                const uint64_t tileSize = sdk::fabs(sdk::fptodp(tile.height));
                const uint64_t pullSize = sdk::fabs(sdk::fptodp(pull));
                if (sdk::fpLtdf2(tileSize, pullSize) == 0)
                    continue;
                if (mode == 2)
                    tile.height = pull <= tile.height ? tile.height : pull;
                else if (mode == 1)
                    tile.height = pull < tile.height ? pull : tile.height;
                else if (mode == 0)
                    tile.height = (pull + tile.height) / ps2f(2.0f);
                if (!(tile.height <= cap))
                    tile.height = cap;
                if ((flags & 0x20u) != 0 && !(tile.height <= ps2f(0.0f)))
                    tile.flags = static_cast<uint16_t>(tile.flags | 0x20u);
            }
        }
    }

    // Terrain generation: a hill on the 16-tile block (cx, cz): a bump of height 5 and reach 9 at its middle, then a ring of
    // radius 7 walked as a midpoint circle (x from 7 down, y from 0 up) with a size-and-position-jittered bump (mode 2) on
    // each of the eight mirror points of every step. The jitter is up to 2 tiles either way and the size wobbles by up to 5
    // (the first bump of a step) or 3 (the rest); `high` sets tile flag 0x20 on the land the bumps raise. The original
    // returns whatever its last bump left in v0, which its one caller (Generate) never reads.
    void TerrainGen_Hills(GuestPtr<CityMap> map, int cx, int cz, int high)
    {
        port::GuestFrame<0x110> frame;
        const int centreX = (cx << 4) + 8, centreZ = (cz << 4) + 8;
        const uint32_t flags = high != 0 ? 0x20u : 0u;
        const ps2f wobbleUnit(std::bit_cast<float>(0x38D1B717u)), shrink(std::bit_cast<float>(0x3E99999Au)), base(5.0f);
        const auto draw = [] { return static_cast<int32_t>(Random_Next()); };
        const auto jitter = [&] { return draw() % 4 - 2; };
        // The draws come in this order: the size, the x jitter, the z jitter.
        const auto bump = [&](int modulus, int offsetX, int offsetZ) {
            const ps2f wobble = wobbleUnit * fromInt(draw() % modulus);
            const int dx = jitter();
            const int dz = jitter();
            const ps2f radius = base + wobble;
            TerrainGen_ApplyBump(map, dx + (centreX + offsetX), dz + (centreZ + offsetZ), 2, flags, shrink * radius, radius);
        };
        TerrainGen_ApplyBump(map, centreX, centreZ, 0, flags, base, ps2f(9.0f));
        int x = 7, y = 0, error = 7;
        do
        {
            bump(0xC350, x, y);
            bump(0x7530, x, -y);
            bump(0x7530, -x, y);
            bump(0x7530, -x, -y);
            bump(0x7530, y, x);
            bump(0x7530, y, -x);
            bump(0x7530, -y, x);
            bump(0x7530, -y, -x);
            error -= 2 * y - 1;
            ++y;
            if (error < 0)
            {
                error += 2 * (x - 1);
                --x;
            }
        } while (x >= y);
    }

    // Terrain generation: one river. It starts on the map's edge at a point set by `start` and walks in steps. Each step
    // moves the position by (0, 0, 1.5 + width) turned by two Y rotations, one swaying with sin(phase) and one turning with
    // cos(heading), and drops a mode 3 bump of reach 1.5 + width + |cos(phase)| (and 1.4 times that for height, at least 3.2)
    // at the tile the position falls on. `edge` says how the walk's axes lie on the map (0 to 3), `direction` where the
    // heading starts (0, 90, 180, -90 degrees). The first bump waits for a tile at or below height 1; later bumps stop the
    // river when they land on tiles above 1.6, and so does leaving the map by more than four tiles.
    // curveA, curveB, wobbleA and wobbleB are the river's shape (the turning rate and the sway). The original's frame is kept
    // for its two matrices and the position vectors.
    void TerrainGen_River(GuestPtr<CityMap> map, int edge, int start, int width, int curveA, int curveB, int direction,
                          int wobbleA, int wobbleB)
    {
        using namespace vu0;
        port::GuestFrame<0x190> frame;
        const uint32_t sp = frame.ptr().address;
        const auto F = [](uint32_t bits) { return ps2f(std::bit_cast<float>(bits)); };
        const GuestPtr<Vec4> position{sp + 0x160u}, step{sp + 0x170u}, moved{sp + 0x180u};
        const GuestPtr<Vec4> turnA{sp + 0xE0u}, turnB{sp + 0x120u};
        const ps2f widthF = fromInt(width);
        position->x = F(0x3F000000u) + (fromInt(start << 5) - F(0x42800000u));
        position->y = 0.0f;
        position->z = F(0xC28A0000u);
        position->w = F(0x3F800000u);
        const ps2f three = F(0x40400000u);
        const ps2f curveB_F = fromInt(curveB), curveA_F = fromInt(curveA);
        // The soft doubles are the original's: the turning scale, the heading's step and the sway.
        const ps2f turnBase = sdk::dptofp(sdk::dpadd(0x3FDC28F5C28F5C29ull, sdk::fptodp(F(0x3D23D70Au) * (three - widthF))));
        const ps2f turnScale = curveB_F * turnBase;
        const ps2f headingStep = ((F(0x3CCCCCCDu) + F(0x3CB1C433u) * widthF) + curveA_F * (F(0x3C75C28Fu) + F(0x3C09A027u) * widthF)) +
                                 (three - curveB_F) * (F(0x3C54FDF4u) + F(0x3C1374BCu) * widthF);
        ps2f heading(0.0f);
        switch (direction)
        {
        case 3: heading = F(0xBFC90FDBu); break;
        case 2: heading = F(0x40490FDBu); break;
        case 1: heading = F(0x3FC90FDBu); break;
        case 0: heading = F(0u); break;
        }
        const ps2f wobbleF = fromInt(wobbleA);
        const ps2f lead = sdk::dptofp(sdk::dpadd(0x3FC999999999999Aull, sdk::fptodp(F(0x3D89374Cu) * widthF)));
        const ps2f phaseStep = lead + wobbleF * (F(0x3D0F5C29u) + F(0x3D3851ECu) * widthF);
        const uint64_t swayBase = sdk::dpmul(0x3FD5604189374BC7ull, sdk::fptodp(fromInt(wobbleB)));
        const ps2f swayScale = sdk::dptofp(sdk::dpadd(swayBase, sdk::fptodp(F(0x3DCCCCCDu) * wobbleF)));
        const ps2f reach = F(0x3FC00000u) + widthF;
        const ps2f pi = F(0x40490FDBu), twoPi = F(0x40C90FDBu), mapHalf = F(0x42800000u);
        ps2f phase(0.0f);
        bool first = true;
        int x = 0, z = 0; // kept between steps for an `edge` outside 0 to 3
        for (;;)
        {
            step->x = 0.0f;
            step->y = 0.0f;
            step->z = reach;
            step->w = 0.0f;
            sdk::sceVu0RotMatrixY(turnA.address, globals::kIdentityMatrix, swayScale * sdk::sinf(phase));
            sdk::sceVu0RotMatrixY(turnB.address, globals::kIdentityMatrix, turnScale * sdk::cosf(heading));
            applyMatrix(turnA, step, moved);
            applyMatrix(turnB, moved, moved);
            vf(4) = load(position);
            vf(5) = load(moved);
            set(vf(4), add(vf(4), vf(5)), XYZW);
            store(position, vf(4));
            const ps2f across = sdk::dptofp(sdk::fabs(sdk::fptodp(sdk::cosf(phase))));
            const int tileX = sdk::fptosi(mapHalf + position->x);
            const int tileZ = sdk::fptosi(mapHalf + position->z);
            switch (edge)
            {
            case 3: x = kMapSize - tileZ; z = tileX; break;
            case 2: x = tileX; z = tileZ; break;
            case 1: x = tileZ; z = tileX; break;
            case 0: x = tileX; z = kMapSize - tileZ; break;
            }
            const auto bump = [&] {
                const ps2f radius = reach + across;
                ps2f height = F(0x3FB33333u) * radius;
                if (height < F(0x404CCCCDu))
                    height = F(0x404CCCCDu);
                TerrainGen_ApplyBump(map, x, z, 3, 0, height, radius);
            };
            if (first)
            {
                if (onMap(x, z) && tileAt(map, x, z).height <= F(0x3F800000u))
                {
                    bump();
                    first = false;
                }
            }
            else
            {
                ps2f ground(0.0f);
                if (onMap(x, z))
                    ground = tileAt(map, x, z).height;
                bump();
                if (!(ground <= F(0x3FCCCCCDu)))
                    return;
            }
            if (tileX < -4 || tileX >= kMapSize + 4 || tileZ < -4 || tileZ >= kMapSize + 4)
                return;
            phase = phase + phaseStep;
            if (!(phase <= pi))
                phase = phase - twoPi;
            heading = heading + headingStep;
            if (!(heading <= pi))
                heading = heading - twoPi;
        }
    }

    // Terrain generation: builds the whole map from a 10 x 10 grid of cells (16 tiles each) behind a 4-byte river header
    // (`params`). A cell byte holds the terrain type in bits 0..2 (6 and 7: hills, 7 the high kind; 1 to 4: smoothed
    // towards the sea) and a plant style in bits 4..6. In turn: hills, the two rivers (header bytes 0..3), the smoothing, 16
    // plant spots per cell (style 0 none, 1 kind 2, 2 and 3 a kind drawn from a table, any other keeps the last kind) each
    // with a scatter of kind 2 and the drawn kind, the border objects (type 11) on the edge tiles, and every tile's geometry.
    void TerrainGen_Generate(GuestPtr<CityMap> map, GuestPtr<uint8_t> params)
    {
        port::GuestFrame<0x110> frame;
        const auto cell = [&](int row, int column) -> unsigned { return params[4 + row * 10 + column]; };
        for (int row = 8; row >= -1; --row)
            for (int column = 8; column >= -1; --column)
            {
                const unsigned type = cell(8 - row, 8 - column) & 7u;
                if (type == 7)
                    TerrainGen_Hills(map, column, row, 1);
                else if (type == 6)
                    TerrainGen_Hills(map, column, row, 0);
            }
        for (int i = 0; i < 2; ++i)
        {
            const unsigned b0 = params[2 * i], b1 = params[2 * i + 1];
            if ((b0 & 3u) != 0)
                TerrainGen_River(map, (b0 >> 2) & 3, b0 & 3, (b0 >> 4) & 3, (b0 >> 6) & 3, b1 & 3, (b1 >> 2) & 3,
                                 (b1 >> 4) & 3, b1 >> 6);
        }
        for (int row = 0; row < 10; ++row)
            for (int column = 0; column < 10; ++column)
            {
                const unsigned type = cell(row, column) & 7u;
                if (type == 0 || type >= 5)
                    continue;
                TerrainGen_Smooth(map, 0x88 - 16 * column, 0x88 - 16 * row,
                                  port::load<float>(globals::kSmoothStrength + 4 * type),
                                  port::load<float>(globals::kSmoothRadius + 4 * type),
                                  port::load<float>(globals::kSmoothFloor + 4 * type));
            }
        int kind = 0; // the original's register: styles 4 to 7 keep whatever was drawn last
        for (int row = 0; row < 10; ++row)
            for (int column = 0; column < 10; ++column)
            {
                const unsigned data = cell(row, column);
                if ((data & 7u) >= 5)
                    continue;
                const unsigned style = (data >> 4) & 7u;
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j)
                    {
                        if (style == 3)
                            kind = port::load<int32_t>(globals::kScatterKindsStyle3 + 4 * (static_cast<int32_t>(Random_Next()) % 16));
                        else if (style == 2)
                            kind = port::load<int32_t>(globals::kScatterKindsStyle2 + 4 * (static_cast<int32_t>(Random_Next()) % 16));
                        else if (style == 1)
                            kind = 2;
                        else if (style == 0)
                            kind = 0;
                        const int x = 0x80 - 16 * column + 4 * i + 2;
                        const int z = 0x80 - 16 * row + 4 * j + 2;
                        TerrainGen_ScatterObjects(map, x, z, 2, ps2f(4.0f));
                        TerrainGen_ScatterObjects(map, x, z, kind, ps2f(4.0f));
                    }
            }
        for (int i = 1, p = 0x10; i < 8; ++i, p += 0x10)
        {
            Map_CreateObject(map, 0, 0xB, 2, p);
            Map_CreateObject(map, 0, 0xB, 0x7D, p);
            Map_CreateObject(map, 0, 0xB, p, 2);
            Map_CreateObject(map, 0, 0xB, p, 0x7D);
        }
        for (int z = 0; z < kMapSize; ++z)
            for (int x = 0; x < kMapSize; ++x)
                Map_UpdateTileGeometry(map, x, z);
    }

    // Terrain drawing: the city's terrain patches around the camera, one frame. `zoom` is the camera step; steps 0 and 1 read
    // their projection and reach straight from the tables, the others blend between two table rows by the camera height
    // (`variant` picks the far-distance and height tables). Two projection matrices (the second 200 wider) are built, the
    // view direction moved to the camera's tile gives the window of tiles (reach tiles around it), and for every tile in the
    // window the distance to the camera (VU0, with the clip flags of the tile's centre against both projections) decides
    // whether the patch is drawn: it is linked into the distance bucket (a quarter-tile each, 150 buckets) and handed to
    // mm_Terrain_DrawPatch, until 600 tiles or the quad buffer are full. Tiles with an object that has flag 0x80 are skipped,
    // tiles with an object are centred on its footprint. The unused rest of both buffers gets its draw flag cleared.
    // The original's frame holds the matrices and vectors; vf1 to vf9 keep the two matrices and the camera through the
    // loop (the callees leave them alone), `extra` is passed on to the patch in a register it ignores.
    void Terrain_Draw(GuestPtr<CityMap> map, GuestPtr<Vec4> view, GuestPtr<Vec4> camera, int zoom, int variant, int extra,
                      ps2f level)
    {
        using namespace vu0;
        (void)extra;
        port::GuestFrame<0x1A0> frame;
        const uint32_t sp = frame.ptr().address;
        const auto F = [](uint32_t bits) { return ps2f(std::bit_cast<float>(bits)); };
        const uint32_t farTable = variant == 0 ? globals::kTerrainFarA : globals::kTerrainFarB;
        const uint32_t heights = variant == 0 ? globals::kTerrainZoomHeightsA : globals::kTerrainZoomHeightsB;
        const GuestPtr<Vec4> screen{sp + 0x80u}, offset{sp + 0x90u}, distance{sp + 0xA0u};
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
            // The first step whose height the camera has reached; the blend runs from there to the next.
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
        Math_ProjectionMatrix(GuestPtr<ps2f>{projection.address}, GuestPtr<ps2f>{screen.address}, F(0x44400000u), F(0x3DCCCCCDu), farDistance);
        sdk::sceVu0MulMatrix(projection.address, projection.address, view.address);
        screen->x = ps2f(screen->x) + F(0x43480000u);
        Math_ProjectionMatrix(GuestPtr<ps2f>{projectionWide.address}, GuestPtr<ps2f>{screen.address}, F(0x44400000u), F(0x3DCCCCCDu), farDistance);
        sdk::sceVu0MulMatrix(projectionWide.address, projectionWide.address, view.address);
        // The view's rotation, inverted, turns the offset to where the camera looks from.
        std::memcpy(inverse.get(), view.get(), 3 * sizeof(Vec4));
        store(GuestPtr<Vec4>{inverse.address + 0x30u}, vf(0));
        sdk::sceVu0InversMatrix(inverse.address, inverse.address);
        applyMatrix(inverse, offset, offset);
        offset->x = ps2f(offset->x) + ps2f(camera->x);
        offset->z = ps2f(offset->z) + ps2f(camera->z);
        const ps2f mapHalf = F(0x42800000u), half = F(0x3F000000u), one = F(0x3F800000u);
        const int tileX = sdk::fptosi(mapHalf + ps2f(offset->x));
        const int tileZ = sdk::fptosi(mapHalf - ps2f(offset->z));
        const int left = tileX - reach, top = tileZ - reach, right = tileX + reach, bottom = tileZ + reach;
        const ps2f startX = (fromInt(left) - mapHalf) - half;
        // The distance buckets: a chain of 150 empty nodes.
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
        const uint32_t parity = port::load<uint32_t>(globals::kFrameParity) & 1u;
        map->terrainWrite = map->terrainBuffers[parity];
        map->sideVertices = 0;
        map->sideWrite = map->sideBuffers[parity];
        vf(1) = load(projection);
        vf(2) = load(GuestPtr<Vec4>{projection.address + 0x10u});
        vf(3) = load(GuestPtr<Vec4>{projection.address + 0x20u});
        vf(4) = load(GuestPtr<Vec4>{projection.address + 0x30u});
        vf(5) = load(projectionWide);
        vf(6) = load(GuestPtr<Vec4>{projectionWide.address + 0x10u});
        vf(7) = load(GuestPtr<Vec4>{projectionWide.address + 0x20u});
        vf(8) = load(GuestPtr<Vec4>{projectionWide.address + 0x30u});
        vf(9) = load(camera);
        here->z = half + (fromInt(0x80 - top) - mapHalf);
        here->w = one;
        bool full = false;
        for (int row = top; row < bottom && !full; ++row)
        {
            here->z = ps2f(here->z) - one;
            here->x = startX;
            for (int col = left; col < right; ++col)
            {
                here->x = ps2f(here->x) + one;
                const ps2f height = Map_GetHeightAt(map, col, row);
                here->y = height;
                uint32_t object = 0;
                if (onMap(col, row))
                {
                    object = tileAt(map, col, row).firstObject;
                    const GuestPtr<MapObject> placed = map->objects + static_cast<int>(object);
                    const GuestPtr<BuildingType> type{
                        port::load<uint32_t>(globals::buildingTypeTable.get() + 4u * static_cast<uint32_t>(placed->type))};
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
                store(distance, vf(10));
                const ps2f dist = distance->x;
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
                if (!draw)
                    continue;
                const int entry = map->sortedCount;
                TerrainEntry &node = map->sorted[entry];
                node.object = object;
                node.distance = dist;
                int bucket = sdk::fptosi(F(0x40800000u) * dist);
                bucket = bucket < 0 ? 0 : bucket >= 150 ? 149 : bucket;
                TerrainNode &head = map->buckets[bucket];
                const uint32_t at = GuestPtr<TerrainEntry>::of(node).address;
                node.next = head.next;
                head.next = at;
                node.prev = head.prev;
                head.prev = at;
                Terrain_DrawPatch(map, col, row, static_cast<int>(object), here->x, here->z, level);
                map->sortedCount = entry + 1;
                if (map->sortedCount >= 600 || map->terrainQuads >= 0x9C0)
                {
                    full = true;
                    break;
                }
            }
        }
        // What the frame did not fill keeps its draw flag cleared.
        for (int quads = map->terrainQuads; quads < 0x9C0; quads += 4)
        {
            port::store<uint32_t>(map->terrainWrite.address + 0x2Cu, 0u);
            map->terrainWrite = GuestPtr<void>{map->terrainWrite.address + 0x40u};
        }
        for (int sides = map->sideVertices; sides < 0x870; sides += 3)
        {
            port::store<uint32_t>(map->sideWrite.address + 0x2Cu, 0u);
            map->sideWrite = GuestPtr<void>{map->sideWrite.address + 0x30u};
        }
    }

    PORT_FN(TerrainGen_ApplyBump);
    PORT_FN(Terrain_Draw);
    PORT_FN(TerrainGen_River);
    PORT_FN(TerrainGen_Generate);
    PORT_FN(TerrainGen_Hills);
    PORT_FN(TerrainGen_SampleSmooth);
    PORT_FN(TerrainGen_Smooth);
    PORT_FN(TerrainGen_ScatterObjects);
    PORT_FN(MapObj_Delete);
    PORT_FN(People_UnlinkFromList);
    PORT_FN(Map_AppendObjectToTile);
    PORT_FN(MapObj_FindFreeSlot);
    PORT_FN(Map_PlaceObject);
    PORT_FN(Placement_CheckSlope);
    PORT_FN(Map_CheckPlacement);
    PORT_FN(Map_RemoveObjectFromTile);
    PORT_FN(MapPeople_RemoveForObject);
    PORT_FN(CityMap_ClearObject);
    PORT_FN(Map_UpdateAreaGeometry);
    PORT_FN(Terrain_AddQuad);
    PORT_FN(Terrain_BuildStrip);
    PORT_FN(Terrain_DrawPatch);

    // ---- areas, people lookups, history, saving (batch 38)

    // Whether type `typeIndex` can be placed at (x, z) (mm_Map_CheckPlacement, its rectangle in this frame).
    int Map_CanPlace(GuestPtr<CityMap> map, int, int typeIndex, int x, int z)
    {
        port::GuestFrame<0x20> frame;
        return Map_CheckPlacement(map, static_cast<uint32_t>(typeIndex), x, z, GuestPtr<int32_t>{frame.ptr().address + 0x10u});
    }

    // The resident (a class without flag 1) of the family and member, or 0.
    GuestPtr<MapPerson> Map_FindPersonEntry(GuestPtr<CityMap> map, int16_t family, uint16_t member)
    {
        for (int i = 0; i < kNoPerson; ++i)
        {
            const GuestPtr<MapPerson> person = map->people + i;
            if (person->charType == 0)
                continue;
            const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
            if ((cls->flags & 1u) == 0 && person->family == static_cast<uint16_t>(family) && person->member == member)
                return person;
        }
        return GuestPtr<MapPerson>{0};
    }

    // The first non-resident (a class with flag 1, a vehicle) heading for `home`.
    GuestPtr<MapPerson> Map_FindPersonOfFlaggedType(GuestPtr<CityMap> map, int16_t home)
    {
        const uint32_t end = map->people.address + 6000u * 0x30u;
        for (uint32_t p = map->people.address; p < end; p += 0x30u)
        {
            const GuestPtr<MapPerson> person{p};
            if (person->charType == 0)
                continue;
            const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
            if ((cls->flags & 1u) != 0 && person->target == static_cast<uint16_t>(home))
                return person;
        }
        return GuestPtr<MapPerson>{0};
    }

    namespace
    {
        // The top object of each tile of a size x size area (the last in the tile list) gets or loses flag 1.
        void markArea(GuestPtr<CityMap> map, int x, int z, int size, bool on)
        {
            for (int dz = 0; dz < size; ++dz)
            {
                for (int dx = 0; dx < size; ++dx)
                {
                    uint16_t object = map->tiles[(z + dz) * 128 + x + dx].firstObject;
                    while (map->objects[object].nextOnTile != kNoObject)
                        object = map->objects[object].nextOnTile;
                    if (on)
                        map->objects[object].flags |= 1u;
                    else
                        map->objects[object].flags &= 0xFFFEu;
                }
            }
        }

        // Places object type `type` on every tile of a size x size area.
        void fillArea(GuestPtr<CityMap> map, int x, int z, int size, int type, int arg)
        {
            for (int dz = 0; dz < size; ++dz)
                for (int dx = 0; dx < size; ++dx)
                    Map_PlaceObject(map, 0, type, x + dx, z + dz, arg);
        }
    }

    void Map_HighlightArea(GuestPtr<CityMap> map, int x, int z, int size) { markArea(map, x, z, size, true); }
    void Map_UnhighlightArea(GuestPtr<CityMap> map, int x, int z, int size) { markArea(map, x, z, size, false); }
    void Map_LevelArea(GuestPtr<CityMap> map, int x, int z, int size, int arg) { fillArea(map, x, z, size, 3, arg); }
    void Map_PlaceRubbleArea(GuestPtr<CityMap> map, int x, int z, int size, int arg) { fillArea(map, x, z, size, 0xA, arg); }

    // Places small goods of `kind` (types 0x115 on) and gives the new object its count (byte +0x10).
    void Map_PlaceSmallGoods(GuestPtr<CityMap> map, int x, int z, int a3, uint8_t value, int kind)
    {
        const int object = Map_PlaceObject(map, 0, kind + 0x115, x, z, a3);
        if (object != kNoObject)
            port::store<uint8_t>((map->objects + object).address + 0x10u, value);
    }

    void Map_SaveState(GuestPtr<CityMap> map, GuestPtr<McManager> mc)
    {
        SaveFile_Write(mc, static_cast<int>(map->packedTiles.address), 0x18000);
        SaveFile_Write(mc, static_cast<int>(map.address + 0x10u), 0x20);
        SaveFile_Write(mc, static_cast<int>(map->objects.address), 0x6F200);
        TileLists_SaveState(map->tileLists, mc);
        SaveFile_Write(mc, static_cast<int>(map->people.address), 0x46500);
        SaveFile_Write(mc, static_cast<int>(map.address + 0x6Cu), 0xC);
    }

    // The object matrix: the building matrix of the tile kind, moved to the footprint centre on the terrain.
    void MapObj_GetMatrix(GuestPtr<CityMap> map, int object, GuestPtr<Vec4> matrix)
    {
        const GuestPtr<MapObject> obj = map->objects + object;
        const int kind = Map_GetTileKind(map, obj->tile);
        for (uint32_t i = 0; i < 0x40u; i += 4u)
            port::store<uint32_t>(matrix.address + i, port::load<uint32_t>(globals::kBuildingMatrices + 0x40u * static_cast<uint32_t>(kind) + i));
        const int x = obj->tile & 0x7F, z = obj->tile >> 7;
        const ps2f half = fromInt(GuestPtr<BuildingType>{globals::buildingTypeAddress(obj->type)}->size) / ps2f(2.0f);
        matrix[3].x = fromInt(x) - ps2f(64.0f) + half;
        matrix[3].y = Map_GetHeightAt(map, x, z);
        matrix[3].z = fromInt(128 - z) - ps2f(64.0f) - half;
    }

    // The new-object history: one period older (the bytes move up one) and an empty newest period.
    void MapObj_PushHistory(GuestPtr<CityMap> map)
    {
        sdk::memmove(map.address + 0x29u, map.address + 0x28u, 5);
        map->flag8History[0] = 0;
    }

    int MapObj_SumHistory(GuestPtr<CityMap> map)
    {
        int sum = 0;
        for (const uint8_t count : map->flag8History)
            sum += count;
        return sum;
    }

    void MapObjects_Slot0C(GuestPtr<CityMap>) {}

    PORT_FN(Map_CanPlace);
    PORT_FN(Map_FindPersonEntry);
    PORT_FN(Map_FindPersonOfFlaggedType);
    PORT_FN(Map_HighlightArea);
    PORT_FN(Map_UnhighlightArea);
    PORT_FN(Map_LevelArea);
    PORT_FN(Map_PlaceRubbleArea);
    PORT_FN(Map_PlaceSmallGoods);
    PORT_FN(Map_SaveState);
    PORT_FN(MapObj_GetMatrix);
    PORT_FN(MapObj_PushHistory);
    PORT_FN(MapObj_SumHistory);
    PORT_FN(MapObjects_Slot0C);

    // ---- tile lists, terrain (batch 38)

    // The items of a list (their count through `count`).
    GuestPtr<uint16_t> TileLists_Get(GuestPtr<TileLists> lists, int list, GuestPtr<uint32_t> count)
    {
        *count = lists->count[list];
        return GuestPtr<uint16_t>{lists.address + 2u * lists->start[list]};
    }

    // Removes the first `item` of a list: everything after it moves down one and the later lists start one
    // earlier. Returns 1 if it was there.
    int TileLists_Remove(GuestPtr<TileLists> lists, uint32_t item, int list)
    {
        const uint32_t start = lists->start[list], count = lists->count[list];
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t at = start + i;
            if (lists->items[at] != item)
                continue;
            for (uint32_t j = at; j < 0x3F7Eu; ++j)
                lists->items[j] = lists->items[j + 1];
            for (int k = list + 1; k < 0x50; ++k)
                lists->start[k] = static_cast<uint16_t>(lists->start[k] - 1);
            lists->count[list] = static_cast<uint16_t>(lists->count[list] - 1);
            return 1;
        }
        return 0;
    }

    void TileLists_SaveState(GuestPtr<TileLists> lists, GuestPtr<McManager> mc)
    {
        SaveFile_Write(mc, static_cast<int>(lists.address), 0x7F00);
        SaveFile_Write(mc, static_cast<int>(lists.address + 0x7F00u), 0xA0);
        SaveFile_Write(mc, static_cast<int>(lists.address + 0x7FA0u), 0xA0);
    }

    // A terrain layer's vectors (`a` at +0, `b` at +0x220) and its word +0x21C.
    void Terrain_SetVec(GuestPtr<uint32_t> layer, int value, GuestPtr<uint32_t> b, GuestPtr<uint32_t> a)
    {
        *GuestPtr<Vec4>{layer.address} = *GuestPtr<Vec4>{a.address};
        *GuestPtr<Vec4>{layer.address + 0x220u} = *GuestPtr<Vec4>{b.address};
        port::store<int32_t>(layer.address + 0x21Cu, value);
    }

    PORT_FN(TileLists_Get);
    PORT_FN(TileLists_Remove);
    PORT_FN(TileLists_SaveState);
    PORT_FN(Terrain_SetVec);

    // ---- placement search (batch 39)

    // A spot to put a building of the family's type near (x, z): for a size 2 home the four tiles from one back
    // along each axis, for size 3 the 5 x 5 tiles starting from the side of the player (a start of -3 when the
    // player stands left of the tile `half` from x, or beyond the one `half` from z, else -1; wrapping 2 and up
    // round to the other end), the first where the map answers 2 (placeable and highlighted). The rectangle (x, z,
    // x + size, z + size) is filled in and 1 returned; other sizes and failures give 0.
    int Placement_FindSpot(GuestPtr<Player> player, GuestPtr<CityScene> city, int family, int typeIndex, int x, int z, int half,
                           GuestPtr<int32_t> rect)
    {
        const GuestPtr<BuildingType> type{globals::buildingTypeAddress(typeIndex)};
        const auto found = [&](int tileX, int tileZ) {
            rect[0] = tileX;
            rect[1] = tileZ;
            rect[2] = rect[0] + type->size;
            rect[3] = rect[1] + type->size;
            return 1;
        };
        if (type->size == 2)
        {
            for (int dz = -1; dz < 2; ++dz)
                for (int dx = -1; dx < 2; ++dx)
                    if (Map_CanPlace(city->map, family, typeIndex, x + dx, z + dz) == 2)
                        return found(x + dx, z + dz);
        }
        else if (type->size == 3)
        {
            const int startX = ps2f(player->position.x) < fromInt(x - half) - ps2f(64.0f) + ps2f(1.5f) ? -3 : -1;
            const int startZ = fromInt(0x80 - (z - half)) - ps2f(64.0f) - ps2f(1.5f) < ps2f(player->position.z) ? -3 : -1;
            for (int j = 0; j < 5; ++j)
            {
                int dz = startZ + j;
                if (dz > 1)
                    dz -= 5;
                for (int i = 0; i < 5; ++i)
                {
                    int dx = startX + i;
                    if (dx > 1)
                        dx -= 5;
                    if (Map_CanPlace(city->map, family, typeIndex, x + dx, z + dz) == 2)
                        return found(x + dx, z + dz);
                }
            }
        }
        return 0;
    }

    PORT_FN(Placement_FindSpot);

    // ---- the player against the objects round it (batch 40)

    namespace
    {
        // Visits every object stack on the 3x3 tiles round the position, each object once (a list of the ids seen is
        // kept in the caller's frame at `list`), the stack's objects from the bottom up. For each the tile's placement
        // matrix (the layer matrix of the tile's kind, its position row set to the footprint's middle, the ground height
        // plus the lift (0.8 for each object above the first) and the footprint's far edge) is built at `matrix`, and a
        // flag word at `flagsAt` is kept: 1 for the first object, 2 for those above, with 3 or 4 added to the top one
        // (3 when its depth equals the type's halfword at +0x138, else 4; the first object alone keeps its 1) and bit 8
        // carried over. `visit(btype, obj)` returns true to stop the walk.
        template <typename Visit>
        bool eachObjectNear(GuestPtr<CityMap> map, GuestPtr<ps2f> position, uint32_t list, uint32_t matrix, uint32_t flagsAt, Visit visit)
        {
            const int tileX = sdk::fptosi(ps2f(64.0f) + ps2f(position[0]));
            const int tileZ = sdk::fptosi(ps2f(64.0f) - ps2f(position[2]));
            int count = 0;
            uint32_t listEnd = list;
            for (int dz = -1; dz < 2; ++dz)
            {
                const int z = tileZ + dz;
                for (int dx = -1; dx < 2; ++dx)
                {
                    GuestPtr<MapObject> obj = map->objects;
                    if (z >= 0 && z < 0x80 && tileX + dx >= 0 && tileX + dx < 0x80)
                        obj = map->objects + map->tiles[tileX + dx + z * 0x80].firstObject;
                    const uint32_t firstType = globals::buildingTypeAddress(obj->type);
                    bool seen = false;
                    for (int k = 0; k < count; ++k)
                        if (obj->id == port::load<uint16_t>(list + 2u * static_cast<uint32_t>(k)))
                        {
                            seen = true;
                            break;
                        }
                    if (seen)
                        continue;
                    ps2f lift = 0.0f;
                    ++count;
                    uint32_t depth = 0;
                    port::store<uint16_t>(listEnd, obj->id);
                    listEnd += 2u;
                    port::store<uint32_t>(flagsAt, 1);
                    for (;;)
                    {
                        ++depth;
                        if (obj->nextOnTile == kNoObject)
                        {
                            const uint32_t flags = port::load<uint32_t>(flagsAt);
                            if ((flags & 7u) == 1)
                                port::store<uint32_t>(flagsAt, flags & 8u);
                            else
                                port::store<uint32_t>(flagsAt, (flags & 8u) | (depth == port::load<uint16_t>(firstType + 0x138u) ? 3u : 4u));
                        }
                        const int kind = Map_GetTileKind(map, obj->tile);
                        for (uint32_t row = 0; row < 0x40; row += 0x10)
                            *GuestPtr<Vec4>{matrix + row} = *GuestPtr<Vec4>{0x001B2590u + static_cast<uint32_t>(kind) * 0x40u + row};
                        const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(obj->type)};
                        const ps2f half = fromInt(btype->size) / ps2f(2.0f);
                        const uint32_t tile = obj->tile;
                        port::store<float>(matrix + 0x30u, static_cast<float>((fromInt(static_cast<int32_t>(tile & 0x7F)) - ps2f(64.0f)) + half));
                        port::store<float>(matrix + 0x34u, static_cast<float>(lift + Map_GetHeightAt(map, tileX + dx, z)));
                        port::store<float>(matrix + 0x38u, static_cast<float>((fromInt(0x80 - static_cast<int32_t>(tile >> 7)) - ps2f(64.0f)) - half));
                        if (visit(btype, obj))
                            return true;
                        if (obj->nextOnTile == kNoObject)
                            break;
                        lift = lift - ps2f(0.8f);
                        obj = map->objects + obj->nextOnTile;
                        port::store<uint32_t>(flagsAt, (port::load<uint32_t>(flagsAt) & 8u) | 2u);
                    }
                }
            }
            return false;
        }
    }

    // Whether the player's circle (position, radius) touches one of the objects round it: each object's type is asked
    // (vtable +0x2C) with the position, the placement matrix, a2 and the flag word.
    int Player_CollideObjects(GuestPtr<CityMap> map, GuestPtr<ps2f> position, int a2, ps2f radius)
    {
        port::GuestFrame<0x190> frame; // the list of ids at +0x120, the matrix at +0x140, the flag word at +0x18C
        const uint32_t base = frame.ptr().address;
        const bool hit = eachObjectNear(map, position, base + 0x120u, base + 0x140u, base + 0x18Cu, [&](GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj) {
            return port::callGuest<int64_t>(port::load<uint32_t>(btype->vtable.address + 0x2Cu), btype, obj, position, GuestPtr<Vec4>{base + 0x140u}, a2, base + 0x18Cu, radius) != 0;
        });
        return hit ? 1 : 0;
    }

    // The nearest hit among the objects round the player: *distance starts at 100 and takes the smallest distance an
    // object's type reports (vtable +0x28, with the hit point and the distance as outputs); *hit gets the hit point
    // (four words). The answer is whether anything was hit.
    int Player_NearestObject(GuestPtr<CityMap> map, GuestPtr<ps2f> position, GuestPtr<uint32_t> hit, GuestPtr<ps2f> distance, ps2f radius)
    {
        port::GuestFrame<0x1B0> frame; // the list at +0x140, the matrix at +0x160, the hit point at +0x130, its distance at +0x1A8, flags at +0x1AC
        const uint32_t base = frame.ptr().address;
        *distance = 100.0f;
        eachObjectNear(map, position, base + 0x140u, base + 0x160u, base + 0x1ACu, [&](GuestPtr<BuildingType> btype, GuestPtr<MapObject> obj) {
            const int64_t answer = port::callGuest<int64_t>(port::load<uint32_t>(btype->vtable.address + 0x28u), btype, obj, position, GuestPtr<Vec4>{base + 0x160u}, base + 0x130u, base + 0x1A8u, base + 0x1ACu, radius);
            if (answer != 0 && ps2f(port::load<float>(base + 0x1A8u)) < ps2f(*distance))
            {
                *distance = port::load<float>(base + 0x1A8u);
                *GuestPtr<Vec4>{hit.address} = *GuestPtr<Vec4>{base + 0x130u};
            }
            return false;
        });
        return ps2f(*distance) == ps2f(100.0f) ? 0 : 1;
    }

    PORT_FN(Player_CollideObjects);
    PORT_FN(Player_NearestObject);

    // ---- the map's ticks (batch 40)

    // Every object that stands in the scan's current column of 128 (unk_14, stepping through 128 stripes; the scan is
    // 0x3F80 records) asks its building type (vtable +0x10, with the "any flag-8 object" word) whether it falls down; if
    // so, a sound is requested (unless the type is flag 0x80), the minimap plots the lot, the object is removed (map slot
    // +0xC) and rubble is laid where it stood. After the scan has covered all 128 columns the flag-8 count and the
    // approval it gave drop by one, the applicants' counter is reset when nobody waits or the clock is outside
    // 7:36..18:12 (or counts to 4 then wraps), and the claim re-check counter counts down.
    void MapObjects_Tick(GuestPtr<CityMap> map, GuestPtr<CityScene> city)
    {
        port::GuestFrame<0x60> frame; // the original's frame: the type slots' callees keep the stack they read
        if (map->refreshDelay > 0)
        {
            map->refreshDelay = static_cast<int16_t>(map->refreshDelay - 1);
            if (!(map->refreshDelay > 0))
                map->objectsChanged = 1;
        }
        if (map->objectsChanged != 0)
        {
            map->claimRecheck = 0x80;
            map->objectsChanged = 0;
        }
        for (int32_t scan = map->unk_14; scan < 0x3F80; scan += 0x80)
        {
            const GuestPtr<MapObject> obj = map->objects + scan;
            if (obj->type == 0)
                continue;
            const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(obj->type)};
            const int64_t falls = port::callGuest<int64_t>(port::load<uint32_t>(btype->vtable.address + 0x10u), btype, city, obj, map->unk_1C);
            if (falls == 0)
                continue;
            if ((btype->flags & 0x80u) == 0)
                Se_RequestForObject(map->objects[scan].tile, 3, 0x44, 0, 1, 10);
            Minimap_Plot(city->minimap, map->objects[scan].tile, btype->size, 4, 0);
            port::callGuest<void>(port::load<uint32_t>(map->vtable.address + 0xCu), map, map->objects[scan].id);
            const uint32_t tile = map->objects[scan].tile;
            Map_PlaceRubbleArea(map, static_cast<int>(tile & 0x7Fu), static_cast<int>(tile >> 7), btype->size, static_cast<int>(city.address));
        }
        map->unk_14 = map->unk_14 + 1;
        if (map->unk_14 >= 0x80)
        {
            if (map->unk_1C != 0 && map->flag8Objects > 0)
            {
                map->flag8Objects = map->flag8Objects - 1;
                city->pendingApproval = city->pendingApproval - 1;
            }
            map->unk_1C = map->flag8Objects > 0 ? 1 : 0;
            map->unk_14 = 0;
            if (Applicants_AnyWaiting(city->applicants) == 0)
                map->unk_60 = 0;
            if (city->clock < 0x6270 || !(city->clock < 0x10B30))
                map->unk_60 = 0;
            map->unk_60 = map->unk_60 + 1;
            if (!(map->unk_60 < 5))
                map->unk_60 = 0;
        }
        if (map->claimRecheck > 0)
            map->claimRecheck = static_cast<int16_t>(map->claimRecheck - 1);
    }

    // Once a second: bit 1 flags the 600-tick marks (every 600 ticks, and with at least two people on the map and
    // the city flag 4 the intro call timer restarts at a random point in the first half of the people), 2 the hours, 4
    // the days and 8 the first day of a twelve-month cycle. Every person's class then ticks it (vtable +0xC), the
    // member statuses follow and the map's objects tick.
    void MapPeople_Tick(GuestPtr<CityMap> map, GuestPtr<CityScene> city)
    {
        port::GuestFrame<0x60> frame; // the original's frame, as for the objects
        uint32_t marks = 0;
        if (remainder(city->clock, 600) == 0)
        {
            const int32_t inUse = map->peopleInUse;
            marks = 1;
            if (inUse >= 2 && (city->flags & 4u) != 0)
            {
                const int32_t random = static_cast<int32_t>(Random_Next());
                globals::introCallTick.set(0);
                globals::introCallDue.set(remainder(random, (inUse >= 0 ? inUse : inUse + 1) >> 1) + 1);
            }
        }
        const int32_t clock = city->clock;
        if (remainder(clock, 0xE10) == 0)
            marks |= 2;
        if (remainder(clock, 0x15180) == 0)
            marks |= 4;
        if (clock == 0 && remainder(city->monthCount, 12) == 0)
            marks |= 8;
        for (int32_t i = 0; i < 6000; ++i)
        {
            const GuestPtr<MapPerson> person = map->people + i;
            if (person->charType == 0)
                continue;
            const uint32_t cls = port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType);
            port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(cls + 0x20u) + 0xCu), cls, city, person, marks);
        }
        MemberStatus_Tick(city->clock, city->monthCount);
        MapObjects_Tick(map, city);
    }

    PORT_FN(MapObjects_Tick);
    PORT_FN(MapPeople_Tick);

    // ---- the city renderer's object walks (batch 40)

    namespace
    {
        // The camera's list of what is in view starts at the map's +0x78: nodes with the next node at +0 (and a second
        // list from +0x774 with the next at +4), the object index at +8 and the camera distance at +0xC; the objects
        // drawn this frame are listed from +0x788 (count at +0x2D00, 16 bytes apart) and lose their "drawn" flag 4 at the end.
        void clearDrawnFlags(GuestPtr<CityMap> map)
        {
            for (int i = 0; i < port::load<int32_t>(map.address + 0x2D00u); ++i)
            {
                const GuestPtr<MapObject> obj = map->objects + port::load<uint32_t>(map.address + 0x788u + 0x10u * static_cast<uint32_t>(i));
                obj->flags = static_cast<uint16_t>(obj->flags & 0xFFFBu);
            }
        }

        // The tile's centre of the object's footprint: x = tile x - 64 + size / 2, z = 64 - tile z' - size / 2 (z' counted
        // from the other side, 128 - z).
        ps2f footprintX(uint32_t tile, ps2f half) { return (fromInt(static_cast<int32_t>(tile & 0x7F)) - ps2f(64.0f)) + half; }
        ps2f footprintZ(uint32_t tile, ps2f half) { return (fromInt(0x80 - static_cast<int32_t>(tile >> 7)) - ps2f(64.0f)) - half; }
    }

    // Every object in view that is not drawn yet this frame (flag 4 marks it) has its type's draw-people slot (vtable
    // +0x18) called with the footprint's middle and the ground height, the tile kind, the city, the object, a flag word
    // (0) and the detail level (2 when the camera distance + 1 is past the fog start, else 0).
    void CityRender_UpdatePeople(GuestPtr<CityMap> map, GuestPtr<CityScene> city)
    {
        port::GuestFrame<0x70> frame; // the flag word at +0x6C
        const uint32_t flagsAt = frame.ptr().address + 0x6Cu;
        const ps2f fogNear = city->cityCamera->base.fogNear;
        uint32_t node = map.address + 0x78u;
        do
        {
            const uint32_t index = port::load<uint32_t>(node + 8u);
            if (index != 0)
            {
                const GuestPtr<MapObject> obj = map->objects + index;
                if ((obj->flags & 4u) == 0)
                {
                    obj->flags = static_cast<uint16_t>(obj->flags | 4u);
                    const int kind = Map_GetTileKind(map, obj->tile);
                    const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(obj->type)};
                    const uint32_t tile = obj->tile;
                    const ps2f half = fromInt(btype->size) / ps2f(2.0f);
                    const ps2f x = footprintX(tile, half);
                    const ps2f y = map->tiles[tile].height;
                    const ps2f z = footprintZ(tile, half);
                    const int mode = ps2f(1.0f) + ps2f(port::load<float>(node + 0xCu)) <= fogNear ? 0 : 2;
                    port::store<uint32_t>(flagsAt, 0);
                    const GuestPtr<MapObject> current = map->objects + port::load<uint32_t>(node + 8u);
                    const GuestPtr<BuildingType> type{globals::buildingTypeAddress(current->type)};
                    port::callGuest<void>(port::load<uint32_t>(type->vtable.address + 0x18u),
                                          x, y, z, type, kind, city, current, flagsAt, mode, map->people);
                }
            }
            node = port::load<uint32_t>(node);
        } while (node != 0);
        clearDrawnFlags(map);
        CharType_TickSlots();
    }

    // The same walk for drawing the buildings: objects not marked yet and without flag 0x80 go through every object of
    // their stack (bottom up, 0.8 lower for each above the first) with the type's draw slot (vtable +0x14), the flag word
    // as in the collision walks (1 for the first, then 2, with 3 or 4 added to the top one), and the detail level 2 when
    // the camera is below -10 or the object is past the fog start, 1 when the camera is within the footprint's reach, else
    // 0. A flag-8 entrance with a counting variant byte (+0x18, up to 0x2D) also asks LayerA for an instance at the highest
    // object that counts.
    void CityRender_CollectObjects(GuestPtr<CityMap> map, GuestPtr<CityScene> city)
    {
        port::GuestFrame<0xE0> frame; // the flag word at +0xDC
        const uint32_t flagsAt = frame.ptr().address + 0xDCu;
        const GuestPtr<CityCamera> camera = city->cityCamera;
        const ps2f fogNear = camera->base.fogNear;
        LayerA_SetSource(map->instances, static_cast<int>(port::load<uint32_t>(0x001BDCD0u) & 1u));
        GuestPtr<BuildingType> lastType;
        ps2f bestHeight = 0.0f;
        uint32_t node = map.address + 0x774u;
        do
        {
            const uint32_t index = port::load<uint32_t>(node + 8u);
            if (index != 0)
            {
                const GuestPtr<MapObject> obj = map->objects + index;
                if ((obj->flags & 4u) == 0)
                {
                    obj->flags = static_cast<uint16_t>(obj->flags | 4u);
                    if ((obj->flags & 0x80u) == 0)
                    {
                        const int kind = Map_GetTileKind(map, obj->tile);
                        GuestPtr<BuildingType> btype{globals::buildingTypeAddress(obj->type)};
                        const ps2f size = fromInt(btype->size);
                        const uint32_t tile = obj->tile;
                        const ps2f half = size / ps2f(2.0f);
                        ps2f y = map->tiles[tile].height;
                        const ps2f x = footprintX(tile, half);
                        const ps2f z = footprintZ(tile, half);
                        uint32_t reached = port::load<uint32_t>(node + 8u);
                        port::store<uint32_t>(flagsAt, 1);
                        const ps2f distance = port::load<float>(node + 0xCu);
                        int mode;
                        if (ps2f(port::load<float>(camera.address + 0x134u)) < ps2f(-10.0f) || fogNear < distance + half)
                            mode = 2;
                        else
                            mode = distance < size + half ? 1 : 0;
                        uint32_t depth = 0, best = 0;
                        lastType = btype;
                        while (reached != kNoObject)
                        {
                            ++depth;
                            const GuestPtr<MapObject> level = map->objects + reached;
                            if (level->nextOnTile == kNoObject)
                            {
                                const uint32_t flags = port::load<uint32_t>(flagsAt);
                                if ((flags & 7u) == 1)
                                    port::store<uint32_t>(flagsAt, flags & 8u);
                                else
                                    port::store<uint32_t>(flagsAt, (flags & 8u) | (depth == port::load<uint16_t>(lastType.address + 0x138u) ? 3u : 4u));
                            }
                            lastType = GuestPtr<BuildingType>{globals::buildingTypeAddress(level->type)};
                            port::callGuest<void>(port::load<uint32_t>(lastType->vtable.address + 0x14u), x, y, z, lastType, kind, city, level, flagsAt, mode);
                            const GuestPtr<MapObject> after = map->objects + reached;
                            if ((after->flags & 8u) != 0 && (after->flags & 0x20u) != 0)
                            {
                                const uint32_t count = after->variant & 0xFFu;
                                if (count != 0)
                                {
                                    if (best < count)
                                    {
                                        best = count;
                                        bestHeight = y;
                                    }
                                    const uint32_t next = (count + 1u) & 0xFFu;
                                    port::store<uint8_t>(after.address + 0x18u, static_cast<uint8_t>(next));
                                    if (next >= 0x2E)
                                        port::store<uint8_t>(after.address + 0x18u, 0);
                                }
                            }
                            port::store<uint32_t>(flagsAt, (port::load<uint32_t>(flagsAt) & 8u) | 2u);
                            y = y - ps2f(0.8f);
                            reached = (map->objects + reached)->nextOnTile;
                        }
                        if (best != 0)
                            LayerA_AddInstance(map->instances, lastType->size, static_cast<int>(best), x, bestHeight, z);
                    }
                }
            }
            node = port::load<uint32_t>(node + 4u);
        } while (node != 0);
        clearDrawnFlags(map);
    }

    PORT_FN(CityRender_UpdatePeople);
    PORT_FN(CityRender_CollectObjects);

    // ---- Tile array and save buffer lifetime (plan P6 batch 1) ----

    // Deletes the tile array (its block starts 0x10 bytes before the first tile), then the base object.
    GuestPtr<CityMap> MapTiles_dtor(GuestPtr<CityMap> map, int16_t deleteFlag)
    {
        if (map)
        {
            if (map->tiles)
                sdk::mw_DeleteArray(map->tiles.address - 0x10u);
            BaseObject_dtor(static_cast<int>(map.address), 0);
            if (0 < deleteFlag)
                sdk::operator_delete(map.address);
        }
        return map;
    }

    // The element constructors the array allocations below call through a pointer (code labels without a Ghidra function:
    // config/platform_signatures.txt). A tile starts flat, with no object and not walkable.
    GuestPtr<MapTile> MapTile_ctor(GuestPtr<MapTile> tile)
    {
        tile->firstObject = kNoObject;
        tile->flags = 0;
        tile->height = 0.0f;
        return tile;
    }

    // An object starts as type 0.
    GuestPtr<MapObject> MapObject_ctor(GuestPtr<MapObject> object)
    {
        object->type = 0;
        return object;
    }

    // A person starts with no character type and the friendship status byte at 0xE0.
    GuestPtr<MapPerson> MapPerson_ctor(GuestPtr<MapPerson> person)
    {
        person->status = 0xE0;
        person->charType = 0;
        return person;
    }

    // Allocates the 0x4000 tiles (0x18 bytes each, after a 0x10-byte header) and constructs each with MapTile_ctor.
    GuestPtr<CityMap> MapTiles_ctor(GuestPtr<CityMap> map)
    {
        BaseObject_ctor(static_cast<int>(map.address));
        const uint32_t block = sdk::mw_NewArray(0x60010u);
        map->tiles = GuestPtr<MapTile>{sdk::construct_new_array(block, addr::MapTile_ctor, 0u, 0x18u, 0x4000u)};
        return map;
    }

    // The save buffer: 6 bytes per tile for 0x4000 tiles.
    void MapSave_AllocBuffer(GuestPtr<CityMap> map)
    {
        map->packedTiles = GuestPtr<void>{sdk::malloc(0x18000u)};
    }

    void MapSave_FreeBuffer(GuestPtr<CityMap> map)
    {
        if (map->packedTiles)
        {
            sdk::free(map->packedTiles.address);
            map->packedTiles = GuestPtr<void>{0u};
        }
    }

    PORT_FN(MapTile_ctor);
    PORT_FN(MapObject_ctor);
    PORT_FN(MapPerson_ctor);
    PORT_FN(MapTiles_dtor);
    PORT_FN(MapTiles_ctor);
    PORT_FN(MapSave_AllocBuffer);
    PORT_FN(MapSave_FreeBuffer);

    // ---- Object arrays (plan P6 batch 2) ----

    // The object array (its block starts 0x10 bytes before the first object) and the tile lists are deleted, then
    // the tile array and the base object through MapTiles_dtor.
    GuestPtr<CityMap> MapObjects_dtor(GuestPtr<CityMap> map, int16_t deleteFlag)
    {
        if (map)
        {
            port::store<uint32_t>(map.address + 0x64u, 0x001BCEF0u);
            const uint32_t objects = port::load<uint32_t>(map.address + 0x8u);
            if (objects != 0u)
            {
                sdk::mw_DeleteArray(objects - 0x10u);
                port::store<uint32_t>(map.address + 0x8u, 0u);
            }
            const uint32_t tileLists = port::load<uint32_t>(map.address + 0xCu);
            if (tileLists != 0u)
            {
                sdk::operator_delete(tileLists);
                port::store<uint32_t>(map.address + 0xCu, 0u);
            }
            MapTiles_dtor(map, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(map.address);
        }
        return map;
    }

    PORT_FN(MapObjects_dtor);

    // ---- Packing the tile array for the save file (plan P6 batch 2) ----

    // Packs the 0x4000 tiles into the save buffer (malloc 0x18000): per tile 6 bytes, the height in 1/2048 units (16 bits), then
    // the tile's first-object halfword (+0x14) and flags halfword (+0x16).
    void MapSave_PackTiles(GuestPtr<CityMap> map)
    {
        map->packedTiles = GuestPtr<void>{sdk::malloc(0x18000u)};
        for (uint32_t i = 0; i < 0x4000u; ++i)
        {
            const uint32_t tile = map->tiles.address + 0x18u * i;
            const uint32_t packed = map->packedTiles.address + 6u * i;
            port::store<uint16_t>(packed, static_cast<uint16_t>(sdk::fptosi(ps2f(2048.0f) * ps2f(port::load<float>(tile)))));
            port::store<uint16_t>(packed + 2u, port::load<uint16_t>(tile + 0x14u));
            port::store<uint16_t>(packed + 4u, port::load<uint16_t>(tile + 0x16u));
        }
    }

    // Unpacks the save buffer into the tiles, rebuilds every tile's corner heights, frees the buffer, and recounts the buildings
    // per category from the objects (category 4 only without type flag 0x80; category 5 of size 3 and up also in +0x5C).
    void MapSave_UnpackTiles(GuestPtr<CityMap> map)
    {
        for (uint32_t i = 0; i < 0x4000u; ++i)
        {
            const uint32_t tile = map->tiles.address + 0x18u * i;
            const uint32_t packed = map->packedTiles.address + 6u * i;
            port::store<float>(tile, static_cast<float>(fromInt(port::load<int16_t>(packed)) / ps2f(2048.0f)));
            port::store<uint16_t>(tile + 0x14u, port::load<uint16_t>(packed + 2u));
            port::store<uint16_t>(tile + 0x16u, port::load<uint16_t>(packed + 4u));
        }
        for (int z = 0; z < 0x80; ++z)
            for (int x = 0; x < 0x80; ++x)
                Map_UpdateTileGeometry(map, x, z);
        if (map->packedTiles)
        {
            sdk::free(map->packedTiles.address);
            map->packedTiles = GuestPtr<void>{0u};
        }
        for (uint32_t offset = 0x30u; offset <= 0x5Cu; offset += 4u)
            port::store<uint32_t>(map.address + offset, 0u);
        for (uint32_t i = 0; i < 0x3F80u; ++i)
        {
            const uint32_t type = port::load<uint16_t>(map->objects.address + 0x1Cu * i);
            if (type == 0)
                continue;
            const uint32_t btype = globals::buildingTypeAddress(type);
            const uint32_t category = port::load<uint8_t>(btype + 0x145u);
            if (category == 4)
            {
                if ((port::load<uint16_t>(btype + 0x136u) & 0x80u) == 0)
                    port::store<uint32_t>(map.address + 0x30u + 4u * 4u, port::load<uint32_t>(map.address + 0x30u + 4u * 4u) + 1u);
                continue;
            }
            if (category == 5 && port::load<uint8_t>(btype + 0x142u) >= 3)
                port::store<uint32_t>(map.address + 0x5Cu, port::load<uint32_t>(map.address + 0x5Cu) + 1u);
            port::store<uint32_t>(map.address + 0x30u + 4u * category, port::load<uint32_t>(map.address + 0x30u + 4u * category) + 1u);
        }
    }

    PORT_FN(MapSave_PackTiles);
    PORT_FN(MapSave_UnpackTiles);

    // The map's object part: the tile array first, then the counters cleared, the tile lists (0x8040 bytes), and the 0x3F80
    // object records constructed in one block; object 0 is the "none" object, and the rest of the set-up creates the grass
    // (type 2) on tiles 2 to 0x7D and the border walls (type 9) round the map on every second tile of its four edges.
    GuestPtr<CityMap> MapObjects_ctor(GuestPtr<CityMap> map)
    {
        MapTiles_ctor(map);
        port::store<uint32_t>(map.address + 0x64u, 0x001BCEF0u);
        port::store<int32_t>(map.address + 0x10u, 1);
        port::store<int32_t>(map.address + 0x14u, 0);
        port::store<int32_t>(map.address + 0x18u, 0);
        port::store<int32_t>(map.address + 0x1Cu, 0);
        port::store<int16_t>(map.address + 0x20u, 0);
        port::store<int16_t>(map.address + 0x22u, 0);
        port::store<int32_t>(map.address + 0x24u, 0);
        sdk::memset(map.address + 0x28u, 0, 6);
        port::store<int16_t>(map.address + 0x2Eu, 0);
        port::store<int32_t>(map.address + 0x60u, 0);
        for (uint32_t offset = 0x30u; offset <= 0x5Cu; offset += 4u)
            port::store<uint32_t>(map.address + offset, 0u);
        uint32_t lists = sdk::operator_new(0x8040u);
        if (lists != 0u)
            lists = TileLists_Clear(GuestPtr<TileLists>{lists}).address;
        port::store<uint32_t>(map.address + 0xCu, lists);
        const uint32_t block = sdk::mw_NewArray(0x6F210u);
        port::store<uint32_t>(map.address + 8u, sdk::construct_new_array(block, addr::MapObject_ctor, 0u, 0x1Cu, 0x3F80u));
        MapObj_Init(map, 0, 1, 0, 0x4000);
        for (int z = 2; z < 0x7E; ++z)
            for (int x = 2; x < 0x7E; ++x)
                Map_CreateObject(map, 0, 2, x, z);
        for (int z = 0; z < 0x7E; z += 2)
            Map_CreateObject(map, 0, 9, 0, z);
        for (int z = 0; z < 0x7E; z += 2)
            Map_CreateObject(map, 0, 9, 0x7E, z);
        for (int x = 2; x < 0x7E; x += 2)
            Map_CreateObject(map, 0, 9, x, 0);
        for (int x = 0; x < 0x80; x += 2)
            Map_CreateObject(map, 0, 9, x, 0x7E);
        return map;
    }

    PORT_FN(MapObjects_ctor);

    // ---- The people array (plan P6 batch 2) ----

    // The object map, then the table of 6000 people (0x30 bytes each, constructed through 0x143630) at +0x68; the person lists
    // (+0x6C, +0x70, +0x74) start empty.
    GuestPtr<CityMap> MapPeople_ctor(GuestPtr<CityMap> map)
    {
        MapObjects_ctor(map);
        port::store<uint32_t>(map.address + 0x64u, 0x001BCEE0u);
        port::store<uint32_t>(map.address + 0x6Cu, 0u);
        const uint32_t block = sdk::mw_NewArray(0x46510u);
        port::store<uint32_t>(map.address + 0x68u, sdk::construct_new_array(block, addr::MapPerson_ctor, 0u, 0x30u, 6000u));
        port::store<uint32_t>(map.address + 0x70u, 0u);
        port::store<uint32_t>(map.address + 0x74u, 0u);
        return map;
    }

    // The people array (its block starts 0x10 bytes before the first person) is deleted, then the object map below it.
    GuestPtr<CityMap> MapPeople_dtor(GuestPtr<CityMap> map, int16_t deleteFlag)
    {
        if (map)
        {
            port::store<uint32_t>(map.address + 0x64u, 0x001BCEE0u);
            const uint32_t people = port::load<uint32_t>(map.address + 0x68u);
            if (people != 0u)
            {
                sdk::mw_DeleteArray(people - 0x10u);
                port::store<uint32_t>(map.address + 0x68u, 0u);
            }
            MapObjects_dtor(map, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(map.address);
        }
        return map;
    }

    PORT_FN(MapPeople_ctor);
    PORT_FN(MapPeople_dtor);
}
