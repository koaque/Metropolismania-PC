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
struct TextBox { uint8_t data[0x40]; };
/* Window frame (0xC0 bytes). */
struct Frame { uint8_t data[0xC0]; };
/* Model instance from a .A3D file (0x60 bytes). */
struct A3DObject { uint8_t data[0x60]; };
/* Camera (0x310 bytes). */
struct Camera { uint8_t data[0x310]; };
/* City camera with the sky (0x540 bytes); eye at +0x130, target at +0x140. */
struct CityCamera { uint8_t data[0x540]; };
/* Disc file handle (0x8 bytes), mm_File_*. */
struct GameFile { uint8_t data[0x8]; };
/* Sprite from a .TEX file (0x38 bytes). */
struct TexSprite { uint8_t data[0x38]; };
/* Light set (0x1B0 bytes). */
struct LightSet { uint8_t data[0x1B0]; };
/* List menu widget (0x2A0 bytes). */
struct ListMenu { uint8_t data[0x2A0]; };
/* Loading screen (0x28 bytes). */
struct LoadingScreen { uint8_t data[0x28]; };
/* Memory card manager (0x28 bytes). */
struct McManager { uint8_t data[0x28]; };
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
struct CityCursor { uint8_t data[0x1E0]; };
/* Phone call manager (0x7C bytes). */
struct PhoneCalls { uint8_t data[0x7C]; };
/* Event manager (0xB0 bytes); +0xA4 and +0xA8 are the events the talk script works with. */
struct EventMgr { uint8_t data[0xB0]; };
/* Wants window (0x544 bytes); starts with the 0x504-byte wants list. */
struct WantsWin { uint8_t data[0x544]; };
/* Laptop menu (0x3E0 bytes). */
struct Laptop { uint8_t data[0x3E0]; };
/* Player character (0x90 bytes). */
struct Player { uint8_t data[0x90]; };
/* Applicants list (0x350 bytes): 32 entries of 0x14 bytes, count at +0x280. */
struct Applicants { uint8_t data[0x350]; };
/* Markers model (0x130 bytes). */
struct Markers { uint8_t data[0x130]; };
/* Notice line (0x14 bytes). */
struct Notice { uint8_t data[0x14]; };
/* Town's Pages phone book (0x64 bytes). */
struct TownPages { uint8_t data[0x64]; };
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
struct LayerA { uint8_t data[0x2440]; };
struct LayerB { uint8_t data[0x230]; };
struct LayerC { uint8_t data[0x240]; };
struct LayerD { uint8_t data[0x410]; };
struct LayerE { uint8_t data[0x6E0]; };
struct LayerF { uint8_t data[0x140]; };
struct LayerG { uint8_t data[0x2E0]; };
