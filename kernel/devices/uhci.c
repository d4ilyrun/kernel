#include "kernel/mmu.h"
#include "kernel/spinlock.h"
#include "kernel/timer.h"
#define LOG_DOMAIN "uhci"

#include <kernel/devices/pci.h>
#include <kernel/devices/uhci.h>
#include <kernel/devices/usb.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>

/** UHCI (USB1.0) host controller. */
struct uhci_controller {
    struct usb_controller controller;
    u16 io_reg;
    spinlock_t lock;

    struct uhci_pointer *frame_list;
    size_t frame_list_size;
    size_t frame_list_index;

    unsigned int pending_iso;
    unsigned int pending_bulk;
    unsigned int pending_control;
};

static inline struct uhci_controller *to_uhci(struct usb_controller *usb)
{
    return container_of(usb, struct uhci_controller, controller);
}

generate_device_io_rw_functions(uhci, struct uhci_controller, io_reg, u16);

/* Number of times transfers should be retried when they do not complete. */
#define UHCI_TRANSFER_RETRY_COUNT 4

#define UHCI_FRAME_LIST_POINTER_COUNT 1024

/*
 *
 */
static error_t uhci_frame_push(struct uhci_controller *uhci)
{
    spinlock_acquire(&uhci->lock);
    spinlock_release(&uhci->lock);

}

/*
 *
 */
static error_t uhci_frame_collect(struct uhci_controller *uhci)
{
    spinlock_acquire(&uhci->lock);
    spinlock_release(&uhci->lock);
}

/*
 *
 */
static error_t uhci_control_transfer(struct usb_controller *ctrl)
{
    UNUSED(ctrl);
    return E_SUCCESS;
}

static struct usb_controller_ops uhci_controller_ops = {
    .control_transfer = uhci_control_transfer,
};

/*
 * Start or stop the controller's scheduler.
 *
 * When disabled the controller first completes the current transaction before
 * halting.
 */
static inline int uhci_enable_transfer(struct uhci_controller *uhci, bool enable)
{
    u16 val;

    val = uhci_inw(uhci, UHCI_USBCMD);
    if (enable)
        val |= UHCI_USBCMD_RUN;
    else
        val &= ~UHCI_USBCMD_RUN;
    uhci_outw(uhci, UHCI_USBCMD, val);
}

/*
 * Interrput handler for UHCI controller interrputs.
 */
static u32 uhci_interrupt_handler(void *data)
{
    UNUSED(data);
    log_info("interrupt received");
    return E_SUCCESS;
}

/*
 *
 */
static error_t uhci_init_interrupts(struct uhci_controller *uhci)
{
    u16 val;
    error_t err;

    err = pci_device_register_interrupt_handler(uhci->controller.pcidev,
                                                uhci_interrupt_handler, uhci);
    if (err)
        return err;

    val = 0;
    uhci_outw(uhci, UHCI_USBINTR, val);

    return E_SUCCESS;
}

/*
 * Allocate and initialize the controller's framelist.
 */
static error_t uhci_init_framelist(struct uhci_controller *uhci)
{
    paddr_t paddr;

    uhci->frame_list = kcalloc(UHCI_FRAME_LIST_POINTER_COUNT,
                               sizeof(*uhci->frame_list), KMALLOC_KERNEL);
    if (!uhci->frame_list)
        return E_NOMEM;

    /* Save framelist address for hardware. */
    paddr = mmu_find_physical((vaddr_t)uhci->frame_list);
    uhci_outl(uhci, UHCI_FLBASEADD, paddr);

    /* Mark all entries as empty. */
    for (int i = 0; i < UHCI_FRAME_LIST_POINTER_COUNT; ++i)
        uhci->frame_list[i].terminate = 1;

    return E_SUCCESS;
}

/*
 * Reset the USB bus connected to an UHCI controller.
 */
static void uhci_reset(struct uhci_controller *uhci)
{
    u16 val;

    val = uhci_inw(uhci, UHCI_USBCMD);
    val |= UHCI_USBCMD_GLOBAL_RESET;
    val |= UHCI_USBCMD_HC_RESET;
    uhci_outw(uhci, UHCI_USBCMD, val);

    timer_delay_ms(10);

    val &= ~UHCI_USBCMD_GLOBAL_RESET;
    uhci_outw(uhci, UHCI_USBCMD, val);
}

/*
 *
 */
struct usb_controller *uhci_init_controller(struct pci_device *pcidev)
{
    struct uhci_controller *uhci;
    error_t err;
    u32 val;

    uhci = kcalloc(1, sizeof(*uhci), KMALLOC_KERNEL);
    if (!uhci)
        return PTR_ERR(E_NOMEM);

    INIT_SPINLOCK(uhci->lock);

    uhci->controller.ops = &uhci_controller_ops;
    uhci->controller.pcidev = pcidev;
    uhci->io_reg = pci_device_read_config(pcidev, UHCI_PCI_USBBASE,
                                          UHCI_PCI_USBBASE_SIZE);
    uhci->io_reg &= ~1; /* clear i/o mem indicator. */

    pci_device_enable_bus_master(pcidev, true);

    uhci_reset(uhci);

    err = uhci_init_framelist(uhci);
    if (err) {
        log_err("failed to init framelist: %pe", &err);
        goto error;
    }

    err = uhci_init_interrupts(uhci);
    if (err) {
        log_err("failed to init interrupts: %pe", &err);
        goto error;
    }

    /* Mark the controller as being completely configured. */
    val = uhci_inw(uhci, UHCI_USBCMD);
    val |= UHCI_USBCMD_CF;
    uhci_outw(uhci, UHCI_USBCMD, val);

    uhci_enable_transfer(uhci, true);

    return &uhci->controller;

error:
    kfree(uhci);
    return PTR_ERR(err);
}
