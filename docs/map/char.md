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

84 functions (verified 84). Blocks: 19 can reach a thread switch, 2 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0012E370` | `uint16_t mm_Path_PickRoadLink(CharClass *cls, CityScene *city, MapObject *target, MapObject *from, MapPerson *person)` | 1092 | no | the road link to take from one road block towards another: follows each link through bends, up to 9 blocks, nearest junction wins (was mm_Path_ScanLine) |
| `0x0012E7D0` | `uint16_t mm_Path_NextWaypoint(CharClass *cls, CityScene *city, MapPerson *person, int tile)` | 1588 | no | the person's next waypoint towards its building (+0x16), every 8th frame (else 0x4000): road entrance, shared links or mm_Path_PickRoadLink on roads, nearest road off them; sets +0x18 and +0x1A |
| `0x0012EE20` | `float mm_Path_TileDistance(int a0, uint32_t tileA, uint32_t tileB)` | 176 | no | (provisional) VU0 distance test between tiles a1 and a2 |
| `0x0012EED0` | `void mm_CharType_Slot18(CharClass *cls)` | 8 | no | base vtable slot +0x18: empty |
| `0x0012EEE0` | `void mm_CharType_Draw2(CharClass *cls)` | 8 | no | base slot +0x14: empty |
| `0x0012EEF0` | `void mm_CharType_Draw(CharClass *cls)` | 8 | no | base slot +0x10: empty |
| `0x0012EF00` | `int mm_CharType_Update(CharClass *cls)` | 8 | no | base slot +0xC: returns 0 |
| `0x0012EF10` | `CharClass *mm_CharType_dtor(CharClass *cls, int16_t deleteFlag)` | 76 | yes | base vtable 0x1BCE70; delete when the flag is > 0 |
| `0x0012EF60` | `CharClass *mm_CharType_ctor(CharClass *cls, uint8_t *record)` | 180 | no | base vtable 0x1BCE70; four model parts from the table 0x1F3B90 by record bytes 8/9/10/14, sizes from bytes 11-13, ids from bytes 2/4/0x14 |
| `0x0012F020` | `void mm_Char2_UpdateSpeedA(CharClass *cls, CityScene *city, MapPerson *vehicle, MapObject *obj, int dir, float dx, float dz)` | 3808 | no | a vehicle's speed (+0x2C) for its next step: brakes behind vehicles heading the same way, at junctions against the lights (trafficPhase), gains on straight road; direction 4 tests the tiles around (mm_Char_CheckNeighbourTile) |
| `0x0012FF00` | `int mm_Char_CheckNeighbourTile(CharClass *cls, CityScene *city, MapPerson *person, int x, int z)` | 440 | no | (provisional, was mm_Path_CheckNeighbour) neighbour tile test shared by both probe functions (tables 0x1AEFF0/0x1AF020) |
| `0x001300C0` | `void mm_Char2_UpdateSpeedB(CharClass *cls, CityScene *city, MapPerson *vehicle, MapObject *obj, int dir, float dx, float dz)` | 3808 | no | mm_Char2_UpdateSpeedA with gentler rules (brake 80, slow 20, follow at a 20th, gain 10), for movement mode B |
| `0x00130FA0` | `void mm_Char2_MoveA(CharClass *cls, CityScene *city, MapPerson *vehicle, MapObject *obj, int tile)` | 2300 | no | class-2 movement mode A: on one frame in four steers for the waypoint or, on road, back into its lane (turning up to 0.16), sets the speed (mm_Char2_UpdateSpeedA), then moves along its heading and leaves the old tile's list |
| `0x001318A0` | `void mm_Char2_MoveB(CharClass *cls, CityScene *city, MapPerson *vehicle, MapObject *obj, int tile)` | 2280 | no | class-2 movement mode B: mode A steering every frame, turning up to 0.04, with mm_Char2_UpdateSpeedB |
| `0x00132190` | `void mm_Char2_Draw(CharClass *cls, CityScene *city, MapPerson *person, Vec4 *colour, int flags, Vec4 *place)` | 612 | no | class 2 slot +0x10: instance of the character on the terrain (height from 0x1415E0), within the draw range (0x1BD980) |
| `0x00132400` | `void mm_Char2_Update(CharClass *cls, CityScene *city, MapPerson *person, uint32_t timeFlags)` | 1048 | yes | class 2 slot +0xC: moves the character (mode A or B) and updates its tile |
| `0x00132820` | `CharClass *mm_Char2_dtor(CharClass *cls, int16_t deleteFlag)` | 96 | yes | vtable 0x1BCE90, then the base destructor |
| `0x00132880` | `CharClass *mm_Char2_ctor(CharClass *cls, uint8_t *record)` | 52 | no | base constructor, vtable 0x1BCE90 |
| `0x001328C0` | `void mm_CharType_LoadAll(void)` | 416 | yes | loads DATA\BIN\CHARTYPE.BIN and creates one class object per 0x18-byte record (class 0 base, 1 person, 2 class 2) in the table at gp 0x1BDA3C |
| `0x00132A70` | `void mm_Person_CheckBuilding(CharClass *cls, CityScene *city, MapPerson *person)` | 244 | yes | (provisional) person vs. its target building record: flags 0x20/0x200/4, the building's state byte +0x27 |
| `0x00132B70` | `void mm_Person_TickCounters(CharClass *cls, CityScene *city, MapPerson *person)` | 300 | no | friendship level tick (+0x2F), +0x25 countdown, and with city flag 4 the family a resident introduces phones to ask about moving in (up to 4 calls) |
| `0x00132CA0` | `int mm_Person_ProbeTile(CharClass *cls, CityScene *city, MapPerson *person, uint32_t tile)` | 248 | no | (provisional) tests the tile a person would step onto |
| `0x00132DA0` | `void mm_Person_Walk(CharClass *cls, CityScene *city, MapPerson *person, MapObject *obj, int tile)` | 3528 | no | a person's walk each frame: aims at the building on its waypoint (or the followed person), keeps to a pavement lane on road, steps aside when the tile ahead is taken, turns and steps at its class's walking speed |
| `0x00133B70` | `void mm_Person_UpdatePose(CharClass *cls, CityScene *city, MapPerson *person, int a3, int tile)` | 780 | no | (provisional) person's matrix from heading and terrain height |
| `0x00133E80` | `void mm_Person_Draw2(CharClass *cls, CityScene *city, MapPerson *person, uint32_t family, uint32_t member, Vec4 *matrix, int t2, int32_t *mesh, float *phase)` | 700 | yes | class 1 slot +0x14: (provisional) extra drawing (shadow/marker) for a person |
| `0x00134140` | `void mm_CharType_TickSlots(void)` | 92 | no | counts down the 25 character type slot timers (0x1C5310) and frees a slot (0x1C5110 = 6000) when its timer reaches 0 |
| `0x001341A0` | `void mm_CharType_ClearSlots(void)` | 184 | no | frees all 25 character type slots (0x1C5110 = 6000) and clears their timers (0x1C5310) |
| `0x00134260` | `void mm_Person_TurnToPlayer(CharClass *cls, CityScene *city, MapPerson *person)` | 896 | no | turns a person's model part (nodes 0 and 3) to face the player, blended 9:1 with the direction kept in its slot (0x1C5110/0x1C5180/0x1C5310) (was mm_Person_BuildPose) |
| `0x001345E0` | `void mm_Person_Draw(CharClass *cls, CityScene *city, MapPerson *person, int a3, int t0, int32_t *t1, int t2)` | 1088 | yes | class 1 slot +0x10: person instances on the terrain, within the draw range |
| `0x00134A20` | `void mm_Person_Update(CharClass *cls, CityScene *city, MapPerson *person, uint32_t timeFlags)` | 3032 | yes | class 1 slot +0xC: a person's behaviour per tick: target building, walking, waiting, random choices |
| `0x00135600` | `CharClass *mm_Person_dtor(CharClass *cls, int16_t deleteFlag)` | 96 | yes | vtable 0x1BCEB0, then the base destructor |
| `0x00135660` | `PersonClass *mm_Person_ctor(PersonClass *cls, uint8_t *record)` | 136 | no | base constructor, vtable 0x1BCEB0, walking speed +0x24 = record u16 +6 / 1e6 |
| `0x0013C010` | `void mm_StatusByte_IncHighLevel(uint8_t *status)` | 64 | no | (provisional) level 12..13 -> +1 |
| `0x0013C050` | `void mm_StatusByte_ClearMid2(uint8_t *status)` | 48 | no | clears bits 2-3 when set |
| `0x0013C080` | `void mm_StatusByte_IncLevel(uint8_t *status)` | 108 | no | (provisional) level +1 below 12; above 1 clears bits 0-1 |
| `0x001439D0` | `int mm_Player_CollideObjects(CityMap *map, float *position, int a2, float radius)` | 1048 | indirect | (provisional) objects on the 3 x 3 tiles around the player, collision push |
| `0x00143DF0` | `int mm_Player_NearestObject(CityMap *map, float *position, uint32_t *hit, float *distance, float radius)` | 1144 | indirect | (provisional) objects on the 3 x 3 tiles around the player, nearest hit distance (100 = none) |
| `0x00157D80` | `void mm_Player_LoadState(Player *player, McManager *mc)` | 20 | no | reads 0x38 bytes from the save file |
| `0x00157DA0` | `void mm_Player_SaveState(Player *player, McManager *mc)` | 20 | no | writes 0x38 bytes |
| `0x00157DC0` | `void mm_Player_GetMatrix(Player *player, Vec4 *matrix)` | 40 | no | the player model's matrix (object +0x88) |
| `0x00157DF0` | `void mm_Player_PlayFootstep(Player *player, CityScene *city)` | 528 | no | footsteps on frames 10/30 of the walking animations (4-7) and frame 10 of 8-11, on the ground only: variant by the object on the tile, the water level and snow |
| `0x00158000` | `void mm_Player_UpdateAction(Player *player, Vec4 *move, CityScene *city)` | 880 | no | the player's animation from its movement, pad direction and height (idle 0-2, moving 4-15), advances its frame; sound 5 while walking with pad 0x40 |
| `0x00158370` | `void mm_Player_PickTarget(Player *player, CityScene *city)` | 1704 | no | keeps or drops the player's target (gone, or 30 frames off the heading by pi/8), then steps a ray along the heading: a flag-0x40 building it hits, or while walking the nearest resident within 0.3 of the ray; sets the city cursor |
| `0x00159320` | `void mm_Player_SetMode(Player *player, int mode)` | 56 | no | mode a1 (-1 = previous), remembers the previous mode, resets the mode timer |
| `0x00159360` | `int mm_Player_GetMode(Player *player)` | 8 | no | current mode |
| `0x00159370` | `void mm_Player_Draw(Player *player, CityScene *city)` | 100 | no | player model instances |
| `0x001593E0` | `void mm_Player_Move(Player *player, Vec4 *move)` | 1896 | no | the player's grid steps: the pad starts a step (forward/back/sideways 2/speed frames, turns 32 frames); idle, eases onto the block middle and a quarter turn; the step moves the move vector or turns pi/64 |
| `0x00159B50` | `void mm_Player_Turn(Player *player, float *move)` | 428 | no | (provisional) turning with the pad |
| `0x00159D00` | `void mm_Player_Update(Player *player, CityScene *city)` | 1988 | yes | player per frame by mode: movement, camera, minimap, footsteps, placing items and homes |
| `0x0015A4D0` | `Player *mm_Player_DeletingDtor(Player *player, int16_t deleteFlag)` | 68 | yes | delete when the flag is > 0 |
| `0x0015A520` | `Player *mm_Player_ctor(Player *player)` | 92 | no | model 0x1F3CD4 at +0x88, cleared state |
| `0x0015DB30` | `int16_t mm_Person_FindBuildingInList(CityScene *city, int list, int x, int z, int mode, int16_t skipA, int16_t skipB)` | 444 | no | (provisional) a building of the wanted kind from a tile list near the person |
| `0x0015DCF0` | `uint32_t mm_Person_PlaceHomeObject(CityScene *city, MapPerson *person)` | 800 | no | a resident's home (kept in its target field): the current one, a home of the family near work with the claim meter below 60, or a new home of the work type's home type (+0x13A) placed on flat ground nearby; else the workplace |
| `0x0015E010` | `uint16_t mm_Person_RandomTarget(CityScene *city, MapPerson *person, int a2, int a3)` | 436 | no | (provisional) random target along walkable tiles |
| `0x0015E1D0` | `uint16_t mm_Person_TargetWork(CityScene *city, MapPerson *person)` | 292 | no | (provisional) |
| `0x0015E300` | `int mm_Person_TileHasCompanion(CityScene *city, MapPerson *person, MapObject *building)` | 212 | no | (provisional) people on the tile matching the person |
| `0x0015E3E0` | `uint16_t mm_Person_TargetVisit(CityScene *city, MapPerson *person)` | 276 | no | (provisional) |
| `0x0015E500` | `uint16_t mm_Person_TargetShop(CityScene *city, MapPerson *person, int a2, int a3)` | 456 | no | (provisional) |
| `0x0015E6D0` | `int16_t mm_Person_TargetHome(CityScene *city, MapPerson *person)` | 296 | no | (provisional) |
| `0x0015E800` | `int16_t mm_Person_ChooseDestination(CityScene *city, MapPerson *person, uint32_t *activity, int exclude)` | 1492 | yes | (provisional) picks where a person goes next by schedule and needs, using the target helpers |
| `0x0015EE10` | `uint8_t mm_Schedule_QueryA(CityScene *city, uint32_t family, int member)` | 416 | yes | (provisional) schedule lookup for drawing |
| `0x0015EFB0` | `uint8_t mm_Schedule_QueryB(CityScene *city, uint32_t family, int member)` | 384 | yes | (provisional) schedule lookup used by the scripts |
| `0x0015F130` | `uint8_t mm_Schedule_GetActivity(CityScene *city, MapPerson *person)` | 520 | yes | (provisional) current activity of a person |
| `0x0015F340` | `uint8_t mm_Schedule_GetBuildingActivity(CityScene *city, MapPerson *person)` | 452 | yes | (provisional) current activity for a person and building |
| `0x0015F510` | `int mm_Schedule_PickEntry(int a0, int a1, int a2, int a3, int t0, int t1)` | 120 | no | (provisional) |
| `0x0015F590` | `uint8_t mm_Schedule_Evaluate(uint32_t family, int member)` | 828 | yes | (provisional) schedule entry for a family member at the current time (family record, time globals 0x1BD9A4..) |
| `0x0015F8D0` | `void mm_Schedule_Load(void)` | 68 | yes | loads DATA\BIN\SCHEDULE.BIN |
| `0x00162370` | `void mm_Stamina_LoadState(Stamina *stamina, McManager *mc)` | 20 | no | reads the 0xC-byte stamina record from the save file |
| `0x00162390` | `void mm_Stamina_SaveState(Stamina *stamina, McManager *mc)` | 20 | no | writes it |
| `0x001623B0` | `void mm_Stamina_Tick(Stamina *stamina, CityScene *city)` | 184 | no | stamina +4 drops by stamina/100000 + 1 per tick (not below 0) |
| `0x00162470` | `void mm_Stamina_Eat(Stamina *stamina, uint8_t *goods)` | 124 | no | eating goods of categories 0x11-0x14 adds to stamina |
| `0x001624F0` | `int mm_Stamina_Zero(Stamina *stamina)` | 8 | no | returns 0 (8 bytes); only reached by the tail call in mm_WordsFn_StaminaZero |
| `0x00162500` | `int mm_Stamina_HungerLevel(Stamina *stamina)` | 64 | no | 0 below 100000, else a higher level (event "Hunger") |
| `0x00162540` | `void mm_Stamina_Init(Stamina *stamina)` | 24 | no | stamina 399999 |
| `0x00162560` | `void mm_Stamina_CreateGlobal(void)` | 60 | yes | 0x1BDBB8 = new stamina record |
| `0x0019AA80` | `void mm_Person_ExitBuilding(CityScene *city, MapPerson *person)` | 160 | no | if person a1 is inside a building: lowers the building's occupant count (+0x17) and marks the person outside |
| `0x0019AE50` | `int mm_Person_IsOnOpenTile(CityScene *city, ActorRef *actor)` | 212 | no | (provisional) the person's tile holds no building with type flag 0x40 |
| `0x0019AF30` | `int mm_Person_IsAtBuilding(CityScene *city, ActorRef *actor, ActorRef *target)` | 232 | no | person a1 (6000 counts as yes) stands at building a2 |
| `0x0019B020` | `MapPerson *mm_Person_FindIdleOfType(CityScene *city, MapObject *building, uint32_t charType)` | 136 | no | first person of char type a2 whose home building is free (0x165660) |
| `0x0019B0B0` | `int mm_Person_IsNearBuilding(CityScene *city, MapPerson *person, MapObject *obj, int range)` | 424 | no | person position within a3 tiles of building a2's footprint |
| `0x0019B260` | `int mm_Player_DistanceToBuilding(CityScene *city, ActorRef *actor)` | 360 | no | tiles between the player and the building of actor a1 (footprint aware); a0 is the city |
| `0x0019B3D0` | `float mm_Player_DistanceToPerson(CityScene *city, ActorRef *actor)` | 104 | no | distance from the player to person a1 (sqrt) |
| `0x0019B440` | `float mm_Player_DistanceToPoint(CityScene *city, float *point)` | 80 | no | distance from the player to point a1 |
| `0x0019B490` | `float mm_Person_Distance(CityScene *city, ActorRef *actorA, ActorRef *actorB)` | 84 | no | distance between persons a1 and a2 |
