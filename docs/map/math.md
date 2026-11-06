# math

Vector and matrix helpers (VU0 macro code in the original), line/plane and triangle collision tests.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

14 functions (verified 14). Blocks: 0 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00128CC0` | `void mm_Mat4_Copy(Vec4 *dst, Vec4 *src)` | 36 | no | (dst, src) copies a 4x4 float matrix (16 words) from `src` to `dst` |
| `0x00129C10` | `void mm_Mat4_MulVu0(Vec4 *out, Vec4 *a, Vec4 *b)` | 116 | no | (out, a, b) `out` = `a` x `b` (4x4, VU0 macro mode) |
| `0x0012AB10` | `void mm_Vec3_Normalize(Vec4 *out, Vec4 *v)` | 36 | no | (out, v) `out` = `v` / \|`v`\| (VU0, xyz) |
| `0x00136CB0` | `void mm_Math_ProjectPoint(int unused_a0, Vec4 *out, Vec4 *point, Vec4 *matrix)` | 60 | no | projects a 3D point with the given matrices (VU0) |
| `0x00141890` | `void mm_Math_TriangleNormal(Vec4 *out, Vec4 *a, Vec4 *b, Vec4 *c)` | 60 | no | unit normal of triangle (a, b, c) in out.xyz via VU0; out.w = (w of vf6 as earlier VU0 code left it) squared (used by mm_Map_SampleHeight) |
| `0x0015EDE0` | `uint32_t mm_Math_SqrtBits(int value)` | 48 | no | (value) bit pattern of sqrtf((float)`value`) (VU0 VSQRT), returned in v0; mm_Person_ChooseDestination uses it mod 3600 as a per-person time offset |
| `0x00164740` | `void mm_Math_MulMatVecVu0(Vec4 *out, Vec4 *matrix, Vec4 *v)` | 44 | no | out = v x matrix (row vector) |
| `0x00182BF0` | `int mm_Collide_NormalRayTriangle(Vec4 *origin, Vec4 *a, Vec4 *b, Vec4 *c, Vec4 *hit, float *distance, float range)` | 304 | no | whether the line from the origin along the triangle's normal meets triangle (a, b, c) closer than the range: the plane, the crossing into hit, its distance, then the inside test |
| `0x00182D20` | `int mm_Collide_PointInTriangle(Vec4 *p, Vec4 *a, Vec4 *b, Vec4 *c)` | 356 | no | whether point p lies inside triangle (a, b, c), on VU0: the normals (a - p) x (c - p), (b - a) x (c - a) and (p - b) x (c - b) must all face the same way (no dot product of neighbours below 0) |
| `0x00182E90` | `int mm_Collide_SegmentTriangle(Vec4 *start, Vec4 *end, Vec4 *a, Vec4 *b, Vec4 *c, Vec4 *hit)` | 400 | no | whether the segment from start to end crosses triangle (a, b, c): the crossing with the plane must lie between the ends and inside the triangle |
| `0x001859D0` | `int mm_Math_LinePlaneIntersect(float *point, float *out, float dx, float dy, float dz, float a, float b, float c, float d)` | 724 | no | (point, out, dx, dy, dz, a, b, c, d) where the line through `point` with direction (`dx`, `dy`, `dz`) meets the plane a x + b y + c z + d = 0: *`out` = (x, y, z, 1) and 1, or 0 when the line runs parallel to it; zero direction components take their own formulas |
| `0x00185CB0` | `float mm_Math_RayPlaneDistance(Vec4 *from, Vec4 *to, Vec4 *planePoint, Vec4 *hit, float *distance)` | 228 | no | (from, to, planePoint, hit, *distance): where the ray from `from` towards `to` meets the plane through planePoint facing along it; returns (f0) and stores the distance from planePoint to that point, v0 = 1 |
| `0x00185DA0` | `void mm_Math_ProjectionMatrix(float *matrix, float *screen, float distance, float zNear, float zFar)` | 204 | no | (matrix, screen, distance, zNear, zFar) a perspective matrix into `matrix`: m0 = 2n / w and m5 = 2n / h, w and h being twice `screen`[0] and `screen`[1] scaled by `zNear` / `distance`; m10 = (f + n) / (f - n), m14 = -2fn / (f - n), m11 = 1, m15 = 0, with n = `zNear` and f = `zFar` |
| `0x00185E70` | `int mm_Math_Log2(int value)` | 56 | no | (value) floor(log2(`value`)) for `value` >= 1 |
