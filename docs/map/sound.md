# sound

Sound on the EE side; the driver is YYSOUND on the IOP.

- `mm_SoundCmd_*`: the YYSound command ring: 256 entries of 0xD0 bytes at `0x001FE6C0`, write index `0x001BDD44`,
  read index `0x001BDD48`, flushed to the IOP over SIF RPC by `mm_SoundCmd_Flush`, which also builds the
  `cdrom0:\DATA\SOUND\{,HD,BD,SQ}\` paths.
- `mm_Se_*`: sound effects. Voice group A has 16 effect voices (stop is command 4); group B has voices 0-15, with 15
  used by the BGM (stop is command `0xD`).
- `mm_Bgm_*`: background music. `mm_Sound_*`: module loading (`LIBSD`, `SDRDRV`, `MODHSYN`, `MODMIDI`, `MODSEIN`,
  `YYSOUND`), RPC binding and the per-city sound setup.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

57 functions (ported 6, typed 5, verified 46). Blocks: 4 can reach a thread switch, 1 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001876B0` | `void mm_Sound_StopAllPlaying(void)` | 156 | no | asks the driver for its status (0x18F200) and stops every voice whose bit is set in the two 16-bit masks at +4 and +8 |
| `0x00187750` | `void mm_Se_SetEnabled(int enabled, int resetVolumes)` | 196 | no | 0x1BDD20 = a0; a0 == 0 stops all 16 + 15 effect voices; a1 != 0 resets their volumes (0xFFF and 0x100) |
| `0x00187820` | `int mm_Se_SetVoice15Enabled(int enabled, int a1)` | 76 | no | same as mm_Se_SetEnabled for the single voice 15 (volume 0xC0) |
| `0x00187870` | `void mm_Se_UpdateCityAmbience(int advance)` | 3152 | no | one quarter of the 2x2 blocks within 16 tiles of the listener per frame: building sounds (group 2), terrain sounds (group 3) and, within 8 tiles, vehicles; VU0 bearings from the listener; 17:00 chime 0x3B for busy cities |
| `0x001884E0` | `int mm_Se_UpdateListener(void)` | 1092 | no | copies the city camera's eye and target (camera +0x130/+0x140) into the listener vectors at 0x1FC650 and derives its orientation |
| `0x00188930` | `uint32_t mm_Se_BuildingSoundState(MapObject *obj, BuildingType *btype)` | 1144 | indirect | a building object's ambient sound this frame: low byte the sound (0xFF none), 0x100 after the lead of its window, 0x200 entrance, 0x400 the type's own (+0x14E); each object once per frame (flag 4, again 0x4FF); the sound's schedule (SeSound +0x02) decides |
| `0x00188DB0` | `int mm_Se_InTimeWindow(MapObject *obj, uint32_t start, int length)` | 300 | no | checks the game clock (seconds of day mod 3600, 0x1BDD34) against a window [a1, a2) offset per object; 0xFF = outside, 0x100 = inside |
| `0x00188EE0` | `int mm_Se_RequestTileSounds(int x, int z, int horn, float angle, float distance)` | 468 | no | the vehicles on map tile (x, z) unless its object already has a request: engine 0x18 pitched by speed; the horn 0x1A at one-in-four while still allowed; returns what is left of the horn flag |
| `0x001890C0` | `uint32_t mm_Se_PickTerrainAmbience(int x, int z, int a2)` | 508 | no | ambient sound id for a position: above the city's level threshold (+0xB0) picks 0x1C/0x1D, else from the 5-byte table at 0x1B35F0 through mm_Se_MatchSchedule |
| `0x001892D0` | `int mm_Se_MatchSchedule(uint32_t monthFrom, uint32_t monthTo, uint32_t hourFrom, uint32_t hourTo, uint32_t length, uint32_t start10, int lead)` | 912 | no | 1 when the current month (city +0x88 mod 12) and hour/minute (0x1BDD34) fall inside the given month and time ranges |
| `0x00189660` | `int mm_Se_Request(uint16_t owner, uint32_t group, uint8_t sound, uint8_t param0A, uint8_t param18, uint16_t param0C, uint8_t param0B, uint8_t param19, float angle, float distance)` | 1244 | no | adds a sound request (position, sound id, priority, volume, pitch, ...) to the request list at 0x1F8710 (count at gp 0x1BDD08); ids look up the 16-byte sound table 0x1B2EE0 |
| `0x00189B40` | `void mm_Se_StopOwner(char owner)` | 260 | no | stops every effect voice (16 at 0x1F8140, 15 at 0x1F8440, 0x30 bytes each) and request owned by a0 |
| `0x00189C50` | `void mm_Se_RequestForObject(uint32_t a0, uint32_t a1, uint32_t a2, int a3, int t0, int t1)` | 1876 | no | requests the sounds of a moving or placed object by its type and distance to the listener |
| `0x0018A3B0` | `int mm_Sound_PlaySe(int sound, int a1, int a2, float angle, float distance)` | 148 | no | plays sound id a0 & 0xFF through 0x189660; ids flagged 0x10 in the 16-byte table 0x1B2EE0 get a rotating handle (0x1BDCFC, wraps at 255) |
| `0x0018A450` | `void mm_Se_PlayMessageSound(char code)` | 1456 | no | message window sound for code a0 (0x14, 0x0A, ...), several fixed sound ids |
| `0x0018AA00` | `void mm_Se_PlayVariant(uint8_t variant)` | 1148 | no | plays one of seven effects (0x29, 0x2A, 0x2B, 0x2D with a random pitch 0..199 from mm_Random_TimerMix; 0x3E, 0x2C, 0x2E) |
| `0x0018AE80` | `void mm_Se_UpdateTimeOfDay(void)` | 452 | no | time-of-day cues: fades sound 0x36 by the clock (silent 6:00-18:00), chime 0x37 at fixed times in months 7-8 |
| `0x0018B050` | `void mm_Se_UpdateVoices(void)` | 2736 | no | per frame, by priority class: one step of each busy voice's sound script (0x1B3410, 0x18 bytes by voiceParam low byte): start, wait, branch at random, pitch slides, volume offset, stop |
| `0x0018BB10` | `void mm_Se_ApplyVoiceParams(uint32_t voice)` | 188 | no | voice a0: volume L/R clamped to 1..0x3FFF (flag 1, 0x18DD90) and pitch (flag 2, 0x18DC70) |
| `0x0018BBD0` | `void mm_Se_AssignVoices(void)` | 2952 | no | sorts the frame's requests by priority into the 16 + 15 effect voice slots, stealing low-priority voices |
| `0x0018C770` | `void mm_Sound_Update(void)` | 580 | no | per frame from mm_GameUpdate: assigns and updates effect voices, city ambience, time-of-day cues and the BGM state |
| `0x0018C9C0` | `void mm_Sound_SetCity(CityScene *city)` | 516 | no | sets the city object (0x1BDD00) used by the sound code; null stops all effect voices and resets their volumes |
| `0x0018CBD0` | `int mm_Sound_StopAllSequences(void)` | 60 | no | queues cmd 0xB for sequence slots 7..0 |
| `0x0018CC10` | `void mm_Sound_LoadPlaylandBanks(void)` | 88 | no | queues cmd 9 (load SQ/HD/BD bank) for the 4 bank name triples at 0x1B2E20 (PLAYLAND.SQ ...) |
| `0x0018CC70` | `void mm_Bgm_FadeOut(int time)` | 48 | no | cmd 0xE on voice 15 to volume 0 over a0, BGM state (gp 0x1BDCF4?) = 4 |
| `0x0018CCA0` | `int mm_Bgm_SetActive(int active)` | 132 | no | a0 == 0 fades the BGM out (state 4); otherwise stops a pending jingle, restores volume 0xC0 and sets state 3 |
| `0x0018CD30` | `void mm_Bgm_SetTrackSlot(char slot, int play, int enable)` | 360 | no | sets or adds track slot + 6 in the music table (0x1F8120, entries 3..12 of (id, enabled)); with play: fades voice 15 out with it next, or queues it (0x1BDCF0) |
| `0x0018CEA0` | `void mm_Bgm_Update(void)` | 688 | no | picks the BGM for the city state (flags +0xB4, +0x8C) and switches banks (NOMAL_A.SQ/HD/BD ...) when the driver is idle |
| `0x0018D150` | `int mm_SoundCmd_02_Zero(void)` | 12 | no | queues cmd 2 with zero arguments |
| `0x0018D160` | `void mm_Sound_ResetVoices(int bankSet)` | 284 | no | queues cmd 1, resets all voice volumes (0xFFF group A, 0x100 group B, 0xC0 voice 15) and clears the effect voice tables |
| `0x0018D280` | `void mm_Se_BuildAmbienceMap(void)` | 348 | no | clears the 4096-entry ambience grid at 0x1FC690 and fills it from the city map and the table at 0x1B35F0 |
| `0x0018D3E0` | `void mm_Sound_InitState(void)` | 196 | no | called by mm_GameInit: marks all 31 effect voices free (0xFF) and clears the sound globals |
| `0x0018D4B0` | `void mm_SoundCmd_CancelPlayB(char voice)` | 256 | no | drops the unsent PlayB commands (3) from the ring and closes the gap; matches the voice against byte 3 of the command word (always 0), so called with 0 it drops every queued PlayB |
| `0x0018D5B0` | `int mm_Sound_IsBusy(void)` | 164 | no | refreshes the status copy (0x20B7C0 -> 0x20B840, merged with pending masks); true if the driver is busy or commands are queued |
| `0x0018D660` | `int mm_Sound_IsDriverBusy(void)` | 128 | no | like mm_Sound_IsBusy but ignores queued commands |
| `0x0018D6E0` | `int mm_SoundCmd_SetDiscBusy(uint32_t busy)` | 100 | no | queues cmd 0x18 with a0 & 0xFF; the file code sends 1 before waiting on the disc and 0 after |
| `0x0018D750` | `int mm_SoundCmd_11(uint8_t voice, uint16_t a1, uint16_t a2)` | 140 | no | queues cmd 0x11 (voice a0, a1, a2) |
| `0x0018D7E0` | `int mm_SoundCmd_SetVolumeB(uint8_t voice, int volume, uint16_t a2)` | 140 | no | queues cmd 0x10 (voice a0, volume a1, a2) |
| `0x0018D870` | `int mm_SoundCmd_0F(uint8_t voice, int a1, uint16_t a2)` | 140 | no | queues cmd 0xF (voice a0, a1, a2), same layout as cmd 0x10 |
| `0x0018D900` | `int mm_SoundCmd_FadeB(uint8_t voice, uint16_t target, uint16_t time, uint16_t a3)` | 144 | no | queues cmd 0xE (voice a0, target a1, time a2, a3) |
| `0x0018D990` | `int mm_SoundCmd_StopB(uint32_t voice, uint32_t a1)` | 168 | no | queues cmd 0xD (voice a0, a1) |
| `0x0018DA40` | `int mm_SoundCmd_PlaySequence(uint32_t slot, uint8_t a1, uint8_t a2, int a3, uint16_t t0)` | 168 | no | queues cmd 0xC (slot a0, a1, a2, a3, a4) and sets the slot's bit in the pending mask (gp) |
| `0x0018DAF0` | `int mm_SoundCmd_StopSequence(uint32_t slot, uint32_t a1)` | 168 | no | queues cmd 0xB (slot a0, a1) |
| `0x0018DBA0` | `int mm_SoundCmd_LoadBank(uint32_t slot, int sqName, int hdName, int bdName, uint32_t flags)` | 200 | no | queues cmd 9: slot a0, SQ/HD/BD file names a1/a2/a3, flags a4 |
| `0x0018DC70` | `int mm_SoundCmd_SetPitchA(uint8_t voice, uint16_t pitch, uint16_t a2)` | 140 | no | queues cmd 8 (voice a0, pitch a1, a2) |
| `0x0018DD00` | `int mm_SoundCmd_SetVolumeA(uint8_t voice, int volume, uint16_t a2)` | 140 | no | queues cmd 7 (voice a0, volume a1, a2) |
| `0x0018DD90` | `int mm_SoundCmd_SetVolumeLRA(uint8_t voice, int left, int right, uint16_t a3)` | 144 | no | queues cmd 6 (voice a0, left a1, right a2, a3) |
| `0x0018DE20` | `int mm_SoundCmd_PlayA(uint8_t voice, uint16_t a1, uint16_t a2, uint16_t a3, uint16_t t0, uint16_t t1)` | 152 | no | queues cmd 5 (voice a0 and five sound parameters) |
| `0x0018DEC0` | `int mm_SoundCmd_StopA(uint32_t voice, uint32_t a1)` | 168 | no | queues cmd 4 (voice a0, a1) |
| `0x0018DF70` | `int mm_SoundCmd_PlayB(uint32_t voice, uint8_t bank, uint16_t sound, uint8_t a3, uint8_t t0, uint8_t t1, int t2, uint16_t t3)` | 180 | no | queues cmd 3 (voice a0, bank a1, sound a2, ...) and sets the voice's bit in the pending mask (gp) |
| `0x0018E030` | `int mm_SoundCmd_02(uint32_t a0, uint32_t a1)` | 168 | no | queues cmd 2 (a0, a1) |
| `0x0018E0E0` | `int mm_SoundCmd_01(uint32_t a0, uint32_t a1, int a2, int a3, uint32_t t0)` | 204 | no | queues cmd 1 (a0, a1, a2, a3, a4) |
| `0x0018E1B0` | `void mm_SoundCmd_Flush(void)` | 4164 | yes | (was mm_SetSoundDataPaths) sends the queued commands to YYSOUND over SIF RPC 0x12346 (paths cdrom0:\DATA\SOUND\SQ\|HD\|BD\), drops commands when too many are pending ("URGENT CUT", "ManyRequest") and refreshes the status block |
| `0x0018F200` | `int mm_Sound_GetStatus(void)` | 124 | no | refreshes the status copy at 0x20B840 (merged with the pending masks) and returns it |
| `0x0018F280` | `void mm_Sound_LoadModules(void)` | 452 | yes | (was mm_LoadSoundModules) loads LIBSD, SDRDRV, MODMIDI, MODHSYN, MODSEIN, YYSOUND, retrying each until it loads |
| `0x0018F450` | `void mm_Sound_Shutdown(void)` | 80 | yes | YYSound RPC function 0 |
| `0x0018F4A0` | `void mm_Sound_Init(void)` | 300 | yes | (was mm_InitSound) loads the modules, binds RPC 0x12346, sends function 1 with the status block address, clears the command ring |
