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

48 functions (ported 4, typed 23, verified 21). Blocks: 13 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0015AF80` | `int mm_McManager_FindSave(McManager *mc, int a1, int a2)` | 248 | no | (provisional) finds a save by name in the slot tables (0x1F3F00/0x1F4340/0x1F4780) |
| `0x0015B080` | `void mm_McManager_FormatSaveInfo(McManager *mc, int a1, char *out)` | 1208 | yes | "Stage n Month m Year y "name" Population ... / Approval Rate ..." |
| `0x0015B540` | `void mm_McManager_FormatSlotLine(McManager *mc, int slot, char *out, int a3)` | 848 | yes | short slot line " Stage n "name" Pop. ../A.R. ..%", "*Newest*" |
| `0x0015B890` | `void mm_McManager_BuildSaveHeader(McManager *mc, int a1, char *out)` | 428 | no | fills the save header from the stage state (0x1C5380) |
| `0x0015BA40` | `int mm_SaveFile_GetResult(McManager *mc, int a1)` | 8 | no | result of the file's last memory card request |
| `0x0015BA50` | `int mm_SaveFile_IsDone(McManager *mc, int a1)` | 8 | no | whether the file's last request is done |
| `0x0015BA60` | `int mm_SaveFile_Close(McManager *mc)` | 16 | no | queues close of the file's descriptor (+0x24) |
| `0x0015BA70` | `int mm_SaveFile_Read(McManager *mc, int buffer, int size)` | 28 | no | queues sceMcRead of a2 bytes into a1 from the open save file |
| `0x0015BA90` | `int mm_SaveFile_Write(McManager *mc, int buffer, int size)` | 28 | no | queues sceMcWrite of a2 bytes from a1 to the open save file |
| `0x0015BAB0` | `int mm_SaveFile_Open(McManager *mc, int mode)` | 24 | no | queues sceMcOpen of a1 on the current port/slot (gp), descriptor to +0x24 |
| `0x0015BAD0` | `int mm_SaveFile_Format(McManager *mc, int port)` | 140 | no | format step machine; returns when done |
| `0x0015BB60` | `void mm_SaveFile_ResetState(McManager *mc)` | 8 | no | state +4 = 0 |
| `0x0015BB70` | `int mm_SaveFile_PollDone(McManager *mc)` | 12 | no | whether request +0x14 is done |
| `0x0015BB80` | `void mm_SaveFile_DeleteIconSys(McManager *mc, int dir)` | 64 | no | queues delete of icon.sys in directory a1 |
| `0x0015BBC0` | `void mm_SaveFile_WriteIconSys(McManager *mc, int a1, int a2, int a3)` | 56 | yes | writes icon.sys (mm_McSave_WriteIconSys) |
| `0x0015BC00` | `int mm_SaveFile_PrepareDir(McManager *mc, int a1, int a2, int a3)` | 1052 | yes | step machine: card info, mkdir/chdir, copies DATA\STATIC.ICO to STATIC.ICO |
| `0x0015C020` | `void mm_McManager_ResetState(McManager *mc)` | 8 | no | state +4 = 0 |
| `0x0015C030` | `int mm_McManager_PrepareNewSlot(McManager *mc, int card)` | 108 | yes | (provisional) fills the first empty slot of card a1 with a new save name |
| `0x0015C0A0` | `int mm_McManager_FindSlotA(McManager *mc, int index, int32_t *card)` | 128 | no | (provisional) slot lookup over the slot tables |
| `0x0015C120` | `int mm_McManager_FindSlotB(McManager *mc, int a1, int32_t *slot)` | 128 | no | (provisional) slot lookup over the slot tables |
| `0x0015C1A0` | `int mm_McManager_IsCardUsable(McManager *mc, int card)` | 68 | no | (provisional) card a1 state 2 and no error |
| `0x0015C1F0` | `uint8_t mm_McManager_GetCardStatus(McManager *mc, int card)` | 156 | no | card a1 status for the menus (formatted, unformatted, none, ...) |
| `0x0015C290` | `int mm_McManager_FormatSlotText(McManager *mc, int32_t *a1, int a2, uint32_t *a3)` | 1588 | yes | "Slot %d: %s %s%s, %s. %shrs %smins" with the RTC time |
| `0x0015C8D0` | `int mm_McManager_HasAnySave(void)` | 84 | no | whether any slot holds a save |
| `0x0015C930` | `int mm_McManagerUpdate(McManager *mc)` | 3200 | no |  |
| `0x0015D5B0` | `void mm_McManager_NewSaveName(McManager *mc, uint32_t card)` | 848 | yes | (provisional) builds a new save directory name from the CD clock |
| `0x0015D900` | `void mm_McManager_ClearSlots(McManager *mc)` | 160 | no | resets the slot tables |
| `0x0015D9A0` | `void mm_McManager_InitSlots(void)` | 136 | no | (provisional) initial slot tables |
| `0x0015DA30` | `McManager *mm_McManager_dtor(McManager *mc, int16_t deleteFlag)` | 124 | yes | deletes its sub-object and file |
| `0x0015DAB0` | `McManager *mm_McManager_ctor(McManager *mc)` | 128 | yes | sub-object, file, slot tables |
| `0x00185EB0` | `int mm_McSave_WriteIconSys(int a0, int a1, int a2, int a3)` | 1120 | yes | builds the icon.sys header ('PS2D', title, STATIC.ICO icons) in 0x1F65F0 and writes it through the memory card queue |
| `0x00186310` | `int mm_McQueue_Seek(int queue, int fd, int offset, int mode)` | 152 | no | queues sceMcSeek (command 12); returns the request id |
| `0x001863B0` | `int mm_McQueue_Write(int queue, int fd, int buffer, int size, int32_t *result)` | 152 | no | queues sceMcWrite (command 10) |
| `0x00186450` | `int mm_McQueue_Read(int queue, int fd, int buffer, int size, int32_t *result)` | 152 | no | queues sceMcRead (command 9) |
| `0x001864F0` | `int mm_McQueue_Delete(int queue, int port, int name)` | 136 | no | queues sceMcDelete (command 8) |
| `0x00186580` | `int mm_McQueue_Close(int queue, int fd, int32_t *result)` | 120 | no | queues sceMcClose (command 7) |
| `0x00186600` | `int mm_McQueue_Open(int queue, int port, int name, int mode, int32_t *result)` | 152 | no | queues sceMcOpen (command 6) |
| `0x001866A0` | `int mm_McQueue_Chdir(int queue, int port, int newDir, int oldDir, int32_t *result)` | 152 | no | queues sceMcChdir (command 5) |
| `0x00186740` | `int mm_McQueue_Mkdir(int queue, int port, int name)` | 136 | no | queues sceMcMkdir (command 4) |
| `0x001867D0` | `int mm_McQueue_Format(int queue, int port, int32_t *result)` | 120 | no | queues sceMcFormat (command 3) |
| `0x00186850` | `int mm_McQueue_GetDir(int queue, int port, int name, int mode, int maxEntries, int table, int32_t *result)` | 184 | no | queues sceMcGetDir (command 2) |
| `0x00186910` | `int mm_McQueue_GetInfo(int queue, int port, int type, int free, int format, int32_t *result)` | 168 | no | queues sceMcGetInfo (command 1) |
| `0x001869C0` | `int mm_McQueue_GetResult(int queue, int request)` | 48 | no | result word of request a1 (64 slots of 0x24 bytes at 0x1F5CF0, result at +0xC) |
| `0x001869F0` | `int mm_McQueue_IsDone(int queue, int request)` | 72 | no | 1 when request a1 is no longer pending |
| `0x00186A40` | `void mm_McQueue_Init(void)` | 96 | yes | sceMcInit and clears the 64 request slots |
| `0x00186AA0` | `int mm_McQueue_Pump(void)` | 504 | yes | issues the request at the queue tail (sceMcGetInfo ... sceMcSeek by command 1-12), then polls sceMcSync for its result |
| `0x00186CA0` | `int mm_McManagerSub_DeletingDtor(int queue, int16_t deleteFlag)` | 68 | yes | delete when the flag is > 0 |
| `0x00186CF0` | `int mm_McManagerSub_ctor(int queue)` | 8 | no | returns this |
