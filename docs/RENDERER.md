# The native renderer (plan N)

The game draws through the PS2's graphics hardware: the CPU code builds one DMA chain per frame, VIF1 unpacks it into
VU1 memory, 13 VU1 microcode programs turn model and terrain data into GS primitives, and the GS rasterizes them into two
640x224 field buffers. Today the runtime emulates all of that (the VU1 programs compiled ahead of time, the GS in
software) and shows the fields, woven, in a window. Plan N replaces the VU1 programs and the GS with native drawing: the
renderer reads the chain the game already builds and draws its geometry on the GPU, in model space with the game's own
camera, light and fog data, at any resolution, one progressive frame per game frame. The game code is not changed; the
emulated path stays available for the faithful build.

This is how the best ports of this kind work: OpenGOAL (the native Jak and Daxter ports, also PS2 games) gives each VU1
program of the game a native renderer fed from the DMA chain, and the N64 recompiled ports render through RT64, which does
the same for the N64's microcode. The render path itself is mapped in [RENDER_PIPELINE.md](RENDER_PIPELINE.md).

## Graphics API

SDL3's GPU API (SDL 3.4, linked statically): one renderer on Direct3D 12 or Vulkan on Windows (`PS2X_GPU=d3d12|vulkan`;
SDL's choice by default) and Metal on macOS, with shaders written in HLSL. It was chosen over Direct3D 11 or 12 alone
(Windows only, so Linux and macOS would need a second renderer), raw Vulkan (the same reach as SDL's Vulkan backend with far
more code), and OpenGL (simple to add, but deprecated on macOS). SDL3 also gives the window, keyboard and mouse, gamepads
and audio, so it replaced raylib as the runtime's desktop host (`patches/0155`).

## Phases

| Phase | What | State |
|---|---|---|
| N0 | Deterministic timer (`PS2X_TIMER0=frame`, `patches/0154`) and `tools/lockstep_compare.py`: the game takes timer 0's count as a random number, so this keeps a change in drawing cost from changing the simulation, and lets two runs be compared state for state | Done |
| N1 | SDL3 host instead of raylib (`patches/0155`) | Done |
| N2 | `tools/draw_inventory.py`: what the frames ask of the hardware (below) | Done for the city, new game and rain |
| N3 | Renderer core: chain walker, GS state, texture cache, 2D primitives, progressive output, resolution scale (`src/render/`, below) | Done: everything drawn without VU1 |
| N4 | The 13 VU1 programs as native code and shaders, compared image by image with the emulated output | All 13 native, checked against captured runs (below); their math still on the CPU |
| N5 | Native-only mode (the emulated VU1 and GS drawing skipped), the game state checked unchanged with `PS2X_TIMER0=frame` | Done (below); the faithful build's default is still the emulated picture |
| N6 | 4K and MSAA, filtering, widescreen, frame pacing; then PC controls | Started: resolution from the window, MSAA, fullscreen, widescreen (below) |

## What the frames use (N2)

From 180 frames captured with the original code (a new game from the title, an explore run in a snowy city, and the rain
and placement script), walked by `tools/draw_inventory.py`:

* **Programs.** Eleven of the 13 VU1 programs run: the two model programs for buildings and objects and for people,
  the player and menus (most of the work: in a city frame about 80 building instances in about 1,100 batches and 160
  people or menu instances), the four terrain passes (ground, cliff sides, water bottom, lit roads), the water, and the
  rain, snow and their ground passes. Only the placement highlight and the unlit model program were not reached.
* **Render targets.** The frame lists never change the frame buffer, scissor or Z buffer: everything is drawn into the two
  field buffers. One case reads the frame back: while a talk overlay is up, the city draws the other field buffer over the
  frame at FIX 0x50 (62.5%), as 35 sprites of 128x32 that each read one tile of it (`mm_DmaList_QueueOtherOverlay`).
* **GS state.** A small set. Blending: the usual alpha blend, additive, and a constant-alpha fade (FIX 30, 80 or 100).
  Tests: alpha test "not 0" with the depth test "greater or equal", Z-only writes (the Z clear), and "always". Filtering:
  bilinear or nearest, no mipmaps. Texture wrap: repeat, clamp and region clamp.
* **Textures.** Almost all 4-bit and 8-bit indexed textures with 32-bit palettes, modulated by the vertex colour; a few
  32-bit ones. About 70 image uploads a frame: palettes, 12x22 font glyphs (text is uploaded glyph by glyph), and some
  larger images.
* **2D.** Drawn directly (no VU1 program): mostly sprites, textured and blended or not, a few strips and points.
* **VU1 data.** UNPACK formats V4-32, V4-16, V4-8 and masked V2-32.
* **Not in the frame lists.** Movies send their pictures by their own DMA transfers, from the player's vertical blank
  handler: each field uploads the decoded picture as a texture (1,120 macroblocks) and draws it as one sprite, then the
  fade and the subtitles.

The records and the inventory's output are game-derived and stay in `work/`.

## The renderer (N3)

`src/render/` (staged into the runner like `src/port/`; switched on with `MMPC_RENDERER`):

* `native`: the native picture only, with the emulated VU1 and GS drawing nothing (below; `MMPC_RENDER_EMULATED=1` keeps
  them drawing); `side`: either picture, F2 switches; `split`: the emulated picture on the left and the native one on the
  right. The internal resolution is a multiple of 640x448 chosen from the window (enough that the picture is drawn at
  least as tall as it is shown; 2x at least, 8x at most; made again when the window changes); `MMPC_RENDER_SCALE` (1 to
  8) fixes it. `MMPC_RENDER_MSAA=2|4|8` draws with that many samples a pixel (the multisampled targets keep their
  contents like the others and are resolved into the ones shown and read back). F11 switches borderless fullscreen and
  `PS2X_FULLSCREEN=1` starts in it (`patches/0158`). `PS2X_WINDOW_FOCUS=1` lets a placed window (test runs) take the focus;
  in the background the game keeps its speed, since the host asks Windows for a 1 ms timer and opts out of power
  throttling (`patches/0159`; `PS2X_POWER_THROTTLING=1` keeps Windows' default).
  Frames go out right after the game's vertical blank, so on a variable refresh display (FreeSync, G-Sync) each is shown
  16.68 ms after the one before. The vertical blanks themselves are timed to about a quarter of a millisecond: the
  scheduler's timed waits end 1.5 ms early and yield the rest away (`patches/0160`; in the city the gaps between presents
  went from 14.5 to 18.9 ms, deviation 0.7 to 0.9, to 15.8 to 17.6 ms, deviation 0.16 to 0.28; `PS2X_PRECISE_WAIT=0` keeps
  the plain wait). `MMPC_FRAME_PACING=1` prints those gaps every 2 s, and each gap over 22.7 ms with the number of
  vertical blanks in it: one means the game's own vertical blank came late (a game frame took about 25 ms; about 3 in
  800 city frames under the explore autoplay), more means a present was missed. On a fixed refresh display at another rate than
  59.94 Hz (144 Hz, say) each frame stays up for a whole number of refreshes, 2 or 3 at 144 Hz.
  `MMPC_RENDER_STATS=1` prints what a frame held every 120 frames (batches, new textures, VU1 programs it ran, program runs
  without a native version, the time per frame on the game thread and on the render thread, frames drawn).
* A runtime hook (`PS2Memory::setDmaStartObserver`) hands the renderer every VIF1 and GIF DMA start. On the game thread the
  renderer only copies the transfer's data out of RDRAM (walking the chain) with the GS's drawing environment at that
  moment; a render thread then takes the transfers in order, sends DIRECT payloads to its GS state machine
  (`render_gs.cpp`), image transfers to its own copy of GS memory (`render_vram.cpp`) and VU1 data to the native programs.
  The game thread waits only when two frames are still queued. A frame ends when the game starts its frame list (`kDmaLists`); while no frame list
  runs (movies), at the first transfer after a vertical blank.
* Textures are decoded from that memory into RGBA8 and kept by content: a texture whose pages were written but whose texels
  did not change keeps its GPU copy, so the city uploads no textures in a steady frame although the game uploads its
  glyphs and palettes again every frame.
* Output is progressive: the field's half-line offset is dropped and the 224-line field coordinates are doubled, so both
  fields' lines come from one draw and nothing is woven.
* Sampling follows the GS: wrap modes including region clamp and region repeat, its bilinear filter (half a texel back,
  four texels), texture coordinates taken half a 1x pixel back (the GS samples a pixel at its integer position, the GPU at
  its centre), and sprite corners rounded up to whole pixels as the GS fills them. One deliberate difference: a sprite drawn
  one texel to one frame pixel (the text, the HUD, the movie picture) takes each 1x pixel's texel whole at its centre, so it
  upscales without its neighbours bleeding into its edges. On the GS the game's text, drawn bilinear from texel edges, comes
  out as half of one texel and half of the next; here it stays sharp.
* A draw whose texture lies in a field buffer drawn in an earlier frame (the talk overlay above) reads the previous native
  frame at the same place. The two render targets keep their contents from one use to the next, as the GS's two field
  buffers do (the game counts on it: under a talk overlay it blends the other buffer over a frame it does not draw again),
  and share one Z buffer.

## The VU1 programs (N4)

The renderer keeps its own copy of VU1 data memory, filled from the VIF1 stream as the hardware fills it (`render_vif.cpp`:
UNPACK with the write cycle, mask and row modes, the double buffer's TOP and TOPS). The chain walker marks each DMA CALL of
a microcode upload packet, so the VIF knows which program the next MPG loads; MSCAL and MSCNT then run the native version
of that program, which hands its primitives to the renderer's GS as float vertices where the VU1 would have sent a
fixed-point GIF packet. The math runs on the CPU for now, in the programs' own order, so the output can be checked against
the real programs; moving it into shaders is a later step.

Each program is written from the microcode and checked with `MMPC_RENDER_VU1TEST=<folder>`, which replays captured runs
(`.vu1r`, from `pipeline.py vu1-capture`) through the native program and compares its packets with the ones the real
program sent: the vertex count and every ADC bit exactly, positions to a 16th of a pixel, colours to one step, texture
coordinates to float rounding.

| Program | Draws | Captured runs matching |
|---|---|---|
| 0x1A3C80 | people, the player, menus and windows, the title logo, the HUD's clock and phone, the talk scenes | 190 of 193 (165 bit for bit) |
| 0x1A4780 | buildings and objects, with its clipping mode (triangles that cross the clip volume clipped against five planes and sent as fans) | 103 of 103 (89 bit for bit) |
| 0x1A5EB0 | the base ground: per quad its TEX0, corners in strip order, two lit faces | 94 of 94 |
| 0x1AA340 | the water bottom: the ground pass with each height scaled and offset | 105 of 105 |
| 0x1A91C0 | the lit roads: the ground pass unlit, one colour | 60 of 60 |
| 0x1A76F0 | the cliff sides: triangles, texture t from the height, one lit face each | 83 of 83 |
| 0x1A6440 | the water: each corner's height a wave along z (the VU's ESIN), every quad drawn twice in two contexts | 48 of 48 |
| 0x1A8A00 | the rain: twelve streaks a quad, as lines, dropped by a hash of the quad's position | 8 of 8 |
| 0x1A8E20 | the rain's ground pass | 8 of 8 |
| 0x1A9A90 | the snow: flakes as sprites, turned and moved by tables a hash picks | 17 of 17 |
| 0x1A9F10 | the snowy ground: the ground pass untextured, lifted 0.01 | 14 of 14 |
| 0x1A7B30 | the placement highlight: each quad as a filled strip and a closed outline | 6 of 6 |
| 0x1A8330 | the unlit model program (the claim-met effect, the placement marker) | no captured run; written from the code |

The counts are over the six capture folders, in which some runs appear more than once; every match but the model
programs' is bit for bit, and theirs differ only within float rounding. The native programs round as the VU does
(toward zero, as the runtime's VU1); the rain and snow pick their tables from the low bits of a float's bit pattern, so
nothing less would do. The three runs that differ are one run of 0x1A3C80, a quirk of the game: that batch ran without
its own setup, on the VI registers another program had left (its ADC bits come out different through an IAND with 16
where the setup sets 1).

Things learned on the way: `ILW` reads only the low 16 bits of a word (the draw mode is stored as a float, whose low half
is 0, so those instances take the no-fog loop); in a captured batch the instance's matrix in data memory may already be
the next instance's (the VIF unpacks it while the last batch runs; the setup keeps what it needs at 0x30-0x32 for that
reason); the terrain passes leave a skipped quad's slots in their output buffer as they were (only its ADC bits are
written) and set ADC by storing 0x8000 over a vertex's whole w word, so its F goes to 0; and the rain's ground pass
multiplies its second and fourth corners' texture coordinates by the next corner's 1 / w, because its software pipelining
issues those multiplies after the next DIV has landed (kept, as the game draws it).

## Widescreen (N6)

`MMPC_WIDESCREEN=1` (16:9) or `W:H` (for example `21:9`) opens the city's view that much wider than 4:3 at the same height
(Hor+). It needs the native renderer, which shows the wider picture, and is off by default (`src/port/enhance.h`, the
port's first enhancement switch; with it off the port runs as before).

* In the game: the visibility test behind the city's draw lists and the snow's clip frustum widen by k = (W/H) / (4/3),
  4/3 for 16:9, so the sides are not left empty. The game's camera is not changed: its projection (screen scale and
  matrices) is part of what a save keeps, so a save made with widescreen on is the same as one made without.
* In the renderer: the city marks its frame list at the world camera's packet (`port::enhance::markFrameList`), and as that
  packet's camera block reaches the renderer's VU1 memory, the screen matrix and the clip matrix are narrowed in x by k
  about the screen centre, so every program (models, terrain, water, weather) draws the wider view into the 640 pixels
  (the effect of the PCSX2 widescreen patch, which changes the camera's x scale at 0x12A92C). A city frame drawn that way
  goes to render targets k times as wide and is shown at 4:3 times k. The HUD keeps its 4:3 proportions in the middle:
  the city marks where the HUD starts (at the HUD camera's packet), the chain walker turns each mark into a VIF NOP, and
  what is drawn from there on is drawn 1/k as wide around the centre, with its x 0 on a whole render pixel so text stays
  as sharp. 2D primitives across the whole width (the sky, fades, the Z clear) and frame-buffer reads still fill the
  width, and a HUD piece that covers the 4:3 screen (the pause window's frame) is marked as a backdrop: its points at or
  past the 4:3 edges go out to the wide edges.
* The centre of the world keeps its 4:3 scale (the middle of a 16:9 frame matches the 4:3 frame at the same render scale),
  and HUD pieces placed over the world by the game's own 4:3 projection (the cursor's arrow and info window) stay over what
  they belong to; talk scenes show their residents at their proportions and more of the room.
  Movies, the title and the scenes outside the city stay 4:3.
* A first version (fb0f304) changed the game's camera instead, once, as the city was built: loading a save restores the
  camera's projection, so after a load the city was the 4:3 view stretched to 16:9 (people and buildings a third too wide),
  and saving with it on would have written the changed projection into the save.
* The town differs from a 4:3 run: the game walks a pedestrian only while it draws them (each draw moves them on by their
  walk stride), so who walks depends on what is in view, as in the original when the camera turns. With
  `PS2X_TIMER0=frame`, a 4:3 and a 16:9 run under the explore autoplay are the same state up to the city and differ in
  pedestrians within its first 300 frames.
* Speed at 16:9: 59.9 frames a second in the city at 2x scale, as at 4:3 (152 of 162 two-second windows over a whole
  evening and the change of day; the lowest 58.0, at the change of day).
* Checked against 4:3 runs of the same scripts (the middle of each wide frame against the 4:3 frame): the eleven camera
  views at 16:9 and 21:9, the laptop and its town pages, notebook and bag, the pause menu, a talk scene, an evening into
  night and the next day, leaving the city (the fade covers the whole width; the title's screens are 4:3 again), and split
  mode (the emulated half 4:3, the native half wide).

## Native only (N5)

`MMPC_RENDERER=native` also turns on a frame skip that skips every frame (`patches/0157`, `ps2xFrameSkipSetAlways`): the
emulated VU1 runs no program and the emulated GS draws no primitive, while VIF unpacks, image transfers, CLUT loads and
GS register writes all still happen (patch 0145's machinery), and the vertical blanks keep their place in real time.

* The game state is unchanged by it: with the deterministic timer (`PS2X_TIMER0=frame`), 20 snapshots of a loaded city
  under the explore autoplay (GameUpdate every 300 frames, 6,000 frames) are byte for byte the same in RDRAM and the
  scratchpad with the native renderer alone and with the emulated picture (`tools/lockstep_compare.py`).
* Speed, explore autoplay in a snowy city: the game holds 59.5 to 60 frames a second and the native renderer draws 113 to
  120 of every 120 (the game thread spends about 0.5 to 1.3 ms of a frame in the renderer, the render thread about 10 ms).
  The emulated build in the same run keeps the game near its speed by frame skip but draws 28 to 50 frames in every 120,
  each taking 28 to 49 ms.
