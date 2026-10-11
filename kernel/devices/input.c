#define LOG_DOMAIN "input"

#include <kernel/console.h>
#include <kernel/devices/input.h>
#include <kernel/file.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/vfs.h>

#include <utils/macro.h>
#include <utils/math.h>

#include <string.h>

static atomic_t input_dev_count = {0};

#ifdef DEBUG_INPUT
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
    [INPUT_EV_KEY_PRESS] = "EV_KEY_PRESS  ",
    [INPUT_EV_KEY_RELEASE] = "EV_KEY_RELEASE",
    [INPUT_EV_CURSOR_REL] = "EV_CURSOR_REL ",
    [INPUT_EV_CURSOR_ABS] = "EV_CURSOR_ABS ",
};

static inline void input_event_dump(const struct input_device *dev, const struct input_event *ev)
{
	switch (ev->ev_type) {
	case INPUT_EV_KEY_PRESS:
	case INPUT_EV_KEY_RELEASE:
		log_dbg("%s: %s %s", device_name(&dev->dev), ev_type_names[ev->ev_type],
			key_code_names[ev->ev_data.key_code]);
		break;
	case INPUT_EV_CURSOR_REL:
	case INPUT_EV_CURSOR_ABS:
		log_dbg("%s: %s (%d,%d)", device_name(&dev->dev), ev_type_names[ev->ev_type],
			ev->ev_data.cursor_pos.pos_x, ev->ev_data.cursor_pos.pos_y);
		break;
	}
}

#else
#define input_event_dump(...)
#endif

static bool input_key_code_translate(enum key_code code, char *c)
{
	static const char key_code_to_ascii[] = {
		[KEY_A] = 'a', [KEY_B] = 'b', [KEY_C] = 'c', [KEY_D] = 'd', [KEY_E] = 'e',
		[KEY_F] = 'f', [KEY_G] = 'g', [KEY_H] = 'h', [KEY_I] = 'i', [KEY_J] = 'j',
		[KEY_K] = 'k', [KEY_L] = 'l', [KEY_M] = 'm', [KEY_N] = 'n', [KEY_O] = 'o',
		[KEY_P] = 'p', [KEY_Q] = 'q', [KEY_R] = 'r', [KEY_S] = 's', [KEY_T] = 't',
		[KEY_U] = 'u', [KEY_V] = 'v', [KEY_W] = 'w', [KEY_X] = 'x', [KEY_Y] = 'y',
		[KEY_Z] = 'z',

		[KEY_0] = '0', [KEY_1] = '1', [KEY_2] = '2', [KEY_3] = '3', [KEY_4] = '4',
		[KEY_5] = '5', [KEY_6] = '6', [KEY_7] = '7', [KEY_8] = '8', [KEY_9] = '9',

		[KEY_KP_0] = '0', [KEY_KP_1] = '1', [KEY_KP_2] = '2', [KEY_KP_3] = '3',
		[KEY_KP_4] = '4', [KEY_KP_5] = '5', [KEY_KP_6] = '6', [KEY_KP_7] = '7',
		[KEY_KP_8] = '8', [KEY_KP_9] = '9',
		[KEY_KP_DOT] = '.',
		[KEY_KP_PLUS] = '+',
		[KEY_KP_MINUS] = '-',
		[KEY_KP_ASTERISK] = '*',
		[KEY_KP_SLASH] = '/',
		[KEY_KP_ENTER] = '\n',

		[KEY_ENTER] = '\n',
		[KEY_TAB] = '\t',
		[KEY_BACKSPACE] = '\b',
		[KEY_SPACE] = ' ',

		[KEY_DOT] = '.',
		[KEY_COMMA] = ',',
		[KEY_SLASH] = '/',
		[KEY_STAR] = '*',
		[KEY_MINUS] = '-',
		[KEY_PLUS] = '+',
		[KEY_EQUAL] = '=',
		[KEY_SEMICOLON] = ';',
		[KEY_BACKSLASH] = '\\',
		[KEY_BACKTICK] = '`',
		[KEY_APOSTROPHE] = '\'',

		[KEY_LEFT_BRACE] = '[',
		[KEY_RIGHT_BRACE] = ']',
		[KEY_LEFT_PARENTHESIS] = '(',
		[KEY_RIGHT_PARENTHESIS] = ')',
	};

	if ((size_t)code >= ARRAY_SIZE(key_code_to_ascii) || !key_code_to_ascii[code])
		return false;

	*c = key_code_to_ascii[code];
	return true;
}

/*
 * Insert a new event inside the queue of pending events.
 */
void input_device_push_event(struct input_device *dev, const struct input_event *ev)
{
	spinlock_acquire(&dev->ev_lock);
	input_event_dump(dev, ev);
	if (ringbuffer_available(&dev->ev_buffer) >= sizeof(*ev)) {
		ringbuffer_push(&dev->ev_buffer, (const void *)ev, sizeof(*ev));
		waitqueue_dequeue_all(&dev->ev_waiters);
	} else
		log_warn("%s: event buffer full", device_name(&dev->dev));
	spinlock_release(&dev->ev_lock);

	switch (ev->ev_type) {
	case INPUT_EV_KEY_PRESS: {
		char c;

		if (input_key_code_translate(ev->ev_data.key_code, &c))
			console_input(&c, 1);
		break;
	}

	default:
		break;
	}
}

/*
 * Pop one event from the queue of pending events.
 */
static void input_device_pop_event_locked(struct input_device *dev, struct input_event *ev)
{
	ASSERT(spinlock_is_held(&dev->ev_lock));

	if (ringbuffer_remaining(&dev->ev_buffer) >= sizeof(*ev))
		ringbuffer_pop(&dev->ev_buffer, (void *)ev, sizeof(*ev));
	else
		log_warn("%s: event buffer empty during pop ? (remaining: %zu)",
			 device_name(&dev->dev), ringbuffer_remaining(&dev->ev_buffer));
}

/*
 * Read events from an input device.
 */
static ssize_t input_device_read(struct file *file, char *buffer, size_t size)
{
	struct input_device *dev = to_input_device(file->vnode->pdata);
	ssize_t read = 0;

again:
	spinlock_acquire(&dev->ev_lock);
	if (ringbuffer_is_empty(&dev->ev_buffer)) {
		waitqueue_lock(&dev->ev_waiters);
		spinlock_release(&dev->ev_lock);
		waitqueue_enqueue_locked(&dev->ev_waiters, current);
		goto again;
	}

	/* Pop as many input events as can fit inside the buffer. */
	while ((size_t)read < size && !ringbuffer_is_empty(&dev->ev_buffer)) {
		struct input_event ev;

		/* partial read */
		if (size - (size_t)read < sizeof(ev))
			break;

		input_device_pop_event_locked(dev, &ev);
		memcpy(buffer + read, &ev, sizeof(ev));
		read += sizeof(ev);
	}

	spinlock_release(&dev->ev_lock);

	return read;
}

/*
 * TODO: support O_NONBLOCK
 */
static error_t input_device_open(struct file *file)
{
    if (!(file->flags & O_NONBLOCK))
        return E_INVAL;

    return 0;
}

static const struct file_operations input_dev_fops = {
    .open = input_device_open,
    .read = input_device_read,
};

/*
 * Initialize and register a new input device.
 */
error_t register_input_device(struct input_device *input)
{
	struct device *dev = &input->dev;
	void *ev_buf;
	error_t err;

	ev_buf = kcalloc(1, INPUT_DEVICE_EV_BUFFER_SIZE, KMALLOC_KERNEL);
	if (!ev_buf)
		return E_NOMEM;

	dev->fops = &input_dev_fops;
	device_set_name(dev, "input%d", atomic_inc(&input_dev_count));
	ringbuffer_init(&input->ev_buffer, ev_buf, INPUT_DEVICE_EV_BUFFER_SIZE);
	INIT_SPINLOCK(input->ev_lock);
	INIT_WAITQUEUE(input->ev_waiters);

	err = device_register(dev);
	if (err) {
		kfree(ev_buf);
		return err;
	}

	return E_SUCCESS;
}
