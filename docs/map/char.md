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

84 functions (typed 79, verified 5). Blocks: 19 can reach a thread switch, 2 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0012E370` | `uint16_t mm_Path_ScanLine(CharClass *cls, CityScene *city, MapPerson *person, int a3, float *position)` | 1092 | no | (provisional) walks tiles along a line looking for a walkable tile (flag 0x10) |
| `0x0012E7D0` | `uint16_t mm_Path_NextWaypoint(CharClass *cls, CityScene *city, MapPerson *person, int tile)` | 1588 | no | next waypoint tile toward the person's building (+0x16); 0x4000 when none |
| `0x0012EE20` | `float mm_Path_TileDistance(int a0, uint32_t tileA, uint32_t tileB)` | 176 | no | (provisional) VU0 distance test between tiles a1 and a2 |
| `0x0012EED0` | `void mm_CharType_Slot18(CharClass *cls)` | 8 | no | base vtable slot +0x18: empty |
| `0x0012EEE0` | `void mm_CharType_Draw2(CharClass *cls)` | 8 | no | base slot +0x14: empty |
| `0x0012EEF0` | `void mm_CharType_Draw(CharClass *cls)` | 8 | no | base slot +0x10: empty |
| `0x0012EF00` | `int mm_CharType_Update(CharClass *cls)` | 8 | no | base slot +0xC: returns 0 |
| `0x0012EF10` | `CharClass *mm_CharType_dtor(CharClass *cls, int16_t deleteFlag)` | 76 | yes | base vtable 0x1BCE70; delete when the flag is > 0 |
| `0x0012EF60` | `CharClass *mm_CharType_ctor(CharClass *cls, uint8_t *record)` | 180 | no | base vtable 0x1BCE70; four model parts from the table 0x1F3B90 by record bytes 8/9/10/14, sizes from bytes 11-13, ids from bytes 2/4/0x14 |
| `0x0012F020` | `float mm_Char2_ProbeTilesA(CharClass *cls, CityScene *city, MapPerson *person, MapObject *obj, int t0, float f12, float f13)` | 3808 | no | (provisional, was mm_Path_Search) tests the 8 neighbour tiles around a position for class-2 movement mode A |
| `0x0012FF00` | `int mm_Char_CheckNeighbourTile(CharClass *cls, CityScene *city, float *position, int x, int z)` | 440 | no | (provisional, was mm_Path_CheckNeighbour) neighbour tile test shared by both probe functions (tables 0x1AEFF0/0x1AF020) |
| `0x001300C0` | `float mm_Char2_ProbeTilesB(CharClass *cls, CityScene *city, MapPerson *person, MapObject *obj, int t0, float f12, float f13)` | 3808 | no | (provisional) same as mode A for movement mode B |
| `0x00130FA0` | `void mm_Char2_MoveA(CharClass *cls, CityScene *city, MapPerson *person, int a3, int t0)` | 2300 | no | (provisional) class-2 movement mode A: rotates the heading (sceVu0RotMatrixY) and probes ahead with mm_Char2_ProbeTilesA |
| `0x001318A0` | `void mm_Char2_MoveB(CharClass *cls, CityScene *city, MapPerson *person, int a3, int t0)` | 2280 | no | (provisional) class-2 movement mode B with mm_Char2_ProbeTilesB |
| `0x00132190` | `void mm_Char2_Draw(CharClass *cls, CityScene *city, MapPerson *person, int a3, int t0, uint32_t *t1)` | 612 | no | class 2 slot +0x10: instance of the character on the terrain (height from 0x1415E0), within the draw range (0x1BD980) |
| `0x00132400` | `void mm_Char2_Update(CharClass *cls, CityScene *city, MapPerson *person, uint32_t timeFlags)` | 1048 | yes | class 2 slot +0xC: moves the character (mode A or B) and updates its tile |
| `0x00132820` | `CharClass *mm_Char2_dtor(CharClass *cls, int16_t deleteFlag)` | 96 | yes | vtable 0x1BCE90, then the base destructor |
| `0x00132880` | `CharClass *mm_Char2_ctor(CharClass *cls, uint8_t *record)` | 52 | no | base constructor, vtable 0x1BCE90 |
| `0x001328C0` | `void mm_CharType_LoadAll(void)` | 416 | yes | loads DATA\BIN\CHARTYPE.BIN and creates one class object per 0x18-byte record (class 0 base, 1 person, 2 class 2) in the table at gp 0x1BDA3C |
| `0x00132A70` | `void mm_Person_CheckBuilding(CharClass *cls, CityScene *city, MapPerson *person)` | 244 | yes | (provisional) person vs. its target building record: flags 0x20/0x200/4, the building's state byte +0x27 |
| `0x00132B70` | `int mm_Person_TickCounters(CharClass *cls, CityScene *city, MapPerson *person)` | 300 | no | friendship level tick (+0x2F), +0x25 countdown, and with city flag 4 the family a resident introduces phones to ask about moving in (up to 4 calls) |
| `0x00132CA0` | `int mm_Person_ProbeTile(CharClass *cls, CityScene *city, float *position, uint32_t tile)` | 248 | no | (provisional) tests the tile a person would step onto |
| `0x00132DA0` | `void mm_Person_Walk(CharClass *cls, CityScene *city, MapPerson *person, int a3, int t0)` | 3528 | no | (provisional) 3.5 KB walking movement for people over map tiles |
| `0x00133B70` | `void mm_Person_UpdatePose(CharClass *cls, CityScene *city, float *position, int a3, int t0)` | 780 | no | (provisional) person's matrix from heading and terrain height |
| `0x00133E80` | `void mm_Person_Draw2(CharClass *cls, CityScene *city, MapPerson *person, uint32_t family, uint32_t member, Vec4 *matrix, int t2, int32_t *mesh, float *phase)` | 700 | yes | class 1 slot +0x14: (provisional) extra drawing (shadow/marker) for a person |
| `0x00134140` | `void mm_CharType_ResetLookup(void)` | 92 | no | counts down the 25 character type slot timers (0x1C5310) and frees a slot (0x1C5110 = 6000) when its timer reaches 0 |
| `0x001341A0` | `void mm_CharType_BuildLookup(void)` | 184 | no | (provisional) fills the tables at 0x1C5110/0x1C5310 after loading CHARTYPE.BIN |
| `0x00134260` | `void mm_Person_BuildPose(CharClass *cls, CityScene *city, float *position)` | 896 | no | (provisional) walking pose matrices for a person (copies of the base matrix 0x1B2550, animation table 0x1C5180) |
| `0x001345E0` | `void mm_Person_Draw(CharClass *cls, CityScene *city, MapPerson *person, int a3, int t0, int32_t *t1, int t2)` | 1088 | yes | class 1 slot +0x10: person instances on the terrain, within the draw range |
| `0x00134A20` | `void mm_Person_Update(CharClass *cls, CityScene *city, MapPerson *person, uint32_t timeFlags)` | 3032 | yes | class 1 slot +0xC: a person's behaviour per tick: target building, walking, waiting, random choices |
| `0x00135600` | `CharClass *mm_Person_dtor(CharClass *cls, int16_t deleteFlag)` | 96 | yes | vtable 0x1BCEB0, then the base destructor |
| `0x00135660` | `CharClass *mm_Person_ctor(CharClass *cls, uint8_t *record)` | 136 | no | base constructor, vtable 0x1BCEB0, walking speed +0x24 = record u16 +6 / 1e6 |
| `0x0013C010` | `void mm_StatusByte_IncHighLevel(uint8_t *status)` | 64 | no | (provisional) level 12..13 -> +1 |
| `0x0013C050` | `void mm_StatusByte_ClearMid2(uint8_t *status)` | 48 | no | clears bits 2-3 when set |
| `0x0013C080` | `void mm_StatusByte_IncLevel(uint8_t *status)` | 108 | no | (provisional) level +1 below 12; above 1 clears bits 0-1 |
| `0x001439D0` | `int mm_Player_CollideObjects(CityMap *map, float *position, int a2, float radius)` | 1048 | indirect | (provisional) objects on the 3 x 3 tiles around the player, collision push |
| `0x00143DF0` | `int mm_Player_NearestObject(CityMap *map, float *position, uint32_t *hit, float *distance, float radius)` | 1144 | indirect | (provisional) objects on the 3 x 3 tiles around the player, nearest hit distance (100 = none) |
| `0x00157D80` | `void mm_Player_LoadState(Player *player, McManager *mc)` | 20 | no | reads 0x38 bytes from the save file |
| `0x00157DA0` | `void mm_Player_SaveState(Player *player, McManager *mc)` | 20 | no | writes 0x38 bytes |
| `0x00157DC0` | `void mm_Player_GetMatrix(Player *player, Vec4 *matrix)` | 40 | no | the player model's matrix (object +0x88) |
| `0x00157DF0` | `void mm_Player_PlayFootstep(Player *player, CityScene *city)` | 528 | no | footstep sound by the tile under the player |
| `0x00158000` | `void mm_Player_UpdateAction(Player *player, float *move, int a2)` | 880 | no | (provisional) pad-held actions for the player (analog bits 0x100000-0x800000), with a sound |
| `0x00158370` | `void mm_Player_PickTarget(Player *player, CityScene *city)` | 1704 | no | (provisional) what the player faces: building or person in front (ray, tiles, people), sets the cursor target |
| `0x00159320` | `void mm_Player_SetMode(Player *player, int mode)` | 56 | no | mode a1 (-1 = previous), remembers the previous mode, resets the mode timer |
| `0x00159360` | `int mm_Player_GetMode(Player *player)` | 8 | no | current mode |
| `0x00159370` | `void mm_Player_Draw(Player *player, int pass)` | 100 | no | player model instances |
| `0x001593E0` | `void mm_Player_Move(Player *player, float *move)` | 1896 | no | (provisional) walking/running with the pad and the analog stick |
| `0x00159B50` | `void mm_Player_Turn(Player *player, float *move)` | 428 | no | (provisional) turning with the pad |
| `0x00159D00` | `void mm_Player_Update(Player *player, CityScene *city)` | 1988 | yes | player per frame by mode: movement, camera, minimap, footsteps, placing items and homes |
| `0x0015A4D0` | `Player *mm_Player_DeletingDtor(Player *player, int16_t deleteFlag)` | 68 | yes | delete when the flag is > 0 |
| `0x0015A520` | `Player *mm_Player_ctor(Player *player)` | 92 | no | model 0x1F3CD4 at +0x88, cleared state |
| `0x0015DB30` | `int16_t mm_Person_FindBuildingInList(CityScene *city, int list, int a2, int a3, int t0, int16_t t1, int16_t t2)` | 444 | no | (provisional) a building of the wanted kind from a tile list near the person |
| `0x0015DCF0` | `uint32_t mm_Person_PlaceHomeObject(CityScene *city, MapPerson *person)` | 800 | no | (provisional) places an object for the person's home |
| `0x0015E010` | `uint16_t mm_Person_RandomTarget(CityScene *city, MapPerson *person, int a2, int a3)` | 436 | no | (provisional) random target along walkable tiles |
| `0x0015E1D0` | `uint16_t mm_Person_TargetWork(CityScene *city, MapPerson *person)` | 292 | no | (provisional) |
| `0x0015E300` | `int mm_Person_TileHasCompanion(CityScene *city, MapPerson *person, int tile)` | 212 | no | (provisional) people on the tile matching the person |
| `0x0015E3E0` | `uint16_t mm_Person_TargetVisit(CityScene *city, MapPerson *person)` | 276 | no | (provisional) |
| `0x0015E500` | `uint16_t mm_Person_TargetShop(CityScene *city, MapPerson *person, int a2, int a3)` | 456 | no | (provisional) |
| `0x0015E6D0` | `int16_t mm_Person_TargetHome(CityScene *city, MapPerson *person)` | 296 | no | (provisional) |
| `0x0015E800` | `int16_t mm_Person_ChooseDestination(CityScene *city, MapPerson *person, uint32_t *a2, int a3)` | 1492 | yes | (provisional) picks where a person goes next by schedule and needs, using the target helpers |
| `0x0015EE10` | `uint8_t mm_Schedule_QueryA(CityScene *city, uint32_t family, int member)` | 416 | yes | (provisional) schedule lookup for drawing |
| `0x0015EFB0` | `uint8_t mm_Schedule_QueryB(CityScene *city, uint32_t family, int member)` | 384 | yes | (provisional) schedule lookup used by the scripts |
| `0x0015F130` | `uint8_t mm_Schedule_GetActivity(CityScene *city, MapPerson *person)` | 520 | yes | (provisional) current activity of a person |
| `0x0015F340` | `uint8_t mm_Schedule_GetBuildingActivity(CityScene *city, MapPerson *person)` | 452 | yes | (provisional) current activity for a person and building |
| `0x0015F510` | `int mm_Schedule_PickEntry(int a0, int a1, int a2, int a3, int t0, int t1)` | 120 | no | (provisional) |
| `0x0015F590` | `uint8_t mm_Schedule_Evaluate(uint32_t family, int member)` | 828 | yes | (provisional) schedule entry for a family member at the current time (family record, time globals 0x1BD9A4..) |
| `0x0015F8D0` | `void mm_Schedule_Load(void)` | 68 | yes | loads DATA\BIN\SCHEDULE.BIN |
| `0x00162370` | `void mm_Stamina_LoadState(Stamina *stamina, McManager *mc)` | 20 | no | reads the 0xC-byte stamina record from the save file |
| `0x00162390` | `void mm_Stamina_SaveState(Stamina *stamina, McManager *mc)` | 20 | no | writes it |
| `0x001623B0` | `void mm_Stamina_Tick(Stamina *stamina, int a1)` | 184 | no | stamina +4 drops by stamina/100000 + 1 per tick (not below 0) |
| `0x00162470` | `void mm_Stamina_Eat(Stamina *stamina, uint8_t *goods)` | 124 | no | eating goods of categories 0x11-0x14 adds to stamina |
| `0x001624F0` | `int mm_Stamina_Zero(Stamina *stamina)` | 8 | no | returns 0 (8 bytes); only reached by the tail call in mm_WordsFn_StaminaZero |
| `0x00162500` | `int mm_Stamina_HungerLevel(Stamina *stamina)` | 64 | no | 0 below 100000, else a higher level (event "Hunger") |
| `0x00162540` | `void mm_Stamina_Init(Stamina *stamina)` | 24 | no | stamina 399999 |
| `0x00162560` | `void mm_Stamina_CreateGlobal(void)` | 60 | yes | 0x1BDBB8 = new stamina record |
| `0x0019AA80` | `void mm_Person_ExitBuilding(CityScene *city, MapPerson *person)` | 160 | no | if person a1 is inside a building: lowers the building's occupant count (+0x17) and marks the person outside |
| `0x0019AE50` | `int mm_Person_IsOnOpenTile(CityScene *city, uint16_t *actor)` | 212 | no | (provisional) the person's tile holds no building with type flag 0x40 |
| `0x0019AF30` | `int mm_Person_IsAtBuilding(CityScene *city, uint16_t *actor, int building)` | 232 | no | person a1 (6000 counts as yes) stands at building a2 |
| `0x0019B020` | `int mm_Person_FindIdleOfType(CityScene *city, int a1, uint32_t charType)` | 136 | no | first person of char type a2 whose home building is free (0x165660) |
| `0x0019B0B0` | `int mm_Person_IsNearBuilding(CityScene *city, float *position, MapObject *obj, int range)` | 424 | no | person position within a3 tiles of building a2's footprint |
| `0x0019B260` | `int mm_Player_DistanceToBuilding(Player *player, int building)` | 360 | no | distance in tiles from the player to building a1's footprint |
| `0x0019B3D0` | `float mm_Player_DistanceToPerson(Player *player, uint16_t *actor)` | 104 | no | distance from the player to person a1 (sqrt) |
| `0x0019B440` | `float mm_Player_DistanceToPoint(Player *player, float *point)` | 80 | no | distance from the player to point a1 |
| `0x0019B490` | `float mm_Person_Distance(int a0, uint16_t *actorA, uint16_t *actorB)` | 84 | no | distance between persons a1 and a2 |
