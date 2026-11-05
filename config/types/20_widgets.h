/* Widget-derived classes (see Widget in 10_game.h), at their allocation sizes; fields replace the
   data arrays as functions are ported. */

typedef struct StageScene StageScene;
typedef struct MsgWin MsgWin;
typedef struct TalkCard TalkCard;
typedef struct TalkMoney TalkMoney;
typedef struct TalkConv TalkConv;

/* The stage scene: the stage transition (0x104 bytes, mm_StageScene_*): title cards, approval rate, the boss's briefing, the mission
   movie and the credits text, one step of the action table 0x1B07C0 at a time. A stage object with a widget at +4. */
struct StageScene {
    uint32_t unk_00;
    Widget widget;             /* +0x04 vtable 0x1BCF18 */
    Camera *camera;            /* +0x10 */
    LightSet *lights;          /* +0x14 */
    TextBox *caption;          /* +0x18 the title card's or the credits' text */
    uint32_t unk_1C;
    TexSprite *picture;        /* +0x20 the ending roll's picture (END_ROLL.TEX) */
    int32_t stage;             /* +0x24 the stage number, one more after loading the next stage */
    uint8_t step;              /* +0x28 the step in the action table */
    uint8_t fadeMode;          /* +0x29 0 none, 1 fading out, 2 fading in */
    uint8_t unk_2A[2];
    int32_t fadeFrame;         /* +0x2C frames of the fade */
    int32_t unk_30;
    int32_t phase;             /* +0x34 the sub-step within the step */
    int32_t fade;              /* +0x38 the screen's fade, 0 to 0x80 */
    int32_t scroll;            /* +0x3C the credits text's scroll (mm_StageScene_ShowTelop) */
    float animTime;            /* +0x40 the boss's animation frame */
    int32_t animation;         /* +0x44 the boss's animation */
    int32_t nextAnimation;     /* +0x48 the boss's animation once the current one ends, -1 none (2 after the briefing's text) */
    int32_t briefingIndex;     /* +0x4C the message within the phase (mm_StageScene_BuildBriefingText) */
    int32_t briefingPhase;     /* +0x50 0 the stage's special messages, 1 its general text, 2 the goal */
    A3DObject *office;         /* +0x54 the office model (0x1F3B2C) the briefing is drawn in */
    MsgWin *window;            /* +0x58 the briefing's message window */
    char text[0xA0];           /* +0x5C the briefing's text being shown */
    A3DObject *boss;           /* +0xFC the boss (0x1F3C68) */
    void *vtable;              /* +0x100 0x1BCF10 */
};
/* Message window (0x2CC bytes), a widget. */
struct MsgWin {
    Widget base;
    A3DObject *model;          /* +0x0C window model */
    TextBox *text;             /* +0x10 */
    uint32_t arrowPacket;      /* +0x14 blinking "more" arrow */
    char source[0x200];        /* +0x018 the message being typed */
    char lines[0x94];          /* +0x218 four lines of 36 columns, as shown */
    char *readAt;              /* +0x2AC the next character of the message (mm_MsgWin_TypeNext) */
    char *wordEnd;             /* +0x2B0 the end of the word being typed */
    char *writeAt;             /* +0x2B4 where the next character goes in the lines */
    int32_t exactFit;          /* +0x2B8 the last word exactly filled its line */
    int32_t bracketed;         /* +0x2BC inside brackets (the glyph pairs at 0x1B5A70), typed without the sound */
    int32_t typeDelay;         /* +0x2C0 frames before the next character; Cross clears it (mm_MsgWin_Update) */
    int32_t node;              /* +0x2C4 model node drawn as the window */
    uint8_t sound;             /* +0x2C8 the message sound (mm_MsgWin_SetMessage) */
    uint8_t unk_2C9[3];
};
/* Menu window (0x20 bytes), a widget. */
struct MenuWin {
    Widget base;
    A3DObject *model;          /* +0x0C window model */
    ListMenu *list;            /* +0x10 */
    int32_t size;              /* +0x14 picks the window node: 3 + max(size - 2, 0) */
    int32_t choice;            /* +0x18 the item chosen, -1 none (mm_ListMenu_HandleInput) */
    int32_t wideList;          /* +0x1C the list of menu -0x7D1 (14 columns, any number of rows); others 13 columns and up
                                  to 4 rows (mm_MenuWin_Open) */
};
/* Talk person card (0x38 bytes, vtable 0x1BD720), a widget: name, age and gauges of the person or family member
   chosen in a talk. */
struct TalkCard {
    Widget base;
    int32_t person;            /* +0x0C person record, 6000 = a family member by phone */
    int32_t family;            /* +0x10 */
    int32_t member;            /* +0x14 */
    int32_t slot;              /* +0x18 place on the screen: x from the table at 0x1B1B28 */
    A3DObject *model;          /* +0x1C the talk building model (0x1F3B2C) */
    TextBox *lowerText;        /* +0x20 at y 0x7D60 */
    TextBox *upperText;        /* +0x24 at y 0x7C00 */
    CityScene *city;           /* +0x28 */
    float friendship;          /* +0x2C the friendship meter */
    Frame *frame;              /* +0x30 */
    int32_t mood;              /* +0x34 gauge face: 0 none, 1 to 4 (mm_TalkCard_Show) */
};
/* Talk money panel (0x1C bytes, vtable 0x1BD7C0), a widget: the shown amount counts towards the real one
   by 100 a frame (mm_TalkMoney_Active). */
struct TalkMoney {
    Widget base;
    int32_t shown;             /* +0x0C */
    int32_t amount;            /* +0x10 */
    A3DObject *model;          /* +0x14 the talk building model (0x1F3B2C) */
    TextBox *text;             /* +0x18 */
};
/* Talk conversation (0x508 bytes), a message window. */
struct TalkConv {
    MsgWin base;
    CityScene *city;           /* +0x2CC */
    int32_t script;            /* +0x2D0 the words script being read (mm_Words_LoadForPerson, mm_Words_Step) */
    MenuWin *menu;             /* +0x2D4 */
    Widget *panel;             /* +0x2D8 drawn while set (provisional: the money panel) */
    NameEntry *nameEntry;      /* +0x2DC while entering a name (state 8) */
    TalkCard *card;            /* +0x2E0 */
    int32_t code;              /* +0x2E4 what the script returned for the last piece: -0x1777 the end, -0x7D5 a menu with no
                                  choice, -0x7D1 a typed answer, below -0x7A8 a name to enter (mm_TalkConv_NextLine) */
    char text[0x200];          /* +0x2E8 the piece of text being shown */
    int32_t byFamily;          /* +0x4E8 the talk's target, as mm_TalkConv_Start was given it */
    int32_t person;            /* +0x4EC */
    int32_t family;            /* +0x4F0 */
    int32_t member;            /* +0x4F4 */
    int32_t peopleInside;      /* +0x4F8 the building has people inside (type flag 0x40) */
    int32_t arg5;              /* +0x4FC */
    int32_t ended;             /* +0x500 the conversation reached its end (the friendship went up) */
    int32_t typedAnswer;       /* +0x504 the script asked for a typed answer */
};
