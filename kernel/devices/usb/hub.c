#define LOG_DOMAIN "usb-hub"

#include <kernel/devices/usb.h>
#include <kernel/error.h>
#include <kernel/kmalloc.h>
#include <kernel/semaphore.h>
#include <kernel/timer.h>
#include <kernel/worker.h>

#include <utils/macro.h>

#include <specs/usb.h>
#include <sys/endian.h>

static inline bool is_usb_port_feature(u16 feature)
{
    switch (feature) {
    case USB_HUB_FEAT_PORT_CONNECTION:
    case USB_HUB_FEAT_PORT_ENABLE:
    case USB_HUB_FEAT_PORT_SUSPEND:
    case USB_HUB_FEAT_PORT_OVER_CURRENT:
    case USB_HUB_FEAT_PORT_RESET:
    case USB_HUB_FEAT_PORT_POWER:
    case USB_HUB_FEAT_PORT_LOW_SPEED:
    case USB_HUB_FEAT_C_PORT_CONNECTION:
    case USB_HUB_FEAT_C_PORT_RESET:
    case USB_HUB_FEAT_C_PORT_ENABLE:
    case USB_HUB_FEAT_C_PORT_SUSPEND:
    case USB_HUB_FEAT_C_PORT_OVER_CURRENT:
    case USB_HUB_FEAT_PORT_TEST:
    case USB_HUB_FEAT_PORT_INDICATOR:
        return true;

    default:
        return false;
    }
}

static error_t usb_set_port_feature(struct usb_hub *hub, u16 feature,
                                    u16 port)
{
    if (!is_usb_port_feature(feature))
        return E_INVAL;
    if (port <= 0 || port > hub->max_port)
        return E_INVAL;

    if (hub->set_port_feature)
        return hub->set_port_feature(hub, feature, port);

    return usb_device_request_raw(hub->udev,
                                  USB_SETUP_REQ_TYPE_CLASS |
                                  USB_SETUP_REQ_TYPE_FOR_PORT,
                                  USB_REQUEST_SET_FEATURE,
                                  feature, port, NULL, 0);
}

static error_t usb_clear_port_feature(struct usb_hub *hub, u16 feature,
                                      u16 port)
{
    if (!is_usb_port_feature(feature))
        return E_INVAL;
    if (port <= 0 || port > hub->max_port)
        return E_INVAL;

    if (hub->clear_port_feature)
        return hub->clear_port_feature(hub, feature, port);

    return usb_device_request_raw(hub->udev,
                                  USB_SETUP_REQ_TYPE_CLASS |
                                  USB_SETUP_REQ_TYPE_FOR_PORT,
                                  USB_REQUEST_CLEAR_FEATURE,
                                  feature, port, NULL, 0);
}

static error_t usb_get_port_status(struct usb_hub *hub, u16 port,
                                   u16 *status, u16 *change)

{
    le32_t raw_status;
    error_t err;

    if (hub->get_port_status)
        return hub->get_port_status(hub, port, status, change);

    err = usb_device_request_raw(hub->udev,
                                 USB_SETUP_REQ_TYPE_CLASS |
                                 USB_SETUP_REQ_TYPE_FOR_PORT,
                                 USB_REQUEST_GET_STATUS,
                                 0, port, &raw_status, 4);
    if (err)
        return err;

    if (status)
        *status = le32toh(raw_status) >> 16;
    if (change)
        *change = le32toh(raw_status) & 0xffff;

    return E_SUCCESS;
}

/*
 *
 */
static error_t usb_hub_reset_port(struct usb_hub *hub, unsigned int port)
{
    error_t err;

    err = usb_set_port_feature(hub, USB_HUB_FEAT_PORT_RESET, port);
    if (err) {
        log_warn("%s: failed to reset port %d",
                 usb_device_name(hub->udev), port);
        return E_SUCCESS; /* try to use the device without a proper reset */
    }

    timer_delay_ms(100);

    err = usb_clear_port_feature(hub, USB_HUB_FEAT_PORT_RESET, port);
    if (err) {
        log_err("%s: failed to release reset for port %d",
                usb_device_name(hub->udev), port);
        return err; /* return err since device is stuck reset */
    }

    timer_delay_ms(50);

    return E_SUCCESS;
}

/*
 * @see USB 2.0 - 9.1.2 (Bus Enumeration).
 */
static error_t usb_hub_init_port_device(struct usb_hub *hub,
                                        struct usb_device *udev,
                                        unsigned int port)
{
    static DECLARE_MUTEX(default_address_lock);
    struct usb_device_descriptor desc;
    error_t err;
    int addr;

    /* All devices respond to the default address (0) after reset.
     * We must never have multiple devices in this state at the same time. */
    semaphore_acquire(&default_address_lock);

    /* Issue a port enable and reset. The device responds to transactions
     * made to the default address after this. */
    err = usb_set_port_feature(hub, USB_HUB_FEAT_PORT_ENABLE, port);
    if (err) {
        semaphore_release(&default_address_lock);
        goto fail;
    }

    /* The duration of the Resetting state is 10 ms to 20 ms. */
    timer_delay_ms(20);

    addr = usb_get_address();
    if (addr < 0) {
        log_err("no available USB address left for a new device");
        err = E_NOENT;
        goto fail;
    }

    err = usb_device_request(udev, USB_REQUEST_SET_ADDRESS, addr, 0, NULL, 0);
    timer_delay_ms(50);
    udev->address = addr;
    semaphore_release(&default_address_lock);
    if (err)
        goto fail;

    err = usb_device_get_descriptor(udev, USB_DESCRIPTOR_DEVICE, 0, &desc, 8);
    if (err) {
        log_err("failed to get device's descriptor: %pe", &err);
        goto fail;
    }

    udev->pipes_in[USB_CONTROL_ENDPOINT]->max_packet_size = desc.bMaxPacketSize;
    udev->pipes_out[USB_CONTROL_ENDPOINT]->max_packet_size = desc.bMaxPacketSize;

    return E_SUCCESS;

fail:
    return err;
}

/*
 *
 */
static error_t usb_hub_connect(struct usb_hub *hub, unsigned int port,
                               enum usb_speed speed)
{
    struct usb_device *udev;
    error_t err;

    log_info("%s: new device connected on port %d",
             usb_device_name(hub->udev), port);

    udev = usb_device_alloc();
    if (!udev)
        return E_NOMEM;

    err = usb_device_init(udev);
    if (IS_ERR(udev))
        goto fail;

    udev->speed = speed;
    udev->controller = hub->udev->controller;

    err = usb_hub_reset_port(hub, port);
    if (err)
        goto fail;

    err = usb_hub_init_port_device(hub, udev, port);
    if (err)
        goto fail;

    err = usb_device_probe(udev);
    if (err) {
        log_err("failed to probe USB device: %pe", &err);
        goto fail;
    }

    hub->ports[port] = udev;

    return E_SUCCESS;

fail:
    usb_device_destroy(udev);
    return err;
}


/*
 *
 */
static void usb_hub_disconnect(struct usb_hub *hub, unsigned int port)
{
    struct usb_device *udev;

    log_info("%s: device disconnected from port %d",
             usb_device_name(hub->udev), port);

    udev = hub->ports[port];
    if (!udev)
        return;

    usb_device_destroy(udev);
    hub->ports[port] = NULL;
}

/*
 * Detect USB PnP events (device connect/disconnect) and update the connected
 * device's state accordingly.
 */
static void usb_hub_poll_work(void *data)
{
    struct usb_hub *hub = data;

    INFINITE_LOOP() {
        /* NOTE: A valid port number for a hub is greater than zero. */
        for (unsigned int port = 1; port <= hub->max_port; ++port) {
            u16 status;
            u16 change;
            enum usb_speed speed;

            usb_get_port_status(hub, port, &status, &change);

            speed = USB_SPEED_FULL;
            if (status & USB_HUB_STATUS_PORT_LOW_SPEED)
                speed = USB_SPEED_LOW;

            if (change & USB_HUB_STATUS_C_PORT_CONNECTION) {
                if (status & USB_HUB_STATUS_PORT_CONNECTION)
                    usb_hub_connect(hub, port, speed);
                else
                    usb_hub_disconnect(hub, port);
            }

            usb_clear_port_feature(hub, USB_HUB_FEAT_C_PORT_ENABLE, port);
            usb_clear_port_feature(hub, USB_HUB_FEAT_C_PORT_CONNECTION, port);
        }

        timer_wait_ms(1000);
    }
}

/*
 * Called by usb_device_destroy();
 */
static void usb_hub_destroy(struct usb_device *udev)
{
    struct usb_hub *hub = udev->priv;

    if (!hub)
        return;

    worker_release(&hub->worker);

    if (hub->ports) {
        for (unsigned int i = 0; i <= hub->max_port; ++i)
            usb_device_destroy(hub->ports[i]);
        kfree(hub->ports);
    }

    kfree(hub);
}

/*
 *
 */
error_t usb_hub_init(struct usb_hub *hub,
                     struct usb_device *udev,
                     unsigned int max_port)
{
    struct usb_device **ports;

    /* done first so we can call usb_device_destroy() inside the error path */
    hub->udev = udev;
    udev->priv = hub;
    udev->destroy = usb_hub_destroy;

    if (max_port <= 0)
        return E_INVAL;

    ports = kcalloc(max_port + 1, sizeof(*ports), KMALLOC_KERNEL);
    if (!ports)
        return E_NOMEM;

    hub->ports = ports;
    hub->max_port = max_port;

    worker_init(&hub->worker);
    worker_start(&hub->worker, usb_hub_poll_work, hub);

    return E_SUCCESS;
}

/*
 *
 */
static error_t usb_hub_probe(struct device *device)
{
    struct usb_device *udev = to_usb_device(device);
    struct usb_hub_descriptor desc;
    struct usb_hub *hub;
    error_t err;

    hub = kcalloc(1, sizeof(*hub), KMALLOC_KERNEL);
    if (!hub)
        return E_NOMEM;

    err = usb_device_request_raw(udev,
                                 USB_SETUP_REQ_TYPE_CLASS,
                                 USB_REQUEST_GET_DESCRIPTOR,
                                 USB_DESCRIPTOR_HUB, 0, &desc,
                                 sizeof(desc));
    if (err) {
        log_err("failed to get hub descriptor");
        kfree(hub); /* hub isn't free'd by usb_device_destroy() at this stage */
        return err;
    }

    err = usb_hub_init(hub, udev, desc.bNbrPorts);
    if (err)
        return err;

    return E_SUCCESS;
}

static const struct usb_compatible usb_hub_compatible[] = {
    USB_COMPATIBLE_CLASS(0x9),
    {},
};

static struct usb_driver usb_hub_driver = {
    .compatible = usb_hub_compatible,
    .driver = {
        .name = "usb_hub",
        .operations = {
            .probe = usb_hub_probe,
        },
    },
};

DECLARE_USB_DRIVER(hub, &usb_hub_driver);
