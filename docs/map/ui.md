# ui

Windows and widgets.

Widget base class (vtable at object `+8`, `0x001BD400`): `+0` state, `+4` timer. Slots: `+8` dtor, `+0xC` Open
(state 1), `+0x10` Close (state 4), `+0x14` Update (calls the slot for the state), `+0x18` Step(dt) (timer += dt; at
1.0 the state advances, 6 wraps to 0), `+0x1C` GetState, `+0x20` Draw, `+0x24` IsIdle (state 0), `+0x28` IsActive
(state 3), then one slot per state: `+0x2C` idle, `+0x30` opening, `+0x34` opened, `+0x38` active, `+0x3C` closing,
`+0x40` closed. The message window (`mm_MsgWin_*`, `0x1BCE10`), the menu window (`mm_MenuWin_*`, `0x1BCF80`), scene B
and the talk classes derive from it.

Other windows: text boxes (`mm_TextBox_*`), frames (`mm_Frame_*`), list menus, the laptop menu, the status panel,
the wants window, the pause/save menu, the applicants list, the city cursor, the minimap, notices, phone calls,
the Town's Pages (the residents' phone book), name entry, the loading screen and the sound test.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

168 functions (ported 3, verified 165). Blocks: 62 can reach a thread switch, 10 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00128610` | `int mm_Widget_GetState(Widget *widget)` | 8 | no | getState slot: the state |
| `0x00128620` | `int mm_Widget_IsIdle(Widget *widget)` | 16 | no | isIdle slot: state == 0 |
| `0x00128630` | `int mm_Widget_IsActive(Widget *widget)` | 16 | no | isActive slot: state == 3 |
| `0x00128640` | `void mm_MsgWin_TypeNext(MsgWin *msgwin)` | 788 | no | message window typewriter step: copies the next word into the text box (read from readAt), plays the message sound unless inside the quote characters at 0x1B5A70 |
| `0x00128960` | `int mm_MsgWin_NextLine(MsgWin *msgwin, char *text)` | 212 | no | when waiting (state 3): takes the next line, scrolls the 4-line text box up by one line, state 6 |
| `0x00128A40` | `void mm_MsgWin_Closing(MsgWin *msgwin)` | 68 | indirect | state 4: Step(0.1), then idle |
| `0x00128A90` | `void mm_MsgWin_Opening(MsgWin *msgwin)` | 68 | indirect | state 1: Step(0.1), then active |
| `0x00128AE0` | `int mm_MsgWin_Update(MsgWin *msgwin)` | 108 | no | typing (state 6): button 0x40 skips the delay, then mm_MsgWin_TypeNext; other states run the base update (0x173450) |
| `0x00128B50` | `void mm_MsgWin_Draw(MsgWin *msgwin, int pass)` | 356 | no | in pass 2: fades the window model node by state (alpha via its matrix row 0x1B2550), draws it and the text box |
| `0x00128CF0` | `int mm_MsgWin_SetMessage(MsgWin *msgwin, int message, uint8_t sound)` | 136 | no | starts message a1 with sound code a2: resets buffers and cursors, clears the text box |
| `0x00128D80` | `MsgWin *mm_MsgWin_dtor(MsgWin *msgwin, int16_t deleteFlag)` | 148 | yes | vtable 0x1BCE10; deletes the frame (0x14C210) and the text box, base destructor 0x173560 |
| `0x00128E20` | `MsgWin *mm_MsgWin_ctor(MsgWin *msgwin)` | 184 | yes | vtable 0x1BCE10; text box 18x4 (style -2) at 0x7240,0x8240 and a 0xC0-byte window frame (0x14C270) |
| `0x00135EF0` | `void mm_CityCursor_SelectTile(CityCursor *cursor, int tile, CityScene *city)` | 456 | no | picking a building: one marked by an event (flag 0x200) queues event 0x106; otherwise the building menu opens (mode 0xA) for it, the cursor in mode 4 with its open timer at 30, and this frame's info window rectangle is set from the frame (grown by 100 and 25 per step of the timer run so far, 15 at most |
| `0x001360C0` | `void mm_CityCursor_UpdateInfo(CityCursor *cursor, CityScene *city)` | 1272 | yes | the info window of the city cursor: the view for this frame's parity (0x1BDCD0 & 1) has its five line frames reset to style 0 and the lines refilled from what the cursor points at (target, 0xFFFF for nothing; a building when targetIsPerson is 0, otherwise a person) |
| `0x001365C0` | `void mm_CityCursor_Move(CityCursor *cursor, CityScene *city, int a2)` | 420 | yes | the Cross button on the thing under the cursor (target; 0xFFFF for nothing), when the city is in mode 1 and the Square-side button 0x20 is not held. A cursor on a building (targetIsPerson 0) either picks the tile (the player is in mode 3) or starts a talk about the object (mode view 1), or asks for mode 0xA with its marks reset |
| `0x00136770` | `void mm_CityCursor_Update(CityCursor *cursor, CityScene *city, int a2)` | 1292 | yes | the city cursor's info window for one frame (nothing while visible is clear). The info text is refreshed (CityCursor_UpdateInfo), then by the mode: 0 moves the cursor (CityCursor_Move, with the enabled flag) |
| `0x00136C80` | `void mm_CityCursor_SetTarget(CityCursor *cursor, uint16_t target, int isPerson, Vec4 *position)` | 44 | no | the thing under the cursor: object or person index `target`, whether it is a person (`isPerson`), and its position (target, targetIsPerson, targetPos) |
| `0x00136CF0` | `void mm_CityCursor_LayoutInfo(CityCursor *cursor, Vec4 *point, Vec4 *matrix)` | 536 | no | places the info window next to the projected cursor point (GS units, 4 fraction bits): x kept within 0x7080..0x9140, the lines stacked upwards from 24 pixels above it, or from 12 pixels below it when they would run off the top (y 0x7A60 at least), and kept above y 0x8700 at the bottom |
| `0x00136F10` | `void mm_CityCursor_Draw(CityCursor *cursor)` | 276 | no | the cursor's info window for this frame: its packet and info lines, then either the extra packet over a cleared Z (mode 4) or a dimmed screen with the fade frame (other non-zero modes) |
| `0x00137030` | `CityCursor *mm_CityCursor_dtor(CityCursor *cursor, int16_t deleteFlag)` | 268 | yes | two views of 0xD0 bytes: the fade and info frames, then five lines each with the frame behind it |
| `0x00137140` | `CityCursor *mm_CityCursor_ctor(CityCursor *cursor)` | 1064 | yes | the city cursor: the base object, a model loaded (0x1B5EF0) and deleted again at once, the lines and the fade counters cleared, and for each of the two views (0xD0 bytes, one per frame parity) five text boxes (15 by 1, style -1, lines) with five frames (lineFrames, a 24 by 12 box) and the two frames of the fade (frame and fadeFrame), then the view's GS packet (extraPacket): the TEX0 style register in its fourth quadword (the sizes are log2 of 128 and 64 in its texture width and height fields) and the sprite, colour and coordinate words after it |
| `0x0013DB40` | `void mm_Minimap_BuildPacket(Minimap *minimap)` | 1096 | no | builds the minimap DMA packet (packet): palette 0x1AF180 to block 0x1E00, the cells as a 128 x 128 4-bit image to 0x1E01, then one sprite |
| `0x0013DF90` | `void mm_Minimap_SetView(Minimap *minimap, float *camera, float angle)` | 144 | no | rotates the minimap by the camera (sceVu0RotMatrixZ) and offsets it by the position (scale 0.0105) |
| `0x0013E020` | `void mm_Minimap_Plot(Minimap *minimap, uint32_t tile, int size, int colour, int keepHigher)` | 388 | no | marks tile a1 on the minimap with colour/size arguments |
| `0x0013E1B0` | `void mm_Minimap_Draw(Minimap *minimap)` | 180 | no | draws the minimap models; marker colour blinks every 32 frames |
| `0x0013E270` | `Minimap *mm_Minimap_DeletingDtor(Minimap *minimap, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x0013E2D0` | `Minimap *mm_Minimap_ctor(Minimap *minimap)` | 172 | no | base constructor, models from 0x1F3B14/0x1F3B18, builds the packet |
| `0x00149990` | `uint32_t *mm_StatusPanel_BuildPacket(StatusPanel *panel, uint32_t *packet)` | 828 | no | status panel packet (texture sizes via mm_Math_Log2, table 0x1AF3B0) |
| `0x00149CD0` | `void mm_StatusPanel_Update(StatusPanel *panel, CityScene *city)` | 2232 | yes | status text: "STATUS", population vs target, theme share, salary, money, digits |
| `0x0014A590` | `void mm_StatusPanel_Draw(StatusPanel *panel)` | 492 | no | panel models and its four text boxes |
| `0x0014A780` | `StatusPanel *mm_StatusPanel_dtor(StatusPanel *panel, int16_t deleteFlag)` | 184 | yes | deletes its four text boxes, base destructor |
| `0x0014A840` | `StatusPanel *mm_StatusPanel_ctor(StatusPanel *panel)` | 828 | yes | four text boxes ("Strength" ...) and the panel packet |
| `0x0014AB80` | `void mm_Laptop_LoadState(Laptop *laptop, McManager *mc)` | 68 | no | reads gp 0x1BDAB8.. (4 bytes) and the 14 laptop entries at 0x1F36F0 (0x38 bytes) from the save file |
| `0x0014ABD0` | `void mm_Laptop_SaveState(Laptop *laptop, McManager *mc)` | 68 | no | writes them |
| `0x0014AC20` | `uint32_t mm_Laptop_CountActive(void)` | 232 | no | active laptop entries |
| `0x0014AD10` | `void mm_Laptop_SetItemText(Laptop *laptop, CityScene *city, uint32_t item)` | 108 | indirect | text for laptop item a2 (14 items, jump table 0x1B6570); default "-Laptop Computer- Save or quit the game." |
| `0x0014AE10` | `void mm_Laptop_RemoveEntry(Laptop *laptop, int entry)` | 184 | no | removes the entry: the ones after it move up, the last slot is freed, and the cursor (selected) steps back when nothing follows |
| `0x0014AED0` | `void mm_Laptop_AddEntry(Laptop *laptop, int entry)` | 256 | no | adds an entry (14 slots, -1 free) unless it is there. Entries 6 to 8 are tiers of one thing: a better one replaces a worse, a worse one is not added |
| `0x0014AFD0` | `void mm_Laptop_Draw(Laptop *laptop)` | 356 | yes | laptop model (luminosity per item), text, fade |
| `0x0014B140` | `void mm_Laptop_Animate(Laptop *laptop)` | 772 | no | the laptop entries' carousel matrices: turn about y by slot and turn, pi/12 tilt, radius + centre, scale; the selected entry lifted in state 2 |
| `0x0014B450` | `int mm_Laptop_RunEntry(Laptop *laptop, CityScene *city)` | 484 | no | runs the selected entry by its type (events, monthly pass, ...) |
| `0x0014B640` | `int mm_Laptop_Update(Laptop *laptop, CityScene *city)` | 1640 | yes | laptop menu: cursor with key repeat, select, sounds, texts |
| `0x0014BCB0` | `Laptop *mm_Laptop_dtor(Laptop *laptop, int16_t deleteFlag)` | 100 | yes | deletes its text box |
| `0x0014BD20` | `Laptop *mm_Laptop_ctor(Laptop *laptop)` | 32 | no | model from 0x1F3B08, clears its state |
| `0x0014BD40` | `void mm_Laptop_InitEntries(void)` | 216 | no | first-time defaults of the laptop entries (0x1AF640) |
| `0x0014BE20` | `void mm_Frame_SetStyle(Frame *frame, int style)` | 644 | no | sets the frame's style and, from the cell table, its texture coordinates (in 32nds) |
| `0x0014C0B0` | `void mm_Frame_GetRect(Frame *frame, uint32_t *x, uint32_t *y, uint32_t *width, uint32_t *height)` | 120 | no | frame position and size in screen units (GS offsets removed) |
| `0x0014C130` | `void mm_Frame_SetCorners(Frame *frame, int x0, int y0, int x1, int y1)` | 124 | no | GS XY of two corners; a pair outside 0..0xFFFF is zeroed |
| `0x0014C1B0` | `void mm_Frame_SetRect(Frame *frame, int x, int y, int width, int height)` | 72 | no | frame at (a1, a2) size a3 x a4 (GS coordinates) |
| `0x0014C210` | `Frame *mm_Frame_DeletingDtor(Frame *frame, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x0014C270` | `Frame *mm_Frame_ctor(Frame *frame, int style, int x, int y, int width, int height, uint32_t colour)` | 580 | no | window frame: position, size, style, colour |
| `0x0014C4C0` | `void mm_WaitText_Draw(void)` | 68 | no | blinking "Please wait...." text box (0x1BDAC4) |
| `0x0014C510` | `int mm_WaitText_Append(char *text)` | 16 | no | appends a0 to the wait text |
| `0x0014C520` | `void mm_WaitText_Set(char *text)` | 16 | no | sets the wait text to a0 |
| `0x0014C530` | `void mm_WaitText_Delete(void)` | 44 | yes | deletes the wait text box |
| `0x0014C560` | `void mm_WaitText_Create(char *text)` | 180 | yes | creates the wait text box ("Please wait....") |
| `0x00150990` | `void mm_ListMenu_ScrollTo(ListMenu *menu, int item)` | 80 | no | scroll so item a1 is visible (visibleItems, itemHeight) |
| `0x001509E0` | `void mm_ListMenu_SetCursor(ListMenu *menu, int dx, int dy, int depth)` | 188 | no | cursor offset a1/a2 and colour a3 |
| `0x00150AA0` | `void mm_ListMenu_ClearItem(ListMenu *menu, int item)` | 216 | no | empties the text of item a1 (the last one is popped), or of every item and resets the counts (-1) |
| `0x00150B80` | `void mm_ListMenu_SetItemText(ListMenu *menu, char *text, int item)` | 148 | no | text of item a2 (-1 = selected) |
| `0x00150C20` | `void mm_ListMenu_AddItems(ListMenu *menu, int a1, int a2, int a3, int t0, int t1, int t2)` | 384 | yes | creates the item text boxes |
| `0x00150DA0` | `int mm_ListMenu_HandleInput(ListMenu *menu, int32_t *choice)` | 888 | no | cursor with key repeat, select/cancel, sounds; returns the result |
| `0x00151140` | `void mm_ListMenu_Draw(ListMenu *menu)` | 340 | no | frame, highlight bar and item text boxes |
| `0x001512A0` | `ListMenu *mm_ListMenu_dtor(ListMenu *menu, int16_t deleteFlag)` | 168 | yes | deletes item text boxes and the frame |
| `0x00151350` | `ListMenu *mm_ListMenu_ctor(ListMenu *menu, int a1, int a2, int a3, int t0, int t1, int t2)` | 604 | yes | list menu with a frame at the given position/size |
| `0x00152A50` | `void mm_WantsWin_FormatLine(WantsWin *wants, CityScene *city, int index, char *out)` | 212 | yes | "%s %s wants %s." for a family's request (claims) |
| `0x00152B30` | `void mm_WantsWin_Draw(WantsWin *wants)` | 132 | no | window model and four text boxes |
| `0x00152BC0` | `void mm_WantsWin_Update(WantsWin *wants, CityScene *city)` | 2248 | yes | residents' wants window: lists, cursor, values |
| `0x00153490` | `WantsWin *mm_WantsWin_dtor(WantsWin *wants, int16_t deleteFlag)` | 172 | yes | deletes its four text boxes |
| `0x00153540` | `WantsWin *mm_WantsWin_ctor(WantsWin *wants)` | 152 | no | model 0x1F3B04 and defaults |
| `0x001535E0` | `void mm_PauseMenu_Draw(PauseMenu *pause)` | 276 | no | window sliding in as it opens (openTime / 120), the main or slot menu, the message box |
| `0x00153700` | `void mm_PauseMenu_StateMain(PauseMenu *pause)` | 612 | no | Resume / Save / Quit selection |
| `0x00153970` | `void mm_PauseMenu_ShowCardMessage(PauseMenu *pause, int message)` | 524 | no | memory card messages: "Failed to save.", "Failed to format.", "does not have enough space ... 1006KB" |
| `0x00153B80` | `int mm_PauseMenu_StateSave(PauseMenu *pause, CityScene *city)` | 684 | yes | saving: memory card manager steps, results |
| `0x00153E30` | `void mm_PauseMenu_StateFormat(PauseMenu *pause)` | 756 | yes | the pause menu's format state, a step machine on its step counter (waitFrames): 0 the card check (as in the wait state), 1 the card's status (4 still being checked: the "Now checking memory card" text; 3 goes on after resetting the save file; 2 message 5; else message 2), 2 waits for the card manager to settle, 3 formats the card (mm_SaveFile_Format: 2 failed, message 4; 1 done, back to the save state 6 with the manager reset; 0 still running, the "Formatting memory card" text alternating with the do-not-remove text every 16 frames of the blink counter) |
| `0x00154130` | `void mm_PauseMenu_StateWait(PauseMenu *pause)` | 324 | yes | the pause menu's card check state: the card manager is stepped; when the card in the slot (cardPort) reports 2 the menu shows card message 5. Otherwise the main menu (mainMenu) takes its input: choice 1 or the cancel (-1) gives the pad focus back, resets the menu and goes to state 4 (the slot menu) |
| `0x00154280` | `int mm_PauseMenu_StateSlotSelect(PauseMenu *pause)` | 1356 | yes | slot list and "Do you wish to overwrite?" |
| `0x001547D0` | `void mm_PauseMenu_Reset(PauseMenu *pause)` | 192 | yes | back to the top menu |
| `0x00154890` | `int mm_PauseMenu_Update(PauseMenu *pause, CityScene *city)` | 1804 | yes | pause menu state machine (Resume, Save, Quit "Are you sure you want to quit?") |
| `0x00154FA0` | `PauseMenu *mm_PauseMenu_dtor(PauseMenu *pause, int16_t deleteFlag)` | 172 | yes | deletes its menus and the card manager |
| `0x00155050` | `PauseMenu *mm_PauseMenu_ctor(PauseMenu *pause)` | 76 | no | clears state, default name (0x1B73D0) |
| `0x00155330` | `void mm_Applicants_Layout(Applicants *applicants)` | 820 | no | the card slides in over the first 20 frames of its counter (slideFrame; its x grows by 0.15 a frame), stays, and from frame 60 for 6 frames drifts (its y grows by 0.4 a frame); after that the counter, sliding and cardShown clear, the cursor and the top entry take the saved entry (savedTop) and the list scrolls back to its first entry |
| `0x00155670` | `void mm_Applicants_Select(Applicants *applicants, int index)` | 184 | yes | puts the cursor on applicant `index` (the top one before is kept in savedTop): the card model's matrix is the standard one moved 3 and 2.5 units from the first model's place, the view scrolls to nothing, the card text is built, and the mode goes to 1 (cardShown) |
| `0x00155730` | `void mm_Applicants_SetMode(Applicants *applicants, int mode)` | 40 | yes | sets cardShown to `mode`; mode 1 refreshes the info text |
| `0x00155760` | `void mm_Applicants_BuildInfo(Applicants *applicants)` | 1008 | yes | info text: name, "Month %s Year %s", building or unit, size "%sm" |
| `0x00155B50` | `void mm_Applicants_ScrollTo(Applicants *applicants, int index)` | 260 | no | after entry `index` left the list (-1: none did): pulls the two list positions back over it, clamps them to the list and refreshes each entry's claim section |
| `0x00155C60` | `void mm_Applicants_PlaceAt(Applicants *applicants, float *point)` | 408 | no | places the applicant card at `point`: both card models there (their matrices' last rows), and the card text positioned and sized from the point's x, y and depth (z) through fixed factors |
| `0x00155E00` | `void mm_Applicants_DrawDetails(Applicants *applicants)` | 1620 | yes | the lighting of the applicant card's claim icons as the cursor and the shown entry (top) move. Idle (rollDir 0), a gap between them starts a step: forward (top < cursor: direction +1, the icon of the top entry lit on the eight nodes of the table at 0x1B7A10, the cursor's on the first) or backward (direction -1, rollPhase at 40, the other way round) |
| `0x00156470` | `void mm_Applicants_Draw(Applicants *applicants)` | 1540 | yes | the applicants panel |
| `0x00156A90` | `Applicants *mm_ApplicantsBase_dtor(Applicants *applicants, int16_t deleteFlag)` | 124 | yes | vtable 0x1BCF60, deletes its text box |
| `0x00156B10` | `Applicants *mm_ApplicantsBase_ctor(Applicants *applicants)` | 320 | yes | vtable 0x1BCF60, text box, empty list |
| `0x00157110` | `int mm_Applicants_Update(Applicants *applicants, CityScene *city)` | 1660 | yes | the applicants panel per frame: cursor, prefetching family records, assigning |
| `0x001577A0` | `Applicants *mm_Applicants_dtor(Applicants *applicants, int16_t deleteFlag)` | 96 | yes | vtable 0x1BCF70, then the base destructor |
| `0x00157800` | `Applicants *mm_Applicants_ctor(Applicants *applicants)` | 156 | yes | base constructor, vtable 0x1BCF70 |
| `0x0015F920` | `void mm_MenuWin_FillItems(MenuWin *menuwin, char *item)` | 104 | yes | fills its list (13 or 14 items) |
| `0x0015F990` | `void mm_MenuWin_Open(MenuWin *menuwin, int menuId, int count)` | 248 | yes | creates its list menus for menu id a1 (-2001 ...) |
| `0x0015FA90` | `void mm_MenuWin_Draw(MenuWin *menuwin, int pass)` | 268 | no | window model and list menu in pass 2 |
| `0x0015FBA0` | `void mm_MenuWin_Closing(MenuWin *menuwin)` | 92 | yes | state 4: Step(0.1), frees the frame, then idle |
| `0x0015FC00` | `void mm_MenuWin_Update(MenuWin *menuwin)` | 68 | indirect | list input; on a choice calls its close slot |
| `0x0015FC50` | `void mm_MenuWin_Opening(MenuWin *menuwin)` | 68 | indirect | state 1: Step(0.1), then active |
| `0x0015FCA0` | `MenuWin *mm_MenuWin_dtor(MenuWin *menuwin, int16_t deleteFlag)` | 124 | yes | vtable 0x1BCF80, deletes the list menu, base destructor |
| `0x0015FD20` | `MenuWin *mm_MenuWin_ctor(MenuWin *menuwin)` | 76 | no | base constructor, vtable 0x1BCF80 |
| `0x0015FE30` | `int mm_Markers_CheckCircle(Markers *markers, CityScene *city, int a2)` | 212 | no | the building markers' Circle check (a Markers method): while Circle alone (0x20) is held in city mode 1 (and a2) hides the cursor and shows the markers; hides them again on release |
| `0x00161F00` | `int mm_LoadingScreen_Update(LoadingScreen *loading, int fadeIn)` | 604 | no | loading screen frame: sprite circling or pulsing, background and sprite drawn, fade in or out by 4 (2 while fading, then 0 or 1), text once clear |
| `0x00162160` | `int mm_LoadingScreen_SetText(LoadingScreen *loading, char *text)` | 12 | no | text box text |
| `0x00162170` | `void mm_LoadingScreen_SetTextPos(LoadingScreen *loading, int x, int y)` | 28 | no | text box position (screen coordinates) |
| `0x00162190` | `void mm_LoadingScreen_SetSpritePos(LoadingScreen *loading, int x, int y, int z)` | 48 | no | sprite position |
| `0x001621C0` | `LoadingScreen *mm_LoadingScreen_dtor(LoadingScreen *loading, int16_t deleteFlag)` | 180 | yes | deletes text box and sprites |
| `0x00162280` | `LoadingScreen *mm_LoadingScreen_ctor(LoadingScreen *loading, int a1, uint8_t a2)` | 232 | yes | DATA\A3D\INTRO\LOADINGT.TEX sprites and a text box |
| `0x001625A0` | `void mm_NameEntry_HandleInput(NameEntry *entry)` | 1712 | no | one frame of the name entry. The keyboard is a grid of 6 columns (column the cursor column, row its row, firstRow the first row shown of the `rows`, 4 shown) over the keys of the set at 0x1B13E0 (keySet; keyCount keys; a key byte of 0xFF is "done", 0xFE a space, 0xFD a delete) |
| `0x00162C50` | `void mm_NameEntry_Draw(NameEntry *entry)` | 644 | no | draws the name entry (state; 0 draws nothing, 1 is typing, else only the board model): the typed name padded to its length (maxLength) with filler glyphs (0x1B8038), the character under the cursor (length) blinking between a space and '*' at the 16th frame of its 32-frame cycle (blink) |
| `0x00162EE0` | `int mm_NameEntry_Update(NameEntry *entry)` | 240 | no | states 3 and 4 slide the panel in (26 frames, then input) and out (to closed); state 1 takes input. Returns the state |
| `0x00162FD0` | `void mm_NameEntry_Close(NameEntry *entry)` | 48 | no | closes the name entry: unless it is closed already it slides out over 25 frames (state 4), or closes at once (state 0) when it does not slide |
| `0x00163000` | `void mm_NameEntry_Setup(NameEntry *entry, uint32_t mode, uint8_t maxLength, int a3, int t0, char *text)` | 912 | no | opens the name entry: the keyboard set (`mode`, the text pointer table at 0x1B13E0), the name length (up to 24, maxLength) with the name cleared or taken from `text`, the scrolling hint (`hint`, else empty; scrolled when longer than 10 characters), the cursor at the end of the name, the state (3 when `a3` is 0, else 1) with the board model placed (0.3 lower in state 3), the keyboard text from its first line (6 characters a line) and the arrows for more lines above and below (moreAbove, moreBelow) |
| `0x00163390` | `NameEntry *mm_NameEntry_dtor(NameEntry *entry, int16_t deleteFlag)` | 184 | yes | the name entry's destructor: its four text boxes (hintText, keysText, nameText, lineText) deleted, then the object when asked |
| `0x00163450` | `void mm_NameEntry_CreateTexts(NameEntry *entry)` | 424 | yes | the four text boxes of the name entry window (hintText 16 by 1, keysText 3 by 4 with larger glyphs, nameText 12 by 1 and lineText 16 by 1, all at depth 0x10001000), and the model it shows: the shared model (0x1F3B6C, model), its matrix (boardMatrix) and the world matrix of its node 1 (markerMatrix), set to the one at 0x1B2550 moved to markerX, markerY and the board's z |
| `0x00163600` | `NameEntry *mm_NameEntry_ctor(NameEntry *entry)` | 136 | yes | the name entry window: its texts, then the board placed at restHeight and z -4.4 (the marker takes the same z) and the place kept in boardPlace |
| `0x0016C3A0` | `void mm_PhoneCall_Next(PhoneCalls *phone, CityScene *city)` | 204 | no | shifts the call queue and drops input focus |
| `0x0016C470` | `void mm_PhoneCall_Remove(PhoneCalls *phone, CityScene *city, uint32_t family, uint32_t member)` | 372 | no | removes the queued call for (a2, a3) |
| `0x0016C5F0` | `int mm_PhoneCall_Post(PhoneCalls *phone, int person, uint32_t family, uint16_t member, int t0)` | 124 | no | queues a call from a family member (4 entries; none when the family already has one); 1 when queued |
| `0x0016C670` | `void mm_PhoneCall_Draw(PhoneCalls *phone)` | 364 | yes | phone model, ringing light and sound |
| `0x0016C7E0` | `int mm_PhoneCall_Update(PhoneCalls *phone, CityScene *city)` | 1940 | yes | ringing phone: "Call from boss", "Call from stranger", "Call from %s", answering, input focus |
| `0x0016CF80` | `PhoneCalls *mm_PhoneCall_DeletingDtor(PhoneCalls *phone, int16_t deleteFlag)` | 68 | yes | delete when the flag is > 0 |
| `0x0016CFD0` | `PhoneCalls *mm_PhoneCall_ctor(PhoneCalls *phone)` | 52 | no | model 0x1F3B08, empty queue |
| `0x0016D010` | `void mm_Notice_Draw(Notice *notice)` | 76 | no | its two text boxes while shown |
| `0x0016D060` | `void mm_Notice_Tick(Notice *notice)` | 56 | no | counts down the display timer, then hides |
| `0x0016D0A0` | `void mm_Notice_Clear(Notice *notice)` | 16 | no | id -1, hidden |
| `0x0016D0B0` | `void mm_Notice_ShowCentered(Notice *notice, char *text, int a2)` | 168 | no | centred text for 300 frames |
| `0x0016D160` | `void mm_Notice_ShowText(Notice *notice, char *text)` | 144 | no | text for 300 frames |
| `0x0016D1F0` | `void mm_Notice_Show(Notice *notice, int id)` | 180 | no | notice a1 from the table at 0x1B81E0 ("A bit hungry." ...), 300 frames |
| `0x0016D2B0` | `Notice *mm_Notice_dtor(Notice *notice, int16_t deleteFlag)` | 124 | yes | deletes its two text boxes |
| `0x0016D330` | `Notice *mm_Notice_ctor(Notice *notice)` | 208 | yes | creates its two text boxes |
| `0x0016D400` | `int mm_SoundTest_BgmPage(TitleScene *scene, uint32_t a1, uint32_t a2, uint32_t a3)` | 936 | no | sound test BGM page: loads banks, plays/stops sequences |
| `0x0016D7B0` | `int mm_SoundTest_SePage(TitleScene *scene, uint32_t a1, uint32_t a2, uint32_t a3)` | 760 | no | sound test SE page: plays effects with distance/pan |
| `0x0016DAB0` | `int mm_SoundTest_Update(TitleScene *scene)` | 1176 | no | "*** Sound Test ***" screen |
| `0x0016DF50` | `void mm_SoundTest_BgmText(TitleScene *scene)` | 284 | no | BGM page text ("BGM No:") |
| `0x0016E070` | `void mm_SoundTest_SeText(TitleScene *scene)` | 508 | no | SE page text ("SE No:", "R1/R2 Distance(+/-)") |
| `0x001714A0` | `int mm_TownPages_SelectedEntry(TownPages *pages)` | 440 | no | the entry under the cursor. On a contents page: the family type in row `row`, counting only types with known families, 10 per page. On a family page: the family id, counting known families from the first one of the type whose listing the page belongs to |
| `0x00171660` | `int mm_TownPages_ClampCursor(TownPages *pages, int cursor, int direction)` | 180 | no | cursor 0..10 limited by the entries on the page (pageEntries); 10 = the CLOSE item |
| `0x00171720` | `void mm_TownPages_BuildIndex(TownPages *pages)` | 632 | yes | builds the book's index: one 8-byte entry per family type (count of known families, the first of them, the page the type starts on), pages of 10 families each. The contents pages (10 types a page, at least one) come first, so every start page moves up by their number, and the total page count includes them |
| `0x001719A0` | `void mm_TownPages_FillContents(TownPages *pages)` | 604 | no | a contents page: ten family types with known families, name and start page each, five per column; an empty book gets the explanation and the cursor on CLOSE |
| `0x00171C10` | `void mm_TownPages_FillPage(TownPages *pages)` | 1064 | yes | page text, one resident per line ("%s\n TEL:%s") |
| `0x00172040` | `void mm_TownPages_Draw(TownPages *pages)` | 228 | no | book model, text boxes and the cursor frame |
| `0x00172130` | `void mm_TownPages_Update(TownPages *pages, CityScene *city)` | 4136 | yes | open animation, page turning and cursor input, calling a resident (mm_CityScene_StartTalk) |
| `0x00173160` | `TownPages *mm_TownPages_dtor(TownPages *pages, int16_t deleteFlag)` | 268 | yes | frame, entry table and six text boxes |
| `0x00173270` | `TownPages *mm_TownPages_Init(TownPages *pages)` | 212 | yes | book model 0x1F3B20, cursor frame, cleared state |
| `0x00173350` | `int mm_Widget_Step(Widget *widget, float dt)` | 92 | no | step slot: timer += f12; at 1.0 timer = 0 and state + 1 (6 wraps to 0), returns 1 |
| `0x001733B0` | `void mm_Widget_Closed(Widget *widget)` | 24 | indirect | base onState slot of state 5 (closed): Step(1.0) |
| `0x001733D0` | `void mm_Widget_Closing(Widget *widget)` | 24 | indirect | base onState slot of state 4 (closing): Step(1.0) |
| `0x001733F0` | `void mm_Widget_Active(Widget *widget)` | 8 | no | base onState slot of state 3 (active): nothing |
| `0x00173400` | `void mm_Widget_Opened(Widget *widget)` | 24 | indirect | base onState slot of state 2 (opened): Step(1.0) |
| `0x00173420` | `void mm_Widget_Opening(Widget *widget)` | 24 | indirect | base onState slot of state 1 (opening): Step(1.0) |
| `0x00173440` | `void mm_Widget_Idle(Widget *widget)` | 8 | no | base onState slot of state 0 (idle): nothing |
| `0x00173450` | `int mm_Widget_Update(Widget *widget)` | 212 | indirect | update slot: calls the onState slot of the current state |
| `0x00173530` | `void mm_Widget_Draw(Widget *widget, int pass)` | 8 | no | draw slot: nothing |
| `0x00173540` | `void mm_Widget_Close(Widget *widget)` | 16 | no | close slot: state 4, timer 0 |
| `0x00173550` | `int mm_Widget_Open(Widget *widget)` | 16 | no | open slot: state 1, timer 0 |
| `0x00173560` | `Widget *mm_Widget_dtor(Widget *widget, short deleteFlag)` | 76 | yes | vtable 0x1BD400 |
| `0x001735B0` | `Widget *mm_Widget_ctor(Widget *widget)` | 28 | no | vtable 0x1BD400 at +8, state and timer 0 |
| `0x00183500` | `void mm_TextBox_Build(TextBox *textbox, int clut)` | 1556 | no | rebuilds the text box's DMA packet: palette upload (when clut != 0), one glyph upload per cell to GS block 0x1E01 + 2n, then a packed GIF tag of one textured sprite per cell |
| `0x00183B20` | `void mm_TextBox_Draw(TextBox *textbox)` | 8 | no | queues the text box packet (this+8) with mm_Dma_AppendCall |
| `0x00183B30` | `void mm_TextBox_AppendText(TextBox *textbox, char *text, int raw)` | 692 | no | lays out text from the cursor on, wrapping words at ' \n\r'; glyphs from mm_Font_GetGlyph |
| `0x00183DF0` | `void mm_TextBox_SetText(TextBox *textbox, char *text, int raw)` | 8 | no | resets the cursor and lays out the text like mm_TextBox_AppendText |
| `0x00183E00` | `void mm_TextBox_SetCharSize(TextBox *textbox, int width, int height, int pitchX, int pitchY)` | 56 | no | glyph size (charWidth, charHeight) and pitch (pitchX, pitchY; values <= 0 keep the old one), then rebuilds |
| `0x00183E40` | `void mm_TextBox_SetPos(TextBox *textbox, int x, int y, int z)` | 20 | no | x, y and z in GS units, then rebuilds |
| `0x00183E60` | `void mm_TextBox_SetPosAndSize(TextBox *textbox, int x, int y, int charWidth, int charHeight, int pitchX, int pitchY, int z)` | 36 | no | position, glyph size and pitch at once, then rebuilds |
| `0x00183E90` | `TextBox *mm_TextBox_dtor(TextBox *textbox, int16_t deleteFlag)` | 112 | yes | sceGsSyncPath, frees the packet (+8); deletes this when the flag is > 0 |
| `0x00183F00` | `TextBox *mm_TextBox_Init(TextBox *textbox, int columns, int rows, int style)` | 300 | yes | grid of (a1*2) cols x a2 rows; default pos 0x6C00,0x7900 z 16000000, glyph 0xC0 x 0xB0; a3 = style table (-1 -> 0x1B24A0, -2 -> 0x1B24E0, 0 none) in clut; mallocs the packet |
