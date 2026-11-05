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
typedef struct MenuWin MenuWin;
typedef struct PauseSlotText PauseSlotText;
typedef struct LoadingScreen LoadingScreen;
typedef struct McManager McManager;
typedef struct CardTime CardTime;
typedef struct CardDirEntry CardDirEntry;
typedef struct SaveTitle SaveTitle;
typedef struct NameEntry NameEntry;
typedef struct TitleScene TitleScene;
typedef struct Bag Bag;
typedef struct BagShortcut BagShortcut;
typedef struct BagSlot BagSlot;
typedef struct GoodsRecord GoodsRecord;
typedef struct StatusPanel StatusPanel;
typedef struct PauseMenu PauseMenu;
typedef struct Minimap Minimap;
typedef struct CityCursor CityCursor;
typedef struct CityCursorView CityCursorView;
typedef struct PhoneCalls PhoneCalls;
typedef struct PhoneCallEntry PhoneCallEntry;
typedef struct EventMgr EventMgr;
typedef struct EventSlot EventSlot;
typedef struct EventQueueEntry EventQueueEntry;
typedef struct EventQueue EventQueue;
typedef struct EventTest EventTest;
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
typedef struct EvCultData EvCultData;
typedef struct AdoptionChild AdoptionChild;
typedef struct EvAdoptionData EvAdoptionData;
typedef struct EvShoppingData EvShoppingData;
typedef struct EvDeliveryData EvDeliveryData;
typedef struct EvPartyData EvPartyData;
typedef struct EvThiefData EvThiefData;
typedef struct MemberId MemberId;
typedef struct EventType EventType;
typedef struct EventHandler EventHandler;
typedef struct EvAdvertiseData EvAdvertiseData;
typedef struct EvNoodlesData EvNoodlesData;
typedef struct EvMiddleAgedData EvMiddleAgedData;
typedef struct EvCaveManData EvCaveManData;
typedef struct EvMissingChildrenData EvMissingChildrenData;
typedef struct CharClass CharClass;
typedef struct CharClassVtable CharClassVtable;
typedef struct PersonClass PersonClass;
typedef struct StageName StageName;
typedef struct StageState StageState;
typedef struct TexImage TexImage;
typedef struct TexImageFile TexImageFile;
typedef struct LayerA LayerA;
typedef struct LayerB LayerB;
typedef struct PlacementLayer PlacementLayer;
typedef struct RainLayer RainLayer;
typedef struct SnowLayer SnowLayer;
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
    float length;              /* +0x0C read from track 0 as the animation's length (mm_A3D_GetAnimLength) */
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
/* A memory card time stamp (8 bytes): plain numbers, not BCD. */
struct CardTime {
    uint8_t unk_00;
    uint8_t second;            /* +0x01 */
    uint8_t minute;            /* +0x02 */
    uint8_t hour;              /* +0x03 */
    uint8_t day;               /* +0x04 */
    uint8_t month;             /* +0x05 */
    uint16_t year;             /* +0x06 */
};
/* A memory card directory entry (0x40 bytes): one per save slot (0x1F3F00), the scan's last read (0x1F4300), the
   loaded save's (0x1C5640, mm_Title_LoadMenu). */
struct CardDirEntry {
    uint8_t unk_00[8];
    CardTime modified;         /* +0x08 finds the newest save (mm_McManager_FormatSlotText); a save changed since it was
                                  loaded is not overwritten */
    uint8_t unk_10[0x10];
    char name[0x20];           /* +0x20 the save folder's name */
};
/* A save's title as icon.sys keeps it (0x44 bytes, mm_McManager_BuildSaveTitle): the prefix and the city's name in
   full-width characters, cut at 0x3C bytes, then four little-endian numbers for the load screen. */
struct SaveTitle {
    char text[0x3C];           /* +0x00 */
    uint8_t end;               /* +0x3C 0 */
    uint8_t months[2];         /* +0x3D months played */
    int8_t approval;           /* +0x3F the approval rate */
    uint8_t population[2];     /* +0x40 */
    uint8_t stage[2];          /* +0x42 the stage number */
};
/* Name entry window (0x124 bytes): a keyboard of 6 columns on a board model (mm_NameEntry_*). */
struct NameEntry {
    A3DObject *model;          /* +0x000 the board (0x1F3B6C) */
    TextBox *hintText;         /* +0x004 16 by 1: the hint, scrolled when longer than 10 */
    TextBox *keysText;         /* +0x008 3 by 4: the keys shown */
    TextBox *nameText;         /* +0x00C 12 by 1: the name typed */
    TextBox *lineText;         /* +0x010 16 by 1 */
    Vec4 *boardMatrix;         /* +0x014 the model's matrix */
    Vec4 *markerMatrix;        /* +0x018 the cursor marker's, in the model's node data (+0x130) */
    char name[0x24];           /* +0x01C up to 24 characters */
    int32_t state;             /* +0x040 0 closed, 1 open, 2 done, 3 sliding in, 4 sliding out */
    int32_t keySet;            /* +0x044 the keyboard (the text table at 0x1B13E0) */
    int32_t column;            /* +0x048 the cursor's column (of 6) */
    int32_t row;               /* +0x04C its row (of 4 shown) */
    int32_t firstRow;          /* +0x050 the first row shown */
    int32_t rows;              /* +0x054 */
    int32_t keyCount;          /* +0x058 */
    int32_t length;            /* +0x05C the name's length, where the next key goes */
    int32_t moreAbove;         /* +0x060 the up arrow is shown */
    int32_t moreBelow;         /* +0x064 the down arrow is shown */
    uint8_t maxLength;         /* +0x068 at most 24 */
    uint8_t unk_069[3];
    int32_t blink;             /* +0x06C restarts the cursor's blink */
    int32_t slideFrames;       /* +0x070 0 to 25 while sliding */
    int32_t noSlide;           /* +0x074 opens and closes at once */
    float restHeight;          /* +0x078 the board's y when open */
    float markerX;             /* +0x07C the marker's x at column 0 (0.016 a column) */
    float markerY;             /* +0x080 its y at row 0 (0.0227 a row) */
    char hint[0x80];           /* +0x084 */
    int32_t hintScroll;        /* +0x104 the hint's first character shown, -1 when it fits */
    uint8_t hintStep;          /* +0x108 the hint moves every tenth frame */
    uint8_t unk_109[0xB];
    float boardPlace[3];       /* +0x114 the board's x, y and z at construction */
    void *vtable;              /* +0x120 0x1BCFE8 */
};
/* The title scene: logos, memory card check, title (0x104 bytes, mm_TitleScene_*). */
struct TitleScene {
    Camera *camera;            /* +0x00 */
    LightSet *lights;          /* +0x04 */
    TexSprite *logoSprite;     /* +0x08 drawn in layer set 0x0A/0x0B (mm_Title_DrawCommon) */
    TexSprite *titleSprite;    /* +0x0C layer sets 0x1E..0x29 */
    TexSprite *logoLetters[3]; /* +0x10 the title logo's three bouncing letters (mm_Title_LogoLetters) */
    TexSprite *choices[2];     /* +0x1C the title menu's two choices (mm_Title_DrawChoices) */
    A3DObject *menuModel;      /* +0x24 layer set 0x28/0x29 */
    A3DObject *logoModel;      /* +0x28 layer set 0x14/0x15 */
    LoadingScreen *loading;    /* +0x2C text and spinner shown while loading (mm_Title_NewGameIntro sets its text) */
    TexSprite *loadingFrame;   /* +0x30 the loading bar's frame (mm_TitleScene_StartGame) */
    TexSprite *loadingBar;     /* +0x34 one segment of it, drawn up to 16 times */
    float logoTime;            /* +0x38 frame of the logo model's intro animation (mm_Title_Intro) */
    int32_t choice;            /* +0x3C selected choice */
    int32_t choicePulsing;     /* +0x40 cleared when the selected choice's pulse ends */
    int32_t choiceCount;       /* +0x44 (provisional) 1: only the first choice is shown */
    int32_t pulseFrame;        /* +0x48 0..19 while the choice pulses (mm_Title_DrawChoices), then the frames of its idle wobble (mm_Title_DrawChoicesIdle) */
    int32_t loadStep;          /* +0x4C the city asset loader's state (mm_CityAssets_LoadStep), 0 once it is done */
    int32_t logoSound;         /* +0x50 next timed effect of the logo intro (mm_Title_Intro) */
    int32_t loadProgress;      /* +0x54 the loader's progress (0x1BDB54), the bar shows it in 70ths */
    int32_t showHint;          /* +0x58 the "[SELECT]SoundTest" line is drawn (mm_Title_PressStart) */
    uint8_t song;              /* +0x5C the sound test's song, 0..0x13 (mm_SoundTest_BgmPage) */
    uint8_t effect;            /* +0x5D its effect, 0..0x47 (mm_SoundTest_SePage) */
    int16_t pitch;             /* +0x5E within +-0x4000 */
    uint8_t note;              /* +0x60 0..0x7F */
    uint8_t unk_61;
    int16_t tempo;             /* +0x62 within +-0xFF */
    int32_t volume;            /* +0x64 0..0x3FFF */
    int8_t pan;                /* +0x68 -10..10 */
    uint8_t distance;          /* +0x69 0..16 */
    uint8_t unk_6A[2];
    int32_t playingSong;       /* +0x6C the song whose bank is loaded, -1 none */
    uint8_t codeStep;          /* +0x70 buttons of the sound test code entered so far (mm_Title_MenuInput) */
    uint8_t unk_71[3];
    int32_t testPage;          /* +0x74 the sound test's page: 0 effects, 1 music, 100 and up its opening and closing */
    int32_t bankLoading;       /* +0x78 a song's bank is loading */
    uint8_t unk_7C[0x34];
    ListMenu *loadMenu;        /* +0xB0 layer set 0x28/0x29 */
    McManager *mc;             /* +0xB4 */
    TextBox *hintText;         /* +0xB8 */
    TextBox *messageBox;       /* +0xBC 21 by 12 (mm_Title_LoadMenu, mm_TitleScene_Load) */
    int32_t mode;              /* +0xC0 the scene's step (mm_TitleScene_Update) */
    int32_t pressStep;         /* +0xC4 step of the current sequence: mm_Title_Intro 0..5; the title 0 before its first frame, 1 after */
    int32_t waitFrames;        /* +0xC8 frames the memory card warning stays after Cross (mm_TitleScene_MemoryCardCheck) */
    int32_t fade;              /* +0xCC screen fade alpha (mm_Title_DrawCommon) */
    int32_t layers;            /* +0xD0 layer set drawn */
    int32_t idleTimer;         /* +0xD4 idle frames (restarted at 1800; provisional) */
    int32_t introFrame;        /* +0xD8 logo intro counter: title sprite alpha, then frames of the letters, then frames left (mm_Title_Intro) */
    uint8_t unk_DC[4];
    int32_t afterGame;         /* +0xE0 made again after a city ended (game state 1): the logos and card check are skipped
                                  and the game data is only reset (mm_TitleScene_StartGame) */
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
/* A goods record (0x88 bytes): GOODS.BIN section B, grouped by the building type that sells them (section A holds each
   group's first record, mm_Goods_GetRecordOffset). */
struct GoodsRecord {
    uint16_t name;             /* +0x00 its name: an offset into section C (mm_BagMenu_RefreshList); also its id
                                  (mm_Bag_FindItem) */
    int8_t priceDigits;        /* +0x02 the price is this times ten to the power of the next (mm_Goods_GetPrice) */
    uint8_t priceExponent;     /* +0x03 */
    uint8_t kind;              /* +0x04 what it is (mm_WordsFn_BuyGoods): 4, 5, 10, 11 laptop modules; 6 to 8 bags; 0x10
                                  presents; 0x11 and 0x13 food to carry, 0x12 and 0x14 food eaten at once; 0x16 music;
                                  0x17 and 0x1A to 0x1C things to place; 0x19 houses */
    uint8_t value;             /* +0x05 by kind: the bag size (6 to 8), the music track (0x16), the family that applies
                                  (0x19), ... */
    int8_t count;              /* +0x06 copied to the bag slot; shown as xN for the kinds to place */
    uint8_t unk_07;
    char text[0x80];           /* +0x08 the description (bytes after its end are leftovers) */
};
/* Bag item slot (0x10 bytes, mm_Bag_AddItem). */
struct BagSlot {
    uint16_t group;            /* +0x00 the goods group (the building type that sells it), 0 = empty */
    uint16_t index;            /* +0x02 its record in the group */
    int16_t stage;             /* +0x04 the stage it was bought in */
    int8_t count;              /* +0x06 from the goods record */
    uint8_t unk_07;
    int32_t month;             /* +0x08 the month count it was bought in */
    uint16_t seller;           /* +0x0C the family of the shop it came from */
    uint8_t unk_0E;            /* +0x0E 0xFF when bought */
    uint8_t unk_0F;
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
    TextBox *nameText;         /* +0x170 the selected item's name (mm_BagMenu_Update) */
    ListMenu *list;            /* +0x174 the item list */
    MenuWin *question;         /* +0x178 the use or discard question */
    Frame *frames[3];          /* +0x17C */
    TextBox *labels[3];        /* +0x188 */
    int32_t state;             /* +0x194 the bag menu's state (mm_BagMenu_Update) */
    Vec4 *matrix;              /* +0x198 the model's matrix */
    float slide;               /* +0x19C how far the bag has slid in, 0 to 120 */
    int32_t cursor;            /* +0x1A0 the slot under the cursor */
    int32_t setupSlot;         /* +0x1A4 the slot whose item is being placed, -1 none (mm_BagMenu_GetSetupKind) */
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
    int32_t infoFrames;        /* +0xBC frames of the info line's cycle: the goal for 0x1E0, then the money; 0x3C0 in all */
};
/* One save's line in the pause menu's slot list (0x92 bytes). */
struct PauseSlotText {
    char text[0x92];
};
/* Pause and save menu (0x99C bytes). */
struct PauseMenu {
    int32_t state;             /* +0x00 4: picking a save slot (mm_PauseMenu_StateSlotSelect), 10: a card message */
    ListMenu *mainMenu;        /* +0x04 */
    ListMenu *slotMenu;        /* +0x08 */
    McManager *mc;             /* +0x0C */
    TextBox *message;          /* +0x10 */
    int32_t itemsListed;       /* +0x14 items in the slot menu, new-save items included (mm_PauseMenu_StateSlotSelect) */
    int32_t savesListed;       /* +0x18 saves found on the cards */
    int32_t cardPort;          /* +0x1C the memory card port in use, 0 or 1 (message 5 names it, mm_PauseMenu_ShowCardMessage) */
    int32_t saveSlot;          /* +0x20 the save slot on the card (mm_McManager_PrepareNewSlot, mm_McManager_GetSaveFolder) */
    int32_t waitFrames;        /* +0x24 frames in the card message state (mm_PauseMenu_StateMain) */
    int32_t blink;             /* +0x28 frames, for the blinking while saving */
    float openTime;            /* +0x2C rises by 10 per frame; the window is fully in at 120 */
    SaveTitle saveTitle;       /* +0x30 the city's save title (mm_McManager_BuildSaveTitle) */
    int32_t cardMessage;       /* +0x74 the message shown (mm_PauseMenu_ShowCardMessage) */
    PauseSlotText slotTexts[16]; /* +0x78 the listed saves' texts (the first byte cleared by mm_PauseMenu_Reset) */
    int32_t pendingChoice;     /* +0x998 the save to overwrite, -1 none */
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
    int32_t familyOnly;        /* +0x08 a call of the family (person 6000): it goes through only while the city is popular */
};
struct PhoneCalls {
    void *unk_00;              /* +0x00 from 0x1F3B08 at construction */
    int32_t state;             /* +0x04 0 no call shown, 1 ringing, 2 answered (mm_PhoneCall_Update) */
    int32_t shown;             /* +0x08 the entry being shown */
    int32_t frames;            /* +0x0C frames in the state */
    PhoneCallEntry calls[4];   /* +0x10 queue */
    int32_t unk_40;            /* +0x40 1 when a call is taken, negated when it ends */
    int32_t unk_44;            /* +0x44 */
    char caller[0x34];         /* +0x48 the caller's text, built once a call */
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
/* An event type (0x4C bytes): a record of DATA\BIN\EVENT.BIN, 96 of them (mm_Event_LoadTypes). Times are minutes of the
   day. Groups: 1 residents' requests, 2 visitors and incidents, 3 the farewell party, 4 city news and trends, 5 residents'
   pastimes and gossip, 6 the game's own (paydays, population milestones, retreat, mission cleared). */
struct EventType {
    char name[0x20];           /* +0x00 the event's title; the mm_Ev*_ handlers are named after them */
    union {
        uint16_t id;           /* +0x20 group and number together: a save keeps the list of ids to match the table with
                                  (mm_Event_InitForCity); wants an event holds carry it (mm_Wants_AddEventHold) */
        struct {
            uint8_t group;     /* +0x20 3: no second event starts while it runs; 4: from a full event slot it takes the
                                  free first talk; 6: its Check gets the second talk event */
            uint8_t number;    /* +0x21 within the group; talk scripts read it (mm_WordsFn_EventStage) */
        };
    };
    int8_t priority;           /* +0x22 the order in a queue bucket, lowest first */
    uint8_t level;             /* +0x23 the queue bucket it waits in; 1 and 5 ignore the start window, 2 takes one
                                  character type */
    uint8_t handler;           /* +0x24 index into the handler table at 0x1AF0D0 (EventHandler), 0 = none */
    uint8_t tileFlags;         /* +0x25 1 + the tile flag set while it runs, 0 = none (mm_Event_SetTileFlags) */
    uint8_t mask;              /* +0x26 tested by mm_EventList_MoveByMask */
    uint8_t ranks;             /* +0x27 the population ranks it runs at (bit rank - 1): below them it waits in bucket 5,
                                  after them it goes to bucket 6 (mm_Event_Update) */
    int8_t wait;               /* +0x28 after it finishes: days to wait in bucket 4, 0 its own level again, -1 bucket 6,
                                  -2 bucket 7 (mm_Event_Finish); also the first wait of every type */
    int8_t days;               /* +0x29 the days it runs: each day takes one, and on the last it ends at finishAt */
    uint16_t startFrom;        /* +0x2A the window it may start in (it may wrap past midnight) */
    uint16_t startUntil;       /* +0x2C */
    uint16_t finishAt;         /* +0x2E */
    uint16_t population;       /* +0x30 the population the city needs */
    uint16_t modules;          /* +0x32 the laptop modules it needs (bits, mm_Laptop_CountActive) */
    uint8_t approvalTier;      /* +0x34 1 + the approval tier it needs (mm_CityScene_ApprovalTier), 0 = any */
    uint8_t scoreA0Tier;       /* +0x35 1 + the tier of mm_CityScene_ScoreA0Tier, 0 = any */
    uint8_t scoreA8Tier;       /* +0x36 1 + the tier of mm_CityScene_ScoreA8Tier, 0 = any */
    uint8_t unk_37;
    uint16_t charTypes[4];     /* +0x38 the person's character type must be one of these (level 2: the first); 0 ends */
    uint8_t gender;            /* +0x40 1 + (person flags & 3), 0 = any */
    uint8_t claimMeter;        /* +0x41 1: the person's workplace has its claim meter at level 0 */
    uint8_t friendship;        /* +0x42 1: the person's friendship band is at least 1 */
    uint8_t place;             /* +0x43 where the person stands: 1 in its workplace, 2 in its target, 3 between two
                                  buildings, 4 outside its workplace at least 8 from its middle (mm_Event_CheckCondition) */
    uint16_t workTypes[4];     /* +0x44 the person's workplace has one of these building types; 0 ends */
};
/* An event handler (0x18 bytes, one per handler id; the table at 0x1AF0D0 points at them): its slots, 0 = none. */
struct EventHandler {
    uint32_t check;            /* +0x00 whether the event may start (event, city, person) */
    uint32_t start;            /* +0x04 */
    uint32_t update;           /* +0x08 every frame (copied to EventData.update) */
    uint32_t end;              /* +0x0C */
    uint32_t text;             /* +0x10 a text for the talk script (mm_Event_CallText) */
    uint32_t answer;           /* +0x14 the talk script's questions and actions (mm_Event_CallAnswer) */
};
/* An active event (0x1C bytes; some types keep more state, up to 0x7C, mm_EventData_GetSize7C): the first argument of
   every mm_Ev*_ handler slot, and the three the talk works with (EventMgr talks). */
struct EventData {
    uint32_t vtable;           /* +0x00 the record's class (+8: its size, mm_EventData_GetSize7C) */
    uint32_t update;           /* +0x04 its handler's Update slot (EventHandler.update), run every frame */
    uint8_t kind;              /* +0x08 row of the event table (EventType) */
    int8_t days;               /* +0x09 days left to run (EventType.days); when it finishes, the type's wait */
    uint8_t var;               /* +0x0A a value talk scripts set and test; some types keep their phase in it
                                  (mm_EvUselessMiddleAged_Update) */
    uint8_t unk_0B;
    ActorRef actor;            /* +0x0C the main actor; types with more list them on from here, 8 bytes each, over the
                                  words below (talk scripts pick them by index, mm_WordsFn_SelectEventActor) */
    int32_t unk_14;            /* +0x14 per type: a month (mm_EvUselessMiddleAged_Start) */
    int32_t unk_18;            /* +0x18 per type: a timer, a distance (mm_EvBodyguard_Answer) */
};
/* The Crazy Cult event's record (type 18, mm_EvCrazyCult_*): residents join the cult one by one while its members are
   found and told in turn. */
struct EvCultData {
    uint8_t unk_00[0xC];       /* +0x00 the EventData header (kind, var) */
    ActorRef actors[6];        /* +0x0C its leaders (family entries 0xAB, 0xAC) and four members (0xAD on) */
    int32_t waitMonth;         /* +0x3C the month count to wait for */
    int32_t waitTime;          /* +0x40 and the time of day, in seconds */
    int32_t tellFrames;        /* +0x44 frames after an actor is told */
    int32_t spreadFrames;      /* +0x48 every 30 frames another resident joins (person flag 0x400) */
    int32_t allJoined;         /* +0x4C every resident has joined */
    int32_t metBits;           /* +0x50 the members the player has talked to (bit k for the actor in the list at 0x1B37C0);
                                  at 7 the leader is told next (mm_EvCrazyCult_Answer) */
};
/* One of the children of the adoption event (4 bytes, the table at 0x1B37D0). */
struct AdoptionChild {
    uint8_t claim;             /* +0x0 the claim it is placed by; 0 ends the list */
    uint8_t seen;              /* +0x1 buildings seen with the claim this check (low 7 bits), bit 7 one of them pressing */
    uint16_t familyType;       /* +0x2 entry + 1 of the family type table its family comes from */
};
/* The adoption event's record (mm_EvAdoption_*). */
struct EvAdoptionData {
    uint8_t unk_00[0xC];       /* +0x00 the EventData header (kind, var) */
    ActorRef actor;            /* +0x0C the adoption's contact (family entry 0xB1, member 0) */
    int32_t frames;            /* +0x14 a timer, or every tenth frame a check */
    int32_t toldCount;         /* +0x18 times the contact was told, six at most */
    int32_t month;             /* +0x1C the month count noted (phases 17 and 20) */
    int32_t pick;              /* +0x20 the child whose claim the city met */
    int32_t applicants[10];    /* +0x24 the families applying when it started, 0 ends */
    AdoptionChild children[6]; /* +0x4C from 0x1B37D0; placed ones are removed */
};
/* The shopping event's record (mm_EvShopping_*): a resident is sent round the shops for up to four goods. */
struct EvShoppingData {
    uint8_t unk_00[0xC];       /* +0x00 the EventData header (kind, var) */
    ActorRef actors[5];        /* +0x0C the shopper, then the seller of each of the goods */
    int32_t shop;              /* +0x34 the shop's record (0x28 bytes each in the table at 0x1BDAB4; goods at +0x20) */
    int32_t goodsCount;        /* +0x38 goods on its list */
    int32_t prices[4];         /* +0x3C */
    int32_t bill;              /* +0x4C their total, rounded up to the next thousand */
    int32_t refund;            /* +0x50 the prices of the goods taken back out of the bag (mm_EvShopping_Answer mode 9) */
    int32_t tellFrames;        /* +0x54 frames after the shopper is told */
    int32_t endFrames;         /* +0x58 the event ends after 240 */
};
/* The delivery man event's record (type 3, mm_EvDeliveryMan_*): in rain or snow a delivery comes from a nearby shop. */
struct EvDeliveryData {
    uint8_t unk_00[0xC];       /* +0x00 the EventData header (kind, var) */
    ActorRef actors[2];        /* +0x0C the customer, then the delivery man (his building and person) */
    int32_t entry;             /* +0x1C the catalogue entry (0x1BA090, 0x184 bytes each, the building type first) */
    int32_t month;             /* +0x20 the month of the year, which picks the item (0x20 bytes each) */
    int32_t tellFrames;        /* +0x24 frames after the customer is told */
    int32_t walkFrames;        /* +0x28 the delivery man walks for 120 */
    int32_t waitFrames;        /* +0x2C then waits 60 */
    int32_t endFrames;         /* +0x30 the event ends after 240 */
    int32_t state;             /* +0x34 2 at the start, 0 once he has walked, 1 once he has waited (talk value 5) */
};
/* The farewell party event's record (mm_EvFarewellParty_*): a family leaves, and the residents who are friends come. */
struct EvPartyData {
    uint8_t unk_00[0xC];       /* +0x00 the EventData header (kind, var) */
    ActorRef actors[3];        /* +0x0C the leaving family, its best friend at work, the friend the player points at */
    int32_t tellFrames;        /* +0x24 frames after an actor is told */
    int32_t guests;            /* +0x28 residents who are friends enough to come (a quarter of the population fills it) */
    int32_t partyTime;         /* +0x2C the party's time of day in seconds: 13:00 plus an hour per 50 guests, 19:00 at most */
    int32_t stageStep;         /* +0x30 the step of the party's staging (fade, clock, overlay, music) */
    int32_t endStep;           /* +0x34 the step of the end's staging */
};
/* The thief event's record (mm_EvThief_*). */
struct EvThiefData {
    uint8_t unk_00[0xC];       /* +0x00 the EventData header (kind, var) */
    ActorRef actors[4];        /* +0x0C */
    int32_t nextMonth;         /* +0x2C the month count of the next strike */
    uint8_t unk_30[0xC];
    int32_t strikes;           /* +0x3C buildings robbed (mm_EvThief_Answer) */
};
/* A family member by family and member number (4 bytes; in lists 0 ends them). */
struct MemberId {
    uint16_t family;           /* +0x0 */
    uint16_t member;           /* +0x2 */
};
/* The advertisement event's record (mm_EvAdvertise_*): residents are sent to the event building. */
struct EvAdvertiseData {
    uint8_t unk_00[0xC];       /* +0x00 the EventData header (kind, var) */
    ActorRef actor;            /* +0x0C the event building and its contact */
    int32_t tellFrames;        /* +0x14 frames after the contact is told */
    int32_t unk_18;
    int32_t endFrames;         /* +0x1C the event ends after 240 */
    int32_t visitorCount;      /* +0x20 up to 15 */
    MemberId visitors[15];     /* +0x24 the family members it brought */
};
/* The noodle shop event's record (type 13, mm_EvNoodles_*): three shops' workers and their buildings. */
struct EvNoodlesData {
    uint8_t unk_00[0xC];       /* +0x00 the EventData header (kind, var) */
    ActorRef actors[10];       /* +0x0C 1 to 3 the story's first, second and third; 4 to 6 the workers found, 7 to 9 their
                                  partners at their buildings */
    int32_t month;             /* +0x5C the month count to wait for */
    int32_t time;              /* +0x60 and the time of day, in seconds */
    int32_t timer;             /* +0x64 frames, counted down or up */
};
/* The cave man event's record (type 15, mm_EvCaveMan_*). */
struct EvCaveManData {
    uint8_t unk_00[0xC];       /* +0x00 the EventData header (kind, var) */
    ActorRef actors[8];        /* +0x0C 0 to 3 the families that join (mm_EvCaveMan_Answer), 4 the leader, 5, 6 the cave
                                  and its owner, 7 */
    int32_t counter;           /* +0x4C actors found (phase 3); the talk's count of families joined */
    int32_t frames;            /* +0x50 counted down or up */
    int32_t month;             /* +0x54 the month count noted; actors are told after it */
};
/* The missing children event's record (type 6, mm_EvMissingChildren_*). */
struct EvMissingChildrenData {
    uint8_t unk_00[0xC];       /* +0x00 the EventData header (kind, var) */
    ActorRef actors[3];        /* +0x0C the parent, the child, the family's first member */
    int32_t counter;           /* +0x24 frames after the parent is told, or calls while searching */
    int32_t outcome;           /* +0x28 how the child was found: 1 near both, 2 stopped by the player (talk value) */
    int32_t frames;            /* +0x2C the event ends after 180 */
};
/* The useless middle-aged man event's record (type 7, mm_EvUselessMiddleAged_*). */
struct EvMiddleAgedData {
    uint8_t unk_00[0xC];       /* +0x00 the EventData header (kind, var) */
    ActorRef actor;            /* +0x0C */
    int32_t month;             /* +0x14 the month it started; he is told on a later one */
    int32_t frames;            /* +0x18 frames after he is told */
    int32_t answer;            /* +0x1C the talk's choice, modes 0 to 3 (mm_EvUselessMiddleAged_Answer) */
};
/* One of the event manager's five event slots (0x20 bytes, filled from the front): an event a talk script started
   (mm_Event_StartFromScript) with the residents it involves. Each day its counter goes down; at 0 the type joins the
   queue again, and a group 4 type whose list is full takes the free first talk instead (mm_Event_Update). */
struct EventSlot {
    uint8_t kind;              /* +0x00 row of the event table, 0 = free (mm_Event_CountActive, mm_WordsFn_EventStage) */
    int8_t stage;              /* +0x01 days left; talk scripts read 3 - stage (mm_WordsFn_EventCountdown) */
    uint8_t unk_02[2];
    uint16_t familyA;          /* +0x04 the persons a talk script selects (mm_WordsFn_SelectEventPersonA/B) */
    uint16_t memberA;          /* +0x06 */
    uint16_t familyB;          /* +0x08 */
    uint16_t memberB;          /* +0x0A */
    MemberId joined[5];        /* +0x0C the residents who joined it (mm_Event_FindForFamily) */
};
struct EventMgr {
    uint16_t minute;           /* +0x00 the minute of the day the events last ran at (mm_Event_Update) */
    uint8_t rank;              /* +0x02 the population rank reached (mm_CityScene_PopulationRank) */
    uint8_t unk_03;
    EventSlot slots[5];        /* +0x04 filled from the front; mm_EventMgr_RequeueFirst drops the first */
    EventData *talks[3];       /* +0xA4 the event records the talk script works with (0x84, 0x24, 0x7C bytes; saved) */
};
/* An entry of the event queue (2 bytes); event slots and event records start with the same pair. */
struct EventQueueEntry {
    uint8_t type;              /* +0x00 row of the event table */
    int8_t wait;               /* +0x01 days to wait in bucket 4, counted down each day (mm_Event_Update) */
};
/* The event queue (0x224 bytes, mm_Event_LoadTypes; saved with the city): every event type not running is in one of
   eight buckets (docs/map/event.md), each kept in the order of the types' priorities, lowest first. */
struct EventQueue {
    int32_t starts[9];         /* +0x000 bucket k runs from entry starts[k] up to starts[k + 1]; the last is the count */
    EventQueueEntry entries[0x100]; /* +0x024 */
};
/* The test of a queue move (mm_EventList_MoveDue and the others): the event table, then what the types are compared
   with. */
struct EventTest {
    EventType *types;          /* +0x00 */
    union {
        int32_t ranks;         /* +0x04 a rank bit, against the types' ranks */
        int32_t level;         /* +0x04 against the types' levels (mm_EventList_MoveDueLevel) */
        uint8_t mask;          /* +0x04 against the types' masks (mm_EventList_MoveByMask) */
    };
    int32_t rankLevel;         /* +0x08 the level, with a rank bit (mm_EventList_MoveInRankLevel) */
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
    uint16_t held;             /* +0x02 the id of the event holding it (EventType.id); non-zero: mm_Wants_Prune leaves it */
};
/* Wants window (0x544 bytes); starts with the 0x504-byte wants list. */
struct WantsWin {
    uint16_t count;            /* +0x00 wants in the index (cleared by mm_Wants_ctor) */
    uint16_t wordsUsed;        /* +0x02 of the data area */
    WantsIndex index[64];      /* +0x004 */
    uint32_t data[256];        /* +0x104 the wants (Want, four words each) */
    A3DObject *model;          /* +0x504 window model (from 0x1F3B04) */
    TextBox *texts[4];         /* +0x508 made once the window is open (mm_WantsWin_Update) */
    int32_t textsShown;        /* +0x518 the texts are drawn (mm_WantsWin_Draw) */
    int32_t state;             /* +0x51C 0 closed, 1 opening, 2 open, 3 turning a page, 4 closing */
    int32_t turnBack;          /* +0x520 the page turns back */
    Vec4 *matrix;              /* +0x524 the model's matrix */
    float animTime;            /* +0x528 the model's animation: 0 to 25 opening, 0 to 120 a page turn */
    int32_t page;              /* +0x52C the pair of pages shown */
    uint8_t unk_530[8];
    int32_t pageCount;         /* +0x538 10 at construction */
    uint8_t unk_53C[8];
};
/* Laptop menu (0x3E0 bytes). */
struct Laptop {
    A3DObject *model;          /* +0x00 from 0x1F3B08 at construction */
    uint8_t unk_04[0xC];
    Vec4 radius;               /* +0x10 an entry's offset from the centre before its turn (mm_Laptop_Animate) */
    Vec4 centre;               /* +0x20 the carousel's centre */
    float turn;                /* +0x30 the carousel's turn, radians */
    float turnTarget;          /* +0x34 the turn of the selected entry */
    float scale;               /* +0x38 the entries' scale */
    int32_t state;             /* +0x3C 2: the selected entry is lifted */
    int32_t turnDir;           /* +0x40 0 turns one way, 1 the other (mm_Laptop_Update) */
    TextBox *text;             /* +0x44 the selected entry's text (mm_Laptop_SetItemText) */
    int32_t cancelled;         /* +0x48 closed with Triangle; with Cross the entry runs */
    uint8_t unk_4C[0x384];
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
    Vec4 colour;               /* +0x20 the tint the building under the player gives (mm_BldgType_DrawOccupant) */
    uint8_t unk_30[4];
    float swayPhase;           /* +0x34 animation frame, passed to mm_Instance_DrawSwayingIfRoom (mm_Player_UpdateAction) */
    int32_t swayArg;           /* +0x38 animation: 0-2 standing, 4-15 moving (mm_Player_UpdateAction) */
    int32_t visible;           /* +0x3C drawn when non-zero (mm_Player_Draw) */
    int32_t target;            /* +0x40 object or person the cursor points at, -1 none (mm_Player_PickTarget) */
    int32_t targetIsPerson;    /* +0x44 */
    int32_t unk_48;            /* +0x48 */
    float jumpSpeed;           /* +0x4C */
    float airTime;             /* +0x50 time in the air, 1/60 a frame (mm_Player_Update) */
    float moveSpeed;           /* +0x54 step per frame (mm_Player_Turn) */
    uint8_t unk_58[8];
    Vec4 push;                 /* +0x60 the flat unit direction from the player to the object hit (mm_Player_Update) */
    uint8_t unk_70[0x10];
    float stepFrames;          /* +0x80 frames left of the step (mm_Player_Move) */
    int32_t jumps;             /* +0x84 */
    A3DObject *model;          /* +0x88 */
    uint8_t unk_8C[4];
};
/* Applicant list entry (0x14 bytes). */
struct ApplicantEntry {
    uint16_t index;            /* +0x00 own index */
    int16_t state;             /* +0x02 0 free, 2 waiting, 3 to be placed, 6 being placed (mm_Applicants_StartPlacing) */
    uint16_t family;           /* +0x04 */
    uint16_t tile;             /* +0x06 where the player put its home (mm_Player_PlaceForApplicant), 0x4000 not yet */
    uint16_t home;             /* +0x08 the home built for it (mm_Applicants_AssignToBuilding), 0x3F80 none */
    uint16_t type;             /* +0x0A building type the family wants (index into the table at gp 0x1BDBF8) */
    int32_t month;             /* +0x0C the month count when it applied (mm_Applicants_BuildInfo) */
    int32_t timer;             /* +0x10 set to 6 when its placing starts */
};
/* Applicants list (0x350 bytes). */
struct Applicants {
    ApplicantEntry entries[32]; /* +0x000 */
    int32_t count;             /* +0x280 */
    int32_t shownCount;        /* +0x284 entries drawn the last time (mm_Applicants_Draw) */
    uint8_t unk_288[4];        /* +0x288 (the stage object base at +0x289 is handed to mm_StageObj_ctor) */
    A3DObject *frameModel;     /* +0x28C 0x1F3B10 */
    A3DObject *cardModel;      /* +0x290 0x1F3B68 */
    TextBox *cardText;         /* +0x294 */
    uint8_t unk_298[4];
    int32_t cardShown;         /* +0x29C mm_Applicants_SetMode's mode; 1 once a card is selected */
    Vec4 *frameMatrix;         /* +0x2A0 the frame model's matrix, placed by mm_Applicants_PlaceAt */
    Vec4 *cardMatrix;          /* +0x2A4 the card model's */
    Vec4 *cardNodeA;           /* +0x2A8 a node matrix in the card model's data (+0x70): the card that slides in */
    Vec4 *cardNodeB;           /* +0x2AC another (+0x1F0) */
    float rollPhase;           /* +0x2B0 the list's roll, 0 to 40 by 2 a frame (mm_Applicants_Draw) */
    float rollDir;             /* +0x2B4 +1 or -1 while the list rolls, 0 when still */
    int32_t sliding;           /* +0x2B8 the card is sliding in (mm_Applicants_Layout) */
    int32_t relight;           /* +0x2BC the card's light is set again on the next draw */
    int32_t slideFrame;        /* +0x2C0 */
    int32_t savedTop;          /* +0x2C4 the top entry before the selection */
    uint8_t stateColour;       /* +0x2C8 the selected entry's state colour (table 0x1BD998), 0xFF at construction */
    uint8_t enabled;           /* +0x2C9 cleared while a menu or a talk is open */
    uint8_t unk_2CA[2];
    int32_t cursor;            /* +0x2CC (provisional) */
    int32_t top;               /* +0x2D0 (provisional) */
    uint8_t unk_2D4[8];
    uint16_t claims[32];       /* +0x2DC claim section C of each entry's building type */
    int32_t mode;              /* +0x31C 4 an applicant is picked, 6 its home is being placed (mm_Applicants_StartPlacing) */
    void *vtable;              /* +0x320 0x1BCF70 (0x1BCF60 for the base) */
    int32_t assignIndex;       /* +0x324 the next entry to try to house (mm_Applicants_Update) */
    int32_t assignTimer;       /* +0x328 frames until the next try */
    int32_t unk_32C;
    Vec4 cardPoint;            /* +0x330 where the cards are placed, (0, 2.3, 0, 1) at construction */
    float step;                /* +0x340 */
    uint8_t unk_344[0xC];
};
/* Markers model (0x130 bytes). */
struct Markers {
    A3DObject *model;          /* +0x000 from 0x1F3B28 */
    A3DObject *markerModel;    /* +0x004 from 0x1F3B08: the marker nodes */
    uint8_t unk_008[8];
    Vec4 matrices[16];         /* +0x010 four 4x4 matrices, one per marker */
    int32_t nodes[4];          /* +0x110 node of markerModel per marker, -1 = none (1, 2, 3, -1 at construction; the fourth
                                  becomes 4 when laptop entry 4 is active, mm_Markers_CheckCircle) */
    int32_t visible;           /* +0x120 set while Circle alone is held in the city view (mm_Markers_CheckCircle) */
    int32_t circleHeld;        /* +0x124 Circle was held with other buttons last frame, so it does not show the markers */
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
    int32_t turnBack;          /* +0x20 the page being turned goes back, else forward */
    Vec4 *bookMatrix;          /* +0x24 the book model's matrix */
    float openTime;            /* +0x28 frames since opening (open at 25) */
    int32_t page;              /* +0x2C */
    int32_t savedPage;         /* +0x30 */
    TownPagesIndex *index;     /* +0x34 */
    int32_t contentsPages;     /* +0x38 */
    int32_t pageCount;         /* +0x3C */
    int32_t pageEntries;       /* +0x40 entries on the current page (up to 10) */
    int32_t targetPage;        /* +0x44 the page a flip goes to (state 6) */
    int32_t chosenFamily;      /* +0x48 the family to call once the book has closed, 0 none (mm_TownPages_SelectedEntry) */
    Frame *cursorFrame;        /* +0x4C */
    int32_t cursorX;           /* +0x50 */
    int32_t cursorY;           /* +0x54 */
    int32_t targetRow;         /* +0x58 the row the cursor slides to (state 5) */
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
    uint8_t voice;             /* +0x16 record byte 2: the message sound of its talks (mm_TalkConv_Start) */
    uint8_t unk_17;
    uint16_t flags;            /* +0x18 record halfword 4; 1: other people keep clear of this one
                                  (mm_Char_CheckNeighbourTile, mm_Map_FindWorkerOnTile) */
    uint8_t unk_1A[2];
    uint32_t unk_1C;           /* +0x1C record word 0x14 */
    CharClassVtable *vtable;   /* +0x20 */
};
/* A character class's virtual methods (0x1C bytes): base 0x1BCE70, class 2 0x1BCE90, people 0x1BCEB0 (docs/map/char.md).
   Each takes the class first. */
struct CharClassVtable {
    uint8_t unk_00[8];
    uint32_t dtor;             /* +0x08 */
    uint32_t update;           /* +0x0C a person's tick (mm_Person_Update, mm_Char2_Update) */
    uint32_t draw;             /* +0x10 on the terrain (mm_Person_Draw, mm_Char2_Draw) */
    uint32_t drawAt;           /* +0x14 at a given matrix, in the talk views (mm_Person_Draw2) */
    uint32_t slot18;           /* +0x18 empty in every class */
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
    int32_t buffer;            /* +0x243C the buffer being filled (mm_LayerA_BeginFrame) */
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
struct PlacementLayer {
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
    int32_t rectActive;        /* +0x21C 1: the highlight (mm_PlacementLayer_InTileRect), 2: the marker */
    int32_t rectX0;            /* +0x220 */
    int32_t rectZ0;            /* +0x224 */
    int32_t rectX1;            /* +0x228 */
    int32_t rectZ1;            /* +0x22C */
    A3DObject *marker;         /* +0x230 the marker model (0x1F3B1C) */
    uint8_t unk_234[0xC];
};
struct RainLayer {
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
struct SnowLayer {
    uint32_t gifTag[4];        /* +0x000 VU1 0x12: 8 loops per step of ST, XYZF2 twice, PRIM 0x76 (blended sprites) */
    Vec4 unk_010[3];           /* +0x010 VU1 0x13..0x15: (0, 0, 1, 0), (1, 1, 1, 0), (10, 5, 0, 0) */
    Vec4 points[13];           /* +0x040 VU1 0x16..0x22 */
    Vec4 turns[32];            /* +0x110 VU1 0x23..0x42: eight rotation matrices about y */
    Vec4 projection[4];        /* +0x310 VU1 0x43..0x46: the projection times the view (mm_SnowLayer_Draw) */
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
