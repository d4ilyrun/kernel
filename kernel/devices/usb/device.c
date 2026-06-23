#define LOG_DOMAIN "usb"

#include <kernel/devices/usb.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>

#include <libalgo/bitmap.h>
#include <string.h>

static BITMAP(usb_addresses, USB_MAX_ADDRESS + 1);
static DECLARE_SPINLOCK(usb_addresses_lock);

/*
 *
 */
int usb_get_address(void)
{
    int address;

    spinlock_acquire(&usb_addresses_lock);
    address = bitmap_first_clear(usb_addresses);
    if (address > 0)
        bitmap_set(usb_addresses, address);
    spinlock_release(&usb_addresses_lock);

    address += 1;
    if (address > USB_MAX_ADDRESS)
        return -1;

    return address;
}

/*
 *
 */
void usb_release_address(int address)
{
    if (address == USB_DEFAULT_ADDRESS || address > USB_MAX_ADDRESS)
        return;

    spinlock_acquire(&usb_addresses_lock);
    bitmap_clear(usb_addresses, address);
    spinlock_release(&usb_addresses_lock);
}

/*
 * Send a USB device request.
 */
error_t usb_device_request_raw(struct usb_device *udev, u8 bRequestType,
                               u8 bRequest, u16 wValue, u16 wIndex,
                               void *data, u16 data_size)
{
    struct usb_pipe *pipe;
    struct usb_setup setup;
    struct urb urb;
    error_t err;

    switch (bRequest) {
    case USB_REQUEST_GET_CONFIGURATION:
    case USB_REQUEST_GET_DESCRIPTOR:
    case USB_REQUEST_GET_INTERFACE:
    case USB_REQUEST_GET_STATUS:
    case USB_REQUEST_SYNCH_FRAME:
        bRequestType |= USB_SETUP_REQ_TYPE_TO_HOST;
        pipe = udev->pipes_in[USB_CONTROL_ENDPOINT];
        break;

    default:
        pipe = udev->pipes_out[USB_CONTROL_ENDPOINT];
        break;
    }

    setup.bRequestType = bRequestType;
    setup.bRequest = bRequest;
    setup.wValue = htole16(wValue);
    setup.wIndex = htole16(wIndex);
    setup.wLength = htole16(data_size);

    memset(&urb, 0, sizeof(urb));
    urb.pipe = pipe;
    urb.data = data;
    urb.data_size = data_size;
    urb.setup = &setup;

    err = usb_send_urb(udev, &urb);
    if (err) {
        log_warn("fail to send USB request: %pe", &err);
        return err;
    }

    /* set by urb_complete() */
    /* TODO: Linux's completion mechanism. */
    WAIT_FOR(urb.completed);

    return E_SUCCESS;
}

error_t usb_device_request(struct usb_device *udev,
                           u8 bRequest, u16 wValue, u16 wIndex,
                           void *data, u16 data_size)
{
    return usb_device_request_raw(udev, USB_SETUP_REQ_TYPE_FOR_DEVICE,
                                  bRequest, wValue, wIndex, data, data_size);
}

error_t usb_endpoint_request(struct usb_device *udev,
                             u8 bRequest, u16 wValue, u16 wIndex,
                             void *data, u16 data_size)
{
    return usb_device_request_raw(udev, USB_SETUP_REQ_TYPE_FOR_EP,
                                  bRequest, wValue, wIndex, data, data_size);
}

error_t usb_interface_request(struct usb_device *udev,
                              u8 bRequest, u16 wValue, u16 wIndex,
                              void *data, u16 data_size)
{
    return usb_device_request_raw(udev, USB_SETUP_REQ_TYPE_FOR_IFACE,
                                  bRequest, wValue, wIndex, data, data_size);
}

/*
 *
 */
static error_t __usb_device_get_descriptor(struct usb_device *udev, u8 type,
                                           u8 index, u16 lang_id, void *data,
                                           u16 data_size)
{
    /* Index is only used for configuration and string descriptors. For other
     * standard descriptors a descriptor index of zero must be used.
     */
    if (type != USB_DESCRIPTOR_CONFIGURATION && type != USB_DESCRIPTOR_STRING)
        index = 0;
    if (type != USB_DESCRIPTOR_STRING)
        lang_id = 0;

    return usb_device_request(udev, USB_REQUEST_GET_DESCRIPTOR,
                              ((u16)type << 8) | index, lang_id,
                              data, data_size);
}

/*
 *
 */
error_t usb_device_get_descriptor(struct usb_device *udev,
                                  u8 type, u8 index,
                                  void *data, u16 data_size)
{
    return __usb_device_get_descriptor(udev, type, index, 0, data, data_size);
}

/*
 *
 */
error_t usb_device_get_string_descriptor(struct usb_device *udev,
                                         u8 index, u16 lang_id,
                                         void *data, u16 data_size)
{
    return __usb_device_get_descriptor(udev, USB_DESCRIPTOR_STRING, index,
                                       lang_id, data, data_size);
}

/*
 * Free a USB device.
 */
void usb_device_destroy(struct usb_device *udev)
{
    if (udev->destroy)
        udev->destroy(udev);

    usb_release_address(udev->address);

    for (int i = 0; i < USB_ENDPOINT_MAX; ++i) {
        kfree(udev->pipes_in[i]);
        kfree(udev->pipes_out[i]);
    }

    kfree(udev);
}

/*
 *
 */
struct usb_device *usb_device_alloc(void)
{
    return kcalloc(1, sizeof(struct usb_device), KMALLOC_KERNEL);
}

/*
 * Initialize a new USB device.
 *
 * This function initializes the structure just enough that the caller
 * can query the returned device's descriptors.
 */
error_t usb_device_init(struct usb_device *udev)
{
    struct usb_pipe *pipe_in;
    struct usb_pipe *pipe_out;

    udev->address = USB_DEFAULT_ADDRESS;

    pipe_in = kcalloc(1, sizeof(*pipe_in), KMALLOC_KERNEL);
    if (!pipe_in)
        return E_NOMEM;
    udev->pipes_in[USB_CONTROL_ENDPOINT] = pipe_in;

    pipe_out = kcalloc(1, sizeof(*pipe_out), KMALLOC_KERNEL);
    if (!pipe_out)
        return E_NOMEM;
    udev->pipes_out[USB_CONTROL_ENDPOINT] = pipe_out;

    /* default value until we read the device descriptor. */
    pipe_out->max_packet_size = 8;
    pipe_out->endpoint = USB_ENDPOINT_OUT | USB_CONTROL_ENDPOINT;
    pipe_out->type = USB_XFER_CONTROL;

    pipe_in->max_packet_size = 8;
    pipe_in->endpoint = USB_ENDPOINT_IN | USB_CONTROL_ENDPOINT;
    pipe_in->type = USB_XFER_CONTROL;

    return E_SUCCESS;
}
