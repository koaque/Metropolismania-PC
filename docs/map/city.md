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

Claim data (`DATA\BIN\CLAIM.BIN`, at `*0x001BDA50`) starts with the `ClaimThresholds` (low, high, meter step,
switch factor, spread step, hold time). Per building type, section B (`+0x147`) lists claim entries of 8 bytes: a flag
byte (0 a need, set a wish), the claimed id (from 0x8000 a group of ids in section D), a start value and a reach in
tiles; section C (`+0x148`) lists the ids the type provides with their values. `mm_BldgClaims_CountNearby` counts a
claim among the buildings of the id's tile lists that overlap the reach; `mm_BldgClaims_EvalB` draws the building's
claim by those weights, `EvalC` checks it, `EvalD` weighs it against the neighbours' and `EvalA` spreads it to them
over a 13 x 13 grid of weights at `0x001B8060`. Claim slot 0x18 means none, id 0x32 is "no entrance".

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

185 functions (ported 1, typed 4, verified 180). Blocks: 55 can reach a thread switch, 7 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001357F0` | `void mm_ClaimIcon_GetColor(uint32_t icon, int level, Vec4 *colour)` | 100 | no | colour of a claim icon: id a0 (bit 15 selects the second half), level a1 (0 = white 0x1B26C0) |
| `0x0013BDD0` | `uint8_t mm_StatusByte_Low2(uint8_t *status)` | 12 | no | bits 0-1 of *a0 |
| `0x0013BDE0` | `uint32_t mm_StatusByte_Mid2(uint8_t *status)` | 16 | no | bits 2-3 of *a0 |
| `0x0013BDF0` | `int mm_StatusByte_LevelAbove13(uint8_t *status)` | 36 | no | level (bits 4-7) > 13 |
| `0x0013BED0` | `void mm_StatusByte_Advance(uint8_t *status)` | 144 | no | the same advance on a status byte pointer |
| `0x0013BFD0` | `void mm_StatusByte_OnLevel12(int family, int member, uint8_t *status, int amount, uint32_t *approval)` | 52 | no | friendship gain when the byte's level is above 11 |
| `0x00150190` | `void mm_Wants_RemoveEventHold(WantsWin *wants, uint16_t buildingType, int value)` | 272 | no | removes the entry an event holds for building type a1 with value a2 (events ending) |
| `0x001502A0` | `int mm_Wants_AddEventHold(WantsWin *wants, uint16_t buildingType, int value)` | 172 | no | one-word entry held by an event for building type a1 (needs a free entry and word) |
| `0x00150350` | `int mm_Wants_CheckEntry(WantsWin *wants, CityScene *city, uint16_t *entry)` | 208 | no | a want's state: 2 no longer holds (family gone, claim changed, meter not above the low threshold), 1 met (flags 0x48), 3 meter above the high threshold and not phoned yet, else 0 |
| `0x00150420` | `int mm_Wants_Find(WantsWin *wants, CityScene *city, uint32_t *claimSlot, uint32_t *object, uint32_t *person, int32_t *iterator)` | 292 | no | next open resident want from iterator *a5: claim slot a2, object a3, person a4 |
| `0x00150550` | `int mm_Wants_AddForPerson(WantsWin *wants, CityScene *city, MapPerson *person, int claimSlot)` | 256 | no | records person a2's want (claim slot a3) when an entry and four words are free |
| `0x00150650` | `void mm_Wants_LoadState(WantsWin *wants, McManager *mc)` | 20 | no | reads the 0x504-byte list from the save file |
| `0x00150670` | `void mm_Wants_SaveState(WantsWin *wants, McManager *mc)` | 20 | no | writes it |
| `0x00150690` | `void mm_Wants_Prune(WantsWin *wants, CityScene *city)` | 672 | no | every 121st call: wants no longer holding are removed, met ones once the family's call is queued; pressing ones (state 3) get the call once |
| `0x00150940` | `int mm_Wants_HasRoom(WantsWin *wants, int words)` | 52 | no | entry count below 0x40 and at least a1 words left of 0x100 |
| `0x00150980` | `WantsWin *mm_Wants_ctor(WantsWin *wants)` | 16 | no | zeroes the two counters |
| `0x001550A0` | `int mm_Applicants_CountActive(Applicants *applicants)` | 68 | no | applicants whose state (+2) is below 7 |
| `0x001550F0` | `void mm_Applicants_LoadState(Applicants *applicants, McManager *mc)` | 20 | no | reads 0x284 bytes from the save file |
| `0x00155110` | `void mm_Applicants_SaveState(Applicants *applicants, McManager *mc)` | 20 | no | writes 0x284 bytes |
| `0x00155130` | `int mm_Applicants_Find(Applicants *applicants, uint32_t family)` | 60 | no | index of the applicant for family a1 |
| `0x00155170` | `int mm_Applicants_FindFrom(Applicants *applicants, uint32_t *iterator, uint32_t state)` | 100 | no | next applicant from iterator *a1 whose state (+2) is a2 (32 entries) |
| `0x001551E0` | `int mm_Applicants_Add(Applicants *applicants, int family, int month, uint16_t state)` | 184 | yes | adds family a1 (max 32 entries of 0x14 bytes) |
| `0x001552A0` | `int mm_Applicants_Clear(Applicants *applicants)` | 132 | no | empties the list |
| `0x00156C50` | `int mm_Applicants_AnyWaiting(Applicants *applicants)` | 52 | no | (provisional) 1 if an applicant entry has state 2 |
| `0x00156C90` | `void mm_Applicants_RemoveFamily(Applicants *applicants, uint32_t family)` | 80 | no | removes the entry for family a1 |
| `0x00156CE0` | `void mm_Applicants_RemoveAt(Applicants *applicants, int index)` | 148 | no | removes entry a1 and shifts the rest |
| `0x00156D80` | `void mm_Applicants_AssignToBuilding(Applicants *applicants, CityScene *city, int index)` | 512 | yes | places the family's object and assigns its home building, then removes the entry |
| `0x00156F80` | `void mm_Applicants_StartPlacing(Applicants *applicants, CityScene *city)` | 260 | yes | placement mode: state 3 -> 6, highlights the footprint, takes pad focus, clears the notice |
| `0x00157090` | `void mm_Applicants_AddAndShow(Applicants *applicants, int family, int month, int select)` | 68 | yes | mm_Applicants_Add, selected when a3 != 0 |
| `0x001570E0` | `int mm_Applicants_GetSelected(Applicants *applicants)` | 48 | no | selected entry or 0 |
| `0x00158A20` | `int mm_Placement_FindSpot(Player *player, CityScene *city, int family, int typeIndex, int x, int z, int half, int32_t *rect)` | 748 | no | tries offsets around (a5, a6) until mm_Map_CanPlace returns 2; writes the footprint rectangle |
| `0x00158D10` | `void mm_Player_PlaceBagItem(Player *player, CityScene *city)` | 756 | yes | placing an item from the bag: preview, confirm (button 0x40), place object, finish use |
| `0x00159010` | `void mm_Player_PlaceForApplicant(Player *player, CityScene *city)` | 780 | yes | placement preview for the selected applicant: footprint highlight green or red, then mm_Placement_FindSpot |
| `0x0015A580` | `void mm_CityAssets_LoadMyHome(void)` | 324 | yes | async load step for DATA\BIN\MYHOME.A3L |
| `0x0015A6D0` | `void mm_CityAssets_LoadChars(void)` | 468 | yes | async load step for DATA\A3D\CHAR.A3D |
| `0x0015A8B0` | `void mm_CityAssets_BuildCharParts(void)` | 480 | no | fills the character part table 0x1F3B90 from CHAR.A3D |
| `0x0015AA90` | `void mm_CityAssets_LoadCharObjs(void)` | 448 | yes | async load step for DATA\A3D\CHAROBJ.A3D |
| `0x0015AC50` | `void mm_CityAssets_LoadObjects(void)` | 448 | yes | async load step for DATA\A3D\OBJECT.A3D |
| `0x0015AE10` | `int mm_CityAssets_LoadStep(void)` | 356 | yes | state machine over the asset loads; returns when done |
| `0x0015FD70` | `void mm_Markers_Draw(Markers *markers)` | 184 | no | (provisional) model 0x1F3B28 with up to four nodes placed by the city update |
| `0x0015FF10` | `Markers *mm_Markers_dtor(Markers *markers, int16_t deleteFlag)` | 76 | yes | (provisional) vtable 0x1BCFC8 at +0x128 |
| `0x0015FF60` | `Markers *mm_Markers_ctor(Markers *markers)` | 216 | no | (provisional) vtable 0x1BCFC8 at +0x128, models 0x1F3B28/0x1F3B08, nodes 1-3 |
| `0x00163690` | `int mm_Collide_SegmentA(int a0, int mesh, Vec4 *point, Vec4 *matrix, uint32_t *hit, float lift)` | 436 | no | (provisional) segment test against building geometry |
| `0x00163850` | `int mm_Collide_SegmentB(int a0, int mesh, Vec4 *point, Vec4 *matrix, uint32_t *hit, float *distance, float f12)` | 444 | no | (provisional) second segment test |
| `0x00163A10` | `void mm_BldgType_Slot34(BuildingType *btype)` | 8 | no | base slot +0x34 |
| `0x00163A20` | `int mm_BldgType_IsOpenAt(BuildingType *btype, CityScene *city, MapObject *obj)` | 184 | no | open between +0x13C and +0x13E (minutes of the day, a2 shifts the time); closed in holiday months (city flag 2) for types with flag 0x2000 |
| `0x00163AE0` | `int mm_Clock_AddWrap(int a0, int time, int offset)` | 52 | no | a1 + a2 mod 3600, wrapped at 86400 (seconds per day) |
| `0x00163B20` | `int mm_BldgType_IsMyHome(BuildingType *btype)` | 32 | no | type id/kind (+0x134) in 5..8, the My Home types |
| `0x00163B40` | `void mm_BldgType_DrawOccupants(BuildingType *btype, CityScene *city, MapObject *obj, MapPerson *person, int flags)` | 256 | indirect | (provisional) draws people at a building |
| `0x00163C40` | `void mm_BldgType_OccupantMatrix(BuildingType *btype, Vec4 *colour, Vec4 *position, uint32_t tile)` | 292 | no | (provisional) matrix for an occupant |
| `0x00163D70` | `void mm_BldgType_DrawOccupant(BuildingType *btype, Vec4 *colour, MapPerson *person, CityScene *city)` | 400 | no | (provisional) one occupant on its tile |
| `0x00163F00` | `uint16_t mm_BldgType_FindEntrance(BuildingType *btype, CityScene *city, int x, int z, int32_t *side)` | 344 | no | (provisional) entrance tile next to a building (walkable flag 0x10, tables 0x1B13F0/0x1B1400) |
| `0x00164060` | `int mm_BldgType_Slot30(BuildingType *btype)` | 8 | no | base slot +0x30 |
| `0x00164070` | `int mm_BldgType_CollideA(BuildingType *btype)` | 8 | no | base slot +0x2C |
| `0x00164080` | `int mm_BldgType_CollideB(BuildingType *btype)` | 8 | no | base slot +0x28 |
| `0x00164090` | `uint8_t mm_BldgType_Slot1C(BuildingType *btype)` | 8 | no | base slot +0x1C |
| `0x001640A0` | `int mm_BldgType_Slot20(BuildingType *btype)` | 8 | no | base slot +0x20 |
| `0x001640B0` | `void mm_BldgType_DrawOccupants2(BuildingType *btype, int layer, CityScene *city, MapObject *obj, int t0, int flags, MapPerson *people)` | 356 | indirect | (provisional) |
| `0x00164220` | `void mm_BldgType_Draw(BuildingType *btype)` | 8 | no | base slot +0x14 |
| `0x00164230` | `int mm_BldgType_Tick(BuildingType *btype)` | 8 | no | base slot +0x10 |
| `0x00164240` | `void mm_BldgType_InitRecord(BuildingType *btype)` | 8 | no | base slot +0xC |
| `0x00164250` | `char *mm_BldgType_GetPostName(BuildingType *btype, int post)` | 44 | no | POSTNAME.BIN string (data at 0x1BDBC0): offset table entry for the type's post group (byte +0x146), then entry a1; used by mm_WordsFn_TextOccupation |
| `0x00164280` | `BuildingType *mm_BldgType_BaseDtor(BuildingType *btype, int16_t deleteFlag)` | 136 | yes | vtable 0x1BD000; releases the shared name data when the last type goes |
| `0x00164310` | `BuildingType *mm_BldgType_BaseCtor(BuildingType *btype, uint8_t *record, uint16_t id)` | 1068 | yes | base constructor: shared DATA\BIN\POSTNAME.BIN, model and parameters |
| `0x00164770` | `BuildingType *mm_BldgAmusementPark_dtor(BuildingType *btype, int16_t deleteFlag)` | 128 | yes | category 11 (Amusement Park), vtable 0x1BD040 |
| `0x001647F0` | `void mm_BldgAmusementPark_DrawAnim(BuildingType *btype, int time)` | 232 | no | swaying ride animation between the opening hours |
| `0x001648E0` | `void mm_BldgAmusementPark_Draw(BuildingType *btype, int layer, CityScene *city, MapObject *obj, int t0, int t1, float x, float y, float z)` | 524 | no | instances for the park |
| `0x00164AF0` | `BuildingType *mm_BldgAmusementPark_ctor(BuildingType *btype, uint8_t *record, int id)` | 64 | yes | Building constructor, vtable 0x1BD040 |
| `0x00164B30` | `BuildingType *mm_BldgBusiness_dtor(BuildingType *btype, int16_t deleteFlag)` | 112 | yes | category 6 (factories, shops), vtable 0x1BD1C0 |
| `0x00164BA0` | `BuildingType *mm_BldgOffice_dtor(BuildingType *btype, int16_t deleteFlag)` | 128 | yes | category 7 (offices), vtable 0x1BD080 |
| `0x00164C20` | `int mm_BldgOffice_CollideA(BuildingType *btype, MapObject *obj, Vec4 *point, Vec4 *matrix, uint32_t *hit, uint32_t *part, float lift)` | 224 | no | slot +0x2C |
| `0x00164D00` | `int mm_BldgOffice_CollideB(BuildingType *btype, MapObject *obj, Vec4 *point, Vec4 *matrix, uint32_t *hit, float *distance, uint32_t *part, float f12)` | 224 | no | slot +0x28 |
| `0x00164DE0` | `void mm_BldgOffice_Draw(BuildingType *btype, int layer, CityScene *city, MapObject *obj, uint32_t *part, int flags, float x, float y, float z)` | 852 | no | slot +0x14 |
| `0x00165140` | `int mm_BldgOffice_Tick(BuildingType *btype, CityScene *city, MapObject *obj, int timeFlags)` | 132 | no | Building tick plus a check of the tile's first object |
| `0x001651D0` | `BuildingType *mm_BldgOffice_ctor(BuildingType *btype, uint8_t *record, int id)` | 72 | yes | Building constructor, vtable 0x1BD080, +0x160 from record byte 0x92 |
| `0x00165220` | `BuildingType *mm_BldgRanch_dtor(BuildingType *btype, int16_t deleteFlag)` | 128 | yes | category 12 (Ranch), vtable 0x1BD0C0 |
| `0x001652A0` | `void mm_BldgRanch_DrawAnim(BuildingType *btype, int time)` | 232 | no | animals' animation between the opening hours |
| `0x00165390` | `void mm_BldgRanch_Draw(BuildingType *btype, int layer, CityScene *city, MapObject *obj, int t0, int t1, float x, float y, float z)` | 524 | indirect | instances for the ranch |
| `0x001655A0` | `BuildingType *mm_BldgRanch_ctor(BuildingType *btype, uint8_t *record, int id)` | 88 | yes | Building constructor, vtable 0x1BD0C0, models from 0x1F3B50.. |
| `0x00165600` | `BuildingType *mm_Bldg_dtor(BuildingType *btype, int16_t deleteFlag)` | 96 | yes | intermediate Building class, vtable 0x1BD100 |
| `0x00165660` | `int mm_ClaimMeter_Level(uint16_t meter)` | 60 | no | claim meter a0 against the thresholds at *0x1BDA50: 0 below low, 1 between, 2 above high |
| `0x001656A0` | `void mm_BldgClaims_EvalA(BuildingType *btype, CityScene *city, MapObject *obj)` | 932 | no | the building's claim spreads to its neighbours: over the 13 x 13 weight grid (every other tile) each building first met there, and the objects stacked on it, with the same claim (flag and id), above the low threshold and not maxed, get their meter raised by weight x spread step; at 0xFF it maxes out (flag 0x40, hold time); a claim rising a band costs a point of approval |
| `0x00165A50` | `void mm_BldgClaims_EvalB(BuildingType *btype, CityScene *city, MapObject *obj)` | 504 | no | draws the building's claim: each section B entry weighs its value among the nearby buildings (mm_BldgClaims_CountNearby; id 0x32 weighs its start value while the building has no entrance), one is drawn by weight as the claim slot and timer, a need not at its start value marks the building (flag 2) |
| `0x00165C50` | `uint32_t mm_BldgClaims_CountNearby(BuildingType *btype, CityScene *city, char *claimEntry, int32_t *rect)` | 552 | no | the value of a claim entry (flag, id or section D group, start value, reach) among the buildings of its tile lists overlapping the rectangle: a need counts down from the start by each building's section C value (0 once met), a wish counts up (cap 0xFF) |
| `0x00165E80` | `void mm_BldgClaims_EvalC(BuildingType *btype, CityScene *city, MapObject *obj)` | 712 | no | checks whether the claim is met (nothing missing nearby): flag 8, approval +3, sound and minimap mark; else meter handling |
| `0x00166150` | `void mm_BldgClaims_EvalD(BuildingType *btype, CityScene *city, MapObject *obj)` | 1768 | no | weighs the building's claims against its neighbours': the table at 0x1F4B90 sums, per claim, the weights and weighted meters of the neighbours holding it; the heaviest wins: the building's own claim moves its meter towards their average by half a step, another is taken when the meter is low enough (switch factor) |
| `0x00166840` | `int mm_BldgClaims_Evaluate(BuildingType *btype, CityScene *city, MapObject *obj, int rise)` | 628 | no | per building: claim check (part C), meter rise by the rate, part D above the low threshold, flag 0x40 above 0xFE |
| `0x00166AC0` | `void mm_BldgClaims_DrawIcon(BuildingType *btype, MapObject *obj, Vec4 *colour)` | 372 | no | claim icon colour from the meter level (1 slow pulse, 2 fast pulse) |
| `0x00166C40` | `void mm_Bldg_InitRecordBase(BuildingType *btype, MapObject *obj)` | 12 | no | record +0x12 = 0x18 |
| `0x00166C50` | `BuildingType *mm_Bldg_ctor(BuildingType *btype, uint8_t *record, int id)` | 52 | yes | base constructor, vtable 0x1BD100 |
| `0x00166C90` | `BuildingType *mm_BldgField_dtor(BuildingType *btype, int16_t deleteFlag)` | 96 | yes | category 4 (crop fields), vtable 0x1BD140 |
| `0x00166CF0` | `uint8_t mm_BldgField_Slot1C(BuildingType *btype, MapObject *obj)` | 84 | no | slot +0x1C |
| `0x00166D50` | `void mm_BldgField_DrawPeople(BuildingType *btype, int layer, CityScene *city, MapObject *obj, int t0, int flags, MapPerson *people, float x, float y, float z)` | 472 | indirect | slot +0x18: farm workers |
| `0x00166F30` | `void mm_BldgField_Draw(BuildingType *btype, int layer, CityScene *city, MapObject *obj, int t0, int t1, float x, float y, float z)` | 564 | no | crop instances |
| `0x00167170` | `int mm_BldgField_Tick(BuildingType *btype, CityScene *city, MapObject *obj)` | 408 | no | marks residents on the minimap |
| `0x00167310` | `BuildingType *mm_BldgField_ctor(BuildingType *btype, uint8_t *record, int id)` | 348 | yes | base constructor, vtable 0x1BD140, crop parameters from the record |
| `0x00167470` | `BuildingType *mm_BldgPort_dtor(BuildingType *btype, int16_t deleteFlag)` | 128 | yes | category 9 (fishing ports), vtable 0x1BD180 |
| `0x001674F0` | `int mm_BldgPort_CollideA(BuildingType *btype, MapObject *obj, Vec4 *point, Vec4 *matrix, uint32_t *hit, float lift)` | 84 | no | slot +0x2C |
| `0x00167550` | `int mm_BldgPort_CollideB(BuildingType *btype, MapObject *obj, Vec4 *point, Vec4 *matrix, uint32_t *hit, float *distance, float f12)` | 84 | no | slot +0x28 |
| `0x001675B0` | `void mm_BldgPort_Draw(BuildingType *btype, int layer, CityScene *city, MapObject *obj, int t0, int t1, float x, float y, float z)` | 504 | no | slot +0x14 |
| `0x001677B0` | `int mm_BldgPort_Tick(BuildingType *btype, CityScene *city, MapObject *obj, int timeFlags)` | 236 | no | Building tick plus terrain height changes at the shore |
| `0x001678A0` | `BuildingType *mm_BldgPort_ctor(BuildingType *btype, uint8_t *record, int id)` | 52 | yes | Building constructor, vtable 0x1BD180 |
| `0x001678E0` | `int mm_Bldg_IsOpen(BuildingType *btype, CityScene *city, MapObject *obj)` | 72 | no | slot +0x24: opening hours for a building or its type |
| `0x00167930` | `int mm_Bldg_Slot30(BuildingType *btype)` | 8 | no | slot +0x30 |
| `0x00167940` | `int mm_Bldg_CollideA(BuildingType *btype, MapObject *obj, Vec4 *point, Vec4 *matrix, uint32_t *hit, float lift)` | 64 | no | slot +0x2C |
| `0x00167980` | `int mm_Bldg_CollideB(BuildingType *btype, MapObject *obj, Vec4 *point, Vec4 *matrix, uint32_t *hit, float *distance, float f12)` | 64 | no | slot +0x28 |
| `0x001679C0` | `int mm_BldgType_FindEntrance2(BuildingType *btype, CityScene *city, int x, int z, int side)` | 368 | no | whether a building of flag 8 lies within 3 tiles beyond side a5 (stops at walkable tiles) |
| `0x00167B40` | `void mm_Bldg_DrawPeople(BuildingType *btype, int layer, CityScene *city, MapObject *obj, int t0, int flags, MapPerson *people, float x, float y, float z)` | 1452 | indirect | slot +0x18: people at the building |
| `0x00168100` | `void mm_BldgBusiness_Draw(BuildingType *btype, int layer, CityScene *city, MapObject *obj, int t0, int t1, float x, float y, float z)` | 532 | no | slot +0x14 for Business and My Home: instances and claim icon |
| `0x00168320` | `int mm_Bldg_Tick(BuildingType *btype, CityScene *city, MapObject *obj, int timeFlags)` | 1204 | no | slot +0x10: main part finds its entrance and side (road-grid parity), levels the ground under the footprint (0.1 / multiples of 0.5); entrance part runs the claims (result), others count +0x18; minimap colour by claim level |
| `0x001687E0` | `void mm_Bldg_InitRecord(BuildingType *btype, MapObject *obj)` | 44 | no | slot +0xC: base init, record +0x14 = 0x4000 |
| `0x00168810` | `BuildingType *mm_BldgBusiness_ctor(BuildingType *btype, uint8_t *record, int id)` | 52 | yes | Building constructor, vtable 0x1BD1C0 |
| `0x00168850` | `BuildingType *mm_BldgKeepOut_dtor(BuildingType *btype, int16_t deleteFlag)` | 96 | yes | category 15 (Keep Out), vtable 0x1BD200 |
| `0x001688B0` | `void mm_BldgKeepOut_Draw(BuildingType *btype, int layer, CityScene *city, MapObject *obj, int t0, int t1, float x, float y, float z)` | 344 | no | fence instances |
| `0x00168A10` | `BuildingType *mm_BldgKeepOut_ctor(BuildingType *btype, uint8_t *record, int id)` | 52 | yes | base constructor, vtable 0x1BD200 |
| `0x00168A50` | `BuildingType *mm_BldgLeveled_dtor(BuildingType *btype, int16_t deleteFlag)` | 96 | yes | category 2 (leveled land), vtable 0x1BD240 |
| `0x00168AB0` | `int mm_BldgLeveled_CheckArea(BuildingType *btype, CityScene *city, int x, int z, int size, int t1)` | 1152 | no | (provisional) tests the 4 surrounding tile objects |
| `0x00168F30` | `void mm_BldgLeveled_Draw(BuildingType *btype, int layer, CityScene *city, MapObject *obj, int t0, int t1, float x, float y, float z)` | 132 | no | instances when the record flag 1 is set |
| `0x00168FC0` | `int mm_BldgLeveled_Tick(BuildingType *btype, CityScene *city, MapObject *obj)` | 1128 | no | (provisional) builds on leveled land for waiting applicants: levels the terrain, places the building |
| `0x00169430` | `BuildingType *mm_BldgLeveled_ctor(BuildingType *btype, uint8_t *record, int id)` | 72 | yes | base constructor, vtable 0x1BD240, +0x160 from record byte 0x90 |
| `0x00169480` | `void mm_BldgType_CallAll(int time)` | 120 | indirect | calls a virtual method on every building type |
| `0x00169500` | `BuildingType *mm_BldgType_Create(int id, uint8_t *record)` | 716 | yes | building type object for a STRTYPE.BIN record by its category (+0x20) |
| `0x001697D0` | `void mm_BldgType_DeleteAll(void)` | 140 | yes | deletes every building type object |
| `0x00169860` | `void mm_BldgType_LoadAll(void)` | 248 | yes | loads DATA\BIN\STRTYPE.BIN (0x9C-byte records, 281 types) and creates the type objects |
| `0x00169960` | `BuildingType *mm_BldgHousing_dtor(BuildingType *btype, int16_t deleteFlag)` | 128 | yes | category 8 (housing), vtable 0x1BD280 |
| `0x001699E0` | `int mm_BldgHousing_CollideA(BuildingType *btype, MapObject *obj, Vec4 *point, Vec4 *matrix, uint32_t *hit, uint32_t *part, float lift)` | 196 | no | slot +0x2C |
| `0x00169AB0` | `int mm_BldgHousing_CollideB(BuildingType *btype, MapObject *obj, Vec4 *point, Vec4 *matrix, uint32_t *hit, float *distance, uint32_t *part, float f12)` | 196 | no | slot +0x28 |
| `0x00169B80` | `void mm_BldgHousing_Draw(BuildingType *btype, int layer, CityScene *city, MapObject *obj, uint32_t *part, int flags, float x, float y, float z)` | 928 | no | slot +0x14 |
| `0x00169F20` | `uint8_t mm_BldgHousing_UnitAt(BuildingType *btype, int kind, int x, int z, int count, uint16_t rule, CityScene *city, uint32_t *isUnit, float height)` | 372 | no | (provisional) unit index of a tile within a housing block |
| `0x0016A0A0` | `void mm_BldgHousing_CountUnits(BuildingType *btype, CityScene *city, MapObject *obj)` | 756 | no | a housing object's unit shape (variant high byte) from the units two tiles away along its terrain axis (tables 0x1B1480, 0x1B14C0); a changed shape passes style (mesh low byte) on and recurses into the neighbours |
| `0x0016A3A0` | `int mm_BldgHousing_Tick(BuildingType *btype, CityScene *city, MapObject *obj, int timeFlags)` | 160 | no | Building tick plus unit counting |
| `0x0016A440` | `void mm_BldgHousing_InitRecord(BuildingType *btype, MapObject *obj)` | 80 | no | Building init; variant +0x1A = (x/2) mod 3 |
| `0x0016A490` | `BuildingType *mm_BldgHousing_ctor(BuildingType *btype, uint8_t *record, int id)` | 52 | yes | Building constructor, vtable 0x1BD280 |
| `0x0016A4D0` | `BuildingType *mm_BldgMyHome_dtor(BuildingType *btype, int16_t deleteFlag)` | 128 | yes | category 10 (My Home), vtable 0x1BD2C0 |
| `0x0016A550` | `int mm_BldgMyHome_Tick(BuildingType *btype, CityScene *city, MapObject *obj)` | 376 | no | (provisional) Building tick for the player's home |
| `0x0016A6D0` | `BuildingType *mm_BldgMyHome_ctor(BuildingType *btype, uint8_t *record, int id)` | 52 | yes | Building constructor, vtable 0x1BD2C0 |
| `0x0016A710` | `BuildingType *mm_BldgPigFarm_dtor(BuildingType *btype, int16_t deleteFlag)` | 128 | yes | category 13 (Pig Farm), vtable 0x1BD300 |
| `0x0016A790` | `void mm_BldgPigFarm_Draw(BuildingType *btype, int layer, CityScene *city, MapObject *obj, int t0, int t1, float x, float y, float z)` | 524 | no | instances and claim icon |
| `0x0016A9A0` | `BuildingType *mm_BldgPigFarm_ctor(BuildingType *btype, uint8_t *record, int id)` | 64 | yes | Building constructor, vtable 0x1BD300 |
| `0x0016A9E0` | `BuildingType *mm_BldgRoad_dtor(BuildingType *btype, int16_t deleteFlag)` | 96 | yes | category 3 (road), vtable 0x1BD340 |
| `0x0016AA40` | `void mm_BldgRoad_FindNeighbours(BuildingType *btype, CityScene *city, MapObject *obj, int x, int z)` | 296 | no | (provisional) walkable neighbours of a road tile (tables 0x1B13F0/0x1B1400) |
| `0x0016AB70` | `void mm_BldgRoad_DrawPeople(BuildingType *btype, int layer, CityScene *city, MapObject *obj, int t0, int flags, MapPerson *people, float x, float y, float z)` | 908 | indirect | (provisional) people walking on the road |
| `0x0016AF10` | `void mm_BldgRoad_Draw(BuildingType *btype, int layer, CityScene *city, MapObject *obj, int t0, int t1, float x, float y, float z)` | 688 | no | road piece instances by neighbour connections |
| `0x0016B1C0` | `int mm_BldgRoad_Tick(BuildingType *btype, CityScene *city, MapObject *obj)` | 1164 | no | road shape from neighbouring road blocks, flattens its tiles to 0, picks the lamp, traffic-light or slope mesh, marks the minimap |
| `0x0016B650` | `void mm_BldgRoad_InitRecord(BuildingType *btype, MapObject *obj)` | 24 | no | slot +0xC |
| `0x0016B670` | `int mm_BldgRoad_Slot1C(BuildingType *btype, int a1)` | 16 | no | slot +0x1C |
| `0x0016B680` | `BuildingType *mm_BldgRoad_ctor(BuildingType *btype, uint8_t *record, int id)` | 88 | yes | base constructor, vtable 0x1BD340, two parameters from record bytes 0x90/0x91 |
| `0x0016B6E0` | `BuildingType *mm_BldgSmallGoods_dtor(BuildingType *btype, int16_t deleteFlag)` | 96 | yes | category 14 (Small Goods), vtable 0x1BD380 |
| `0x0016B740` | `int mm_BldgSmallGoods_CollideA(BuildingType *btype, MapObject *obj, Vec4 *point, Vec4 *matrix, uint32_t *hit, float lift)` | 64 | no | segment test A when record +0x10 > 0x26 |
| `0x0016B780` | `int mm_BldgSmallGoods_CollideB(BuildingType *btype, MapObject *obj, Vec4 *point, Vec4 *matrix, uint32_t *hit, float *distance, float f12)` | 64 | no | segment test B when record +0x10 > 0x26 |
| `0x0016B7C0` | `void mm_BldgSmallGoods_Draw(BuildingType *btype, int layer, CityScene *city, MapObject *obj, int t0, int t1, float x, float y, float z)` | 316 | no | item instances scaled by height |
| `0x0016B900` | `int mm_BldgSmallGoods_Tick(BuildingType *btype, CityScene *city, MapObject *obj)` | 540 | no | entrance, terrain, minimap |
| `0x0016BB20` | `BuildingType *mm_BldgSmallGoods_ctor(BuildingType *btype, uint8_t *record, int id)` | 52 | yes | base constructor, vtable 0x1BD380 |
| `0x0016BB60` | `BuildingType *mm_BldgWasteland_dtor(BuildingType *btype, int16_t deleteFlag)` | 96 | yes | category 1 (wasteland), vtable 0x1BD3C0 |
| `0x0016BBC0` | `void mm_BldgWasteland_Draw(BuildingType *btype, int layer, CityScene *city, MapObject *obj, int t0, int t1, float x, float y, float z)` | 296 | no | weed/rock instances |
| `0x0016BCF0` | `int mm_BldgWasteland_FindSpot(BuildingType *btype, CityScene *city, int a2, uint32_t x, uint32_t z, uint32_t *spot)` | 332 | no | (provisional) walkable spot near a tile |
| `0x0016BE40` | `int mm_BldgWasteland_Tick(BuildingType *btype, CityScene *city, MapObject *obj)` | 1120 | no | drops the mark (+0x14) of wasteland on a sloping tile (VU0 normals), plots its minimap colour by how much of it lies above the water level, and under water may place a type 3 object by a straight road |
| `0x0016C2A0` | `void mm_BldgWasteland_InitRecord(BuildingType *btype, MapObject *obj)` | 180 | no | random appearance bytes |
| `0x0016C360` | `BuildingType *mm_BldgWasteland_ctor(BuildingType *btype, uint8_t *record, int id)` | 52 | yes | base constructor, vtable 0x1BD3C0 |
| `0x00180520` | `int mm_CityScene_GetFade(CityScene *city)` | 8 | no | fade counter (+0xCC), 0 when no fade runs |
| `0x00180530` | `void mm_CityScene_StartFade(CityScene *city, int speed)` | 52 | no | starts a fade (out or in by +0xC8) at speed a1 when none runs |
| `0x00180570` | `int mm_CityScene_HasFlag8(CityScene *city)` | 20 | no | (provisional) city flag 8 |
| `0x00180590` | `void mm_CityScene_AddApplicantFamily(CityScene *city, uint32_t family, int select)` | 212 | yes | adds family a1 to the applicants and gives the residents who introduced it a new introduction |
| `0x00180670` | `int mm_CityScene_MissionProgress(CityScene *city)` | 212 | no | percent: the map counter for the goal type 0x1C5550 over the families in the city (0x1F32E0), capped at 100 |
| `0x00180750` | `int mm_CityScene_PopulationRank(CityScene *city)` | 132 | no | rank 1..8 from map +0x74 (100/300/500/1000/1500/2000/2500) |
| `0x001807E0` | `int mm_CityScene_ApprovalTier(CityScene *city)` | 60 | no | approval below 0, up to 50, up to 100, higher |
| `0x00180820` | `int mm_CityScene_ScoreA8Tier(CityScene *city)` | 48 | no | placement score +0xA8 below 200, below 300, or higher |
| `0x00180850` | `int mm_CityScene_ScoreA0Tier(CityScene *city)` | 48 | no | placement score +0xA0 below 200, below 300, or higher |
| `0x00180880` | `int mm_CityScene_SaveGame(CityScene *city, McManager *mc)` | 264 | no | writes the save: header, camera, stage, events, families, map, laptop, wants, applicants, player, stamina |
| `0x00180990` | `int mm_CityScene_LoadGame(CityScene *city)` | 2388 | yes | load screen (REPORT.TEX) and loading every *_LoadState from the memory card |
| `0x001812F0` | `int mm_CityScene_SaveStep(CityScene *city, McManager *mc)` | 308 | yes | save from the pause menu: map vertex buffers, open, write, wait |
| `0x00181430` | `void mm_CityScene_PopMode(CityScene *city)` | 68 | no | pops the scene mode stack (+0x10.., depth +0x24) |
| `0x00181480` | `void mm_CityScene_PushMode(CityScene *city, int mode)` | 56 | no | pushes mode a1 on the mode stack |
| `0x001814C0` | `int mm_CityScene_GetMode(CityScene *city)` | 8 | no | current mode (+0x10) |
| `0x001814D0` | `void mm_CityScene_RecordPlacementScore(CityScene *city, int scoreA8, int scoreA0)` | 220 | no | decaying averages +0xA0 (a2) and +0xA8 (a1) of placement scores |
| `0x001815B0` | `void mm_CityScene_AddApproval(CityScene *city, int delta)` | 16 | no | pending approval change +0xBC += a1 |
| `0x001815C0` | `int mm_CityScene_TalkEndChoosing(CityScene *city)` | 8 | no | tail call mm_Talk_EndChoosing(city->talk); pair of mm_CityScene_TalkSelectPerson |
| `0x001815D0` | `int mm_CityScene_TalkSelectPerson(CityScene *city, int family, int member)` | 8 | no | talk view choice = slot of family a1 member a2 |
| `0x001815E0` | `int mm_CityScene_StartTalk(CityScene *city, int byFamily, int target, int member, int arg5)` | 128 | yes | mm_Talk_Start, then pushes mode 5 and hides the HUD |
| `0x00181660` | `void mm_CityScene_UpdateApproval(CityScene *city)` | 204 | yes | every 6 frames moves the approval rate one step toward the pending change |
| `0x00181730` | `void mm_CityScene_TickClock(CityScene *city)` | 724 | no | clock, approval drift and events, new day: month, weather, map daily update |
| `0x00181A10` | `uint32_t mm_CityScene_Update(CityScene *city)` | 2600 | yes | city frame by mode: camera, cursor, player, menus, windows, events, drawing |
| `0x00182440` | `CityScene *mm_CityScene_dtor(CityScene *city, int16_t deleteFlag)` | 616 | yes | deletes every city subsystem |
| `0x00182700` | `CityScene *mm_CityScene_ctor(CityScene *city)` | 1252 | yes | creates camera, map, terrain, cursor, events, menus, windows, player, talk session |
| `0x0019B840` | `void mm_Building_GetFootprint(MapObject *obj, int a1, uint32_t *rect)` | 136 | no | from a placed building (type a0[0], packed tile a0[2] = x \| y<<7) and margin a1: x, y and the far corner from the type's size (type table gp, +0x142) into a2[0..3] |
