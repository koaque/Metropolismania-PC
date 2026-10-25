# scene

The scenes before the city. Scene A (`mm_SceneA_*`): logos, memory card check, title screen and its menus
(`mm_Title_*`: logo animation, press start, new game intro, opening movie). Scene B (`mm_SceneB_*`): the stage
introduction (city name, stage title, approval rate) and the mission briefing telop. Scene C is the city (`city`).

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

25 functions (ported 1, typed 4, verified 20). Blocks: 15 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001515B0` | `void mm_SceneB_ShowCityName(SceneB *scene)` | 260 | yes | (provisional) title card with the city name (0x1C5680) |
| `0x001516C0` | `void mm_SceneB_ShowStageTitle(SceneB *scene)` | 256 | yes | "STAGE n" title card |
| `0x001517C0` | `void mm_SceneB_ShowApprovalRate(SceneB *scene)` | 596 | yes | "Your Approval Rate" screen |
| `0x00151A20` | `void mm_SceneB_MissionBriefing(SceneB *scene)` | 1064 | yes | the boss explains the mission in the message window |
| `0x00151E50` | `int mm_SceneB_BuildBriefingText(SceneB *scene)` | 452 | yes | "Let me explain Mission %d to you." + the goal text + "That's all. Good luck to you!" |
| `0x00152020` | `void mm_SceneB_ShowTelop(SceneB *scene)` | 1204 | yes | (provisional) TELOP%d text screens with music ("Thanks to your Mr. ...") |
| `0x001524E0` | `int mm_SceneB_Update(SceneB *scene)` | 884 | yes | stage transition scene: title cards, approval, briefing, mission movie, ending roll (END_ROLL.TEX) |
| `0x00152860` | `SceneB *mm_SceneB_dtor(SceneB *scene, int16_t deleteFlag)` | 172 | yes | vtables 0x1BCF10/0x1BCF18; deletes camera, stage object, lights |
| `0x00152910` | `SceneB *mm_SceneB_ctor(SceneB *scene, int loadStage)` | 300 | yes | stage object, camera, light set; loads the stage |
| `0x0016E270` | `int mm_Title_DrawCommon(SceneA *scene, int layers)` | 456 | no | title screen layers, logo animation and fades |
| `0x0016E440` | `int mm_Title_LoadMenu(SceneA *scene)` | 1024 | yes | load game screen: memory card slots and their info |
| `0x0016E840` | `int mm_Title_PressStart(SceneA *scene)` | 260 | no | title idle screen ("[SELECT]SoundTest") |
| `0x0016E950` | `int mm_SceneA_StartGame(SceneA *scene, int newGame)` | 764 | yes |  |
| `0x0016EC50` | `void mm_Title_NewGameIntro(SceneA *scene)` | 1300 | no | new game intro scene with town models and loading text |
| `0x0016F170` | `int mm_Title_RandomCharParts(SceneA *scene)` | 248 | no | random character parts for the intro |
| `0x0016F270` | `int mm_Title_OpeningMovieStep(SceneA *scene)` | 424 | yes | plays the opening movie, then loads TITLE.SQ/HD/BD |
| `0x0016F420` | `int mm_Title_MenuInput(SceneA *scene)` | 520 | no | title menu input: cursor, Cross/Start pulse, and the sound test code (Up Up Down Down Left Right Left Right L1 R1, table 0x1B1790) that shows the Select hint; 10 opens the sound test |
| `0x0016F630` | `int mm_Title_DrawChoices(SceneA *scene)` | 488 | no | the title menu choices: the selected one pulses for 20 frames (then returns 1), the other at 0.6 |
| `0x0016F820` | `void mm_Title_DrawChoicesIdle(SceneA *scene)` | 640 | no | the title menu choices at rest: the selected one wobbles on two circles (sceVu0RotMatrixZ), the other at 0.6 (was mm_Title_LogoAnimB) |
| `0x0016FAA0` | `int mm_Title_Intro(SceneA *scene)` | 696 | no | title logo intro, six steps: music, logo model animation with timed effects (0x1B1A20), title sprite fade-in, bouncing letters, 50-frame hold; returns 1 when done |
| `0x0016FD60` | `int mm_Title_LogoLetters(SceneA *scene)` | 436 | no | logo letter sprites (table 0x1B17E0) |
| `0x0016FF20` | `int mm_SceneA_MemoryCardCheck(SceneA *scene, int noPolling)` | 944 | no | boot memory card check: goes on when a save or a fine card is found, else shows which slot has no card or too little space and waits for Cross (card states at 0x1BDC04/08) |
| `0x001704A0` | `int mm_SceneA_Load(SceneA *scene, int a1)` | 1852 | yes |  |
| `0x00170BE0` | `int mm_SceneA_Update(SceneA *scene)` | 1004 | yes |  |
| `0x00171220` | `SceneA *mm_SceneA_ctor(SceneA *scene, int a1)` | 640 | yes |  |
