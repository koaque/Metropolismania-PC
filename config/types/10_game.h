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
typedef struct MyHomeView MyHomeView;
typedef struct TalkPersonView TalkPersonView;
typedef struct TalkPose TalkPose;
typedef struct WordsCtx WordsCtx;
typedef struct Words Words;
typedef struct Stamina Stamina;
typedef struct MemberStatus MemberStatus;
typedef struct CharTypeRecord CharTypeRecord;
typedef struct SeRequest SeRequest;
typedef struct SeVoice SeVoice;
typedef struct SeSound SeSound;
typedef struct ClaimThresholds ClaimThresholds;

/* Map tile (0x18 bytes): 128 x 128 of them at CityMap +0x04. */
struct MapTile {
    float height;              /* +0x00 terrain height (loaded with lwc1, mm_Map_GetTileHeightWord) */
    float verts[4];            /* +0x04 corner heights towards (-x,-z), (+x,-z), (+x,+z), (-x,+z) (mm_Map_UpdateTileGeometry) */
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
    uint8_t claimTimer;        /* +0x13 runs down while the claim is met or the meter maxed; its tenth speeds the meter (mm_BldgClaims_Evaluate) */
    uint16_t entranceTile;     /* +0x14 (provisional) */
    uint8_t headingHere;       /* +0x16 people whose target it is (mm_MapPeople_RemoveForObject) */
    uint8_t occupants;         /* +0x17 people inside, kept by types with flag 0x40 (mm_EvActor_Release) */
    uint16_t variant;          /* +0x18 roads: shape, bit d = open towards direction d, 5 and 10 straight
                                  (added to the model index by mm_BldgRoad_Slot1C); buildings: bits 3-7 pick
                                  the model variant (mm_Bldg_CollideA); housing: the high byte is the unit
                                  shape (mm_BldgHousing_UnitAt, table 0x1B14F0) */
    uint16_t mesh;             /* +0x1A roads: mesh drawn (0 = none; street lamps and traffic lights by range);
                                  housing: the low byte is the style, 26 meshes each (mm_BldgHousing_Draw) */
};

/* Person on the map (0x30 bytes): 6000 records at CityMap +0x68 (6000 = none). */
struct MapPerson {
    float x;                   /* +0x00 */
    float y;                   /* +0x04 height */
    float z;                   /* +0x08 */
    float heading;             /* +0x0C atan2 of the facing (mm_Person_UpdatePose) */
    uint16_t nextOnTile;       /* +0x10 */
    uint16_t index;            /* +0x12 own index */
    uint16_t work;             /* +0x14 workplace object */
    uint16_t inside;           /* +0x16 object the person is in, 0x3F80 = outside */
    uint16_t walkTile;         /* +0x18 tile the person walks to (mm_Person_UpdatePose); the waypoint (mm_Path_NextWaypoint) */
    uint16_t previousTile;     /* +0x1A the waypoint before it; mm_Path_PickRoadLink never leads back there */
    uint8_t unk_1C[2];
    uint16_t destination;      /* +0x1E */
    uint16_t follow;           /* +0x20 follow target */
    uint16_t flags;            /* +0x22 */
    uint8_t charType;          /* +0x24 index into the character type table (gp 0x1BDA3C) */
    uint8_t cooldown;          /* +0x25 counts down one per tick to 0 (mm_Person_TickCounters) */
    uint8_t schedState;        /* +0x26 */
    uint8_t activity;          /* +0x27 */
    uint16_t target;           /* +0x28 building the person heads for, 0x3F80 = none (mm_Person_TargetWork) */
    uint16_t family;           /* +0x2A */
    uint16_t introduces;       /* +0x2C family this resident can introduce; a vehicle's speed (mm_Char2_UpdateSpeedA) */
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
    int32_t nextId;            /* +0x10 next record to try for a new object (mm_Map_CreateObject); its low half is the object's id */
    int32_t unk_14;
    int32_t flag8Objects;      /* +0x18 objects of types with flag 8 */
    int32_t unk_1C;            /* +0x1C set while there are any */
    int16_t objectsChanged;    /* +0x20 objects were added or removed (mm_MapObjects_Tick then starts the claim re-check) */
    int16_t claimRecheck;      /* +0x22 frames of claim section C re-checks left (0x80 after a change) */
    int32_t objectCount;       /* +0x24 object records in use (of 0x3F80) */
    uint8_t flag8History[6];   /* +0x28 new flag-8 objects per period, newest first (mm_MapObj_PushHistory) */
    int16_t refreshDelay;      /* +0x2E frames until objectsChanged is set (0x81 after a flag-2 type is placed) */
    int32_t categoryCount[11]; /* +0x30 buildings per STRTYPE.BIN category (mm_MapObj_Delete; category 4 only without type flag 0x80) */
    int32_t bigCategory5Count; /* +0x5C category 5 buildings of size 3 and up */
    int32_t unk_60;            /* +0x60 cleared after each placement (mm_Map_PlaceObject) */
    void *vtable;              /* +0x64 */
    MapPerson *people;         /* +0x68 */
    uint8_t unk_6C[4];
    int32_t peopleInUse;       /* +0x70 person records in use, vehicles too (mm_MapPeople_RemoveForObject) */
    int32_t population;        /* +0x74 residents: people records of classes without flag 1 */
    uint8_t unk_78[0x2C8C];
    int32_t terrainQuads;      /* +0x2D04 vertices in the terrain buffer this frame (limit 0x9BD) */
    int32_t sideVertices;      /* +0x2D08 vertices in the cliff-side buffer this frame (limit 0x86E, mm_Terrain_BuildStrip) */
    void *terrainBuffers[2];   /* +0x2D0C 0x9C00 bytes each */
    void *terrainWrite;        /* +0x2D14 write position (mm_Terrain_AddQuad) */
    void *sideBuffers[2];      /* +0x2D18 cliff-side vertex buffers, 0x8700 bytes each */
    void *sideWrite;           /* +0x2D20 write position in the cliff-side buffer (mm_Terrain_BuildStrip) */
    uint8_t unk_2D24[0x23C];
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
    uint8_t unk_000[0x100];
    Vec4 talkEye;              /* +0x100 eye offset from the building for a talk, type flag 0x4000 (mm_TalkView_BuildingCamera) */
    char name[0x20];           /* +0x110 */
    uint8_t unk_130[4];
    uint16_t kind;             /* +0x134 */
    uint16_t flags;            /* +0x136 0x10 talk camera 2.5 lower, 0x20, 0x40 people inside, 0x100, 0x4000 own talk eye (talkEye) */
    uint8_t unk_138[4];
    uint16_t openMinute;       /* +0x13C minutes of the day */
    uint16_t closeMinute;      /* +0x13E */
    uint16_t capacity;         /* +0x140 */
    uint8_t size;              /* +0x142 footprint in tiles */
    uint8_t unk_143;
    uint8_t rule;              /* +0x144 */
    uint8_t category;          /* +0x145 STRTYPE.BIN category */
    uint8_t postGroup;         /* +0x146 group in POSTNAME.BIN (mm_BldgType_GetPostName) */
    uint8_t claimB;            /* +0x147 claim section B index */
    uint8_t claimC;            /* +0x148 claim section C index */
    uint8_t unk_149[5];
    uint8_t ambientSound;      /* +0x14E sound id, 0xFF = none (mm_Se_BuildingSoundState) */
    uint8_t unk_14F;
    uint8_t modelNode;         /* +0x150 */
    uint8_t entranceDrawMesh;  /* +0x151 businesses: mesh of the entrance (+1 for the closed sign, type flag 0x1000) */
    uint8_t firstDrawMesh;     /* +0x152 businesses: first mesh of the other parts, plus their variant */
    uint8_t unk_153;
    int8_t callFlags;          /* +0x154 bit 7: families of this type phone when introduced (mm_Person_TickCounters) */
    uint8_t unk_155[3];
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
    float idleTime;            /* +0xA8 counts frames, wraps at 180 */
    int32_t scrollMode;        /* +0xAC 1 zoom, 2 scroll */
    int32_t scrollDir;         /* +0xB0 */
    int32_t scrollStep;        /* +0xB4 */
    int32_t forcedChoice;      /* +0xB8 -1 = none */
    int32_t cursorSlot;        /* +0xBC */
    int32_t cursorX;           /* +0xC0 */
    int32_t choiceSlot;        /* +0xC4 */
    int32_t peopleCount;       /* +0xC8 */
    int32_t firstShown;        /* +0xCC first person in view (three are shown) */
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

/* The near and far views of a person (allocated 0x290 and 0x2A0 bytes): the talk view plus the camera. */
struct TalkPersonView {
    TalkView base;
    Vec4 eye;                  /* +0x270 behind the person, 1.3 along the view direction from 0.55 under the person */
    Vec4 target;               /* +0x280 the person, 0.9 under the position */
    float facing;              /* +0x290 atan2 of the view direction (x, z) */
};

/* The view inside a home (0x2A0 bytes, vtable 0x1BD810): the talk view plus the walk-around state (mm_MyHome_ctor). */
struct MyHomeView {
    TalkView base;
    Vec4 position;             /* +0x270 viewer position, from the home's entrance */
    Vec4 target;               /* +0x280 looked-at point (position minus 0.1 along the facing) */
    float facing;              /* +0x290 turn about y, from the tile kind */
    float pitch;               /* +0x294 looking up and down, -0.05 to 0.1 */
    void *collision;           /* +0x298 the interior's triangle blob (table at 0x1F3CE0) */
    int32_t size;              /* +0x29C building size */
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

/* The claim meter limits at the start of the claim data (gp 0x1BDA50). */
struct ClaimThresholds {
    uint16_t low;              /* +0x00 above it the claim shows (band 1) */
    uint16_t high;             /* +0x02 above it band 2 */
    uint8_t step;              /* +0x04 meter rise per evaluation */
    uint8_t switchFactor;      /* +0x05 a building takes the neighbours' claim when its meter times this is below their weighted meters (mm_BldgClaims_EvalD) */
    uint8_t spreadStep;        /* +0x06 meter rise per weight of a claiming neighbour (mm_BldgClaims_EvalA) */
    uint8_t holdTime;          /* +0x07 claim timer once the meter maxes out */
};

/* Sound effect request (0x1C bytes): the frame's requests at 0x1F8710, kept in priority order by
   mm_Se_Request, count at 0x1BDD08 (up to 578). */
struct SeRequest {
    uint8_t sound;             /* +0x00 0xFF = none */
    uint8_t voice;             /* +0x01 from the sound's voice list */
    uint8_t unk_02[2];
    uint32_t owner;            /* +0x04 owner id (0xFFFF for sounds with flag 0x20) + (group * 16 + voice index) << 16 */
    uint16_t priority;         /* +0x08 the sound's priority bits << 8 + half the distance; lower comes first */
    uint8_t param0A;           /* +0x0A mm_Se_Request's a3; non-zero only updates a voice already playing it (mm_Se_AssignVoices) */
    uint8_t param0B;           /* +0x0B t2 (0x3C from mm_Sound_PlaySe) */
    int16_t param0C;           /* +0x0C t1, the voice's pitch */
    uint16_t voiceParam;       /* +0x0E second voice list byte + (sound flags & 0xF0) << 4 */
    int32_t volumeLeft;        /* +0x10 1..0x3FFF */
    int32_t volumeRight;       /* +0x14 */
    uint8_t param18;           /* +0x18 t0, frames before the sound can start again */
    uint8_t param19;           /* +0x19 t3, frames the voice is kept without requests */
    uint8_t unk_1A[2];
};

/* Effect voice (0x30 bytes): 16 of group A (driver voices 0..15) then 15 of group B at 0x1F8140, given to the
   frame's requests by mm_Se_AssignVoices. */
struct SeVoice {
    uint8_t sound;             /* +0x00 0xFF = free */
    uint8_t voice;             /* +0x01 */
    uint8_t unk_02[2];
    uint32_t owner;            /* +0x04 as SeRequest */
    uint16_t priority;         /* +0x08 the highest is stolen first */
    int16_t pitch;             /* +0x0A group A, sent with flag 2 (mm_Se_ApplyVoiceParams) */
    uint8_t param0B;           /* +0x0C group A */
    uint8_t scriptStep;        /* +0x0D step in the sound script (mm_Se_UpdateVoices) */
    uint16_t scriptTimer;      /* +0x0E frames left in a wait step; pitch slides keep a step count in the high byte */
    uint16_t voiceParam;       /* +0x10 low byte: sound script (0x1B3410 + 0x18 each); 0x100 (sound flag 0x10): kept
                                  until stopped; 0x800: a group A wait step holds after the sound ends */
    int16_t volumeOffset;      /* +0x12 added to both volumes */
    int32_t volumeLeft;        /* +0x14 */
    int32_t volumeRight;       /* +0x18 */
    uint8_t flags;             /* +0x1C 1 volume changed, 2 pitch changed, 0x80 requested this frame; 0 stops it */
    uint8_t hold;              /* +0x1D frames left without requests */
    uint8_t unk_1E[2];
    int32_t heldLeft;          /* +0x20 loudest volumes requested while held, taken on ambience phase 3 */
    int32_t heldRight;         /* +0x24 */
    int16_t heldPitch;         /* +0x28 group A: the pitch of the loudest */
    uint8_t rest;              /* +0x2A group A: frames before a freed voice is reused (20 when started) */
    uint8_t unk_2B[5];
};

/* Sound table entry (0x10 bytes) at 0x1B2EE0, by sound id (83). */
struct SeSound {
    uint8_t flags;             /* +0x00 0x0F priority, 0x10 owned (an owner id, kept until stopped), 0x20 shared owner */
    uint8_t maxVoices;         /* +0x01 requests started while playing is below it */
    uint8_t schedule;          /* +0x02 when a building's ambient sound plays (mm_Se_BuildingSoundState): 1 while open,
                                  2/3/4 a window in each hour, 5/10 on the hour, 11/12 in a month at night */
    uint8_t volume;            /* +0x03 percent, 0 = 100 */
    uint8_t playing;           /* +0x04 owned voices and requests started this frame (mm_Se_AssignVoices) */
    uint8_t cooldown;          /* +0x05 frames before it can start again */
    uint8_t unk_06[2];
    uint8_t voices[8];         /* +0x08 up to four (voice, parameter) pairs, voice 0xFF = none, 200 and up group B */
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
    float waterLevel;          /* +0xB0 height of the water surface (mm_LayerG_Draw, mm_Se_PickTerrainAmbience) */
    uint32_t flags;            /* +0xB4 2 holiday month, 4 popular (approval 118+), 0x40 pad focus after a talk */
    int32_t paydayScore;       /* +0xB8 raised through the month; with the families moved in and out it decides the salary rise (mm_EvPayday_Update) */
    int32_t pendingApproval;   /* +0xBC */
    int32_t trafficPhase;      /* +0xC0 traffic lights: 0 lets traffic along z through, 2 along x, 1 and 3 neither (mm_Char2_UpdateSpeedA) */
    int32_t trafficTimer;      /* +0xC4 0..999 per tick: phase 0 below 400, 1 below 500, 2 below 900, then 3 */
    int32_t fadeLevel;         /* +0xC8 */
    int32_t fade;              /* +0xCC fade counter, 0 = none */
    int32_t fadeSpeed;         /* +0xD0 */
    int32_t unk_D4;
};
