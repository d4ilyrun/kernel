/*
 * USB host controller driver API.
 */

#define LOG_DOMAIN "hcd"

#include <kernel/atomic.h>
#include <kernel/devices/pci.h>
#include <kernel/devices/usb.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>

static atomic_t hcd_bus_count = { 0 };

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
    /*
     * Make sure URB is valid.
     */
    switch (urb->pipe->type) {
    case USB_XFER_BULK:
    case USB_XFER_CONTROL:
        urb->completed = false;
        break;

    case USB_XFER_INTERRUPT:
        /* interrupt transfers must define an interrupt callback. */
        if (urb->interrupt == NULL)
            return E_INVAL;
        break;

    case USB_XFER_ISOCHRONOUS:
        WARN("xfer_type: %d", urb->pipe->type);
        break;
    }

    return device->controller->ops->urb_submit(device, urb);
}

/*
 *
 */
void urb_cancel(struct urb *urb, error_t error)
{
    urb->result = error;

    switch (urb->pipe->type) {
    case USB_XFER_INTERRUPT:
        urb->interrupt(urb, urb->interrupt_data);
        break;

    case USB_XFER_BULK:
    case USB_XFER_CONTROL:
        urb->completed = true;
        break;

    case USB_XFER_ISOCHRONOUS:
        break;
    }
}

/*
 *
 */
static interrupt_return_t hcd_interrupt(void *data)
{
    struct usb_controller *hcd = data;

    return hcd->ops->interrupt_handler(hcd);
}

/*
 *
 */
static void hcd_destroy(struct usb_controller *hcd)
{
    /* recursively destroy all USB devices connected to the controller */
    if (hcd->root_hub)
        usb_device_destroy(hcd->root_hub->udev);

    if (hcd->ops->destroy)
        hcd->ops->destroy(hcd);

    kfree(hcd);
}

/*
 *
 */
static struct usb_controller *hcd_init(struct pci_device *pcidev)
{
    struct usb_controller *hcd;
    struct usb_controller_ops *hcd_ops;
    error_t err;

    pci_device_enable_memory(pcidev, true);
    pci_device_enable_bus_master(pcidev, true);

    hcd = kcalloc(1, sizeof(*hcd), KMALLOC_KERNEL);
    if (!hcd)
        return PTR_ERR(E_NOMEM);

    switch (pcidev->class.interface) {
    case USB_UHCI_PCI_INTERFACE:
        device_set_name(&pcidev->device, "uhci-hcd");
        hcd_ops = &uhci_controller_ops;
        break;
    default:
        log_err("unsupported PCI class %02x", pcidev->class.interface);
        return PTR_ERR(E_NOT_SUPPORTED);
    }

    hcd->ops = hcd_ops;
    hcd->pcidev = pcidev;
    hcd->bus = atomic_inc(&hcd_bus_count) + 1;

    err = hcd_ops->init(hcd);
    if (err)
        goto fail;

    err = pci_device_install_interrupt_handler(pcidev, hcd_interrupt, hcd);
    if (err) {
        log_err("failed to register interrupt");
        goto fail;
    }

    return hcd;

fail:
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
    { .class = PCI_CLASS(0x0C, 0x03, USB_UHCI_PCI_INTERFACE) },
    { /* sentinel */ },
};

struct pci_driver usb_driver = {
    .compatible = usb_compatible,
    .driver =
        {
            .name = "usb-hcd",
            .operations.probe = usb_probe,
        },
};

DECLARE_PCI_DRIVER(usb, &usb_driver);
