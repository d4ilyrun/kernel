/*
 * Driver for the I8042 PS/2 Controller.
 *
 * @ref http://wiki.osdev.org/I8042_PS/2_Controller
 * @ref https://users.utcluj.ro/~baruch/sie/labor/PS2/PS-2_Mouse_Interface.htm
 */

#define LOG_DOMAIN "ps2"

#include <kernel/device.h>
#include <kernel/init.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/devices/input.h>
#include <kernel/kmalloc.h>

#include <kernel/arch/i686/devices/pic.h>
#include <kernel/arch/i686/devices/pit.h>

#include <dailyrun/input.h>

#include <utils/macro.h>
#include <utils/math.h>

#include <string.h>

#define PORT_DATA    0x60 /* Data from/to the controller or device (R/W) */
#define PORT_STATUS  0x64
#define   STATUS_OUTPUT_BUFFER  BIT(0) /* must be set before READING from DATA */
#define   STATUS_INPUT_BUFFER   BIT(1) /* must be set before WRITING to DATA */
#define   STATUS_SYSTEM_FLAG    BIT(2) /* set by firmware after POST */
#define   STATUS_COMMAND        BIT(3) /* indicate whether data written is for
                                       * the controller or for the device. */
#define PORT_COMMAND 0x64 /* Send command to the controller (RO) */
#define   COMMAND_READ_CONFIG   0x20 /* read controller config byte */
#define   COMMAND_WRITE_CONFIG  0x60 /* write controller config byte */
#define   COMMAND_DISABLE_PORT2 0xA7 /* disable second PS/2 port */
#define   COMMAND_ENABLE_PORT2  0xA8 /* enable second PS/2 port */
#define   COMMAND_PORT2_TEST    0xA9 /* test PS/2 port 2 */
#define   COMMAND_SELF_TEST     0xAA /* test PS/2 controller */
#define   COMMAND_PORT1_TEST    0xAB /* test PS/2 port 1 */
#define   COMMAND_DISABLE_PORT1 0xAD /* disable first PS/2 port */
#define   COMMAND_ENABLE_PORT1  0xAE /* enable first PS/2 port */
#define   COMMAND_ENABLE_PORT1  0xAE /* enable first PS/2 port */
#define   COMMAND_PORT2_WRITE   0xD4 /* send next byte to the second port  */

/* Configuration byte format (commands 0x20 & 0x60) */
#define CFG_PORT1_INTERRUPT     BIT(0) // Port 1 interrupt enable
#define CFG_PORT2_INTERRUPT     BIT(1) // Port 2 interrupt enable
#define CFG_SYSTEM_FLAG         BIT(2) // System Flag (1 = system passed POST)
#define CFG_PORT1_CLOCK_DIS     BIT(4) // Port 1 clock disable
#define CFG_PORT2_CLOCK_DIS     BIT(5) // Port 2 clock disable
#define CFG_PORT1_TRANSLATE     BIT(6) // Port 1 translation enable

#define DEVICE_COMMAND_IDENTIFY      0xF2
#define DEVICE_COMMAND_ENABLE        0xF4 /* Enable data reporting  */
#define DEVICE_COMMAND_DISABLE       0xF5 /* Disable data reporting  */
#define DEVICE_COMMAND_SET_DEFAULTS  0xF6 /* Set resolution  */
#define DEVICE_COMMAND_ACK           0xFA /* Returned by device after a command */

#define MOUSE_COMMAND_SCALE_1        0xE6
#define MOUSE_COMMAND_SCALE_2        0xE7
#define MOUSE_COMMAND_SET_RESOLUTION 0xE8

#define KEYBOARD_COMMAND_SCAN_CODE   0xF0

/* Data sent by the controller when the mouse is moved. */
struct mouse_packet {
    u8 flags;
    u8 x_movement;
    u8 y_movement;
};

#define MOUSE_FLAG_BL           BIT(0)
#define MOUSE_FLAG_BR           BIT(1)
#define MOUSE_FLAG_BM           BIT(2)
#define MOUSE_FLAG_X_SIGN       BIT(4)
#define MOUSE_FLAG_Y_SIGN       BIT(5)
#define MOUSE_FLAG_X_OVERFLOW   BIT(6)
#define MOUSE_FLAG_Y_OVERFLOW   BIT(7)

struct i8042_mouse {
    struct input_device input_dev;
    struct mouse_packet cur_packet;
    unsigned int        cur_step;
    u8                  prev_packet_flags;
};

struct i8042_keyboard {
    struct input_device input_dev;
    /* status for the current scan code */
    bool cur_is_extended;
    bool cur_is_release;
    enum key_code cur_code;
};

/*
 * Read a byte from the controller's output buffer.
 */
static inline u8 i8042_read_data(void)
{
    int timeout = 100000;

    while (--timeout) {
        /* Wait until the controller's output buffer has data in it. */
        if (inb(PORT_STATUS) & STATUS_OUTPUT_BUFFER)
            return inb(PORT_DATA);
    }

    return 0xFE;
}

/*
 * Write a byte into the controller's input buffer.
 */
static inline bool i8042_write_data(u8 data)
{
    int timeout = 100000;

    while (--timeout) {
        /* Wait until the controller's input buffer is empty. */
        if(!(inb(PORT_STATUS) & STATUS_INPUT_BUFFER)) {
            outb(PORT_DATA, data);
            return true;
        }
    }

    return false;
}

/*
 *
 */
static inline void i8042_send_command(u8 command)
{
    outb(PORT_COMMAND, command);
}

/*
 *
 */
static inline void i8042_send_command_with_data(u8 command, u8 data)
{
    i8042_send_command(command);
    i8042_write_data(data);
}

/*
 *
 */
static inline u8 i8042_read_command_result(void)
{
    return i8042_read_data();
}

/*
 * Send data to the first device (i.e. the keyboard).
 */
static inline u8 i8042_keyboard_write(u8 byte)
{
    if (!i8042_write_data(byte))
        return 0xFF;

    return i8042_read_data();
}

/*
 * Send data to the second device (i.e. the mouse).
 */
static inline u8 i8042_mouse_write(u8 byte)
{
    i8042_send_command(COMMAND_PORT2_WRITE);
    if (!i8042_write_data(byte))
        return 0xFF;

    return i8042_read_data();
}

#define SCAN_CODE_EXTENDED  0xE0
#define SCAN_CODE_RELEASE   0xF0
#define SCAN_CODE_UNSET     0xFF

/* raw set 2 scancode table (copied from linux) */
static const enum key_code key_codes[256] = {
/* 00 */  KEY_NONE,     KEY_F9,             KEY_NONE,           KEY_F5,             KEY_F3,             KEY_F1,         KEY_F2,             KEY_F12,
/* 08 */  KEY_ESC,      KEY_F10,            KEY_F8,             KEY_F6,             KEY_F4,             KEY_TAB,        KEY_BACKTICK,       KEY_F2,
/* 10 */  KEY_NONE,     KEY_LEFT_ALT,       KEY_LEFT_SHIFT,     KEY_NONE,           KEY_LEFT_CTRL,      KEY_Q,          KEY_1,              KEY_F3,
/* 18 */  KEY_NONE,     KEY_LEFT_ALT,       KEY_Z,              KEY_S,              KEY_A,              KEY_W,          KEY_2,              KEY_F4,
/* 20 */  KEY_NONE,     KEY_C,              KEY_X,              KEY_D,              KEY_E,              KEY_4,          KEY_3,              KEY_F5,
/* 28 */  KEY_NONE,     KEY_SPACE,          KEY_V,              KEY_F,              KEY_T,              KEY_R,          KEY_5,              KEY_F6,
/* 30 */  KEY_NONE,     KEY_N,              KEY_B,              KEY_H,              KEY_G,              KEY_Y,          KEY_6,              KEY_F7,
/* 38 */  KEY_NONE,     KEY_RIGHT_ALT,      KEY_M,              KEY_J,              KEY_U,              KEY_7,          KEY_8,              KEY_F8,
/* 40 */  KEY_NONE,     KEY_COMMA,          KEY_K,              KEY_I,              KEY_O,              KEY_0,          KEY_9,              KEY_F9,
/* 48 */  KEY_NONE,     KEY_DOT,            KEY_SLASH,          KEY_L,              KEY_SEMICOLON,      KEY_P,          KEY_MINUS,          KEY_F10,
/* 50 */  KEY_NONE,     KEY_NONE,           KEY_APOSTROPHE,     KEY_NONE,           KEY_LEFT_BRACE,     KEY_EQUAL,      KEY_F11,            KEY_SYSRQ,
/* 58 */  KEY_CAPSLOCK, KEY_RIGHT_SHIFT,    KEY_ENTER,          KEY_RIGHT_BRACE,    KEY_BACKSLASH,      KEY_BACKSLASH,  KEY_F12,            KEY_SCROLL_LOCK,
/* 60 */  KEY_DOWN,     KEY_NONE,           KEY_PAUSE,          KEY_UP,             KEY_DEL,            KEY_END,        KEY_BACKSPACE,      KEY_INSERT,
/* 68 */  KEY_NONE,     KEY_KP_1,           KEY_RIGHT,          KEY_KP_4,           KEY_KP_7,           KEY_PAGE_DOWN,  KEY_HOME,           KEY_PAGE_UP,
/* 70 */  KEY_KP_0,     KEY_KP_DOT,         KEY_KP_2,           KEY_KP_5,           KEY_KP_6,           KEY_KP_8,       KEY_ESC,            KEY_NUMLOCK,
/* 78 */  KEY_F11,      KEY_KP_PLUS,        KEY_KP_3,           KEY_KP_MINUS,       KEY_KP_ASTERISK,    KEY_KP_9,       KEY_SCROLL_LOCK,    KEY_NONE,
/* 80 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_F7,             KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 88 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 90 */  KEY_NONE,     KEY_RIGHT_ALT,      KEY_NONE,           KEY_NONE,           KEY_RIGHT_CTRL,     KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 98 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_CAPSLOCK,   KEY_NONE,           KEY_LEFT_SUPER,
/* a0 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_RIGHT_SUPER,
/* a8 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* b0 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* b8 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* c0 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* c8 */  KEY_NONE,     KEY_NONE,           KEY_KP_SLASH,       KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* d0 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* d8 */  KEY_NONE,     KEY_NONE,           KEY_KP_ENTER,       KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* e0 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* e8 */  KEY_NONE,     KEY_END,            KEY_NONE,           KEY_LEFT,           KEY_HOME,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* f0 */  KEY_INSERT,   KEY_DEL,            KEY_DOWN,           KEY_NONE,           KEY_RIGHT,          KEY_UP,         KEY_NONE,           KEY_PAUSE,
/* f8 */  KEY_NONE,     KEY_NONE,           KEY_PAGE_DOWN,      KEY_NONE,           KEY_SYSRQ,          KEY_PAGE_UP,    KEY_NONE,           KEY_NONE,
};

/* These are for scan codes that come after a 0xE0 */
static const enum key_code extended_key_codes[256] = {
/* 00 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_F7,             KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 08 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_LEFT_SUPER,     KEY_RIGHT_SUPER,    KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 10 */  KEY_NONE,     KEY_RIGHT_ALT,      KEY_NONE,           KEY_NONE,           KEY_RIGHT_CTRL,     KEY_PREV,       KEY_NONE,           KEY_NONE,
/* 18 */  KEY_FAV,      KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_LEFT_SUPER,
/* 20 */  KEY_NONE,     KEY_VOLUME_DOWN,    KEY_NONE,           KEY_MUTE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_RIGHT_SUPER,
/* 28 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 30 */  KEY_NONE,     KEY_NONE,           KEY_VOLUME_UP,      KEY_NONE,           KEY_PLAY,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 38 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_STOP,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 40 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 48 */  KEY_NONE,     KEY_NONE,           KEY_KP_SLASH,       KEY_NONE,           KEY_NONE,           KEY_NEXT,       KEY_NONE,           KEY_NONE,
/* 50 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 58 */  KEY_NONE,     KEY_NONE,           KEY_KP_ENTER,       KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 60 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 68 */  KEY_NONE,     KEY_END,            KEY_NONE,           KEY_LEFT,           KEY_HOME,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 70 */  KEY_INSERT,   KEY_DEL,            KEY_DOWN,           KEY_NONE,           KEY_RIGHT,          KEY_UP,         KEY_NONE,           KEY_NONE,
/* 78 */  KEY_NONE,     KEY_NONE,           KEY_PAGE_DOWN,      KEY_NONE,           KEY_NONE,           KEY_PAGE_UP,    KEY_NONE,           KEY_NONE,
/* 80 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 88 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 90 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* 98 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* a0 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* a8 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* b0 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* b8 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* c0 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* c8 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* d0 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* d8 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* e0 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* e8 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* f0 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE,
/* f8 */  KEY_NONE,     KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,           KEY_NONE,       KEY_NONE,           KEY_NONE
};

/*
 * Hardware interrupt
 */
static interrupt_return_t i8042_keyboard_interrupt(void *cookie)
{
    struct i8042_keyboard *kb = cookie;
    interrupt_return_t ret = INTERRUPT_HANDLED;
    u8 code;

    code = inb(PORT_DATA); /* no polling needed in interrupts. */
    switch (code) {
    case SCAN_CODE_RELEASE:
        kb->cur_is_release = true;
        break;
    case SCAN_CODE_EXTENDED:
        kb->cur_is_extended = true;
        break;
    default:
        if (kb->cur_code == SCAN_CODE_UNSET) {
            kb->cur_code = code;
            ret = INTERRUPT_THREADED;
        } else
            log_warn("kb: rx buffer full, discarding input");
        break;
    }

    return ret;
}

/*
 *
 */
static interrupt_return_t i8042_keyboard_threaded_interrupt(void *cookie)
{
    struct i8042_keyboard *kb = cookie;
    struct input_event ev = { 0 };

    ev.ev_type = kb->cur_is_release ? INPUT_EV_KEY_RELEASE : INPUT_EV_KEY_PRESS;
    ev.ev_data.key_code = kb->cur_is_extended ? extended_key_codes[kb->cur_code]
                                              : key_codes[kb->cur_code];
    input_device_push_event(&kb->input_dev, &ev);

    kb->cur_is_release = false;
    kb->cur_is_extended = false;
    kb->cur_code = SCAN_CODE_UNSET;

    return INTERRUPT_HANDLED;
}

/*
 *
 */
static void i8042_handle_mouse_packet(struct i8042_mouse *mouse)
{
    struct mouse_packet *packet = &mouse->cur_packet;
    u8 changed_flags = packet->flags ^ mouse->prev_packet_flags;
    int x_movement = packet->x_movement;
    int y_movement = packet->y_movement;
    struct input_event ev = { 0 };

    /* movement value is a 9bit signed value, convert it when negative */
    if (packet->flags & MOUSE_FLAG_X_SIGN)
        x_movement -= BIT(8);
    if (packet->flags & MOUSE_FLAG_Y_SIGN)
        y_movement -= BIT(8);

    /* mouse clicks are treated as regular key presses */
    ev.ev_type = INPUT_EV_KEY_PRESS;
    if (changed_flags & MOUSE_FLAG_BL) {
        ev.ev_data.key_code = KEY_MOUSE_LEFT;
        input_device_push_event(&mouse->input_dev, &ev);
    }
    if (changed_flags & MOUSE_FLAG_BR) {
        ev.ev_data.key_code = KEY_MOUSE_RIGHT;
        input_device_push_event(&mouse->input_dev, &ev);
    }
    if (changed_flags & MOUSE_FLAG_BM) {
        ev.ev_data.key_code = KEY_MOUSE_MIDDLE;
        input_device_push_event(&mouse->input_dev, &ev);
    }

    if (x_movement || y_movement) {
        ev.ev_type = INPUT_EV_CURSOR_REL;
        ev.ev_data.cursor_pos.pos_x = x_movement;
        ev.ev_data.cursor_pos.pos_y = y_movement;
        input_device_push_event(&mouse->input_dev, &ev);
    }

    mouse->prev_packet_flags = packet->flags;
}

/*
 *
 */
static interrupt_return_t i8042_mouse_interrupt(void *cookie)
{
    struct i8042_mouse *mouse = cookie;
    interrupt_return_t ret = INTERRUPT_HANDLED;
    u8 byte;

    byte = inb(PORT_DATA); /* no polling needed in interrupts. */
    switch (mouse->cur_step++) {
    case 0:
        memset(&mouse->cur_packet, 0, sizeof(mouse->cur_packet));
        mouse->cur_packet.flags = byte;
        break;
    case 1:
        mouse->cur_packet.x_movement = byte;
        break;
    case 2:
        mouse->cur_packet.y_movement = byte;
        mouse->cur_step = 0;
        ret = INTERRUPT_THREADED;
        break;

    default:
        assert_not_reached();
    }

    return ret;
}

/*
 *
 */
static interrupt_return_t i8042_mouse_threaded_interrupt(void *mouse)
{
    i8042_handle_mouse_packet(mouse);
    return INTERRUPT_THREADED;
}

/*
 *
 */
static error_t i8042_init_keyboard(void)
{
    struct i8042_keyboard *kb;
    error_t err;

    kb = kcalloc(1, sizeof(*kb), KMALLOC_KERNEL);
    if (!kb)
        return E_NOMEM;

    err = register_input_device(&kb->input_dev);
    if (err) {
        kfree(kb);
        return err;
    }

    kb->cur_code = SCAN_CODE_UNSET;

    i8042_send_command(COMMAND_ENABLE_PORT1);
    i8042_keyboard_write(DEVICE_COMMAND_ENABLE);

    /* select scan code 2 */
    i8042_keyboard_write(KEYBOARD_COMMAND_SCAN_CODE);
    i8042_keyboard_write(2);

    interrupts_install_threaded_handler(PIC_MASTER_VECTOR + IRQ_KEYBOARD,
                                        i8042_keyboard_interrupt,
                                        i8042_keyboard_threaded_interrupt,
                                        kb);

    return E_SUCCESS;
}

/*
 *
 */
static error_t i8042_init_mouse(void)
{
    struct i8042_mouse *mouse;
    error_t err;

    mouse = kcalloc(1, sizeof(*mouse), KMALLOC_KERNEL);
    if (!mouse)
        return E_NOMEM;

    err = register_input_device(&mouse->input_dev);
    if (err) {
        kfree(mouse);
        return err;
    }

    i8042_send_command(COMMAND_ENABLE_PORT2);
    i8042_mouse_write(DEVICE_COMMAND_ENABLE);

    i8042_mouse_write(MOUSE_COMMAND_SET_RESOLUTION);
    i8042_mouse_write(0); /* 1 movement unit per milimeter */

    interrupts_install_threaded_handler(PIC_MASTER_VECTOR + IRQ_PS2,
                                        i8042_mouse_interrupt,
                                        i8042_mouse_threaded_interrupt,
                                        mouse);

    return E_SUCCESS;
}

/*
 *
 */
static error_t i8042_init(void)
{
    bool has_keyboard = true;
    bool has_mouse = true;
    u8 byte;

    /* TODO: Disable USB legacy support of PS/2 devices before anything else. */
    /* TODO: Detect PS/2 presence through ACPI */

    /* disable both devices during the initialization */
    i8042_send_command(COMMAND_DISABLE_PORT1);
    i8042_send_command(COMMAND_DISABLE_PORT2);

    /* flush the controller's output buffer */
    for (int i = 0; i < 1024; ++i)
        if (i8042_read_data() == 0xFE)
            break;

    /* Initialize both channels, but keep interrupts disabled for now. */
    i8042_send_command(COMMAND_READ_CONFIG);
    byte = i8042_read_command_result();
    byte &= ~(CFG_PORT1_INTERRUPT | CFG_PORT2_INTERRUPT);
    byte &= ~(CFG_PORT1_CLOCK_DIS | CFG_PORT2_CLOCK_DIS);
    byte &= ~CFG_PORT1_TRANSLATE;
    i8042_send_command_with_data(COMMAND_WRITE_CONFIG, byte);

    i8042_send_command(COMMAND_SELF_TEST);
    if (i8042_read_command_result() != 0x55) {
        log_err("controller self test failed");
        return E_IO;
    }

    /* NOTE: On some controllers, performing a self test resets the controller.
     *       To account for such cases we need to restore the config byte.
     */
    i8042_send_command_with_data(COMMAND_WRITE_CONFIG, byte);

    i8042_send_command(COMMAND_PORT1_TEST);
    if ((byte = i8042_read_command_result())) {
        log_err("port 1 test failed: %x", byte);
        has_keyboard = false;
    }

    i8042_send_command(COMMAND_PORT2_TEST);
    if ((byte = i8042_read_command_result())) {
        log_err("port 2 test failed: %x", byte);
        has_mouse = false;
    }

    if (has_keyboard)
        i8042_init_keyboard();
    if (has_mouse)
        i8042_init_mouse();

    /* enable interrupts */
    i8042_send_command(COMMAND_READ_CONFIG);
    byte = i8042_read_command_result();
    byte |= CFG_PORT1_INTERRUPT | CFG_PORT2_INTERRUPT;
    i8042_send_command_with_data(COMMAND_WRITE_CONFIG, byte);

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_LATE, i8042_init);
