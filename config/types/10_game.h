/* Core game structs (guest view, 32-bit pointers). Offsets come from the decompiled code; fields named
   unk_XX are not understood yet. docs/map/<subsystem>.md describes each struct. Keep sizes exact: they
   are checked again by static_assert when src/port/ overlays them on guest memory. */

typedef struct CityScene CityScene;
typedef struct CityMap CityMap;
typedef struct TileLists TileLists;
typedef struct MapTile MapTile;
typedef struct MapObject MapObject;
typedef struct MapPerson MapPerson;
typedef struct BuildingType BuildingType;
typedef struct Family Family;
typedef struct FamilyMember FamilyMember;
typedef struct Widget Widget;
typedef struct TalkSession TalkSession;
typedef struct TalkView TalkView;
typedef struct TalkPose TalkPose;
typedef struct WordsCtx WordsCtx;
typedef struct Words Words;
typedef struct Stamina Stamina;
typedef struct MemberStatus MemberStatus;
typedef struct CharTypeRecord CharTypeRecord;

/* Map tile (0x18 bytes): 128 x 128 of them at CityMap +0x04. */
struct MapTile {
    float height;              /* +0x00 terrain height (loaded with lwc1, mm_Map_GetTileHeightWord) */
    float verts[4];            /* +0x04 vertex data (mm_Map_GetTileVertex) */
    uint16_t firstObject;      /* +0x14 0x3F80 = none */
    uint16_t flags;            /* +0x16 0x10 = walkable */
};

/* Placed object (0x1C bytes): building, road, field ..., at CityMap +0x08. */
struct MapObject {
    uint16_t type;             /* +0x00 building type index (table at gp 0x1BDBF8) */
    uint16_t id;               /* +0x02 own index */
    uint16_t tile;             /* +0x04 x = low 7 bits, z = bits 7 and up */
    uint16_t flags;            /* +0x06 1 highlight, 8 claim met, 0x20 entrance, 0x40 meter maxed, 0x200 event */
    uint16_t firstPerson;      /* +0x08 6000 = none */
    uint16_t nextOnTile;       /* +0x0A next object on the same tile */
    uint16_t family;           /* +0x0C family living here */
    uint16_t unk_0E;
    uint16_t claimMeter;       /* +0x10 rises while the claim is unmet (mm_ClaimMeter_Level) */
    uint8_t claimSlot;         /* +0x12 entry in the type's claim section */
    uint8_t unk_13;
    uint16_t entranceTile;     /* +0x14 (provisional) */
    uint8_t unk_16[2];
    uint16_t variant;          /* +0x18 roads: shape, bit d = open towards direction d, 5 and 10 straight
                                  (added to the model index by mm_BldgRoad_Slot1C); buildings: bits 3-7 pick
                                  the model variant (mm_Bldg_CollideA) */
    uint16_t mesh;             /* +0x1A roads: mesh drawn (0 = none; street lamps and traffic lights by range) */
};

/* Person on the map (0x30 bytes): 6000 records at CityMap +0x68 (6000 = none). */
struct MapPerson {
    float x;                   /* +0x00 */
    float y;                   /* +0x04 height */
    float z;                   /* +0x08 */
    float unk_0C;
    uint16_t nextOnTile;       /* +0x10 */
    uint16_t index;            /* +0x12 own index */
    uint16_t work;             /* +0x14 workplace object */
    uint16_t inside;           /* +0x16 object the person is in, 0x3F80 = outside */
    uint8_t unk_18[6];
    uint16_t destination;      /* +0x1E */
    uint16_t follow;           /* +0x20 follow target */
    uint16_t flags;            /* +0x22 */
    uint8_t charType;          /* +0x24 index into the character type table (gp 0x1BDA3C) */
    uint8_t unk_25;
    uint8_t schedState;        /* +0x26 */
    uint8_t activity;          /* +0x27 */
    uint16_t target;           /* +0x28 building the person heads for, 0x3F80 = none (mm_Person_TargetWork) */
    uint16_t family;           /* +0x2A */
    uint16_t introduces;       /* +0x2C family this resident can introduce */
    uint8_t member;            /* +0x2E */
    uint8_t status;            /* +0x2F friendship status byte */
};

/* Object lists per claim item (0x8040 bytes) at CityMap +0x0C: 80 lists packed into one array. */
struct TileLists {
    uint16_t items[0x3F80];    /* +0x0000 object ids, list by list */
    uint16_t start[0x50];      /* +0x7F00 first item of each list */
    uint16_t count[0x50];      /* +0x7FA0 items in each list */
};

/* The city map object (0x2F90 bytes): tiles, objects, people, render and city levels in one object. */
struct CityMap {
    void *packedTiles;         /* +0x00 save buffer, 6 bytes per tile (mm_MapSave_*) */
    MapTile *tiles;            /* +0x04 */
    MapObject *objects;        /* +0x08 */
    TileLists *tileLists;      /* +0x0C */
    uint8_t unk_10[0x14];
    int32_t objectCount;       /* +0x24 object records in use (of 0x3F80) */
    uint8_t unk_28[8];
    int32_t categoryCount[11]; /* +0x30 buildings per STRTYPE.BIN category (mm_MapObj_Delete; category 4 only without type flag 0x80) */
    int32_t bigCategory5Count; /* +0x5C category 5 buildings of size 3 and up */
    uint8_t unk_60[4];
    void *vtable;              /* +0x64 */
    MapPerson *people;         /* +0x68 */
    uint8_t unk_6C[8];
    int32_t population;        /* +0x74 */
    uint8_t unk_78[0x2C8C];
    int32_t terrainQuads;      /* +0x2D04 vertices in the terrain buffer this frame (limit 0x9BD) */
    uint8_t unk_2D08[4];
    void *terrainBuffers[2];   /* +0x2D0C 0x9C00 bytes each */
    void *terrainWrite;        /* +0x2D14 write position (mm_Terrain_AddQuad) */
    void *buffers2D18[2];      /* +0x2D18 0x8700 bytes each */
    uint8_t unk_2D20[0x240];
    void *packets[5];          /* +0x2F60 static DMA packets queued by mm_CityRender_DrawOverlays */
    LayerD *sky;               /* +0x2F74 */
    LayerE *layerE;            /* +0x2F78 */
    LayerF *layerF;            /* +0x2F7C */
    LayerG *water;             /* +0x2F80 */
    LayerB *layerB;            /* +0x2F84 */
    LayerC *buildings;         /* +0x2F88 */
    LayerA *instances;         /* +0x2F8C */
};

/* Building type base (0x160 bytes): one object per type in the table at gp 0x1BDBF8; classes add fields after it. */
struct BuildingType {
    uint8_t unk_000[0x110];
    char name[0x20];           /* +0x110 */
    uint8_t unk_130[4];
    uint16_t kind;             /* +0x134 */
    uint16_t flags;            /* +0x136 0x20, 0x40 people inside, 0x100 */
    uint8_t unk_138[4];
    uint16_t openMinute;       /* +0x13C minutes of the day */
    uint16_t closeMinute;      /* +0x13E */
    uint16_t capacity;         /* +0x140 */
    uint8_t size;              /* +0x142 footprint in tiles */
    uint8_t unk_143;
    uint8_t rule;              /* +0x144 */
    uint8_t category;          /* +0x145 STRTYPE.BIN category */
    uint8_t unk_146;
    uint8_t claimB;            /* +0x147 claim section B index */
    uint8_t claimC;            /* +0x148 claim section C index */
    uint8_t unk_149[7];
    uint8_t modelNode;         /* +0x150 */
    uint8_t unk_151[7];
    uint8_t entranceMesh;      /* +0x158 collision mesh of an entrance object (flag 0x20) */
    uint8_t firstMesh;         /* +0x159 first collision mesh of the other objects, plus their variant */
    uint8_t unk_15A[2];
    void *vtable;              /* +0x15C */
};

/* Family member (0x48 bytes) inside a family record. */
struct FamilyMember {
    uint16_t charType;         /* +0x00 */
    uint8_t unk_02;
    uint8_t flags;             /* +0x03 */
    uint8_t role;              /* +0x04 */
    uint8_t unk_05;
    uint8_t unk_06;
    uint8_t range;             /* +0x07 neighbourhood range index */
    uint16_t acquaintances[16]; /* +0x08 family ids this member can introduce, 0 ends */
    char name[16];             /* +0x28 */
    char name2[16];            /* +0x38 */
};

/* Family record from FAMILY.BIN (0x1D4 bytes), cached in 256 slots at 0x1D52E0. */
struct Family {
    uint8_t unk_00[0x20];
    uint16_t homeType;         /* +0x20 */
    uint16_t memberCount;      /* +0x22 */
    FamilyMember members[6];   /* +0x24 */
};

/* Member status entry (0x4 bytes): 256 of them at 0x1F32F0. */
struct MemberStatus {
    uint16_t family;
    uint8_t member;
    uint8_t status;            /* bits 0-1, bits 2-3, level in the high nibble */
};

/* Character type record from CHARTYPE.BIN (0x18 bytes). */
struct CharTypeRecord {
    uint8_t charClass;         /* +0x00 1 person, 2 other */
    uint8_t unk_01[7];
    uint8_t parts[3];          /* +0x08 model parts */
    uint8_t unk_0B[3];
    uint8_t part4;             /* +0x0E */
    uint8_t unk_0F[9];
};

/* Player stamina (0xC bytes) at 0x1BDBB8. */
struct Stamina {
    int32_t unk_00;
    int32_t value;             /* +0x04 maximum 399999 */
    int32_t unk_08;
};

/* Widget base (0xC bytes): state machine for windows and talk classes, vtable at +8. */
struct Widget {
    int32_t state;             /* 0 idle, 1 opening, 2 opened, 3 active, 4 closing, 5 closed */
    float timer;
    void *vtable;
};

/* Talk pose entry (0x4 bytes). */
struct TalkPose {
    uint16_t family;
    uint8_t member;
    uint8_t pose;
};

/* Talk view base (0x270 bytes, vtable 0x1BD450): the building or family on screen with its people.
   The near/far subclasses add camera vectors after it (allocations of 0x290 and 0x2A0 bytes). */
struct TalkView {
    Widget base;
    int32_t unk_0C;
    float eye[4];              /* +0x10 camera eye at open */
    float target[4];           /* +0x20 camera target at open */
    void *buildingModel;       /* +0x30 */
    CityScene *city;           /* +0x34 */
    uint8_t unk_38[8];
    float color[4];            /* +0x40 */
    uint8_t packet[0x20];      /* +0x50 draw packet */
    int32_t building;          /* +0x70 0x3F80 = by family */
    int32_t family;            /* +0x74 */
    void *nameBoxes[4];        /* +0x78 */
    void *frames[4];           /* +0x88 */
    void *cursorFrame;         /* +0x98 */
    int32_t unk_9C;
    void *personModel;         /* +0xA0 */
    float zoom;                /* +0xA4 */
    int32_t unk_A8;
    int32_t scrollMode;        /* +0xAC 1 zoom, 2 scroll */
    int32_t scrollDir;         /* +0xB0 */
    int32_t scrollStep;        /* +0xB4 */
    int32_t forcedChoice;      /* +0xB8 -1 = none */
    int32_t cursorSlot;        /* +0xBC */
    int32_t cursorX;           /* +0xC0 */
    int32_t choiceSlot;        /* +0xC4 */
    int32_t peopleCount;       /* +0xC8 */
    int32_t unk_CC;
    int32_t anim[32];          /* +0xD0 per slot, 7 = idle */
    int32_t animFlip[32];      /* +0x150 */
    float animTime[32];        /* +0x1D0 */
    int32_t chosenSlot;        /* +0x250 */
    int32_t choosing;          /* +0x254 */
    int32_t forceLit;          /* +0x258 */
    int32_t unk_25C;
    int32_t targetObject;      /* +0x260 */
    int32_t targetPerson;      /* +0x264 */
    uint8_t unk_268[8];
};

/* Talk session (0xC0 bytes, vtable 0x1BD770), one per city at CityScene +0x3C. */
struct TalkSession {
    Widget base;
    CityScene *city;           /* +0x0C */
    TalkView *view;            /* +0x10 */
    Widget *conversation;      /* +0x14 */
    int32_t building;          /* +0x18 0x3F80 = by family */
    int32_t person;            /* +0x1C 6000 = none */
    int32_t family;            /* +0x20 */
    int32_t member;            /* +0x24 6 = whole family */
    int32_t arg5;              /* +0x28 */
    int32_t peopleInside;      /* +0x2C the building type has flag 0x40 */
    int32_t singleTarget;      /* +0x30 */
    int32_t slot;              /* +0x34 */
    int32_t unk_38;
    int32_t endChoosing;       /* +0x3C */
    TalkPose poses[32];        /* +0x40 */
};

/* WORDS talk script context (0x140 bytes). A is the person talked to, B a second subject. */
struct WordsCtx {
    CityScene *city;           /* +0x00 */
    int32_t unk_04;
    uint16_t familyA;          /* +0x08 */
    uint16_t memberA;          /* +0x0A */
    MapPerson *personA;        /* +0x0C */
    MapObject *buildingA;      /* +0x10 */
    uint16_t buildingTypeB;    /* +0x14 */
    uint16_t familyB;          /* +0x16 */
    uint16_t memberB;          /* +0x18 */
    uint16_t memberIndex;      /* +0x1A */
    MapPerson *personB;        /* +0x1C */
    MapObject *buildingB;      /* +0x20 */
    void *bagSlot;             /* +0x24 */
    uint16_t result;           /* +0x28 */
    int16_t arg;               /* +0x2A */
    int32_t candidateCount;    /* +0x2C */
    uint16_t candidates[64];   /* +0x30 */
    uint8_t unk_B0[0x80];
    int32_t cancelAllowed;     /* +0x130 */
    int32_t flag134;           /* +0x134 */
    int16_t eventSlot;         /* +0x138 */
    int16_t wantIndex;         /* +0x13A chosen want */
    int32_t friendshipChanged; /* +0x13C */
};

/* The WORDS script object at 0x1BDDF0 (gp-0x7B80); its methods take a pointer to it. */
struct Words {
    WordsCtx *ctx;             /* +0x00 0x140-byte context */
    int32_t unk_04;
    int32_t extraLoaded;       /* +0x08 */
    int32_t bound;             /* +0x0C tables bound on first step */
    uint16_t line;             /* +0x10 */
    uint16_t unk_12;
    uint16_t column;           /* +0x14 */
    uint8_t unk_16[0xA];
    void *extraTable;          /* +0x20 */
    void *lineTable;           /* +0x24 */
};

/* City scene (0xD8 bytes), gp 0x1BDD00. */
struct CityScene {
    StatusPanel *statusPanel;  /* +0x00 */
    TownPages *townPages;      /* +0x04 */
    PauseMenu *pauseMenu;      /* +0x08 */
    void *unk_0C;
    int32_t modeStack[5];      /* +0x10 */
    int32_t modeDepth;         /* +0x24 */
    int32_t unk_28;
    int32_t hudVisible[4];     /* +0x2C */
    TalkSession *talk;         /* +0x3C */
    LightSet *lights;          /* +0x40 */
    Camera *camera;            /* +0x44 */
    Laptop *laptop;            /* +0x48 */
    CityCamera *cityCamera;    /* +0x4C */
    CityMap *map;              /* +0x50 */
    Applicants *applicants;    /* +0x54 */
    Player *player;            /* +0x58 */
    WantsWin *wants;           /* +0x5C wants window; starts with the wants list */
    Bag *bag;                  /* +0x60 */
    Stamina *stamina;          /* +0x64 */
    EventMgr *events;          /* +0x68 */
    PhoneCalls *phoneCalls;    /* +0x6C */
    CityCursor *cursor;        /* +0x70 */
    Minimap *minimap;          /* +0x74 */
    Markers *markers;          /* +0x78 */
    Notice *notice;            /* +0x7C */
    int32_t unk_80;
    int32_t clock;             /* +0x84 seconds, starts at 6:00 */
    int32_t monthCount;        /* +0x88 one month per in-game day */
    int32_t approval;          /* +0x8C -99..120, starts at 50 */
    int32_t weatherToday;      /* +0x90 0 clear, 2 rain, 3 snow */
    int32_t weatherTomorrow;   /* +0x94 */
    int32_t weatherYesterday;  /* +0x98 */
    int32_t unk_9C;
    int32_t scoreA0;           /* +0xA0 decaying average, starts at 250 */
    int32_t scoreA0Count;      /* +0xA4 */
    int32_t scoreA8;           /* +0xA8 */
    int32_t scoreA8Count;      /* +0xAC */
    float unk_B0;
    uint32_t flags;            /* +0xB4 2 holiday month, 4 popular (approval 118+), 0x40 pad focus after a talk */
    int32_t unk_B8;
    int32_t pendingApproval;   /* +0xBC */
    int32_t unk_C0;
    int32_t unk_C4;
    int32_t fadeLevel;         /* +0xC8 */
    int32_t fade;              /* +0xCC fade counter, 0 = none */
    int32_t fadeSpeed;         /* +0xD0 */
    int32_t unk_D4;
};
