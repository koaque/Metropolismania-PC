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
| N4 | The 13 VU1 programs as native code and shaders, compared image by image with the emulated output | |
| N5 | Native-only mode (the emulated VU1 and GS drawing skipped), the game state checked unchanged with `PS2X_TIMER0=frame` | |
| N6 | 4K and MSAA, filtering, widescreen, frame pacing; then PC controls | |

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

* `native`: the native picture only; `side`: either picture, F2 switches; `split`: the emulated picture on the left and the
  native one on the right. `MMPC_RENDER_SCALE` (1 to 8, default 3) sets the internal resolution as a multiple of 640x448.
  `MMPC_RENDER_STATS=1` prints what a frame held every 120 frames (batches, new textures, VU1 programs it ran).
* A runtime hook (`PS2Memory::setDmaStartObserver`) hands the renderer every VIF1 and GIF DMA start. The renderer walks the
  chain itself, sends the DIRECT payloads to its GS state machine (`render_gs.cpp`) and the image transfers to its own copy
  of GS memory (`render_vram.cpp`). A frame ends when the game starts its frame list (`kDmaLists`); while no frame list
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
  frame at the same place.
* Not drawn yet, all VU1 model draws (N4): the 3D city, people and vehicles, the title logo, the HUD clock and phone, the
  meter at the bottom, the laptop, the book and most window panels.
