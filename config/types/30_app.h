/* The application object: what mm_Main builds on its stack and mm_MainLoop drives. */

typedef struct App App;
typedef struct Game Game;

/* The application base (vtable 0x1BD8D0): slot +8 starts it, slot +0xC runs one frame. mm_App_ctor only sets the vtable. */
struct App {
    void *vtable;              /* +0x00 */
};

/* The game, the application with vtable 0x1BCE60 (slot +8 mm_GameInit, slot +0xC mm_GameUpdate): the state machine that
   makes and runs one scene at a time. */
struct Game {
    void *vtable;              /* +0x00 */
    int32_t state;             /* +0x04 0 and 1 make the title scene, 2 runs it, 3 and 7 make the briefing, 4 runs it, 5 makes the city, 6 runs it */
    CityScene *city;           /* +0x08 */
    SceneA *title;             /* +0x0C */
    int32_t unk_10;            /* +0x10 cleared by mm_GameInit */
    SceneB *briefing;          /* +0x14 */
};
