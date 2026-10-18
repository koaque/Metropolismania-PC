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
  (`mm_LayerA` .. `mm_LayerG`) at `+0x2F60`. Each layer has a GS register setup, a packet builder that references a
  VU1 microprogram (`0x001A3E98` .. `0x001AB1B8`), a destructor and a draw. Which layer draws what is provisional.
- Lights (`mm_LightSet_*`), display setup (`mm_Gs_InitDisplay`).

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

117 functions (typed 57, verified 60). Blocks: 30 can reach a thread switch, 2 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00128EE0` | `void mm_LightSet_BuildPackets(LightSet *lights)` | 432 | no | two VU1 packets: the set's 26 quadwords from +0x10 to VU1 0x12, a call of the program chain (0x1A3C80 / 0x1A4780), VIF base and offset |
| `0x00129090` | `void mm_Packet2_Draw(uint32_t *packets, int second)` | 48 | no | queues packet a0[0], or a0[1] when a1 != 0 |
| `0x001290C0` | `A3DObject *mm_A3DObjectBase_DeletingDtor(A3DObject *model, int16_t deleteFlag)` | 84 | yes | base destructor 0x12AB40, then delete when the flag is > 0 |
| `0x00129120` | `int mm_LightSet_Init(LightSet *lights)` | 228 | no | (provisional, was mm_Unk129120_Init) base init; +0x190 = 0x110, +0x194 = 0xE0; 12 colour/direction slots and ambient set to 1.0 |
| `0x00129210` | `float mm_A3D_GetMeshField0C(A3DObject *model, int mesh)` | 24 | no | word +0xC of mesh a1 in the model's mesh table (+0x50) |
| `0x00129230` | `void mm_Instance_DrawSwaying(A3DObject *model, int anim, float time)` | 816 | no | (provisional, was mm_A3D_SineDeform) adds instances for mesh a2 of model a1 with sine-wave motion at time a0 (callers check the instance budget first) |
| `0x00129570` | `void mm_A3D_Relocate(int32_t *data, int base)` | 988 | yes | turns the loaded model's offsets into pointers, patches the addresses in its DMA chains (mm_DmaChain_*) and uploads its textures (mm_Dma_KickVif1Now) |
| `0x00129950` | `int mm_A3DObject_ChangeLuminosity(A3DObject *model, uint32_t *colour, int a2)` | 96 | yes | CA3DObject::ChangeLuminusity (name from its error string): sets node a2's colour (node +0x60) to the vec4 at a1; prints an error for a bad node index |
| `0x001299B0` | `void mm_A3DObject_DrawNode(A3DObject *model, int node)` | 104 | no | queues node a1's packet (node +0x50) and its mesh packet (mesh +0x10); nodes are 0xC0 bytes at model +8 |
| `0x00129A20` | `void mm_A3DObject_Draw(A3DObject *model)` | 228 | no | updates every node's world matrix (mm_A3DObject_UpdateNodeMatrix) and queues all nodes |
| `0x00129B10` | `void mm_A3DObject_UpdateMatrices(A3DObject *model)` | 248 | no | world matrix of each node: root nodes = object matrix (this+0x10) x local, children via mm_A3DObject_UpdateNodeMatrix |
| `0x00129C90` | `void mm_A3DObject_UpdateNodeMatrix(A3DObject *model, int32_t *node)` | 412 | no | node world matrix = parent's world matrix x local (recursive) |
| `0x00129E30` | `A3DObject *mm_A3DObject_dtor(A3DObject *model, int16_t deleteFlag)` | 136 | yes | frees the model data (+0) and the node array (+0x50), base destructor; delete when the flag is > 0 |
| `0x00129EC0` | `int mm_A3DObject_LoadAsync(A3DObject *model, GameFile *gamefile, int a2)` | 544 | yes | asynchronous .A3D load from an open file, one step per call (state gp 0x1BD...80A0): header into 0x1C5100, then data; 0 = busy, -1 = error |
| `0x0012A0E0` | `int32_t *mm_A3DObject_Load(A3DObject *model, int file, int a2)` | 380 | yes | synchronous .A3D load: header (word counts), data and pointer table, then mm_A3D_Relocate |
| `0x0012A260` | `A3DObject *mm_InstanceBase_ctor(A3DObject *model)` | 44 | no | (provisional) base object constructor, clears 0x1BDA10 |
| `0x0012A2B0` | `void mm_Packet_Draw(uint32_t *a0)` | 8 | no | queues the packet whose address is the object's first word |
| `0x0012A2C0` | `void mm_Camera_BuildPacket(Camera *camera)` | 216 | no | the camera's VU1 packet: a GS register write, then +0x10..+0x12F (matrices, fog) unpacked to VU1 address 0 |
| `0x0012A650` | `void mm_LightSet_SetField20(LightSet *lights, int value)` | 12 | no | (provisional) (*this)+0x20 = a1, from the time-of-day update |
| `0x0012C4D0` | `void mm_Instance_DrawSwayingIfRoom(A3DObject *model, int a1, float phase)` | 60 | no | mm_Instance_DrawSwaying when the model's nodes still fit in the 1800-instance budget |
| `0x0012C510` | `void mm_InstanceBuf_Init(void)` | 400 | yes | allocates the two instance buffers and pre-fills each slot's DMA/VIF header (CNT 5, STCYCL, UNPACK) |
| `0x0012C6A0` | `void mm_Instance_DrawMesh(A3DObject *model, Vec4 *matrix, Vec4 *colour, int mesh, int flags)` | 236 | no | writes matrix a1/a2 and colour into the next instance slot and queues it with the mesh packet |
| `0x0012C790` | `void mm_Instance_DrawNode(A3DObject *model, Vec4 *colour, int node, int flags)` | 240 | no | instance of node a2 of model a0 with colour a1 |
| `0x0012C880` | `void mm_Instance_DrawModel(A3DObject *model, Vec4 *colour, int reserve, int flags)` | 324 | no | updates the model's node matrices and adds an instance per node with a mesh, if the budget allows |
| `0x0012C9D0` | `A3DObject *mm_InstanceModel_ctor(A3DObject *model)` | 68 | yes | model constructor; the first one allocates the instance buffers |
| `0x0012D6B0` | `void mm_Instance_DrawSwaying2(A3DObject *model, int anim, float time)` | 824 | no | (provisional) second sine-wave instance variant, used by 0x133E80/0x1345E0 |
| `0x0012D9F0` | `void mm_Instance_DrawModelLit(A3DObject *model, Vec4 *colour, int variant, int style)` | 852 | no | (provisional) instances of a model whose nodes hold mesh-table indexes: nodes 0-2 with their own meshes (node 0 offset, node 2 only for style 0), later nodes from the style's group of 13 |
| `0x0012DD50` | `void mm_Instance_DrawNodeLimited(A3DObject *model, Vec4 *colour, int node, int flags)` | 280 | no | like mm_Instance_DrawNode, also capped by a second counter (gp) |
| `0x0012DE70` | `void mm_Instance_DrawModelEx(A3DObject *model, Vec4 *colour, int flags, int offset0, int group, int offset3, int attach, int offset1)` | 940 | no | (provisional) as mm_Instance_DrawModelLit with per-call mesh offsets and flags, an optional attached part (0x1F3B80) at node 2, later nodes by group x the size at +0x60 |
| `0x0012E220` | `int mm_InstanceModel_LoadAsync(A3DObject *model, GameFile *gamefile, int a2)` | 120 | yes | mm_A3DObject_LoadAsync; when done (and a2 == 0) stores the frame count (+0x60 = data +4 / 13) |
| `0x0012E2A0` | `A3DObject *mm_InstanceModel_ctor2(A3DObject *model)` | 40 | yes | mm_InstanceModel_ctor wrapper |
| `0x0012E2D0` | `void mm_Perf_AdaptDrawRange(void)` | 152 | no | (provisional) 0x1BD980 tracks 0x1BD9B8: shrinks when it is above 95, else grows by 1; clamped to 0xFF..0x3A7 |
| `0x001480F0` | `void mm_CityRender_InitParams(CityMap *map)` | 356 | no | GS register defaults of the render level (+0x2D60..) |
| `0x00148260` | `void mm_CityRender_UpdatePeople(CityMap *map, CityScene *city)` | 568 | indirect | (provisional) per-frame pass over the people for drawing (char type lookup tables) |
| `0x001484A0` | `void mm_CityRender_DrawSky(CityMap *map, CityScene *city, int pass, Vec4 *a3)` | 96 | no | (provisional) sky layer (+0x2F74) by the city clock (city +0x84), two variants by a2 |
| `0x00148500` | `void mm_CityRender_CollectObjects(CityMap *map, CityScene *city)` | 1088 | indirect | (provisional) collects instances for the objects on visible tiles |
| `0x00148950` | `void mm_CityRender_DrawWater(CityMap *map, CityScene *city)` | 20 | no | (provisional) water layer (+0x2F80) with city +0xB0 |
| `0x00148970` | `void mm_CityRender_DrawInstanceLayer(CityMap *map)` | 8 | no | layer +0x2F8C draw |
| `0x00148980` | `void mm_CityRender_DrawBuildings(CityMap *map, int pass)` | 36 | no | (provisional) layer +0x2F88: buildings on the visible tiles |
| `0x001489B0` | `void mm_CityRender_DrawOverlays(CityMap *map, CityScene *city)` | 136 | no | (provisional) queues the two-packet layers |
| `0x00148A40` | `CityMap *mm_CityRender_dtor(CityMap *map, int16_t deleteFlag)` | 364 | yes | vtable 0x1BCF00; deletes the seven layers and buffers, people-level destructor |
| `0x00148BB0` | `CityMap *mm_CityRender_ctor(CityMap *map)` | 436 | yes | people-level constructor, vtable 0x1BCF00; builds the terrain packets and the seven layers in the packet buffer |
| `0x00148D70` | `void mm_TexImage_Convert(TexImage *image, uint32_t textureBlock, uint32_t clutBlock)` | 856 | no | (image, textureBlock, clutBlock): DMA packet over the start of the image that uploads its palette and texels (PSMT4/PSMT8/32-bit, square) to the given GS blocks |
| `0x001490D0` | `int mm_Dma_KickVif1Now(uint32_t address)` | 8 | yes |  |
| `0x001490E0` | `TexImage *mm_TexImage_dtor(TexImage *image, int16_t deleteFlag)` | 100 | yes | frees the image data (+0x1000); delete when the flag is > 0 |
| `0x00149150` | `int mm_TexImage_Load(TexImage *image, char *name, int a2, int a3)` | 132 | yes | loads file a1 whole into +0x1000 and converts it (a2 x a3) |
| `0x001491E0` | `uint32_t *mm_LayerA_BuildPacket(LayerA *layer, uint32_t *packet)` | 280 | no | (provisional) packet for layer A (microcode 0x1A8330) |
| `0x00149300` | `void mm_LayerA_Draw(LayerA *layer)` | 256 | no | (provisional) queues layer A and its mesh instances |
| `0x00149400` | `void mm_LayerA_AddInstance(LayerA *layer, int a1, int a2, float x, float y, float z)` | 800 | no | (provisional) adds a rotated instance to layer A |
| `0x00149720` | `void mm_LayerA_SetSource(LayerA *layer, int source)` | 12 | no | +0x243C = a1, count +0x2430 = 0 |
| `0x00149730` | `LayerA *mm_LayerA_DeletingDtor(LayerA *layer, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x00149790` | `LayerA *mm_LayerA_ctor(LayerA *layer)` | 512 | no | base constructor, builds its packet in the packet buffer |
| `0x0014C200` | `void mm_Dma_AppendCall(int address)` | 8 | no |  |
| `0x0014C620` | `void mm_LightSet_UpdateByClock(LightSet *lights, int clock, int weather)` | 508 | no | interpolates the light colours over the day (2-hour segments, tables 0x1B0550/0x1B0670) |
| `0x0014C820` | `LightSet *mm_LightSet_DeletingDtor(LightSet *lights, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x0014C880` | `LightSet *mm_LightSet_ctor(LightSet *lights)` | 40 | no | mm_LightSet_Init |
| `0x0014C8E0` | `uint32_t *mm_LayerB_BuildPacket(LayerB *layer, uint32_t *packet)` | 372 | no | (provisional) packet for layer B (microcode 0x1AA340) |
| `0x0014CA60` | `void mm_LayerB_InitParams(LayerB *layer)` | 428 | no | (provisional) GS register defaults of layer B |
| `0x0014CC10` | `void mm_LayerB_Draw(LayerB *layer, int packet)` | 76 | no | (provisional) queues its two packets around a1 |
| `0x0014CC60` | `LayerB *mm_LayerB_DeletingDtor(LayerB *layer, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x0014CCC0` | `LayerB *mm_LayerB_ctor(LayerB *layer)` | 72 | no | base constructor, parameters, packet |
| `0x0014CD10` | `int mm_LayerC_InTileRect(LayerC *layer, int x, int z)` | 108 | no | 1 when the layer's tile rectangle is active (+0x21C == 1) and tile (a1, a2) lies inside x +0x220..+0x228, z +0x224..+0x22C; mm_Terrain_DrawPatch then sets flag 4 on the patch (was mm_Terrain_Nop14CD10, not empty) |
| `0x0014CD80` | `void mm_Terrain_SetVec(uint32_t *a0, int a1, uint32_t *a2, uint32_t *a3)` | 28 | no | (provisional) copies vec4 a3 into a0 with a1 |
| `0x0014CDA0` | `uint32_t *mm_LayerC_BuildPacket(LayerC *layer, uint32_t *packet)` | 584 | no | (provisional) packet for layer C (microcode 0x1A7B30/0x1A8330) |
| `0x0014CFF0` | `void mm_LayerC_InitParams(LayerC *layer)` | 252 | no | (provisional) |
| `0x0014D0F0` | `void mm_LayerC_DrawBuildings(LayerC *layer, int packet, CityMap *map, int pass)` | 548 | no | (provisional) building models on visible tiles (map objects, node packets) |
| `0x0014D320` | `LayerC *mm_LayerC_DeletingDtor(LayerC *layer, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x0014D380` | `LayerC *mm_LayerC_ctor(LayerC *layer)` | 88 | no | base constructor, parameters, packet |
| `0x0014D3E0` | `uint32_t *mm_LayerD_BuildPacket(LayerD *layer, uint32_t *packet, int32_t *a2)` | 1216 | no | (provisional) packet for layer D (microcode 0x1A8A00/0x1A8E20) |
| `0x0014D8A0` | `void mm_LayerD_InitParams(LayerD *layer)` | 1652 | no | (provisional) |
| `0x0014DF20` | `void mm_LayerD_DrawSkyByClock(LayerD *layer, int pass, int packet, int clock)` | 436 | no | (provisional) sky by the clock: day 5:00-19:00 variants |
| `0x0014E0E0` | `LayerD *mm_LayerD_DeletingDtor(LayerD *layer, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x0014E140` | `LayerD *mm_LayerD_ctor(LayerD *layer, int buffers)` | 88 | no | base constructor, parameters, packet |
| `0x0014E1A0` | `uint32_t *mm_LayerE_BuildPacket(LayerE *layer, uint32_t *packet, int32_t *a2)` | 1796 | no | (provisional) packet for layer E (microcode 0x1A9A90/0x1A9F10) |
| `0x0014E8B0` | `void mm_LayerE_InitParams(LayerE *layer)` | 1280 | no | (provisional) random placement with rotations (clouds or stars) |
| `0x0014EDB0` | `void mm_LayerE_Draw(LayerE *layer, int pass, int packet, int a3, Vec4 *t0)` | 752 | no | (provisional) projected layer draw |
| `0x0014F0A0` | `LayerE *mm_LayerE_DeletingDtor(LayerE *layer, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x0014F100` | `LayerE *mm_LayerE_ctor(LayerE *layer, int buffers)` | 88 | no | base constructor, parameters, packet |
| `0x0014F160` | `uint32_t *mm_LayerF_BuildPacket(LayerF *layer, uint32_t *packet)` | 296 | no | (provisional) packet for layer F (microcode 0x1A91C0) |
| `0x0014F290` | `void mm_LayerF_InitParams(LayerF *layer)` | 356 | no | (provisional) |
| `0x0014F400` | `void mm_LayerF_Draw(LayerF *layer, int packet)` | 48 | no | (provisional) queues its packet around a1 |
| `0x0014F430` | `LayerF *mm_LayerF_DeletingDtor(LayerF *layer, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x0014F490` | `LayerF *mm_LayerF_ctor(LayerF *layer)` | 72 | no | base constructor, parameters, packet |
| `0x0014F4E0` | `void mm_LayerG_AnimateWaves(LayerG *layer, float weather)` | 776 | no | (provisional) sine-wave animation state (0x1BDAD8..) |
| `0x0014F7F0` | `uint32_t *mm_LayerG_BuildPacket(LayerG *layer, uint32_t *packet, int32_t *a2)` | 1796 | no | (provisional) packet for layer G (microcode 0x1A6440) |
| `0x0014FF00` | `void mm_LayerG_InitParams(LayerG *layer)` | 392 | no | (provisional) |
| `0x00150090` | `void mm_LayerG_Draw(LayerG *layer, int pass, float weather)` | 64 | no | (provisional) animates the waves and queues packet a1 |
| `0x001500D0` | `LayerG *mm_LayerG_DeletingDtor(LayerG *layer, int16_t deleteFlag)` | 84 | yes | base destructor, then delete |
| `0x00150130` | `LayerG *mm_LayerG_ctor(LayerG *layer, int buffers)` | 88 | no | base constructor, parameters, packet |
| `0x00161290` | `int mm_TexSprite_SetFrame(TexSprite *sprite, int frameX, int frameY)` | 152 | no | first tile (+0x30/+0x34, within the image), trims the shown tiles to fit, then rebuilds |
| `0x00161330` | `int mm_TexSprite_SetClip(TexSprite *sprite, uint32_t width, uint32_t height)` | 124 | no | tiles shown (+0x28/+0x2C, up to the image's tiles past the frame), then rebuilds; mm_TexSprite_BuildPacket uses them as end tiles |
| `0x001613B0` | `void mm_TexSprite_GreenToAlpha(TexSprite *sprite)` | 148 | no | (provisional) palette pass: grey-green entries become translucent (logo texture) |
| `0x00161450` | `void mm_TexSprite_SetAlpha(TexSprite *sprite, uint8_t alpha, int keepClear)` | 108 | no | (provisional) palette alpha a1 |
| `0x001614C0` | `int mm_TexSprite_SetPosScale(TexSprite *sprite, int x, int y, int z, float scaleX, float scaleY)` | 24 | no | position a3..a5 and scale a0/a1, then rebuilds |
| `0x001614E0` | `void mm_TexSprite_Draw(TexSprite *sprite)` | 8 | no | queues the sprite packet (+8) |
| `0x001614F0` | `int mm_TexSprite_BuildPacket(TexSprite *sprite)` | 2152 | no | builds the sprite's DMA packet: palette upload to block 0x1E00, then per tile a texel upload, texture flush and a textured sprite (TEX0, ST, XYZ2); returns the scaled tile height |
| `0x00161D60` | `TexSprite *mm_TexSprite_dtor(TexSprite *sprite, int16_t deleteFlag)` | 136 | yes | vtable 0x1BCFD8, frees image and packet |
| `0x00161DF0` | `int mm_TexSprite_Load(TexSprite *sprite, int file, int a2)` | 260 | yes | loads a .TEX file whole and builds its packet |
| `0x00184030` | `int mm_DmaChain_Next(int reader)` | 208 | no | steps a DMA chain reader (+4 = current tag): CNT skips qwc+1, END stays, RET returns 0; caches tag id/qwc/words at +8..+0x24 |
| `0x00184100` | `int mm_DmaChain_Begin(int reader, int chain)` | 20 | no | reader starts at a1 with an END marker so the first mm_DmaChain_Next reads the first tag |
| `0x00184F60` | `void mm_Gs_InitDisplay(uint16_t a0, int a1)` | 652 | yes | (provisional) display and draw environments, double buffer, clears both buffers (from mm_MainLoop) |
| `0x00186D00` | `void mm_PacketBuf_Seek(int address)` | 8 | no | sets the packet buffer write pointer (0x1BDCE8) |
| `0x00186D10` | `int mm_PacketBuf_Tell(void)` | 8 | no | returns the packet buffer write pointer (0x1BDCE8) |
| `0x00186D20` | `int mm_Dma_KickVif1Now(uint32_t address)` | 192 | yes | sends a two-tag chain (CALL a0, END) on VIF1 right away and waits for GS path idle before and after |
| `0x00186DE0` | `void mm_Dma_AppendCall(int address)` | 116 | no | appends a DMA CALL tag (id 5) to address a0 at the gp list pointer, then an END tag the next append overwrites; at most 0x4000 qwords per list |
| `0x00186E60` | `void mm_DmaList_QueueGsDefaults(void)` | 124 | no | CALLs the static packet 0x1B2760: standard alpha blend in ALPHA_1/2, TEST_1/2 alpha != 0 and Z >= |
| `0x00186EE0` | `void mm_DmaList_QueueOtherOverlay(void)` | 152 | no | CALLs the other one of the two 0xBA0-byte packets at 0x1F69C0 built by 0x1873D0 |
| `0x00186F80` | `void mm_DmaList_QueueClearScreen(void)` | 124 | no | CALLs the static packet 0x1B28E0: full-screen sprite writing colour 0 and Z 0 |
| `0x00187000` | `void mm_DmaList_QueueFade(int alpha, int a1)` | 272 | no | full-screen black sprite with alpha a0 (patched into 0x1B2830/0x1B28A0); a1 == 0 draws it at Z 0xFFFFF000 (0x1B2800), else at Z max (0x1B2870) |
| `0x00187110` | `void mm_DmaList_QueueClearZ(void)` | 124 | no | CALLs the static packet 0x1B26D0: full-screen sprite that only writes Z = 0 (alpha test NEVER, AFAIL = ZB_ONLY) |
| `0x00187190` | `void mm_DmaList_Rewind(void)` | 56 | yes | waits for GS path idle, then resets the write pointer to the start of the current list |
| `0x001871D0` | `void mm_DmaList_Flip(void)` | 40 | no | switches to the other of the two 64 KB lists (0x1BD9C0/0x1BD9C4) |
| `0x00187200` | `void mm_DmaList_BeginScene(void)` | 132 | no | rewinds the 0x8000-byte packet buffer (0x1BDCE4 -> 0x1BDCE8) and queues the default GS state |
| `0x00187290` | `void mm_DmaList_KickFrame(void)` | 220 | no | appends the default GS state, then starts the current list on VIF1 (chain mode); the city's VU1/GS work happens under this kick |
| `0x00187370` | `void mm_DmaList_Free(void)` | 92 | yes | frees both lists and the packet buffer |
| `0x001873D0` | `void mm_DmaList_InitPackets(void)` | 616 | no | builds two static packets at 0x1F69C0 that draw a 640 x 224 picture from GS block 0x8000 (+0x8C0) as 5 x 7 sprites of 128 x 32 |
| `0x00187640` | `void mm_DmaList_Init(void)` | 104 | yes | allocates the two 64 KB lists and the 0x8000-byte packet buffer, then builds the overlay packets (0x1873D0) |
