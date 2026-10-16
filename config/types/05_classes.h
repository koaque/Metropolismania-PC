/* Game classes whose layout is not mapped yet: named types at their real allocation sizes (from the
   operator new call before each constructor), so prototypes and decompiles carry the class name.
   Fields replace the data arrays as functions are ported. Parsed before 10_game.h, which points at them. */

typedef struct TextBox TextBox;
typedef struct Frame Frame;
typedef struct A3DObject A3DObject;
typedef struct Camera Camera;
typedef struct CityCamera CityCamera;
typedef struct GameFile GameFile;
typedef struct TexSprite TexSprite;
typedef struct LightSet LightSet;
typedef struct ListMenu ListMenu;
typedef struct LoadingScreen LoadingScreen;
typedef struct McManager McManager;
typedef struct NameEntry NameEntry;
typedef struct SceneA SceneA;
typedef struct Bag Bag;
typedef struct StatusPanel StatusPanel;
typedef struct PauseMenu PauseMenu;
typedef struct Minimap Minimap;
typedef struct CityCursor CityCursor;
typedef struct PhoneCalls PhoneCalls;
typedef struct EventMgr EventMgr;
typedef struct WantsWin WantsWin;
typedef struct Laptop Laptop;
typedef struct Player Player;
typedef struct Applicants Applicants;
typedef struct ApplicantEntry ApplicantEntry;
typedef struct Markers Markers;
typedef struct Notice Notice;
typedef struct TownPages TownPages;
typedef struct EventData EventData;
typedef struct CharClass CharClass;
typedef struct TexImage TexImage;
typedef struct LayerA LayerA;
typedef struct LayerB LayerB;
typedef struct LayerC LayerC;
typedef struct LayerD LayerD;
typedef struct LayerE LayerE;
typedef struct LayerF LayerF;
typedef struct LayerG LayerG;

/* Text box (0x40 bytes), mm_TextBox_Init builds one. */
struct TextBox {
    uint8_t unk_00[0x2C];
    int32_t cursor;            /* +0x2C reset by mm_TextBox_SetText */
    uint8_t unk_30[0x10];
};
/* Window frame (0xC0 bytes). */
struct Frame {
    uint8_t unk_00[0x60];
    uint32_t cornerA;          /* +0x60 GS XY of the top left (mm_Frame_SetRect), or colour bytes (mm_Frame_SetColor) */
    uint8_t unk_64[0x1C];
    uint32_t cornerB;          /* +0x80 GS XY of the bottom right, or colour bytes */
    uint8_t unk_84[0x20];
    int32_t x;                 /* +0xA4 */
    int32_t y;                 /* +0xA8 */
    int32_t width;             /* +0xAC */
    int32_t height;            /* +0xB0 */
    uint8_t unk_B4[0xC];
};
/* Model instance from a .A3D file (0x60 bytes). */
struct A3DObject {
    void *model;               /* +0x00 the loaded .A3D data (node array at +8) */
    uint8_t unk_04[0xC];
    Vec4 matrix[4];            /* +0x10 object matrix (mm_Player_GetMatrix) */
    void *meshes;              /* +0x50 table of mesh pointers (mm_A3D_GetMeshField0C) */
    uint8_t unk_54[0xC];
};
/* Camera (0x310 bytes). */
struct Camera {
    uint8_t unk_000[0x110];
    Vec4 fogA;                 /* +0x110 (0, 0, 0, fog coefficient) from mm_Camera_SetFogRange */
    Vec4 fogB;                 /* +0x120 (0, 0, 0, fog coefficient) */
    Vec4 eye;                  /* +0x130 */
    Vec4 target;               /* +0x140 */
    uint8_t unk_150[0x1B4];
    float fogFar;              /* +0x304 */
    float fogNear;             /* +0x308 */
    uint8_t unk_30C[4];
};
/* City camera with the sky (0x540 bytes): a camera plus the zoom level and sky packets. */
struct CityCamera {
    Camera base;
    uint8_t unk_310[0x220];
    int32_t zoomLevel;         /* +0x530 0..3, L1/R1 and L2/R2 */
    uint8_t unk_534[0xC];
};
/* Disc file handle (0x8 bytes), mm_File_*. */
struct GameFile {
    int32_t fd;                /* +0x00 -1 when closed */
    int32_t async;             /* +0x04 requests go through the file queue */
};
/* Sprite from a .TEX file (0x38 bytes). */
struct TexSprite { uint8_t data[0x38]; };
/* Light set (0x1B0 bytes). */
struct LightSet {
    void *unk_00;              /* +0x00 object whose +0x20 mm_LightSet_SetField20 sets */
    uint8_t unk_04[0x1AC];
};
/* List menu widget (0x2A0 bytes). */
struct ListMenu { uint8_t data[0x2A0]; };
/* Loading screen (0x28 bytes). */
struct LoadingScreen { uint8_t data[0x28]; };
/* Memory card manager (0x28 bytes). */
struct McManager {
    void *queue;               /* +0x00 memory card request queue object */
    int32_t state;             /* +0x04 step of the current operation */
    uint8_t unk_08[0xC];
    int32_t request;           /* +0x14 last queued request */
    uint8_t unk_18[8];
    GameFile *file;            /* +0x20 */
    int32_t fd;                /* +0x24 open save file descriptor */
};
/* Name entry window (0x124 bytes). */
struct NameEntry { uint8_t data[0x124]; };
/* Scene A: logos, memory card check, title (0x104 bytes). */
struct SceneA { uint8_t data[0x104]; };
/* Player's bag (0x1AC bytes). */
struct Bag { uint8_t data[0x1AC]; };
/* Status panel (0xC0 bytes). */
struct StatusPanel { uint8_t data[0xC0]; };
/* Pause and save menu (0x99C bytes). */
struct PauseMenu { uint8_t data[0x99C]; };
/* Minimap (0x21DC bytes). */
struct Minimap { uint8_t data[0x21DC]; };
/* City cursor (0x1E0 bytes). */
struct CityCursor {
    uint8_t unk_000[0x1A4];
    int32_t targetId;          /* +0x1A4 */
    uint16_t targetKind;       /* +0x1A8 */
    uint8_t unk_1AA[6];
    Vec4 targetPos;            /* +0x1B0 */
    uint8_t unk_1C0[0x20];
};
/* Phone call manager (0x7C bytes). */
struct PhoneCalls { uint8_t data[0x7C]; };
/* Event manager (0xB0 bytes); +0xA4 and +0xA8 are the events the talk script works with. */
struct EventMgr { uint8_t data[0xB0]; };
/* Wants window (0x544 bytes); starts with the 0x504-byte wants list. */
struct WantsWin {
    uint16_t counters[2];      /* +0x00 cleared by mm_Wants_ctor */
    uint8_t unk_004[0x540];
};
/* Laptop menu (0x3E0 bytes). */
struct Laptop { uint8_t data[0x3E0]; };
/* Player character (0x90 bytes). */
struct Player {
    int32_t mode;              /* +0x00 0 turn, 1 walk (Cross lays road), 2 place for applicant, 5 place bag item */
    uint8_t unk_04[0xC];
    Vec4 position;             /* +0x10 */
    uint8_t unk_20[0x2C];
    float jumpSpeed;           /* +0x4C */
    uint8_t unk_50[0x34];
    int32_t jumps;             /* +0x84 */
    A3DObject *model;          /* +0x88 */
    uint8_t unk_8C[4];
};
/* Applicant list entry (0x14 bytes). */
struct ApplicantEntry {
    uint16_t index;            /* +0x00 own index */
    int16_t state;             /* +0x02 0 free, 2 waiting */
    uint16_t family;           /* +0x04 */
    uint8_t unk_06[0xE];
};
/* Applicants list (0x350 bytes). */
struct Applicants {
    ApplicantEntry entries[32]; /* +0x000 */
    int32_t count;             /* +0x280 */
    int32_t unk_284;           /* +0x284 */
    uint8_t unk_288[0xC8];
};
/* Markers model (0x130 bytes). */
struct Markers { uint8_t data[0x130]; };
/* Notice line (0x14 bytes). */
struct Notice {
    int32_t id;                /* +0x00 shown notice, -1 none */
    int32_t timer;             /* +0x04 frames left */
    uint8_t unk_08[8];
    int32_t unk_10;            /* +0x10 */
};
/* Town's Pages phone book (0x64 bytes). */
struct TownPages {
    uint8_t unk_00[0x40];
    int32_t pageEntries;       /* +0x40 entries on the current page (up to 10) */
    uint8_t unk_44[0x20];
};
/* Per-event handler data (base 0x1C bytes; some handlers use 0x7C), the first argument of every mm_Ev*_ slot. */
struct EventData { uint8_t data[0x1C]; };
/* Character type class object (0x24 bytes; the person class is 0x28 with its walking speed at +0x24),
   one per CHARTYPE.BIN record in the table at gp 0x1BDA3C. */
struct CharClass { uint8_t data[0x24]; };
/* Texture image loaded whole and converted for upload (0x1010 bytes, often on the stack): image data pointer at +0x1000. */
struct TexImage { uint8_t data[0x1010]; };
/* City render layers, built by mm_CityRender_ctor, each with its own VU1 packet (names provisional).
   A: rotated mesh instances (0x2440); B: two-packet layer (0x230); C: buildings on visible tiles (0x240);
   D: sky by the clock (0x410); E: projected, randomly placed sprites (0x6E0); F: single packet (0x140);
   G: animated water waves (0x2E0). */
struct LayerA {
    uint8_t unk_0000[0x2430];
    int32_t count;             /* +0x2430 instances this frame */
    uint8_t unk_2434[8];
    int32_t source;            /* +0x243C */
};
struct LayerB { uint8_t data[0x230]; };
struct LayerC {
    uint8_t unk_000[0x21C];
    int32_t rectActive;        /* +0x21C 1: only tiles inside the rectangle (mm_LayerC_InTileRect) */
    int32_t rectX0;            /* +0x220 */
    int32_t rectZ0;            /* +0x224 */
    int32_t rectX1;            /* +0x228 */
    int32_t rectZ1;            /* +0x22C */
    uint8_t unk_230[0x10];
};
struct LayerD { uint8_t data[0x410]; };
struct LayerE { uint8_t data[0x6E0]; };
struct LayerF { uint8_t data[0x140]; };
struct LayerG { uint8_t data[0x2E0]; };
