# boot

Scene A's logo sequence (`mm_SceneA_LogoStep`: NATSUME, MF_US and LOGO.TEX, each loaded, faded in, held for
140 frames and faded out) and its destructor. The rest of scene A is in `scene`.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

2 functions (typed 2). Blocks: 2 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001702D0` | `int mm_SceneA_LogoStep(SceneA *scene, char logo)` | 464 | yes | one logo (a1: 0 NATSUME, 1 MF_US, else LOGO.TEX): load, fade in, hold 140 frames, fade out, free |
| `0x00170FD0` | `SceneA *mm_SceneA_dtor(SceneA *scene, int16_t deleteFlag)` | 588 | yes | deletes camera, models, menus, memory card manager, loading screen and text boxes |
