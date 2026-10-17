# talk

Visiting buildings and talking to people, phone calls from the Town's Pages, and the My Home visit.

Flow: `mm_CityScene_StartTalk` calls `mm_Talk_Start`, which picks a view class from the target and the camera
distance (over 20 units the view fades and cuts the camera, otherwise it pans). The view (a widget, `ui`) shows the
building and the people there; the player picks one (`mm_Talk_ChooseInput`) and `mm_TalkConv_Start` loads the WORDS
script for that person (`mm_Words_LoadForPerson`) or family member (`mm_Words_LoadForFamily`).

| Class | Vtable | Used for |
|---|---|---|
| `mm_Talk_*` session | `0x1BD770` | one per city: `+0xC` city, `+0x10` view, `+0x14` conversation, `+0x18` building, `+0x1C` person (6000 none), `+0x20` family, `+0x24` member (6 = whole family), `+0x30` single target, `+0x40` 32 pose entries |
| `mm_TalkView_*` base | `0x1BD450` | `+0x34` city, `+0x70` building (`0x3F80` = by family), `+0x74` family, `+0x260` target |
| `mm_TalkPhoneFamily_*` / `mm_TalkPhoneMember_*` | `0x1BD4A0` / `0x1BD4F0` | a whole family / one member, from the Town's Pages |
| `mm_TalkBldgFar_*` / `mm_TalkBldgNear_*` | `0x1BD540` / `0x1BD590` | a building and everyone in it |
| `mm_TalkOccupantFar_*` / `mm_TalkOccupantNear_*` | `0x1BD5E0` / `0x1BD630` | one person at a building |
| `mm_TalkPersonFar_*` / `mm_TalkPersonNear_*` | `0x1BD680` / `0x1BD6D0` | a person outside |
| `mm_MyHome_*` | `0x1BD810` | walking inside the player's home; leaving refills stamina |
| `mm_TalkCard_*` | `0x1BD720` | name, age and "met today" card |
| `mm_TalkMoney_*` | `0x1BD7C0` | money panel during shopping |
| `mm_TalkConv_*` | `0x1BD860` | the conversation, a message window subclass |

Session states: 1 view opening, 2 conversation opening, 3 talking, 4 conversation closing, 5 view closing, 6
choosing a person.

WORDS script (`mm_Words_*`): the WORDS object is at `0x001BDDF0` (`+0` context pointer, `+0x10` line, `+0x24` line
table). `DATA\BIN\WORDS.BIN`, `WORDSEVE.BIN`, `WORDSCOM.BIN` and `WORDSTEN.BIN` are loaded by `mm_Words_Init`;
their table descriptors live at `0x007181C0`. Script codes: `5000 + file * 1000 + index` select texts (file 2 is
indexed by building type); -6000 .. -2001 open a choice menu (-2001 also shows the money panel); -2000 and up open
name entry; -6007 ends the talk and applies the friendship gain. Conditions, text getters and actions are the 147
`mm_WordsFn_*` member functions (the notes give each one's table index `#N`), called through `__ptmf_scall`; the
table template at `0x001B1B4C` is copied to `0x001B9100` by `mm_StaticInit_WordsTable`.

Script context (0x140 bytes): `+0` city, `+8` family A, `+0xA` member A, `+0xC` person A, `+0x10` building A,
`+0x14` building type B, `+0x16` family B, `+0x18` member B, `+0x1C` person B, `+0x20` building B, `+0x24` bag slot,
`+0x28` result word, `+0x2A` argument, `+0x2C`/`+0x30` candidate buildings, `+0x130` cancel allowed, `+0x13C`
friendship changed. A is the person talked to.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

322 functions (typed 311, verified 11). Blocks: 86 can reach a thread switch, 27 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001735D0` | `void mm_TalkView_CollectOccupants(TalkView *view)` | 492 | no | (provisional) people at the building sorted by grade (+0x2E), up to 6 |
| `0x001737C0` | `void mm_TalkView_ToggleAnim(TalkView *view)` | 88 | no | (provisional) flips the selected person's animation (7 -> 2/3) |
| `0x00173820` | `void mm_TalkView_DrawPeople(TalkView *view)` | 3232 | yes | people at the building or of the family in a row with names, grades and the cursor |
| `0x001744E0` | `int mm_TalkView_NobodyHome(TalkView *view, int byFamily, int target)` | 296 | yes | (provisional) 1 when no one of the family or building is there (schedule, outside flags) |
| `0x00174610` | `void mm_TalkView_DrawBuilding(TalkView *view, int byFamily, int target)` | 916 | yes | the target building (a1 0: object, 1: family's home), lit when someone is home |
| `0x001749B0` | `void mm_TalkView_BuildingCamera(TalkView *view, int object, Vec4 *a2, Vec4 *a3)` | 904 | no | camera eye and target in front of the building's entrance |
| `0x00174D40` | `void mm_TalkView_DrawCursor(TalkView *view)` | 64 | no | cursor frame at +0xC0 |
| `0x00174D80` | `uint16_t mm_TalkView_CursorPerson(TalkView *view)` | 184 | no | person index of the cursor slot (+0xBC), 0x3F80 when none |
| `0x00174E40` | `void mm_TalkView_HandleInput(TalkView *view)` | 640 | no | cursor left/right with scrolling and zoom steps (pad repeat) |
| `0x001750C0` | `int mm_TalkView_GetChoice(TalkView *view, uint32_t *person, uint32_t *slot)` | 176 | no | the chosen person (a1) and slot (a2) once a choice is made |
| `0x00175170` | `uint32_t mm_TalkView_FindPersonSlot(TalkView *view, uint32_t family, uint32_t member)` | 260 | no | slot of the person with family a1 and member a2 |
| `0x00175280` | `void mm_TalkView_CountPeople(TalkView *view)` | 200 | yes | people present (+0xC8), or family members when by family |
| `0x00175350` | `void mm_TalkView_SetTarget(TalkView *view, int building, int family)` | 288 | yes | building a1 / family a2, name text boxes and frames |
| `0x00175470` | `void mm_TalkView_PersonCamera(TalkView *view, int object, Vec4 *a2, Vec4 *a3)` | 620 | no | (provisional) camera eye and target facing a person at a building |
| `0x001756E0` | `void mm_TalkView_DrawMember(TalkView *view, int family, int member)` | 448 | yes | (provisional) family member model in front of the camera |
| `0x001758A0` | `void mm_TalkView_DrawPerson(TalkView *view, int person)` | 436 | indirect | (provisional) person model in front of the camera |
| `0x00175A60` | `void mm_TalkView_DrawPersonTurned(TalkView *view, int person, float angle)` | 268 | indirect | (provisional) person model rotated by a0 |
| `0x00175B70` | `TalkView *mm_TalkView_dtor(TalkView *view, int16_t deleteFlag)` | 212 | yes | vtable 0x1BD450, text boxes and frames |
| `0x00175C50` | `TalkView *mm_TalkView_ctor(TalkView *view, CityScene *city)` | 408 | no | vtable 0x1BD450, camera copy, models 0x1F3B2C/0x1F3B7C, packets |
| `0x00175DF0` | `void mm_TalkPhoneFamily_Draw(TalkView *view, int pass)` | 92 | yes | (provisional) vtable 0x1BD4A0, the whole family (member 6) from the Town's Pages |
| `0x00175E50` | `void mm_TalkPhoneFamily_Closed(TalkView *view)` | 64 | no | waits for the fade, then idle |
| `0x00175E90` | `void mm_TalkPhoneFamily_Closing(TalkView *view)` | 68 | no | waits for the fade, fades again |
| `0x00175EE0` | `void mm_TalkPhoneFamily_Close(TalkView *view)` | 52 | no | starts the fade and closes |
| `0x00175F20` | `void mm_TalkPhoneFamily_Active(TalkView *view)` | 8 | no |  |
| `0x00175F30` | `void mm_TalkPhoneFamily_Opened(TalkView *view)` | 52 | no |  |
| `0x00175F70` | `void mm_TalkPhoneFamily_Opening(TalkView *view)` | 68 | no | waits for the fade, fades again |
| `0x00175FC0` | `void mm_TalkPhoneFamily_Open(TalkView *view)` | 52 | no | starts the fade and opens |
| `0x00176000` | `TalkView *mm_TalkPhoneFamily_dtor(TalkView *view, int16_t deleteFlag)` | 96 | yes | vtable 0x1BD4A0 |
| `0x00176060` | `TalkView *mm_TalkPhoneFamily_ctor(TalkView *view, CityScene *city, int family)` | 88 | yes | vtable 0x1BD4A0, family a2 |
| `0x001760C0` | `void mm_TalkPhoneMember_Draw(TalkView *view, int pass)` | 92 | yes | (provisional) vtable 0x1BD4F0, one family member from the Town's Pages |
| `0x00176120` | `void mm_TalkPhoneMember_Closed(TalkView *view)` | 64 | no | waits for the fade, then idle |
| `0x00176160` | `void mm_TalkPhoneMember_Closing(TalkView *view)` | 68 | no | waits for the fade, fades again |
| `0x001761B0` | `void mm_TalkPhoneMember_Close(TalkView *view)` | 52 | no | starts the fade and closes |
| `0x001761F0` | `void mm_TalkPhoneMember_Active(TalkView *view)` | 8 | no |  |
| `0x00176200` | `void mm_TalkPhoneMember_Opened(TalkView *view)` | 52 | no | waits for the fade |
| `0x00176240` | `void mm_TalkPhoneMember_Opening(TalkView *view)` | 68 | no | waits for the fade, fades again |
| `0x00176290` | `void mm_TalkPhoneMember_Open(TalkView *view)` | 52 | no | starts the fade and opens |
| `0x001762D0` | `TalkView *mm_TalkPhoneMember_dtor(TalkView *view, int16_t deleteFlag)` | 96 | yes | vtable 0x1BD4F0 |
| `0x00176330` | `TalkView *mm_TalkPhoneMember_ctor(TalkView *view, CityScene *city, int family, int member)` | 84 | no | vtable 0x1BD4F0, family a2, member a3 |
| `0x00176390` | `void mm_TalkBldgFar_Draw(TalkView *view, int pass)` | 92 | yes | vtable 0x1BD540: building with its occupants, camera cut |
| `0x001763F0` | `void mm_TalkBldgFar_Closed(TalkView *view)` | 96 | no | restores the camera, waits for the fade |
| `0x00176450` | `void mm_TalkBldgFar_Closing(TalkView *view)` | 120 | no | building camera until the fade ends |
| `0x001764D0` | `void mm_TalkBldgFar_Close(TalkView *view)` | 52 | no | starts the fade and closes |
| `0x00176510` | `void mm_TalkBldgFar_Active(TalkView *view)` | 88 | no | building camera and cursor input |
| `0x00176570` | `void mm_TalkBldgFar_Opened(TalkView *view)` | 104 | no | building camera, waits for the fade |
| `0x001765E0` | `void mm_TalkBldgFar_Opening(TalkView *view)` | 100 | no | waits for the fade, fades again |
| `0x00176650` | `void mm_TalkBldgFar_Open(TalkView *view)` | 52 | no | starts the fade and opens |
| `0x00176690` | `TalkView *mm_TalkBldgFar_dtor(TalkView *view, int16_t deleteFlag)` | 96 | yes | vtable 0x1BD540 |
| `0x001766F0` | `TalkView *mm_TalkBldgFar_ctor(TalkView *view, CityScene *city, int object)` | 128 | yes | vtable 0x1BD540, building a2, collects its occupants |
| `0x00176770` | `void mm_TalkBldgNear_Draw(TalkView *view, int pass)` | 84 | yes | vtable 0x1BD590: building with its occupants, camera pans |
| `0x001767D0` | `void mm_TalkBldgNear_Closing(TalkView *view)` | 276 | indirect | camera pans back |
| `0x001768F0` | `void mm_TalkBldgNear_Active(TalkView *view)` | 96 | no | building camera and cursor input |
| `0x00176950` | `void mm_TalkBldgNear_Opening(TalkView *view)` | 256 | indirect | camera pans to the building |
| `0x00176A50` | `TalkView *mm_TalkBldgNear_dtor(TalkView *view, int16_t deleteFlag)` | 96 | yes | vtable 0x1BD590 |
| `0x00176AB0` | `TalkView *mm_TalkBldgNear_ctor(TalkView *view, CityScene *city, int object)` | 116 | yes | vtable 0x1BD590, building a2, collects its occupants |
| `0x00176B30` | `void mm_TalkOccupantFar_Draw(TalkView *view, int pass)` | 88 | yes | vtable 0x1BD5E0: one person at a building, camera cut |
| `0x00176B90` | `void mm_TalkOccupantFar_Closed(TalkView *view)` | 96 | no | restores the camera, waits for the fade |
| `0x00176BF0` | `void mm_TalkOccupantFar_Closing(TalkView *view)` | 120 | no | building camera until the fade ends |
| `0x00176C70` | `void mm_TalkOccupantFar_Close(TalkView *view)` | 52 | no | starts the fade and closes |
| `0x00176CB0` | `void mm_TalkOccupantFar_Active(TalkView *view)` | 80 | no | building camera |
| `0x00176D00` | `void mm_TalkOccupantFar_Opened(TalkView *view)` | 104 | no | building camera, waits for the fade |
| `0x00176D70` | `void mm_TalkOccupantFar_Opening(TalkView *view)` | 100 | no | waits for the fade, fades again |
| `0x00176DE0` | `void mm_TalkOccupantFar_Open(TalkView *view)` | 52 | no | starts the fade and opens |
| `0x00176E20` | `TalkView *mm_TalkOccupantFar_dtor(TalkView *view, int16_t deleteFlag)` | 96 | yes | vtable 0x1BD5E0 |
| `0x00176E80` | `TalkView *mm_TalkOccupantFar_ctor(TalkView *view, CityScene *city, int object, int person)` | 116 | no | vtable 0x1BD5E0, building a2, person a3 |
| `0x00176F00` | `void mm_TalkOccupantNear_Draw(TalkView *view, int pass)` | 80 | yes | vtable 0x1BD630: one person at a building, camera pans |
| `0x00176F50` | `void mm_TalkOccupantNear_Closing(TalkView *view)` | 276 | indirect | camera pans back |
| `0x00177070` | `void mm_TalkOccupantNear_Active(TalkView *view)` | 88 | no | building camera |
| `0x001770D0` | `void mm_TalkOccupantNear_Opening(TalkView *view)` | 256 | indirect | camera pans to the person |
| `0x001771D0` | `TalkView *mm_TalkOccupantNear_dtor(TalkView *view, int16_t deleteFlag)` | 96 | yes | vtable 0x1BD630 |
| `0x00177230` | `TalkView *mm_TalkOccupantNear_ctor(TalkView *view, CityScene *city, int object, int person)` | 112 | no | vtable 0x1BD630, building a2, person a3 |
| `0x001772A0` | `void mm_TalkPersonFar_Draw(TalkView *view, int pass)` | 64 | no | vtable 0x1BD680: a person outside, camera cut |
| `0x001772E0` | `void mm_TalkPersonFar_Closed(TalkView *view)` | 64 | no | waits for the fade, then idle |
| `0x00177320` | `void mm_TalkPersonFar_Closing(TalkView *view)` | 100 | no | camera until the fade ends |
| `0x00177390` | `void mm_TalkPersonFar_Close(TalkView *view)` | 52 | no | starts the fade and closes |
| `0x001773D0` | `void mm_TalkPersonFar_Active(TalkView *view)` | 8 | no |  |
| `0x001773E0` | `void mm_TalkPersonFar_Opened(TalkView *view)` | 52 | no | waits for the fade |
| `0x00177420` | `void mm_TalkPersonFar_Opening(TalkView *view)` | 100 | no | camera, waits for the fade, fades again |
| `0x00177490` | `void mm_TalkPersonFar_Open(TalkView *view)` | 52 | no | starts the fade and opens |
| `0x001774D0` | `TalkView *mm_TalkPersonFar_dtor(TalkView *view, int16_t deleteFlag)` | 96 | yes | vtable 0x1BD680 |
| `0x00177530` | `TalkView *mm_TalkPersonFar_ctor(TalkView *view, CityScene *city, int person)` | 548 | no | vtable 0x1BD680, person a2: camera facing them from the terrain heights |
| `0x00177760` | `void mm_TalkPersonNear_Draw(TalkView *view, int pass)` | 40 | no | vtable 0x1BD6D0: a person outside, camera pans |
| `0x00177790` | `void mm_TalkPersonNear_Closing(TalkView *view)` | 276 | indirect | camera pans back |
| `0x001778B0` | `void mm_TalkPersonNear_Active(TalkView *view)` | 72 | no | camera held on the person |
| `0x00177900` | `void mm_TalkPersonNear_Opening(TalkView *view)` | 268 | indirect | camera pans to the person |
| `0x00177A10` | `TalkView *mm_TalkPersonNear_dtor(TalkView *view, int16_t deleteFlag)` | 96 | yes | vtable 0x1BD6D0 |
| `0x00177A70` | `TalkView *mm_TalkPersonNear_ctor(TalkView *view, CityScene *city, int person)` | 548 | no | vtable 0x1BD6D0, person a2: camera facing them from the terrain heights |
| `0x00177CA0` | `void mm_TalkCard_FillText(TalkCard *card)` | 648 | yes | name, age and how recently met ("Met today", "Been awhile", "Lives in next town") |
| `0x00177F30` | `void mm_TalkCard_DrawGauges(TalkCard *card)` | 788 | no | (provisional) grade and friendship gauges of the person |
| `0x00178250` | `void mm_TalkCard_Draw(TalkCard *card, int pass)` | 256 | yes | vtable 0x1BD720: card model, texts and gauges while active |
| `0x00178350` | `void mm_TalkCard_Show(TalkCard *card, int person, int family, int member, int slot)` | 480 | no | opens for person a1 (or family a2 member a3) at slot a4, friendship (+0x2C) and mood (+0x34) |
| `0x00178530` | `TalkCard *mm_TalkCard_dtor(TalkCard *card, int16_t deleteFlag)` | 172 | yes | vtable 0x1BD720 |
| `0x001785E0` | `TalkCard *mm_TalkCard_ctor(TalkCard *card, CityScene *city)` | 220 | yes | vtable 0x1BD720, two text boxes and a frame |
| `0x001786C0` | `void mm_Talk_SetViewLit(TalkSession *talk)` | 24 | no | (provisional) view +0x258 = 1 (building drawn as lit), used by the caveman event |
| `0x001786E0` | `int mm_Talk_LookupPose(TalkSession *talk, uint32_t family, uint32_t member, uint32_t *pose)` | 80 | no | (provisional) value for family a1 member a2 in the 32-entry table at +0x40 |
| `0x00178730` | `void mm_Talk_SetPose(TalkSession *talk, uint16_t family, uint8_t member, uint8_t pose)` | 124 | no | stores pose a3 for family a1 member a2 in the 32-entry table at +0x40 (u16 family, u8 member, u8 pose): updates a matching entry, else fills the first free one; pair of mm_Talk_LookupPose |
| `0x001787B0` | `void mm_Talk_EndChoosing(TalkSession *talk)` | 24 | no | view (+0x10) choice (+0xB8) = -1, +0x3C = 1 |
| `0x001787D0` | `void mm_Talk_SelectPerson(TalkSession *talk, uint32_t family, uint32_t member)` | 44 | no | forces the view's choice (+0xB8) to the slot of family a1 member a2 |
| `0x00178800` | `int mm_Talk_ChooseInput(TalkSession *talk)` | 348 | yes | choosing a person: confirm (0x40) starts the conversation, cancel (0x10) closes |
| `0x00178960` | `void mm_Talk_Closed(TalkSession *talk)` | 240 | indirect | state 5: when the view is idle, pops the scene mode, deletes view and conversation, restores HUD |
| `0x00178A50` | `void mm_Talk_Closing(TalkSession *talk)` | 120 | indirect | state 4: when the conversation is idle, closes the view or goes back to choosing |
| `0x00178AD0` | `void mm_Talk_Active(TalkSession *talk)` | 104 | indirect | state 3: person animation while talking, closes when the conversation does |
| `0x00178B40` | `void mm_Talk_Opened(TalkSession *talk)` | 104 | indirect | state 2: waits for the conversation to open |
| `0x00178BB0` | `void mm_Talk_Opening(TalkSession *talk)` | 168 | yes | state 1: when the view is active, starts the conversation or choosing |
| `0x00178C60` | `int mm_Talk_Update(TalkSession *talk)` | 228 | yes | updates view and conversation, then the state machine |
| `0x00178D50` | `int mm_Talk_Start(TalkSession *talk, int byFamily, int target, int member, int arg5, CityScene *city)` | 1332 | yes | target building a2/person a3 or family a2/member a3; picks the view class (near/far, home, person) and the conversation |
| `0x00179290` | `void mm_Talk_Draw(TalkSession *talk, int pass)` | 96 | indirect | draws view and conversation |
| `0x001792F0` | `TalkSession *mm_Talk_dtor(TalkSession *talk, int16_t deleteFlag)` | 180 | yes | vtable 0x1BD770 |
| `0x001793B0` | `TalkSession *mm_Talk_ctor(TalkSession *talk)` | 68 | no | vtable 0x1BD770 |
| `0x00179400` | `void mm_TalkMoney_Draw(TalkMoney *money, int pass)` | 156 | no | vtable 0x1BD7C0: panel model and the money text |
| `0x001794A0` | `void mm_TalkMoney_Closing(TalkMoney *money)` | 68 | indirect | Step(0.1), then idle |
| `0x001794F0` | `void mm_TalkMoney_Active(TalkMoney *money)` | 100 | no |  |
| `0x00179560` | `void mm_TalkMoney_Opening(TalkMoney *money)` | 68 | indirect | Step(0.1), then active |
| `0x001795B0` | `TalkMoney *mm_TalkMoney_dtor(TalkMoney *money, int16_t deleteFlag)` | 124 | yes | vtable 0x1BD7C0 |
| `0x00179630` | `TalkMoney *mm_TalkMoney_ctor(TalkMoney *money, int amount)` | 188 | yes | vtable 0x1BD7C0, amount a1 (player money 0x1C5540) |
| `0x001796F0` | `void mm_MyHome_EntranceRect(TalkView *view, int facing, int x, int z, int size, int32_t *rect)` | 360 | no | tile rectangle in front of a building by facing (0-3), clamped to the map |
| `0x00179860` | `void mm_MyHome_MarkEntrance(TalkView *view, int set)` | 300 | no | sets (a1 1) or clears tile flag 0x80 in front of the home |
| `0x00179990` | `float mm_MyHome_FloorHit(TalkView *view, Vec4 *matrix, uint32_t *hit)` | 492 | no | (provisional) ray against the interior floor triangles (collision 0x1F3CE0) |
| `0x00179B80` | `int mm_MyHome_WallHit(TalkView *view, Vec4 *matrix, uint32_t *hit, float *distance)` | 488 | no | (provisional) ray against the interior wall triangles |
| `0x00179D70` | `void mm_MyHome_Collide(TalkView *view)` | 412 | no | (provisional) pushes the viewer out of the interior walls |
| `0x00179F10` | `void mm_MyHome_Draw(TalkView *view, int pass)` | 232 | no | vtable 0x1BD810: the home interior model while active |
| `0x0017A000` | `void mm_MyHome_Closing(TalkView *view)` | 284 | indirect | camera moves back out |
| `0x0017A120` | `void mm_MyHome_Active(TalkView *view)` | 944 | no | (provisional) walking around inside with the pad, collision and camera |
| `0x0017A4D0` | `void mm_MyHome_Opening(TalkView *view)` | 312 | indirect | camera moves into the home |
| `0x0017A610` | `void mm_MyHome_Close(TalkView *view)` | 136 | no | refills stamina (mm_Stamina_Init) when below the maximum, clears the entrance, closes |
| `0x0017A6A0` | `TalkView *mm_MyHome_dtor(TalkView *view, int16_t deleteFlag)` | 96 | yes | vtable 0x1BD810 |
| `0x0017A700` | `TalkView *mm_MyHome_ctor(TalkView *view, CityScene *city, int object)` | 600 | no | vtable 0x1BD810, building a2, camera start from its entrance |
| `0x0017A960` | `void mm_TalkConv_Draw(TalkConv *conv, int pass)` | 164 | indirect | vtable 0x1BD860: message window, menu, name entry, card and money panel |
| `0x0017AA10` | `void mm_TalkConv_Closing(TalkConv *conv)` | 448 | yes | Step(0.1); then friendship status updates for the person or family member |
| `0x0017ABD0` | `void mm_TalkConv_Active(TalkConv *conv)` | 848 | yes | script code handling: choice menus, money panel, name entry, end of talk |
| `0x0017AF20` | `void mm_TalkConv_Opening(TalkConv *conv)` | 80 | yes | Step(0.1), then active and the first line |
| `0x0017AF70` | `int mm_TalkConv_Update(TalkConv *conv)` | 388 | yes | name entry results, menu choices back into the script, message window update |
| `0x0017B100` | `void mm_TalkConv_NextLine(TalkConv *conv)` | 456 | yes | next script line; the end code (-6007) applies the friendship gain |
| `0x0017B2D0` | `void mm_TalkConv_Start(TalkConv *conv, int byFamily, int target, int buildingOrMember, int slot, int arg5)` | 380 | yes | loads the script for a person (a1 0) or family member (a1 1), shows the card, sets the message |
| `0x0017B450` | `TalkConv *mm_TalkConv_dtor(TalkConv *conv, int16_t deleteFlag)` | 260 | yes | vtable 0x1BD860, deletes menu, name entry, card, money panel |
| `0x0017B560` | `TalkConv *mm_TalkConv_ctor(TalkConv *conv, int a1)` | 152 | yes | message window plus a menu window and the person card |
| `0x0017B600` | `void mm_WordsFn_CitySetFlag40(WordsCtx *ctx)` | 20 | no | #146 act: city flag 0x40 (pad focus returns after the talk) |
| `0x0017B620` | `void mm_WordsFn_TalkEndChoosing(WordsCtx *ctx)` | 8 | no | #145 act: talk view choice cleared, session single target |
| `0x0017B630` | `void mm_WordsFn_TalkSelectActor(WordsCtx *ctx)` | 40 | no | #144 act: talk view choice = actor arg of event +0xA4 |
| `0x0017B660` | `void mm_WordsFn_EventRemoveWant(WordsCtx *ctx)` | 96 | no | #143 act: removes the want the event holds for its building type |
| `0x0017B6C0` | `void mm_WordsFn_EventAddWant(WordsCtx *ctx)` | 96 | no | #142 act: the event holds a want for its building type |
| `0x0017B720` | `void mm_WordsFn_ActorMoveTo(WordsCtx *ctx)` | 140 | no | #141 act: moves the subjects to actor arg's place |
| `0x0017B7B0` | `void mm_WordsFn_ClearPersonFlag20(WordsCtx *ctx)` | 20 | no | #140 act: clears person A flag 0x20 |
| `0x0017B7D0` | `void mm_WordsFn_ActorFollow(WordsCtx *ctx)` | 156 | no | #139 act: person A leaves the building and follows actor arg |
| `0x0017B870` | `void mm_WordsFn_ActorDetach(WordsCtx *ctx)` | 108 | no | #138 act: detaches the subjects from the event |
| `0x0017B8E0` | `void mm_WordsFn_SelectEventActor(WordsCtx *ctx)` | 168 | no | #137 act: subjects B from actor arg of event +0xA4 |
| `0x0017B990` | `void mm_WordsFn_EndEventA4Result(WordsCtx *ctx)` | 16 | indirect | #136 act: ends event slot +0xA4 with its result code |
| `0x0017B9A0` | `void mm_WordsFn_StartEventA4(WordsCtx *ctx)` | 20 | indirect | #135 act: starts event slot +0xA4 (tiles, handler Start) |
| `0x0017B9C0` | `void mm_WordsFn_EndEventA4(WordsCtx *ctx)` | 16 | indirect | #134 act: ends event slot +0xA4 |
| `0x0017B9D0` | `void mm_WordsFn_SetEventA4Var(WordsCtx *ctx)` | 24 | no | #133 act: event slot +0xA4 variable = arg |
| `0x0017B9F0` | `void mm_WordsFn_EndEventA8Result(WordsCtx *ctx)` | 16 | indirect | #132 act: ends event slot +0xA8 with its result code |
| `0x0017BA00` | `void mm_WordsFn_EndEventA8(WordsCtx *ctx)` | 16 | indirect | #131 act: ends event slot +0xA8 (handler End, tiles, requeue by kind) |
| `0x0017BA10` | `void mm_WordsFn_SetEventA8Var(WordsCtx *ctx)` | 24 | no | #130 act: event slot +0xA8 variable = arg |
| `0x0017BA30` | `void mm_WordsFn_SelectEventPersonB(WordsCtx *ctx)` | 100 | no | #129 act: subject B = the event's second person |
| `0x0017BAA0` | `void mm_WordsFn_SelectEventPersonA(WordsCtx *ctx)` | 100 | no | #128 act: subject B = the event's first person |
| `0x0017BB10` | `void mm_WordsFn_EventQueueShift(WordsCtx *ctx)` | 12 | no | #127 act: (provisional) requeues the first event and shifts the list |
| `0x0017BB20` | `void mm_WordsFn_PlaySe(WordsCtx *ctx)` | 24 | no | #126 act: sound effect arg |
| `0x0017BB40` | `void mm_WordsFn_GiveBagItem(WordsCtx *ctx)` | 332 | no | #125 act: gives the selected bag item, friendship by its price |
| `0x0017BC90` | `void mm_WordsFn_BuyGoods(WordsCtx *ctx)` | 620 | yes | #124 act: pays for goods item arg, friendship by price, then laptop/bag/stamina/music/applicant by kind |
| `0x0017BF00` | `void mm_WordsFn_AddWant(WordsCtx *ctx)` | 68 | no | #123 act: records person A's want (building A's claim), sound 0x15 |
| `0x0017BF50` | `void mm_WordsFn_InviteFamily(WordsCtx *ctx)` | 92 | yes | #122 act: family A becomes an applicant when it can apply, sound 0x17 |
| `0x0017BFB0` | `void mm_WordsFn_NewIntroduction(WordsCtx *ctx)` | 64 | yes | #121 act: person A picks a new family to introduce (+0x2C) |
| `0x0017BFF0` | `void mm_WordsFn_InviteIntroduced(WordsCtx *ctx)` | 108 | yes | #120 act: the family in the result word becomes an applicant, member A's flag cleared, sound 0x17 |
| `0x0017C060` | `void mm_WordsFn_SetPose(WordsCtx *ctx)` | 24 | no | #119 act: talk session pose entry for member A = arg |
| `0x0017C080` | `void mm_WordsFn_MarkFamilyKnown(WordsCtx *ctx)` | 52 | no | #118 act: (provisional) family bit via 0x13C950, sound 0x14 |
| `0x0017C0C0` | `void mm_WordsFn_FriendshipDown(WordsCtx *ctx)` | 132 | yes | #117 act: friendship loss for member A (character type bonus + 3) |
| `0x0017C150` | `void mm_WordsFn_FriendshipUp(WordsCtx *ctx)` | 132 | yes | #116 act: friendship gain for member A (character type bonus + 5) |
| `0x0017C1E0` | `void mm_WordsFn_SelectMemberB(WordsCtx *ctx)` | 72 | no | #115 act: subject B = member +0x1A of family A |
| `0x0017C230` | `void mm_WordsFn_SelectBuildingB(WordsCtx *ctx)` | 432 | no | #114 act: building B = A's (0), home (1), under the person (2), current (3), destination (4), result (5) |
| `0x0017C3E0` | `void mm_WordsFn_SetCancelAllowed(WordsCtx *ctx)` | 12 | no | #113 act: context +0x130 = arg (cancel button allowed) |
| `0x0017C3F0` | `void mm_WordsFn_SetStoryFlagB(WordsCtx *ctx)` | 80 | no | #112 act: sets bit arg of 0x1C5530 |
| `0x0017C440` | `void mm_WordsFn_ClearStoryFlagB(WordsCtx *ctx)` | 84 | no | #111 act: clears bit arg of 0x1C5530 |
| `0x0017C4A0` | `void mm_WordsFn_SetVar1C5602(WordsCtx *ctx)` | 16 | no | #110 act: script variable 0x1C5602 = arg |
| `0x0017C4B0` | `void mm_WordsFn_EventA4Slot10(WordsCtx *ctx)` | 20 | indirect | #109 act: event slot +0xA4 handler Slot10 with arg |
| `0x0017C4D0` | `char *mm_WordsFn_TextEventA4Name(WordsCtx *ctx)` | 48 | no | #108 text: name of the event in slot +0xA4 |
| `0x0017C500` | `void mm_WordsFn_EventA8Slot10(WordsCtx *ctx)` | 20 | indirect | #107 act: event slot +0xA8 handler Slot10 with arg |
| `0x0017C520` | `char *mm_WordsFn_TextEventTitle(WordsCtx *ctx)` | 16 | no | #106 text: event title, with the member name when it has one |
| `0x0017C530` | `char *mm_WordsFn_TextOpenHours(WordsCtx *ctx)` | 72 | no | #105 text: opening hours of the family's building type |
| `0x0017C580` | `char *mm_WordsFn_TextBagItemName(WordsCtx *ctx)` | 68 | no | #104 text: name of the selected bag item |
| `0x0017C5D0` | `char *mm_WordsFn_TextGoodsName(WordsCtx *ctx)` | 104 | no | #103 text: name of the goods item arg |
| `0x0017C640` | `char *mm_WordsFn_TextWantClaim(WordsCtx *ctx)` | 160 | no | #102 text: claim line of the chosen want |
| `0x0017C6E0` | `char *mm_WordsFn_TextPopulation(WordsCtx *ctx)` | 52 | no | #101 text: (provisional) map value +0x29 with thousands separators |
| `0x0017C720` | `char *mm_WordsFn_TextClaimLine(WordsCtx *ctx)` | 60 | no | #100 text: claim line of building A/B |
| `0x0017C760` | `char *mm_WordsFn_TextFamilyRecord(WordsCtx *ctx)` | 60 | no | #99 text: (provisional) cached family record (FAMILY.BIN 0x1D4 bytes) |
| `0x0017C7A0` | `char *mm_WordsFn_TextOccupation(WordsCtx *ctx)` | 144 | yes | #98 text: (provisional) occupation from the home type and member role |
| `0x0017C830` | `char *mm_WordsFn_TextBuildingName(WordsCtx *ctx)` | 104 | no | #97 text: building type name (+0x110) |
| `0x0017C8A0` | `char *mm_WordsFn_TextMemberName2(WordsCtx *ctx)` | 120 | yes | #96 text: (provisional) member second name (+0x5C) |
| `0x0017C920` | `char *mm_WordsFn_TextMemberName(WordsCtx *ctx)` | 120 | yes | #95 text: member name (+0x4C) of A, B or the result family |
| `0x0017C9A0` | `char *mm_WordsFn_TextNameSlot(WordsCtx *ctx)` | 24 | no | #94 text: name slot arg |
| `0x0017C9C0` | `void mm_WordsFn_EventCallSlot14Off(WordsCtx *ctx)` | 120 | no | #93 act: event Slot14 for the subjects with arg, mode 0 |
| `0x0017CA40` | `int mm_WordsFn_EventSetActor(WordsCtx *ctx)` | 324 | no | #92 cond: sets actor arg of event +0xA4 to the subjects, marks the building |
| `0x0017CB90` | `int mm_WordsFn_EventActorIs(WordsCtx *ctx)` | 76 | no | #91 cond: actor arg of event +0xA4 is subject A |
| `0x0017CBE0` | `uint8_t mm_WordsFn_EventA4Var(WordsCtx *ctx)` | 20 | no | #90 cond: event slot +0xA4 variable (+10) |
| `0x0017CC00` | `int mm_WordsFn_EventA4IsTalk(WordsCtx *ctx)` | 80 | no | #89 cond: (provisional) event slot +0xA4 kind is 1 or 2 |
| `0x0017CC50` | `void mm_WordsFn_EventCallSlot14On(WordsCtx *ctx)` | 120 | no | #88 act: event Slot14 for the subjects with arg, mode 1 |
| `0x0017CCD0` | `uint8_t mm_WordsFn_EventA8Var(WordsCtx *ctx)` | 20 | no | #87 cond: event slot +0xA8 variable (+10) |
| `0x0017CCF0` | `int mm_WordsFn_EventA8Kind(WordsCtx *ctx)` | 96 | no | #86 cond: (provisional) kind of event slot +0xA8 plus 1 |
| `0x0017CD50` | `int mm_WordsFn_EventCountdown(WordsCtx *ctx)` | 36 | no | #85 cond: (provisional) 3 minus the event's counter |
| `0x0017CD80` | `uint32_t mm_WordsFn_EventStage(WordsCtx *ctx)` | 168 | no | #84 cond: (provisional) stage of the context's event |
| `0x0017CE30` | `void mm_WordsFn_StartEvent(WordsCtx *ctx)` | 60 | no | #83 act: starts an event from the script for person A |
| `0x0017CE70` | `int mm_WordsFn_EventsActive(WordsCtx *ctx)` | 36 | no | #82 cond: any event active |
| `0x0017CEA0` | `int mm_WordsFn_LikesBagItem(WordsCtx *ctx)` | 332 | yes | #81 cond: (provisional) the person likes the selected bag item |
| `0x0017CFF0` | `int mm_WordsFn_CanAfford(WordsCtx *ctx)` | 124 | no | #80 cond: player money covers the goods item arg |
| `0x0017D070` | `int mm_WordsFn_GoodsFitsBag(WordsCtx *ctx)` | 236 | no | #79 cond: (provisional) the goods item arg is a bag item and the bag has room |
| `0x0017D160` | `int mm_WordsFn_GoodsOwned(WordsCtx *ctx)` | 320 | no | #78 cond: (provisional) the goods item arg is already owned or unlocked |
| `0x0017D2A0` | `int mm_WordsFn_LaptopHasBit(WordsCtx *ctx)` | 64 | no | #77 cond: laptop entry bit arg is active |
| `0x0017D2E0` | `int mm_WordsFn_AnyAcquaintanceSuppliesWant(WordsCtx *ctx)` | 8 | no | #76 cond: the same over every member of family A; result word and member index (+0x1A) |
| `0x0017D2F0` | `int mm_WordsFn_AcquaintanceSuppliesWant(WordsCtx *ctx)` | 8 | no | #75 cond: an acquaintance of member A who can apply supplies the chosen want's claim; result word |
| `0x0017D300` | `int mm_WordsFn_FamilySuppliesWant(WordsCtx *ctx)` | 8 | no | #74 cond: family A can apply and its type supplies the chosen want's claim |
| `0x0017D310` | `int mm_WordsFn_SuppliesWantBody(WordsCtx *ctx, int a1)` | 896 | yes | shared body of #74/#75/#76 (entered past their prologues) |
| `0x0017D6A0` | `int mm_WordsFn_AcquaintanceSuppliesClaim(WordsCtx *ctx)` | 380 | yes | #73 cond: an acquaintance of member A who can apply and whose type supplies building A's claim; stored as the person's introduction |
| `0x0017D820` | `int mm_WordsFn_WantClaimMatch(WordsCtx *ctx)` | 240 | no | #72 cond: an open want with the same claim as building A, else a room check |
| `0x0017D910` | `int mm_WordsFn_HasWantForPerson(WordsCtx *ctx)` | 104 | no | #71 cond: person A has an open want |
| `0x0017D980` | `void mm_WordsFn_FindWant(WordsCtx *ctx)` | 52 | no | #70 act: first want lookup (result unused) |
| `0x0017D9C0` | `int mm_WordsFn_ApplicantCountTier(WordsCtx *ctx)` | 56 | no | #69 cond: applicants: none, up to 8, up to 31, full |
| `0x0017DA00` | `uint32_t mm_WordsFn_BuildingOpen(WordsCtx *ctx)` | 204 | indirect | #68 cond: building B (or type) open now |
| `0x0017DAD0` | `int mm_WordsFn_BuildingsOverlap(WordsCtx *ctx)` | 204 | no | #67 cond: (provisional) footprints of buildings A and B overlap |
| `0x0017DBA0` | `int mm_WordsFn_FamilyModArg(WordsCtx *ctx)` | 32 | no | #66 cond: family id mod arg |
| `0x0017DBC0` | `int mm_WordsFn_PickClaimResident(WordsCtx *ctx)` | 1116 | yes | #65 cond: (provisional) resident whose wants match the chosen want's claim |
| `0x0017E020` | `int mm_WordsFn_PickTestableResident(WordsCtx *ctx)` | 564 | yes | #64 cond: a resident of a candidate home who agrees (mm_Family_FriendAgrees: 3), else a random resident (1) |
| `0x0017E260` | `int mm_WordsFn_PickByCandidates(WordsCtx *ctx)` | 48 | yes | #63 cond: collects candidate buildings and picks (0x17E3A0) |
| `0x0017E290` | `int mm_WordsFn_PickRandomResident(WordsCtx *ctx)` | 264 | yes | #62 cond: collects candidate buildings, picks a random resident as B |
| `0x0017E3A0` | `int mm_Words_PickResidentByType(WordsCtx *ctx)` | 488 | no | random resident of a candidate home whose character type matches arg (type or list 0x1BE950) |
| `0x0017E590` | `void mm_Words_CollectNearbyHomes(WordsCtx *ctx, MapObject *building)` | 768 | yes | homes with known families around a building, radius by the member's range (table 0x1B97F0), up to 80 |
| `0x0017E890` | `int mm_WordsFn_PickOtherResident(WordsCtx *ctx)` | 248 | yes | #61 cond: picks another resident of building B as subject B |
| `0x0017E990` | `int mm_WordsFn_ScheduleIs(WordsCtx *ctx)` | 76 | yes | #60 cond: schedule activity equals arg |
| `0x0017E9E0` | `int mm_WordsFn_PersonFlagBit(WordsCtx *ctx)` | 32 | no | #59 cond: person +0x22 bit arg |
| `0x0017EA00` | `uint32_t mm_WordsFn_ScheduleState(WordsCtx *ctx)` | 344 | yes | #58 cond: the person's schedule state, folded by arg (0..5) |
| `0x0017EB60` | `int mm_WordsFn_HomeIsType104(WordsCtx *ctx)` | 92 | no | #57 cond: (provisional) the family's home type is 0x104 or 0x105 |
| `0x0017EBC0` | `int mm_WordsFn_Arg4(WordsCtx *ctx)` | 8 | no | #56 cond: context +4 |
| `0x0017EBD0` | `uint8_t mm_WordsFn_MemberByte2B(WordsCtx *ctx)` | 68 | yes | #55 cond: (provisional) member byte +0x2B |
| `0x0017EC20` | `uint8_t mm_WordsFn_MemberByte2A(WordsCtx *ctx)` | 68 | yes | #54 cond: (provisional) member byte +0x2A |
| `0x0017EC70` | `uint16_t mm_WordsFn_MemberCharType(WordsCtx *ctx)` | 104 | yes | #53 cond: member character type, folded (0x26 -> 13, others above 0x1D -> 0) |
| `0x0017ECE0` | `int mm_WordsFn_IsSpecialFamily(WordsCtx *ctx)` | 120 | no | #52 cond: (provisional) family equals one of three ids in a gp table (arg selects) |
| `0x0017ED60` | `int mm_WordsFn_IsApplicant(WordsCtx *ctx)` | 16 | no | #51 cond: the family is on the applicant list |
| `0x0017ED70` | `int mm_WordsFn_FamilyHasLeft(WordsCtx *ctx)` | 8 | no | #50 cond: family A moved away (bitset C) |
| `0x0017ED80` | `int mm_WordsFn_FamilyInCity(WordsCtx *ctx)` | 8 | no | #49 cond: family A lives in the city (bitset A) |
| `0x0017ED90` | `uint8_t mm_WordsFn_CanApply(WordsCtx *ctx)` | 116 | no | #48 cond: city flag 4, or flag 2 of 0x1C5600 with family A able to apply |
| `0x0017EE10` | `int mm_WordsFn_ScoreA8Tier(WordsCtx *ctx)` | 8 | no | #47 cond: placement score +0xA8 below 200, below 300, or higher |
| `0x0017EE20` | `int mm_WordsFn_ScoreA0Tier(WordsCtx *ctx)` | 8 | no | #46 cond: placement score +0xA0 below 200, below 300, or higher |
| `0x0017EE30` | `int mm_WordsFn_ApprovalTier(WordsCtx *ctx)` | 60 | no | #45 cond: approval tier folded to 0..2 |
| `0x0017EE70` | `int mm_WordsFn_MapHistoryTier(WordsCtx *ctx)` | 76 | no | #44 cond: map history sum: none, up to 10, more |
| `0x0017EEC0` | `int mm_WordsFn_FamilyCountTier(void)` | 84 | no | #43 cond: families in the city (0x1F32E0) in five bands (20/50/100/200) |
| `0x0017EF20` | `int mm_WordsFn_PopulationRank(WordsCtx *ctx)` | 32 | no | #42 cond: population rank (1..8) minus 1 |
| `0x0017EF40` | `int mm_WordsFn_TimeOfDay(WordsCtx *ctx)` | 80 | no | #41 cond: 0 from 3:00 to 12:00, 1 to 18:00, else 2 |
| `0x0017EF90` | `int mm_WordsFn_BeforeSix(WordsCtx *ctx)` | 16 | no | #40 cond: clock before 6:00 |
| `0x0017EFA0` | `int mm_WordsFn_WeatherTomorrow(WordsCtx *ctx)` | 12 | no | #39 cond: tomorrow's weather (city +0x94) |
| `0x0017EFB0` | `int mm_WordsFn_WeatherToday(WordsCtx *ctx)` | 12 | no | #38 cond: today's weather (city +0x90) |
| `0x0017EFC0` | `int mm_WordsFn_CityFlag2(WordsCtx *ctx)` | 24 | no | #37 cond: city flag 2 |
| `0x0017EFE0` | `int mm_WordsFn_Month(WordsCtx *ctx)` | 36 | no | #36 cond: month (city +0x88 mod 12) |
| `0x0017F010` | `int mm_WordsFn_StaminaZero(WordsCtx *ctx)` | 12 | no | #35 cond: tail call mm_Stamina_Zero(city->stamina), always 0 (was mm_WordsFn_Nop35) |
| `0x0017F020` | `int mm_WordsFn_StaminaTier(WordsCtx *ctx)` | 12 | no | #34 cond: player stamina below 100000, below 1800001, or higher |
| `0x0017F030` | `uint32_t mm_WordsFn_StoryFlagB(WordsCtx *ctx)` | 76 | no | #33 cond: bit arg of 0x1C5530 (set/cleared by #112/#111) |
| `0x0017F080` | `uint32_t mm_WordsFn_StoryFlagA(WordsCtx *ctx)` | 76 | no | #32 cond: bit arg of 0x1C5520 |
| `0x0017F0D0` | `uint16_t mm_WordsFn_GetVar1C5602(void)` | 12 | no | #31 cond: (provisional) script variable 0x1C5602 (set by #110) |
| `0x0017F0E0` | `int mm_WordsFn_Flag134(WordsCtx *ctx)` | 8 | no | #30 cond: context +0x134 |
| `0x0017F0F0` | `int mm_WordsFn_RandomN(WordsCtx *ctx)` | 60 | no | #29 cond: random mod arg |
| `0x0017F130` | `int mm_WordsFn_Random3(void)` | 92 | no | #28 cond: random 0..2 |
| `0x0017F190` | `int mm_WordsFn_Random50(void)` | 72 | no | #27 cond: random, about half |
| `0x0017F1E0` | `int mm_WordsFn_False(void)` | 8 | no | #26 cond: 0 |
| `0x0017F1F0` | `int mm_WordsFn_BuildingCrowd(WordsCtx *ctx)` | 260 | no | #25 cond: visitors at the building: none, some, over half capacity |
| `0x0017F300` | `int mm_WordsFn_BuildingFlag100(WordsCtx *ctx)` | 48 | no | #24 cond: building flag 0x100 |
| `0x0017F330` | `void mm_WordsFn_TryFriendAgrees(WordsCtx *ctx)` | 164 | no | #23 act: mm_Family_FriendAgrees for member A when applicants and people have room |
| `0x0017F3E0` | `int mm_WordsFn_MeterAgrees(WordsCtx *ctx)` | 52 | no | #22 cond: mm_Family_MeterAgrees for member B when arg (+0x2A) != 0, else member A, with the city's approval (was mm_WordsFn_Nop22: it only sets v0, so a void prototype hid it) |
| `0x0017F420` | `int mm_WordsFn_FindOutsideAcquaintance(WordsCtx *ctx)` | 260 | yes | #21 cond: an acquaintance of member A from an Outside-category type who can apply; result word |
| `0x0017F530` | `int mm_WordsFn_GetIntroduction(WordsCtx *ctx)` | 136 | yes | #20 cond: the family subject A/B introduces (picks one when the person has none); nonzero when there is one |
| `0x0017F5C0` | `int mm_WordsFn_BuildingFlag2(WordsCtx *ctx)` | 48 | no | #19 cond: building flag 2 |
| `0x0017F5F0` | `int mm_WordsFn_ClaimIs50(WordsCtx *ctx)` | 108 | no | #18 cond: claim entry value 0x32 |
| `0x0017F660` | `uint8_t mm_WordsFn_BuildingClaim(WordsCtx *ctx)` | 100 | no | #17 cond: the building's claim entry (CLAIM.BIN section B) |
| `0x0017F6D0` | `int mm_WordsFn_BuildingState(WordsCtx *ctx)` | 128 | no | #16 cond: building flags 8 (claim met, 3) / 0x40 (meter maxed, 4), else its claim meter level |
| `0x0017F750` | `int mm_WordsFn_FriendshipTier(WordsCtx *ctx)` | 100 | no | #15 cond: friendship value below 48, below 100, or higher |
| `0x0017F7C0` | `int mm_WordsFn_FriendshipBand(WordsCtx *ctx)` | 44 | no | #14 cond: band of member A/B's friendship meter |
| `0x0017F7F0` | `int mm_WordsFn_FamilyKnown(WordsCtx *ctx)` | 36 | no | #13 cond: family A/B is known (bitset B) |
| `0x0017F820` | `int mm_WordsFn_PersonFlag10(WordsCtx *ctx)` | 48 | no | #12 cond: person +0x22 flag 0x10 |
| `0x0017F850` | `int mm_WordsFn_IsBirthdayMonth(WordsCtx *ctx)` | 124 | no | #11 cond: (family + member) mod 12 equals the month |
| `0x0017F8D0` | `int mm_WordsFn_WorksAtHome(WordsCtx *ctx)` | 152 | no | #10 cond: (provisional) home equals workplace and the type has flag 0x20 |
| `0x0017F970` | `int mm_WordsFn_MemberRole(WordsCtx *ctx)` | 184 | yes | #9 cond: (provisional) member +0x28 role and +0x27 bit 0, six cases |
| `0x0017FA30` | `int mm_WordsFn_HomeTypeFlag100(WordsCtx *ctx)` | 88 | no | #8 cond: the family's home type has flag 0x100 |
| `0x0017FA90` | `uint8_t mm_WordsFn_BuildingCategory(WordsCtx *ctx)` | 100 | yes | #7 cond: category (+0x145) of the subject's building type |
| `0x0017FB00` | `int mm_WordsFn_PersonHeading(WordsCtx *ctx)` | 180 | no | #6 cond: person's destination: 0 work, 1 home, 2 other building, 3 none |
| `0x0017FBC0` | `int mm_WordsFn_PersonWhere(WordsCtx *ctx)` | 180 | no | #5 cond: person's current building: 0 home, 1 work, 2 other building, 3 outside |
| `0x0017FC80` | `int mm_WordsFn_PersonAtBuilding(WordsCtx *ctx)` | 564 | indirect | #4 cond: (provisional) where the person stands relative to the building and its hours |
| `0x0017FEC0` | `uint32_t mm_WordsFn_StatusMid2(WordsCtx *ctx)` | 96 | no | #3 cond: middle two bits of the status byte |
| `0x0017FF20` | `uint8_t mm_WordsFn_StatusLow2(WordsCtx *ctx)` | 96 | no | #2 cond: low two bits of the status byte |
| `0x0017FF80` | `int mm_WordsFn_FriendLevelHigh(WordsCtx *ctx)` | 148 | no | #1 cond: friendship level above 13 for A/B (2 when not acquainted) |
| `0x00180020` | `int mm_WordsFn_NameSlotSet(WordsCtx *ctx)` | 48 | no | #0 cond: name slot (0x1C5380 + arg*16) is not empty |
| `0x00180050` | `int mm_Words_CallCond(Words *words)` | 32 | no | calls a condition through its member function pointer (__ptmf_scall) |
| `0x00180070` | `int mm_Words_CallText(Words *words)` | 32 | no | calls a text getter through its member function pointer |
| `0x00180090` | `int mm_Words_CallAct(Words *words)` | 32 | no | calls an action through its member function pointer |
| `0x001800B0` | `void mm_Words_SetFlag134(Words *words)` | 16 | no | context +0x134 = 1 |
| `0x001800C0` | `void mm_Words_SelectBagItem(Words *words, int index)` | 192 | no | context +0x24 = the a1-th bag item of kinds 0x10/0x11/0x13 |
| `0x00180180` | `void mm_Words_SetWantIndex(Words *words, uint16_t index)` | 12 | no | context +0x13A = a1 (the chosen want) |
| `0x00180190` | `int mm_Words_SelectEvent(Words *words, int choice)` | 92 | yes | (provisional) context +0x138 = a1 - 1, event lookup for the family |
| `0x001801F0` | `int mm_Words_GetGoods(Words *words, int index)` | 88 | no | goods record a1 for building type B this season |
| `0x00180250` | `uint16_t mm_Words_GetBuildingTypeB(Words *words)` | 12 | no | context +0x14 |
| `0x00180260` | `void mm_Words_SetArg(Words *words, uint16_t arg)` | 12 | no | context +0x2A = a1 |
| `0x00180270` | `int mm_Words_GetFriendshipChanged(Words *words)` | 12 | no | context +0x13C (set by #116/#117) |
| `0x00180280` | `int mm_Words_GetCancelAllowed(Words *words)` | 12 | no | context +0x130 |
| `0x00180290` | `void mm_Words_SetFamily(Words *words, int family, uint16_t member, int arg4, CityScene *city)` | 248 | yes | subject A = family a1 member a2 (prefetches the family), clears the rest |
| `0x00180390` | `void mm_Words_SetPerson(Words *words, MapPerson *person, int arg4, CityScene *city)` | 256 | yes | subject A = person a1 with home building, clears the rest |
| `0x00180490` | `Words *mm_Words_dtor(Words *words, int16_t deleteFlag)` | 84 | yes | frees the 0x140-byte context |
| `0x001804F0` | `Words *mm_Words_ctor(Words *words)` | 48 | yes | allocates the 0x140-byte context |
| `0x001A0C40` | `int mm_Words_MatchTypedAnswer(int script, char *typed)` | 840 | no | which answer of the current question matches the typed text (strcmp) |
| `0x001A0F90` | `char *mm_Words_GetEntryDefault(void)` | 580 | no | default text for a name entry question |
| `0x001A11E0` | `uint16_t mm_Words_GetEntryMode(void)` | 504 | no | (provisional) first field of the name entry question (passed to mm_NameEntry_Setup) |
| `0x001A13E0` | `char *mm_Words_GetChoiceText(int script, CityScene *city, int code, int index)` | 1036 | yes | text of menu choice a3 for code a2: goods with prices ("Don't buy"), claims, wants, events |
| `0x001A17F0` | `void mm_Words_ApplyChoice(int script, CityScene *city, int code, int choice)` | 504 | yes | stores menu choice a3 for code a2 (bag item, want, event, argument) and jumps |
| `0x001A19F0` | `int mm_Words_CountChoices(int script, CityScene *city, int code)` | 664 | no | number of menu choices for code a2 |
| `0x001A1CA0` | `void mm_Words_AppendClipped(char *buffer, char *text)` | 312 | no | (provisional) appends a string to the message buffer within the line limit |
| `0x001A1DE0` | `uint32_t mm_Words_ExpandLine(int a0, int a1, int a2)` | 2044 | no | expands a script line: conditions pick branches, text getters fill in names, actions run |
| `0x001A2620` | `int16_t mm_Words_StepImpl(int32_t *script, char *buffer)` | 888 | yes | binds the loaded tables on first use, then runs codes until the next message or end code |
| `0x001A29B0` | `int16_t mm_Words_Step(int32_t *script, char *buffer)` | 8 | no | next message of the running script (to mm_Words_StepImpl) |
| `0x001A29C0` | `int mm_Words_FriendshipChanged(void)` | 8 | no | global context +0x13C |
| `0x001A29D0` | `int mm_Words_CancelAllowed(void)` | 8 | no | global context +0x130 |
| `0x001A29E0` | `int mm_Words_LoadForWants(int a0, uint16_t a1, int a2, int a3)` | 596 | yes | script for a wants window line (event or default), reads its block |
| `0x001A2C40` | `int mm_Words_LoadForFamily(int family, int member, int arg5, CityScene *city)` | 424 | yes | script for family a1 member a2 (talk by phone), reads its block |
| `0x001A2DF0` | `int mm_Words_LoadForPerson(MapPerson *person, int arg5, CityScene *city)` | 604 | yes | script for person a1 (may start an event), reads its block |
| `0x001A3050` | `uint32_t *mm_Words_ReadBlock(int a0)` | 336 | yes | reads a script block from the open WORDS file into the buffer |
| `0x001A31A0` | `int mm_Words_OpenFiles(int a0)` | 172 | yes | opens DATA\BIN\WORDS.BIN and WORDSEVE.BIN for streaming |
| `0x001A3250` | `void mm_Words_Init(void)` | 744 | yes | loads the WORDS, WORDSEVE, WORDSCOM and WORDSTEN tables at game start |
| `0x001BAEC0` | `void mm_StaticInit_WordsTable(void)` | 7884 | no | copies the WORDS member function table template (0x1B1B48) to 0x1B9100 |
| `0x001BCD90` | `void mm_StaticInit_Words(void)` | 52 | yes | constructs the WORDS object (0x1BDDF0) and registers its destructor |
