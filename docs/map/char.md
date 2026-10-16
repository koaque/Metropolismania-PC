# char

People and the player character.

- Character types come from `DATA\BIN\CHARTYPE.BIN` (0x18-byte records: byte 0 is the class, bytes 8, 9, 10 and 14
  are model parts). Class objects sit in the table at gp `0x001BDA3C`; vtables: base `0x001BCE70`, class 2
  `0x001BCE90`, class 1 (people) `0x001BCEB0`; slots `+8` dtor, `+0xC` update, `+0x10` draw, `+0x14` draw2.
- People are the 0x30-byte records in the map object (`map`): `+0` x, `+8` z, `+0x10` next person on the tile,
  `+0x12` own index, `+0x14` workplace, `+0x16` building they are in (`0x3F80` = outside), `+0x1E` destination,
  `+0x20` follow target, `+0x22` flags, `+0x24` character type, `+0x26` schedule state, `+0x27` activity,
  `+0x28` home, `+0x2A` family, `+0x2C` family this resident can introduce, `+0x2E` member, `+0x2F` friendship status byte.
- `mm_Person_*` moves people between home, work and other buildings by their schedules (`mm_Schedule_*`) and path
  waypoints (`mm_Path_*`, walkable tiles have flag `0x10`).
- `mm_Player_*` is the player: walking, turning, picking the building or person in front, placing bag items and homes
  for applicants.
- `mm_Stamina_*`: the player's stamina record (`0x001BDBB8`, maximum 399999). Eating restores it; sleeping at home
  refills it (`mm_MyHome_Close` in `talk`).
- `mm_StatusByte_*`: the friendship status byte (bits 0-1, bits 2-3, level in the high nibble).

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

83 functions (named 81, typed 2). Blocks: 19 can reach a thread switch, 2 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Name | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0012E370` | `mm_Path_ScanLine` | 1092 | no | (provisional) walks tiles along a line looking for a walkable tile (flag 0x10) |
| `0x0012E7D0` | `mm_Path_NextWaypoint` | 1588 | no | next waypoint tile toward the person's building (+0x16); 0x4000 when none |
| `0x0012EE20` | `mm_Path_TileDistance` | 176 | no | (provisional) VU0 distance test between tiles a1 and a2 |
| `0x0012EED0` | `mm_CharType_Slot18` | 8 | no | base vtable slot +0x18: empty |
| `0x0012EEE0` | `mm_CharType_Draw2` | 8 | no | base slot +0x14: empty |
| `0x0012EEF0` | `mm_CharType_Draw` | 8 | no | base slot +0x10: empty |
| `0x0012EF00` | `mm_CharType_Update` | 8 | no | base slot +0xC: returns 0 |
| `0x0012EF10` | `mm_CharType_dtor` | 76 | yes | base vtable 0x1BCE70; delete when the flag is > 0 |
| `0x0012EF60` | `mm_CharType_ctor` | 180 | no | base vtable 0x1BCE70; four model parts from the table 0x1F3B90 by record bytes 8/9/10/14, sizes from bytes 11-13, ids from bytes 2/4/0x14 |
| `0x0012F020` | `mm_Char2_ProbeTilesA` | 3808 | no | (provisional, was mm_Path_Search) tests the 8 neighbour tiles around a position for class-2 movement mode A |
| `0x0012FF00` | `mm_Char_CheckNeighbourTile` | 440 | no | (provisional, was mm_Path_CheckNeighbour) neighbour tile test shared by both probe functions (tables 0x1AEFF0/0x1AF020) |
| `0x001300C0` | `mm_Char2_ProbeTilesB` | 3808 | no | (provisional) same as mode A for movement mode B |
| `0x00130FA0` | `mm_Char2_MoveA` | 2300 | no | (provisional) class-2 movement mode A: rotates the heading (sceVu0RotMatrixY) and probes ahead with mm_Char2_ProbeTilesA |
| `0x001318A0` | `mm_Char2_MoveB` | 2280 | no | (provisional) class-2 movement mode B with mm_Char2_ProbeTilesB |
| `0x00132190` | `mm_Char2_Draw` | 612 | no | class 2 slot +0x10: instance of the character on the terrain (height from 0x1415E0), within the draw range (0x1BD980) |
| `0x00132400` | `mm_Char2_Update` | 1048 | yes | class 2 slot +0xC: moves the character (mode A or B) and updates its tile |
| `0x00132820` | `mm_Char2_dtor` | 96 | yes | vtable 0x1BCE90, then the base destructor |
| `0x00132880` | `mm_Char2_ctor` | 52 | no | base constructor, vtable 0x1BCE90 |
| `0x001328C0` | `mm_CharType_LoadAll` | 416 | yes | loads DATA\BIN\CHARTYPE.BIN and creates one class object per 0x18-byte record (class 0 base, 1 person, 2 class 2) in the table at gp 0x1BDA3C |
| `0x00132A70` | `mm_Person_CheckBuilding` | 244 | yes | (provisional) person vs. its target building record: flags 0x20/0x200/4, the building's state byte +0x27 |
| `0x00132B70` | `mm_Person_TickCounters` | 300 | no | friendship level tick (+0x2F), +0x25 countdown, and with city flag 4 the family a resident introduces phones to ask about moving in (up to 4 calls) |
| `0x00132CA0` | `mm_Person_ProbeTile` | 248 | no | (provisional) tests the tile a person would step onto |
| `0x00132DA0` | `mm_Person_Walk` | 3528 | no | (provisional) 3.5 KB walking movement for people over map tiles |
| `0x00133B70` | `mm_Person_UpdatePose` | 780 | no | (provisional) person's matrix from heading and terrain height |
| `0x00133E80` | `mm_Person_Draw2` | 700 | yes | class 1 slot +0x14: (provisional) extra drawing (shadow/marker) for a person |
| `0x00134140` | `mm_CharType_ResetLookup` | 92 | no | (provisional) resets the two 128-entry tables at 0x1C5110/0x1C5310 |
| `0x001341A0` | `mm_CharType_BuildLookup` | 184 | no | (provisional) fills the tables at 0x1C5110/0x1C5310 after loading CHARTYPE.BIN |
| `0x00134260` | `mm_Person_BuildPose` | 896 | no | (provisional) walking pose matrices for a person (copies of the base matrix 0x1B2550, animation table 0x1C5180) |
| `0x001345E0` | `mm_Person_Draw` | 1088 | yes | class 1 slot +0x10: person instances on the terrain, within the draw range |
| `0x00134A20` | `mm_Person_Update` | 3032 | yes | class 1 slot +0xC: a person's behaviour per tick: target building, walking, waiting, random choices |
| `0x00135600` | `mm_Person_dtor` | 96 | yes | vtable 0x1BCEB0, then the base destructor |
| `0x00135660` | `mm_Person_ctor` | 136 | no | base constructor, vtable 0x1BCEB0, walking speed +0x24 = record u16 +6 / 1e6 |
| `0x0013C010` | `mm_StatusByte_IncHighLevel` | 64 | no | (provisional) level 12..13 -> +1 |
| `0x0013C050` | `mm_StatusByte_ClearMid2` | 48 | no | clears bits 2-3 when set |
| `0x0013C080` | `mm_StatusByte_IncLevel` | 108 | no | (provisional) level +1 below 12; above 1 clears bits 0-1 |
| `0x001439D0` | `mm_Player_CollideObjects` | 1048 | indirect | (provisional) objects on the 3 x 3 tiles around the player, collision push |
| `0x00143DF0` | `mm_Player_NearestObject` | 1144 | indirect | (provisional) objects on the 3 x 3 tiles around the player, nearest hit distance (100 = none) |
| `0x00157D80` | `mm_Player_LoadState` | 20 | no | reads 0x38 bytes from the save file |
| `0x00157DA0` | `mm_Player_SaveState` | 20 | no | writes 0x38 bytes |
| `0x00157DC0` | `mm_Player_GetMatrix` | 40 | no | the player model's matrix (object +0x88) |
| `0x00157DF0` | `mm_Player_PlayFootstep` | 528 | no | footstep sound by the tile under the player |
| `0x00158000` | `mm_Player_UpdateAction` | 880 | no | (provisional) pad-held actions for the player (analog bits 0x100000-0x800000), with a sound |
| `0x00158370` | `mm_Player_PickTarget` | 1704 | no | (provisional) what the player faces: building or person in front (ray, tiles, people), sets the cursor target |
| `0x00159320` | `mm_Player_SetMode` | 56 | no | mode a1 (-1 = previous), remembers the previous mode, resets the mode timer |
| `0x00159360` | `mm_Player_GetMode` | 8 | no | current mode |
| `0x00159370` | `mm_Player_Draw` | 100 | no | player model instances |
| `0x001593E0` | `mm_Player_Move` | 1896 | no | (provisional) walking/running with the pad and the analog stick |
| `0x00159B50` | `mm_Player_Turn` | 428 | no | (provisional) turning with the pad |
| `0x00159D00` | `mm_Player_Update` | 1988 | yes | player per frame by mode: movement, camera, minimap, footsteps, placing items and homes |
| `0x0015A4D0` | `mm_Player_DeletingDtor` | 68 | yes | delete when the flag is > 0 |
| `0x0015A520` | `mm_Player_ctor` | 92 | no | model 0x1F3CD4 at +0x88, cleared state |
| `0x0015DB30` | `mm_Person_FindBuildingInList` | 444 | no | (provisional) a building of the wanted kind from a tile list near the person |
| `0x0015DCF0` | `mm_Person_PlaceHomeObject` | 800 | no | (provisional) places an object for the person's home |
| `0x0015E010` | `mm_Person_RandomTarget` | 436 | no | (provisional) random target along walkable tiles |
| `0x0015E1D0` | `mm_Person_TargetWork` | 292 | no | (provisional) |
| `0x0015E300` | `mm_Person_TileHasCompanion` | 212 | no | (provisional) people on the tile matching the person |
| `0x0015E3E0` | `mm_Person_TargetVisit` | 276 | no | (provisional) |
| `0x0015E500` | `mm_Person_TargetShop` | 456 | no | (provisional) |
| `0x0015E6D0` | `mm_Person_TargetHome` | 296 | no | (provisional) |
| `0x0015E800` | `mm_Person_ChooseDestination` | 1492 | yes | (provisional) picks where a person goes next by schedule and needs, using the target helpers |
| `0x0015EE10` | `mm_Schedule_QueryA` | 416 | yes | (provisional) schedule lookup for drawing |
| `0x0015EFB0` | `mm_Schedule_QueryB` | 384 | yes | (provisional) schedule lookup used by the scripts |
| `0x0015F130` | `mm_Schedule_GetActivity` | 520 | yes | (provisional) current activity of a person |
| `0x0015F340` | `mm_Schedule_GetBuildingActivity` | 452 | yes | (provisional) current activity for a person and building |
| `0x0015F510` | `mm_Schedule_PickEntry` | 120 | no | (provisional) |
| `0x0015F590` | `mm_Schedule_Evaluate` | 828 | yes | (provisional) schedule entry for a family member at the current time (family record, time globals 0x1BD9A4..) |
| `0x0015F8D0` | `mm_Schedule_Load` | 68 | yes | loads DATA\BIN\SCHEDULE.BIN |
| `0x00162370` | `mm_Stamina_LoadState` | 20 | no | reads the 0xC-byte stamina record from the save file |
| `0x00162390` | `mm_Stamina_SaveState` | 20 | no | writes it |
| `0x001623B0` | `mm_Stamina_Tick` | 184 | no | stamina +4 drops by stamina/100000 + 1 per tick (not below 0) |
| `0x00162470` | `mm_Stamina_Eat` | 124 | no | eating goods of categories 0x11-0x14 adds to stamina |
| `0x00162500` | `mm_Stamina_HungerLevel` | 64 | no | 0 below 100000, else a higher level (event "Hunger") |
| `0x00162540` | `mm_Stamina_Init` | 24 | no | stamina 399999 |
| `0x00162560` | `mm_Stamina_CreateGlobal` | 60 | yes | 0x1BDBB8 = new stamina record |
| `0x0019AA80` | `mm_Person_ExitBuilding` | 160 | no | if person a1 is inside a building: lowers the building's occupant count (+0x17) and marks the person outside |
| `0x0019AE50` | `mm_Person_IsOnOpenTile` | 212 | no | (provisional) the person's tile holds no building with type flag 0x40 |
| `0x0019AF30` | `mm_Person_IsAtBuilding` | 232 | no | person a1 (6000 counts as yes) stands at building a2 |
| `0x0019B020` | `mm_Person_FindIdleOfType` | 136 | no | first person of char type a2 whose home building is free (0x165660) |
| `0x0019B0B0` | `mm_Person_IsNearBuilding` | 424 | no | person position within a3 tiles of building a2's footprint |
| `0x0019B260` | `mm_Player_DistanceToBuilding` | 360 | no | distance in tiles from the player to building a1's footprint |
| `0x0019B3D0` | `mm_Player_DistanceToPerson` | 104 | no | distance from the player to person a1 (sqrt) |
| `0x0019B440` | `mm_Player_DistanceToPoint` | 80 | no | distance from the player to point a1 |
| `0x0019B490` | `mm_Person_Distance` | 84 | no | distance between persons a1 and a2 |
