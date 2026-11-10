# The city render pipeline

How a city frame gets from the game's code to the GS: which functions queue what, which VU1 program draws it, what the program is given and what it sends to the GS. This is the map a native renderer would replace the VU1 and GS emulation with (plan R1).

Sources: the readable port (`src/port/gfx.cpp`, `map.cpp`, `layers.cpp`, `city.cpp`), the game's microcode read with `tools/vu1_inspect.py`, and captured VU1 runs: 105 from the Rhinecliff save with random play (`work/vu1_aot/records/`), and the scripted capture `tools/autoplay/render_capture.txt` (`pipeline.py vu1-capture --script ... --env MMPC_WEATHER=2`: rain, the camera at zoom level 0 against a building, a bag item being placed). `MMPC_WEATHER` holds the city's weather for a capture: the game only changes it at midnight (rain or snow the day after a month count that is 2 mod 5), and a new stage starts clear. Each program below says how it was checked:

* **recorded**: captured runs show its inputs and the GIF packets it sent.
* **ran**: the game ran it in a live city run (it has a compiled program in `work/vu1_aot/gen/`), but no run of it was captured.
* **read only**: known from its code and the packets that call it; not seen running yet.

`tools/vu1_inspect.py` reads the user's own ELF and captures: `packets` disassembles the 13 microcode packets into `work/vu1_dis/`, `records` summarizes every captured run (entry, loaded microcode, GIF output), `record FILE --trace` disassembles the instructions one run reaches. Its output is game-derived and stays in `work/`.

## Frame flow

1. The city scene's update (`mm_CityScene_*`, `src/port/city.cpp` around line 4325) queues everything for the frame on the current DMA list: each draw call appends a DMA CALL tag (`mm_Dma_AppendCall`) to a packet built once, at load time, in the packet buffer. Two 64 KB lists alternate by frame (`mm_DmaList_Flip`); objects that change every frame keep two copies of their packets and pick one by the frame parity at 0x1BDCD0.
2. `mm_DmaList_KickFrame` starts the list on the VIF1 DMA channel in chain mode. From there on the game's CPU code is done with the frame.
3. VIF1 walks the chain: GIF packets that set GS registers go straight through (DIRECT), data is unpacked into VU1 data memory (UNPACK), microcode is uploaded (MPG) and VU1 programs are started (MSCAL from address 0, MSCNT from where the last run stopped).
4. Each VU1 program run turns a batch of vertices into a GIF packet in VU1 memory and sends it to the GS (XGKICK). VU1 memory is double-buffered by the VIF (BASE and OFFSET per program), so VIF1 unpacks the next batch while VU1 works on the current one.

### City frame order

From the city scene's draw step, in the order the GS sees it:

| Call | What it queues | Program |
|---|---|---|
| `mm_DmaList_QueueClearZ` | full-screen sprite writing Z = 0 | none (GIF) |
| `mm_Sky_Draw` | the three sky packets in the city camera (+0x310, +0x470, +0x3D0) | none (GIF) |
| `mm_Packet_Draw(camera)` | the camera block to VU1 0x00-0x11 | none |
| `mm_Terrain_Draw` | fills the terrain quad and cliff-side buffers for this frame (CPU) | none yet |
| `mm_CityRender_DrawTerrain` | terrain passes: base ground, cliff sides, the water bottom (layer B), the lit roads (layer F) | 0x1A5EB0, 0x1A76F0, 0x1AA340, 0x1A91C0 |
| light set packet 1, `mm_CityRender_CollectObjects` | buildings and objects on visible tiles, as model instances | 0x1A4780 |
| light set packet 0, `mm_CityRender_UpdatePeople`, `mm_Player_Draw` | people and the player, as model instances | 0x1A3C80 |
| `mm_CityRender_DrawWater` | the water (layer G) | 0x1A6440 |
| `mm_CityRender_DrawWeather` (weather 2 or 3 only) | rain (layer D) or snow (layer E) | 0x1A8A00 + 0x1A8E20, or 0x1A9A90 + 0x1A9F10 |
| `mm_CityRender_DrawPlacement` x2 | the placement highlight and marker (layer C), only while placing | 0x1A7B30, 0x1A8330 |
| `mm_CityRender_DrawClaimMet` | the claim-met layer's instances (layer A) | 0x1A8330 |
| HUD, minimap, windows | sprites and text | none (GIF) |

Several of the names in that table are older guesses that this map corrects (see "Names to fix" at the end).

## The microcode

The game's VU1 microcode is 13 upload packets in one block of the ELF, 0x1A3C80 to 0x1AB1C0. Each is a DMA RET tag whose body is VIF MPG commands that load code into VU1 micro memory at 0 (and above 0x800 or 0x1000 for the big ones). A packet that draws with a program CALLs the program's upload packet first, so microcode is uploaded again every time a different program is needed, many times a frame.

Every program has the same shape: a setup part at address 0, started by MSCAL 0, that loads constants into registers and stops (E bit); then a batch loop, started by MSCNT for every batch of data, that ends each batch with XGKICK and stops. The instruction after each stop is a branch back to the loop, so MSCNT always lands on the loop. (That is why the compiled programs have the same body under several entry points: 0x188, 0x718, 0xAD8 and 0xD68 of 0x1A4780 are one loop reached from four stops.)

## VU1 data memory

Shared by all programs, uploaded by the camera's packet (`mm_Camera_BuildPacket`, the camera's 0x12 quadwords from +0x10):

| VU1 qwords | Contents | How the programs use it |
|---|---|---|
| 0x00-0x03 | screen matrix (`Camera.worldScreenA`, world to screen) | position transform, then divide by w |
| 0x04-0x07 | clip matrix (`worldScreenB`, a second screen matrix) | CLIP test; a primitive with a vertex outside is not drawn (ADC bit) |
| 0x08-0x0B | light direction matrix (`normalLight`, from `sceVu0NormalLightMatrix`) | normal to light space, clamped at 0 |
| 0x0C-0x0F | light colour matrix (`lightColour`, from `sceVu0LightColorMatrix`: three light colours and the ambient) | lit colour = this matrix times the clamped light terms |
| 0x10, 0x11 | fog (`fogA`, `fogB`): a in 0x10.w, b in 0x11.w | fog = a + b / w, clamped to 0..255, into XYZF2's F |

Above 0x11 the layout depends on the program family.

## Model programs (0x1A3C80, 0x1A4780; 0x1A8330)

Used for everything built from `.A3D` models: buildings, objects, people, the player, the placement marker.

**Data.** The light set's packet (`mm_LightSet_BuildPackets`, one per program) uploads the light set's 26 quadwords (+0x10) to 0x12-0x2B: a 24-entry colour table at 0x12-0x29, and 0x2A with the output offsets (0x110 for 0x1A3C80, 0xE0 for 0x1A4780). It then calls the program's upload packet and sets the double buffers: 0x42 and 0x221 for 0x1A3C80, 0xEA and 0x275 for 0x1A4780 (the TOP values in the records). Each instance is a 0x70-byte slot (`InstanceSlot`, `mm_Instance_*`, up to 1800 a frame in two buffers) that unpacks its colour and 4x4 matrix to 0x2B-0x2F and runs MSCAL 0. The matrix's last word (0x2F.w) carries the draw mode (below), and the colour's w is a colour-table index. Then the mesh's own DMA chain (inside the `.A3D` file, relocated by `mm_A3D_Relocate`) unpacks the vertex batches and runs MSCNT for each.

**Setup (MSCAL 0).** Multiplies the instance matrix into the three camera matrices (screen, clip, light direction), so the batch loop works in model space. It also copies the instance colour into 12 entries of the colour table.

**Batch (MSCNT).** A batch at TOP is: qword 0 the GIF tag of the strip (copied to the output, so the mesh decides PRIM, texture and blending), qword 1 the vertex count (x) and a colour-table index (z); then 4 qwords per vertex: position, normal, colour (integers) and texture coordinates, the fixed-point ones with 12 fraction bits. The normal's w is a sign used for back-face culling. For each vertex it transforms and divides, culls back faces of the strip from the cross product of its last two edges, clip-tests against the clip matrix, lights the vertex (the light matrices times the clamped normal terms, times the vertex colour, times the batch's colour from the table, clamped at 255), computes fog, and writes ST (perspective-correct, times 1/w), RGBAQ and XYZF2, setting ADC on vertices that must not draw. Output: one `tristrip` GIF packet of [ST, RGBAQ, XYZF2] per batch.

| Program | Checked | Notes |
|---|---|---|
| 0x1A3C80 (light set packet 0) | recorded (entries 0, 0x188, 0x718, 0xAD0) | two copies of the loop, chosen by the instance flags: non-zero computes fog per vertex (0x308), zero does not (0x728); 0x718 and 0xAD0 follow their stops (the captured runs at 0x718 write a fog value per vertex, those at 0xAD0 write 0) |
| 0x1A4780 (light set packet 1) | recorded (entries 0, 0x188, 0x718, 0xAD8, 0xD68) | the same two loops (mode 2 fog, mode 0 none) and a third for mode 1 (0xAE8): no fog; a triangle wholly outside one plane of the clip volume is dropped, one wholly inside is drawn as usual, and one that crosses it is clipped (0xD78 saves the registers to VU1 0xAC onward; 0x13F0 clips the polygon against one plane, called for -z, -x, +x, -y and +y but not +z; 0x1628 interpolates a crossing edge) and the corners left (at least three) are drawn as their own GIF packet, from two output buffers at 0x32 and 0x4B used in turn. The batch's qword 1 carries two more tag words: .y is the strip's tag (qword 0 .y) without the fog bit, and .w the same as a triangle fan (PRIM type 5). The fog loop sends the strip under qword 0's tag; the other loops send it under qword 1 .y, and the clipped polygons each as a packet of their own under qword 1 .w with NLOOP the corner count (3 or 4 in the captures) |
| 0x1A8330 (layers A and C) | read only | the same setup code as the other two and one batch loop: 0x1A3C80's loop without fog (the strip under qword 1 .y, the same facing test and strip shortening; the instance's draw mode at 0x2F.w is not read), made unlit. The light is still computed, then replaced: a vertex's colour is its own colour times the batch's colour-table entry in red, green and blue only, so its alpha is the vertex's own. The strip's last vertex is the exception: it keeps the lit colour (0x5D0 multiplies the light by its colour, as 0x1A3C80 does), then the batch's red, green and blue. Read from the code; no capture has run it |

The fog and no-fog loops of both programs also shorten a strip: after the back-face product they take the facing test's result (FMEQ on the MAC flags, 0x340 and 0x760 in 0x1A4780), and a vertex is not written when the test failed for the triangle it ends and the two before it (`IBNE vi15` falls through to the branch past the stores). The tag's NLOOP is the count of vertices written (vi11, stored at the end of the batch), so a strip seen partly from behind goes out with fewer vertices than the batch holds (captured runs at 0xAD8: 21 of 35, 37 of 48). The clipping loop does not do this.

The buildings and objects pick the mode by distance (`mm_CityRender_CollectObjects`): 2 when the camera is more than 10 above the ground or the object is past the fog start, 1 when the camera is within the object's footprint's reach (so walls next to the camera are cut at the screen's edges and the near plane instead of vanishing), else 0.

## Terrain: one quad buffer, many passes

The ground of the whole visible city is one buffer of quads, filled on the CPU every frame and then drawn by up to seven programs. Each pass gets all the quads and draws only those whose flags match its mask.

**Filling it.** `mm_Terrain_DrawPatch` builds each visible tile as a quad and `mm_Terrain_AddQuad` appends it to `CityMap.terrainBuffers` (two buffers of 0x9C00 bytes, at most 624 quads). It also builds the raised sides between tiles of different heights into `CityMap.sideBuffers` as triangles (`mm_Terrain_BuildStrip`, at most 718). A quad is four vertices whose w words carry its draw data:

| Word | Contents |
|---|---|
| vertex 0 .w | TEX0 low word: the ground texture of the tile's object (texture base at 0x1BD990) |
| vertex 1 .w | VU1 address of its texture-coordinate set: 0x21 plus an entry of the table at 0x1AF360 picked by the tile's x and z parity, so a texture spans 2 x 2 tiles |
| vertex 2 .w | flags, below |
| vertex 3 .w | 1.0 |

| Flag | Set when (`mm_Terrain_DrawPatch`) | Drawn by |
|---|---|---|
| 0x01 | the object's type gives a ground texture | base ground (0x1A5EB0, mask 1); rain and snow ground passes (mask 1) |
| 0x02 | a corner lies under the water: its height is above the level passed in (the water level, 0.5; heights grow downwards) | the water: layer G's program (0x1A6440) loads its mask word (VU1 0x3A.w, 2) and skips the quads without the bit |
| 0x04 | the tile is in the placement rectangle (`mm_PlacementLayer_InTileRect`) | placement highlight: layer C's program (0x1A7B30) loads its mask word (VU1 0x22.w, 4) and skips the quads without the bit |
| 0x08 | always | the rain (layer D, mask 8 at 0x15.x), the snow (layer E, mask 8 at 0x67.y) and the snowy ground pass (mask 8 at 0x2D.x): every visible quad |
| 0x10 | the tile and its first object both have flag 0x10 | layer F (0x1A91C0, mask 0x10) |
| 0x20, 0x40 | a corner lies below ground level (height above 0) on a type without flag 1 (not leveled ground): 0x20 in the sea (tile flag 0x20), else 0x40 (a lake) | layer B (0x1AA340), the bottom: its first pass mask 0x20 with GROUND.TEX's sand (part 17, GS block 0x2698), its second pass mask 0x40 with its rock (part 24, block 0x2708) |

Heights grow downwards (y points down): 0 is ground level, the water surface is at 0.5, basins go down to 3.2 and hills rise to -3.6. STAGE.BIN's terrain cells say what the generator does where: types 1 to 4 raise hills of four heights (`mm_TerrainGen_Hill`, tops -1.6, -2.8, -2.0 and -3.6), 6 digs a lake and 7 the sea (`mm_TerrainGen_Basin`). The stage briefings agree, record by record (record 9, "divided by 4 lakes", has only type 6; record 19, "between the southern sea and northern lake", both; record 23, "a little island without vegetation", is 98 sea cells and no plants; record 4, "a high tableland", 70 type 1 cells; record 24, "you see nothing", only flat cells). Tile flag 0x20 marks the tiles the sea lowered, and objects placed later keep it. Layer B is the bottom: its program (0x1AA340) loads each quad's four corners, sets their w to 0 and their height to 0.6 y + 0.08 (the parameters at 0x2E), then draws them like the ground. That copy lies above the real ground only where the ground is deeper than 0.2, so with the depth test the sand (sea) or rock (lakes) covers the bottom and the low strip of shore, seen through the water. Tile flag 0x10 is walkable ground (the road, a type with flag 2). Object flag 0x10 is lit: each draw sets it after dark (a road piece while its street lamps are on, a business while open) and clears it otherwise, so layer F's pass (quad flag 0x10) covers the road tiles whose lamps are lit.

**The passes' parameters.** Each pass uploads its own 17 to 19 quadwords to 0x1C and runs MSCAL 0, then the frame's chunk packet (`CityMap.chunkPackets[parity]`) sends the quads to VU1 104 quadwords (26 quads) at a time, each followed by MSCNT. The same chunk packet is called once per pass. Parameter block, as captured (`TerrainPassParams` in `config/types/05_classes.h`; the layers keep it at the start or at `ground`):

| VU1 qword | Contents |
|---|---|
| 0x1C | colour scale (128 in each lane) |
| 0x1D | TEX0: the base pass takes the low word from the quad and the high word from here; layers B and F use their own texture word whole |
| 0x1E | output offset (0x68) |
| 0x1F | GIF tag: 26 loops of [TEX0_1, PRIM, then ST, RGBAQ, XYZF2 four times] |
| 0x20 | PRIM: 0x3C (strip, Gouraud, textured, fog), 0x7C for layer F (blended) |
| 0x21-0x2C | three sets of four texture coordinates (the quarters of a 2 x 2 texture) |
| 0x2D.w | the pass's flag mask (the snow's ground pass sets x to 8 and leaves w, and its program reads x) |
| 0x2E | per pass: layer B 0.6 and 0.08 (each corner's height becomes 0.6 y + 0.08, read in 0x1AA340); the snow ground pass (0, -0.01, 0, 1): its program adds y to each corner's height, lifting the snowy ground 0.01 above the base ground; the rain ground pass gets the same vector and loads it, but does not use it |

**Programs.**

| Program | Pass | Checked | What it does per quad |
|---|---|---|---|
| 0x1A5EB0 | base ground (terrain packet B) | recorded | transform and divide; two face normals (one per triangle), each normalized, lit with the light matrices and scaled by 0x1C, so each half of the quad has one flat colour; fog; the whole quad is skipped (ADC) when any corner fails the clip test or the mask does not match |
| 0x1AA340 | layer B | recorded | the same lighting and fog with the layer's own texture; mask 0x20, then a second pass (`WaterBottomLayer.lakePacket`) that uploads another TEX0 to 0x1D and mask 0x40 to 0x2D and runs again; none of the captured chunks had a matching quad |
| 0x1A91C0 | the lit roads (layer F) | recorded | unlit, added to the frame (the packet sets ALPHA_1 to 0x30_00000048 first: the source times its alpha, plus the frame); mask 0x10; no matching quad in the captures |
| 0x1A76F0 | cliff sides (terrain packet A) | recorded | the same for triangles: 3 quadwords each (TEX0 low word, texture-coordinate set, 1), 36 per chunk of 108 quadwords, one TEX0 per triangle |

The cliff sides are double-buffered like the quads (`CityMap.sideBuffers`, filled by frame parity), but `mm_Terrain_BuildSidePackets` stores the start of the first buffer's packet in both of the map's side packet slots (checked against the decompile), so odd frames draw the triangles of the frame before. A port that wants both buffers drawn would store the second section's start there.

## Weather and water

| Program | Layer, when | Checked | Data and work |
|---|---|---|---|
| 0x1A6440 | water (layer G), every frame | recorded (entries 0, 0x68, 0x1280) | the layer's 33 quadwords to 0x1C (`mm_WaterLayer_AnimateWaves`), then the terrain buffer in chunks of 64 quadwords (16 quads). Only quads with flag 2 (mask at 0x3A.w): each corner's height becomes the water level plus 0.06 sin((\|z\| mod pi) + phase - pi/2) (ESIN; the parameters at 0x3B and 0x3C, the sign flipping every half period), a wave along z; one face normal per triangle (ESADD, ERSQRT), lit like the ground and times the layer's colour (alpha 60). Each quad is drawn twice as a blended strip: in GS context 2 (PRIM at 0x20) with texture coordinates from 0x22-0x2D and in context 1 (0x21) with 0x2E-0x39, the set picked by the quad's coordinate-set word as for the ground (the header sets ALPHA and TEX0 for both contexts); a quad with a corner outside the clip volume is dropped. 0x1D is not read |
| 0x1A8A00 | rain (layer D), weather 2 | recorded (entries 0, 0x90, 0x3C0; captured runs: 96 lines a batch, white, the lower end's alpha 40 and the upper end's 0 at that hour, the ends that fail the clip test with ADC) | its 44 quadwords to 0x12 (a GIF tag of 96 blended lines, PRIM 0x69; the colours of a streak's lower and upper end, both times the ambient light (0x0F) and clamped at 255, the lower one's alpha 0..40 by the clock and the upper one's 0; the quad mask 8 at 0x15.x; 16 drop offsets that move 0.4 a frame and wrap at 40; twelve streaks as pairs of end points), then the terrain buffer in chunks of 32 quadwords (8 quads): on every quad, the twelve streaks at the quad's x and z, moved down by the drop that a hash of the quad's position picks, wrapped into the 40 above the ground, and dropped (ADC) when an end fails the clip test |
| 0x1A8E20 | rain ground pass, weather 2 | recorded (entries 0, 0x68, 0x380; PRIM 0x7C, blended and textured, the colour (148, 148, 148, 60) at that hour) | a terrain pass with mask 1 whose TEX0 steps through six texture pages every two frames (likely splashes); it loads 0x2E but does not use it; its fixed alpha (ALPHA_1 FIX) follows the clock |
| 0x1A9A90 | snow (layer E), weather 3 | recorded (entries 0, 0x98, 0x420; 104 sprites a batch, 13 points on each of 8 quads, ST 0 to 1) | its 86 quadwords to 0x12 (a GIF tag of blended sprites, PRIM 0x76, 8 per flake; the sprite's corner ST at 0x13 and 0x14 and its size (10, 5) at 0x15; 13 points; 8 rotation matrices that turn 0.03 a frame; 32 flake offsets whose y goes from -40 to 0 by 0.03 a frame, falling, since y points down; a projection matrix; the flakes per quad (1 to 13, from the clock) and the quad mask 8 at 0x67), then the terrain buffer in chunks of 32 quadwords (8 quads): on every quad, the first n points turned by one matrix and moved by one offset (a hash of the quad's position picks both), wrapped into the 40 above the ground, each drawn as a sprite 10 x 5 times 10 / w; a 32 x 32 sprite texture (PSMT4 at GS block 0x3F70) whose alpha (in the start packet's RGBAQ) follows the clock |
| 0x1A9F10 | snow ground pass, weather 3 | recorded (entries 0, 0x78, 0x408) | a terrain pass like the rain's with mask 8 (every quad, read from 0x2D.x), each corner lifted 0.01 (0x2E.y), its alpha following the clock. Untextured: its PRIM is 0x6C (strip, Gouraud, fog, blended, no texture bit) although it still sends TEX0 and ST, and the captured colour is white with alpha 64, so the snow cover is a plain white translucent copy of the ground |

The snowflakes that land in other places than in PCSX2 (docs/STATUS.md, line 37) are this snow layer's.

## Layers A and C

| Program | Layer | Checked | Notes |
|---|---|---|---|
| 0x1A7B30 | placement highlight (layer C, pass 0, while a building or a bag item is being placed) | ran (entries 0, 0x58, 0x7D8; the captured batches hold no quad with flag 4, so they show only every quad skipped) | a terrain pass, unlit, with its own seven quadwords at 0x1C: two colours (255, 0, 0, 128) and (0, 0, 0, 128), the output offset, a GIF tag of 26 loops of 14 registers, PRIM 0x44 (blended triangle strip) and 0x82 (antialiased line strip), and mask 4 at 0x22.w, which the program tests against each quad's flags. Read from the code: each quad with flag 4 becomes 14 registers, PRIM 0x44 (flat, blended triangle strip), the first colour and corners 0, 1, 3 and 2 (two triangles over the tile), then PRIM 0x82 (antialiased line strip), the second colour and corners 0, 1, 2, 3 and 0 (the tile's outline). Each corner is lifted 0.2 and kept no deeper than 0.2 (SUBi and MINIi on y; heights grow downwards), so over water the highlight stays above the surface. A tile with a corner outside the clip volume, and every quad without flag 4, gets ADC on the vertices that would draw. The first colour is the caller's (`mm_PlacementLayer_Set`): blue (0, 0, 255, 64) where the building or item fits, red (255, 0, 0, 64) where it does not. The second stays black (0, 0, 0, 128), and under the packet's ALPHA_1 0x48 (the source times its alpha, plus the frame) a blended black line adds nothing, so the outline shows only if the GS draws this AA1 line without blending (not checked) |
| 0x1A8330 | placement marker (layer C, pass 1) and the claim-met layer (A) | read only | an unlit model program (see above): the meshes keep their own colours. Both are added to the frame. The marker is node 0 or 1 of the marker model at the footprint's centre, under ALPHA_1 0x48 (the source times its own alpha, plus the frame). The claim-met layer's instances each set ALPHA_1 0x68 (the source times FIX / 128, plus the frame), FIX being their fade, so they brighten what is behind them and fade out |

Layer A, the claim-met layer (`ClaimMetLayer`, `CityMap.claimMet`), holds instances added by `mm_CityRender_CollectObjects` for built objects whose claim was met and whose variant byte counts up (object flags 0x20 built and 8 claim met; the byte starts at 1 when the claim is met, counts to 0x2D and starts over): meshes 2n and 2n + 1 of the layer's model for a building of size n + 1, turned and squashed in y by the count, fading as it grows. They are drawn unlit (0x1A8330) and added to the frame (ALPHA_1 0x68, FIX the fade), so each one brightens what is behind it while it grows, turns and fades, then starts again while the claim stays met. What they show in the game is not confirmed by a capture; the timing points to a short effect when a building's claim is met.

## .A3D models

Read from the loader (`mm_A3DObject_Load`, `mm_A3DObject_LoadAsync`, `mm_A3D_Relocate`) and the code that draws models; the layouts are `A3DModel`, `A3DNode`, `A3DMesh`, `A3DTrack`, `A3DKey` and `A3DChainBlock` in `config/types/05_classes.h`. A file holds models one after the other, each with a 16-byte header (its size and its texture block's size, in words); an empty header ends the file.

| Part | Contents |
|---|---|
| header (`A3DModel`, 0x20 bytes) | the sizes, the word offsets of four tables (the loader turns them into addresses) and their counts |
| nodes (0xC0 bytes each) | the parent (null for a root), the mesh, the matrix relative to the parent, and the node's draw packet at +0x50, laid out as an instance slot (CNT 5, UNPACK of the colour and the world matrix to VU1 0x2B, RET with MSCAL 0; checked in `OBJECT.A3D`). The world matrix's last word marks it up to date while the matrices are updated |
| meshes | each a block with its size, its vertex count (all its batches' GIF tag NLOOPs together, in 176 of `CHAR.A3D` model 0's 177 meshes; `CHAR.A3D` fills unused part slots with one-triangle placeholders, which the loader replaces) and the DMA chain that draws its vertex batches; a model loaded "indexed" gets mesh addresses in its nodes, the others keep mesh indexes there and look them up in the table (the people and the player, drawn in groups of `CharPartModel.groupSize` meshes) |
| animations | a track count, then per animation one track per node (key count, node, keys) and the keys (position and time, rotation quaternion); `mm_A3D_Animate` slerps between the two keys around the time into the node's local matrix; track 0's last word is read as the animation's length |
| chain blocks | more DMA chains, relocated the same way |
| texture block | DMA packets that upload the model's textures; sent to the GS at load and freed |

## What a native renderer needs

* Per frame, the 3D content comes as four kinds of data that the CPU code already builds: the terrain quad buffer with its flags, the cliff-side triangles, model instances (a matrix, a colour and a flags word per mesh, up to 1800) whose meshes are fixed vertex batches inside the `.A3D` files, and the parameters of the water and weather layers.
* What VU1 does with them is ordinary vertex work: a model-view-projection transform, a per-vertex or per-face directional light with ambient, a linear fog in 1/w, a clip test that drops whole primitives, and back-face culling. All of it maps to a GPU vertex shader with the matrices and tables above as uniforms. The GS state (textures, blending, alpha test) comes from the GIF tags and A+D packets the same calls queue.
* The terrain's passes are one mesh drawn with up to seven materials, chosen by the flag bits.

## Not checked yet

* 0x1A8330's output (its loop is read from the code, above): no capture has drawn the claim-met layer (a building whose claim gets met during the capture) or the placement marker (an applicant's building whose spot is taken). None of the saves at hand has an applicant waiting to be placed; new ones come from talks.
* 0x1A7B30's output for the highlighted quads. The capture keys runs by the whole micro memory and skips a resume that starts with a busy pipeline, so it keeps mostly the first batch after a setup, and the highlighted tiles came later in the buffer.
* Which triangles exactly the strip shortening below drops: read from the code only, not traced in a run.

## Names (plan R1 step 3)

Renamed after this map: layer C is `PlacementLayer`, D `RainLayer` and E `SnowLayer` (`mm_*Layer_*`);
`mm_CityRender_DrawTerrain` (was DrawOverlays), `mm_CityRender_DrawWeather` (DrawSky), `mm_CityRender_DrawPlacement`
(DrawBuildings), `mm_RainLayer_Draw` (LayerD_DrawSkyByClock), `mm_PlacementLayer_Draw` (LayerC_DrawBuildings),
`mm_PlacementLayer_Set` (Terrain_SetVec), `mm_A3D_GetAnimLength` (A3D_GetMeshField0C), `mm_ClaimMetLayer_BeginFrame`
(LayerA_SetSource), `mm_Camera_SetFogColour` (LightSet_SetField20: word 8 of the camera's packet, the GS FOGCOL data).

Then (R27): layer B is `WaterBottomLayer` (`CityMap.waterBottom`; its passes `seaPacket` and `lakePacket`), F
`LitRoadLayer` (`CityMap.litRoads`) and G `WaterLayer` (`mm_*Layer_*`); the light set's two packets are
`LightSet.packets`, picked by `mm_LightSet_SelectProgram` (was Packet2_Draw; 1 for buildings).

Then (R28): layer A is `ClaimMetLayer` (`mm_ClaimMetLayer_*`, `CityMap.claimMet`, `mm_CityRender_DrawClaimMet`, was
DrawInstanceLayer), named by when it draws: the claim check's `claimMet` sets the count to 1 (city.cpp). What its
instances look like still waits for a capture.
