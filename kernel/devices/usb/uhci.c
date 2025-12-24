/*
 * UHCI host controller driver.
 *
 * References:
 * - USB 1.1 documentation
 * - Intel UHCI documentation
 * - https://www.beyondlogic.org/usbnutshell
 */

#define LOG_DOMAIN "uhci"

#include <kernel/devices/pci.h>
#include <kernel/devices/uhci.h>
#include <kernel/devices/usb.h>
#include <kernel/init.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/memory/slab.h>
#include <kernel/mmu.h>
#include <kernel/spinlock.h>
#include <kernel/timer.h>

#include <libalgo/linked_list.h>

/*
 * Transfer descriptor.
 */
struct uhci_td {
    /* Hardware fields (UHCI documentation 3.2) */
    u32 link_pointer;
    u32 status;
    u32 token;
    u32 buffer_pointer;

    /* Software fields */
    node_t this;
};

/*
 * Queue head.
 */
struct uhci_qh {
    /* Hardware fields (UHCI documentation 3.3) */
    u32 link_pointer;
    u32 element_pointer;

    /* Software fields */
    llist_t tds; /* linked list of transfer descriptors */
};

#define UHCI_QH_COUNT 4
#define   UHCI_QH_INTERRUPT     0   /* Queue-head for interrupt TDs. */
#define   UHCI_QH_CONTROL_LOW   1   /* Queue-head for low speed control TDs. */
#define   UHCI_QH_CONTROL_FULL  2   /* Queue-head for full speed control TDs. */
#define   UHCI_QH_BULK          3   /* Queue-head for bulk TDs. */

/** UHCI (USB1.0) host controller. */
struct uhci_controller {
    struct usb_controller controller;
    spinlock_t lock;
    u16 io_reg;

    struct uhci_qh *queue_heads[UHCI_QH_COUNT];

    void *frame_list;
};

static inline struct uhci_controller *to_uhci(struct usb_controller *usb)
{
    return container_of(usb, struct uhci_controller, controller);
}

static struct kmem_cache *kmem_cache_uhci_td;

generate_device_io_rw_functions(uhci, struct uhci_controller, io_reg, u16);

static struct usb_controller_ops uhci_controller_ops = {};

/*
 * Cache used to allocate uhci_td structures.
 */
static struct kmem_cache *kmem_cache_uhci_td;

/*
 *
 */
static void uhci_td_free(struct uhci_td *td)
{
    kmem_cache_free(kmem_cache_uhci_td, td);
}

/*
 *
 */
static void uhci_td_destroy(struct uhci_td *td)
{
    llist_remove(&td->this);
    uhci_td_free(td);
}

/*
 *
 */
static struct uhci_td *uhci_new_td(struct uhci_qh *qh)
{
    struct uhci_td *td;

    td = kmem_cache_alloc(kmem_cache_uhci_td, 0);
    if (!td)
        return NULL;

    llist_add_tail(&qh->tds, &td->this);

    return td;
}

/*
 *
 */
static void uhci_qh_free(struct uhci_qh *qh)
{
    kfree_dma(qh);
}

/*
 *
 */
static void uhci_qh_destroy(struct uhci_qh *qh)
{
    struct uhci_td *td;
    struct uhci_td *tmp;

    FOREACH_LLIST_ENTRY_SAFE(td, tmp, &qh->tds, this) {
        uhci_td_destroy(td);
    }

    uhci_qh_free(qh);
}

/*
 * Start or stop the controller's scheduler.
 *
 * When disabled the controller first completes the current transaction before
 * halting.
 */
static inline void
uhci_enable_transfer(struct uhci_controller *uhci, bool enable)
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
 * Reset the USB bus connected to an UHCI controller.
 */
static void uhci_bus_reset(struct uhci_controller *uhci)
{
    u16 val;

    val = uhci_inw(uhci, UHCI_USBCMD);
    val |= UHCI_USBCMD_GLOBAL_RESET;
    val |= UHCI_USBCMD_HC_RESET;
    uhci_outw(uhci, UHCI_USBCMD, val);

    timer_delay_ms(10);

    val = uhci_inw(uhci, UHCI_USBCMD);
    val &= ~UHCI_USBCMD_GLOBAL_RESET;
    uhci_outw(uhci, UHCI_USBCMD, val);
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

    uhci->frame_list = kmalloc_dma(UHCI_FRAMELIST_SIZE);
    if (!uhci->frame_list)
        return E_NOMEM;

    /* Save framelist address for hardware. */
    paddr = mmu_find_physical((vaddr_t)uhci->frame_list);
    uhci_outl(uhci, UHCI_FLBASEADD, paddr);

    /* Mark all entries as empty. */
    for (size_t i = 0; i < uhci->frame_list_size; ++i)
        uhci->frame_list[i].terminate = 1;

    return E_SUCCESS;
}

/*
 *
 */
static error_t uhci_init_queue_heads(struct uhci_controller *uhci)
{
    struct uhci_qh *qh;
    error_t err;

    err = E_NOMEM;
    for (int i = UHCI_QH_COUNT - 1; i >= 0; --i) {
        qh = kmalloc_dma(sizeof(*qh));
        if (!qh)
            goto exit_error;
        uhci->queue_heads[i] = qh;
    }

    return E_SUCCESS;

exit_error:
    return err;
}

/*
 *
 */
static void uhci_controller_destroy(struct uhci_controller *uhci)
{
    for (int i = 0; i < UHCI_QH_COUNT; ++i) {
        if (uhci->queue_heads[i])
            uhci_qh_destroy(uhci->queue_heads[i]);
    }
}

/*
 *
 */
struct usb_controller *uhci_init_controller(struct pci_device *pcidev)
{
    struct uhci_controller *uhci;
    error_t err;
    u32 val;

    /*
     * uhci_init() failed so UHCI is unusable.
     */
    if (!kmem_cache_uhci_td)
        return PTR_ERR(E_NOT_SUPPORTED);

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

    uhci_bus_reset(uhci);

    err = uhci_init_framelist(uhci);
    if (err) {
        log_err("failed to init framelist: %pe", &err);
        goto error;
    }

    err = uhci_init_queue_heads(uhci);
    if (err) {
        log_err("failed to init queue heads: %pe", &err);
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
    uhci_controller_destroy(uhci);
    return PTR_ERR(err);
}

/*
 * Initialize the UHCI API before PCI devices are probed.
 */
static error_t uhci_init(void)
{
    kmem_cache_uhci_td = kmem_cache_create("uhci:td", sizeof(struct uhci_td),
                                           CPU_CACHE_ALIGN, NULL, NULL);
    if (!kmem_cache_uhci_td)
        return E_NOMEM;

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_EARLY, uhci_init);
