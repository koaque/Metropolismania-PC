/* Structures of the SDK libraries that the platform layer (src/port/platform/) works on. */

typedef struct GifPacket GifPacket;
typedef struct GsDrawEnv GsDrawEnv;

/* The state of a GIF packet being built (libpkt, 0x10 bytes): words are written at the cursor, a DMA tag and a GIF tag can be open. */
struct GifPacket {
    uint32_t *cursor;          /* +0x00 where the next word goes */
    uint32_t *base;            /* +0x04 the start of the buffer, which a reset goes back to */
    uint32_t *dmaTag;          /* +0x08 the open DMA tag (0 = none): its quadword count is added when the packet moves on */
    uint32_t *gifTag;          /* +0x0C the open GIF tag */
};

/* The part of a GS drawing environment (libgraph, one context) that the half pixel offset touches: the registers are 64 bit words
   followed by the address of the register they go to. The environment goes on after the scissor. */
struct GsDrawEnv {
    uint64_t frame;            /* +0x00 */
    uint64_t frameAddress;     /* +0x08 */
    uint64_t zbuf;             /* +0x10 */
    uint64_t zbufAddress;      /* +0x18 */
    uint64_t xyoffset;         /* +0x20 the drawing offset, 12.4 fixed point: x in bits 0 to 15, y in bits 32 to 47 */
    uint64_t xyoffsetAddress;  /* +0x28 */
    uint64_t scissor;          /* +0x30 the drawing area: x1 in bits 16 to 26, y1 in bits 48 to 58 */
};
