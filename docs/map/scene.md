# scene

The scenes before the city. Scene A (`mm_SceneA_*`): logos, memory card check, title screen and its menus
(`mm_Title_*`: logo animation, press start, new game intro, opening movie). Scene B (`mm_SceneB_*`): the stage
introduction (city name, stage title, approval rate) and the mission briefing telop. Scene C is the city (`city`).

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

25 functions (named 25). Blocks: 15 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Name | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001515B0` | `mm_SceneB_ShowCityName` | 260 | yes | (provisional) title card with the city name (0x1C5680) |
| `0x001516C0` | `mm_SceneB_ShowStageTitle` | 256 | yes | "STAGE n" title card |
| `0x001517C0` | `mm_SceneB_ShowApprovalRate` | 596 | yes | "Your Approval Rate" screen |
| `0x00151A20` | `mm_SceneB_MissionBriefing` | 1064 | yes | the boss explains the mission in the message window |
| `0x00151E50` | `mm_SceneB_BuildBriefingText` | 452 | yes | "Let me explain Mission %d to you." + the goal text + "That's all. Good luck to you!" |
| `0x00152020` | `mm_SceneB_ShowTelop` | 1204 | yes | (provisional) TELOP%d text screens with music ("Thanks to your Mr. ...") |
| `0x001524E0` | `mm_SceneB_Update` | 884 | yes | stage transition scene: title cards, approval, briefing, mission movie, ending roll (END_ROLL.TEX) |
| `0x00152860` | `mm_SceneB_dtor` | 172 | yes | vtables 0x1BCF10/0x1BCF18; deletes camera, stage object, lights |
| `0x00152910` | `mm_SceneB_ctor` | 300 | yes | stage object, camera, light set; loads the stage |
| `0x0016E270` | `mm_Title_DrawCommon` | 456 | no | title screen layers, logo animation and fades |
| `0x0016E440` | `mm_Title_LoadMenu` | 1024 | yes | load game screen: memory card slots and their info |
| `0x0016E840` | `mm_Title_PressStart` | 260 | no | title idle screen ("[SELECT]SoundTest") |
| `0x0016E950` | `mm_SceneA_StartGame` | 764 | yes |  |
| `0x0016EC50` | `mm_Title_NewGameIntro` | 1300 | no | new game intro scene with town models and loading text |
| `0x0016F170` | `mm_Title_RandomCharParts` | 248 | no | random character parts for the intro |
| `0x0016F270` | `mm_Title_OpeningMovieStep` | 424 | yes | plays the opening movie, then loads TITLE.SQ/HD/BD |
| `0x0016F420` | `mm_Title_MenuInput` | 520 | no | title menu cursor and selection |
| `0x0016F630` | `mm_Title_LogoAnimA` | 488 | no | logo sprite animation |
| `0x0016F820` | `mm_Title_LogoAnimB` | 640 | no | rotating logo sprite animation |
| `0x0016FAA0` | `mm_Title_Intro` | 696 | no | title intro sequence with music |
| `0x0016FD60` | `mm_Title_LogoLetters` | 436 | no | logo letter sprites (table 0x1B17E0) |
| `0x0016FF20` | `mm_SceneA_MemoryCardCheck` | 944 | no |  |
| `0x001704A0` | `mm_SceneA_Load` | 1852 | yes |  |
| `0x00170BE0` | `mm_SceneA_Update` | 1004 | yes |  |
| `0x00171220` | `mm_SceneA_ctor` | 640 | yes |  |
