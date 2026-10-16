/* Math types shared by the game's structs; parsed before the classes that embed them. */

typedef struct Vec4 Vec4;

/* VU0-style vector (0x10 bytes); a 4x4 matrix is four of them. */
struct Vec4 {
    float x;                   /* +0x00 */
    float y;                   /* +0x04 */
    float z;                   /* +0x08 */
    float w;                   /* +0x0C */
};
