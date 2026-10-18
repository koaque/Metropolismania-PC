// The city map: 128 x 128 tiles, the placed objects and the per-tile object lists, and the people.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"
#include "vu0.h"

#include <algorithm>

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
}
