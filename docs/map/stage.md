# stage

Stages from `DATA\BIN\STAGE.BIN` (the mission list) and the stage object that holds the current stage.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

8 functions (verified 8). Blocks: 4 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00135A70` | `void mm_Stage_Load(void)` | 328 | yes | reads this stage's record from DATA\BIN\STAGE.BIN into 0x1C5380 (target population 0x1C554C, theme 0x1C5550, theme share 0x1C5554) |
| `0x00135BC0` | `void mm_Stage_FormatGoalText(int stage, char *out)` | 308 | yes | "%d people and %d%% must be the theme. Build a ..." with the theme text (high-class, business, leisure, fishing, farming, industrial) |
| `0x00135D00` | `void mm_Stage_FormatTargetText(int stage, char *out)` | 160 | yes | "Target\nBuild %s with more than %d people.\n%d%% of the buildings must be %s." |
| `0x00135DA0` | `void mm_Stage_LoadState(int stage, McManager *mc)` | 24 | no | reads the stage state (0x284 bytes at 0x1C5380) from the save file |
| `0x00135DC0` | `void mm_Stage_SaveState(int stage, McManager *mc)` | 24 | no | writes the stage state |
| `0x00135DE0` | `void mm_Stage_Reset(void)` | 168 | no | defaults: no stage, 3000 and 2500, cleared tables |
| `0x00135E90` | `int mm_Stage_DeletingDtor(int stage, int16_t deleteFlag)` | 68 | yes | (this, deleteFlag) an empty Stage has nothing to destroy: deletes the object when the flag is above 0 |
| `0x00135EE0` | `int mm_Stage_ctor(int stage)` | 16 | no | (this) the empty Stage base of the title, stage and city scenes and a member of the applicants window (+0x289); its data is the static stage state (0x1C5380), and making one clears its first byte, the city's name (no city: the save title then takes the default) |
