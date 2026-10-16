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
struct MsgWin { Widget base; uint8_t data[0x2C0]; };
/* Menu window (0x20 bytes), a widget. */
struct MenuWin { Widget base; uint8_t data[0x14]; };
/* Talk person card (0x38 bytes), a widget. */
struct TalkCard { Widget base; uint8_t data[0x2C]; };
/* Talk money panel (0x1C bytes), a widget. */
struct TalkMoney { Widget base; uint8_t data[0x10]; };
/* Talk conversation (0x508 bytes), a message window. */
struct TalkConv { MsgWin base; uint8_t data[0x23C]; };
