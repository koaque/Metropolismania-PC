# bag

The player's bag: items bought from shops or kept to give away. `mm_Bag_*` keeps the item list (in the object at
city `+0x60`), `mm_BagMenu_*` is the bag window. Item kinds come from the goods records (`goods`).

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

14 functions (named 14). Blocks: 5 can reach a thread switch, 1 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Name | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0012AED0` | `mm_Bag_RemoveSlot` | 236 | no | removes item slot a1 (16 bytes each from +4) and shifts the rest down; capacity per bag type from 0x1B5B08 |
| `0x0012AFC0` | `mm_Player_AddMusicTrack` | 180 | no | adds the track id of goods item (a1, a2) to the 10-entry list at +0x144 if missing and registers it with mm_Bgm_SetTrackSlot |
| `0x0012B080` | `mm_Bag_AddItem` | 136 | no | puts goods item (group a1, index a2) with count and flags into the first empty slot |
| `0x0012B110` | `mm_Bag_FindItem` | 148 | no | slot holding the item whose id is a1 |
| `0x0012B1B0` | `mm_BagMenu_UpdateItemText` | 380 | no | item name and the action text for the selected slot: "Empty bag", "Eat", "Drink", "New clothes", "Set up" |
| `0x0012B330` | `mm_BagMenu_FinishUse` | 124 | yes | after using an item: count - 1 when a1, removes the slot when empty, refreshes the list |
| `0x0012B3B0` | `mm_BagMenu_GetSetupKind` | 180 | no | for the item being used: kind 3/2/1 for goods categories 0x1C/0x1B/0x1A (placeable items) |
| `0x0012B470` | `mm_BagMenu_UseSelected` | 420 | yes | uses the selected item by goods category (placing, eating, ...), with sounds |
| `0x0012B620` | `mm_BagMenu_RefreshList` | 312 | yes | rebuilds the item list text ("name * count") |
| `0x0012B760` | `mm_BagMenu_Draw` | 472 | indirect | draws the bag window model, its highlight and the four text boxes |
| `0x0012B940` | `mm_BagMenu_Update` | 2516 | yes | the bag screen: creates the windows, handles cursor, use/discard/keep prompts ("Set up", "Close", "Discard", "Keep it") |
| `0x0012C320` | `mm_Bag_Clear` | 112 | no | empties the 10 item slots and sets the 10 track ids to 0xFFFF |
| `0x0012C390` | `mm_Bag_ctor` | 248 | no | empty bag plus its window data (0x1F3B24 ...) |
| `0x0012C490` | `mm_Bag_CreateGlobal` | 56 | yes | 0x1BDA18 = new bag (0x1AC bytes) |
