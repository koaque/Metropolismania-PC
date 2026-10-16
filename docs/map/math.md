# math

Vector and matrix helpers (VU0 macro code in the original), line/plane and triangle collision tests.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

14 functions (named 14). Blocks: 0 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Name | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00128CC0` | `mm_Mat4_Copy` | 36 | no | copies a 4x4 float matrix (16 words) from a1 to a0 |
| `0x00129C10` | `mm_Mat4_MulVu0` | 116 | no | a0 = a1 x a2 (4x4, VU0 macro mode) |
| `0x0012AB10` | `mm_Vec3_Normalize` | 36 | no | a0 = a1 / \|a1\| (VU0, xyz) |
| `0x00136CB0` | `mm_Math_ProjectPoint` | 60 | no | projects a 3D point with the given matrices (VU0) |
| `0x00141890` | `mm_Math_TriangleHeight` | 60 | no | (provisional) VU0 helper for mm_Map_SampleHeight |
| `0x0015EDE0` | `mm_Math_Rsqrt` | 48 | no | (provisional) VU0 reciprocal square root of a0 |
| `0x00164740` | `mm_Math_MulMatVecVu0` | 44 | no | (provisional) VU0 matrix x vector |
| `0x00182BF0` | `mm_Collide_RayTriangleA` | 304 | no | (provisional) ray against a triangle's plane, then inside test |
| `0x00182D20` | `mm_Collide_PointInTriangle` | 356 | no | (provisional) same-side cross product test |
| `0x00182E90` | `mm_Collide_RayTriangleB` | 400 | no | (provisional) second ray/triangle test |
| `0x001859D0` | `mm_Math_LinePlaneIntersect` | 724 | no | intersection of a line with plane a0..a3 (a,b,c,d), handles axis-parallel cases |
| `0x00185CB0` | `mm_Math_RayPlaneDistance` | 228 | no | (provisional) normalised ray against a plane, distance to the hit |
| `0x00185DA0` | `mm_Math_ProjectionMatrix` | 204 | no | perspective matrix into a3: m0 = 2n/w, m5 = 2n/h, m10 = (f+n)/(f-n), m14 = -2fn/(f-n), m11 = 1, from a1 = near, a2 = far and the screen size in a4 |
| `0x00185E70` | `mm_Math_Log2` | 56 | no | floor(log2(a0)) for a0 >= 1 |
