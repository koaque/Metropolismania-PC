# city

The city scene and the building simulation.

City scene (`mm_CityScene_*`, object at gp `0x001BDD00`; it is also the `this` of most city code):

| Offset | Meaning |
|---|---|
| `+0x10` .. `+0x24` | mode stack (`mm_CityScene_PushMode`/`PopMode`), depth at `+0x24` |
| `+0x3C` | talk session (`talk`) |
| `+0x48` | laptop, `+0x4C` camera, `+0x50` map object, `+0x54` applicants, `+0x58` player |
| `+0x5C` | vacancies, `+0x60` bag, `+0x64` stamina, `+0x68` event manager, `+0x6C` phone calls |
| `+0x84` | clock in seconds (30 per tick before 6:00, then 3) |
| `+0x88` | month count: one month per in-game day, so the month is `+0x88 mod 12` |
| `+0x8C` | approval rate (-99..120), moved toward the pending change at `+0xBC` |
| `+0x90`, `+0x94`, `+0x98` | weather today, tomorrow, yesterday (0 clear, 2 rain, 3 snow) |
| `+0xA0`, `+0xA8` | placement scores (decaying averages, `mm_CityScene_RecordPlacementScore`) |
| `+0xB4` | flags: 2 holiday month (months 0, 3, 7, 10), 4 popular city (approval 118 or more), 0x40 pad focus returns after a talk |
| `+0xCC` | screen fade counter (`mm_CityScene_StartFade`) |

Building types (`mm_BldgType_*` and the category classes `mm_Bldg<Category>_*`): one object per type in the table at
gp `0x001BDBF8`, built from `DATA\BIN\STRTYPE.BIN` (0x9C-byte records, category at byte `+0x20`). Type fields:
`+0x110` name, `+0x134` kind, `+0x136` flags (`0x40` = people live or work there), `+0x13C`/`+0x13E` opening and closing times in minutes (closed in holiday months, city flag 2, for types with flag `0x2000`),
`+0x142` footprint size, `+0x145` category, `+0x147`/`+0x148` claim indexes, vtable at `+0x15C`. Vtable slots:
`+8` dtor, `+0xC` InitRecord, `+0x10` Tick, `+0x14` Draw, `+0x18` DrawPeople, `+0x24` IsOpen, `+0x28`/`+0x2C`
collision, `+0x34` DrawAnim. Categories and classes: 0 base (none, prohibited, rubble), 1 Wasteland `0x1BD3C0`,
2 Leveled land `0x1BD240`, 3 Road `0x1BD340`, 4 Field `0x1BD140`, 5 Outside (base), 6 Business `0x1BD1C0`,
7 Office `0x1BD080`, 8 Housing `0x1BD280`, 9 Port `0x1BD180`, 10 My Home `0x1BD2C0`, 11 Amusement Park
`0x1BD040`, 12 Ranch `0x1BD0C0`, 13 Pig Farm `0x1BD300`, 14 Small Goods `0x1BD380`, 15 Keep Out `0x1BD200`;
`0x1BD100` is the shared Building class of 6-13.

Claims (`mm_BldgClaims_*`) evaluate what a building's residents want (`claim`). While a claim is unmet the object's
claim meter (object `+0x10`) rises; `mm_ClaimMeter_Level` compares it with the thresholds at `*0x001BDA50`. A met
claim sets object flag 8 (approval +3), a maxed meter (above 0xFE) flag 0x40. The wants list (`mm_Wants_*`,
0x504 bytes at the start of the wants window, CityScene `+0x5C`) records the requests residents voice in
conversations (person, workplace, family, member, claim slot) and the entries events hold; up to 64 entries in
256 words. Applicants (`mm_Applicants_*`, list at city `+0x54`, 0x14-byte entries) are the families waiting to
move in.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

183 functions (named 168, typed 15). Blocks: 55 can reach a thread switch, 7 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Name | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001357F0` | `mm_ClaimIcon_GetColor` | 100 | no | colour of a claim icon: id a0 (bit 15 selects the second half), level a1 (0 = white 0x1B26C0) |
| `0x0013BDD0` | `mm_StatusByte_Low2` | 12 | no | bits 0-1 of *a0 |
| `0x0013BDE0` | `mm_StatusByte_Mid2` | 16 | no | bits 2-3 of *a0 |
| `0x0013BDF0` | `mm_StatusByte_LevelAbove13` | 36 | no | level (bits 4-7) > 13 |
| `0x0013BED0` | `mm_StatusByte_Advance` | 144 | no | the same advance on a status byte pointer |
| `0x0013BFD0` | `mm_StatusByte_OnLevel12` | 52 | no | friendship gain when the byte's level is above 11 |
| `0x00150190` | `mm_Wants_RemoveEventHold` | 272 | no | removes the entry an event holds for building type a1 with value a2 (events ending) |
| `0x001502A0` | `mm_Wants_AddEventHold` | 172 | no | one-word entry held by an event for building type a1 (needs a free entry and word) |
| `0x00150350` | `mm_Wants_CheckEntry` | 208 | no | a want's state: 0 open, 1 the building's claim is met or maxed (flags 0x48), 2 stale (family gone or claim changed), 3 claim meter above the high threshold |
| `0x00150420` | `mm_Wants_Find` | 292 | no | next open resident want from iterator *a5: claim slot a2, object a3, person a4 |
| `0x00150550` | `mm_Wants_AddForPerson` | 256 | no | records person a2's want (claim slot a3) when an entry and four words are free |
| `0x00150650` | `mm_Wants_LoadState` | 20 | no | reads the 0x504-byte list from the save file |
| `0x00150670` | `mm_Wants_SaveState` | 20 | no | writes it |
| `0x00150690` | `mm_Wants_Prune` | 672 | no | every 120 frames drops stale wants (state 2) and compacts the list |
| `0x00150940` | `mm_Wants_HasRoom` | 52 | no | entry count below 0x40 and at least a1 words left of 0x100 |
| `0x00150980` | `mm_Wants_ctor` | 16 | no | zeroes the two counters |
| `0x001550A0` | `mm_Applicants_CountActive` | 68 | no | applicants whose state (+2) is below 7 |
| `0x001550F0` | `mm_Applicants_LoadState` | 20 | no | reads 0x284 bytes from the save file |
| `0x00155110` | `mm_Applicants_SaveState` | 20 | no | writes 0x284 bytes |
| `0x00155130` | `mm_Applicants_Find` | 60 | no | index of the applicant for family a1 |
| `0x00155170` | `mm_Applicants_FindFrom` | 100 | no | next applicant from iterator *a1 whose state (+2) is a2 (32 entries) |
| `0x001551E0` | `mm_Applicants_Add` | 184 | yes | adds family a1 (max 32 entries of 0x14 bytes) |
| `0x001552A0` | `mm_Applicants_Clear` | 132 | no | empties the list |
| `0x00156C50` | `mm_Applicants_AnyWaiting` | 52 | no | (provisional) 1 if an applicant entry has state 2 |
| `0x00156C90` | `mm_Applicants_RemoveFamily` | 80 | no | removes the entry for family a1 |
| `0x00156CE0` | `mm_Applicants_RemoveAt` | 148 | no | removes entry a1 and shifts the rest |
| `0x00156D80` | `mm_Applicants_AssignToBuilding` | 512 | yes | places the family's object and assigns its home building, then removes the entry |
| `0x00156F80` | `mm_Applicants_StartPlacing` | 260 | yes | placement mode: state 3 -> 6, highlights the footprint, takes pad focus, clears the notice |
| `0x00157090` | `mm_Applicants_AddAndShow` | 68 | yes | mm_Applicants_Add, selected when a3 != 0 |
| `0x001570E0` | `mm_Applicants_GetSelected` | 48 | no | selected entry or 0 |
| `0x00158A20` | `mm_Placement_FindSpot` | 748 | no | tries offsets around (a5, a6) until mm_Map_CanPlace returns 2; writes the footprint rectangle |
| `0x00158D10` | `mm_Player_PlaceBagItem` | 756 | yes | placing an item from the bag: preview, confirm (button 0x40), place object, finish use |
| `0x00159010` | `mm_Player_PlaceForApplicant` | 780 | yes | placement preview for the selected applicant: footprint highlight green or red, then mm_Placement_FindSpot |
| `0x0015A580` | `mm_CityAssets_LoadMyHome` | 324 | yes | async load step for DATA\BIN\MYHOME.A3L |
| `0x0015A6D0` | `mm_CityAssets_LoadChars` | 468 | yes | async load step for DATA\A3D\CHAR.A3D |
| `0x0015A8B0` | `mm_CityAssets_BuildCharParts` | 480 | no | fills the character part table 0x1F3B90 from CHAR.A3D |
| `0x0015AA90` | `mm_CityAssets_LoadCharObjs` | 448 | yes | async load step for DATA\A3D\CHAROBJ.A3D |
| `0x0015AC50` | `mm_CityAssets_LoadObjects` | 448 | yes | async load step for DATA\A3D\OBJECT.A3D |
| `0x0015AE10` | `mm_CityAssets_LoadStep` | 356 | yes | state machine over the asset loads; returns when done |
| `0x0015FD70` | `mm_Markers_Draw` | 184 | no | (provisional) model 0x1F3B28 with up to four nodes placed by the city update |
| `0x0015FF10` | `mm_Markers_dtor` | 76 | yes | (provisional) vtable 0x1BCFC8 at +0x128 |
| `0x0015FF60` | `mm_Markers_ctor` | 216 | no | (provisional) vtable 0x1BCFC8 at +0x128, models 0x1F3B28/0x1F3B08, nodes 1-3 |
| `0x00163690` | `mm_Collide_SegmentA` | 436 | no | (provisional) segment test against building geometry |
| `0x00163850` | `mm_Collide_SegmentB` | 444 | no | (provisional) second segment test |
| `0x00163A10` | `mm_BldgType_Slot34` | 8 | no | base slot +0x34 |
| `0x00163A20` | `mm_BldgType_IsOpenAt` | 184 | no | open between +0x13C and +0x13E (minutes of the day, a2 shifts the time); closed in holiday months (city flag 2) for types with flag 0x2000 |
| `0x00163AE0` | `mm_Clock_AddWrap` | 52 | no | a1 + a2 mod 3600, wrapped at 86400 (seconds per day) |
| `0x00163B20` | `mm_BldgType_IsMyHome` | 32 | no | type id/kind (+0x134) in 5..8, the My Home types |
| `0x00163B40` | `mm_BldgType_DrawOccupants` | 256 | indirect | (provisional) draws people at a building |
| `0x00163C40` | `mm_BldgType_OccupantMatrix` | 292 | no | (provisional) matrix for an occupant |
| `0x00163D70` | `mm_BldgType_DrawOccupant` | 400 | no | (provisional) one occupant on its tile |
| `0x00163F00` | `mm_BldgType_FindEntrance` | 344 | no | (provisional) entrance tile next to a building (walkable flag 0x10, tables 0x1B13F0/0x1B1400) |
| `0x00164060` | `mm_BldgType_Slot30` | 8 | no | base slot +0x30 |
| `0x00164070` | `mm_BldgType_CollideA` | 8 | no | base slot +0x2C |
| `0x00164080` | `mm_BldgType_CollideB` | 8 | no | base slot +0x28 |
| `0x00164090` | `mm_BldgType_Slot1C` | 8 | no | base slot +0x1C |
| `0x001640A0` | `mm_BldgType_Slot20` | 8 | no | base slot +0x20 |
| `0x001640B0` | `mm_BldgType_DrawOccupants2` | 356 | indirect | (provisional) |
| `0x00164220` | `mm_BldgType_Draw` | 8 | no | base slot +0x14 |
| `0x00164230` | `mm_BldgType_Tick` | 8 | no | base slot +0x10 |
| `0x00164240` | `mm_BldgType_InitRecord` | 8 | no | base slot +0xC |
| `0x00164280` | `mm_BldgType_BaseDtor` | 136 | yes | vtable 0x1BD000; releases the shared name data when the last type goes |
| `0x00164310` | `mm_BldgType_BaseCtor` | 1068 | yes | base constructor: shared DATA\BIN\POSTNAME.BIN, model and parameters |
| `0x00164770` | `mm_BldgAmusementPark_dtor` | 128 | yes | category 11 (Amusement Park), vtable 0x1BD040 |
| `0x001647F0` | `mm_BldgAmusementPark_DrawAnim` | 232 | no | swaying ride animation between the opening hours |
| `0x001648E0` | `mm_BldgAmusementPark_Draw` | 524 | no | instances for the park |
| `0x00164AF0` | `mm_BldgAmusementPark_ctor` | 64 | yes | Building constructor, vtable 0x1BD040 |
| `0x00164B30` | `mm_BldgBusiness_dtor` | 112 | yes | category 6 (factories, shops), vtable 0x1BD1C0 |
| `0x00164BA0` | `mm_BldgOffice_dtor` | 128 | yes | category 7 (offices), vtable 0x1BD080 |
| `0x00164C20` | `mm_BldgOffice_CollideA` | 224 | no | slot +0x2C |
| `0x00164D00` | `mm_BldgOffice_CollideB` | 224 | no | slot +0x28 |
| `0x00164DE0` | `mm_BldgOffice_Draw` | 852 | no | slot +0x14 |
| `0x00165140` | `mm_BldgOffice_Tick` | 132 | no | Building tick plus a check of the tile's first object |
| `0x001651D0` | `mm_BldgOffice_ctor` | 72 | yes | Building constructor, vtable 0x1BD080, +0x160 from record byte 0x92 |
| `0x00165220` | `mm_BldgRanch_dtor` | 128 | yes | category 12 (Ranch), vtable 0x1BD0C0 |
| `0x001652A0` | `mm_BldgRanch_DrawAnim` | 232 | no | animals' animation between the opening hours |
| `0x00165390` | `mm_BldgRanch_Draw` | 524 | indirect | instances for the ranch |
| `0x001655A0` | `mm_BldgRanch_ctor` | 88 | yes | Building constructor, vtable 0x1BD0C0, models from 0x1F3B50.. |
| `0x00165600` | `mm_Bldg_dtor` | 96 | yes | intermediate Building class, vtable 0x1BD100 |
| `0x00165660` | `mm_ClaimMeter_Level` | 60 | no | claim meter a0 against the thresholds at *0x1BDA50: 0 below low, 1 between, 2 above high |
| `0x001656A0` | `mm_BldgClaims_EvalA` | 932 | no | (provisional) claim evaluation part A (CLAIM.BIN) |
| `0x00165A50` | `mm_BldgClaims_EvalB` | 504 | no | (provisional) part B, random |
| `0x00165C50` | `mm_BldgClaims_CountNearby` | 552 | no | (provisional) counts matching buildings in the claim's tile lists |
| `0x00165E80` | `mm_BldgClaims_EvalC` | 712 | no | checks whether the claim is met (nothing missing nearby): flag 8, approval +3, sound and minimap mark; else meter handling |
| `0x00166150` | `mm_BldgClaims_EvalD` | 1768 | no | (provisional) part D |
| `0x00166840` | `mm_BldgClaims_Evaluate` | 628 | no | per building: claim check (part C), meter rise by the rate, part D above the low threshold, flag 0x40 above 0xFE |
| `0x00166AC0` | `mm_BldgClaims_DrawIcon` | 372 | no | claim icon colour from the meter level (1 slow pulse, 2 fast pulse) |
| `0x00166C40` | `mm_Bldg_InitRecordBase` | 12 | no | record +0x12 = 0x18 |
| `0x00166C50` | `mm_Bldg_ctor` | 52 | yes | base constructor, vtable 0x1BD100 |
| `0x00166C90` | `mm_BldgField_dtor` | 96 | yes | category 4 (crop fields), vtable 0x1BD140 |
| `0x00166CF0` | `mm_BldgField_Slot1C` | 84 | no | slot +0x1C |
| `0x00166D50` | `mm_BldgField_DrawPeople` | 472 | indirect | slot +0x18: farm workers |
| `0x00166F30` | `mm_BldgField_Draw` | 564 | no | crop instances |
| `0x00167170` | `mm_BldgField_Tick` | 408 | no | marks residents on the minimap |
| `0x00167310` | `mm_BldgField_ctor` | 348 | yes | base constructor, vtable 0x1BD140, crop parameters from the record |
| `0x00167470` | `mm_BldgPort_dtor` | 128 | yes | category 9 (fishing ports), vtable 0x1BD180 |
| `0x001674F0` | `mm_BldgPort_CollideA` | 84 | no | slot +0x2C |
| `0x00167550` | `mm_BldgPort_CollideB` | 84 | no | slot +0x28 |
| `0x001675B0` | `mm_BldgPort_Draw` | 504 | no | slot +0x14 |
| `0x001677B0` | `mm_BldgPort_Tick` | 236 | no | Building tick plus terrain height changes at the shore |
| `0x001678A0` | `mm_BldgPort_ctor` | 52 | yes | Building constructor, vtable 0x1BD180 |
| `0x001678E0` | `mm_Bldg_IsOpen` | 72 | no | slot +0x24: opening hours for a building or its type |
| `0x00167930` | `mm_Bldg_Slot30` | 8 | no | slot +0x30 |
| `0x00167940` | `mm_Bldg_CollideA` | 64 | no | slot +0x2C |
| `0x00167980` | `mm_Bldg_CollideB` | 64 | no | slot +0x28 |
| `0x001679C0` | `mm_BldgType_FindEntrance2` | 368 | no | whether a building of flag 8 lies within 3 tiles beyond side a5 (stops at walkable tiles) |
| `0x00167B40` | `mm_Bldg_DrawPeople` | 1452 | indirect | slot +0x18: people at the building |
| `0x00168100` | `mm_BldgBusiness_Draw` | 532 | no | slot +0x14 for Business and My Home: instances and claim icon |
| `0x00168320` | `mm_Bldg_Tick` | 1204 | no | slot +0x10: entrance, minimap marks, terrain under the footprint, claims |
| `0x001687E0` | `mm_Bldg_InitRecord` | 44 | no | slot +0xC: base init, record +0x14 = 0x4000 |
| `0x00168810` | `mm_BldgBusiness_ctor` | 52 | yes | Building constructor, vtable 0x1BD1C0 |
| `0x00168850` | `mm_BldgKeepOut_dtor` | 96 | yes | category 15 (Keep Out), vtable 0x1BD200 |
| `0x001688B0` | `mm_BldgKeepOut_Draw` | 344 | no | fence instances |
| `0x00168A10` | `mm_BldgKeepOut_ctor` | 52 | yes | base constructor, vtable 0x1BD200 |
| `0x00168A50` | `mm_BldgLeveled_dtor` | 96 | yes | category 2 (leveled land), vtable 0x1BD240 |
| `0x00168AB0` | `mm_BldgLeveled_CheckArea` | 1152 | no | (provisional) tests the 4 surrounding tile objects |
| `0x00168F30` | `mm_BldgLeveled_Draw` | 132 | no | instances when the record flag 1 is set |
| `0x00168FC0` | `mm_BldgLeveled_Tick` | 1128 | no | (provisional) builds on leveled land for waiting applicants: levels the terrain, places the building |
| `0x00169430` | `mm_BldgLeveled_ctor` | 72 | yes | base constructor, vtable 0x1BD240, +0x160 from record byte 0x90 |
| `0x00169480` | `mm_BldgType_CallAll` | 120 | indirect | calls a virtual method on every building type |
| `0x00169500` | `mm_BldgType_Create` | 716 | yes | building type object for a STRTYPE.BIN record by its category (+0x20) |
| `0x001697D0` | `mm_BldgType_DeleteAll` | 140 | yes | deletes every building type object |
| `0x00169860` | `mm_BldgType_LoadAll` | 248 | yes | loads DATA\BIN\STRTYPE.BIN (0x9C-byte records, 281 types) and creates the type objects |
| `0x00169960` | `mm_BldgHousing_dtor` | 128 | yes | category 8 (housing), vtable 0x1BD280 |
| `0x001699E0` | `mm_BldgHousing_CollideA` | 196 | no | slot +0x2C |
| `0x00169AB0` | `mm_BldgHousing_CollideB` | 196 | no | slot +0x28 |
| `0x00169B80` | `mm_BldgHousing_Draw` | 928 | no | slot +0x14 |
| `0x00169F20` | `mm_BldgHousing_UnitAt` | 372 | no | (provisional) unit index of a tile within a housing block |
| `0x0016A0A0` | `mm_BldgHousing_CountUnits` | 756 | no | (provisional) recursive walk over connected housing tiles |
| `0x0016A3A0` | `mm_BldgHousing_Tick` | 160 | no | Building tick plus unit counting |
| `0x0016A440` | `mm_BldgHousing_InitRecord` | 80 | no | Building init; variant +0x1A = (x/2) mod 3 |
| `0x0016A490` | `mm_BldgHousing_ctor` | 52 | yes | Building constructor, vtable 0x1BD280 |
| `0x0016A4D0` | `mm_BldgMyHome_dtor` | 128 | yes | category 10 (My Home), vtable 0x1BD2C0 |
| `0x0016A550` | `mm_BldgMyHome_Tick` | 376 | no | (provisional) Building tick for the player's home |
| `0x0016A6D0` | `mm_BldgMyHome_ctor` | 52 | yes | Building constructor, vtable 0x1BD2C0 |
| `0x0016A710` | `mm_BldgPigFarm_dtor` | 128 | yes | category 13 (Pig Farm), vtable 0x1BD300 |
| `0x0016A790` | `mm_BldgPigFarm_Draw` | 524 | no | instances and claim icon |
| `0x0016A9A0` | `mm_BldgPigFarm_ctor` | 64 | yes | Building constructor, vtable 0x1BD300 |
| `0x0016A9E0` | `mm_BldgRoad_dtor` | 96 | yes | category 3 (road), vtable 0x1BD340 |
| `0x0016AA40` | `mm_BldgRoad_FindNeighbours` | 296 | no | (provisional) walkable neighbours of a road tile (tables 0x1B13F0/0x1B1400) |
| `0x0016AB70` | `mm_BldgRoad_DrawPeople` | 908 | indirect | (provisional) people walking on the road |
| `0x0016AF10` | `mm_BldgRoad_Draw` | 688 | no | road piece instances by neighbour connections |
| `0x0016B1C0` | `mm_BldgRoad_Tick` | 1164 | no | levels the road with its neighbours and marks the minimap |
| `0x0016B650` | `mm_BldgRoad_InitRecord` | 24 | no | slot +0xC |
| `0x0016B670` | `mm_BldgRoad_Slot1C` | 16 | no | slot +0x1C |
| `0x0016B680` | `mm_BldgRoad_ctor` | 88 | yes | base constructor, vtable 0x1BD340, two parameters from record bytes 0x90/0x91 |
| `0x0016B6E0` | `mm_BldgSmallGoods_dtor` | 96 | yes | category 14 (Small Goods), vtable 0x1BD380 |
| `0x0016B740` | `mm_BldgSmallGoods_CollideA` | 64 | no | segment test A when record +0x10 > 0x26 |
| `0x0016B780` | `mm_BldgSmallGoods_CollideB` | 64 | no | segment test B when record +0x10 > 0x26 |
| `0x0016B7C0` | `mm_BldgSmallGoods_Draw` | 316 | no | item instances scaled by height |
| `0x0016B900` | `mm_BldgSmallGoods_Tick` | 540 | no | entrance, terrain, minimap |
| `0x0016BB20` | `mm_BldgSmallGoods_ctor` | 52 | yes | base constructor, vtable 0x1BD380 |
| `0x0016BB60` | `mm_BldgWasteland_dtor` | 96 | yes | category 1 (wasteland), vtable 0x1BD3C0 |
| `0x0016BBC0` | `mm_BldgWasteland_Draw` | 296 | no | weed/rock instances |
| `0x0016BCF0` | `mm_BldgWasteland_FindSpot` | 332 | no | (provisional) walkable spot near a tile |
| `0x0016BE40` | `mm_BldgWasteland_Tick` | 1120 | no | (provisional) random growth and object placement on wasteland |
| `0x0016C2A0` | `mm_BldgWasteland_InitRecord` | 180 | no | random appearance bytes |
| `0x0016C360` | `mm_BldgWasteland_ctor` | 52 | yes | base constructor, vtable 0x1BD3C0 |
| `0x00180520` | `mm_CityScene_GetFade` | 8 | no | fade counter (+0xCC), 0 when no fade runs |
| `0x00180530` | `mm_CityScene_StartFade` | 52 | no | starts a fade (out or in by +0xC8) at speed a1 when none runs |
| `0x00180570` | `mm_CityScene_HasFlag8` | 20 | no | (provisional) city flag 8 |
| `0x00180590` | `mm_CityScene_AddApplicantFamily` | 212 | yes | adds family a1 to the applicants and gives the residents who introduced it a new introduction |
| `0x00180670` | `mm_CityScene_MissionProgress` | 212 | no | percent: the map counter for the goal type 0x1C5550 over the families in the city (0x1F32E0), capped at 100 |
| `0x00180750` | `mm_CityScene_PopulationRank` | 132 | no | rank 1..8 from map +0x74 (100/300/500/1000/1500/2000/2500) |
| `0x001807E0` | `mm_CityScene_ApprovalTier` | 60 | no | approval below 0, up to 50, up to 100, higher |
| `0x00180820` | `mm_CityScene_ScoreA8Tier` | 48 | no | placement score +0xA8 below 200, below 300, or higher |
| `0x00180850` | `mm_CityScene_ScoreA0Tier` | 48 | no | placement score +0xA0 below 200, below 300, or higher |
| `0x00180880` | `mm_CityScene_SaveGame` | 264 | no | writes the save: header, camera, stage, events, families, map, laptop, wants, applicants, player, stamina |
| `0x00180990` | `mm_CityScene_LoadGame` | 2388 | yes | load screen (REPORT.TEX) and loading every *_LoadState from the memory card |
| `0x001812F0` | `mm_CityScene_SaveStep` | 308 | yes | save from the pause menu: map vertex buffers, open, write, wait |
| `0x00181430` | `mm_CityScene_PopMode` | 68 | no | pops the scene mode stack (+0x10.., depth +0x24) |
| `0x00181480` | `mm_CityScene_PushMode` | 56 | no | pushes mode a1 on the mode stack |
| `0x001814C0` | `mm_CityScene_GetMode` | 8 | no | current mode (+0x10) |
| `0x001814D0` | `mm_CityScene_RecordPlacementScore` | 220 | no | decaying averages +0xA0 (a2) and +0xA8 (a1) of placement scores |
| `0x001815B0` | `mm_CityScene_AddApproval` | 16 | no | pending approval change +0xBC += a1 |
| `0x001815D0` | `mm_CityScene_TalkSelectPerson` | 8 | no | talk view choice = slot of family a1 member a2 |
| `0x001815E0` | `mm_CityScene_StartTalk` | 128 | yes | mm_Talk_Start, then pushes mode 5 and hides the HUD |
| `0x00181660` | `mm_CityScene_UpdateApproval` | 204 | yes | every 6 frames moves the approval rate one step toward the pending change |
| `0x00181730` | `mm_CityScene_TickClock` | 724 | no | clock, approval drift and events, new day: month, weather, map daily update |
| `0x00181A10` | `mm_CityScene_Update` | 2600 | yes | city frame by mode: camera, cursor, player, menus, windows, events, drawing |
| `0x00182440` | `mm_CityScene_dtor` | 616 | yes | deletes every city subsystem |
| `0x00182700` | `mm_CityScene_ctor` | 1252 | yes | creates camera, map, terrain, cursor, events, menus, windows, player, talk session |
| `0x0019B840` | `mm_Building_GetFootprint` | 136 | no | from a placed building (type a0[0], packed tile a0[2] = x \| y<<7) and margin a1: x, y and the far corner from the type's size (type table gp, +0x142) into a2[0..3] |
