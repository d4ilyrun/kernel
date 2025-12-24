/*
 * USB host controller driver API.
 */

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
error_t usb_send_urb(struct usb_device *usbdev, struct urb *urb)
{
    UNUSED(usbdev);

    switch (urb->xfer_type) {
    case USB_XFER_INTERRUPT:
    case USB_XFER_CONTROL:
    case USB_XFER_BULK:
        return E_NOT_IMPLEMENTED;

    /* Isochronous transfers require additional setup and processing. */
    case USB_XFER_ISOCHRONOUS:
        not_implemented("isochronous USB transfers");
        return E_NOT_IMPLEMENTED;
    }

    return E_NOT_IMPLEMENTED;
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
static struct usb_controller *usb_init_controller(struct pci_device *pcidev)
{
    struct usb_controller *ctlr;

    switch (pcidev->class.interface) {
    case USB_UHCI_PCI_INTERFACE:
        ctlr = uhci_init_controller(pcidev);
        break;
    default:
        log_err("unsupported PCI class %02x", pcidev->class.interface);
        return PTR_ERR(E_NOT_SUPPORTED);
    }

    if (IS_ERR(ctlr))
        return ctlr;

    INIT_SPINLOCK(ctlr->lock);
    ctlr->pcidev = pcidev;

    return ctlr;
}

/*
 *
 */
static error_t usb_probe(device_t *device)
{
    struct pci_device *pcidev = to_pci_dev(device);
    struct usb_controller *ctlr;

    ctlr = usb_init_controller(pcidev);
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
