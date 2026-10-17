/* Widget-derived classes (see Widget in 10_game.h), at their allocation sizes; fields replace the
   data arrays as functions are ported. */

typedef struct SceneB SceneB;
typedef struct MsgWin MsgWin;
typedef struct MenuWin MenuWin;
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
    uint8_t unk_018[0x2AC];
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
/* Talk person card (0x38 bytes), a widget. */
struct TalkCard { Widget base; uint8_t data[0x2C]; };
/* Talk money panel (0x1C bytes), a widget. */
struct TalkMoney { Widget base; uint8_t data[0x10]; };
/* Talk conversation (0x508 bytes), a message window. */
struct TalkConv { MsgWin base; uint8_t data[0x23C]; };
