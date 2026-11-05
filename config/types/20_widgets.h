/* Widget-derived classes (see Widget in 10_game.h), at their allocation sizes; fields replace the
   data arrays as functions are ported. */

typedef struct SceneB SceneB;
typedef struct MsgWin MsgWin;
typedef struct TalkCard TalkCard;
typedef struct TalkMoney TalkMoney;
typedef struct TalkConv TalkConv;

/* Scene B: stage introduction and briefing (0x104 bytes), a widget. */
struct SceneB { Widget base; uint8_t data[0xF8]; };
/* Message window (0x2CC bytes), a widget. */
struct MsgWin {
    Widget base;
    A3DObject *model;          /* +0x0C window model */
    TextBox *text;             /* +0x10 */
    uint32_t arrowPacket;      /* +0x14 blinking "more" arrow */
    uint8_t unk_018[0x2A8];
    int32_t typeDelay;         /* +0x2C0 frames before the next character; Cross clears it (mm_MsgWin_Update) */
    int32_t node;              /* +0x2C4 model node drawn as the window */
    uint8_t unk_2C8[4];
};
/* Menu window (0x20 bytes), a widget. */
struct MenuWin {
    Widget base;
    A3DObject *model;          /* +0x0C window model */
    ListMenu *list;            /* +0x10 */
    int32_t size;              /* +0x14 picks the window node: 3 + max(size - 2, 0) */
    int32_t unk_18;            /* +0x18 */
    int32_t unk_1C;            /* +0x1C */
};
/* Talk person card (0x38 bytes, vtable 0x1BD720), a widget: name, age and gauges of the person or family member
   chosen in a talk. */
struct TalkCard {
    Widget base;
    int32_t person;            /* +0x0C person record, 6000 = a family member by phone */
    int32_t family;            /* +0x10 */
    int32_t member;            /* +0x14 */
    int32_t slot;              /* +0x18 place on the screen: x from the table at 0x1B1B28 */
    void *model;               /* +0x1C the talk building model (0x1F3B2C) */
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
