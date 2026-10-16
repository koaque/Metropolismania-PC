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

320 functions (named 320). Blocks: 86 can reach a thread switch, 27 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Name | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001735D0` | `mm_TalkView_CollectOccupants` | 492 | no | (provisional) people at the building sorted by grade (+0x2E), up to 6 |
| `0x001737C0` | `mm_TalkView_ToggleAnim` | 88 | no | (provisional) flips the selected person's animation (7 -> 2/3) |
| `0x00173820` | `mm_TalkView_DrawPeople` | 3232 | yes | people at the building or of the family in a row with names, grades and the cursor |
| `0x001744E0` | `mm_TalkView_NobodyHome` | 296 | yes | (provisional) 1 when no one of the family or building is there (schedule, outside flags) |
| `0x00174610` | `mm_TalkView_DrawBuilding` | 916 | yes | the target building (a1 0: object, 1: family's home), lit when someone is home |
| `0x001749B0` | `mm_TalkView_BuildingCamera` | 904 | no | camera eye and target in front of the building's entrance |
| `0x00174D40` | `mm_TalkView_DrawCursor` | 64 | no | cursor frame at +0xC0 |
| `0x00174D80` | `mm_TalkView_CursorPerson` | 184 | no | person index of the cursor slot (+0xBC), 0x3F80 when none |
| `0x00174E40` | `mm_TalkView_HandleInput` | 640 | no | cursor left/right with scrolling and zoom steps (pad repeat) |
| `0x001750C0` | `mm_TalkView_GetChoice` | 176 | no | the chosen person (a1) and slot (a2) once a choice is made |
| `0x00175170` | `mm_TalkView_FindPersonSlot` | 260 | no | slot of the person with family a1 and member a2 |
| `0x00175280` | `mm_TalkView_CountPeople` | 200 | yes | people present (+0xC8), or family members when by family |
| `0x00175350` | `mm_TalkView_SetTarget` | 288 | yes | building a1 / family a2, name text boxes and frames |
| `0x00175470` | `mm_TalkView_PersonCamera` | 620 | no | (provisional) camera eye and target facing a person at a building |
| `0x001756E0` | `mm_TalkView_DrawMember` | 448 | yes | (provisional) family member model in front of the camera |
| `0x001758A0` | `mm_TalkView_DrawPerson` | 436 | indirect | (provisional) person model in front of the camera |
| `0x00175A60` | `mm_TalkView_DrawPersonTurned` | 268 | indirect | (provisional) person model rotated by a0 |
| `0x00175B70` | `mm_TalkView_dtor` | 212 | yes | vtable 0x1BD450, text boxes and frames |
| `0x00175C50` | `mm_TalkView_ctor` | 408 | no | vtable 0x1BD450, camera copy, models 0x1F3B2C/0x1F3B7C, packets |
| `0x00175DF0` | `mm_TalkPhoneFamily_Draw` | 92 | yes | (provisional) vtable 0x1BD4A0, the whole family (member 6) from the Town's Pages |
| `0x00175E50` | `mm_TalkPhoneFamily_Closed` | 64 | no | waits for the fade, then idle |
| `0x00175E90` | `mm_TalkPhoneFamily_Closing` | 68 | no | waits for the fade, fades again |
| `0x00175EE0` | `mm_TalkPhoneFamily_Close` | 52 | no | starts the fade and closes |
| `0x00175F20` | `mm_TalkPhoneFamily_Active` | 8 | no |  |
| `0x00175F30` | `mm_TalkPhoneFamily_Opened` | 52 | no |  |
| `0x00175F70` | `mm_TalkPhoneFamily_Opening` | 68 | no | waits for the fade, fades again |
| `0x00175FC0` | `mm_TalkPhoneFamily_Open` | 52 | no | starts the fade and opens |
| `0x00176000` | `mm_TalkPhoneFamily_dtor` | 96 | yes | vtable 0x1BD4A0 |
| `0x00176060` | `mm_TalkPhoneFamily_ctor` | 88 | yes | vtable 0x1BD4A0, family a2 |
| `0x001760C0` | `mm_TalkPhoneMember_Draw` | 92 | yes | (provisional) vtable 0x1BD4F0, one family member from the Town's Pages |
| `0x00176120` | `mm_TalkPhoneMember_Closed` | 64 | no | waits for the fade, then idle |
| `0x00176160` | `mm_TalkPhoneMember_Closing` | 68 | no | waits for the fade, fades again |
| `0x001761B0` | `mm_TalkPhoneMember_Close` | 52 | no | starts the fade and closes |
| `0x001761F0` | `mm_TalkPhoneMember_Active` | 8 | no |  |
| `0x00176200` | `mm_TalkPhoneMember_Opened` | 52 | no | waits for the fade |
| `0x00176240` | `mm_TalkPhoneMember_Opening` | 68 | no | waits for the fade, fades again |
| `0x00176290` | `mm_TalkPhoneMember_Open` | 52 | no | starts the fade and opens |
| `0x001762D0` | `mm_TalkPhoneMember_dtor` | 96 | yes | vtable 0x1BD4F0 |
| `0x00176330` | `mm_TalkPhoneMember_ctor` | 84 | no | vtable 0x1BD4F0, family a2, member a3 |
| `0x00176390` | `mm_TalkBldgFar_Draw` | 92 | yes | vtable 0x1BD540: building with its occupants, camera cut |
| `0x001763F0` | `mm_TalkBldgFar_Closed` | 96 | no | restores the camera, waits for the fade |
| `0x00176450` | `mm_TalkBldgFar_Closing` | 120 | no | building camera until the fade ends |
| `0x001764D0` | `mm_TalkBldgFar_Close` | 52 | no | starts the fade and closes |
| `0x00176510` | `mm_TalkBldgFar_Active` | 88 | no | building camera and cursor input |
| `0x00176570` | `mm_TalkBldgFar_Opened` | 104 | no | building camera, waits for the fade |
| `0x001765E0` | `mm_TalkBldgFar_Opening` | 100 | no | waits for the fade, fades again |
| `0x00176650` | `mm_TalkBldgFar_Open` | 52 | no | starts the fade and opens |
| `0x00176690` | `mm_TalkBldgFar_dtor` | 96 | yes | vtable 0x1BD540 |
| `0x001766F0` | `mm_TalkBldgFar_ctor` | 128 | yes | vtable 0x1BD540, building a2, collects its occupants |
| `0x00176770` | `mm_TalkBldgNear_Draw` | 84 | yes | vtable 0x1BD590: building with its occupants, camera pans |
| `0x001767D0` | `mm_TalkBldgNear_Closing` | 276 | indirect | camera pans back |
| `0x001768F0` | `mm_TalkBldgNear_Active` | 96 | no | building camera and cursor input |
| `0x00176950` | `mm_TalkBldgNear_Opening` | 256 | indirect | camera pans to the building |
| `0x00176A50` | `mm_TalkBldgNear_dtor` | 96 | yes | vtable 0x1BD590 |
| `0x00176AB0` | `mm_TalkBldgNear_ctor` | 116 | yes | vtable 0x1BD590, building a2, collects its occupants |
| `0x00176B30` | `mm_TalkOccupantFar_Draw` | 88 | yes | vtable 0x1BD5E0: one person at a building, camera cut |
| `0x00176B90` | `mm_TalkOccupantFar_Closed` | 96 | no | restores the camera, waits for the fade |
| `0x00176BF0` | `mm_TalkOccupantFar_Closing` | 120 | no | building camera until the fade ends |
| `0x00176C70` | `mm_TalkOccupantFar_Close` | 52 | no | starts the fade and closes |
| `0x00176CB0` | `mm_TalkOccupantFar_Active` | 80 | no | building camera |
| `0x00176D00` | `mm_TalkOccupantFar_Opened` | 104 | no | building camera, waits for the fade |
| `0x00176D70` | `mm_TalkOccupantFar_Opening` | 100 | no | waits for the fade, fades again |
| `0x00176DE0` | `mm_TalkOccupantFar_Open` | 52 | no | starts the fade and opens |
| `0x00176E20` | `mm_TalkOccupantFar_dtor` | 96 | yes | vtable 0x1BD5E0 |
| `0x00176E80` | `mm_TalkOccupantFar_ctor` | 116 | no | vtable 0x1BD5E0, building a2, person a3 |
| `0x00176F00` | `mm_TalkOccupantNear_Draw` | 80 | yes | vtable 0x1BD630: one person at a building, camera pans |
| `0x00176F50` | `mm_TalkOccupantNear_Closing` | 276 | indirect | camera pans back |
| `0x00177070` | `mm_TalkOccupantNear_Active` | 88 | no | building camera |
| `0x001770D0` | `mm_TalkOccupantNear_Opening` | 256 | indirect | camera pans to the person |
| `0x001771D0` | `mm_TalkOccupantNear_dtor` | 96 | yes | vtable 0x1BD630 |
| `0x00177230` | `mm_TalkOccupantNear_ctor` | 112 | no | vtable 0x1BD630, building a2, person a3 |
| `0x001772A0` | `mm_TalkPersonFar_Draw` | 64 | no | vtable 0x1BD680: a person outside, camera cut |
| `0x001772E0` | `mm_TalkPersonFar_Closed` | 64 | no | waits for the fade, then idle |
| `0x00177320` | `mm_TalkPersonFar_Closing` | 100 | no | camera until the fade ends |
| `0x00177390` | `mm_TalkPersonFar_Close` | 52 | no | starts the fade and closes |
| `0x001773D0` | `mm_TalkPersonFar_Active` | 8 | no |  |
| `0x001773E0` | `mm_TalkPersonFar_Opened` | 52 | no | waits for the fade |
| `0x00177420` | `mm_TalkPersonFar_Opening` | 100 | no | camera, waits for the fade, fades again |
| `0x00177490` | `mm_TalkPersonFar_Open` | 52 | no | starts the fade and opens |
| `0x001774D0` | `mm_TalkPersonFar_dtor` | 96 | yes | vtable 0x1BD680 |
| `0x00177530` | `mm_TalkPersonFar_ctor` | 548 | no | vtable 0x1BD680, person a2: camera facing them from the terrain heights |
| `0x00177760` | `mm_TalkPersonNear_Draw` | 40 | no | vtable 0x1BD6D0: a person outside, camera pans |
| `0x00177790` | `mm_TalkPersonNear_Closing` | 276 | indirect | camera pans back |
| `0x001778B0` | `mm_TalkPersonNear_Active` | 72 | no | camera held on the person |
| `0x00177900` | `mm_TalkPersonNear_Opening` | 268 | indirect | camera pans to the person |
| `0x00177A10` | `mm_TalkPersonNear_dtor` | 96 | yes | vtable 0x1BD6D0 |
| `0x00177A70` | `mm_TalkPersonNear_ctor` | 548 | no | vtable 0x1BD6D0, person a2: camera facing them from the terrain heights |
| `0x00177CA0` | `mm_TalkCard_FillText` | 648 | yes | name, age and how recently met ("Met today", "Been awhile", "Lives in next town") |
| `0x00177F30` | `mm_TalkCard_DrawGauges` | 788 | no | (provisional) grade and friendship gauges of the person |
| `0x00178250` | `mm_TalkCard_Draw` | 256 | yes | vtable 0x1BD720: card model, texts and gauges while active |
| `0x00178350` | `mm_TalkCard_Show` | 480 | no | opens for person a1 (or family a2 member a3) at slot a4, friendship (+0x2C) and mood (+0x34) |
| `0x00178530` | `mm_TalkCard_dtor` | 172 | yes | vtable 0x1BD720 |
| `0x001785E0` | `mm_TalkCard_ctor` | 220 | yes | vtable 0x1BD720, two text boxes and a frame |
| `0x001786C0` | `mm_Talk_SetViewLit` | 24 | no | (provisional) view +0x258 = 1 (building drawn as lit), used by the caveman event |
| `0x001786E0` | `mm_Talk_LookupPose` | 80 | no | (provisional) value for family a1 member a2 in the 32-entry table at +0x40 |
| `0x001787D0` | `mm_Talk_SelectPerson` | 44 | no | forces the view's choice (+0xB8) to the slot of family a1 member a2 |
| `0x00178800` | `mm_Talk_ChooseInput` | 348 | yes | choosing a person: confirm (0x40) starts the conversation, cancel (0x10) closes |
| `0x00178960` | `mm_Talk_Closed` | 240 | indirect | state 5: when the view is idle, pops the scene mode, deletes view and conversation, restores HUD |
| `0x00178A50` | `mm_Talk_Closing` | 120 | indirect | state 4: when the conversation is idle, closes the view or goes back to choosing |
| `0x00178AD0` | `mm_Talk_Active` | 104 | indirect | state 3: person animation while talking, closes when the conversation does |
| `0x00178B40` | `mm_Talk_Opened` | 104 | indirect | state 2: waits for the conversation to open |
| `0x00178BB0` | `mm_Talk_Opening` | 168 | yes | state 1: when the view is active, starts the conversation or choosing |
| `0x00178C60` | `mm_Talk_Update` | 228 | yes | updates view and conversation, then the state machine |
| `0x00178D50` | `mm_Talk_Start` | 1332 | yes | target building a2/person a3 or family a2/member a3; picks the view class (near/far, home, person) and the conversation |
| `0x00179290` | `mm_Talk_Draw` | 96 | indirect | draws view and conversation |
| `0x001792F0` | `mm_Talk_dtor` | 180 | yes | vtable 0x1BD770 |
| `0x001793B0` | `mm_Talk_ctor` | 68 | no | vtable 0x1BD770 |
| `0x00179400` | `mm_TalkMoney_Draw` | 156 | no | vtable 0x1BD7C0: panel model and the money text |
| `0x001794A0` | `mm_TalkMoney_Closing` | 68 | indirect | Step(0.1), then idle |
| `0x001794F0` | `mm_TalkMoney_Active` | 100 | no |  |
| `0x00179560` | `mm_TalkMoney_Opening` | 68 | indirect | Step(0.1), then active |
| `0x001795B0` | `mm_TalkMoney_dtor` | 124 | yes | vtable 0x1BD7C0 |
| `0x00179630` | `mm_TalkMoney_ctor` | 188 | yes | vtable 0x1BD7C0, amount a1 (player money 0x1C5540) |
| `0x001796F0` | `mm_MyHome_EntranceRect` | 360 | no | tile rectangle in front of a building by facing (0-3), clamped to the map |
| `0x00179860` | `mm_MyHome_MarkEntrance` | 300 | no | sets (a1 1) or clears tile flag 0x80 in front of the home |
| `0x00179990` | `mm_MyHome_FloorHit` | 492 | no | (provisional) ray against the interior floor triangles (collision 0x1F3CE0) |
| `0x00179B80` | `mm_MyHome_WallHit` | 488 | no | (provisional) ray against the interior wall triangles |
| `0x00179D70` | `mm_MyHome_Collide` | 412 | no | (provisional) pushes the viewer out of the interior walls |
| `0x00179F10` | `mm_MyHome_Draw` | 232 | no | vtable 0x1BD810: the home interior model while active |
| `0x0017A000` | `mm_MyHome_Closing` | 284 | indirect | camera moves back out |
| `0x0017A120` | `mm_MyHome_Active` | 944 | no | (provisional) walking around inside with the pad, collision and camera |
| `0x0017A4D0` | `mm_MyHome_Opening` | 312 | indirect | camera moves into the home |
| `0x0017A610` | `mm_MyHome_Close` | 136 | no | refills stamina (mm_Stamina_Init) when below the maximum, clears the entrance, closes |
| `0x0017A6A0` | `mm_MyHome_dtor` | 96 | yes | vtable 0x1BD810 |
| `0x0017A700` | `mm_MyHome_ctor` | 600 | no | vtable 0x1BD810, building a2, camera start from its entrance |
| `0x0017A960` | `mm_TalkConv_Draw` | 164 | indirect | vtable 0x1BD860: message window, menu, name entry, card and money panel |
| `0x0017AA10` | `mm_TalkConv_Closing` | 448 | yes | Step(0.1); then friendship status updates for the person or family member |
| `0x0017ABD0` | `mm_TalkConv_Active` | 848 | yes | script code handling: choice menus, money panel, name entry, end of talk |
| `0x0017AF20` | `mm_TalkConv_Opening` | 80 | yes | Step(0.1), then active and the first line |
| `0x0017AF70` | `mm_TalkConv_Update` | 388 | yes | name entry results, menu choices back into the script, message window update |
| `0x0017B100` | `mm_TalkConv_NextLine` | 456 | yes | next script line; the end code (-6007) applies the friendship gain |
| `0x0017B2D0` | `mm_TalkConv_Start` | 380 | yes | loads the script for a person (a1 0) or family member (a1 1), shows the card, sets the message |
| `0x0017B450` | `mm_TalkConv_dtor` | 260 | yes | vtable 0x1BD860, deletes menu, name entry, card, money panel |
| `0x0017B560` | `mm_TalkConv_ctor` | 152 | yes | message window plus a menu window and the person card |
| `0x0017B600` | `mm_WordsFn_CitySetFlag40` | 20 | no | #146 act: city flag 0x40 (pad focus returns after the talk) |
| `0x0017B620` | `mm_WordsFn_TalkEndChoosing` | 40 | no | #145 act: talk view choice cleared, session single target |
| `0x0017B630` | `mm_WordsFn_TalkSelectActor` | 40 | no | #144 act: talk view choice = actor arg of event +0xA4 |
| `0x0017B660` | `mm_WordsFn_EventRemoveVacancy` | 96 | no | #143 act: removes the vacancy for the event's building type |
| `0x0017B6C0` | `mm_WordsFn_EventAddVacancy` | 96 | no | #142 act: adds a vacancy for the event's building type |
| `0x0017B720` | `mm_WordsFn_ActorMoveTo` | 140 | no | #141 act: moves the subjects to actor arg's place |
| `0x0017B7B0` | `mm_WordsFn_ClearPersonFlag20` | 20 | no | #140 act: clears person A flag 0x20 |
| `0x0017B7D0` | `mm_WordsFn_ActorFollow` | 156 | no | #139 act: person A leaves the building and follows actor arg |
| `0x0017B870` | `mm_WordsFn_ActorDetach` | 108 | no | #138 act: detaches the subjects from the event |
| `0x0017B8E0` | `mm_WordsFn_SelectEventActor` | 168 | no | #137 act: subjects B from actor arg of event +0xA4 |
| `0x0017B990` | `mm_WordsFn_EndEventA4Result` | 16 | indirect | #136 act: ends event slot +0xA4 with its result code |
| `0x0017B9A0` | `mm_WordsFn_StartEventA4` | 20 | indirect | #135 act: starts event slot +0xA4 (tiles, handler Start) |
| `0x0017B9C0` | `mm_WordsFn_EndEventA4` | 16 | indirect | #134 act: ends event slot +0xA4 |
| `0x0017B9D0` | `mm_WordsFn_SetEventA4Var` | 24 | no | #133 act: event slot +0xA4 variable = arg |
| `0x0017B9F0` | `mm_WordsFn_EndEventA8Result` | 16 | indirect | #132 act: ends event slot +0xA8 with its result code |
| `0x0017BA00` | `mm_WordsFn_EndEventA8` | 16 | indirect | #131 act: ends event slot +0xA8 (handler End, tiles, requeue by kind) |
| `0x0017BA10` | `mm_WordsFn_SetEventA8Var` | 24 | no | #130 act: event slot +0xA8 variable = arg |
| `0x0017BA30` | `mm_WordsFn_SelectEventPersonB` | 100 | no | #129 act: subject B = the event's second person |
| `0x0017BAA0` | `mm_WordsFn_SelectEventPersonA` | 100 | no | #128 act: subject B = the event's first person |
| `0x0017BB10` | `mm_WordsFn_EventQueueShift` | 12 | no | #127 act: (provisional) requeues the first event and shifts the list |
| `0x0017BB20` | `mm_WordsFn_PlaySe` | 24 | no | #126 act: sound effect arg |
| `0x0017BB40` | `mm_WordsFn_GiveBagItem` | 332 | no | #125 act: gives the selected bag item, friendship by its price |
| `0x0017BC90` | `mm_WordsFn_BuyGoods` | 620 | yes | #124 act: pays for goods item arg, friendship by price, then laptop/bag/stamina/music/applicant by kind |
| `0x0017BF00` | `mm_WordsFn_AddVacancy` | 68 | no | #123 act: adds a vacancy for person A at building A, sound 0x15 |
| `0x0017BF50` | `mm_WordsFn_RetreatFamily` | 92 | yes | #122 act: retreats family A, sound 0x17 |
| `0x0017BFB0` | `mm_WordsFn_MoveInPerson` | 64 | yes | #121 act: moves person A's family in |
| `0x0017BFF0` | `mm_WordsFn_RetreatPlace` | 108 | yes | #120 act: retreats the result place, clears the member flag, sound 0x17 |
| `0x0017C060` | `mm_WordsFn_SetPose` | 148 | no | #119 act: talk session pose entry for member A = arg |
| `0x0017C080` | `mm_WordsFn_MarkFamilyKnown` | 52 | no | #118 act: (provisional) family bit via 0x13C950, sound 0x14 |
| `0x0017C0C0` | `mm_WordsFn_FriendshipDown` | 132 | yes | #117 act: friendship loss for member A (type bonus + 3) |
| `0x0017C150` | `mm_WordsFn_FriendshipUp` | 132 | yes | #116 act: friendship gain for member A (type bonus + 5) |
| `0x0017C1E0` | `mm_WordsFn_SelectMemberB` | 72 | no | #115 act: subject B = member +0x1A of family A |
| `0x0017C230` | `mm_WordsFn_SelectBuildingB` | 432 | no | #114 act: building B = A's (0), home (1), under the person (2), current (3), destination (4), result (5) |
| `0x0017C3E0` | `mm_WordsFn_SetCancelAllowed` | 12 | no | #113 act: context +0x130 = arg (cancel button allowed) |
| `0x0017C3F0` | `mm_WordsFn_SetStoryFlagB` | 80 | no | #112 act: sets bit arg of 0x1C5530 |
| `0x0017C440` | `mm_WordsFn_ClearStoryFlagB` | 84 | no | #111 act: clears bit arg of 0x1C5530 |
| `0x0017C4A0` | `mm_WordsFn_SetVar1C5602` | 16 | no | #110 act: script variable 0x1C5602 = arg |
| `0x0017C4B0` | `mm_WordsFn_EventA4Slot10` | 20 | indirect | #109 act: event slot +0xA4 handler Slot10 with arg |
| `0x0017C4D0` | `mm_WordsFn_TextEventA4Name` | 48 | no | #108 text: name of the event in slot +0xA4 |
| `0x0017C500` | `mm_WordsFn_EventA8Slot10` | 20 | indirect | #107 act: event slot +0xA8 handler Slot10 with arg |
| `0x0017C520` | `mm_WordsFn_TextEventTitle` | 16 | no | #106 text: event title, with the member name when it has one |
| `0x0017C530` | `mm_WordsFn_TextOpenHours` | 72 | no | #105 text: opening hours of the family's building type |
| `0x0017C580` | `mm_WordsFn_TextBagItemName` | 68 | no | #104 text: name of the selected bag item |
| `0x0017C5D0` | `mm_WordsFn_TextGoodsName` | 104 | no | #103 text: name of the goods item arg |
| `0x0017C640` | `mm_WordsFn_TextVacancyClaim` | 160 | no | #102 text: claim line of the selected vacancy |
| `0x0017C6E0` | `mm_WordsFn_TextPopulation` | 52 | no | #101 text: (provisional) map value +0x29 with thousands separators |
| `0x0017C720` | `mm_WordsFn_TextClaimLine` | 60 | no | #100 text: claim line of building A/B |
| `0x0017C760` | `mm_WordsFn_TextFamilyRecord` | 60 | no | #99 text: (provisional) cached family record (FAMILY.BIN 0x1D4 bytes) |
| `0x0017C7A0` | `mm_WordsFn_TextOccupation` | 144 | yes | #98 text: (provisional) occupation from the home type and member role |
| `0x0017C830` | `mm_WordsFn_TextBuildingName` | 104 | no | #97 text: building type name (+0x110) |
| `0x0017C8A0` | `mm_WordsFn_TextMemberName2` | 120 | yes | #96 text: (provisional) member second name (+0x5C) |
| `0x0017C920` | `mm_WordsFn_TextMemberName` | 120 | yes | #95 text: member name (+0x4C) of A, B or the result family |
| `0x0017C9A0` | `mm_WordsFn_TextNameSlot` | 24 | no | #94 text: name slot arg |
| `0x0017C9C0` | `mm_WordsFn_EventCallSlot14Off` | 120 | no | #93 act: event Slot14 for the subjects with arg, mode 0 |
| `0x0017CA40` | `mm_WordsFn_EventSetActor` | 324 | no | #92 cond: sets actor arg of event +0xA4 to the subjects, marks the building |
| `0x0017CB90` | `mm_WordsFn_EventActorIs` | 76 | no | #91 cond: actor arg of event +0xA4 is subject A |
| `0x0017CBE0` | `mm_WordsFn_EventA4Var` | 20 | no | #90 cond: event slot +0xA4 variable (+10) |
| `0x0017CC00` | `mm_WordsFn_EventA4IsTalk` | 80 | no | #89 cond: (provisional) event slot +0xA4 kind is 1 or 2 |
| `0x0017CC50` | `mm_WordsFn_EventCallSlot14On` | 120 | no | #88 act: event Slot14 for the subjects with arg, mode 1 |
| `0x0017CCD0` | `mm_WordsFn_EventA8Var` | 20 | no | #87 cond: event slot +0xA8 variable (+10) |
| `0x0017CCF0` | `mm_WordsFn_EventA8Kind` | 96 | no | #86 cond: (provisional) kind of event slot +0xA8 plus 1 |
| `0x0017CD50` | `mm_WordsFn_EventCountdown` | 36 | no | #85 cond: (provisional) 3 minus the event's counter |
| `0x0017CD80` | `mm_WordsFn_EventStage` | 168 | no | #84 cond: (provisional) stage of the context's event |
| `0x0017CE30` | `mm_WordsFn_StartEvent` | 60 | no | #83 act: starts an event from the script for person A |
| `0x0017CE70` | `mm_WordsFn_EventsActive` | 36 | no | #82 cond: any event active |
| `0x0017CEA0` | `mm_WordsFn_LikesBagItem` | 332 | yes | #81 cond: (provisional) the person likes the selected bag item |
| `0x0017CFF0` | `mm_WordsFn_CanAfford` | 124 | no | #80 cond: player money covers the goods item arg |
| `0x0017D070` | `mm_WordsFn_GoodsFitsBag` | 236 | no | #79 cond: (provisional) the goods item arg is a bag item and the bag has room |
| `0x0017D160` | `mm_WordsFn_GoodsOwned` | 320 | no | #78 cond: (provisional) the goods item arg is already owned or unlocked |
| `0x0017D2A0` | `mm_WordsFn_LaptopHasBit` | 64 | no | #77 cond: laptop entry bit arg is active |
| `0x0017D2E0` | `mm_WordsFn_VacancyWantedC` | 8 | no | #76 cond: (provisional) selected vacancy wanted by any member of family A |
| `0x0017D2F0` | `mm_WordsFn_VacancyWantedB` | 8 | no | #75 cond: (provisional) selected vacancy wanted by member A |
| `0x0017D300` | `mm_WordsFn_VacancyWantedA` | 8 | no | #74 cond: (provisional) selected vacancy wanted by family A |
| `0x0017D310` | `mm_WordsFn_VacancyWantedBody` | 896 | yes | shared body of #74/#75/#76 (entered past their prologues) |
| `0x0017D6A0` | `mm_WordsFn_MemberWantsClaim` | 380 | yes | #73 cond: (provisional) member A wants a place matching building A's claim |
| `0x0017D820` | `mm_WordsFn_VacancyClaimMatch` | 240 | no | #72 cond: a vacancy with the same claim as building A, else room check |
| `0x0017D910` | `mm_WordsFn_HasVacancyForPerson` | 104 | no | #71 cond: a vacancy for person A exists |
| `0x0017D980` | `mm_WordsFn_FindVacancy` | 52 | no | #70 act: first vacancy lookup (result unused) |
| `0x0017D9C0` | `mm_WordsFn_ApplicantCountTier` | 56 | no | #69 cond: applicants: none, up to 8, up to 31, full |
| `0x0017DA00` | `mm_WordsFn_BuildingOpen` | 204 | indirect | #68 cond: building B (or type) open now |
| `0x0017DAD0` | `mm_WordsFn_BuildingsOverlap` | 204 | no | #67 cond: (provisional) footprints of buildings A and B overlap |
| `0x0017DBA0` | `mm_WordsFn_FamilyModArg` | 32 | no | #66 cond: family id mod arg |
| `0x0017DBC0` | `mm_WordsFn_PickClaimResident` | 1116 | yes | #65 cond: (provisional) resident whose wants match the selected vacancy's claim |
| `0x0017E020` | `mm_WordsFn_PickTestableResident` | 564 | yes | #64 cond: (provisional) resident of a candidate building that passes TestMember, else random |
| `0x0017E260` | `mm_WordsFn_PickByCandidates` | 48 | yes | #63 cond: collects candidate buildings and picks (0x17E3A0) |
| `0x0017E290` | `mm_WordsFn_PickRandomResident` | 264 | yes | #62 cond: collects candidate buildings, picks a random resident as B |
| `0x0017E3A0` | `mm_Words_PickResidentByType` | 488 | no | random resident of a candidate home whose character type matches arg (type or list 0x1BE950) |
| `0x0017E590` | `mm_Words_CollectNearbyHomes` | 768 | yes | homes with known families around a building, radius by the member's range (table 0x1B97F0), up to 80 |
| `0x0017E890` | `mm_WordsFn_PickOtherResident` | 248 | yes | #61 cond: picks another resident of building B as subject B |
| `0x0017E990` | `mm_WordsFn_ScheduleIs` | 76 | yes | #60 cond: schedule activity equals arg |
| `0x0017E9E0` | `mm_WordsFn_PersonFlagBit` | 32 | no | #59 cond: person +0x22 bit arg |
| `0x0017EA00` | `mm_WordsFn_ScheduleState` | 344 | yes | #58 cond: the person's schedule state, folded by arg (0..5) |
| `0x0017EB60` | `mm_WordsFn_HomeIsType104` | 92 | no | #57 cond: (provisional) the family's home type is 0x104 or 0x105 |
| `0x0017EBC0` | `mm_WordsFn_Arg4` | 8 | no | #56 cond: context +4 |
| `0x0017EBD0` | `mm_WordsFn_MemberByte2B` | 68 | yes | #55 cond: (provisional) member byte +0x2B |
| `0x0017EC20` | `mm_WordsFn_MemberByte2A` | 68 | yes | #54 cond: (provisional) member byte +0x2A |
| `0x0017EC70` | `mm_WordsFn_MemberCharType` | 104 | yes | #53 cond: member character type, folded (0x26 -> 13, others above 0x1D -> 0) |
| `0x0017ECE0` | `mm_WordsFn_IsSpecialFamily` | 120 | no | #52 cond: (provisional) family equals one of three ids in a gp table (arg selects) |
| `0x0017ED60` | `mm_WordsFn_IsApplicant` | 16 | no | #51 cond: the family is on the applicant list |
| `0x0017ED70` | `mm_WordsFn_FamilyBitC` | 8 | no | #50 cond: (provisional) family bit set in 0x1D4DF0 |
| `0x0017ED80` | `mm_WordsFn_FamilyBitB` | 8 | no | #49 cond: (provisional) family bit set in 0x1D4410 |
| `0x0017ED90` | `mm_WordsFn_CanRetreat` | 116 | no | #48 cond: city flag 4, or flag 2 of 0x1C5600 with a family that can retreat |
| `0x0017EE10` | `mm_WordsFn_ScoreA8Tier` | 8 | no | #47 cond: placement score +0xA8 below 200, below 300, or higher |
| `0x0017EE20` | `mm_WordsFn_ScoreA0Tier` | 8 | no | #46 cond: placement score +0xA0 below 200, below 300, or higher |
| `0x0017EE30` | `mm_WordsFn_ApprovalTier` | 60 | no | #45 cond: approval tier folded to 0..2 |
| `0x0017EE70` | `mm_WordsFn_MapHistoryTier` | 76 | no | #44 cond: map history sum: none, up to 10, more |
| `0x0017EEC0` | `mm_WordsFn_MissionGoalTier` | 84 | no | #43 cond: mission goal 0x1F32E0 in five bands (20/50/100/200) |
| `0x0017EF20` | `mm_WordsFn_PopulationRank` | 32 | no | #42 cond: population rank (1..8) minus 1 |
| `0x0017EF40` | `mm_WordsFn_TimeOfDay` | 80 | no | #41 cond: 0 from 3:00 to 12:00, 1 to 18:00, else 2 |
| `0x0017EF90` | `mm_WordsFn_BeforeSix` | 16 | no | #40 cond: clock before 6:00 |
| `0x0017EFA0` | `mm_WordsFn_WeatherTomorrow` | 12 | no | #39 cond: tomorrow's weather (city +0x94) |
| `0x0017EFB0` | `mm_WordsFn_WeatherToday` | 12 | no | #38 cond: today's weather (city +0x90) |
| `0x0017EFC0` | `mm_WordsFn_CityFlag2` | 24 | no | #37 cond: city flag 2 |
| `0x0017EFE0` | `mm_WordsFn_Month` | 36 | no | #36 cond: month (city +0x88 mod 12) |
| `0x0017F010` | `mm_WordsFn_Nop35` | 20 | no | #35 nothing |
| `0x0017F020` | `mm_WordsFn_StaminaTier` | 12 | no | #34 cond: player stamina below 100000, below 1800001, or higher |
| `0x0017F030` | `mm_WordsFn_StoryFlagB` | 76 | no | #33 cond: bit arg of 0x1C5530 (set/cleared by #112/#111) |
| `0x0017F080` | `mm_WordsFn_StoryFlagA` | 76 | no | #32 cond: bit arg of 0x1C5520 |
| `0x0017F0D0` | `mm_WordsFn_GetVar1C5602` | 12 | no | #31 cond: (provisional) script variable 0x1C5602 (set by #110) |
| `0x0017F0E0` | `mm_WordsFn_Flag134` | 8 | no | #30 cond: context +0x134 |
| `0x0017F0F0` | `mm_WordsFn_RandomN` | 60 | no | #29 cond: random mod arg |
| `0x0017F130` | `mm_WordsFn_Random3` | 92 | no | #28 cond: random 0..2 |
| `0x0017F190` | `mm_WordsFn_Random50` | 72 | no | #27 cond: random, about half |
| `0x0017F1E0` | `mm_WordsFn_False` | 8 | no | #26 cond: 0 |
| `0x0017F1F0` | `mm_WordsFn_BuildingCrowd` | 260 | no | #25 cond: visitors at the building: none, some, over half capacity |
| `0x0017F300` | `mm_WordsFn_BuildingFlag100` | 48 | no | #24 cond: building flag 0x100 |
| `0x0017F330` | `mm_WordsFn_TestMember` | 164 | no | #23 act: (provisional) mm_Family_TestMember when there is room for applicants |
| `0x0017F3E0` | `mm_WordsFn_Nop22` | 204 | no | #22 nothing |
| `0x0017F420` | `mm_WordsFn_TryRetreat` | 260 | yes | #21 cond: a member's category-5 place that can retreat |
| `0x0017F530` | `mm_WordsFn_TryMoveIn` | 136 | yes | #20 cond: moves the subject in when not placed yet; nonzero result |
| `0x0017F5C0` | `mm_WordsFn_BuildingFlag2` | 48 | no | #19 cond: building flag 2 |
| `0x0017F5F0` | `mm_WordsFn_ClaimIs50` | 108 | no | #18 cond: claim entry value 0x32 |
| `0x0017F660` | `mm_WordsFn_BuildingClaim` | 100 | no | #17 cond: the building's claim entry (CLAIM.BIN section B) |
| `0x0017F6D0` | `mm_WordsFn_BuildingState` | 128 | no | #16 cond: building flags 8/0x40 or its id range |
| `0x0017F750` | `mm_WordsFn_FriendshipTier` | 100 | no | #15 cond: friendship value below 48, below 100, or higher |
| `0x0017F7C0` | `mm_WordsFn_MemberMeter` | 44 | no | #14 cond: (provisional) meter 0x1C59B0[family*6+member] in five bands |
| `0x0017F7F0` | `mm_WordsFn_FamilyKnown` | 36 | no | #13 cond: (provisional) family bit set in 0x1D4900 |
| `0x0017F820` | `mm_WordsFn_PersonFlag10` | 48 | no | #12 cond: person +0x22 flag 0x10 |
| `0x0017F850` | `mm_WordsFn_IsBirthdayMonth` | 124 | no | #11 cond: (family + member) mod 12 equals the month |
| `0x0017F8D0` | `mm_WordsFn_WorksAtHome` | 152 | no | #10 cond: (provisional) home equals workplace and the type has flag 0x20 |
| `0x0017F970` | `mm_WordsFn_MemberRole` | 184 | yes | #9 cond: (provisional) member +0x28 role and +0x27 bit 0, six cases |
| `0x0017FA30` | `mm_WordsFn_HomeTypeFlag100` | 88 | no | #8 cond: the family's home type has flag 0x100 |
| `0x0017FA90` | `mm_WordsFn_BuildingCategory` | 100 | yes | #7 cond: category (+0x145) of the subject's building type |
| `0x0017FB00` | `mm_WordsFn_PersonHeading` | 180 | no | #6 cond: person's destination: 0 work, 1 home, 2 other building, 3 none |
| `0x0017FBC0` | `mm_WordsFn_PersonWhere` | 180 | no | #5 cond: person's current building: 0 home, 1 work, 2 other building, 3 outside |
| `0x0017FC80` | `mm_WordsFn_PersonAtBuilding` | 564 | indirect | #4 cond: (provisional) where the person stands relative to the building and its hours |
| `0x0017FEC0` | `mm_WordsFn_StatusMid2` | 96 | no | #3 cond: middle two bits of the status byte |
| `0x0017FF20` | `mm_WordsFn_StatusLow2` | 96 | no | #2 cond: low two bits of the status byte |
| `0x0017FF80` | `mm_WordsFn_FriendLevelHigh` | 148 | no | #1 cond: friendship level above 13 for A/B (2 when not acquainted) |
| `0x00180020` | `mm_WordsFn_NameSlotSet` | 48 | no | #0 cond: name slot (0x1C5380 + arg*16) is not empty |
| `0x00180050` | `mm_Words_CallCond` | 32 | no | calls a condition through its member function pointer (__ptmf_scall) |
| `0x00180070` | `mm_Words_CallText` | 32 | no | calls a text getter through its member function pointer |
| `0x00180090` | `mm_Words_CallAct` | 32 | no | calls an action through its member function pointer |
| `0x001800B0` | `mm_Words_SetFlag134` | 16 | no | context +0x134 = 1 |
| `0x001800C0` | `mm_Words_SelectBagItem` | 192 | no | context +0x24 = the a1-th bag item of kinds 0x10/0x11/0x13 |
| `0x00180180` | `mm_Words_SetVacancyIndex` | 12 | no | context +0x13A = a1 (the chosen vacancy) |
| `0x00180190` | `mm_Words_SelectEvent` | 92 | yes | (provisional) context +0x138 = a1 - 1, event lookup for the family |
| `0x001801F0` | `mm_Words_GetGoods` | 88 | no | goods record a1 for building type B this season |
| `0x00180250` | `mm_Words_GetBuildingTypeB` | 12 | no | context +0x14 |
| `0x00180260` | `mm_Words_SetArg` | 12 | no | context +0x2A = a1 |
| `0x00180270` | `mm_Words_GetFriendshipChanged` | 12 | no | context +0x13C (set by #116/#117) |
| `0x00180280` | `mm_Words_GetCancelAllowed` | 12 | no | context +0x130 |
| `0x00180290` | `mm_Words_SetFamily` | 248 | yes | subject A = family a1 member a2 (prefetches the family), clears the rest |
| `0x00180390` | `mm_Words_SetPerson` | 256 | yes | subject A = person a1 with home building, clears the rest |
| `0x00180490` | `mm_Words_dtor` | 84 | yes | frees the 0x140-byte context |
| `0x001804F0` | `mm_Words_ctor` | 48 | yes | allocates the 0x140-byte context |
| `0x001A0C40` | `mm_Words_MatchTypedAnswer` | 840 | no | which answer of the current question matches the typed text (strcmp) |
| `0x001A0F90` | `mm_Words_GetEntryDefault` | 580 | no | default text for a name entry question |
| `0x001A11E0` | `mm_Words_GetEntryMode` | 504 | no | (provisional) first field of the name entry question (passed to mm_NameEntry_Setup) |
| `0x001A13E0` | `mm_Words_GetChoiceText` | 1036 | yes | text of menu choice a3 for code a2: goods with prices ("Don't buy"), claims, vacancies, events |
| `0x001A17F0` | `mm_Words_ApplyChoice` | 504 | yes | stores menu choice a3 for code a2 (bag item, vacancy, event, argument) and jumps |
| `0x001A19F0` | `mm_Words_CountChoices` | 664 | no | number of menu choices for code a2 |
| `0x001A1CA0` | `mm_Words_AppendClipped` | 312 | no | (provisional) appends a string to the message buffer within the line limit |
| `0x001A1DE0` | `mm_Words_ExpandLine` | 2044 | no | expands a script line: conditions pick branches, text getters fill in names, actions run |
| `0x001A2620` | `mm_Words_StepImpl` | 888 | yes | binds the loaded tables on first use, then runs codes until the next message or end code |
| `0x001A29B0` | `mm_Words_Step` | 8 | no | next message of the running script (to mm_Words_StepImpl) |
| `0x001A29C0` | `mm_Words_FriendshipChanged` | 8 | no | global context +0x13C |
| `0x001A29D0` | `mm_Words_CancelAllowed` | 8 | no | global context +0x130 |
| `0x001A29E0` | `mm_Words_LoadForWants` | 596 | yes | script for a wants window line (event or default), reads its block |
| `0x001A2C40` | `mm_Words_LoadForFamily` | 424 | yes | script for family a1 member a2 (talk by phone), reads its block |
| `0x001A2DF0` | `mm_Words_LoadForPerson` | 604 | yes | script for person a1 (may start an event), reads its block |
| `0x001A3050` | `mm_Words_ReadBlock` | 336 | yes | reads a script block from the open WORDS file into the buffer |
| `0x001A31A0` | `mm_Words_OpenFiles` | 172 | yes | opens DATA\BIN\WORDS.BIN and WORDSEVE.BIN for streaming |
| `0x001A3250` | `mm_Words_Init` | 744 | yes | loads the WORDS, WORDSEVE, WORDSCOM and WORDSTEN tables at game start |
| `0x001BAEC0` | `mm_StaticInit_WordsTable` | 7884 | no | copies the WORDS member function table template (0x1B1B48) to 0x1B9100 |
| `0x001BCD90` | `mm_StaticInit_Words` | 52 | yes | constructs the WORDS object (0x1BDDF0) and registers its destructor |
