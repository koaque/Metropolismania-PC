# bag

The player's bag: items bought from shops or kept to give away. `mm_Bag_*` keeps the item list (in the object at
city `+0x60`), `mm_BagMenu_*` is the bag window. Item kinds come from the goods records (`goods`).

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

16 functions (verified 16). Blocks: 5 can reach a thread switch, 1 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0012AE90` | `void mm_Bag_LoadState(Bag *bag, McManager *mc)` | 20 | no | reads the bag's first 0x16C bytes (items and track ids) from the save (mm_SaveFile_Read); only caller mm_CityScene_LoadGame |
| `0x0012AEB0` | `void mm_Bag_SaveState(Bag *bag, McManager *mc)` | 20 | no | writes the bag's first 0x16C bytes to the save (mm_SaveFile_Write); only caller mm_CityScene_SaveGame |
| `0x0012AED0` | `void mm_Bag_RemoveSlot(Bag *bag, BagSlot *slot)` | 236 | no | (bag, slot) removes the item slot `slot` (Bag.slots) and moves the ones after it up; the slots per bag size from 0x1B5B08 |
| `0x0012AFC0` | `void mm_Bag_AddMusicTrack(Bag *bag, int group, int index)` | 180 | no | (bag, group, index) adds the track id of goods item (`group`, `index`) to the ten music tracks (musicTracks) if missing and registers it with mm_Bgm_SetTrackSlot |
| `0x0012B080` | `void mm_Bag_AddItem(Bag *bag, int group, int index, int16_t stage, int month, int16_t seller, uint8_t unk0E, uint8_t unk0F)` | 136 | no | (bag, group, index, stage, month, seller, unk0E, unk0F) puts the goods (record `index` of group `group`) in the first empty slot with the stage and month it was bought in, the seller's family and its count from the goods record; the bag gets size 1 if it had none. unk0E and unk0F go to the slot's write-only bytes (0xFF and 0 from both callers) |
| `0x0012B110` | `int mm_Bag_FindItem(Bag *bag, uint32_t id)` | 148 | no | (bag, id) slot holding the item whose id is `id` |
| `0x0012B1B0` | `void mm_BagMenu_UpdateItemText(Bag *bag)` | 380 | no | item name and the action text for the selected slot: "Empty bag", "Eat", "Drink", "New clothes", "Set up" |
| `0x0012B330` | `void mm_BagMenu_FinishUse(Bag *bag, int used)` | 124 | yes | (bag, used) after using an item: count - 1 when `used`, removes the slot when empty, refreshes the list |
| `0x0012B3B0` | `uint32_t mm_BagMenu_GetSetupKind(Bag *bag, uint32_t *kind)` | 180 | no | the goods record of the slot being set up (setupSlot): returns its value (-1 with none) and writes its kind for placing, 0 to 3 for goods kinds 0x17, 0x1A, 0x1B and 0x1C (the Small Goods type is 0x115 + kind) |
| `0x0012B470` | `void mm_BagMenu_UseSelected(Bag *bag, CityScene *city)` | 420 | yes | uses the selected item by goods category (placing, eating, ...), with sounds |
| `0x0012B620` | `int mm_BagMenu_RefreshList(Bag *bag)` | 312 | yes | rebuilds the item list text ("name * count") |
| `0x0012B760` | `void mm_BagMenu_Draw(Bag *bag)` | 472 | indirect | draws the bag window model, its highlight and the four text boxes |
| `0x0012B940` | `void mm_BagMenu_Update(Bag *bag, CityScene *city)` | 2516 | yes | the bag screen: creates the windows, handles cursor, use/discard/keep prompts ("Set up", "Close", "Discard", "Keep it") |
| `0x0012C320` | `void mm_Bag_Clear(Bag *bag)` | 112 | no | empties the 10 item slots and sets the 10 track ids to 0xFFFF |
| `0x0012C390` | `Bag *mm_Bag_ctor(Bag *bag)` | 248 | no | empty bag plus its window data (0x1F3B24 ...) |
| `0x0012C490` | `void mm_Bag_CreateGlobal(void)` | 56 | yes | 0x1BDA18 = new bag (0x1AC bytes) |
