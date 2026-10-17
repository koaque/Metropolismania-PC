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
typedef struct InstanceSlot InstanceSlot;
typedef struct ListMenu ListMenu;
typedef struct ListMenuItem ListMenuItem;
typedef struct LoadingScreen LoadingScreen;
typedef struct McManager McManager;
typedef struct NameEntry NameEntry;
typedef struct SceneA SceneA;
typedef struct Bag Bag;
typedef struct BagShortcut BagShortcut;
typedef struct BagSlot BagSlot;
typedef struct StatusPanel StatusPanel;
typedef struct PauseMenu PauseMenu;
typedef struct Minimap Minimap;
typedef struct CityCursor CityCursor;
typedef struct CityCursorView CityCursorView;
typedef struct PhoneCalls PhoneCalls;
typedef struct PhoneCallEntry PhoneCallEntry;
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
typedef struct PersonClass PersonClass;
typedef struct StageName StageName;
typedef struct StageState StageState;
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
    uint8_t unk_00[8];
    uint32_t packet;           /* +0x08 GS packet the box draws (mm_TextBox_Draw) */
    int32_t style;             /* +0x0C passed to mm_TextBox_Build */
    int32_t charWidth;         /* +0x10 */
    int32_t charHeight;        /* +0x14 */
    int32_t x;                 /* +0x18 GS units */
    int32_t y;                 /* +0x1C */
    int32_t z;                 /* +0x20 */
    int32_t pitchX;            /* +0x24 */
    int32_t pitchY;            /* +0x28 */
    int32_t cursor;            /* +0x2C reset by mm_TextBox_SetText */
    uint8_t unk_30[0x10];
};
/* Window frame (0xC0 bytes). */
struct Frame {
    uint8_t unk_00[0x50];
    float stA[2];              /* +0x50 texture S, T of the top left (mm_Frame_SetStyle) */
    uint8_t unk_58[8];
    uint32_t cornerA;          /* +0x60 GS XY of the top left (mm_Frame_SetRect), or colour bytes (mm_Frame_SetColor) */
    uint32_t colourA;          /* +0x64 (mm_Frame_ctor) */
    uint8_t unk_68[8];
    float stB[2];              /* +0x70 texture S, T of the bottom right */
    uint8_t unk_78[8];
    uint32_t cornerB;          /* +0x80 GS XY of the bottom right, or colour bytes */
    uint32_t colourB;          /* +0x84 */
    uint8_t unk_88[0x18];
    int32_t style;             /* +0xA0 icon cell of the frame texture (mm_Frame_SetStyle) */
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
struct TexSprite {
    uint8_t unk_00[4];
    void *image;               /* +0x04 TEX image: +4 palette entries, +0x10 width, +0x12 height, colours from +0x20 */
    uint32_t packet;           /* +0x08 GS packet (mm_TexSprite_Draw) */
    int32_t x;                 /* +0x0C */
    int32_t y;                 /* +0x10 */
    int32_t z;                 /* +0x14 */
    float scaleX;              /* +0x18 */
    float scaleY;              /* +0x1C */
    uint8_t unk_20[0x18];
};
/* Model instance slot (0x70 bytes): one per mesh drawn this frame, at most 0x708, in two buffers by frame. */
struct InstanceSlot {
    uint8_t header[0x10];      /* +0x00 prebuilt packet header */
    Vec4 colour;               /* +0x10 */
    Vec4 matrix[4];            /* +0x20 the last word (+0x5C) carries the caller's flags */
    uint8_t trailer[0x10];
};
/* One parity's half of the city cursor (0xD0 bytes): info lines, packets and the fade frame. */
struct CityCursorView {
    TextBox *lines[5];         /* +0x00 */
    uint32_t linePackets[5];   /* +0x14 */
    uint32_t packet;           /* +0x28 cursor packet */
    Frame *fadeFrame;          /* +0x2C */
    uint8_t extraPacket[0xA0]; /* +0x30 drawn in mode 4 */
};
/* Light set (0x1B0 bytes). */
struct LightSet {
    void *unk_00;              /* +0x00 object whose +0x20 mm_LightSet_SetField20 sets */
    uint8_t unk_04[0x1AC];
};
/* List menu item (0xC bytes). */
struct ListMenuItem {
    int32_t textHeight;        /* +0x00 centres the text in the item (mm_ListMenu_Draw) */
    TextBox *text;             /* +0x04 */
    int32_t unk_08;
};
/* List menu widget (0x2A0 bytes). */
struct ListMenu {
    uint8_t unk_000[0x40];
    uint32_t cursorPacket[0x20]; /* +0x040 GS packet of the cursor bar (mm_ListMenu_SetCursor) */
    uint8_t unk_0C0[0x20];
    Frame *cursorFrame;        /* +0x0E0 */
    uint8_t unk_0E4[4];
    ListMenuItem items[31];    /* +0x0E8 (the code accepts item numbers up to 31) */
    uint8_t unk_25C[8];
    int32_t visibleItems;      /* +0x264 */
    int32_t itemHeight;        /* +0x268 */
    int32_t unk_26C;           /* +0x26C */
    int32_t scroll;            /* +0x270 */
    int32_t cursorDx;          /* +0x274 */
    int32_t cursorDy;          /* +0x278 */
    int32_t cursorY;           /* +0x27C */
    int32_t depth;             /* +0x280 GS Z of the item texts */
    uint8_t unk_284[8];
    int32_t nextItem;          /* +0x28C item the next mm_ListMenu_SetItemText(-1) fills */
    int32_t itemCount;         /* +0x290 */
    uint8_t unk_294[0xC];
};
/* Loading screen (0x28 bytes). */
struct LoadingScreen {
    uint8_t unk_00[4];
    TexSprite *sprite;         /* +0x04 */
    TextBox *text;             /* +0x08 */
    uint8_t unk_0C[8];
    int32_t spriteId;          /* +0x14 */
    int32_t spriteX;           /* +0x18 */
    int32_t spriteY;           /* +0x1C */
    uint8_t unk_20[8];
};
/* Memory card manager (0x28 bytes). */
struct McManager {
    void *queue;               /* +0x00 memory card request queue object */
    int32_t state;             /* +0x04 step of the current operation */
    int32_t unk_08;            /* +0x08 cleared by mm_McManager_ClearSlots */
    uint8_t unk_0C[8];
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
/* Bag item slot (0x10 bytes). */
struct BagSlot {
    uint16_t item;             /* +0x00 0 = empty */
    uint8_t unk_02[0xE];
};
/* Bag shortcut (4 bytes). */
struct BagShortcut {
    int16_t slot;              /* +0x00 -1 = none */
    int16_t unk_02;
};
struct Bag {
    uint16_t unk_000;
    uint16_t unk_002;
    BagSlot slots[20];         /* +0x004 */
    BagShortcut shortcuts[10]; /* +0x144 */
    A3DObject *model;          /* +0x16C bag model (from 0x1F3B24) */
    int32_t unk_170;
    int32_t unk_174;
    int32_t unk_178;
    uint8_t unk_17C[0x18];
    int32_t unk_194;
    Vec4 *matrix;              /* +0x198 the model's matrix */
    int32_t unk_19C;
    int32_t unk_1A0;
    int32_t unk_1A4;           /* +0x1A4 -1 at construction */
    uint8_t unk_1A8[4];
};
/* Status panel (0xC0 bytes). */
struct StatusPanel { uint8_t data[0xC0]; };
/* Pause and save menu (0x99C bytes). */
struct PauseMenu { uint8_t data[0x99C]; };
/* Minimap (0x21DC bytes). */
struct Minimap { uint8_t data[0x21DC]; };
/* City cursor (0x1E0 bytes). */
struct CityCursor {
    CityCursorView views[2];   /* +0x000 one per frame parity (0x1BDCD0 & 1) */
    int32_t visible;           /* +0x1A0 */
    int32_t targetId;          /* +0x1A4 */
    uint16_t targetKind;       /* +0x1A8 */
    uint8_t unk_1AA[6];
    Vec4 targetPos;            /* +0x1B0 */
    int32_t lineCount;         /* +0x1C0 info lines shown */
    int32_t mode;              /* +0x1C4 0 plain, 4 with the extra packet, else with the fade frame */
    int32_t frameStep;         /* +0x1C8 moves the fade frame up 16 per step */
    int32_t unk_1CC;
    int32_t frameX;            /* +0x1D0 */
    int32_t frameY;            /* +0x1D4 */
    uint8_t unk_1D8[8];
};
/* Phone call manager (0x7C bytes). */
/* Queued phone call (0xC bytes, mm_PhoneCall_Post). */
struct PhoneCallEntry {
    int32_t person;            /* +0x00 */
    uint16_t family;           /* +0x04 0 = free entry */
    uint16_t member;           /* +0x06 */
    int32_t unk_08;            /* +0x08 */
};
struct PhoneCalls {
    void *unk_00;              /* +0x00 from 0x1F3B08 at construction */
    int32_t unk_04;            /* +0x04 a call is shown (mm_PhoneCall_Next) */
    int32_t unk_08;
    int32_t unk_0C;
    PhoneCallEntry calls[4];   /* +0x10 queue, first entry shown */
    int32_t unk_40;            /* +0x40 negated by mm_PhoneCall_Next */
    uint8_t unk_44[0x38];
};
/* Event manager (0xB0 bytes); +0xA4 and +0xA8 are the events the talk script works with. */
struct EventMgr { uint8_t data[0xB0]; };
/* Wants window (0x544 bytes); starts with the 0x504-byte wants list. */
struct WantsWin {
    uint16_t counters[2];      /* +0x00 cleared by mm_Wants_ctor */
    uint8_t unk_004[0x500];
    A3DObject *model;          /* +0x504 window model (from 0x1F3B04) */
    int32_t unk_508[4];        /* +0x508 */
    int32_t unk_518;           /* +0x518 */
    int32_t unk_51C;           /* +0x51C */
    uint8_t unk_520[4];
    Vec4 *matrix;              /* +0x524 the model's matrix */
    int32_t unk_528;           /* +0x528 */
    int32_t unk_52C;           /* +0x52C */
    uint8_t unk_530[8];
    int32_t unk_538;           /* +0x538 10 at construction */
    uint8_t unk_53C[8];
};
/* Laptop menu (0x3E0 bytes). */
struct Laptop {
    void *unk_00;              /* +0x00 from 0x1F3B08 at construction */
    uint8_t unk_04[0x38];
    int32_t unk_3C;            /* +0x3C */
    uint8_t unk_40[4];
    int32_t unk_44;            /* +0x44 */
    uint8_t unk_48[0xD4];
    int32_t unk_11C;           /* +0x11C 4 when +0x120 gets set while entry bit 0x10 is active (mm_Laptop_CheckOpen) */
    int32_t circleActive;      /* +0x120 set while Circle alone is held in the city view, cleared on release */
    int32_t circleHeld;        /* +0x124 Circle was held last frame, so holding it does not set +0x120 again */
    uint8_t unk_128[0x2A8];
    int32_t unk_3D0;           /* +0x3D0 */
    uint8_t unk_3D4[0xC];
};
/* Player character (0x90 bytes). */
struct Player {
    int32_t mode;              /* +0x00 0 turn, 1 walk (Cross lays road), 2 place for applicant, 5 place bag item */
    int32_t previousMode;      /* +0x04 mm_Player_SetMode(-1) returns to it */
    int32_t unk_08;            /* +0x08 */
    int32_t unk_0C;
    Vec4 position;             /* +0x10 */
    uint8_t unk_20[0x14];
    int32_t unk_34;            /* +0x34 */
    uint8_t unk_38[4];
    int32_t unk_3C;            /* +0x3C */
    int32_t unk_40;            /* +0x40 */
    int32_t unk_44;            /* +0x44 */
    int32_t unk_48;            /* +0x48 */
    float jumpSpeed;           /* +0x4C */
    int32_t unk_50;            /* +0x50 */
    uint8_t unk_54[0x2C];
    int32_t unk_80;            /* +0x80 */
    int32_t jumps;             /* +0x84 */
    A3DObject *model;          /* +0x88 */
    uint8_t unk_8C[4];
};
/* Applicant list entry (0x14 bytes). */
struct ApplicantEntry {
    uint16_t index;            /* +0x00 own index */
    int16_t state;             /* +0x02 0 free, 2 waiting */
    uint16_t family;           /* +0x04 */
    uint8_t unk_06[4];
    uint16_t type;             /* +0x0A building type the family wants (index into the table at gp 0x1BDBF8) */
    uint8_t unk_0C[8];
};
/* Applicants list (0x350 bytes). */
struct Applicants {
    ApplicantEntry entries[32]; /* +0x000 */
    int32_t count;             /* +0x280 */
    int32_t unk_284;           /* +0x284 */
    uint8_t unk_288[0x2C];
    float unk_2B4;             /* +0x2B4 */
    uint8_t unk_2B8[4];
    int32_t unk_2BC;           /* +0x2BC */
    uint8_t unk_2C0[0xC];
    int32_t cursor;            /* +0x2CC (provisional) */
    int32_t top;               /* +0x2D0 (provisional) */
    uint8_t unk_2D4[8];
    uint16_t claims[32];       /* +0x2DC claim section C of each entry's building type */
    uint8_t unk_31C[0x34];
};
/* Markers model (0x130 bytes). */
struct Markers {
    A3DObject *model;          /* +0x000 from 0x1F3B28 */
    void *unk_004;             /* +0x004 from 0x1F3B08 */
    uint8_t unk_008[8];
    Vec4 matrices[16];         /* +0x010 four 4x4 matrices, one per marker */
    int32_t unk_110;           /* +0x110 1 at construction */
    int32_t unk_114;           /* +0x114 2 */
    int32_t unk_118;           /* +0x118 3 */
    int32_t unk_11C;           /* +0x11C -1 */
    int32_t unk_120;
    int32_t unk_124;
    void *vtable;              /* +0x128 */
    uint8_t unk_12C[4];
};
/* Notice line (0x14 bytes). */
struct Notice {
    int32_t id;                /* +0x00 shown notice, -1 none */
    int32_t timer;             /* +0x04 frames left */
    TextBox *text;             /* +0x08 */
    TextBox *text2;            /* +0x0C */
    int32_t unk_10;            /* +0x10 second line shown */
};
/* Town's Pages phone book (0x64 bytes). */
struct TownPages {
    uint8_t unk_00[0x40];
    int32_t pageEntries;       /* +0x40 entries on the current page (up to 10) */
    uint8_t unk_44[0x20];
};
/* Per-event handler data (base 0x1C bytes; some handlers use 0x7C), the first argument of every mm_Ev*_ slot. */
struct EventData { uint8_t data[0x1C]; };
/* Name in the stage state (0x10 bytes). */
struct StageName { char text[0x10]; };
/* Stage state at 0x1C5380, saved with the city (0x284 bytes). */
struct StageState {
    StageName names[26];       /* +0x000 the city's name, the mayor's name, ... (cleared by mm_Stage_Reset) */
    uint8_t unk_1A0[0x20];
    int32_t unk_1C0;           /* +0x1C0 3000 at reset */
    int32_t unk_1C4;           /* +0x1C4 2500 at reset */
    int32_t stage;             /* +0x1C8 stage number, -1 = none */
    int32_t quota;             /* +0x1CC population target (mm_EvQuota80_Check) */
    uint8_t unk_1D0[0xB0];
    int32_t unk_280;           /* +0x280 */
};
/* Character type class object (0x24 bytes; the person class is 0x28 with its walking speed at +0x24),
   one per CHARTYPE.BIN record in the table at gp 0x1BDA3C. */
struct CharClass {
    void *parts[4];            /* +0x00 model parts, from the part table 0x1F3B90 by record bytes 8, 9, 10 and 14 */
    uint16_t unk_10;           /* +0x10 record byte 11 */
    uint16_t unk_12;           /* +0x12 record byte 12 */
    uint16_t unk_14;           /* +0x14 record byte 13 */
    uint8_t unk_16;            /* +0x16 record byte 2 */
    uint8_t unk_17;
    uint16_t unk_18;           /* +0x18 record halfword 4 */
    uint8_t unk_1A[2];
    uint32_t unk_1C;           /* +0x1C record word 0x14 */
    void *vtable;              /* +0x20 */
};
/* Person character type (0x28 bytes, mm_Person_ctor). */
struct PersonClass {
    CharClass base;
    float walkSpeed;           /* +0x24 record halfword 6 / 1e6 */
};
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
struct LayerB {
    uint8_t unk_000[0x130];
    uint32_t packetA;          /* +0x130 */
    uint8_t unk_134[0xEC];
    uint32_t packetB;          /* +0x220 */
    uint8_t unk_224[0xC];
};
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
struct LayerF {
    uint8_t unk_000[0x130];
    uint32_t packet;           /* +0x130 */
    uint8_t unk_134[0xC];
};
struct LayerG { uint8_t data[0x2E0]; };
