# event

City events (visitors, requests, incidents, paydays).

- Types come from `DATA\BIN\EVENT.BIN`: 96 `EventType` records of 0x4C bytes, loaded to gp `0x001BDA84`. Each has a
  title, a group and a number in it (1 residents' requests, 2 visitors and incidents, 3 the farewell party, 4 city news
  and trends, 5 residents' pastimes and gossip, 6 the game's own: paydays, population milestones, retreat, mission
  cleared), its queue level and priority, a handler id, the window it may start in and the time it finishes (minutes
  of the day), the days it runs and then waits, and the conditions `mm_Event_CheckCondition` tests: population, laptop
  modules, the person's character type, gender and friendship, the workplace's type and claim meter, where the person
  stands, and the score and approval tiers.
- The handler id indexes the table at `0x001AF0D0`, which points at one `EventHandler` per id with six slots: Check
  (may the event start), Start, Update (every frame), End, Text (a text for the talk script) and Answer (the talk
  script's questions and actions).
  `mm_Ev<Name>_<Slot>` functions are named after the event titles.
- The event manager (`EventMgr`, `mm_EventMgr_*`, city `+0x68`) keeps the minute and population rank the queue last
  moved at, five `EventSlot`s (events talk scripts started, with the residents they involve, counting days down) and
  `talks[3]` (`+0xA4`), the event records the talk script works with. The waiting queue (`EventQueue` at
  `0x001BDA88`, `mm_EventQueue_*`, `mm_EventList_*`) keeps entries (`EventQueueEntry`: type, days to wait) in eight
  buckets, each in priority order; the moves take an `EventTest` (the rank, level or mask to match). The types' ids
  (`0x001C5790`, the count at `0x001BDA80`) are saved with the city, to match the queue with a later table
  (`mm_Event_InitForCity`). The buckets: 0 and 1 feed the two talk events, 2 the talk scripts, 3 a person's talk, 4 counts
  days down, 5 waits for a population rank; every type starts in 6, and finished ones go to 6 or 7 by their wait.
- `mm_EvActor_*` moves the people taking part (follow, move to, detach, notify).
- Every handler gets the active event's record (`EventData`, 0x1C bytes: its class, its handler's Update slot, the
  kind, the days left, var, the main actor at `+0xC` as an `ActorRef` of person, building, family and member, then two
  words per type). Types with more state keep up to 0x7C bytes and list further actors from `+0xC` on, 8 bytes each.
- Per-type record layouts in `config/types/05_classes.h`: `EvCultData` (Crazy Cult: six actors, the wait month and time,
  the spread counter, the members met), `EvAdoptionData` (the applying families and six `AdoptionChild` entries),
  `EvShoppingData` (the shopper, four sellers and their prices, the bill), `EvDeliveryData` (the catalogue entry and
  month, the delivery man's timers), `EvPartyData` (the farewell party: three actors, the guests, the party time),
  `EvThiefData` (the next strike's month, buildings robbed), `EvAdvertiseData` (the family members it brought),
  `EvNoodlesData` (three shops' workers and buildings), `EvMiddleAgedData` (the month, the talk's answer),
  `EvShoppingTogetherData` (the two actors, the countdown, the outcome), `EvAlienData` (six actors, the day and time
  waited for, a frame count) and `EvMessageData` (the message events and the
  payday: whether their phone call is up). The
  handlers see them through `data.as<...>()`.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

148 functions (ported 2, verified 146). Blocks: 20 can reach a thread switch, 21 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001356F0` | `void mm_Event_ClearTileFlags(int listIndex)` | 120 | no | (listIndex) restores the claim records of event `listIndex`'s tile list (claimData lists at 0x1BDA60): each record's first 8 bytes from its second 8 |
| `0x00135770` | `void mm_Event_SetTileFlags(int listIndex)` | 120 | no | (listIndex) copies each entry of event `listIndex`'s tile list (record, 8 bytes; 12 bytes per entry) over its claim record's first 8 bytes |
| `0x001378D0` | `void mm_Event_InitForCity(EventMgr *events)` | 1448 | indirect | after a game is loaded the event lists are matched with the event table of this build: the ids saved with the game (the list at 0x1C5790, one halfword per type record) against the table's. A type that was not in the saved list is put in the waiting queue (by its rank and level) and every stored type number from it on goes up by one |
| `0x00137EA0` | `void mm_Event_LoadState(EventMgr *events, McManager *mc)` | 164 | indirect | (events, mc) reads from the save file the event type ids (0x200 bytes), the event queue (0x224), the manager itself (0xA4) and the data of its two talk events |
| `0x00137F50` | `void mm_Event_SaveState(EventMgr *events, McManager *mc)` | 164 | indirect | writes them |
| `0x00138000` | `int64_t mm_Event_CallAnswer(EventMgr *events, CityScene *city, ActorRef *actor, int arg, int index)` | 128 | indirect | (events, city, actor, arg, index) calls the answer slot of the handler of talk event `index` (0 talks[0], 1 talks[1]) with the event, the city, `actor` and `arg` |
| `0x00138080` | `int64_t mm_Event_CallText(EventMgr *events, CityScene *city, int index, int arg)` | 128 | indirect | (events, city, index, arg) calls the text slot of the handler of the type of active event `index` |
| `0x00138100` | `void mm_Event_Update(EventMgr *events, CityScene *city)` | 2544 | indirect | per frame: starts queued events whose conditions hold, runs and finishes active ones |
| `0x00138AF0` | `int64_t mm_Event_CheckCondition(EventMgr *events, CityScene *city, uint8_t *type, MapPerson *person)` | 1712 | indirect | whether the event type at `type` (a queue entry) may start now, for the person (null for a script-less start) |
| `0x001391B0` | `uint8_t mm_Event_TryStart(EventMgr *events, CityScene *city, MapPerson *person)` | 504 | indirect | with no event running in the first talk slot, the first event waiting at level 3 whose condition holds for the person starts there; one that starts at level 3 also sets its tile flags and runs its Start slot. The result is the running event's type |
| `0x001393B0` | `int mm_Event_FindForFamily(EventMgr *events, CityScene *city, int choice, int16_t *familyMember)` | 708 | yes | whether the family member `familyMember` (family, member) can be added to the event slot `choice`: 4 when the member is already its second person, 0 when the event type lists the character types it takes (charTypes, up to four) and the member's is not among them, 3 when the member is its first person, 1 when the member is already on its list of five, otherwise 2 (the member goes on the list: straight away, or, for a type whose handler has an answer slot, only when that answer, asked with a probe of the slot's second person and the member as argument, says yes) |
| `0x00139680` | `void mm_EventMgr_RequeueFirst(EventMgr *events)` | 128 | no | puts the first talk slot's event back in the queue (priority level 2), moves the other slots up and clears the last |
| `0x00139700` | `int mm_Event_StartFromScript(EventMgr *events, CityScene *city, MapPerson *person)` | 460 | indirect | a talk script starts an event for the person: with fewer than five events running and none of them waiting on the person's character type (the type's charTypes[0]), the first event waiting at level 2 whose condition holds takes the first running slot (the others move up), with the person as actor A and the actor of the third talk event (talks[2]) as B |
| `0x001398D0` | `char *mm_Event_FormatText(EventMgr *events, int slot)` | 296 | yes | the title text of an event slot: the event type's title, or with a second family in the slot (familyB) the title followed by that member's name, cut to fit 24 characters and ended with a 0xC4 byte when it is cut. Built at 0x1C5990 |
| `0x00139A00` | `int mm_Event_CountActive(EventMgr *events)` | 56 | no | talk slots in use (EventMgr +4, 0x20 bytes each, up to 5; counting stops at the first free one) |
| `0x00139A40` | `void mm_Event_ReleaseTiles(CityScene *city)` | 264 | no | buildings in the 80 tile lists whose claim (section B entry +2) is 0 drop it: meter 0, claim slot 0x18, flags 8 and 0x40 cleared |
| `0x00139B50` | `void mm_Event_Finish(EventMgr *events, CityScene *city, int index)` | 356 | indirect | (events, city, index) ends active event `index`: releases its tiles, calls its handler, removes it (called by the event handlers) |
| `0x00139CC0` | `void mm_Event_Abort(EventMgr *events, CityScene *city, int index)` | 272 | indirect | the talk event in slot `index` is cut short: its handler ends it, and it goes back to the waiting queue (level 3 for a type with level 4) as it was |
| `0x00139DD0` | `int mm_Event_Trigger(EventMgr *events, int16_t id, CityScene *city, MapPerson *person)` | 820 | indirect | an event id (the position in the event list of the loaded EVENT.BIN) is triggered from outside (a script, a talk): the waiting entries of that id in bucket 5 are tried; the first whose condition holds takes the talk slot (1 for id 6, else 0) when that is free or its event has a higher priority value (it is aborted for slot 1, else finished first), runs its Start slot and the call returns 1 |
| `0x0013A110` | `int64_t mm_Event_CallHandlerSlot(EventMgr *events, CityScene *city, int index, int arg)` | 204 | indirect | the talk starts the event's own handler (the Start slot at +4): its tile flags are set, the event's var becomes 1 and the handler runs. Without a handler the result register stays as it was (after the tile flags call) |
| `0x0013A1E0` | `int mm_EventMgr_FindType(EventMgr *events, int16_t id)` | 104 | no | the event type (row of the table) with id `id`, from the id table (0x1C5790); the type count when there is none |
| `0x0013A250` | `EventMgr *mm_EventMgr_dtor(EventMgr *events, int16_t deleteFlag)` | 264 | yes | frees the event lists |
| `0x0013A360` | `EventMgr *mm_EventMgr_ctor(EventMgr *events)` | 500 | yes | creates the event lists and handler objects |
| `0x0013A560` | `void mm_Event_LoadTypes(void)` | 516 | yes | loads DATA\BIN\EVENT.BIN (0x4C-byte type records) |
| `0x0013A770` | `void mm_EventList_MoveDue(EventQueue *queue, int from, int to, EventTest *test)` | 764 | no | bucket `from` -> `to`: the entries whose wait ran out |
| `0x0013AA70` | `void mm_EventList_MoveDueLevel(EventQueue *queue, int from, int to, EventTest *test)` | 884 | no | bucket `from` -> `to`: the entries whose wait ran out and whose type's level is the test's |
| `0x0013ADF0` | `void mm_EventList_MoveInRank(EventQueue *queue, int from, int to, EventTest *test)` | 788 | no | bucket `from` -> `to`: the entries whose type runs at the test's rank (its ranks share a bit with it) |
| `0x0013B110` | `void mm_EventList_MoveInRankLevel(EventQueue *queue, int from, int to, EventTest *test)` | 844 | no | bucket `from` -> `to`: the entries whose type runs at the test's rank and has the test's level |
| `0x0013B460` | `void mm_EventList_MoveOutOfRank(EventQueue *queue, int from, int to, EventTest *test)` | 804 | no | bucket `from` -> `to`: the entries whose type does not run at the test's rank (to bucket 6 when the rank goes up) |
| `0x0013B790` | `void mm_EventList_MoveByMask(EventQueue *queue, int from, int to, EventTest *test)` | 788 | no | bucket `from` -> `to`: the entries whose type's mask shares a bit with the test's (the stage's bit, event manager constructor) |
| `0x0018F5D0` | `int mm_EvShoppingTogether_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 32 | no | (data, city, actor, mode) type 1: Answer (returns the outcome when `mode` == 0) |
| `0x0018F5F0` | `void mm_EvShoppingTogether_Update(EventData *data, CityScene *city)` | 900 | yes | type 1: Update |
| `0x0018F980` | `void mm_EvShoppingTogether_End(EventData *data, CityScene *city)` | 192 | no | type 1: End (clears the building marks) |
| `0x0018FA40` | `MapObject *mm_EvShoppingTogether_Start(EventData *data, CityScene *city)` | 116 | no | type 1: Start (marks the two buildings, timer 180) |
| `0x0018FAC0` | `int mm_EvShoppingTogether_Check(EventData *data, CityScene *city, MapPerson *person)` | 460 | no | event type 1 "Shopping together": slot Check |
| `0x0018FC90` | `int mm_EvAlien_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 56 | yes | type 16: Answer; at its start the first Spaceman's Home family applies |
| `0x0018FCD0` | `void mm_EvAlien_End(EventData *data, CityScene *city)` | 120 | no | type 16: End (clears the building marks) |
| `0x0018FD50` | `MapObject *mm_EvAlien_PickTarget(CityScene *city, int x, int z)` | 400 | no | the building the alien lands on at (x, z): of the (tile list, type) pairs at 0x1B3760 (ended by list 0), the first building whose family lives in the city, without a maxed meter, whose footprint widened by 1 holds the point |
| `0x0018FEE0` | `void mm_EvAlien_Update(EventData *data, CityScene *city)` | 1184 | yes | type 16: Update (finishes after 720 ticks) |
| `0x00190380` | `int mm_EvAlien_Check(EventData *data, CityScene *city)` | 832 | no | type 16 "Alien": Check |
| `0x001906C0` | `int mm_EvBodyguard_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 140 | no | type 5: Answer |
| `0x00190750` | `void mm_EvEscort_End(EventData *data, CityScene *city)` | 148 | no | End shared by types 2 and 5 |
| `0x001907F0` | `void mm_EvEscort_Update(EventData *data, CityScene *city)` | 768 | yes | Update shared by types 2 and 5 |
| `0x00190AF0` | `int mm_EvEscort_Start(EventData *data, CityScene *city)` | 96 | no | Start shared by types 2 "Forgetful Granny" and 5 "Bodyguard" |
| `0x00190B50` | `int mm_EvBodyguard_Check(EventData *data, CityScene *city)` | 8 | no | type 5 "Bodyguard": Check |
| `0x00190B60` | `int mm_EvMsg_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 612 | no | Answer shared by types 23, 29, 30, 35, 41 |
| `0x00190DD0` | `char *mm_EvPayday_Text(EventData *data, CityScene *city, uint32_t value)` | 72 | indirect | Text shared by types 29 and 30 |
| `0x00190F00` | `void mm_EvPayday_Update(EventData *data, CityScene *city)` | 492 | no | Update shared by types 29 and 30 |
| `0x001910F0` | `void mm_EvMsg_UpdateFinal(EventData *data, CityScene *city)` | 256 | no | Update for types 23 "Begin a farewell party", 35 "Forcibly Retreat", 41 "Mission Cleared" |
| `0x001911F0` | `void mm_EvMsg_Update(EventData *data, CityScene *city)` | 292 | no | Update shared by most message events (25-28, 31-34, 36-40) |
| `0x00191320` | `void mm_EvNoRetreat_Update(EventData *data, CityScene *city)` | 256 | no | type 24 "No retreat": Update |
| `0x00191420` | `void mm_EvMsg_End(EventData *data, CityScene *city)` | 48 | no | End shared by the message events |
| `0x00191450` | `int64_t mm_EvMsg_Start(EventData *data, CityScene *city)` | 36 | no | Start shared by the message events (types 23-41): the message comes from the boss (Boss' Home family, member 0) |
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
| `0x00191850` | `int mm_EvCaveMan_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 508 | yes | type 15: Answer; modes 0 to 3 bring in the four Staff Accom families, 7 the first Caveman's Home family |
| `0x00191A50` | `void mm_EvCaveMan_Update(EventData *data, CityScene *city)` | 1652 | yes | type 15: Update |
| `0x001920D0` | `void mm_EvCaveMan_End(EventData *data, CityScene *city)` | 192 | no | type 15: End |
| `0x00192190` | `int64_t mm_EvCaveMan_Start(EventData *data, CityScene *city)` | 8 | no | type 15: Start |
| `0x001921A0` | `int mm_EvCaveMan_Check(EventData *data, CityScene *city)` | 380 | no | type 15 "Cave Man": Check; a cave with its family's first member on the map, with the first University family (member 2) and the first Caveman's Home family as actors |
| `0x00192320` | `int mm_EvDeliveryMan_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 76 | indirect | type 3: Answer |
| `0x00192490` | `char *mm_EvDeliveryMan_Text(EventData *data, CityScene *city, int index)` | 176 | no | type 3: Text |
| `0x00192540` | `void mm_EvDeliveryMan_Update(EventData *data, CityScene *city)` | 328 | no | type 3: Update |
| `0x00192690` | `void mm_EvDeliveryMan_End(EventData *data, CityScene *city)` | 184 | no | type 3: End |
| `0x00192750` | `MapObject *mm_EvDeliveryMan_Start(EventData *data, CityScene *city)` | 116 | no | type 3: Start |
| `0x001927D0` | `int mm_EvDeliveryMan_Check(EventData *data, CityScene *city, MapPerson *person)` | 1272 | no | type 3 "Delivery Man": Check |
| `0x00192CD0` | `int mm_EvBugDamage_Check(EventData *data, CityScene *city)` | 132 | no | type 11 "Bug damage": Check (its only slot) |
| `0x00192D60` | `void mm_EventList_MoveAll(EventQueue *queue, int from, int to)` | 696 | no | bucket `from` -> `to`: every entry (event manager constructor and destructor) |
| `0x00193020` | `void mm_EventQueue_RemoveAt(EventQueue *queue, int bucket, EventQueueEntry *entry)` | 148 | no | (queue, bucket, entry) removes the id at `entry` from queue level `bucket` and shifts the level starts |
| `0x001930C0` | `void mm_EventQueue_InsertByPriority(EventQueue *queue, EventQueueEntry *entry, int bucket, int start)` | 340 | no | (queue, entry, bucket, start) inserts id *`entry` into level `bucket` keeping the type priority order (the type's priority) |
| `0x00193220` | `void mm_PartyPopup_Draw(int popup, int pass)` | 36 | no | in draw pass 2 the popup frame packet (+4) is called |
| `0x00193250` | `void mm_PartyPopup_Update(int popup, CityScene *city)` | 72 | no | shows notice 1 and closes on Circle (0x40) |
| `0x001932A0` | `int mm_PartyPopup_dtor(int popup, int16_t deleteFlag)` | 144 | yes | the vtable steps down to the derived class's destructor state and back to the base class's; the GS is waited for before the frame it may still draw is deleted |
| `0x00193330` | `int mm_EvFarewellParty_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 60 | no | type 19: Answer |
| `0x00193370` | `void mm_EvFarewellParty_End(EventData *data, CityScene *city)` | 104 | indirect | type 19: End |
| `0x001933E0` | `void mm_EvFarewellParty_Update(EventData *data, CityScene *city)` | 1344 | yes | type 19: Update |
| `0x00193920` | `MapObject *mm_EvFarewellParty_Start(EventData *data, CityScene *city)` | 520 | no | type 19 "Farewell Party": Start; the boss's family (Boss' Home) leaves, and residents who are close friends come |
| `0x00193B30` | `int mm_EvForgetfulGranny_Check(EventData *data, CityScene *city, MapPerson *person)` | 44 | no | type 2 "Forgetful Granny": Check |
| `0x00193B60` | `int mm_EvCrazyCult_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 96 | indirect | type 18: Answer |
| `0x00193D00` | `void mm_EvCrazyCult_End(EventData *data, CityScene *city)` | 232 | no | type 18: End |
| `0x00193DF0` | `void mm_EvCrazyCult_Phase3(EventData *data, CityScene *city)` | 324 | no | phases 0x12 to 0x15: actor 0 waits and is told (0x12, 0x13), then its family home is found (0x14) and its person (0x15) |
| `0x00193F40` | `void mm_EvCrazyCult_Phase2(EventData *data, CityScene *city)` | 516 | no | phases 10 to 16: 10 waits until everyone has been reached (allJoined), 11 and 13 tell actor 3 when its time comes and 12 and 14 count those frames back, 15 finds the homes of actors 4, 5 and 1 (marked 0x200; back to 15 while one is missing) and 16 their persons (back to 16) |
| `0x00194150` | `void mm_EvCrazyCult_Phase1(EventData *data, CityScene *city)` | 352 | no | phases 4 to 9: actor 2 waits and is told (4 to 7), then actor 3's family home is found (8) and its person (9, which sets the next wait to the next month at 7:00) |
| `0x001942B0` | `void mm_EvCrazyCult_Update(EventData *data, CityScene *city)` | 992 | no | type 18: Update |
| `0x00194690` | `int64_t mm_EvCrazyCult_Start(EventData *data, CityScene *city)` | 136 | no | type 18 "Crazy Cult": Start; the Zen Monk's and Shadow Monk's families and the first four Crazy Cult families as actors |
| `0x00194720` | `int mm_EvAdoption_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 116 | indirect | type 12: Answer |
| `0x001949F0` | `int mm_EvAdoption_Text(EventData *data, CityScene *city, int index)` | 56 | no | type 12: Text |
| `0x00194A30` | `void mm_EvAdoption_Update(EventData *data, CityScene *city)` | 1452 | no | type 12: Update |
| `0x00194FE0` | `uint32_t mm_EvAdoption_Start(EventData *data, CityScene *city)` | 184 | no | type 12 "Adoption": Start; the boss (Boss' Home family, member 0) asks, and the families with an application are listed |
| `0x001950A0` | `uint32_t mm_EvUselessMiddleAged_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 48 | indirect | type 7: Answer |
| `0x00195160` | `void mm_EvUselessMiddleAged_Update(EventData *data, CityScene *city)` | 160 | no | type 7: Update |
| `0x00195200` | `void mm_EvUselessMiddleAged_End(EventData *data, CityScene *city)` | 140 | no | type 7: End |
| `0x00195290` | `MapObject *mm_EvUselessMiddleAged_Start(EventData *data, CityScene *city)` | 76 | no | type 7: Start |
| `0x001952E0` | `int mm_EvUselessMiddleAged_Check(EventData *data, CityScene *city)` | 20 | no | type 7 "Useless middle-aged": Check |
| `0x00195300` | `int mm_EvMissingChildren_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 276 | no | type 6: Answer |
| `0x00195420` | `void mm_EvMissingChildren_End(EventData *data, CityScene *city)` | 252 | no | type 6: End |
| `0x00195520` | `void mm_EvMissingChildren_Update(EventData *data, CityScene *city)` | 1452 | yes | type 6: Update |
| `0x00195AD0` | `MapObject *mm_EvMissingChildren_Start(EventData *data, CityScene *city)` | 48 | no | type 6: Start |
| `0x00195B00` | `int mm_EvMissingChildren_Check(EventData *data, CityScene *city, MapPerson *person)` | 380 | yes | type 6 "Finds missing children": Check |
| `0x00195C80` | `int mm_EvAdvertise_ClaimsMatch(CityScene *city, ActorRef *actorA, ActorRef *actorB)` | 284 | no | whether an active entry (first byte 0) of building A's claim section B names an id, or a section D group of ids (0x8000 and up), that building B's section C lists |
| `0x00195DA0` | `int mm_EvAdvertise_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 800 | no | type 9: Answer |
| `0x001960C0` | `void mm_EvAdvertise_End(EventData *data, CityScene *city)` | 140 | no | type 9: End |
| `0x00196150` | `void mm_EvAdvertise_Update(EventData *data, CityScene *city)` | 276 | no | type 9: Update |
| `0x00196270` | `MapObject *mm_EvAdvertise_Start(EventData *data, CityScene *city)` | 48 | no | type 9: Start |
| `0x001962A0` | `int mm_EvAdvertise_Check(EventData *data, CityScene *city, MapPerson *person)` | 192 | no | type 9 "To advertise": Check; the person's own shop (category 7 or 9) advertises in its family's month, or any month while newly built |
| `0x00196360` | `int mm_EvShopping_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 120 | indirect | type 8: Answer |
| `0x001965E0` | `char *mm_EvShopping_Text(EventData *data, CityScene *city, int index)` | 128 | no | type 8: Text |
| `0x00196660` | `void mm_EvShopping_End(EventData *data, CityScene *city)` | 192 | no | type 8: End |
| `0x00196720` | `void mm_EvShopping_Update(EventData *data, CityScene *city)` | 592 | no | type 8: Update |
| `0x00196970` | `int64_t mm_EvShopping_Start(EventData *data, CityScene *city)` | 120 | no | type 8: Start |
| `0x001969F0` | `int mm_EvShopping_Check(EventData *data, CityScene *city, MapPerson *person)` | 1312 | indirect | type 8 "Shopping": Check |
| `0x00196F10` | `int mm_TalkEventData_DataSize(void)` | 8 | no | the talk event record class's data size (vtable 0x1BD8F8 slot +8, EventMgr.talks[0]): 0x7C |
| `0x00196F20` | `int mm_EventData_DataSize(void)` | 8 | no | the base event record class's data size (vtable 0x1BD908 slot +8): 0x1C bytes after its first two words |
| `0x00196F30` | `int mm_EvNoodles_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 192 | yes | type 13: Answer; at its start the first Noodle Shop family applies and is set up as two actors with the Studio Appartment (type 0xB8) family; at its end that family applies too |
| `0x00196FF0` | `void mm_EvNoodles_End(EventData *data, CityScene *city)` | 248 | no | type 13: End |
| `0x001970F0` | `void mm_EvNoodles_Update(EventData *data, CityScene *city)` | 5984 | yes | type 13: Update |
| `0x00198850` | `int64_t mm_EvNoodles_Start(EventData *data, CityScene *city)` | 120 | no | type 13: Start |
| `0x001988D0` | `int mm_EvNoodles_Check(EventData *data, CityScene *city)` | 680 | no | type 13 "Noodles": Check |
| `0x00198B80` | `int mm_EvThief_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 252 | no | type 17: Answer |
| `0x00198C80` | `void mm_EvThief_End(EventData *data, CityScene *city)` | 180 | no | type 17: End |
| `0x00198D40` | `void mm_EvThief_Update(EventData *data, CityScene *city)` | 1964 | yes | type 17: Update |
| `0x001994F0` | `int mm_EvThief_Check(EventData *data, CityScene *city)` | 492 | no | type 17 "Thief": Check; the first families of types 0xB9 (Studio Appartment) and 0x105 (Police Station) must live in the city with a home, and a building to rob be found |
| `0x001996E0` | `int mm_EvRelationship_Answer(EventData *data, CityScene *city, ActorRef *actor, int mode)` | 388 | no | Answer shared by types 20-22 |
| `0x00199870` | `int mm_EvAffair_Check(EventData *data, CityScene *city, MapPerson *person)` | 564 | no | type 22 "An affair of": Check |
| `0x00199AB0` | `int mm_EvBoyfriend_Check(EventData *data, CityScene *city, MapPerson *person)` | 604 | no | type 21 "Boyfriend of": Check |
| `0x00199D10` | `int mm_EvGirlfriend_Check(EventData *data, CityScene *city, MapPerson *person)` | 604 | no | type 20 "Girlfriend of": Check |
| `0x00199F70` | `int mm_EvActor_Notify(CityScene *city, ActorRef *actor)` | 212 | no | phones the player for the actor (mm_PhoneCall_Post) unless their person stands on no building of theirs (then 0); actors without a person always call |
| `0x0019A050` | `int mm_EvActor_PointCursor(CityScene *city, ActorRef *actor)` | 236 | yes | an event actor pointed at with the cursor: with the pad focus held elsewhere nothing happens (0). An actor that is a family member (person 6000) starts a family talk (argument 3). Any other actor has the building under its person found on the map by the person's tile; a talk with that building and person starts (mm_CityScene_StartTalk, argument 2), and with none nothing starts |
| `0x0019A140` | `int mm_EvActor_PointAtBuilding(CityScene *city, int actorPerson, int building)` | 108 | yes | an event actor points at a building: unless someone holds the pad focus, a talk about the building (its id at +2) starts and its person is selected from the actor's family and member (+4, +6). 1 when the talk started |
| `0x0019A1B0` | `void mm_EvActor_Wander(CityScene *city, ActorRef *actor)` | 752 | no | in the first minute of the day the person takes the weather flag. Outside or at home, the person goes into a shop away from them: tile lists from a random one on (64 tries), the first shop (type flag 0x40) with a walkable entrance, not their home, that they are not within 5 tiles of; none found leaves them outside |
| `0x0019A4A0` | `int mm_EvActor_FindShop(CityScene *city, MapPerson *person)` | 524 | no | whether there is a shop (type flag 0x40) with a walkable entrance, other than the person's home, that the person is not within 5 tiles of (the shop at the entrance tile, its footprint widened by 5) |
| `0x0019A6B0` | `void mm_EvActor_StopFollowing(CityScene *city, ActorRef *actor)` | 84 | no | (city, actor) the actor's person stops following (flags 0x20 and 0x80 cleared, follow target none) |
| `0x0019A710` | `int mm_EvActor_Follow(CityScene *city, ActorRef *actor, ActorRef *target)` | 332 | no | (city, actor, target) person `actor` leaves its building and follows person `target` (flag 0x20, follow) |
| `0x0019A860` | `int mm_EvActor_Detach(CityScene *city, ActorRef *actor)` | 308 | no | (city, actor) person `actor` leaves its building, follow flag set with no target |
| `0x0019A9A0` | `void mm_EvActor_Release(CityScene *city, ActorRef *actor)` | 212 | no | (city, actor) person `actor` leaves its building and drops flags 0x80/0x100 |
| `0x0019AB20` | `int mm_EvActor_MoveTo(CityScene *city, ActorRef *actor, ActorRef *target, int arrive)` | 816 | no | (city, actor, target, arrive) moves person `actor` towards building `target`; 1 on arrival |
| `0x0019B4F0` | `MapObject *mm_EvAlien_FindBuilding(CityScene *city, int list, uint32_t type, int32_t *rect)` | 400 | no | the first available building of `type` in tile list `list` that overlaps the rectangle |
| `0x0019B680` | `MapObject *mm_EvThief_FindBuilding(CityScene *city, uint32_t category, int32_t *rect)` | 448 | no | the first available building of `category` in any of the 80 tile lists that overlaps the rectangle |
