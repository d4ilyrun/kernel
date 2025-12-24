/*
 * USB host controller driver API.
 */

#include "kernel/error.h"
#define LOG_DOMAIN "hcd"

#include <kernel/devices/pci.h>
#include <kernel/devices/usb.h>
#include <kernel/devices/uhci.h>
#include <kernel/logger.h>

/*
 * PCI class interface constants for each type of USB controller.
 */
#define USB_UHCI_PCI_INTERFACE 0x00
#define USB_OHCI_PCI_INTERFACE 0x10
#define USB_EHCI_PCI_INTERFACE 0x20
#define USB_XHCI_PCI_INTERFACE 0x30

/*
 *
 */
error_t usb_send_urb(struct usb_device *device, struct urb *urb)
{
    return device->controller->ops->urb_submit(device, urb);
}

/*
 *
 */
error_t usb_device_register(struct usb_device *usbdev)
{
    return device_register(&usbdev->dev);
}

/*
 *
 */
error_t hcd_interrupt(void *data)
{
    struct usb_controller *hcd = data;

    if (hcd->ops->interrupt_handler)
        return hcd->ops->interrupt_handler(hcd);

    /* TODO: Shared interrupts. */
    assert_not_reached();
    return E_SUCCESS;
}

/*
 *
 */
static void hcd_destroy(struct usb_controller *hcd)
{}

/*
 *
 */
static struct usb_controller *hcd_init(struct pci_device *pcidev)
{
    struct usb_controller *hcd;
    error_t err;

    switch (pcidev->class.interface) {
    case USB_UHCI_PCI_INTERFACE:
        hcd = uhci_controller_new(pcidev);
        break;
    default:
        log_err("unsupported PCI class %02x", pcidev->class.interface);
        return PTR_ERR(E_NOT_SUPPORTED);
    }

    if (IS_ERR(hcd))
        return hcd;

    INIT_SPINLOCK(hcd->lock);
    hcd->pcidev = pcidev;

    err = pci_device_register_interrupt_handler(pcidev, hcd_interrupt, hcd);
    if (err) {
        log_err("failed to register interrupt");
        goto exit_error;
    }

    return hcd;

exit_error:
    hcd_destroy(hcd);
    return PTR_ERR(err);
}

/*
 *
 */
static error_t usb_probe(device_t *device)
{
    struct pci_device *pcidev = to_pci_dev(device);
    struct usb_controller *ctlr;

    ctlr = hcd_init(pcidev);
    if (IS_ERR(ctlr))
        return ERR_FROM_PTR(ctlr);

    return E_SUCCESS;
}

static const struct pci_compatible usb_compatible[] = {
    {.class = PCI_CLASS(0x0C, 0x03, USB_UHCI_PCI_INTERFACE)},
    {/* sentinel */},
};

struct pci_driver usb_driver = {
    .compatible = usb_compatible,
    .driver =
        {
            .name = "usb",
            .operations.probe = usb_probe,
        },
};

DECLARE_PCI_DRIVER(usb, &usb_driver);
