# save

Saving and loading on the memory card.

- `mm_McQueue_*`: a 64-entry asynchronous command queue at `0x001F5CF0`, pumped once per frame with non-blocking
  `sceMcSync`.
- `mm_McManager_*`: lists the cards (one directory entry per call) and finds the game's save directory and its
  `icon.sys` (964 bytes).
- `mm_SaveFile_*` reads (`mm_SaveFile_Read`, `0x0015BA70`) and writes (`mm_SaveFile_Write`) blocks. Each subsystem has
  `*_LoadState` (reads) and `*_SaveState` (writes); `mm_CityScene_SaveGame` and `mm_CityScene_LoadGame` call them all.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

50 functions (named 50). Blocks: 13 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Name | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0012AE90` | `mm_Save_ReadBlock16C` | 20 | no | reads 0x16C bytes from the save file (0x15BA70) |
| `0x0012AEB0` | `mm_Save_WriteBlock16C` | 20 | no | writes 0x16C bytes to the save file (0x15BA90) |
| `0x0015AF80` | `mm_McManager_FindSave` | 248 | no | (provisional) finds a save by name in the slot tables (0x1F3F00/0x1F4340/0x1F4780) |
| `0x0015B080` | `mm_McManager_FormatSaveInfo` | 1208 | yes | "Stage n Month m Year y "name" Population ... / Approval Rate ..." |
| `0x0015B540` | `mm_McManager_FormatSlotLine` | 848 | yes | short slot line " Stage n "name" Pop. ../A.R. ..%", "*Newest*" |
| `0x0015B890` | `mm_McManager_BuildSaveHeader` | 428 | no | fills the save header from the stage state (0x1C5380) |
| `0x0015BA40` | `mm_SaveFile_GetResult` | 8 | no | result of the file's last memory card request |
| `0x0015BA50` | `mm_SaveFile_IsDone` | 8 | no | whether the file's last request is done |
| `0x0015BA60` | `mm_SaveFile_Close` | 16 | no | queues close of the file's descriptor (+0x24) |
| `0x0015BA70` | `mm_SaveFile_Read` | 28 | no | queues sceMcRead of a2 bytes into a1 from the open save file |
| `0x0015BA90` | `mm_SaveFile_Write` | 28 | no | queues sceMcWrite of a2 bytes from a1 to the open save file |
| `0x0015BAB0` | `mm_SaveFile_Open` | 24 | no | queues sceMcOpen of a1 on the current port/slot (gp), descriptor to +0x24 |
| `0x0015BAD0` | `mm_SaveFile_Format` | 140 | no | format step machine; returns when done |
| `0x0015BB60` | `mm_SaveFile_ResetState` | 8 | no | state +4 = 0 |
| `0x0015BB70` | `mm_SaveFile_PollDone` | 12 | no | whether request +0x14 is done |
| `0x0015BB80` | `mm_SaveFile_DeleteIconSys` | 64 | no | queues delete of icon.sys in directory a1 |
| `0x0015BBC0` | `mm_SaveFile_WriteIconSys` | 56 | yes | writes icon.sys (mm_McSave_WriteIconSys) |
| `0x0015BC00` | `mm_SaveFile_PrepareDir` | 1052 | yes | step machine: card info, mkdir/chdir, copies DATA\STATIC.ICO to STATIC.ICO |
| `0x0015C020` | `mm_McManager_ResetState` | 8 | no | state +4 = 0 |
| `0x0015C030` | `mm_McManager_PrepareNewSlot` | 108 | yes | (provisional) fills the first empty slot of card a1 with a new save name |
| `0x0015C0A0` | `mm_McManager_FindSlotA` | 128 | no | (provisional) slot lookup over the slot tables |
| `0x0015C120` | `mm_McManager_FindSlotB` | 128 | no | (provisional) slot lookup over the slot tables |
| `0x0015C1A0` | `mm_McManager_IsCardUsable` | 68 | no | (provisional) card a1 state 2 and no error |
| `0x0015C1F0` | `mm_McManager_GetCardStatus` | 156 | no | card a1 status for the menus (formatted, unformatted, none, ...) |
| `0x0015C290` | `mm_McManager_FormatSlotText` | 1588 | yes | "Slot %d: %s %s%s, %s. %shrs %smins" with the RTC time |
| `0x0015C8D0` | `mm_McManager_HasAnySave` | 84 | no | whether any slot holds a save |
| `0x0015C930` | `mm_McManagerUpdate` | 3200 | no |  |
| `0x0015D5B0` | `mm_McManager_NewSaveName` | 848 | yes | (provisional) builds a new save directory name from the CD clock |
| `0x0015D900` | `mm_McManager_ClearSlots` | 160 | no | resets the slot tables |
| `0x0015D9A0` | `mm_McManager_InitSlots` | 136 | no | (provisional) initial slot tables |
| `0x0015DA30` | `mm_McManager_dtor` | 124 | yes | deletes its sub-object and file |
| `0x0015DAB0` | `mm_McManager_ctor` | 128 | yes | sub-object, file, slot tables |
| `0x00185EB0` | `mm_McSave_WriteIconSys` | 1120 | yes | builds the icon.sys header ('PS2D', title, STATIC.ICO icons) in 0x1F65F0 and writes it through the memory card queue |
| `0x00186310` | `mm_McQueue_Seek` | 152 | no | queues sceMcSeek (command 12); returns the request id |
| `0x001863B0` | `mm_McQueue_Write` | 152 | no | queues sceMcWrite (command 10) |
| `0x00186450` | `mm_McQueue_Read` | 152 | no | queues sceMcRead (command 9) |
| `0x001864F0` | `mm_McQueue_Delete` | 136 | no | queues sceMcDelete (command 8) |
| `0x00186580` | `mm_McQueue_Close` | 120 | no | queues sceMcClose (command 7) |
| `0x00186600` | `mm_McQueue_Open` | 152 | no | queues sceMcOpen (command 6) |
| `0x001866A0` | `mm_McQueue_Chdir` | 152 | no | queues sceMcChdir (command 5) |
| `0x00186740` | `mm_McQueue_Mkdir` | 136 | no | queues sceMcMkdir (command 4) |
| `0x001867D0` | `mm_McQueue_Format` | 120 | no | queues sceMcFormat (command 3) |
| `0x00186850` | `mm_McQueue_GetDir` | 184 | no | queues sceMcGetDir (command 2) |
| `0x00186910` | `mm_McQueue_GetInfo` | 168 | no | queues sceMcGetInfo (command 1) |
| `0x001869C0` | `mm_McQueue_GetResult` | 48 | no | result word of request a1 (64 slots of 0x24 bytes at 0x1F5CF0, result at +0xC) |
| `0x001869F0` | `mm_McQueue_IsDone` | 72 | no | 1 when request a1 is no longer pending |
| `0x00186A40` | `mm_McQueue_Init` | 96 | yes | sceMcInit and clears the 64 request slots |
| `0x00186AA0` | `mm_McQueue_Pump` | 504 | yes | issues the request at the queue tail (sceMcGetInfo ... sceMcSeek by command 1-12), then polls sceMcSync for its result |
| `0x00186CA0` | `mm_McManagerSub_DeletingDtor` | 68 | yes | delete when the flag is > 0 |
| `0x00186CF0` | `mm_McManagerSub_ctor` | 8 | no | returns this |
