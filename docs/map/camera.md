# camera

The generic camera (`mm_Camera_*`: eye, target, view and projection matrices), the city camera
(`mm_CityCamera_*`: zoom levels, and the time-of-day sky colour, fog and light directions from the tables at
`0x001B0E30`) and the sky dome (`mm_Sky_*`, loaded from `DATA\A3D\SKY.A3D`).

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

18 functions (verified 18). Blocks: 5 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0012A290` | `void mm_Camera_SetEyeTarget(Camera *camera, Vec4 *eye, Vec4 *target)` | 24 | no | stores the eye and target vectors |
| `0x0012A3A0` | `void mm_Camera_UpdateProjection(Camera *camera)` | 572 | no | builds the screen matrices (screenA, screenB, screenC) from the view parameters (depthMax, depthMin, zNear, zFar, distance, view size, scale and centre) |
| `0x0012A5E0` | `void mm_Camera_SetFogRange(Camera *camera, float fogNear, float fogFar)` | 108 | no | fog near and far (fogNear, fogFar) and the fog coefficients (the w of fogA and fogB) for a 0..255 fog value |
| `0x0012A660` | `void mm_Camera_UpdateLightMatrices(Camera *camera)` | 88 | no | sceVu0NormalLightMatrix from the three light directions (lightDir) and sceVu0LightColorMatrix from the light colours (lightRgb) and the ambient |
| `0x0012A6C0` | `void mm_Camera_UpdateMatrices(Camera *camera)` | 136 | no | dir = normalize(target - eye); sceVu0CameraMatrix into view with the up vector; worldScreenA = screenA x view, worldScreenB = screenB x view |
| `0x0012A750` | `Camera *mm_Camera_DeletingDtor(Camera *camera, int16_t deleteFlag)` | 84 | yes | base destructor, then delete when the flag is > 0 |
| `0x0012A7B0` | `Camera *mm_Camera_ctor(Camera *camera)` | 856 | no | default light directions/colours, eye/target, fog and projection, then all matrices |
| `0x0012CA20` | `void mm_CityCamera_LoadState(CityCamera *camera, McManager *mc)` | 76 | no | reads the zoom level (4 bytes) and the camera from its eye on (0x1D4 bytes) from the save file |
| `0x0012CA70` | `void mm_CityCamera_SaveState(CityCamera *camera, McManager *mc)` | 76 | no | writes the same two blocks |
| `0x0012CAC0` | `int mm_CityCamera_GetZoomLevel(CityCamera *camera, CityScene *city)` | 192 | no | height of the eye above the terrain (0x1415E0) as a zoom level: 0 above -1.15, 1 above -7.1, ... |
| `0x0012CB80` | `void mm_CityCamera_Update(CityCamera *camera, CityScene *city)` | 2688 | no | pad-driven city camera: move, rotate, zoom, keeps the eye above the terrain, updates fog, projection and matrices |
| `0x0012D600` | `CityCamera *mm_CityCamera_DeletingDtor(CityCamera *camera, int16_t deleteFlag)` | 92 | yes | camera destructor, then delete when the flag is > 0 |
| `0x0012D660` | `CityCamera *mm_CityCamera_ctor(CityCamera *camera)` | 80 | yes | camera constructor (0x161250) with eye (0, -29, 1.6), zoom level 1 |
| `0x00160040` | `void mm_CityCamera_UpdateTimeOfDay(CityCamera *camera, CityScene *city)` | 2544 | no | lights and sky by the clock: colours blended between two-hour slot tables (0x1B0E30.. 0x1B12B0), sun directions turned by the day's angle, sky packet colours, horizon, scroll and shimmer |
| `0x00160A30` | `void mm_Sky_Draw(CityCamera *camera)` | 56 | no | queues the city camera's three sky packets (skyBands, skyGlow, skyPicture) |
| `0x00160A70` | `void mm_Sky_BuildPackets(CityCamera *camera)` | 1660 | no | builds the sky packets (texture sizes via mm_Math_Log2) |
| `0x001610F0` | `void mm_Sky_Load(void)` | 344 | yes | loads DATA\A3D\SKY.A3D and uploads its textures |
| `0x00161250` | `CityCamera *mm_CityCamera_BaseCtor(CityCamera *camera)` | 52 | yes | camera constructor plus the sky |
