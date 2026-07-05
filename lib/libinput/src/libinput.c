#include <libinput/libinput.h>

#include <stdio.h>

static const char *key_code_names[] = {
    "KEY_NONE",

    /* regular keys */
    "KEY_A",
    "KEY_B",
    "KEY_C",
    "KEY_D",
    "KEY_E",
    "KEY_F",
    "KEY_G",
    "KEY_H",
    "KEY_I",
    "KEY_J",
    "KEY_K",
    "KEY_L",
    "KEY_M",
    "KEY_N",
    "KEY_O",
    "KEY_P",
    "KEY_Q",
    "KEY_R",
    "KEY_S",
    "KEY_T",
    "KEY_U",
    "KEY_V",
    "KEY_W",
    "KEY_X",
    "KEY_Y",
    "KEY_Z",

    /* number row */
    "KEY_0",
    "KEY_1",
    "KEY_2",
    "KEY_3",
    "KEY_4",
    "KEY_5",
    "KEY_6",
    "KEY_7",
    "KEY_8",
    "KEY_9",

    /* keypad keys */
    "KEY_KP_0",
    "KEY_KP_1",
    "KEY_KP_2",
    "KEY_KP_3",
    "KEY_KP_4",
    "KEY_KP_5",
    "KEY_KP_6",
    "KEY_KP_7",
    "KEY_KP_8",
    "KEY_KP_9",
    "KEY_KP_DOT",
    "KEY_KP_PLUS",
    "KEY_KP_MINUS",
    "KEY_KP_ASTERISK",
    "KEY_KP_SCROLL_LOCK",
    "KEY_KP_SLASH",
    "KEY_KP_ENTER",

    "KEY_ESC",
    "KEY_TAB",
    "KEY_SPACE",
    "KEY_ENTER",
    "KEY_BACKSPACE",
    "KEY_CAPSLOCK",

    "KEY_DOT",
    "KEY_COMMA",
    "KEY_SLASH",
    "KEY_STAR",
    "KEY_MINUS",
    "KEY_PLUS",
    "KEY_EQUAL",
    "KEY_SEMICOLON",
    "KEY_BACKSLASH",
    "KEY_BACKTICK",
    "KEY_APOSTROPHE",

    "KEY_LEFT_BRACE",
    "KEY_RIGHT_BRACE",
    "KEY_LEFT_PARENTHESIS",
    "KEY_RIGHT_PARENTHESIS",

    /* arrow keys */
    "KEY_UP",
    "KEY_DOWN",
    "KEY_RIGHT",
    "KEY_LEFT",

    "KEY_LEFT_CTRL",
    "KEY_LEFT_SHIFT",
    "KEY_LEFT_ALT",
    "KEY_LEFT_SUPER",

    "KEY_RIGHT_CTRL",
    "KEY_RIGHT_SHIFT",
    "KEY_RIGHT_ALT",
    "KEY_RIGHT_SUPER",

    "KEY_F1",
    "KEY_F2",
    "KEY_F3",
    "KEY_F4",
    "KEY_F5",
    "KEY_F6",
    "KEY_F7",
    "KEY_F8",
    "KEY_F9",
    "KEY_F10",
    "KEY_F11",
    "KEY_F12",

    /* multimedia keys */
    "KEY_PREV",
    "KEY_NEXT",
    "KEY_PLAY",
    "KEY_STOP",
    "KEY_VOLUME_UP",
    "KEY_VOLUME_DOWN",
    "KEY_MUTE",
    "KEY_FAV",

    "KEY_HOME",
    "KEY_END",
    "KEY_DEL",
    "KEY_INSERT",
    "KEY_PAUSE",
    "KEY_PAGE_DOWN",
    "KEY_PAGE_UP",
    "KEY_SCROLL_LOCK",
    "KEY_PRINT_SCREEN",
    "KEY_NUMLOCK",
    "KEY_SYSRQ",

    "KEY_MOUSE_LEFT",
    "KEY_MOUSE_RIGHT",
    "KEY_MOUSE_MIDDLE",
};

static const char *ev_type_names[] = {
    [INPUT_EV_KEY_PRESS]    = "EV_KEY_PRESS  ",
    [INPUT_EV_KEY_RELEASE]  = "EV_KEY_RELEASE",
    [INPUT_EV_CURSOR_REL]   = "EV_CURSOR_REL ",
    [INPUT_EV_CURSOR_ABS]   = "EV_CURSOR_ABS ",
};

void input_event_dump(const struct input_event *ev)
{
    switch (ev->ev_type) {
    case INPUT_EV_KEY_PRESS:
    case INPUT_EV_KEY_RELEASE:
        printf("%s %s\n",
               ev_type_names[ev->ev_type],
               key_code_names[ev->ev_data.key_code]);
        break;
    case INPUT_EV_CURSOR_REL:
    case INPUT_EV_CURSOR_ABS:
        printf("%s (%d,%d)\n",
               ev_type_names[ev->ev_type],
               ev->ev_data.cursor_pos.pos_x, ev->ev_data.cursor_pos.pos_y);
        break;
    }
}
