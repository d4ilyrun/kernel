#ifndef _KERNEL_DEVICES_INPUT_H
#define _KERNEL_DEVICES_INPUT_H

#include <kernel/device.h>
#include <kernel/spinlock.h>
#include <kernel/waitqueue.h>

#include <dailyrun/input.h>

#include <libalgo/ringbuffer.h>

#define INPUT_DEVICE_EV_BUFFER_SIZE (256 * sizeof(struct input_event))

/* Input devices are keyboards, mouses and any other pointer devices.
 *
 * Such devices register input events that should be polled for and
 * read regularly from userland (typically by the display server).
 */
struct input_device {
    struct device     dev;
    struct ringbuffer ev_buffer;
    spinlock_t        ev_lock;
    struct waitqueue  ev_waiters;
};

static inline struct input_device *to_input_device(struct device *device)
{
    return container_of(device, struct input_device, dev);
}

error_t register_input_device(struct input_device *);
void input_device_push_event(struct input_device *, const struct input_event *);

#endif /* _KERNEL_DEVICES_INPUT_H */
