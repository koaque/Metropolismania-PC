# family

Families: the residents who move into the city.

- Family records come from `FAMILY.BIN` and are cached: 256 slots of 0x1D4 bytes at `0x001D52E0` (ids at
  `0x001F26E0`, LRU stamps at `0x001F2AE0`, pending reads at `0x001F2EE0`). Header: `+0x20` home building type,
  `+0x22` member count; members of 0x48 bytes from `+0x24`: `+0x24` character type, `+0x27` flags, `+0x28` role,
  `+0x2C` .. `+0x4C` up to 16 acquaintances (family ids this member can introduce, often families from the next town), `+0x4C` name, `+0x5C` second name.
- Family ids are grouped by building type (the business or home the family runs): `mm_Family_GetTypeIndex` finds the
  type of an id in a sorted table.
- Bitsets per family: A `0x001D4410` lives in the city, B `0x001D4900` known (Town's Pages), C `0x001D4DF0` moved away.
  `0x001F32E0` counts the families in the city and `0x001F32E8` those that left.
- Introductions: a resident (person `+0x2C`) can introduce an acquaintance's family; `mm_Family_CanApply` checks the
  family is free to apply, `mm_CityScene_AddApplicantFamily` adds it to the applicants. In a popular city (approval
  118 or more, city flag 4) introduced families phone to ask about moving in.
- Friendship meters at `0x001C59B0`, six bytes per family: bits 0-6 hold 0..120, bit 7 marks a friend (their number
  is in the Town's Pages). `mm_Family_AddFriendship` raises or lowers them (talks, gifts, purchases), and
  `mm_Family_FriendAgrees` decides whether a friend agrees to a request.
- Member status table (`mm_MemberStatus_*`): 256 four-byte entries at `0x001F32F0` (`+0` family, `+2` member,
  `+3` status byte) for members known by phone or outside the map; people on the map keep the same byte at
  person `+0x2F`.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

36 functions (ported 1, verified 35). Blocks: 5 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0013BAC0` | `void mm_MemberMeters_Reset(int32_t *meters)` | 160 | no | (provisional) clears four counters and the member meter table at 0x1C59B0 |
| `0x0013BB70` | `uint8_t *mm_MemberStatus_Lookup(int family, int member)` | 448 | no | entry for family a0 member a1 in the table at 0x1F32F0 (adds one when missing) |
| `0x0013BD30` | `uint8_t mm_MemberStatus_GetLow2(int family, int member)` | 36 | no | lookup, then bits 0-1 |
| `0x0013BD60` | `uint32_t mm_MemberStatus_GetMid2(int family, int member)` | 40 | no | lookup, then bits 2-3 |
| `0x0013BD90` | `int mm_MemberStatus_IsLevelAbove13(int family, int member)` | 56 | no | lookup, then level > 13 |
| `0x0013BE20` | `void mm_MemberStatus_Advance(int family, int member)` | 168 | no | level > 1 bumps bits 2-3 (up to 2), then updates bits 0-1 |
| `0x0013BF60` | `int mm_MemberStatus_OnLevel12(int family, int member, int a2, int a3)` | 112 | no | when the level is above 11, applies the friendship gain (0x13CB40) |
| `0x0013C0F0` | `void mm_MemberStatus_Tick(int clock, int month)` | 336 | no | every 600 s the level rises (up to 12); daily and yearly resets of the low bits |
| `0x0013C240` | `void mm_Family_BefriendAll(void)` | 732 | no | every member becomes a friend with meter 64 and every family known (bitset B); from a laptop entry |
| `0x0013C520` | `void mm_Family_ResetForStage(void)` | 204 | no | clears bitsets A and C and the counters for a new stage |
| `0x0013C5F0` | `int mm_Family_MakePhoneNumber(int family)` | 352 | no | (provisional) phone number for the Town's Pages from the family id (a0 * 0x61 + 0x1B08) |
| `0x0013C770` | `void mm_Family_LoadState(McManager *mc)` | 148 | no | reads the member bytes (60000), the three bitsets (0x4E4 each), 0x1F32F0 (0x400) and the counters (0xC) from the save file |
| `0x0013C810` | `void mm_Family_SaveState(McManager *mc)` | 148 | no | writes them |
| `0x0013C8B0` | `int mm_Family_GetTypeIndex(int family)` | 72 | no | building type index of a family id: first entry above a0 in the sorted id table (gp) |
| `0x0013C900` | `int mm_Family_HasLeft(uint32_t family)` | 80 | no | bitset C: the family moved away |
| `0x0013C950` | `void mm_Family_SetKnown(uint32_t family)` | 76 | no | sets bit a0 of bitset B (0x1D4900, families known through the Town's Pages); called by mm_WordsFn_MarkFamilyKnown |
| `0x0013C9A0` | `int mm_Family_IsKnown(uint32_t family)` | 80 | no | bitset B: the family is known (Town's Pages) |
| `0x0013C9F0` | `void mm_Family_SetFlagA(uint32_t family)` | 116 | no | sets bit a0 of bitset A and updates the counters |
| `0x0013CA70` | `int mm_Family_InCity(uint32_t family)` | 80 | no | bitset A: the family lives in the city |
| `0x0013CAC0` | `int mm_Family_GetFriendshipBand(int family, int member)` | 124 | no | band 0..4 of member (a0, a1)'s meter (0, below 24, below 48, below 100, higher) |
| `0x0013CB40` | `int64_t mm_Family_AddFriendship(int family, int member, int32_t *approval, int points)` | 648 | no | adds a3 points to member (a0, a1)'s meter (clamped 0..120); rising may set the friend bit (chance from *a2, the approval) |
| `0x0013CDF0` | `int mm_Family_AddFriendshipScaled(int family, int member, int32_t *approval, int points, int sign)` | 8 | no | mm_Family_AddFriendship with a3 * t0 (t0 = +1 gain, -1 loss) |
| `0x0013CE00` | `uint8_t mm_Family_GetFriendship(int family, int member)` | 60 | no | member (a0, a1)'s meter value, capped at 120 |
| `0x0013CE40` | `void mm_Family_MoveAway(uint32_t family)` | 164 | no | family a0 leaves: bitset A -> C, families in the city - 1, families left + 1 |
| `0x0013CEF0` | `void mm_Family_ClearMemberFlag(int family, int member)` | 44 | no | clears bit 7 of member byte (a0, a1) |
| `0x0013CF20` | `uint32_t mm_Family_PickIntroduction(int family, int member, CityScene *city)` | 480 | yes | an acquaintance of member (a0, a1) who can apply (type limit from 0x1C5548); 0 when none |
| `0x0013D110` | `int mm_Family_CanApply(uint32_t family, CityScene *city)` | 364 | no | family a0 is not in the city, has not left, is not already applying, and there is room for its people |
| `0x0013D280` | `int mm_Family_FriendAgrees(int family, int member, int approval)` | 176 | no | friend bit set and meter at least 100, or at least 48 with approval a2 >= 0, or at least 24 with a2 >= 50 |
| `0x0013D330` | `int mm_Family_MeterAgrees(int family, int member, int approval)` | 152 | no | member (a0, a1)'s meter (capped at 120) is at least 100, or at least 48 with approval a2 >= 0, or at least 24 with a2 >= 50; mm_Family_FriendAgrees without the friend bit |
| `0x0013D3D0` | `int mm_Family_IsCached(int family)` | 196 | no | whether family a0's record has finished loading into the cache |
| `0x0013D4A0` | `int mm_Family_Prefetch(int family, int flags)` | 520 | yes | starts an asynchronous load of family a0's record into the cache (LRU) |
| `0x0013D6B0` | `Family *mm_Family_Get(int family, int flags)` | 476 | yes | family a0's record (0x1D4 bytes), loading it synchronously into the LRU cache if needed |
| `0x0013D890` | `void mm_Family_ResetState(void)` | 436 | no | clears member bytes, bitsets, counters and the cache tables |
| `0x0013DA50` | `void mm_Family_Init(void)` | 152 | yes | resets the family state, opens DATA\BIN\FAMILY.BIN and loads DATA\BIN\EACHSTR.BIN |
| `0x0013DAF0` | `int mm_StatusByte_ctorE0(int status)` | 80 | no | sets byte 3 to level 14 with bits 0-3 clear |
| `0x00142F90` | `void mm_Family_AssignHome(CityMap *map, CityScene *city, int family, int object)` | 1004 | yes | (provisional) family records and flags when a family is assigned a building |
