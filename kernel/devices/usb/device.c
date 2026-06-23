#define LOG_DOMAIN "usb"

#include <kernel/devices/usb.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>

#include <libalgo/bitmap.h>
#include <string.h>

static BITMAP(usb_addresses, USB_MAX_ADDRESS + 1);
static DECLARE_SPINLOCK(usb_addresses_lock);

static const char *usb_speed_names[] = {
    [USB_SPEED_LOW] = "low-speed",
    [USB_SPEED_FULL] = "full-speed",
};

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
    WAIT_FOR(urb.completed);

    return urb.result;
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
static error_t __usb_device_get_descriptor(struct usb_device *udev,
                                           u8 recipient,
                                           u8 type, u8 index, u16 lang_id,
                                           void *data, u16 data_size)
{
    /* Index is only used for configuration and string descriptors. For other
     * standard descriptors a descriptor index of zero must be used.
     */
    if (type != USB_DESCRIPTOR_CONFIGURATION && type != USB_DESCRIPTOR_STRING)
        index = 0;
    if (type != USB_DESCRIPTOR_STRING)
        lang_id = 0;

    return usb_device_request_raw(udev, recipient, USB_REQUEST_GET_DESCRIPTOR,
                                  ((u16)type << 8) | index, lang_id, data,
                                  data_size);
}

/*
 *
 */
error_t usb_device_get_descriptor(struct usb_device *udev,
                                  u8 type, u8 index,
                                  void *data, u16 data_size)
{
    return __usb_device_get_descriptor(udev,
                                       USB_SETUP_REQ_TYPE_FOR_DEVICE,
                                       type, index, 0, data, data_size);
}

/*
 *
 */
error_t usb_interface_get_descriptor(struct usb_device *udev,
                                     u8 type, u8 index,
                                     void *data, u16 data_size)
{
    return __usb_device_get_descriptor(udev,
                                       USB_SETUP_REQ_TYPE_FOR_IFACE,
                                       type, index, 0, data, data_size);
}

/*
 *
 */
error_t usb_device_get_string_descriptor(struct usb_device *udev,
                                         u8 index, u16 lang_id,
                                         void *data, u16 data_size)
{
    return __usb_device_get_descriptor(udev,
                                       USB_SETUP_REQ_TYPE_FOR_DEVICE,
                                       USB_DESCRIPTOR_STRING, index,
                                       lang_id, data, data_size);
}

/*
 *
 */
error_t usb_interface_get_string_descriptor(struct usb_device *udev,
                                            u8 index, u16 lang_id,
                                            void *data, u16 data_size)
{
    return __usb_device_get_descriptor(udev,
                                       USB_SETUP_REQ_TYPE_FOR_IFACE,
                                       USB_DESCRIPTOR_STRING, index,
                                       lang_id, data, data_size);
}

/*
 *
 */
const char* usb_pid_field_name(uint8_t pid)
{
    switch (pid) {
        /* Token PIDs */
        case 0xE1: return "OUT";
        case 0x69: return "IN";
        case 0xA5: return "SOF";
        case 0x2D: return "SETUP";

        /* Data PIDs */
        case 0xC3: return "DATA0";
        case 0x4B: return "DATA1";
        case 0x87: return "DATA2";
        case 0x0F: return "MDATA";

        /* Handshake PIDs */
        case 0xD2: return "ACK";
        case 0x5A: return "NAK";
        case 0x1E: return "STALL";
        case 0x96: return "NYET";

        /* Special PIDs */
        case 0x3C: return "PRE_OR_ERR";
        case 0x78: return "SPLIT";
        case 0xB4: return "PING";

        default: return "UNKNOWN";
    }
}

const char* usb_pid_name(enum usb_pid pid)
{
    return usb_pid_field_name(usb_pid_field(pid));
}

/*
 * Free a USB device.
 */
void usb_device_destroy(struct usb_device *udev)
{
    if (!udev)
        return;

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

#include <kernel/devices/driver.h>

static DECLARE_LLIST(usb_drivers);
static DECLARE_SPINLOCK(usb_drivers_lock);

void usb_driver_register(struct usb_driver *driver)
{
    spinlock_acquire(&usb_drivers_lock);
    llist_add(&usb_drivers, &driver->driver.this);
    spinlock_release(&usb_drivers_lock);
}

static int __usb_driver_match(const void *node, const void *data)
{
    const struct usb_compatible *match = data;
    struct usb_driver *driver = container_of(node, struct usb_driver,
                                             driver.this);

    for (int i = 0; driver->compatible[i].match; ++i) {
        const struct usb_compatible *compatible = &driver->compatible[i];

        if ((compatible->match & USB_COMPATIBLE_MATCH_VENDOR) &&
            compatible->vendor != match->vendor)
            continue;

        if ((compatible->match & USB_COMPATIBLE_MATCH_PRODUCT) &&
            compatible->product != match->product)
            continue;

        if ((compatible->match & USB_COMPATIBLE_MATCH_CLASS) &&
            compatible->class != match->class)
            continue;

        if ((compatible->match & USB_COMPATIBLE_MATCH_SUBCLASS) &&
            compatible->subclass != match->subclass)
            continue;

        if ((compatible->match & USB_COMPATIBLE_MATCH_PROTO) &&
            compatible->protocol != match->protocol)
            continue;

        return COMPARE_EQ;
    }

    return !COMPARE_EQ;
}

/*
 *
 */
struct usb_driver *usb_device_find_driver(u16 id_vendor, u16 id_product,
                                          u8 class, u8 subclass, u8 protocol)
{
    struct usb_compatible compatible;
    node_t *node = NULL;

    spinlock_acquire(&usb_drivers_lock);
    compatible.class = class;
    compatible.subclass = subclass;
    compatible.protocol = protocol;
    compatible.vendor = id_vendor;
    compatible.product = id_product;
    node = llist_find_first(&usb_drivers, &compatible, __usb_driver_match);
    spinlock_release(&usb_drivers_lock);
    if (!node)
        return NULL;

    return container_of(node, struct usb_driver, driver.this);;
}

error_t usb_driver_probe(struct usb_driver *driver, struct usb_device *udev)
{
    device_set_name(&udev->dev, "%s@%u", driver->driver.name, udev->address);
    return driver_probe(&driver->driver, &udev->dev);
}

/*
 *
 */
error_t usb_device_probe(struct usb_device *udev)
{
    struct usb_driver *driver;
    struct usb_device_descriptor desc;
    struct usb_configuration_descriptor *cfg;
    struct usb_interface_descriptor *iface;
    u8 cfg_full[256]; // cfg.bLength is a single byte
    error_t err;


    err = usb_device_get_descriptor(udev, USB_DESCRIPTOR_DEVICE, 0,
                                    &desc, sizeof(desc));
    if (err) {
        log_err("failed to get device's descriptor: %pe", &err);
        return err;
    }

    log_info("%s device connected (idVendor=%04x, idProduct=%04x, address=%d)",
             usb_speed_names[udev->speed], desc.idVendor, desc.idProduct,
             udev->address);

    err = usb_device_get_descriptor(udev, USB_DESCRIPTOR_CONFIGURATION, 0,
                                    &cfg_full, sizeof(cfg_full));
    if (err) {
        log_err("failed to get configuration descriptor: %pe", &err);
        return err;
    }

    /* always select the first configuration */
    cfg = (void *)cfg_full;
    err = usb_device_request(udev, USB_REQUEST_SET_CONFIGURATION,
                             cfg->bConfigurationValue, 0, NULL, 0);
    if (err) {
        log_err("failed to select configuration descriptor: %pe", &err);
        return err;
    }

    driver = usb_device_find_driver(desc.idVendor, desc.idProduct,
                                    desc.bDeviceClass,
                                    desc.bDeviceSubClass,
                                    desc.bDeviceProtocol);
    if (driver)
        return usb_driver_probe(driver, udev);


    /* interface and endpoint descriptors are appended to the configuration */
    iface = (void *)cfg_full + sizeof(struct usb_configuration_descriptor);
    for (int i = 0; i < cfg->bNumInterfaces; ++i) {
        /* TODO: support compound devices */
        driver = usb_device_find_driver(0, 0, iface->bInterfaceClass,
                                        iface->bInterfaceSubClass,
                                        iface->bInterfaceProtocol);
        if (driver)
            return usb_driver_probe(driver, udev);

        iface = (void *)iface + iface->bLength;
    }

    return E_NOENT;
}
