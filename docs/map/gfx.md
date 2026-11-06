# gfx

Rendering below the scene level.

- DMA lists (`mm_DmaList_*`, `mm_Dma_*`, `mm_DmaChain_*`): per-frame packet lists kicked to VIF1 and GIF; the frame
  packet is flipped by `mm_MainLoop`.
- Packets (`mm_Packet*`, `mm_PacketBuf_*`).
- Models (`mm_A3DObject_*`, `mm_A3D_*`): `.A3D` models and `.A3L` model lists; nodes are 0xC0 bytes with a matrix.
- Instancing (`mm_Instance*`): two buffers of 1800 slots x 0x70 bytes (`0x001BDA20`/`0x001BDA24`, current index
  `0x001BDA30`, used count `0x001BDA28`).
- Textures: `.TEX` sprites (`mm_TexSprite_*`) and images (`mm_TexImage_*`).
- The city render level of the map object (vtable `0x001BCF00`, `mm_CityRender_*`) owns seven layer objects
  (`mm_LayerA` .. `mm_LayerG`) at `+0x2F74`. Each layer has a GS register setup, a packet builder that references a
  VU1 microprogram (13 upload packets, `0x001A3C80` .. `0x001AB1C0`), a destructor and a draw. What each layer and
  program draws (terrain passes, rain, snow, water, placement highlight) is mapped in `docs/RENDER_PIPELINE.md`.
- Lights (`mm_LightSet_*`), display setup (`mm_Gs_InitDisplay`).

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

117 functions (ported 1, verified 116). Blocks: 30 can reach a thread switch, 2 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00128EE0` | `void mm_LightSet_BuildPackets(LightSet *lights)` | 432 | no | two VU1 packets (packetA, packetB): the set's 26 quadwords from groupA to VU1 0x12, a call of the program chain (0x1A3C80 / 0x1A4780), VIF base and offset |
| `0x00129090` | `void mm_Packet2_Draw(uint32_t *packets, int second)` | 48 | no | queues packet a0[0], or a0[1] when a1 != 0 |
| `0x001290C0` | `A3DObject *mm_A3DObjectBase_DeletingDtor(A3DObject *model, int16_t deleteFlag)` | 84 | yes | base destructor 0x12AB40, then delete when the flag is > 0 |
| `0x00129120` | `int mm_LightSet_Init(LightSet *lights)` | 228 | no | light set base setup: the two programs' output offsets (0x110 and 0xE0), every colour of both groups and the instance colour set to 1, then its packets. Returns the set |
| `0x00129210` | `float mm_A3D_GetAnimLength(A3DObject *model, int animation)` | 24 | no | the length of a model's animation (its first track's last word) |
| `0x00129230` | `void mm_A3D_Animate(A3DObject *model, int anim, float time)` | 816 | no | keyframe animation a1 of model a0 at time a2: each node track's pair of keys around the time sets the node's local matrix (quaternion slerp, linear position); draws nothing |
| `0x00129570` | `void mm_A3D_Relocate(A3DObject *model, int indexed)` | 988 | yes | turns the loaded model's offsets into pointers, patches the addresses in its DMA chains (mm_DmaChain_*) and uploads its textures (mm_Dma_KickVif1Now) |
| `0x00129950` | `int64_t mm_A3DObject_ChangeLuminosity(A3DObject *model, uint32_t *colour, int index)` | 96 | yes | CA3DObject::ChangeLuminusity (name from its error string): sets node `index`'s colour (its slot's colour) to the vec4 at `colour` when the node has a mesh; prints an error for a bad node index |
| `0x001299B0` | `void mm_A3DObject_DrawNode(A3DObject *model, int node)` | 104 | no | queues node `index`'s packet (its slot) and its mesh's chain; nodes are 0xC0 bytes (the model data's nodes) |
| `0x00129A20` | `void mm_A3DObject_Draw(A3DObject *model)` | 228 | no | updates every node's world matrix (mm_A3DObject_UpdateNodeMatrix) and queues all nodes |
| `0x00129B10` | `void mm_A3DObject_UpdateMatrices(A3DObject *model)` | 248 | no | world matrix of each node: root nodes = the object's matrix x local, children via mm_A3DObject_UpdateNodeMatrix |
| `0x00129C90` | `void mm_A3DObject_UpdateNodeMatrix(A3DObject *model, A3DNode *node)` | 412 | no | node world matrix = parent's world matrix x local (recursive) |
| `0x00129E30` | `A3DObject *mm_A3DObject_dtor(A3DObject *model, int16_t deleteFlag)` | 136 | yes | frees the model data (model) and the animation table (animations), base destructor; delete when the flag is > 0 |
| `0x00129EC0` | `int mm_A3DObject_LoadAsync(A3DObject *model, GameFile *file, int indexed)` | 544 | yes | asynchronous .A3D load from an open file, one step per call (state gp 0x1BD...80A0): header into 0x1C5100, then data; 0 = busy, -1 = error |
| `0x0012A0E0` | `int32_t *mm_A3DObject_Load(A3DObject *model, int path, int indexed)` | 380 | yes | synchronous .A3D load: header (word counts), data and pointer table, then mm_A3D_Relocate |
| `0x0012A260` | `A3DObject *mm_InstanceBase_ctor(A3DObject *model)` | 44 | no | the instance base: the base object constructor, and the word at 0x1BDA10 cleared; returns the model |
| `0x0012A2B0` | `void mm_Packet_Draw(uint32_t *packets)` | 8 | no | queues the packet whose address is the object's first word |
| `0x0012A2C0` | `void mm_Camera_BuildPacket(Camera *camera)` | 216 | no | the camera's VU1 packet: a GS register write, then worldScreenA through fogB (matrices, fog) unpacked to VU1 address 0 |
| `0x0012A650` | `void mm_Camera_SetFogColour(Camera *camera, int colour)` | 12 | no | the fog colour (GS FOGCOL) in the camera's packet (word 8), from the sky's low colour each frame |
| `0x0012C4D0` | `void mm_A3D_AnimateIfRoom(A3DObject *model, int anim, float phase)` | 60 | no | (model, anim, phase) mm_A3D_Animate only while the model's nodes still fit in this frame's instance slots (0x708) |
| `0x0012C510` | `void mm_InstanceBuf_Init(void)` | 400 | yes | allocates the two instance buffers and pre-fills each slot's DMA/VIF header (CNT 5, STCYCL, UNPACK) |
| `0x0012C6A0` | `void mm_Instance_DrawMesh(A3DObject *model, Vec4 *matrix, Vec4 *colour, int mesh, int mode)` | 236 | no | (model, matrix, colour, mesh, mode) an instance slot (the matrix, the colour, the draw mode in the matrix's last word) and the mesh's chain, while fewer than 1800 instances are queued |
| `0x0012C790` | `void mm_Instance_DrawNode(A3DObject *model, Vec4 *colour, int node, int mode)` | 240 | no | (model, colour, node, mode) an instance of a node with its world matrix and its mesh, while fewer than 1800 instances are queued |
| `0x0012C880` | `void mm_Instance_DrawModel(A3DObject *model, Vec4 *colour, int reserve, int mode)` | 324 | no | (model, colour, reserve, mode) updates the model's node matrices and adds an instance per node with a mesh, when its nodes plus `reserve` still fit |
| `0x0012C9D0` | `A3DObject *mm_InstanceModel_ctor(A3DObject *model)` | 68 | yes | model constructor; the first one allocates the instance buffers |
| `0x0012D6B0` | `void mm_A3D_AnimateCharacter(A3DObject *model, int anim, float time)` | 824 | no | the same code as mm_A3D_Animate (tracks without keys skipped a step earlier), used by the character draws (mm_Person_Draw, mm_Person_Draw2) |
| `0x0012D9F0` | `void mm_Instance_DrawModelLit(A3DObject *model, Vec4 *colour, int variant, int style)` | 852 | no | a model whose nodes hold mesh indexes, with no room check (the callers make it): nodes 0 and 1, and node 2 for style 0, each with its own mesh, node 0's offset by `variant`; then each later node that has a mesh, with the meshes of the style's group of 13 in turn. Draw mode 0. The second count rises by the node count. Used for the player (mm_Player_Draw: variant = its mode, style = the bag's item) |
| `0x0012DD50` | `void mm_Instance_DrawNodeLimited(A3DObject *model, Vec4 *colour, int node, int mode)` | 280 | no | like mm_Instance_DrawNode, also capped by a second counter (gp) |
| `0x0012DE70` | `void mm_Instance_DrawModelEx(A3DObject *model, Vec4 *colour, int mode, int offset0, int group, int offset3, int attach, int offset1)` | 940 | no | the same kind of model with the offsets passed in, all with draw mode `mode`: node 0 with its mesh + `offset0`; node 1 with its mesh + `offset1` unless that is 4 or more; when `attach` is set, mesh 0 of the part model at 0x1F3B80 (also kept at 0x1BDA38) at node 2; node 3 with its mesh + `offset3` |
| `0x0012E220` | `int mm_InstanceModel_LoadAsync(A3DObject *model, GameFile *file, int indexed)` | 120 | yes | (model, file, indexed) mm_A3DObject_LoadAsync; when done (1) for a model whose nodes keep mesh indexes (`indexed` 0) it stores groupSize (the first node's mesh index / 13) |
| `0x0012E2A0` | `A3DObject *mm_InstanceModel_ctor2(A3DObject *model)` | 40 | yes | mm_InstanceModel_ctor wrapper |
| `0x0012E2D0` | `void mm_Perf_AdaptDrawRange(void)` | 152 | no | once a frame: the second instance limit follows the frame load, dropping by 12 per percent above 95, else rising by 1, within 255..935 (the second count restarts) |
| `0x001480F0` | `void mm_CityRender_InitParams(CityMap *map)` | 356 | no | GS register defaults of the render level: the base ground pass (groundParams, mask 1) and the cliff sides (sideColour, sideTex0, the GIF tag and the texture coordinates) |
| `0x00148260` | `void mm_CityRender_UpdatePeople(CityMap *map, CityScene *city)` | 568 | indirect | every object in view that is not drawn yet this frame (flag 4 marks it) has its type's drawPeople slot called with the footprint's middle and the ground height, the facing, the city, the object, a part word (0) and the draw mode (2, fog, when the camera distance + 1 is past the fog start, else 0) |
| `0x001484A0` | `void mm_CityRender_DrawWeather(CityMap *map, CityScene *city, int pass, Vec4 *view)` | 96 | no | (map, city, pass, view) the weather: pass 0 the rain, other passes the snow (passed `view`), each with this frame's chunk packet; the city calls it only in rain (2) or snow (3) |
| `0x00148500` | `void mm_CityRender_CollectObjects(CityMap *map, CityScene *city)` | 1088 | indirect | the same walk for drawing the buildings: objects not marked yet and without flag 0x80 go through every object of their stack (bottom up, 0.8 higher for each above the first; heights grow downwards) with the type's draw slot (btype, facing, city, object, part word, draw mode, x, y, z). The part word: 1 for the bottom object, 2 for each above it, 3 for the top of a full stack (maxStack), 4 for the top of a lower one, 0 for a lone object; bit 3 is kept. The draw mode: 2 (fog) when the camera is more than 10 above the ground or the object is past the fog start, 1 (clipped against the view volume) when the camera is within the footprint's reach, else 0 |
| `0x00148950` | `void mm_CityRender_DrawWater(CityMap *map, CityScene *city)` | 20 | no | the water layer (mm_LayerG_Draw) for this frame's parity at the city's water level |
| `0x00148970` | `void mm_CityRender_DrawInstanceLayer(CityMap *map)` | 8 | no | draws the instance layer (instances) |
| `0x00148980` | `void mm_CityRender_DrawPlacement(CityMap *map, int pass)` | 36 | no | the placement layer's pass (0 the highlighted rectangle, 1 the marker) with this frame's chunk packet; it draws only while a building is being placed |
| `0x001489B0` | `void mm_CityRender_DrawTerrain(CityMap *map, CityScene *city)` | 136 | no | the terrain: the base ground pass over this frame's chunk packet, the cliff sides, then layers B and F over the same chunk packet |
| `0x00148A40` | `CityMap *mm_CityRender_dtor(CityMap *map, int16_t deleteFlag)` | 364 | yes | vtable 0x1BCF00; deletes the seven layers and buffers, people-level destructor |
| `0x00148BB0` | `CityMap *mm_CityRender_ctor(CityMap *map)` | 436 | yes | people-level constructor, vtable 0x1BCF00; builds the terrain packets and the seven layers in the packet buffer |
| `0x00148D70` | `void mm_TexImage_Convert(TexImage *image, uint32_t textureBlock, uint32_t clutBlock)` | 856 | no | (image, textureBlock, clutBlock): DMA packet over the start of the image that uploads its palette and texels (PSMT4/PSMT8/32-bit, square) to the given GS blocks |
| `0x001490D0` | `int mm_Dma_KickVif1Now(uint32_t address)` | 8 | yes |  |
| `0x001490E0` | `TexImage *mm_TexImage_dtor(TexImage *image, int16_t deleteFlag)` | 100 | yes | frees the image data (file); delete when the flag is > 0 |
| `0x00149150` | `int mm_TexImage_Load(TexImage *image, char *name, int textureBlock, int clutBlock)` | 132 | yes | (image, name, textureBlock, clutBlock) loads the texture file `name` whole and converts it for upload: the texels to `textureBlock`, the palette to `clutBlock` (mm_TexImage_Convert) |
| `0x001491E0` | `uint32_t *mm_LayerA_BuildPacket(LayerA *layer, uint32_t *packet)` | 280 | no | layer A's packets: the start uploads the layer's output offset and colour to VU1 address 0x2A, flushes, calls the chain at 0x1A8330 and sets the VIF base and offset; the end sets ALPHA_1 (0x20_00000044) through the GIF |
| `0x00149300` | `void mm_LayerA_Draw(LayerA *layer)` | 256 | no | layer A: the start packet, then for each of this frame's instances its packet (ALPHA_1 with its fade) and its two meshes (2n and 2n + 1) under its matrix, then the end packet |
| `0x00149400` | `void mm_LayerA_AddInstance(LayerA *layer, int size, int age, float x, float y, float z)` | 800 | no | (layer, size, age, x, y, z) one more instance in this frame's buffer (at most 32) for a building of `size`, `age` frames into its effect (1 to 45, mm_CityRender_CollectObjects), its step being age - 1: meshes 2(size - 1) and the next; its matrix a turn about y of step / 100 times a scale in y of 0.08 per step (growing from nothing, up to 24 steps), then x, y and z, 0.12 higher for each step past 24 (heights grow downwards); its fade 0x40 less 1.5 per step, not below 0 |
| `0x00149720` | `void mm_LayerA_BeginFrame(LayerA *layer, int source)` | 12 | no | picks the buffer layer A fills this frame (by the frame parity) and empties it |
| `0x00149730` | `LayerA *mm_LayerA_DeletingDtor(LayerA *layer, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x00149790` | `LayerA *mm_LayerA_ctor(LayerA *layer)` | 512 | no | base constructor, builds its packet in the packet buffer |
| `0x0014C200` | `void mm_Dma_AppendCall(int address)` | 8 | no |  |
| `0x0014C620` | `void mm_LightSet_UpdateByClock(LightSet *lights, int clock, int weather)` | 508 | no | interpolates the light colours over the day (2-hour segments, tables 0x1B0550/0x1B0670) |
| `0x0014C820` | `LightSet *mm_LightSet_DeletingDtor(LightSet *lights, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x0014C880` | `LightSet *mm_LightSet_ctor(LightSet *lights)` | 40 | no | mm_LightSet_Init |
| `0x0014C8E0` | `uint32_t *mm_LayerB_BuildPacket(LayerB *layer, uint32_t *packet)` | 372 | no | layer B's packets: the first calls the chain at 0x1AA340, uploads the layer's parameters (19 quadwords) to VU1 0x1C, sets the VIF base and offset and starts the program; the second uploads the second pass's texture and mask to 0x1D and 0x2D and starts the program again |
| `0x0014CA60` | `void mm_LayerB_InitParams(LayerB *layer)` | 428 | no | layer B's two passes: the first over the quads with flag 0x20, the second (its own texture) over those with 0x40 |
| `0x0014CC10` | `void mm_LayerB_Draw(LayerB *layer, int packet)` | 76 | no | layer B: its two passes (packets A and B), each followed by a call of the terrain chunk packet |
| `0x0014CC60` | `LayerB *mm_LayerB_DeletingDtor(LayerB *layer, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x0014CCC0` | `LayerB *mm_LayerB_ctor(LayerB *layer)` | 72 | no | base constructor, parameters, packet |
| `0x0014CD10` | `int mm_PlacementLayer_InTileRect(PlacementLayer *layer, int x, int z)` | 108 | no | 1 when the layer's tile rectangle is active (rectActive == 1) and tile (a1, a2) lies inside x rectX0..rectX1, z rectZ0..rectZ1; mm_Terrain_DrawPatch then sets flag 4 on the patch (was mm_Terrain_Nop14CD10, not empty) |
| `0x0014CD80` | `void mm_PlacementLayer_Set(PlacementLayer *layer, int mode, int32_t *rect, uint32_t *colour)` | 28 | no | the placement highlight: its colour, its rectangle (x0, z0, x1, z1) and its mode (1 the highlight, 2 the marker) |
| `0x0014CDA0` | `uint32_t *mm_PlacementLayer_BuildPacket(PlacementLayer *layer, uint32_t *packet)` | 584 | no | packets of the placement highlight (microcode 0x1A7B30) and the marker (0x1A8330) |
| `0x0014CFF0` | `void mm_PlacementLayer_InitParams(PlacementLayer *layer)` | 252 | no | draw parameters of the placement highlight (two colours, the output offset, a GIF tag of 26 loops, the fill and outline PRIMs, mask 4) and of the marker |
| `0x0014D0F0` | `void mm_PlacementLayer_Draw(PlacementLayer *layer, int packet, CityMap *map, int pass)` | 548 | no | pass 0 (mode 1) the highlighted rectangle's three packets around the map's own; pass 1 (mode 2) the placement marker, node 0 of the marker model (node 1 for a size 3 building) at the middle of the footprint, 0.8 above the tile and each object stacked on it |
| `0x0014D320` | `PlacementLayer *mm_PlacementLayer_DeletingDtor(PlacementLayer *layer, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x0014D380` | `PlacementLayer *mm_PlacementLayer_ctor(PlacementLayer *layer)` | 88 | no | base constructor, parameters, packet |
| `0x0014D3E0` | `uint32_t *mm_RainLayer_BuildPacket(RainLayer *layer, uint32_t *packet, int32_t *buffers)` | 1216 | no | packets of the rain: the streaks (microcode 0x1A8A00) and the wet ground pass (0x1A8E20) |
| `0x0014D8A0` | `void mm_RainLayer_InitParams(RainLayer *layer)` | 1652 | no | the rain's parameters: the streaks' GIF tag (96 blended lines), two colours, 16 drop offsets and twelve streaks; then the wet ground pass (mask 1) |
| `0x0014DF20` | `void mm_RainLayer_Draw(RainLayer *layer, int pass, int packet, int clock)` | 436 | no | the rain by the time of day: streak colour and ground pass alpha from the clock, the sixteen drops fall 0.4 a frame within 40, the ground texture steps through six pages (the splashes); then its packets are queued |
| `0x0014E0E0` | `RainLayer *mm_RainLayer_DeletingDtor(RainLayer *layer, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x0014E140` | `RainLayer *mm_RainLayer_ctor(RainLayer *layer, int buffers)` | 88 | no | base constructor, parameters, packet |
| `0x0014E1A0` | `uint32_t *mm_SnowLayer_BuildPacket(SnowLayer *layer, uint32_t *packet, int32_t *buffers)` | 1796 | no | packets of the snow: the flakes (microcode 0x1A9A90) and the snowy ground pass (0x1A9F10) |
| `0x0014E8B0` | `void mm_SnowLayer_InitParams(SnowLayer *layer)` | 1280 | no | the snow's parameters: eight random turns about y with their matrices and 32 random flake offsets; then the snowy ground pass |
| `0x0014EDB0` | `void mm_SnowLayer_Draw(SnowLayer *layer, int pass, int packet, int clock, Vec4 *view)` | 752 | no | the snow by the time of day: the flake count and alpha and the ground pass alpha from the clock, the turns advance 0.03, the flakes fall 0.03 a frame within 40; then its packets are queued |
| `0x0014F0A0` | `SnowLayer *mm_SnowLayer_DeletingDtor(SnowLayer *layer, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x0014F100` | `SnowLayer *mm_SnowLayer_ctor(SnowLayer *layer, int buffers)` | 88 | no | base constructor, parameters, packet |
| `0x0014F160` | `uint32_t *mm_LayerF_BuildPacket(LayerF *layer, uint32_t *packet)` | 296 | no | layer F's packet: ALPHA_1 (0x30_00000048) through the GIF, then like layer B's first packet with the chain at 0x1A91C0 |
| `0x0014F290` | `void mm_LayerF_InitParams(LayerF *layer)` | 356 | no | layer F's pass parameters: colour 128 in every lane, its GS words, and a mask whose last word is 0x10 |
| `0x0014F400` | `void mm_LayerF_Draw(LayerF *layer, int packet)` | 48 | no | layer F: its packet, then a call of the terrain chunk packet |
| `0x0014F430` | `LayerF *mm_LayerF_DeletingDtor(LayerF *layer, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x0014F490` | `LayerF *mm_LayerF_ctor(LayerF *layer)` | 72 | no | base constructor, parameters, packet |
| `0x0014F4E0` | `void mm_LayerG_AnimateWaves(LayerG *layer, float waterLevel)` | 776 | no | (layer, waterLevel) the water's phase and direction (wave height 0.06 along z around the water level, computed by the program 0x1A6440) and the sway and scroll of its two textures' coordinates |
| `0x0014F7F0` | `uint32_t *mm_LayerG_BuildPacket(LayerG *layer, uint32_t *packet, int32_t *buffers)` | 1796 | no | layer G (the water): two sections, one per terrain buffer. Each starts with the GS state for both contexts (ALPHA_1 and ALPHA_2, TEX0_1 and TEX0_2), the program 0x1A6440 and the layer's parameters, then the buffer's quads in 39 chunks of 0x400 bytes (4 groups of 8, then 7 singles) and a RET. Returns the end |
| `0x0014FF00` | `void mm_LayerG_InitParams(LayerG *layer)` | 392 | no | layer G (the water) keeps its texture coordinates at rest (waveBase) and copies them to both wave sets |
| `0x00150090` | `void mm_LayerG_Draw(LayerG *layer, int pass, float waterLevel)` | 64 | no | (layer, pass, waterLevel) the water: the waves move on around the water level, then this frame's packet (one per terrain buffer) is queued |
| `0x001500D0` | `LayerG *mm_LayerG_DeletingDtor(LayerG *layer, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x00150130` | `LayerG *mm_LayerG_ctor(LayerG *layer, int buffers)` | 88 | no | base constructor, parameters, packet |
| `0x00161290` | `int mm_TexSprite_SetFrame(TexSprite *sprite, int frameX, int frameY)` | 152 | no | first tile (frameX, frameY, within the image), trims the shown tiles to fit, then rebuilds |
| `0x00161330` | `int mm_TexSprite_SetClip(TexSprite *sprite, uint32_t width, uint32_t height)` | 124 | no | tiles shown (clipWidth, clipHeight, up to the image's tiles past the frame), then rebuilds; mm_TexSprite_BuildPacket uses them as end tiles |
| `0x001613B0` | `void mm_TexSprite_GreenToAlpha(TexSprite *sprite)` | 148 | no | turns the green-tinted grey colours (red = blue != green) translucent: alpha 0x80 - green / 2, then green set to red. Transparent colours are left alone |
| `0x00161450` | `void mm_TexSprite_SetAlpha(TexSprite *sprite, uint8_t alpha, int keepClear)` | 108 | no | sets every colour's alpha; with `keepClear`, fully transparent colours stay so |
| `0x001614C0` | `int mm_TexSprite_SetPosScale(TexSprite *sprite, int x, int y, int z, float scaleX, float scaleY)` | 24 | no | position a3..a5 and scale a0/a1, then rebuilds |
| `0x001614E0` | `void mm_TexSprite_Draw(TexSprite *sprite)` | 8 | no | queues the sprite packet (+8) |
| `0x001614F0` | `int mm_TexSprite_BuildPacket(TexSprite *sprite)` | 2152 | no | builds the sprite's DMA packet: palette upload to block 0x1E00, then per tile a texel upload, texture flush and a textured sprite (TEX0, ST, XYZ2); returns the scaled tile height |
| `0x00161D60` | `TexSprite *mm_TexSprite_dtor(TexSprite *sprite, int16_t deleteFlag)` | 136 | yes | vtable 0x1BCFD8, frees image and packet |
| `0x00161DF0` | `int mm_TexSprite_Load(TexSprite *sprite, int file, int blend)` | 260 | yes | loads a .TEX file whole and builds its packet |
| `0x00184030` | `int mm_DmaChain_Next(DmaChainReader *reader)` | 208 | no | steps a DMA chain reader (tag: the current tag): CNT skips qwc+1, END stays, RET returns 0; caches the tag's id, qwc, address, VIF words and data |
| `0x00184100` | `int mm_DmaChain_Begin(DmaChainReader *reader, int chain)` | 20 | no | reader starts at a1 with an END marker so the first mm_DmaChain_Next reads the first tag |
| `0x00184F60` | `void mm_Gs_InitDisplay(uint16_t width, int height)` | 652 | yes | sets up the display: the display environment (0x1F5B10, a frame 8 units on: its first fields as the flag bits below), the draw environment (0x1F5B20) for the given width and a height worked out from the screen's, the double buffer (0x1F5AC0: width by half the height, cleared) and a GIF packet made in a 0x60 byte block that sets five GS registers (the one tag at 0x1B2540 opens it |
| `0x00186D00` | `void mm_PacketBuf_Seek(int address)` | 8 | no | sets the packet buffer write pointer (0x1BDCE8) |
| `0x00186D10` | `int mm_PacketBuf_Tell(void)` | 8 | no | returns the packet buffer write pointer (0x1BDCE8) |
| `0x00186D20` | `int mm_Dma_KickVif1Now(uint32_t address)` | 192 | yes | sends a two-tag chain (CALL a0, END) on VIF1 right away and waits for GS path idle before and after |
| `0x00186DE0` | `void mm_Dma_AppendCall(int address)` | 116 | no | appends a DMA CALL tag (id 5) to address a0 at the gp list pointer, then an END tag the next append overwrites; at most 0x4000 qwords per list |
| `0x00186E60` | `void mm_DmaList_QueueGsDefaults(void)` | 124 | no | CALLs the static packet 0x1B2760: standard alpha blend in ALPHA_1/2, TEST_1/2 alpha != 0 and Z >= |
| `0x00186EE0` | `void mm_DmaList_QueueOtherOverlay(void)` | 152 | no | CALLs the other one of the two 0xBA0-byte packets at 0x1F69C0 built by 0x1873D0 |
| `0x00186F80` | `void mm_DmaList_QueueClearScreen(void)` | 124 | no | CALLs the static packet 0x1B28E0: full-screen sprite writing colour 0 and Z 0 |
| `0x00187000` | `void mm_DmaList_QueueFade(int alpha, int overAll)` | 272 | no | full-screen black sprite with alpha a0 (patched into 0x1B2830/0x1B28A0); a1 == 0 draws it at Z 0xFFFFF000 (0x1B2800), else at Z max (0x1B2870) |
| `0x00187110` | `void mm_DmaList_QueueClearZ(void)` | 124 | no | CALLs the static packet 0x1B26D0: full-screen sprite that only writes Z = 0 (alpha test NEVER, AFAIL = ZB_ONLY) |
| `0x00187190` | `void mm_DmaList_Rewind(void)` | 56 | yes | waits for GS path idle, then resets the write pointer to the start of the current list |
| `0x001871D0` | `void mm_DmaList_Flip(void)` | 40 | no | switches to the other of the two 64 KB lists (0x1BD9C0/0x1BD9C4) |
| `0x00187200` | `void mm_DmaList_BeginScene(void)` | 132 | no | rewinds the 0x8000-byte packet buffer (0x1BDCE4 -> 0x1BDCE8) and queues the default GS state |
| `0x00187290` | `void mm_DmaList_KickFrame(void)` | 220 | no | appends the default GS state, then starts the current list on VIF1 (chain mode); the city's VU1/GS work happens under this kick |
| `0x00187370` | `void mm_DmaList_Free(void)` | 92 | yes | frees both lists and the packet buffer |
| `0x001873D0` | `void mm_DmaList_InitPackets(void)` | 616 | no | builds two static packets at 0x1F69C0 that draw a 640 x 224 picture from GS block 0x8000 (+0x8C0) as 5 x 7 sprites of 128 x 32 |
| `0x00187640` | `void mm_DmaList_Init(void)` | 104 | yes | allocates the two 64 KB lists and the 0x8000-byte packet buffer, then builds the overlay packets (0x1873D0) |
