# event

City events (visitors, requests, incidents, paydays).

- Types come from `DATA\BIN\EVENT.BIN`: 96 records of 0x4C bytes, loaded to gp `0x001BDA84`. Byte `+0x24` is the
  handler type, an index into the handler table at `0x001AF0D0`.
- Handler records have six slots: `+0` Check (may the event start), `+4` Start, `+8` Update (every frame),
  `+0xC` End, `+0x10` Slot10 and `+0x14` Slot14 (called from the talk script). `mm_Ev<Name>_<Slot>` functions are
  named after the event titles.
- The event manager (`mm_EventMgr_*`, city `+0x68`) keeps the active list (`0x001BDA88`) and the queue of waiting ids
  (`0x001C5790`, count `0x001BDA80`, `mm_EventQueue_*`). Its slots `+0xA4` and `+0xA8` are the events the talk script
  works with.
- `mm_EvActor_*` moves the people taking part (follow, move to, detach, notify).

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

148 functions (typed 143, verified 5). Blocks: 20 can reach a thread switch, 21 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001356F0` | `void mm_Event_ClearTileFlags(int listIndex)` | 120 | no | (provisional) walks event a0's tile list (0x1BDA50/0x1BDA60) and clears a flag on each tile |
| `0x00135770` | `void mm_Event_SetTileFlags(int listIndex)` | 120 | no | (provisional) same list, sets the flag |
| `0x001378D0` | `void mm_Event_InitForCity(EventMgr *events)` | 1448 | indirect | (provisional) prepares the event queue and the active list when a city starts |
| `0x00137EA0` | `void mm_Event_LoadState(EventMgr *events, McManager *mc)` | 164 | indirect | reads the queue (0x200), the active list (0x224), a1 (0xA4) and each active event's data from the save file |
| `0x00137F50` | `void mm_Event_SaveState(EventMgr *events, McManager *mc)` | 164 | indirect | writes them |
| `0x00138000` | `void mm_Event_CallSlot14(EventMgr *events, CityScene *city, uint16_t *actor, int arg, int mode)` | 128 | indirect | calls handler slot +0x14 of the type of active event a1 |
| `0x00138080` | `void mm_Event_CallSlot10(EventMgr *events, CityScene *city, int index, int arg)` | 128 | indirect | calls handler slot +0x10 of the type of active event a2 |
| `0x00138100` | `void mm_Event_Update(EventMgr *events, CityScene *city)` | 2544 | indirect | per frame: starts queued events whose conditions hold, runs and finishes active ones |
| `0x00138AF0` | `int mm_Event_CheckCondition(uint16_t *queue, CityScene *city, uint8_t *type, MapPerson *person)` | 1712 | indirect | (provisional) checks an event type's start conditions against the city |
| `0x001391B0` | `uint8_t mm_Event_TryStart(EventMgr *events, CityScene *city, MapPerson *person)` | 504 | indirect | (provisional) starts event a2 if its conditions hold |
| `0x001393B0` | `int mm_Event_FindForFamily(EventMgr *events, CityScene *city, int choice, int16_t *familyMember)` | 708 | yes | (provisional) the a2-th event the family can take part in (from the talk menu) |
| `0x00139680` | `void mm_Event_ClearList(int list)` | 128 | no | (provisional) clears an event list and its entries |
| `0x00139700` | `int mm_Event_StartFromScript(EventMgr *events, CityScene *city, MapPerson *person)` | 460 | indirect | (provisional) called by the script code (0x17CE30) |
| `0x001398D0` | `char *mm_Event_FormatText(EventMgr *events, int slot)` | 296 | yes | (provisional) text for event slot a1 (strings, 0x1C5990) |
| `0x00139A00` | `int mm_Event_CountActive(EventMgr *events)` | 56 | no | active entries (0x20 bytes each, up to 5) in the list |
| `0x00139A40` | `void mm_Event_ReleaseTiles(CityScene *city)` | 264 | no | (provisional) frees the tiles an event used |
| `0x00139B50` | `void mm_Event_Finish(EventMgr *events, CityScene *city, int index)` | 356 | indirect | ends active event a2: releases its tiles, calls its handler, removes it (called by the event handlers) |
| `0x00139CC0` | `void mm_Event_Abort(EventMgr *events, CityScene *city, int index)` | 272 | indirect | (provisional) ends active event a2 without its normal finish |
| `0x00139DD0` | `int mm_Event_Trigger(EventMgr *events, int16_t id, CityScene *city, MapPerson *person)` | 820 | indirect | (provisional) triggers event a1 now or queues it |
| `0x0013A110` | `void mm_Event_CallHandlerSlot(EventMgr *events, CityScene *city, int index, int arg)` | 204 | indirect | (provisional) calls a handler slot for active event a2 |
| `0x0013A1E0` | `int mm_Event_IsQueued(EventMgr *events, int16_t id)` | 104 | no | (provisional) whether id a1 is in the queue |
| `0x0013A250` | `EventMgr *mm_EventMgr_dtor(EventMgr *events, int16_t deleteFlag)` | 264 | yes | frees the event lists |
| `0x0013A360` | `EventMgr *mm_EventMgr_ctor(EventMgr *events)` | 500 | yes | creates the event lists and handler objects |
| `0x0013A560` | `void mm_Event_LoadTypes(void)` | 516 | yes | loads DATA\BIN\EVENT.BIN (0x4C-byte type records) |
| `0x0013A770` | `int mm_EventList_MoveDue(int list, int from, int to, int32_t *ctx)` | 764 | no | (provisional) bucket a1 -> a2 for entries whose wait counter is at or below 0 |
| `0x0013AA70` | `void mm_EventList_MoveDueTo(int list, int from, int to, int32_t *ctx)` | 884 | no | (provisional) like mm_EventList_MoveDue with an extra per-target test (a3[1]) |
| `0x0013ADF0` | `void mm_EventList_MoveOffRank(int list, int from, int to, int32_t *ctx)` | 788 | no | (provisional) entries whose rank mask (+0x27) lacks the current rank bit |
| `0x0013B110` | `void mm_EventList_MoveOnRank(int list, int from, int to, int32_t *ctx)` | 844 | no | (provisional) entries whose rank mask (+0x27) has the current rank bit |
| `0x0013B460` | `void mm_EventList_MoveOffRankB(int list, int from, int to, int32_t *ctx)` | 804 | no | (provisional) second variant of the rank test, used to park entries in bucket 6 |
| `0x0013B790` | `void mm_EventList_MoveByMask26(int list, int from, int to, int32_t *ctx)` | 788 | no | (provisional) entries whose mask at record +0x26 lacks a2[1] (event manager constructor) |
| `0x0018F5D0` | `int mm_EvShoppingTogether_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 32 | no | type 1: Slot14 (returns +0x28 when a3 == 0) |
| `0x0018F5F0` | `void mm_EvShoppingTogether_Update(EventData *data, CityScene *city)` | 900 | yes | type 1: Update |
| `0x0018F980` | `void mm_EvShoppingTogether_End(EventData *data, CityScene *city)` | 192 | no | type 1: End (clears the building marks) |
| `0x0018FA40` | `void mm_EvShoppingTogether_Start(EventData *data, CityScene *city)` | 116 | no | type 1: Start (marks the two buildings, timer 180) |
| `0x0018FAC0` | `int mm_EvShoppingTogether_Check(EventData *data, CityScene *city, float *a2)` | 460 | no | event type 1 "Shopping together": slot Check |
| `0x0018FC90` | `void mm_EvAlien_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 56 | yes | type 16: Slot14 |
| `0x0018FCD0` | `void mm_EvAlien_End(EventData *data, CityScene *city)` | 120 | no | type 16: End (clears the building marks) |
| `0x0018FD50` | `int mm_EvAlien_PickTarget(CityScene *city, int a1, int a2)` | 400 | no | (provisional) picks a building for the alien (footprint checks, table 0x1B3760) |
| `0x0018FEE0` | `void mm_EvAlien_Update(EventData *data, CityScene *city)` | 1184 | yes | type 16: Update (finishes after 720 ticks) |
| `0x00190380` | `int mm_EvAlien_Check(EventData *data, CityScene *city)` | 832 | no | type 16 "Alien": Check |
| `0x001906C0` | `int mm_EvBodyguard_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 140 | no | type 5: Slot14 |
| `0x00190750` | `void mm_EvEscort_End(EventData *data, CityScene *city)` | 148 | no | End shared by types 2 and 5 |
| `0x001907F0` | `void mm_EvEscort_Update(EventData *data, CityScene *city)` | 768 | yes | Update shared by types 2 and 5 |
| `0x00190AF0` | `void mm_EvEscort_Start(EventData *data, CityScene *city)` | 96 | no | Start shared by types 2 "Forgetful Granny" and 5 "Bodyguard" |
| `0x00190B50` | `int mm_EvBodyguard_Check(EventData *data, CityScene *city)` | 8 | no | type 5 "Bodyguard": Check |
| `0x00190B60` | `int mm_EvMsg_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 612 | no | Slot14 shared by types 23, 29, 30, 35, 41 |
| `0x00190DD0` | `int mm_EvPayday_Slot10(EventData *data, CityScene *city, uint32_t a2)` | 72 | indirect | Slot10 shared by types 29 and 30 |
| `0x00190F00` | `void mm_EvPayday_Update(EventData *data, CityScene *city)` | 492 | no | Update shared by types 29 and 30 |
| `0x001910F0` | `void mm_EvMsg_UpdateFinal(EventData *data, CityScene *city)` | 256 | no | Update for types 23 "Begin a farewell party", 35 "Forcibly Retreat", 41 "Mission Cleared" |
| `0x001911F0` | `void mm_EvMsg_Update(EventData *data, CityScene *city)` | 292 | no | Update shared by most message events (25-28, 31-34, 36-40) |
| `0x00191320` | `void mm_EvNoRetreat_Update(EventData *data, CityScene *city)` | 256 | no | type 24 "No retreat": Update |
| `0x00191420` | `void mm_EvMsg_End(EventData *data, CityScene *city)` | 48 | no | End shared by the message events |
| `0x00191450` | `void mm_EvMsg_Start(EventData *data, CityScene *city)` | 36 | no | Start shared by the message events (types 23-41) |
| `0x00191480` | `int mm_EvMissionCleared_Check(EventData *data, CityScene *city)` | 84 | no | type 41 "Mission Cleared": Check |
| `0x001914E0` | `int mm_EvQuota80_Check(EventData *data, CityScene *city)` | 72 | no | type 40 "80% of the quota": Check |
| `0x00191530` | `int mm_EvForciblyRetreat_Check(EventData *data, CityScene *city)` | 20 | no | type 35 "Forcibly Retreat": Check |
| `0x00191550` | `int mm_EvRetreat_Check(EventData *data, CityScene *city)` | 68 | no | type 34 "Retreat": Check |
| `0x001915A0` | `int mm_EvThreeUnsatisfied_Check(EventData *data, CityScene *city)` | 340 | no | type 33 "3 Unsatisfied things": Check |
| `0x00191700` | `int mm_EvNoOffer_Check(EventData *data, CityScene *city)` | 20 | no | type 32 "No Offer": Check |
| `0x00191720` | `int mm_EvHunger_Check(EventData *data, CityScene *city)` | 36 | no | type 31 "Hunger": Check |
| `0x00191750` | `int mm_EvPayday_Check(EventData *data, CityScene *city)` | 96 | no | type 30 "Payday": Check |
| `0x001917B0` | `int mm_EvFirstPayday_Check(EventData *data, CityScene *city)` | 16 | no | type 29 "Your First Payday": Check |
| `0x001917C0` | `int mm_EvWoman_Check(EventData *data, CityScene *city)` | 44 | no | type 28 "Woman": Check |
| `0x001917F0` | `int mm_EvRiddle_Check(EventData *data, CityScene *city)` | 44 | no | type 27 "Riddle": Check |
| `0x00191820` | `int mm_EvPopularity_Check(EventData *data, CityScene *city)` | 40 | no | type 26 "Popularity": Check |
| `0x00191850` | `uint32_t mm_EvCaveMan_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 508 | yes | type 15: Slot14 |
| `0x00191A50` | `void mm_EvCaveMan_Update(EventData *data, CityScene *city)` | 1652 | yes | type 15: Update |
| `0x001920D0` | `void mm_EvCaveMan_End(EventData *data, CityScene *city)` | 192 | no | type 15: End |
| `0x00192190` | `void mm_EvCaveMan_Start(EventData *data, CityScene *city)` | 8 | no | type 15: Start |
| `0x001921A0` | `int mm_EvCaveMan_Check(EventData *data, CityScene *city)` | 380 | no | type 15 "Cave Man": Check |
| `0x00192320` | `int mm_EvDeliveryMan_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 76 | indirect | type 3: Slot14 |
| `0x00192490` | `char *mm_EvDeliveryMan_Slot10(EventData *data, CityScene *city, int a2)` | 176 | no | type 3: Slot10 |
| `0x00192540` | `void mm_EvDeliveryMan_Update(EventData *data, CityScene *city)` | 328 | no | type 3: Update |
| `0x00192690` | `void mm_EvDeliveryMan_End(EventData *data, CityScene *city)` | 184 | no | type 3: End |
| `0x00192750` | `void mm_EvDeliveryMan_Start(EventData *data, CityScene *city)` | 116 | no | type 3: Start |
| `0x001927D0` | `int mm_EvDeliveryMan_Check(EventData *data, CityScene *city, int a2)` | 1272 | no | type 3 "Delivery Man": Check |
| `0x00192CD0` | `int mm_EvBugDamage_Check(EventData *data, CityScene *city)` | 132 | no | type 11 "Bug damage": Check (its only slot) |
| `0x00192D60` | `void mm_EventList_MoveAll(int list, int from, int to)` | 696 | no | (provisional) moves every entry from bucket a1 to a2 (event manager constructor and destructor) |
| `0x00193020` | `void mm_EventQueue_RemoveAt(int queue, int level, int position)` | 148 | no | removes the id at a2 from queue level a1 and shifts the level starts |
| `0x001930C0` | `int mm_EventQueue_InsertByPriority(int queue, uint8_t *id, int level, int a3)` | 340 | no | inserts id *a1 into level a2 keeping the type priority order (type record +0x22) |
| `0x00193220` | `void mm_PartyPopup_Draw(int popup, int pass)` | 36 | no | (provisional) vtable 0x1BD8E0 draw: queues its packet in pass 2 |
| `0x00193250` | `void mm_PartyPopup_Update(int popup, CityScene *city)` | 72 | no | (provisional) refreshes its window; button 0x40 closes (0x181430) |
| `0x001932A0` | `int mm_PartyPopup_dtor(int popup, int16_t deleteFlag)` | 144 | yes | (provisional) vtable 0x1BD8E0 then base 0x1BD8B0; deletes its frame |
| `0x00193330` | `int mm_EvFarewellParty_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 60 | no | type 19: Slot14 |
| `0x00193370` | `void mm_EvFarewellParty_End(EventData *data, CityScene *city)` | 104 | indirect | type 19: End |
| `0x001933E0` | `void mm_EvFarewellParty_Update(EventData *data, CityScene *city)` | 1344 | yes | type 19: Update |
| `0x00193920` | `void mm_EvFarewellParty_Start(EventData *data, CityScene *city)` | 520 | no | type 19 "Farewell Party": Start |
| `0x00193B30` | `int mm_EvForgetfulGranny_Check(EventData *data, CityScene *city, int a2)` | 44 | no | type 2 "Forgetful Granny": Check |
| `0x00193B60` | `int mm_EvCrazyCult_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 96 | indirect | type 18: Slot14 |
| `0x00193D00` | `void mm_EvCrazyCult_End(EventData *data, CityScene *city)` | 232 | no | type 18: End |
| `0x00193DF0` | `int mm_EvCrazyCult_Phase3(int a0, CityScene *city)` | 324 | no | (provisional) states 0x13-0x16: wait, mark the building, find the person |
| `0x00193F40` | `void mm_EvCrazyCult_Phase2(int a0, CityScene *city)` | 516 | no | (provisional) states 0xA-0xE: timed actor notifications |
| `0x00194150` | `void mm_EvCrazyCult_Phase1(int a0, CityScene *city)` | 352 | no | (provisional) states 4-8: timed actor notifications, mark the building |
| `0x001942B0` | `void mm_EvCrazyCult_Update(EventData *data, CityScene *city)` | 992 | no | type 18: Update |
| `0x00194690` | `void mm_EvCrazyCult_Start(EventData *data, CityScene *city)` | 136 | no | type 18 "Crazy Cult": Start |
| `0x00194720` | `int mm_EvAdoption_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 116 | indirect | type 12: Slot14 |
| `0x001949F0` | `void mm_EvAdoption_Slot10(EventData *data, CityScene *city, int a2)` | 56 | no | type 12: Slot10 |
| `0x00194A30` | `void mm_EvAdoption_Update(EventData *data, CityScene *city)` | 1452 | no | type 12: Update |
| `0x00194FE0` | `void mm_EvAdoption_Start(EventData *data, CityScene *city)` | 184 | no | type 12 "Adoption": Start |
| `0x001950A0` | `uint32_t mm_EvUselessMiddleAged_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 48 | indirect | type 7: Slot14 |
| `0x00195160` | `void mm_EvUselessMiddleAged_Update(EventData *data, CityScene *city)` | 160 | no | type 7: Update |
| `0x00195200` | `void mm_EvUselessMiddleAged_End(EventData *data, CityScene *city)` | 140 | no | type 7: End |
| `0x00195290` | `void mm_EvUselessMiddleAged_Start(EventData *data, CityScene *city)` | 76 | no | type 7: Start |
| `0x001952E0` | `int mm_EvUselessMiddleAged_Check(EventData *data, CityScene *city)` | 20 | no | type 7 "Useless middle-aged": Check |
| `0x00195300` | `int mm_EvMissingChildren_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 276 | no | type 6: Slot14 |
| `0x00195420` | `void mm_EvMissingChildren_End(EventData *data, CityScene *city)` | 252 | no | type 6: End |
| `0x00195520` | `void mm_EvMissingChildren_Update(EventData *data, CityScene *city)` | 1452 | yes | type 6: Update |
| `0x00195AD0` | `void mm_EvMissingChildren_Start(EventData *data, CityScene *city)` | 48 | no | type 6: Start |
| `0x00195B00` | `int mm_EvMissingChildren_Check(EventData *data, CityScene *city, int a2)` | 380 | yes | type 6 "Finds missing children": Check |
| `0x00195C80` | `int mm_EvAdvertise_PickClaimText(CityScene *city, int a1, int a2)` | 284 | no | (provisional) claim lines for the two buildings' claim types (CLAIM.BIN sections) |
| `0x00195DA0` | `int mm_EvAdvertise_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 800 | no | type 9: Slot14 |
| `0x001960C0` | `void mm_EvAdvertise_End(EventData *data, CityScene *city)` | 140 | no | type 9: End |
| `0x00196150` | `void mm_EvAdvertise_Update(EventData *data, CityScene *city)` | 276 | no | type 9: Update |
| `0x00196270` | `void mm_EvAdvertise_Start(EventData *data, CityScene *city)` | 48 | no | type 9: Start |
| `0x001962A0` | `int mm_EvAdvertise_Check(EventData *data, CityScene *city, MapPerson *person)` | 192 | no | type 9 "To advertise": Check |
| `0x00196360` | `int mm_EvShopping_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 120 | indirect | type 8: Slot14 |
| `0x001965E0` | `int mm_EvShopping_Slot10(EventData *data, CityScene *city, int a2)` | 128 | no | type 8: Slot10 |
| `0x00196660` | `void mm_EvShopping_End(EventData *data, CityScene *city)` | 192 | no | type 8: End |
| `0x00196720` | `void mm_EvShopping_Update(EventData *data, CityScene *city)` | 592 | no | type 8: Update |
| `0x00196970` | `void mm_EvShopping_Start(EventData *data, CityScene *city)` | 120 | no | type 8: Start |
| `0x001969F0` | `int mm_EvShopping_Check(EventData *data, CityScene *city, MapPerson *person)` | 1312 | indirect | type 8 "Shopping": Check |
| `0x00196F10` | `int mm_EventData_GetSize7C(void)` | 8 | no | (provisional) vtable size slot: 0x7C |
| `0x00196F20` | `int mm_EventData_GetSize1C(void)` | 8 | no | (provisional) vtable size slot: 0x1C (used by the event manager constructor) |
| `0x00196F30` | `void mm_EvNoodles_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 192 | yes | type 13: Slot14 |
| `0x00196FF0` | `void mm_EvNoodles_End(EventData *data, CityScene *city)` | 248 | no | type 13: End |
| `0x001970F0` | `void mm_EvNoodles_Update(EventData *data, CityScene *city)` | 5984 | yes | type 13: Update |
| `0x00198850` | `void mm_EvNoodles_Start(EventData *data, CityScene *city)` | 120 | no | type 13: Start |
| `0x001988D0` | `int mm_EvNoodles_Check(EventData *data, CityScene *city)` | 680 | no | type 13 "Noodles": Check |
| `0x00198B80` | `int mm_EvThief_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 252 | no | type 17: Slot14 |
| `0x00198C80` | `void mm_EvThief_End(EventData *data, CityScene *city)` | 180 | no | type 17: End |
| `0x00198D40` | `void mm_EvThief_Update(EventData *data, CityScene *city)` | 1964 | yes | type 17: Update |
| `0x001994F0` | `int mm_EvThief_Check(EventData *data, CityScene *city)` | 492 | no | type 17 "Thief": Check |
| `0x001996E0` | `int mm_EvRelationship_Slot14(EventData *data, CityScene *city, uint16_t *actor, int mode)` | 388 | no | Slot14 shared by types 20-22 |
| `0x00199870` | `int mm_EvAffair_Check(EventData *data, CityScene *city, int a2)` | 564 | no | type 22 "An affair of": Check |
| `0x00199AB0` | `int mm_EvBoyfriend_Check(EventData *data, CityScene *city, int a2)` | 604 | no | type 21 "Boyfriend of": Check |
| `0x00199D10` | `int mm_EvGirlfriend_Check(EventData *data, CityScene *city, int a2)` | 604 | no | type 20 "Girlfriend of": Check |
| `0x00199F70` | `int mm_EvActor_Notify(CityScene *city, uint16_t *actor)` | 212 | no | (provisional) if the actor (person index, 6000 = none) is on a valid tile, posts message (a2/a3) through the message manager (city +0x6C, 0x16C5F0) |
| `0x0019A050` | `int mm_EvActor_PointCursor(CityScene *city, uint16_t *actor)` | 236 | yes | (provisional) when nobody holds input focus, points the city cursor at the actor's building or position (0x1815E0) |
| `0x0019A140` | `int mm_EvActor_PointAtBuilding(CityScene *city, int actorPerson, int building)` | 108 | yes | (provisional) points the cursor at building a2 and moves to the actor |
| `0x0019A1B0` | `void mm_EvActor_Wander(CityScene *city, uint16_t *actor)` | 752 | no | (provisional) picks a random target building for the actor; sets night flag 0x40 by the time-of-day phase |
| `0x0019A4A0` | `int mm_EvActor_FindShop(CityScene *city, float *position)` | 524 | no | (provisional) searches all buildings with type flag 0x40 for a target for the actor |
| `0x0019A6B0` | `void mm_EvActor_StopFollowing(int a0, uint16_t *actor)` | 84 | no | clears the follow flags (0x20, 0x80) and the follow target of person a1 |
| `0x0019A710` | `int mm_EvActor_Follow(CityScene *city, uint16_t *actor, int16_t *target)` | 332 | no | person a1 leaves its building and follows person a2 (flag 0x20, target +0x20) |
| `0x0019A860` | `int mm_EvActor_Detach(CityScene *city, uint16_t *actor)` | 308 | no | person a1 leaves its building, follow flag set with no target |
| `0x0019A9A0` | `void mm_EvActor_Release(CityScene *city, uint16_t *actor)` | 212 | no | person a1 leaves its building and drops flags 0x80/0x100 |
| `0x0019AB20` | `int mm_EvActor_MoveTo(CityScene *city, uint16_t *actor, int building, int arrive)` | 816 | no | moves person a1 towards building a2; 1 on arrival |
| `0x0019B4F0` | `int mm_EvAlien_FindBuilding(CityScene *city, int a1, uint32_t type, int32_t *rect)` | 400 | no | (provisional) a free building of type a2 whose footprint fits |
| `0x0019B680` | `int mm_EvThief_FindBuilding(CityScene *city, uint32_t category, int32_t *rect)` | 448 | no | (provisional) a free building of category a1 (type +0x145) whose footprint fits |
