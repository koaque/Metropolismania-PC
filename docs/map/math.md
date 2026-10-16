# math

Vector and matrix helpers (VU0 macro code in the original), line/plane and triangle collision tests.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

14 functions (typed 12, verified 2). Blocks: 0 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00128CC0` | `void mm_Mat4_Copy(Vec4 *dst, Vec4 *src)` | 36 | no | copies a 4x4 float matrix (16 words) from a1 to a0 |
| `0x00129C10` | `void mm_Mat4_MulVu0(Vec4 *out, Vec4 *a, Vec4 *b)` | 116 | no | a0 = a1 x a2 (4x4, VU0 macro mode) |
| `0x0012AB10` | `void mm_Vec3_Normalize(Vec4 *out, Vec4 *v)` | 36 | no | a0 = a1 / \|a1\| (VU0, xyz) |
| `0x00136CB0` | `void mm_Math_ProjectPoint(int unused_a0, Vec4 *out, Vec4 *point, Vec4 *matrix)` | 60 | no | projects a 3D point with the given matrices (VU0) |
| `0x00141890` | `void mm_Math_TriangleHeight(Vec4 *a0, Vec4 *a1, Vec4 *a2, Vec4 *a3)` | 60 | no | (provisional) VU0 helper for mm_Map_SampleHeight |
| `0x0015EDE0` | `uint32_t mm_Math_SqrtBits(int value)` | 48 | no | bit pattern of sqrtf((float)a0) (VU0 VSQRT), returned in v0; mm_Person_ChooseDestination uses it mod 3600 as a per-person time offset |
| `0x00164740` | `void mm_Math_MulMatVecVu0(Vec4 *out, Vec4 *matrix, Vec4 *v)` | 44 | no | (provisional) VU0 matrix x vector |
| `0x00182BF0` | `int mm_Collide_RayTriangleA(int a0, Vec4 *a1, Vec4 *a2, Vec4 *a3, int t0, float *t1, float f12)` | 304 | no | (provisional) ray against a triangle's plane, then inside test |
| `0x00182D20` | `int mm_Collide_PointInTriangle(Vec4 *a0, Vec4 *a1, Vec4 *a2, Vec4 *a3)` | 356 | no | (provisional) same-side cross product test |
| `0x00182E90` | `int mm_Collide_RayTriangleB(int a0, Vec4 *a1, Vec4 *a2, Vec4 *a3, Vec4 *t0, int t1)` | 400 | no | (provisional) second ray/triangle test |
| `0x001859D0` | `float mm_Math_LinePlaneIntersect(float *point, float *out, float dx, float dy, float dz, float a, float b, float c, float d)` | 724 | no | intersection of a line with plane a0..a3 (a,b,c,d), handles axis-parallel cases |
| `0x00185CB0` | `float mm_Math_RayPlaneDistance(Vec4 *a0, Vec4 *a1, Vec4 *a2, int a3, uint32_t *t0)` | 228 | no | (provisional) normalised ray against a plane, distance to the hit |
| `0x00185DA0` | `void mm_Math_ProjectionMatrix(float *matrix, float *screen, float distance, float zNear, float zFar)` | 204 | no | perspective matrix into a3: m0 = 2n/w, m5 = 2n/h, m10 = (f+n)/(f-n), m14 = -2fn/(f-n), m11 = 1, from a1 = near, a2 = far and the screen size in a4 |
| `0x00185E70` | `int mm_Math_Log2(int value)` | 56 | no | floor(log2(a0)) for a0 >= 1 |
