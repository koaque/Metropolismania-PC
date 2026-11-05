/* Game classes whose layout is not mapped yet: named types at their real allocation sizes (from the
   operator new call before each constructor), so prototypes and decompiles carry the class name.
   Fields replace the data arrays as functions are ported. Parsed before 10_game.h, which points at them. */

typedef struct TextBox TextBox;
typedef struct Frame Frame;
typedef struct A3DObject A3DObject;
typedef struct Camera Camera;
typedef struct CityCamera CityCamera;
typedef struct GameFile GameFile;
typedef struct FileRequest FileRequest;
typedef struct TexSprite TexSprite;
typedef struct TexFileHeader TexFileHeader;
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
typedef struct EventSlot EventSlot;
typedef struct ActorRef ActorRef;
typedef struct WantsWin WantsWin;
typedef struct WantsIndex WantsIndex;
typedef struct Want Want;
typedef struct Laptop Laptop;
typedef struct Player Player;
typedef struct Applicants Applicants;
typedef struct ApplicantEntry ApplicantEntry;
typedef struct Markers Markers;
typedef struct Notice Notice;
typedef struct TownPages TownPages;
typedef struct TownPagesIndex TownPagesIndex;
typedef struct EventData EventData;
typedef struct CharClass CharClass;
typedef struct PersonClass PersonClass;
typedef struct StageName StageName;
typedef struct StageState StageState;
typedef struct TexImage TexImage;
typedef struct TexImageFile TexImageFile;
typedef struct LayerA LayerA;
typedef struct LayerB LayerB;
typedef struct LayerC LayerC;
typedef struct LayerD LayerD;
typedef struct LayerE LayerE;
typedef struct LayerF LayerF;
typedef struct LayerG LayerG;
typedef struct LayerAInstance LayerAInstance;
typedef struct InstanceModel InstanceModel;
typedef struct TerrainPassParams TerrainPassParams;
typedef struct A3DModel A3DModel;
typedef struct A3DNode A3DNode;
typedef struct A3DMesh A3DMesh;
typedef struct A3DChainBlock A3DChainBlock;
typedef struct A3DTrack A3DTrack;
typedef struct A3DKey A3DKey;
typedef struct DmaChainReader DmaChainReader;

/* Text box (0x40 bytes), mm_TextBox_Init builds one. */
struct TextBox {
    uint8_t unk_00[8];
    uint32_t packet;           /* +0x08 GS packet the box draws (mm_TextBox_Draw) */
    int32_t clut;              /* +0x0C palette uploaded before the glyphs (16 entries), 0 = none (mm_TextBox_Build) */
    int32_t charWidth;         /* +0x10 */
    int32_t charHeight;        /* +0x14 */
    int32_t x;                 /* +0x18 GS units */
    int32_t y;                 /* +0x1C */
    int32_t z;                 /* +0x20 */
    int32_t pitchX;            /* +0x24 */
    int32_t pitchY;            /* +0x28 */
    int32_t cursor;            /* +0x2C reset by mm_TextBox_SetText */
    uint32_t sprites;          /* +0x30 the cells' sprite data in the packet, 0x50 bytes each (mm_TextBox_Build) */
    uint32_t cells;            /* +0x34 character cells, 0x60 bytes each, glyph pointer at +0x54 (mm_TextBox_AppendText) */
    int32_t columns;           /* +0x38 */
    int32_t rows;              /* +0x3C */
};
/* Window frame (0xC0 bytes). */
struct Frame {
    uint8_t unk_00[0x50];
    float stA[2];              /* +0x50 texture S, T of the top left (mm_Frame_SetStyle) */
    uint8_t unk_58[8];
    uint32_t cornerA;          /* +0x60 GS XY of the top left (mm_Frame_SetRect, mm_Frame_SetCorners) */
    uint32_t colourA;          /* +0x64 (mm_Frame_ctor) */
    uint8_t unk_68[8];
    float stB[2];              /* +0x70 texture S, T of the bottom right */
    uint8_t unk_78[8];
    uint32_t cornerB;          /* +0x80 GS XY of the bottom right */
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
    A3DModel *model;           /* +0x00 the loaded .A3D data */
    void *textures;            /* +0x04 the texture block while loading: its packets go to the GS, then it is freed (mm_A3D_Relocate) */
    int32_t trackCount;        /* +0x08 node tracks per animation (mm_A3D_Relocate, mm_Instance_DrawSwaying) */
    uint8_t unk_0C[4];
    Vec4 matrix[4];            /* +0x10 object matrix (mm_Player_GetMatrix) */
    uint32_t *animations;      /* +0x50 one A3DTrack array address per animation (mm_A3D_Relocate) */
    uint8_t unk_54[0xC];
};
/* A model drawn as instances in mesh groups (mm_InstanceModel_ctor; people and the player): a model, then the group size. */
struct InstanceModel {
    A3DObject base;
    int32_t groupSize;         /* +0x60 meshes per group: node 0's mesh index / 13 (mm_InstanceModel_LoadAsync) */
};
/* Camera (0x310 bytes). */
struct Camera {
    uint32_t packet;           /* +0x000 VU1 packet: uploads +0x010..+0x12F to VU1 address 0 (mm_Camera_BuildPacket) */
    uint8_t unk_004[0xC];
    Vec4 worldScreenA[4];      /* +0x010 view x screenA (mm_Camera_UpdateMatrices) */
    Vec4 worldScreenB[4];      /* +0x050 view x screenB */
    Vec4 normalLight[4];       /* +0x090 sceVu0NormalLightMatrix of lightDir (mm_Camera_UpdateLightMatrices) */
    Vec4 lightColour[4];       /* +0x0D0 sceVu0LightColorMatrix of lightRgb and ambient */
    Vec4 fogA;                 /* +0x110 (0, 0, 0, fog coefficient) from mm_Camera_SetFogRange */
    Vec4 fogB;                 /* +0x120 (0, 0, 0, fog coefficient) */
    Vec4 eye;                  /* +0x130 */
    Vec4 target;               /* +0x140 */
    Vec4 up;                   /* +0x150 for sceVu0CameraMatrix */
    Vec4 lightDir[3];          /* +0x160 */
    Vec4 lightRgb[3];          /* +0x190 */
    Vec4 ambient;              /* +0x1C0 */
    float viewWidth;           /* +0x1D0 (mm_Camera_UpdateProjection) */
    float viewHeight;          /* +0x1D4 */
    float unk_1D8;             /* +0x1D8 1.0 from mm_Camera_ctor */
    float unk_1DC;             /* +0x1DC 10.0 from mm_Camera_ctor */
    Vec4 view[4];              /* +0x1E0 world to camera (sceVu0CameraMatrix) */
    Vec4 screenA[4];           /* +0x220 */
    Vec4 screenB[4];           /* +0x260 */
    Vec4 screenC[4];           /* +0x2A0 view size to screen scale (mm_Camera_UpdateProjection) */
    float distance;            /* +0x2E0 projection distance */
    float scaleX;              /* +0x2E4 */
    float scaleY;              /* +0x2E8 */
    float centreX;             /* +0x2EC screen centre */
    float centreY;             /* +0x2F0 */
    float depthMax;            /* +0x2F4 depth range (provisional) */
    float depthMin;            /* +0x2F8 */
    float zNear;               /* +0x2FC */
    float zFar;                /* +0x300 */
    float fogFar;              /* +0x304 */
    float fogNear;             /* +0x308 */
    uint8_t unk_30C[4];
};
/* City camera with the sky (0x540 bytes): a camera plus the zoom level and sky packets. */
struct CityCamera {
    Camera base;
    uint32_t skyBands[48];     /* +0x310 the sky's DMA packets (mm_Sky_BuildPackets, drawn by mm_Sky_Draw): the colour bands */
    uint32_t skyPicture[40];   /* +0x3D0 the sky texture */
    uint32_t skyGlow[48];      /* +0x470 the glow */
    int32_t zoomLevel;         /* +0x530 0..3, L1/R1 and L2/R2 */
    uint8_t unk_534[0xC];
};
/* Disc file handle (0x8 bytes), mm_File_*. */
struct GameFile {
    int32_t fd;                /* +0x00 -1 when closed */
    int32_t async;             /* +0x04 requests go through the file queue */
};
/* Disc request queue slot (0x1C bytes), mm_FileQueue_*; 64 of them at 0x1F4ED0. */
struct FileRequest {
    int32_t op;                /* +0x00 0 free, 1 open, 2 close, 3 seek, 4 read */
    uint16_t state;            /* +0x04 0 not issued yet, 1 issued (poll with sceIoctl) */
    uint16_t t0;               /* +0x06 argument of sceCdDiskReady */
    int32_t fd;                /* +0x08 */
    int32_t buffer;            /* +0x0C read destination */
    int32_t bytes;             /* +0x10 bytes to read */
    int32_t offset;            /* +0x14 seek offset */
    int32_t whence;            /* +0x18 seek origin */
};
/* .TEX image header (0x20 bytes): the palette, then the tiles row by row, each tileWords words. */
struct TexFileHeader {
    uint8_t unk_00[4];
    uint16_t paletteSize;      /* +0x04 entries: 0 (32-bit texels), 16 (4-bit) or 256 (8-bit) */
    uint8_t unk_06[2];
    uint32_t tileWords;        /* +0x08 words per tile */
    uint16_t tilesAcross;      /* +0x0C */
    uint16_t tilesDown;        /* +0x0E */
    uint16_t tileWidth;        /* +0x10 pixels */
    uint16_t tileHeight;       /* +0x12 */
    uint8_t unk_14[0xC];
};
/* Sprite from a .TEX file (0x38 bytes). */
struct TexSprite {
    uint8_t unk_00[4];
    TexFileHeader *image;      /* +0x04 the loaded .TEX file */
    uint32_t packet;           /* +0x08 GS packet (mm_TexSprite_Draw) */
    int32_t x;                 /* +0x0C */
    int32_t y;                 /* +0x10 */
    int32_t z;                 /* +0x14 */
    float scaleX;              /* +0x18 */
    float scaleY;              /* +0x1C */
    uint32_t upload;           /* +0x20 next texels to upload while the packet is built (mm_TexSprite_BuildPacket) */
    int32_t blend;             /* +0x24 non-zero: alpha blended */
    uint32_t clipWidth;        /* +0x28 tiles shown (mm_TexSprite_SetClip), used as the end tile by mm_TexSprite_BuildPacket */
    uint32_t clipHeight;       /* +0x2C */
    uint32_t frameX;           /* +0x30 first tile (mm_TexSprite_SetFrame) */
    uint32_t frameY;           /* +0x34 */
};
/* Model instance slot (0x70 bytes): one per mesh drawn this frame, at most 0x708, in two buffers by frame. A DMA packet that
   unpacks the colour and the matrix to VU1 0x2B..0x2F and starts the program (docs/RENDER_PIPELINE.md). */
struct InstanceSlot {
    uint32_t header[4];        /* +0x00 CNT 5 tag; STCYCL, UNPACK V4-32 of 5 quadwords to 0x2B (mm_InstanceBuf_Init) */
    Vec4 colour;               /* +0x10 VU1 0x2B; w picks the colour table entry */
    Vec4 matrix[4];            /* +0x20 VU1 0x2C..0x2F; the last word (+0x5C) carries the caller's flags */
    uint32_t trailer[4];       /* +0x60 RET tag; MSCAL 0, FLUSH */
};
/* A .A3D model as loaded (mm_A3DObject_Load, mm_A3DObject_LoadAsync): the file's 16-byte header, the table offsets that
   mm_A3D_Relocate turns into addresses, the counts, then the tables. */
struct A3DModel {
    uint32_t sizeWords;        /* +0x00 the file's size in words */
    uint32_t textureWords;     /* +0x04 the texture block's size in words */
    A3DNode *nodes;            /* +0x08 0xC0 bytes each */
    uint32_t *meshes;          /* +0x0C one A3DMesh address each; word offsets still when the nodes were given addresses (indexed) */
    void *animations;          /* +0x10 a 0x10-byte header (the track count first), then each animation's tracks and keys */
    void *chains;              /* +0x14 more DMA chain blocks, their size in words first */
    int16_t nodeCount;         /* +0x18 */
    int16_t meshCount;         /* +0x1A */
    int16_t animationCount;    /* +0x1C */
    int16_t chainCount;        /* +0x1E */
};
/* A mesh of a .A3D model: a header, then the DMA chain that draws its vertex batches (CALLed after an instance slot). */
struct A3DMesh {
    uint32_t data;             /* +0x00 a word offset that mm_A3D_Relocate turns into an address */
    uint32_t sizeWords;        /* +0x04 the block's size in words */
    uint32_t unk_08;           /* +0x08 a character part group whose first mesh has this below 4 is replaced (mm_CityAssets_BuildCharParts) */
    uint8_t unk_0C[4];
    uint32_t chain[4];         /* +0x10 start of the DMA chain */
};
/* A block of DMA data in a .A3D file (the model's extra chains, the texture packets): its size, then the chain. */
struct A3DChainBlock {
    uint32_t sizeWords;        /* +0x00 the block's size in words */
    uint8_t unk_04[0xC];
    uint32_t chain[4];         /* +0x10 start of the DMA chain */
};
/* A node of a .A3D model (0xC0 bytes): its parent, its mesh, its matrix relative to the parent and its draw packet. */
struct A3DNode {
    A3DNode *parent;           /* +0x00 null for a root */
    A3DMesh *mesh;             /* +0x04 null for none; with indexed 0 at load, an index into the mesh table (-1 none) */
    uint8_t unk_08[8];
    Vec4 local[4];             /* +0x10 set by the animations (mm_Instance_DrawSwaying) */
    InstanceSlot slot;         /* +0x50 its draw packet: colour (+0x60) and world matrix (+0x70), whose last word (+0xAC) is
                                  non-zero once it is up to date (mm_A3DObject_UpdateNodeMatrix) */
};
/* One node's track in an animation (0x10 bytes): its keys follow all the animation's tracks. */
struct A3DTrack {
    int32_t keyCount;          /* +0x00 */
    A3DNode *node;             /* +0x04 the node it moves */
    A3DKey *keys;              /* +0x08 */
    float length;              /* +0x0C read from track 0 as the animation's length (mm_A3D_GetMeshField0C) */
};
/* A key of a node track (0x20 bytes). */
struct A3DKey {
    Vec4 position;             /* +0x00 x, y, z; w is the key's time */
    Vec4 rotation;             /* +0x10 quaternion x, y, z, w */
};
/* The working state of a DMA chain walk (0x30 bytes, mm_DmaChain_Begin, mm_DmaChain_Next). */
struct DmaChainReader {
    uint8_t unk_00[4];
    uint32_t tag;              /* +0x04 address of the current tag (0x70000000 before the first) */
    uint32_t id;               /* +0x08 the tag's ID bits (word 0 & 0x70000000): 0x10000000 CNT, 0x30000000 REF, 0x50000000
                                  CALL, 0x60000000 RET, 0x70000000 END */
    uint32_t qwc;              /* +0x0C its quadword count (the low half of word 0) */
    uint32_t address;          /* +0x10 word 1 */
    uint32_t vifCommand0;      /* +0x14 word 2 & 0x7F000000: the first VIF code's command */
    uint32_t vifCode0;         /* +0x18 word 2 */
    uint32_t vifCommand1;      /* +0x1C word 3 & 0x7F000000 */
    uint32_t vifCode1;         /* +0x20 word 3 */
    uint32_t data;             /* +0x24 the address after the tag */
    uint8_t unk_28[8];
};
/* A terrain pass's draw parameters (0x120 bytes), uploaded to VU1 0x1C..0x2D before the pass's program runs over the terrain
   quads (docs/RENDER_PIPELINE.md, "The passes' parameters"). */
struct TerrainPassParams {
    Vec4 colour;               /* +0x00 VU1 0x1C colour scale */
    uint32_t tex0[4];          /* +0x10 VU1 0x1D TEX0_1 (low word, high word) */
    uint32_t outputOffset[4];  /* +0x20 VU1 0x1E (0x68) */
    uint32_t gifTag[4];        /* +0x30 VU1 0x1F: 26 loops of TEX0_1, PRIM, then ST, RGBAQ, XYZF2 four times */
    uint32_t prim[4];          /* +0x40 VU1 0x20 */
    Vec4 stq[12];              /* +0x50 VU1 0x21..0x2C texture coordinates: three sets of four */
    uint32_t mask[4];          /* +0x110 VU1 0x2D: w is the quad flags the pass draws */
};
/* One parity's half of the city cursor (0xD0 bytes): info lines, packets and the fade frame. */
struct CityCursorView {
    TextBox *lines[5];         /* +0x00 */
    Frame *lineFrames[5];      /* +0x14 a frame behind each line (a frame's packet starts the frame) */
    Frame *frame;              /* +0x28 the info window's frame */
    Frame *fadeFrame;          /* +0x2C */
    uint8_t extraPacket[0xA0]; /* +0x30 drawn in mode 4 */
};
/* Light set (0x1B0 bytes): the colour table of the model programs, two groups of twelve, coloured by the clock
   (mm_LightSet_UpdateByClock). Its two packets upload +0x10..+0x1AF to VU1 0x12..0x2B (docs/RENDER_PIPELINE.md). */
struct LightSet {
    uint32_t packetA;          /* +0x00 for program 0x1A3C80 (mm_LightSet_BuildPackets) */
    uint32_t packetB;          /* +0x04 for program 0x1A4780 */
    uint8_t unk_08[8];
    Vec4 groupA[12];           /* +0x10 VU1 0x12..0x1D: entry 1 the colour from the table 0x1B0550, entry 3 (4, 1, 1, 2) */
    Vec4 groupB[12];           /* +0xD0 VU1 0x1E..0x29: entries 1 and 2 the colour from the table 0x1B0670, entry 3 (4, 1, 1, 2) */
    int32_t outputOffsets[4];  /* +0x190 VU1 0x2A: 0x110 and 0xE0, where the two programs write their GIF packets */
    Vec4 instanceColour;       /* +0x1A0 VU1 0x2B (1, 1, 1, 1); each instance slot unpacks its own colour there */
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
    int32_t scrollStep;        /* +0x284 pixels the cursor or list still moves, 4 per frame (> 0 up, < 0 down) */
    int32_t unk_288;
    int32_t nextItem;          /* +0x28C item the next mm_ListMenu_SetItemText(-1) fills */
    int32_t itemCount;         /* +0x290 */
    int32_t canCancel;         /* +0x294 Triangle cancels (mm_ListMenu_HandleInput) */
    int32_t canSelect;         /* +0x298 Cross selects */
    int32_t unk_29C;
};
/* Loading screen (0x28 bytes). */
struct LoadingScreen {
    TexSprite *background;     /* +0x00 drawn under the sprite (mm_LoadingScreen_Update) */
    TexSprite *sprite;         /* +0x04 */
    TextBox *text;             /* +0x08 */
    int32_t fade;              /* +0x0C screen fade level, 0..128 */
    int32_t frame;             /* +0x10 frames shown */
    int32_t spriteX;           /* +0x14 sprite place (mm_LoadingScreen_SetSpritePos) */
    int32_t spriteY;           /* +0x18 */
    int32_t spriteZ;           /* +0x1C */
    uint8_t style;             /* +0x20 sprite animation: 0 circling, 1 pulsing, other none */
    uint8_t unk_21[3];
    int32_t showText;          /* +0x24 the text box is drawn once the fade is clear */
};
/* Memory card manager (0x28 bytes). */
struct McManager {
    void *queue;               /* +0x00 memory card request queue object */
    int32_t state;             /* +0x04 step of the current operation */
    int32_t scanState;         /* +0x08 step of the card and save slot scan (mm_McManagerUpdate); 0 idle */
    int32_t scanSlot;          /* +0x0C save slot being filled, 8 per card port */
    int32_t dirIndex;          /* +0x10 next directory entry to ask for on the card */
    int32_t request;           /* +0x14 last queued request */
    uint8_t unk_18[8];
    GameFile *file;            /* +0x20 */
    int32_t fd;                /* +0x24 open save file descriptor */
};
/* Name entry window (0x124 bytes). */
struct NameEntry { uint8_t data[0x124]; };
/* Scene A: logos, memory card check, title (0x104 bytes). */
struct SceneA {
    uint8_t unk_00[8];
    TexSprite *logoSprite;     /* +0x08 drawn in layer set 0x0A/0x0B (mm_Title_DrawCommon) */
    TexSprite *titleSprite;    /* +0x0C layer sets 0x1E..0x29 */
    TexSprite *logoLetters[3]; /* +0x10 the title logo's three bouncing letters (mm_Title_LogoLetters) */
    TexSprite *choices[2];     /* +0x1C the title menu's two choices (mm_Title_DrawChoices) */
    A3DObject *menuModel;      /* +0x24 layer set 0x28/0x29 */
    A3DObject *logoModel;      /* +0x28 layer set 0x14/0x15 */
    LoadingScreen *loading;    /* +0x2C text and spinner shown while loading (mm_Title_NewGameIntro sets its text) */
    uint8_t unk_30[8];
    float logoTime;            /* +0x38 frame of the logo model's intro animation (mm_Title_Intro) */
    int32_t choice;            /* +0x3C selected choice */
    int32_t choicePulsing;     /* +0x40 cleared when the selected choice's pulse ends */
    int32_t choiceCount;       /* +0x44 (provisional) 1: only the first choice is shown */
    int32_t pulseFrame;        /* +0x48 0..19 while the choice pulses (mm_Title_DrawChoices), then the frames of its idle wobble (mm_Title_DrawChoicesIdle) */
    uint8_t unk_4C[4];
    int32_t logoSound;         /* +0x50 next timed effect of the logo intro (mm_Title_Intro) */
    uint8_t unk_54[4];
    int32_t showHint;          /* +0x58 the "[SELECT]SoundTest" line is drawn (mm_Title_PressStart) */
    uint8_t unk_5C[0x14];
    uint8_t codeStep;          /* +0x70 buttons of the sound test code entered so far (mm_Title_MenuInput) */
    uint8_t unk_71[0x3F];
    ListMenu *loadMenu;        /* +0xB0 layer set 0x28/0x29 */
    McManager *mc;             /* +0xB4 */
    TextBox *hintText;         /* +0xB8 */
    uint8_t unk_BC[8];
    int32_t pressStep;         /* +0xC4 step of the current sequence: mm_Title_Intro 0..5; the title 0 before its first frame, 1 after */
    int32_t waitFrames;        /* +0xC8 frames the memory card warning stays after Cross (mm_SceneA_MemoryCardCheck) */
    int32_t fade;              /* +0xCC screen fade alpha (mm_Title_DrawCommon) */
    int32_t layers;            /* +0xD0 layer set drawn */
    int32_t idleTimer;         /* +0xD4 idle frames (restarted at 1800; provisional) */
    int32_t introFrame;        /* +0xD8 logo intro counter: title sprite alpha, then frames of the letters, then frames left (mm_Title_Intro) */
    uint8_t unk_DC[8];
    float previewTime;         /* +0xE4 frame of the new-game preview model's animation (mm_Title_NewGameIntro) */
    float previewEnd;          /* +0xE8 the animation's last frame */
    A3DObject *previewModel;   /* +0xEC the character preview, 0 once it has turned away */
    uint8_t unk_F0[4];
    int32_t previewState;      /* +0xF4 animation: 0 turns in, 8 waits, 1 turns out */
    float previewTurn;         /* +0xF8 turn progress, also the model's x */
    float previewTurnStep;     /* +0xFC added per frame (doubled while turning out) */
    char *previewText;         /* +0x100 loading text shown once the preview waits */
};
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
    uint16_t size;             /* +0x000 0 none, 1..3: 4, 10 or 20 slots (byte table 0x1B5B08, mm_WordsFn_GoodsBagFull) */
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
struct StatusPanel {
    uint8_t packet[0x90];      /* +0x00 GS packet of the flashing alert (colours cycled in the table 0x1AF370) */
    A3DObject *model;          /* +0x90 panel model (from 0x1F3B0C) */
    A3DObject *alertModel;     /* +0x94 drawn with the alert packet (from 0x1F3B00) */
    A3DObject *detailModel;    /* +0x98 */
    uint32_t panelPacket;      /* +0x9C appended before the panel model (mm_StatusPanel_BuildPacket) */
    TextBox *text[4];          /* +0xA0 the detail view's text boxes */
    int32_t detail;            /* +0xB0 the detail view is shown */
    int32_t alert;             /* +0xB4 */
    int32_t markCount;         /* +0xB8 detail model nodes 1.. drawn */
    int32_t unk_BC;
};
/* Pause and save menu (0x99C bytes). */
struct PauseMenu {
    int32_t state;             /* +0x00 4: picking a save slot (mm_PauseMenu_StateSlotSelect), 10: a card message */
    ListMenu *mainMenu;        /* +0x04 */
    ListMenu *slotMenu;        /* +0x08 */
    McManager *mc;             /* +0x0C */
    TextBox *message;          /* +0x10 */
    uint8_t unk_14[8];
    int32_t unk_1C;            /* +0x1C message 5 names a card by it (mm_PauseMenu_ShowCardMessage) */
    uint8_t unk_20[4];
    int32_t waitFrames;        /* +0x24 frames in the card message state (mm_PauseMenu_StateMain) */
    uint8_t unk_28[4];
    float openTime;            /* +0x2C rises by 10 per frame; the window is fully in at 120 */
    uint8_t unk_30[0x44];      /* +0x30 starts with the save name, default from 0x1B73D0 (Shift-JIS) */
    int32_t cardMessage;       /* +0x74 the message shown (mm_PauseMenu_ShowCardMessage) */
    uint8_t unk_78[0x924];
};
/* Minimap (0x21DC bytes). */
struct Minimap {
    uint8_t cells[0x2000];     /* +0x0000 one nibble per tile (mm_Minimap_Plot) */
    uint8_t packet[0x1D0];     /* +0x2000 GS packet of the map image (mm_Minimap_BuildPacket) */
    A3DObject *model;          /* +0x21D0 placed by mm_Minimap_SetView */
    A3DObject *frameModel;     /* +0x21D4 */
    int32_t blink;             /* +0x21D8 frame counter for the blinking markers */
};
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
    int32_t openTimer;         /* +0x1CC 30 when a building is picked (mm_CityCursor_SelectTile) */
    int32_t frameX;            /* +0x1D0 */
    int32_t frameY;            /* +0x1D4 */
    int32_t picked;            /* +0x1D8 the building picked */
    uint8_t unk_1DC[4];
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
/* One actor of a running event (8 bytes). */
/* A person as the event code passes them around (8 bytes): the person record, the building, the family and the
   member. The talk callbacks build one for person A in their stack frame (mm_WordsFn_ActorDetach). */
struct ActorRef {
    uint16_t person;           /* +0x00 person record, 6000 = none */
    uint16_t object;           /* +0x02 building, 0x3F80 = none */
    uint16_t family;           /* +0x04 */
    uint16_t member;           /* +0x06 */
};
/* An active event (0x1C bytes; some types keep more state, up to 0x7C, mm_EventData_GetSize7C): the first argument of
   every mm_Ev*_ handler slot, and the two the talk works with (EventMgr talkA, talkB). */
struct EventData {
    uint8_t unk_00[8];
    uint8_t kind;              /* +0x08 row of the event table (gp-0x7EEC, 0x4C bytes each) */
    uint8_t unk_09;
    uint8_t var;               /* +0x0A a value talk scripts set and test; some types keep their phase in it
                                  (mm_EvUselessMiddleAged_Update) */
    uint8_t unk_0B;
    ActorRef actor;            /* +0x0C the main actor; types with more list them on from here, 8 bytes each, over the
                                  words below (talk scripts pick them by index, mm_WordsFn_SelectEventActor) */
    int32_t unk_14;            /* +0x14 per type: a month (mm_EvUselessMiddleAged_Start) */
    int32_t unk_18;            /* +0x18 per type: a timer, a distance (mm_EvBodyguard_Slot14) */
};
/* One of the event manager's five talk slots (0x20 bytes). */
struct EventSlot {
    uint8_t kind;              /* +0x00 row of the event table, 0 = free (mm_Event_CountActive, mm_WordsFn_EventStage) */
    int8_t stage;              /* +0x01 talk scripts read 3 - stage (mm_WordsFn_EventCountdown) */
    uint8_t unk_02[2];
    uint16_t familyA;          /* +0x04 the persons a talk script selects (mm_WordsFn_SelectEventPersonA/B) */
    uint16_t memberA;          /* +0x06 */
    uint16_t familyB;          /* +0x08 */
    uint16_t memberB;          /* +0x0A */
    uint8_t unk_0C[0x14];
};
struct EventMgr {
    int32_t unk_00;
    EventSlot slots[5];        /* +0x04 filled from the front; mm_EventMgr_RequeueFirst drops the first */
    EventData *talkA;          /* +0xA4 the events the talk script works with */
    EventData *talkB;          /* +0xA8 */
    uint8_t unk_AC[4];
};
/* Want (0x10 bytes) in the wants list's data area. */
struct Want {
    uint16_t person;           /* +0x00 who wants it (passed to mm_PhoneCall_Post) */
    uint16_t object;           /* +0x02 the building whose claim it is */
    uint16_t family;           /* +0x04 */
    uint16_t member;           /* +0x06 */
    uint32_t claimSlot;        /* +0x08 must still match the building's claim slot (mm_Wants_CheckEntry) */
    uint32_t called;           /* +0x0C the family has phoned about it */
};
/* Wants list index entry (4 bytes). */
struct WantsIndex {
    uint16_t offset;           /* +0x00 the want's position in the data area, in words */
    uint16_t held;             /* +0x02 non-zero: mm_Wants_Prune leaves it */
};
/* Wants window (0x544 bytes); starts with the 0x504-byte wants list. */
struct WantsWin {
    uint16_t count;            /* +0x00 wants in the index (cleared by mm_Wants_ctor) */
    uint16_t wordsUsed;        /* +0x02 of the data area */
    WantsIndex index[64];      /* +0x004 */
    uint32_t data[256];        /* +0x104 the wants (Want, four words each) */
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
    uint8_t unk_04[0xC];
    Vec4 radius;               /* +0x10 an entry's offset from the centre before its turn (mm_Laptop_Animate) */
    Vec4 centre;               /* +0x20 the carousel's centre */
    float turn;                /* +0x30 the carousel's turn, radians */
    uint8_t unk_34[4];
    float scale;               /* +0x38 the entries' scale */
    int32_t state;             /* +0x3C 2: the selected entry is lifted */
    uint8_t unk_40[4];
    int32_t unk_44;            /* +0x44 */
    uint8_t unk_48[0xD4];
    int32_t unk_11C;           /* +0x11C 4 when +0x120 gets set while entry bit 0x10 is active (mm_Laptop_CheckOpen) */
    int32_t circleActive;      /* +0x120 set while Circle alone is held in the city view, cleared on release */
    int32_t circleHeld;        /* +0x124 Circle was held last frame, so holding it does not set +0x120 again */
    uint8_t unk_128[0x2A8];
    int32_t selected;          /* +0x3D0 the selected entry */
    uint8_t unk_3D4[0xC];
};
/* Player character (0x90 bytes). */
struct Player {
    int32_t mode;              /* +0x00 0 turn, 1 walk (Cross lays road), 2 place for applicant, 5 place bag item */
    int32_t previousMode;      /* +0x04 mm_Player_SetMode(-1) returns to it */
    int32_t step;              /* +0x08 grid step in progress: 0 none, 1/2 along z, 5/6 along x, 3/4 turning (mm_Player_Move) */
    float heading;             /* +0x0C radians, -pi..pi (mm_Player_Turn) */
    Vec4 position;             /* +0x10 */
    uint8_t unk_20[0x14];
    float swayPhase;           /* +0x34 animation frame, passed to mm_Instance_DrawSwayingIfRoom (mm_Player_UpdateAction) */
    int32_t swayArg;           /* +0x38 animation: 0-2 standing, 4-15 moving (mm_Player_UpdateAction) */
    int32_t visible;           /* +0x3C drawn when non-zero (mm_Player_Draw) */
    int32_t target;            /* +0x40 object or person the cursor points at, -1 none (mm_Player_PickTarget) */
    int32_t targetIsPerson;    /* +0x44 */
    int32_t unk_48;            /* +0x48 */
    float jumpSpeed;           /* +0x4C */
    int32_t unk_50;            /* +0x50 */
    float moveSpeed;           /* +0x54 step per frame (mm_Player_Turn) */
    uint8_t unk_58[0x28];
    float stepFrames;          /* +0x80 frames left of the step (mm_Player_Move) */
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
    uint8_t unk_288[0xC];
    TextBox *cardText;         /* +0x294 */
    uint8_t unk_298[8];
    void *cardModelA;          /* +0x2A0 positioned by mm_Applicants_PlaceAt (+0x30) */
    void *cardModelB;          /* +0x2A4 */
    uint8_t unk_2A8[0xC];
    float unk_2B4;             /* +0x2B4 */
    uint8_t unk_2B8[4];
    int32_t unk_2BC;           /* +0x2BC */
    uint8_t unk_2C0[9];
    uint8_t enabled;           /* +0x2C9 cleared while a menu or a talk is open */
    uint8_t unk_2CA[2];
    int32_t cursor;            /* +0x2CC (provisional) */
    int32_t top;               /* +0x2D0 (provisional) */
    uint8_t unk_2D4[8];
    uint16_t claims[32];       /* +0x2DC claim section C of each entry's building type */
    uint8_t unk_31C[0x34];
};
/* Markers model (0x130 bytes). */
struct Markers {
    A3DObject *model;          /* +0x000 from 0x1F3B28 */
    A3DObject *markerModel;    /* +0x004 from 0x1F3B08: the marker nodes */
    uint8_t unk_008[8];
    Vec4 matrices[16];         /* +0x010 four 4x4 matrices, one per marker */
    int32_t nodes[4];          /* +0x110 node of markerModel per marker, -1 = none (1, 2, 3, -1 at construction) */
    int32_t visible;           /* +0x120 */
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
/* Town's Pages index entry (8 bytes), one per family type (mm_TownPages_BuildIndex). */
struct TownPagesIndex {
    uint16_t count;            /* +0x00 known families of the type */
    uint16_t firstFamily;      /* +0x02 the first of them */
    uint16_t startPage;        /* +0x04 page the type's listing starts on */
    uint16_t unk_06;
};
/* Town's Pages phone book (0x64 bytes): first the contents pages (the family types, 10 per page), then the
   families, type by type. */
struct TownPages {
    A3DObject *book;           /* +0x00 book model (from 0x1F3B20) */
    TextBox *text[6];          /* +0x04 page texts; +0x18 is the list the cursor moves on */
    int32_t mode;              /* +0x1C 6: no cursor */
    int32_t unk_20;
    Vec4 *bookMatrix;          /* +0x24 the book model's matrix */
    float openTime;            /* +0x28 frames since opening (open at 25) */
    int32_t page;              /* +0x2C */
    int32_t savedPage;         /* +0x30 */
    TownPagesIndex *index;     /* +0x34 */
    int32_t contentsPages;     /* +0x38 */
    int32_t pageCount;         /* +0x3C */
    int32_t pageEntries;       /* +0x40 entries on the current page (up to 10) */
    int32_t unk_44;
    int32_t unk_48;
    Frame *cursorFrame;        /* +0x4C */
    int32_t cursorX;           /* +0x50 */
    int32_t cursorY;           /* +0x54 */
    int32_t unk_58;
    int32_t row;               /* +0x5C cursor row on the page */
    int32_t savedRow;          /* +0x60 */
};
/* Name in the stage state (0x10 bytes). */
struct StageName { char text[0x10]; };
/* Stage state at 0x1C5380, saved with the city (0x284 bytes). */
struct StageState {
    StageName names[26];       /* +0x000 the city's name, the mayor's name, ... (cleared by mm_Stage_Reset) */
    uint8_t unk_1A0[0x20];
    int32_t money;             /* +0x1C0 3000 at reset; goods are affordable up to it (mm_WordsFn_CanAfford) */
    int32_t salary;            /* +0x1C4 2500 at reset; added to the money each payday, 1250 more after a good month (mm_EvPayday_Update) */
    int32_t stage;             /* +0x1C8 stage number, -1 = none */
    int32_t quota;             /* +0x1CC population target (mm_EvQuota80_Check) */
    uint8_t unk_1D0[4];
    int32_t missionTarget;     /* +0x1D4 compared with mm_CityScene_MissionProgress (mm_EvMissionCleared_Check) */
    uint8_t unk_1D8[0xA8];
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
    uint16_t flags;            /* +0x18 record halfword 4; 1: other people keep clear of this one
                                  (mm_Char_CheckNeighbourTile, mm_Map_FindWorkerOnTile) */
    uint8_t unk_1A[2];
    uint32_t unk_1C;           /* +0x1C record word 0x14 */
    void *vtable;              /* +0x20 */
};
/* Person character type (0x28 bytes, mm_Person_ctor). */
struct PersonClass {
    CharClass base;
    float walkSpeed;           /* +0x24 record halfword 6 / 1e6 */
};
/* Texture image loaded whole and converted for upload (0x1010 bytes, often on the stack): the DMA packet that uploads
   it (mm_TexImage_Convert), then at +0x1000 the loaded file (TexImageFile). */
struct TexImage {
    uint8_t packet[0x1000];    /* +0x000 */
    TexImageFile *file;        /* +0x1000 */
    uint8_t unk_1004[0xC];
};
/* Header of a texture image file (0x20 bytes), followed by its palette (32-bit entries) and the texels. */
struct TexImageFile {
    uint32_t unk_00;
    int32_t bitsPerPixel;      /* +0x04 4, 8, or 32 for anything else */
    int32_t paletteEntries;    /* +0x08 0 = no palette */
    int32_t hasTexels;         /* +0x0C */
    uint32_t unk_10;
    uint32_t side;             /* +0x14 width and height in pixels */
    int32_t uploadSpan;        /* +0x18 uploads continue while 0x40 per upload so far stays below it */
    uint32_t unk_1C;
};
/* City render layers, built by mm_CityRender_ctor, each with its own VU1 packets (docs/RENDER_PIPELINE.md). The terrain
   layers (B, C, D, E, F, G) draw the frame's terrain quads again with their own program and parameters, each the quads
   whose flags match its mask. A: instances at entrances (0x2440); B: two passes over raised ground (0x230); C: the
   placement highlight and marker (0x240); D: rain (0x410); E: snow (0x6E0); F: a blended pass (0x140); G: the water (0x2E0). */

/* An instance of layer A (0x90 bytes, mm_LayerA_AddInstance). Its draw packet's RET tag (+0x80) is the next instance's
   first quadword (layer A's first word, the model, sits in the unused quadword before the first instance). */
struct LayerAInstance {
    int32_t meshPair;          /* +0x00 draws meshes 2n and 2n + 1 of the layer's model */
    uint8_t unk_04[0xC];
    Vec4 matrix[4];            /* +0x10 a turn about y times a squash in y, then the position */
    uint32_t packet[8];        /* +0x50 CNT 2 tag with FLUSH and DIRECT 2, then a GIF tag of one A+D register */
    uint32_t alpha;            /* +0x70 ALPHA_1 (0x68: blend with a fixed alpha) */
    int32_t fade;              /* +0x74 ALPHA_1's FIX: 0x40 less 1.5 per step, not below 0 */
    uint32_t alphaRegister[2]; /* +0x78 register 0x42 */
    uint32_t ret[4];           /* +0x80 the packet's RET tag */
};
struct LayerA {
    A3DObject *model;          /* +0x0000 the model (0x1F3B70) */
    uint8_t unk_0004[0xC];
    LayerAInstance instances[64]; /* +0x0010 two buffers of 32, the one filled picked by buffer */
    int32_t outputOffset[4];   /* +0x2410 VU1 0x2A (0x110) */
    Vec4 colour;               /* +0x2420 VU1 0x2B (1, 1, 1, 1) */
    int32_t count;             /* +0x2430 instances this frame */
    uint32_t startPacket;      /* +0x2434 uploads +0x2410 to VU1 0x2A and the program 0x1A8330 (mm_LayerA_BuildPacket) */
    uint32_t endPacket;        /* +0x2438 ALPHA_1 back to 0x20_00000044 */
    int32_t buffer;            /* +0x243C the buffer being filled (mm_LayerA_SetSource) */
};
struct LayerB {
    TerrainPassParams params;  /* +0x000 to VU1 0x1C by the first packet: mask 0x20 */
    Vec4 extra;                /* +0x120 VU1 0x2E (0.6, 0.08, 0, 0) */
    uint32_t packetA;          /* +0x130 the program 0x1AA340 with these parameters */
    uint8_t unk_134[0xCC];
    uint32_t maskB[4];         /* +0x200 to VU1 0x2D by the second packet: mask 0x40 */
    uint32_t tex0B[4];         /* +0x210 to VU1 0x1D by the second packet: its texture */
    uint32_t packetB;          /* +0x220 the second pass */
    uint8_t unk_224[0xC];
};
struct LayerC {
    Vec4 colourA;              /* +0x000 VU1 0x1C (255, 0, 0, 128) */
    Vec4 colourB;              /* +0x010 VU1 0x1D (0, 0, 0, 128) */
    uint32_t outputOffset[4];  /* +0x020 VU1 0x1E (0x68) */
    uint32_t gifTag[4];        /* +0x030 VU1 0x1F: 26 loops of 14 registers */
    uint32_t primA[4];         /* +0x040 VU1 0x20 PRIM 0x44: blended triangle strip */
    uint32_t primB[4];         /* +0x050 VU1 0x21 PRIM 0x82: antialiased line strip */
    uint32_t mask[4];          /* +0x060 VU1 0x22: w 4, the quads in the placement rectangle */
    Vec4 colours[24];          /* +0x070 to VU1 0x12 for the marker: a light set's colour table, all 1 */
    int32_t outputOffsets[4];  /* +0x1F0 VU1 0x2A (0x110) */
    Vec4 instanceColour;       /* +0x200 VU1 0x2B (1, 1, 1, 1) */
    uint32_t highlightPacket;  /* +0x210 ALPHA_1 0x30_00000048, the program 0x1A7B30 and +0x000..+0x06F to VU1 0x1C */
    uint32_t markerPacket;     /* +0x214 ALPHA_1 0x30_00000048, +0x070..+0x20F to VU1 0x12 and the program 0x1A8330 */
    uint32_t endPacket;        /* +0x218 ALPHA_1 back to 0x20_00000044 */
    int32_t rectActive;        /* +0x21C 1: the highlight (mm_LayerC_InTileRect), 2: the marker */
    int32_t rectX0;            /* +0x220 */
    int32_t rectZ0;            /* +0x224 */
    int32_t rectX1;            /* +0x228 */
    int32_t rectZ1;            /* +0x22C */
    A3DObject *marker;         /* +0x230 the marker model (0x1F3B1C) */
    uint8_t unk_234[0xC];
};
struct LayerD {
    uint32_t gifTag[4];        /* +0x000 VU1 0x12: 96 loops of RGBAQ, XYZF2 twice, PRIM 0x69 (blended lines) */
    Vec4 colourA;              /* +0x010 VU1 0x13 (765, 765, 765, the clock's brightness: 0..40) */
    Vec4 colourB;              /* +0x020 VU1 0x14 (765, 765, 765, 0) */
    int32_t unk_030[4];        /* +0x030 VU1 0x15 (8) */
    Vec4 drops[16];            /* +0x040 VU1 0x16..0x25 (40, y): y falls 0.4 a frame and wraps at 40 */
    Vec4 streaks[24];          /* +0x140 VU1 0x26..0x3D: the two ends of twelve streaks */
    uint32_t startPackets[2];  /* +0x2C0 per terrain buffer: +0x000..+0x2BF to VU1 0x12, the program 0x1A8A00, then the chunks */
    uint8_t unk_2C8[8];
    TerrainPassParams ground;  /* +0x2D0 the wet ground pass (program 0x1A8E20): mask 1, its TEX0 cycles every twelve frames */
    Vec4 groundExtra;          /* +0x3F0 VU1 0x2E (0, -0.01, 0, 1) */
    uint32_t groundPackets[2]; /* +0x400 per terrain buffer: ALPHA_1 (FIX at +0x24 from the clock), the program, the parameters */
    uint32_t endPacket;        /* +0x408 ALPHA_1 back to 0x20_00000044 */
    uint8_t unk_40C[4];
};
struct LayerE {
    uint32_t gifTag[4];        /* +0x000 VU1 0x12: 8 loops per step of ST, XYZF2 twice, PRIM 0x76 (blended sprites) */
    Vec4 unk_010[3];           /* +0x010 VU1 0x13..0x15: (0, 0, 1, 0), (1, 1, 1, 0), (10, 5, 0, 0) */
    Vec4 points[13];           /* +0x040 VU1 0x16..0x22 */
    Vec4 turns[32];            /* +0x110 VU1 0x23..0x42: eight rotation matrices about y */
    Vec4 projection[4];        /* +0x310 VU1 0x43..0x46: the projection times the view (mm_LayerE_Draw) */
    Vec4 flakes[32];           /* +0x350 VU1 0x47..0x66: x and z within 0.5, y from -40 to 0 in steps of 0.03 a frame, then again */
    int32_t steps;             /* +0x550 VU1 0x67: the clock's night length / 1500 + 1 */
    int32_t unk_554;           /* +0x554 8 */
    uint8_t unk_558[8];
    uint32_t startPackets[2];  /* +0x560 per terrain buffer: RGBAQ (its alpha, the top byte of +0x20, from the clock), ALPHA_1 and
                                  TEX0_1 (the flake), +0x000..+0x55F to VU1 0x12, the program 0x1A9A90, then the chunks */
    uint8_t unk_568[8];
    TerrainPassParams ground;  /* +0x570 the snowy ground pass (program 0x1A9F10): mask x 8 */
    Vec4 groundExtra;          /* +0x690 VU1 0x2E (0, -0.01, 0, 1) */
    uint32_t groundPackets[2]; /* +0x6A0 per terrain buffer: ALPHA_1 (FIX at +0x24 from the clock), the program, the parameters */
    uint32_t endPacket;        /* +0x6A8 ALPHA_1 back to 0x20_00000044 */
    uint8_t unk_6AC[0x14];
    float angles[8];           /* +0x6C0 of the turns: +0.03 a frame, kept within -pi..pi */
};
struct LayerF {
    TerrainPassParams params;  /* +0x000 to VU1 0x1C: mask 0x10, PRIM 0x7C (blended) */
    Vec4 extra;                /* +0x120 VU1 0x2E (not set) */
    uint32_t packet;           /* +0x130 ALPHA_1 0x30_00000048, the program 0x1A91C0 and the parameters */
    uint8_t unk_134[0xC];
};
struct LayerG {
    Vec4 colour;               /* +0x000 VU1 0x1C (128, 128, 128, 60) */
    uint8_t unk_010[0x10];
    uint32_t outputOffsets[4]; /* +0x020 VU1 0x1E (0x40, 0x10) */
    uint32_t gifTag[4];        /* +0x030 VU1 0x1F: 32 loops of 13 registers */
    uint32_t primB[4];         /* +0x040 VU1 0x20 PRIM 0x27C: blended strip in GS context 2 */
    uint32_t primA[4];         /* +0x050 VU1 0x21 PRIM 0x7C: the same in context 1 */
    Vec4 wavesA[12];           /* +0x060 VU1 0x22..0x2D wave points: x sways, y scrolls (mm_LayerG_AnimateWaves) */
    Vec4 wavesB[12];           /* +0x120 VU1 0x2E..0x39 second set, swaying the other way, scrolling back */
    uint32_t mask[4];          /* +0x1E0 VU1 0x3A: w 2 */
    Vec4 waveParamsA;          /* +0x1F0 VU1 0x3B (1, phase - pi/2, 1/pi, pi) */
    Vec4 waveParamsB;          /* +0x200 VU1 0x3C (0.06 * direction, weather, phase, 0) */
    uint32_t packets[2];       /* +0x210 per terrain buffer: GS state, the program 0x1A6440, +0x000..+0x20F to VU1 0x1C, the chunks */
    uint8_t unk_218[8];
    Vec4 waveBase[12];         /* +0x220 the points at rest */
};
