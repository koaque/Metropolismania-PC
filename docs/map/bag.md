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
| `0x0012AED0` | `void mm_Bag_RemoveSlot(Bag *bag, uint16_t *slot)` | 236 | no | removes item slot a1 (16 bytes each from +4) and shifts the rest down; capacity per bag type from 0x1B5B08 |
| `0x0012AFC0` | `void mm_Bag_AddMusicTrack(Bag *bag, int group, int index)` | 180 | no | adds the track id of goods item (a1, a2) to the 10-entry list at +0x144 if missing and registers it with mm_Bgm_SetTrackSlot |
| `0x0012B080` | `void mm_Bag_AddItem(Bag *bag, int group, int index, int16_t a3, int t0, int16_t t1, uint8_t t2, uint8_t t3)` | 136 | no | puts goods item (group a1, index a2) with count and flags into the first empty slot |
| `0x0012B110` | `int mm_Bag_FindItem(Bag *bag, uint32_t id)` | 148 | no | slot holding the item whose id is a1 |
| `0x0012B1B0` | `void mm_BagMenu_UpdateItemText(Bag *bag)` | 380 | no | item name and the action text for the selected slot: "Empty bag", "Eat", "Drink", "New clothes", "Set up" |
| `0x0012B330` | `void mm_BagMenu_FinishUse(Bag *bag, int used)` | 124 | yes | after using an item: count - 1 when a1, removes the slot when empty, refreshes the list |
| `0x0012B3B0` | `uint32_t mm_BagMenu_GetSetupKind(Bag *bag, uint32_t *a1)` | 180 | no | for the item being used: kind 3/2/1 for goods categories 0x1C/0x1B/0x1A (placeable items) |
| `0x0012B470` | `void mm_BagMenu_UseSelected(Bag *bag, CityScene *city)` | 420 | yes | uses the selected item by goods category (placing, eating, ...), with sounds |
| `0x0012B620` | `int mm_BagMenu_RefreshList(Bag *bag)` | 312 | yes | rebuilds the item list text ("name * count") |
| `0x0012B760` | `void mm_BagMenu_Draw(Bag *bag)` | 472 | indirect | draws the bag window model, its highlight and the four text boxes |
| `0x0012B940` | `void mm_BagMenu_Update(Bag *bag, CityScene *city)` | 2516 | yes | the bag screen: creates the windows, handles cursor, use/discard/keep prompts ("Set up", "Close", "Discard", "Keep it") |
| `0x0012C320` | `void mm_Bag_Clear(Bag *bag)` | 112 | no | empties the 10 item slots and sets the 10 track ids to 0xFFFF |
| `0x0012C390` | `Bag *mm_Bag_ctor(Bag *bag)` | 248 | no | empty bag plus its window data (0x1F3B24 ...) |
| `0x0012C490` | `void mm_Bag_CreateGlobal(void)` | 56 | yes | 0x1BDA18 = new bag (0x1AC bytes) |
