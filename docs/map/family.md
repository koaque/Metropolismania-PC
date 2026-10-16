# family

Families: the residents who move into the city.

- Family records come from `FAMILY.BIN` and are cached: 256 slots of 0x1D4 bytes at `0x001D52E0` (ids at
  `0x001F26E0`, LRU stamps at `0x001F2AE0`, pending reads at `0x001F2EE0`). Header: `+0x20` home building type,
  `+0x22` member count; members of 0x48 bytes from `+0x24`: `+0x24` character type, `+0x27` flags, `+0x28` role,
  `+0x2C` .. `+0x4C` wanted building types, `+0x4C` name, `+0x5C` second name.
- Family ids are grouped by building type: `mm_Family_GetTypeIndex` finds the type of an id in a sorted table.
- Per-member meters at `0x001C59B0` (six bytes per family, value 0..120 in bits 0-6) and three family bitsets at
  `0x001D4410`, `0x001D4900` and `0x001D4DF0`.
- Member status table (`mm_MemberStatus_*`): 256 four-byte entries at `0x001F32F0` (`+0` family, `+2` member,
  `+3` status byte) for members known by phone or outside the map; people on the map keep the same byte at
  person `+0x2F`.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

34 functions (named 31, typed 3). Blocks: 5 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Name | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0013BAC0` | `mm_MemberMeters_Reset` | 160 | no | (provisional) clears four counters and the member meter table at 0x1C59B0 |
| `0x0013BB70` | `mm_MemberStatus_Lookup` | 448 | no | entry for family a0 member a1 in the table at 0x1F32F0 (adds one when missing) |
| `0x0013BD30` | `mm_MemberStatus_GetLow2` | 36 | no | lookup, then bits 0-1 |
| `0x0013BD60` | `mm_MemberStatus_GetMid2` | 40 | no | lookup, then bits 2-3 |
| `0x0013BD90` | `mm_MemberStatus_IsLevelAbove13` | 56 | no | lookup, then level > 13 |
| `0x0013BE20` | `mm_MemberStatus_Advance` | 168 | no | level > 1 bumps bits 2-3 (up to 2), then updates bits 0-1 |
| `0x0013BF60` | `mm_MemberStatus_OnLevel12` | 112 | no | when the level is above 11, applies the friendship gain (0x13CB40) |
| `0x0013C0F0` | `mm_MemberStatus_Tick` | 336 | no | every 600 s the level rises (up to 12); daily and yearly resets of the low bits |
| `0x0013C240` | `mm_Family_MonthlyPass` | 732 | no | (provisional) pass over all member bytes (0x1C59B0) and bitset B |
| `0x0013C520` | `mm_Family_ResetForStage` | 204 | no | clears bitsets A and C and the counters for a new stage |
| `0x0013C5F0` | `mm_Family_MakePhoneNumber` | 352 | no | (provisional) phone number for the Town's Pages from the family id (a0 * 0x61 + 0x1B08) |
| `0x0013C770` | `mm_Family_LoadState` | 148 | no | reads the member bytes (60000), the three bitsets (0x4E4 each), 0x1F32F0 (0x400) and the counters (0xC) from the save file |
| `0x0013C810` | `mm_Family_SaveState` | 148 | no | writes them |
| `0x0013C8B0` | `mm_Family_GetTypeIndex` | 72 | no | building type index of a family id: first entry above a0 in the sorted id table (gp) |
| `0x0013C900` | `mm_Family_IsFlagC` | 80 | no | bit a0 of bitset C (0x1D4DF0) |
| `0x0013C9A0` | `mm_Family_IsFlagB` | 80 | no | bit a0 of bitset B (0x1D4900) |
| `0x0013C9F0` | `mm_Family_SetFlagA` | 116 | no | sets bit a0 of bitset A and updates the counters |
| `0x0013CA70` | `mm_Family_IsFlagA` | 80 | no | bit a0 of bitset A (0x1D4410) |
| `0x0013CAC0` | `mm_Family_GetMemberGrade` | 124 | no | (provisional) grade from member byte (a0, a1), capped at 120 |
| `0x0013CB40` | `mm_Family_PickMember` | 648 | no | (provisional) random member choice under conditions (twin of 0x13CDF0) |
| `0x0013CDF0` | `mm_Family_PickMember2` | 8 | no | (provisional) variant of mm_Family_PickMember with one more argument |
| `0x0013CE00` | `mm_Family_GetMemberValue` | 60 | no | member byte (a0, a1) bits 0-6, capped at 120 |
| `0x0013CE40` | `mm_Family_SetFlagC` | 164 | no | sets bit a0 of bitset C and updates the counters |
| `0x0013CEF0` | `mm_Family_ClearMemberFlag` | 44 | no | clears bit 7 of member byte (a0, a1) |
| `0x0013CF20` | `mm_Family_MoveIn` | 480 | yes | (provisional) moves family a2 into the city (bitsets, messages 0x1550A0/0x155130) |
| `0x0013D110` | `mm_Family_Retreat` | 364 | no | (provisional) family a0 leaves (bitsets, messages) |
| `0x0013D280` | `mm_Family_TestMember` | 176 | no | (provisional) tests member byte (a0, a1) bit 7 and value ranges |
| `0x0013D3D0` | `mm_Family_IsCached` | 196 | no | whether family a0's record has finished loading into the cache |
| `0x0013D4A0` | `mm_Family_Prefetch` | 520 | yes | starts an asynchronous load of family a0's record into the cache (LRU) |
| `0x0013D6B0` | `mm_Family_Get` | 476 | yes | family a0's record (0x1D4 bytes), loading it synchronously into the LRU cache if needed |
| `0x0013D890` | `mm_Family_ResetState` | 436 | no | clears member bytes, bitsets, counters and the cache tables |
| `0x0013DA50` | `mm_Family_Init` | 152 | yes | resets the family state, opens DATA\BIN\FAMILY.BIN and loads DATA\BIN\EACHSTR.BIN |
| `0x0013DAF0` | `mm_StatusByte_ctorE0` | 80 | no | sets byte 3 to level 14 with bits 0-3 clear |
| `0x00142F90` | `mm_Family_AssignHome` | 1004 | yes | (provisional) family records and flags when a family is assigned a building |
