# claim

`DATA\BIN\CLAIM.BIN`: the wants ("claims") that buildings and families express. It starts with the claim meter
thresholds (`ClaimThresholds`); offset tables (in words) lead to its lists.
- `mm_Claim_GetList` (section B, by `BuildingType.claimB`): the claims a building type can have, 8-byte `ClaimEntry`
  records (need or wish, the item, a value, a reach) ended by id 0. `MapObject.claimSlot` is the one a building has.
- `mm_Claim_GetSupplies` (section C, by `BuildingType.claimC`): what a building type supplies, `ClaimSupply` pairs
  (item, amount) ended by id 0. Neighbours' supplies count down a need (met at 0) or count up a wish
  (`mm_BldgClaims_CountNearby`).
- `mm_Claim_GetGroup` (section D): a claim's id from 0x8000 names a group, a 0-ended list of items.
- `mm_BldgClaims_WeighNeighbours` tallies the neighbours' claims at `0x1F4B90` (`ClaimTally`, two per item).
- `mm_Claim_GetLine` returns the text of a claim list's item.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

6 functions (verified 6). Blocks: 1 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00135860` | `int mm_Claim_PickVariant(uint32_t claim)` | 84 | no | entry variant by bit 0x8000 of a0 from the current speaker table (gp) |
| `0x001358C0` | `int mm_Claim_GetLine(int claim, int line)` | 124 | no | line a1 of claim a0 (8-byte items), variant by bit 0x8000 |
| `0x00135940` | `ClaimSupply *mm_Claim_GetSupplies(int index)` | 32 | no | supply list `index` (CLAIM.BIN section C, by BuildingType.claimC): the items a building type supplies and how much, ended by id 0 |
| `0x00135960` | `uint16_t *mm_Claim_GetGroup(uint32_t index)` | 40 | no | item group `index` & 0x7FFF (CLAIM.BIN section D; a claim's id from 0x8000): 0-ended item ids |
| `0x00135990` | `ClaimEntry *mm_Claim_GetList(int index)` | 32 | no | claim list `index` (CLAIM.BIN section B, by BuildingType.claimB): 8-byte claims (need or wish, item or group, value, reach), ended by id 0 |
| `0x001359B0` | `void mm_Claim_Load(void)` | 184 | yes | loads DATA\BIN\CLAIM.BIN and resolves its section offsets (+8, +0xC, +0x10, +0x14, ...) |
