/* Math types shared by the game's structs; parsed before the classes that embed them. */

typedef struct Vec4 Vec4;
typedef struct TileRect TileRect;

/* VU0-style vector (0x10 bytes); a 4x4 matrix is four of them. */
struct Vec4 {
    float x;                   /* +0x00 */
    float y;                   /* +0x04 */
    float z;                   /* +0x08 */
    float w;                   /* +0x0C */
};

/* A block of map tiles (0x10 bytes): its corner columns (x) and rows (z). Whether x1 and z1 lie inside the block or
   one past it is the function's own (mm_Map_UpdateAreaGeometry: inside). */
struct TileRect {
    int32_t x0;                /* +0x00 */
    int32_t z0;                /* +0x04 */
    int32_t x1;                /* +0x08 */
    int32_t z1;                /* +0x0C */
};
