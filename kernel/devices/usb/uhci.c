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
#include <utils/macro.h>

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
    struct urb *urb; /* Only present inside the first TD of a transfer. */
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
#define UHCI_QH_INTERRUPT 0    /* Queue-head for interrupt TDs. */
#define UHCI_QH_CONTROL_LOW 1  /* Queue-head for low speed control TDs. */
#define UHCI_QH_CONTROL_FULL 2 /* Queue-head for full speed control TDs. */
#define UHCI_QH_BULK 3         /* Queue-head for bulk TDs. */

/** UHCI (USB1.0) host controller. */
struct uhci_controller {
    struct usb_controller controller;
    spinlock_t lock;
    u16 io_reg;

    u32 *frame_list;
    size_t frame_list_size;
    struct uhci_qh *queue_heads[UHCI_QH_COUNT];
};

static inline struct uhci_controller *to_uhci(struct usb_controller *usb)
{
    return container_of(usb, struct uhci_controller, controller);
}

generate_device_io_rw_functions(uhci, struct uhci_controller, io_reg, u16);

static struct usb_controller_ops uhci_controller_ops;

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
static struct uhci_td *uhci_td_new(struct uhci_qh *qh)
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

    FOREACH_LLIST_ENTRY_SAFE (td, tmp, &qh->tds, this) {
        uhci_td_destroy(td);
    }

    uhci_qh_free(qh);
}

/*
 *
 */
static inline void uhci_td_fill(struct uhci_td *td, enum usb_pid pid,
                                struct usb_pipe *pipe, paddr_t dma_addr,
                                size_t maxlen)
{
    td->buffer_pointer = dma_addr;
    td->status = 3 << UHCI_TD_STATUS_MAXERR_OFFSET;

    if (pipe->device->speed == USB_SPEED_LOW)
        td->status |= UHCI_TD_STATUS_LS;

    td->token = usb_pid_field(pid);
    td->token |= (pipe->endpoint & UHCI_TD_TOKEN_EP_MASK)
              << UHCI_TD_TOKEN_EP_OFFSET;
    td->token |= (pipe->device->address & UHCI_TD_TOKEN_ADDR_MASK)
              << UHCI_TD_TOKEN_ADDR_OFFSET;
    td->token |= (maxlen & UHCI_TD_TOKEN_MAXLEN_MASK)
              << UHCI_TD_TOKEN_MAXLEN_OFFSET;
}

#define uhci_link_new_td(qh, td)                                        \
    ({                                                                  \
        u32 *link_pointer = &td->link_pointer;                          \
        struct uhci_td *__new = uhci_td_new(qh);                        \
        paddr_t paddr = mmu_find_physical((vaddr_t)__new);               \
        *link_pointer &= ~(UHCI_PTR_LINK_MASK << UHCI_PTR_LINK_OFFSET); \
        *link_pointer |= paddr << UHCI_PTR_LINK_OFFSET;                 \
        __new;                                                          \
    })

/*
 * @ref USB 2.0 - 8.5.3
 */
static error_t
uhci_urb_submit_control(struct uhci_controller *uhci, struct urb *urb)
{
    struct usb_pipe *pipe = urb->pipe;
    struct uhci_qh *qh;
    struct uhci_td *last_td;
    struct uhci_td *td;
    enum usb_pid pid;
    paddr_t paddr;
    paddr_t data;
    size_t off;
    bool toggle;

    qh = (pipe->device->speed == USB_SPEED_LOW)
           ? uhci->queue_heads[UHCI_QH_CONTROL_LOW]
           : uhci->queue_heads[UHCI_QH_CONTROL_FULL];
    last_td = llist_last_entry(&qh->tds, typeof(*last_td), this);

    /* 8.5.3 - Data stage is optional. */
    if (urb->data_size > 0) {
        data = mmu_find_physical((vaddr_t)urb->data);
        if (IS_ERR(data)) {
            log_warn("%s: unmapped data", device_name(&pipe->device->dev));
            return ERR_FROM_PTR((void *)data);
        }
    }

    /*
     * Setup stage.
     */

    paddr = mmu_find_physical((vaddr_t)urb->setup);
    if (IS_ERR(paddr)) {
        log_warn("%s: unmapped setup packet", device_name(&pipe->device->dev));
        return ERR_FROM_PTR((void *)paddr);
    }

    td = last_td;
    td->urb = urb;
    uhci_td_fill(td, USB_PID_SETUP, pipe, paddr, USB_SETUP_PACKET_SIZE);

    /*
     * Data stage.
     */

    off = 0;
    toggle = false;
    pid = pipe->direction == USB_PIPE_DIR_INPUT ? USB_PID_IN : USB_PID_OUT;

    while (off < urb->data_size) {
        size_t size;

        size = pipe->max_packet_size;
        if (size < urb->data_size - off)
            size = urb->data_size - off;

        /* TODO: Detect short packets (8.5.3.2) */
        td = uhci_link_new_td(qh, td);
        uhci_td_fill(td, pid, pipe, data + off, size);
        if (toggle)
            td->token |= UHCI_TD_TOKEN_TOGGLE;

        toggle = !toggle;
        off += size;
    }

    /*
     * Status stage (8.5.3.1).
     */

    td = uhci_link_new_td(qh, td);
    pid = (pid == USB_PID_IN) ? USB_PID_OUT : USB_PID_IN;
    uhci_td_fill(td, pid, pipe, 0, 0);
    td->token |= UHCI_TD_TOKEN_TOGGLE;

    /*
     * Add a new dummy TD and activate this packet.
     */

    td = uhci_link_new_td(qh, td);
    td->status = 0; /* inactive */
    last_td->status |= UHCI_TD_STATUS_ACTIVE;

    return E_SUCCESS;
}

/*
 *
 */
static error_t uhci_urb_submit(struct usb_device *device, struct urb *urb)
{
    struct usb_controller *hcd = device->controller;
    struct uhci_controller *uhci = to_uhci(hcd);

    switch (urb->pipe->type) {
    case USB_XFER_CONTROL:
        return uhci_urb_submit_control(uhci, urb);

    case USB_XFER_ISOCHRONOUS:
    case USB_XFER_INTERRUPT:
    case USB_XFER_BULK:
        not_implemented("usb xfer type: %d", urb->pipe->type);
        return E_NOT_IMPLEMENTED;
    }

    return E_NOT_SUPPORTED;
}

/*
 * Interrput handler for UHCI controller interrputs.
 */
static error_t uhci_interrupt_handler(struct usb_controller *hcd)
{
    struct uhci_controller *uhci = to_uhci(hcd);
    u32 status;

    status = uhci_inw(uhci, UHCI_USBSTS);
    if (!status)
        return E_SUCCESS; /* Not for us. */

    /* Clear interrupt. */
    uhci_outw(uhci, UHCI_USBSTS, status);

    if (status & UHCI_USBINTR_COMPLETE)

    return E_SUCCESS;
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
 * Allocate and initiliaze the queue-heads used to schedule the different
 * types of transfer.
 *
 * We allocate one queue head per type of transfer, sorted by 'priority'
 * (interrupt transfers are always executed first, then control transfers
 * and bulk transfers if there is enough bandwidth.
 */
static error_t uhci_init_queue_heads(struct uhci_controller *uhci)
{
    struct uhci_qh *qh;
    error_t err;

    err = E_NOMEM;
    for (int i = UHCI_QH_COUNT - 1; i >= 0; --i) {
        struct uhci_td *td;
        paddr_t paddr;

        qh = kmalloc_dma(sizeof(*qh));
        if (!qh)
            goto exit_error;

        /* Link queue heads together. */
        uhci->queue_heads[i] = qh;
        if (i != UHCI_QH_COUNT) {
            paddr = mmu_find_physical((vaddr_t)uhci->queue_heads[i + 1]);
            qh->link_pointer = paddr << UHCI_PTR_LINK_OFFSET;
            qh->link_pointer |= UHCI_PTR_QH_SELECT;
        } else {
            qh->link_pointer = UHCI_PTR_TERMINATE;
        }

        /*
         * Initialize dummy TD.
         *
         * An inactive TD is always present at the end of the QH's TDs.
         * The inactive bit is read by hardware to check if the queue
         * has any transfers left to process.
         *
         * @see UHCI 3.4.1.3
         */
        td = uhci_td_new(qh);
        if (!td)
            goto exit_error;

        td->status = 0; /* inactive */

        paddr = mmu_find_physical((vaddr_t)td);
        qh->link_pointer = paddr << UHCI_PTR_LINK_OFFSET;
    }

    return E_SUCCESS;

exit_error:
    return err;
}

/*
 * Allocate and initialize the controller's framelist.
 *
 * All framelist point to the interrupt queue head.
 */
static error_t uhci_init_framelist(struct uhci_controller *uhci)
{
    paddr_t paddr;

    uhci->frame_list_size = UHCI_FRAMELIST_SIZE;
    uhci->frame_list = kmalloc_dma(uhci->frame_list_size *
                                   sizeof(*uhci->frame_list));
    if (!uhci->frame_list)
        return E_NOMEM;

    /* Make all frame list entries point to the interrupt queue head. */
    paddr = mmu_find_physical((vaddr_t)uhci->queue_heads[UHCI_QH_INTERRUPT]);
    for (size_t i = 0; i < uhci->frame_list_size; ++i) {
        uhci->frame_list[i] = paddr << UHCI_PTR_LINK_OFFSET;
        uhci->frame_list[i] |= UHCI_PTR_QH_SELECT;
    }

    /* Save framelist address for hardware. */
    paddr = mmu_find_physical((vaddr_t)uhci->frame_list);
    uhci_outl(uhci, UHCI_FLBASEADD, paddr);

    return E_SUCCESS;
}

/*
 *
 */
static error_t uhci_init_interrupts(struct uhci_controller *uhci)
{
    uhci_outw(uhci, UHCI_USBINTR, 0);

    return E_SUCCESS;
}

/*
 *
 */
static void uhci_controller_destroy(struct usb_controller *hcd)
{
    struct uhci_controller *uhci = to_uhci(hcd);

    for (int i = 0; i < UHCI_QH_COUNT; ++i) {
        if (uhci->queue_heads[i])
            uhci_qh_destroy(uhci->queue_heads[i]);
    }
}

/*
 *
 */
struct usb_controller *uhci_controller_new(struct pci_device *pcidev)
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

    err = uhci_init_queue_heads(uhci);
    if (err) {
        log_err("failed to init queue heads: %pe", &err);
        goto error;
    }

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
    uhci_controller_destroy(&uhci->controller);
    return PTR_ERR(err);
}

static struct usb_controller_ops uhci_controller_ops = {
    .destroy = uhci_controller_destroy,
    .urb_submit = uhci_urb_submit,
    .interrupt_handler = uhci_interrupt_handler,
};

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
