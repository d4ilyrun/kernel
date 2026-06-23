#ifndef _KERNEL_DEVICES_USB_H
#define _KERNEL_DEVICES_USB_H

#include <kernel/device.h>
#include <kernel/devices/driver.h>
#include <kernel/interrupts.h>
#include <kernel/spinlock.h>
#include <kernel/worker.h>

#include <specs/usb.h>

struct urb;
struct usb_device;

enum usb_speed {
    USB_SPEED_LOW,
    USB_SPEED_FULL,
};

/** USB host controller. */
struct usb_controller {
    struct pci_device *pcidev;
    struct usb_controller_ops *ops;
    void *priv; /* used by the HCD driver */
    struct usb_hub *root_hub;
    enum usb_speed speed;
    unsigned int bus; /* software bus number */
};

/***/
struct usb_controller_ops {
    error_t (*init)(struct usb_controller *);
    void (*destroy)(struct usb_controller *);
    error_t (*urb_submit)(struct usb_device *, struct urb *);
    interrupt_return_t (*interrupt_handler)(struct usb_controller *);
};

extern struct usb_controller_ops uhci_controller_ops;

/**
 * @ref USB 2.0 Specification (5.4 - 5.8)
 */
enum usb_xfer_type {
    USB_XFER_ISOCHRONOUS,
    USB_XFER_INTERRUPT,
    USB_XFER_CONTROL,
    USB_XFER_BULK,
};

/*
 * @see USB 2.0 - 9.6.6
 */
struct usb_pipe {
    u8 endpoint; /* Endpoint address copied from the Endpoint descriptor. */
    enum usb_xfer_type type;
    int max_packet_size;
};

static inline bool usb_pipe_is_input(const struct usb_pipe *pipe)
{
    return pipe->endpoint & USB_ENDPOINT_IN;
}

static inline bool usb_pipe_is_output(const struct usb_pipe *pipe)
{
    return !usb_pipe_is_input(pipe);
}

/** USB device.
 *
 * NOTE: The term 'device' is used to refer to what is called a 'function'
 *       inside the USB specification.
 */
struct usb_device {
    struct device dev;
    char name[NAME_MAX];

    enum usb_speed speed;

    /* USB topology */
    struct usb_controller  *controller;
    int                     address; /* address of the device on the bus. */
    struct usb_pipe        *pipes_out[USB_ENDPOINT_MAX];
    struct usb_pipe        *pipes_in[USB_ENDPOINT_MAX];

    /* Fields used by the USB driver associated with the device (hub, HID, ...). */
    void    *priv;
    void    (*destroy)(struct usb_device *device);
};

static inline struct usb_device *to_usb_device(struct device *dev)
{
    return container_of(dev, struct usb_device, dev);
}

static inline const char *usb_device_name(const struct usb_device *udev)
{
    return device_name(&udev->dev);
}

/**
 * Allocate a new USB device.
 */
struct usb_device *usb_device_alloc(void);

/** Initialize a new USB device.
 *
 * This function initializes the structure just enough that the caller
 * can query the returned device's descriptors.
 */
error_t usb_device_init(struct usb_device *udev);

/*
 * Free a USB device.
 */
void usb_device_destroy(struct usb_device *udev);

/*
 *
 */
int usb_get_address(void);

/*
 *
 */
void usb_release_address(int address);

/*
 *
 */
error_t usb_device_probe(struct usb_device *udev);

/*
 * Send a USB device request.
 *
 * @see USB 2.0 - 9.4
 */
error_t usb_device_request_raw(struct usb_device *udev, u8 bRequestType,
                               u8 bRequest, u16 wValue, u16 wIndex, void *data,
                               u16 data_size);

error_t usb_device_request(struct usb_device *udev, u8 bRequest, u16 wValue,
                           u16 wIndex, void *data, u16 data_size);

error_t usb_endpoint_request(struct usb_device *udev, u8 bRequest, u16 wValue,
                             u16 wIndex, void *data, u16 data_size);

error_t usb_interface_request(struct usb_device *udev, u8 bRequest, u16 wValue,
                              u16 wIndex, void *data, u16 data_size);

error_t usb_device_get_descriptor(struct usb_device *udev, u8 type, u8 index,
                                  void *data, u16 data_size);

error_t usb_device_get_string_descriptor(struct usb_device *udev, u8 index,
                                         u16 lang_id, void *data,
                                         u16 data_size);

error_t usb_interface_get_descriptor(struct usb_device *udev, u8 type, u8 index,
                                     void *data, u16 data_size);

error_t usb_interface_get_string_descriptor(struct usb_device *udev, u8 index,
                                            u16 lang_id, void *data,
                                            u16 data_size);

/*
 * Convert the content of a USB packet's PID value into a string.
 */
const char *usb_pid_field_name(uint8_t pid);
const char *usb_pid_name(enum usb_pid pid);

/** USB Request Block. */
struct urb {
    void *data;
    size_t data_size;
    void *setup; /* setup packet's content for control pipes */
    struct usb_pipe *pipe;

    union {
        bool completed; /* For blocking requests */
        struct {        /* For interrupt transfers */
            void (*interrupt)(struct urb *, void *);
            void *interrupt_data;
        };
    };

    error_t result;
};

/** Send a USB request block through a USB pipe.
 *
 *  Sending a USB packet is an asynchronous operation. This function only
 *  configures and inserts the necessary queue heads and data structures
 *  so that the packet can be transferred by the host controller during the next
 *  USB frame.
 */
error_t usb_send_urb(struct usb_device *, struct urb *);

/* Mark a USB request as completed with an error.
 *
 * Same as @ref urb_complete(), but should be used when the controller
 * reports an error related to the request (CRC, timeout, ...).
 */
void urb_cancel(struct urb *, error_t);

/** Mark a USB request as completed.
 *
 * This function is responsible for unblocking the sender for blocking requests
 * and generating the software interrput for interrupt requests. ALL HCD drivers
 * must call this wen a request has been scheduled by the hardware and
 * acknowledged by the device.
 */
static inline void urb_complete(struct urb *urb)
{
    urb_cancel(urb, E_SUCCESS);
}

/*
 * USB hub device.
 */
struct usb_hub {
    struct usb_device *udev;
    struct worker worker;

    unsigned int max_port;
    struct usb_device **ports;

    /* Root hubs implement these requests in software. */
    error_t (*get_port_status)(struct usb_hub *, u8 port, u16 *status,
                               u16 *change);
    error_t (*clear_port_feature)(struct usb_hub *, u16 feature, u8 port);
    error_t (*set_port_feature)(struct usb_hub *, u16 feature, u8 port);
};

/* Initialize a USB hub device.
 *
 * A hub device must be freed by calling usb_device_destroy()
 * on its udev (i.e. the ones passed to this function). This
 * can be done even when this function returns an error.
 */
error_t usb_hub_init(struct usb_hub *hub, struct usb_device *udev,
                     unsigned int max_ports);

/*
 * Identify and register all devices connected to a USB hub.
 */
void usb_hub_enumerate_devices(struct usb_hub *hub);

struct usb_compatible {
    unsigned int match;
    u16 vendor;
    u16 product;
    u8 class;
    u8 subclass;
    u8 protocol;
};

#define USB_COMPATIBLE_MATCH_VENDOR   BIT(0)
#define USB_COMPATIBLE_MATCH_PRODUCT  BIT(1)
#define USB_COMPATIBLE_MATCH_CLASS    BIT(2)
#define USB_COMPATIBLE_MATCH_SUBCLASS BIT(3)
#define USB_COMPATIBLE_MATCH_PROTO    BIT(4)

#define USB_COMPATIBLE_DEVICE(vend, prod)                                    \
    {                                                                        \
        .vendor = (vend),                                                    \
        .product = (prod),                                                   \
        .match = USB_COMPATIBLE_MATCH_VENDOR | USB_COMPATIBLE_MATCH_PRODUCT, \
    }

#define USB_COMPATIBLE_CLASS(cls)            \
    {                                        \
        .class = (cls),                      \
        .match = USB_COMPATIBLE_MATCH_CLASS, \
    }

#define USB_COMPATIBLE_CLASS_SUBCLASS(cls, sub)                              \
    {                                                                        \
        .class = (cls),                                                      \
        .subclass = (sub),                                                   \
        .match = USB_COMPATIBLE_MATCH_CLASS | USB_COMPATIBLE_MATCH_SUBCLASS, \
    }

#define USB_COMPATIBLE_INFO(cls, sub, proto)                                  \
    {                                                                         \
        .class = (cls),                                                       \
        .subclass = (sub),                                                    \
        .protocol = (proto),                                                  \
        .match = USB_COMPATIBLE_MATCH_CLASS | USB_COMPATIBLE_MATCH_SUBCLASS | \
                 USB_COMPATIBLE_MATCH_PROTO,                                  \
    }

struct usb_driver {
    struct device_driver driver;
    const struct usb_compatible *compatible; /* NULL terminated array */
};

void usb_driver_register(struct usb_driver *);

#define DECLARE_USB_DRIVER(_name, _driver) \
    DECLARE_DRIVER(_name, _driver, usb_driver_register);

#endif /* _KERNEL_DEVICES_USB_H */
