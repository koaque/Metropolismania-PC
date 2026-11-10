# scene

The scenes before the city. The title scene (`mm_TitleScene_*`): logos, memory card check, title screen and its
menus (`mm_Title_*`: logo animation, press start, new game intro, opening movie). The stage scene (`mm_StageScene_*`):
the stage introduction (city name, stage title, approval rate) and the mission briefing telop. Then the city (`city`).

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

25 functions (ported 1, verified 24). Blocks: 15 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001515B0` | `void mm_StageScene_ShowCityName(StageScene *scene)` | 260 | yes | the city's name (up to its first line break, from 0x1C5680) on two rows |
| `0x001516C0` | `void mm_StageScene_ShowStageTitle(StageScene *scene)` | 256 | yes | "STAGE n" title card |
| `0x001517C0` | `void mm_StageScene_ShowApprovalRate(StageScene *scene)` | 596 | yes | "Your Approval Rate" screen |
| `0x00151A20` | `void mm_StageScene_MissionBriefing(StageScene *scene)` | 1064 | yes | the boss explains the mission in the message window |
| `0x00151E50` | `int mm_StageScene_BuildBriefingText(StageScene *scene)` | 452 | yes | "Let me explain Mission %d to you." + the goal text + "That's all. Good luck to you!" |
| `0x00152020` | `void mm_StageScene_ShowTelop(StageScene *scene)` | 1204 | yes | the scrolling telop of the briefing. The kind of telop follows the stage: 0 for stage 1, 1 for stage 5, 2 for the others (which also name the city and its mayor). Phase 0 loads the sound bank and writes the text into a box of 17 by 20, 1 starts the music once the sound driver is idle, 2 scrolls the box up (scroll grows by 2 a frame; on stage 6 by 1, and by 2 every seventh frame) until Start or Cross is pressed (not on stage 6) or the scroll has gone far enough, then the fade takes the picture out, the music is stopped, the box and the picture are deleted and the phase restarts with the step counted on |
| `0x001524E0` | `int mm_StageScene_Update(StageScene *scene)` | 884 | yes | stage transition scene: title cards, approval, briefing, mission movie, ending roll (END_ROLL.TEX) |
| `0x00152860` | `StageScene *mm_StageScene_dtor(StageScene *scene, int16_t deleteFlag)` | 172 | yes | vtables 0x1BCF10/0x1BCF18; deletes camera, stage object, lights |
| `0x00152910` | `StageScene *mm_StageScene_ctor(StageScene *scene, int loadStage)` | 300 | yes | stage object, camera, light set; loads the stage |
| `0x0016E270` | `int mm_Title_DrawCommon(TitleScene *scene, int layers)` | 456 | no | title screen layers, logo animation and fades |
| `0x0016E440` | `int mm_Title_LoadMenu(TitleScene *scene)` | 1024 | yes | load game screen: memory card slots and their info |
| `0x0016E840` | `int mm_Title_PressStart(TitleScene *scene)` | 260 | no | title idle screen ("[SELECT]SoundTest") |
| `0x0016E950` | `int mm_TitleScene_StartGame(TitleScene *scene, int loaded)` | 764 | yes | (scene, loaded) the start of a game, one step a call (pressStep) while the city's asset loader is stepped: the title faded and drawn, the camera placed, the loading bar shown until the loader is done, the loading screen faded, the title music stopped; then the first time since start-up (afterGame clear) it loads the character types, families, claims, words, goods, stamina, bag and event types, after a game (afterGame set) it resets the stage, families, stamina and bag, and the laptop entries are made; `loaded` is 1 when the load menu chose a save; 1 for a new game, 2 for a loaded one, once done, else 0 |
| `0x0016EC50` | `void mm_Title_NewGameIntro(TitleScene *scene)` | 1300 | no | new game intro scene with town models and loading text |
| `0x0016F170` | `int mm_Title_RandomCharParts(TitleScene *scene)` | 248 | no | random character parts for the intro |
| `0x0016F270` | `int mm_Title_OpeningMovieStep(TitleScene *scene)` | 424 | yes | plays the opening movie, then loads TITLE.SQ/HD/BD |
| `0x0016F420` | `int mm_Title_MenuInput(TitleScene *scene)` | 520 | no | title menu input: cursor, Cross/Start pulse, and the sound test code (Up Up Down Down Left Right Left Right L1 R1, table 0x1B1790) that shows the Select hint; 10 opens the sound test |
| `0x0016F630` | `int mm_Title_DrawChoices(TitleScene *scene)` | 488 | no | the title menu choices: the selected one pulses for 20 frames (then returns 1), the other at 0.6 |
| `0x0016F820` | `void mm_Title_DrawChoicesIdle(TitleScene *scene)` | 640 | no | the title menu choices at rest: the selected one wobbles on two circles (sceVu0RotMatrixZ), the other at 0.6 (was mm_Title_LogoAnimB) |
| `0x0016FAA0` | `int mm_Title_Intro(TitleScene *scene)` | 696 | no | title logo intro, six steps: music, logo model animation with timed effects (0x1B1A20), title sprite fade-in, bouncing letters, 50-frame hold; returns 1 when done |
| `0x0016FD60` | `int mm_Title_LogoLetters(TitleScene *scene)` | 436 | no | logo letter sprites (table 0x1B17E0) |
| `0x0016FF20` | `int mm_TitleScene_MemoryCardCheck(TitleScene *scene, int noPolling)` | 944 | no | boot memory card check: goes on when a save or a fine card is found, else shows which slot has no card or too little space and waits for Cross (card states at 0x1BDC04/08) |
| `0x001704A0` | `int mm_TitleScene_Load(TitleScene *scene, int unused_a1)` | 1852 | yes | (scene, unused_a1) the title scene's assets, one step a call (waitFrames): models, texture images, sprites, the loading screen, then the sound banks once the sounds are idle; 1 when done |
| `0x00170BE0` | `int mm_TitleScene_Update(TitleScene *scene)` | 1004 | yes | (scene) one frame of the title scene by its mode: the assets loaded, the memory card check, the three logos, the intro, the title screen (new game, load, opening movie, sound test), the load menu, then the start of a new or loaded game (mm_TitleScene_StartGame); 1 or 2 once a game starts, else 0 |
| `0x00171220` | `TitleScene *mm_TitleScene_ctor(TitleScene *scene, int afterGame)` | 640 | yes | (scene, afterGame) the title scene: its stage object, the menu list, the memory card manager, the lights and the camera, a message box and a hint line; `afterGame` (1 when the game state was 1) stops the opening sequences (mm_TitleScene_Load) |
