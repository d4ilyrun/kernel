#ifndef KERNEL_DEVICES_USB_H
#define KERNEL_DEVICES_USB_H

#include <kernel/device.h>
#include <kernel/spinlock.h>

struct urb;

enum usb_speed {
    USB_SPEED_LOW,
    USB_SPEED_FULL,
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
    error_t (*urb_submit)(struct usb_controller *, struct urb *);
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
    int address; /* address of the device on the bus. */
    int endpoint;
};

static inline struct usb_device *to_usb_dev(struct device *dev)
{
    return container_of(dev, struct usb_device, dev);
}

/** Maximum number of devices possible on a single bus. */
#define USB_BUS_MAX_DEVICE 127

/**
 * This endpoint is present on all USB devices. It is used to send/receive
 * control messages during the enumeration phase.
 */
#define USB_CONTROL_ENDPOINT 0

/** Register a new USB device. */
error_t usb_device_register(struct usb_device *);

/**
 * @ref USB 2.0 Specification (5.4 - 5.8)
 */
enum usb_xfer_type {
    USB_XFER_ISOCHRONOUS,
    USB_XFER_INTERRUPT,
    USB_XFER_CONTROL,
    USB_XFER_BULK,
};

/** USB Packet identifiers
 *  @ref USB 2.0 Specification (8.3.1).
 */
enum usb_pid {
    // Token
    USB_PID_OUT = 0b0001,
    USB_PID_IN = 0b1001,
    USB_PID_SOF = 0b0101,
    USB_PID_SETUP = 0b1101,
    // Data
    USB_PID_DATA0 = 0b0011,
    USB_PID_DATA1 = 0b1011,
    USB_PID_DATA2 = 0b0111,
    USB_PID_MDATA = 0b1111,
    // Handshake
    USB_PID_ACK = 0b0010,
    USB_PID_NAK = 0b1010,
    USB_PID_STALL = 0b1110,
    USB_PID_NYET = 0b0110,
    // Special
    USB_PID_PRE = 0b1100,
    USB_PID_ERR = 0b1100,
    USB_PID_SPLIT = 0b1000,
    USB_PID_PING = 0b0100,
    USB_PID_RESERVED = 0b0000,
};

/* Compute the whole 8b PID field value as described per USB 2.0 - 8.3.1. */
static inline u8 usb_pid_field(enum usb_pid pid)
{
    return (~pid << 4) | (pid & 0xf);
}

/**
 *
 */
enum usb_pipe_direction {
    USB_PIPE_DIR_OUTPUT,
    USB_PIPE_DIR_INPUT,
};

/* */
struct usb_pipe {
    u8 ep_number; /* Endpoint number. */
    int ep_max_transaction_size;
};

/** USB Request Block. */
struct urb {

    /*
     * USB device related fields.
     */

    void *data;
    size_t data_size;

    struct usb_pipe *pipe;

    /*
     * Host-controller related fields.
     */

    void *urb_priv; /* Private data used by the underlying HC driver. */
};

/** Send a USB request block through a USB pipe.
 *
 *  Sending a USB packet is an asynchronous operation. This function only
 *  configures and inserts the necessary queue heads and data structures
 *  so that the packet can be transferred by the host controller during the next
 *  USB frame.
 */
error_t usb_send_urb(struct usb_pipe *, struct urb *);

#endif /* KERNEL_DEVICES_USB_H */
