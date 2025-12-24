#ifndef KERNEL_DEVICES_USB_H
#define KERNEL_DEVICES_USB_H

#include <kernel/device.h>
#include <kernel/spinlock.h>

enum usb_speed {
    USB_LOWSPEED,
    USB_FULLSPEED,
};

/** USB host controller. */
struct usb_controller {
    struct pci_device *pcidev;
    struct usb_controller_ops *ops;
    enum usb_speed speed;
    spinlock_t lock;
};

/***/
struct usb_controller_ops {
    error_t (*control_transfer)(struct usb_controller *);
};

/** USB device.
 *
 * NOTE: The term 'device' is used to refer to what is called a 'function'
 *       inside the USB specification.
 */
struct usb_device {
    struct device dev;

    /* USB topology */
    struct usb_controller *controller;
    int address;  /* address of the device on the bus. */
    int endpoint;
};

static inline struct usb_device *to_usb_dev(struct device *dev)
{
    return container_of(dev, struct usb_device, dev);
}

/*
 * This endpoint is present on all USB devices and is used to send/receive
 * control messages.
 */
#define USB_CONTROL_ENDPOINT 0

/** Register a new USB device. */
error_t usb_device_register(struct usb_device *);

/*
 *
 * USB data structures.
 */

#endif /* KERNEL_DEVICES_USB_H */
