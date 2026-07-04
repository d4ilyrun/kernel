#ifndef _DAILYRUN_INPUT_H
#define _DAILYRUN_INPUT_H

#include <stdint.h>

/* Data for INPUT_EV_KEY_PRESS and INPUT_EV_KEY_RELEASE events.
 *
 * These codes are a hardware-independent translation of the physical scan
 * codes sent by the device. They can be interpreted as locations for physical
 * keys.
 *
 * NOTE: These codes correspond to the US QWERTY physical mapping.
 */
enum key_code {
    KEY_NONE,

    /* regular keys */
    KEY_A,
    KEY_B,
    KEY_C,
    KEY_D,
    KEY_E,
    KEY_F,
    KEY_G,
    KEY_H,
    KEY_I,
    KEY_J,
    KEY_K,
    KEY_L,
    KEY_M,
    KEY_N,
    KEY_O,
    KEY_P,
    KEY_Q,
    KEY_R,
    KEY_S,
    KEY_T,
    KEY_U,
    KEY_V,
    KEY_W,
    KEY_X,
    KEY_Y,
    KEY_Z,

    /* number row */
    KEY_0,
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,

    /* keypad keys */
    KEY_KP_0,
    KEY_KP_1,
    KEY_KP_2,
    KEY_KP_3,
    KEY_KP_4,
    KEY_KP_5,
    KEY_KP_6,
    KEY_KP_7,
    KEY_KP_8,
    KEY_KP_9,
    KEY_KP_DOT,
    KEY_KP_PLUS,
    KEY_KP_MINUS,
    KEY_KP_ASTERISK,
    KEY_KP_SCROLL_LOCK,
    KEY_KP_SLASH,
    KEY_KP_ENTER,

    KEY_ESC,
    KEY_TAB,
    KEY_SPACE,
    KEY_ENTER,
    KEY_BACKSPACE,
    KEY_CAPSLOCK,

    KEY_DOT,
    KEY_COMMA,
    KEY_SLASH,
    KEY_STAR,
    KEY_MINUS,
    KEY_PLUS,
    KEY_EQUAL,
    KEY_SEMICOLON,
    KEY_BACKSLASH,
    KEY_BACKTICK,
    KEY_APOSTROPHE,

    KEY_LEFT_BRACE,
    KEY_RIGHT_BRACE,
    KEY_LEFT_PARENTHESIS,
    KEY_RIGHT_PARENTHESIS,

    /* arrow keys */
    KEY_UP,
    KEY_DOWN,
    KEY_RIGHT,
    KEY_LEFT,

    KEY_LEFT_CTRL,
    KEY_LEFT_SHIFT,
    KEY_LEFT_ALT,
    KEY_LEFT_SUPER,

    KEY_RIGHT_CTRL,
    KEY_RIGHT_SHIFT,
    KEY_RIGHT_ALT,
    KEY_RIGHT_SUPER,

    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_F11,
    KEY_F12,

    /* multimedia keys */
    KEY_PREV,
    KEY_NEXT,
    KEY_PLAY,
    KEY_STOP,
    KEY_VOLUME_UP,
    KEY_VOLUME_DOWN,
    KEY_MUTE,
    KEY_FAV,

    KEY_HOME,
    KEY_END,
    KEY_DEL,
    KEY_INSERT,
    KEY_PAUSE,
    KEY_PAGE_DOWN,
    KEY_PAGE_UP,
    KEY_SCROLL_LOCK,
    KEY_PRINT_SCREEN,
    KEY_NUMLOCK,
    KEY_SYSRQ,

    KEY_MOUSE_LEFT,
    KEY_MOUSE_RIGHT,
    KEY_MOUSE_MIDDLE,
};

/* Data for cursor movement events.
 *
 * For CURSOR_REL events a negative pos_x value means that the cursor
 * was moved down, and a negative pos_y value means that the cursor
 * was moved left.
 *
 * For CURSOR_ABS events these values should always be positive since
 * they represent an absolute position on screen.
 */
struct mouse_pos {
    int32_t pos_x;
    int32_t pos_y;
};

enum input_event_type {
    INPUT_EV_KEY_PRESS,   /* keyboard key pressed */
    INPUT_EV_KEY_RELEASE, /* keyboard key released */
    INPUT_EV_CURSOR_REL,  /* pointer device movement (relative position) */
    INPUT_EV_CURSOR_ABS,  /* pointer device movement (absolute position) */
};

struct input_event {
    enum input_event_type ev_type;
    union {
        enum key_code    key_code;   /* KEY_PRESSED, KEY_RELEASE */
        struct mouse_pos cursor_pos; /* CURSOR_REL, CURSOR_ABS */
    } ev_data;
};

#endif /* _DAILYRUN_INPUT_H */
