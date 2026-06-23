/*
 * UHCI host controller driver.
 *
 * References:
 * - USB 1.1 documentation
 * - Intel UHCI documentation
 * - https://www.beyondlogic.org/usbnutshell
 */

#include "kernel/error.h"
#include "specs/usb.h"
#define LOG_DOMAIN "uhci"

#include <kernel/devices/pci.h>
#include <kernel/devices/usb.h>
#include <kernel/init.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/memory/slab.h>
#include <kernel/mmu.h>
#include <kernel/spinlock.h>
#include <kernel/timer.h>

#include <specs/uhci.h>
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
    struct urb *urb; /* Only present inside the first TD of a transfer. */
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
    llist_t    tds; /* linked list of transfer descriptors */
    spinlock_t tds_lock;
};

/*
 * We allocate one queue head per type of transfer. The queues are sorted
 * by 'priority' (interrupt transfers are always executed first, then control
 * transfers and bulk transfers if there is enough bandwidth).
 *
 * This enum defines the different queue-head priorities.
 *
 * TODO: Multiple QH_INTERRUPT queues for the different interrupt frequencies.
 *       As it is, interrupt transfers are sent every 1ms regardless of the
 *       polling frequency specified inside the endpoint's descriptor.
 */
enum uhci_qh_prio {
    UHCI_QH_INTERRUPT,    /* Queue-head for interrupt TDs. */
    UHCI_QH_CONTROL_LOW,  /* Queue-head for low speed control TDs. */
    UHCI_QH_CONTROL_FULL, /* Queue-head for full speed control TDs. */
    UHCI_QH_BULK,         /* Queue-head for bulk TDs. */
    UHCI_QH_COUNT,
};

/** UHCI (USB1.0) host controller. */
struct uhci_controller {
    u16 io_reg;
    u32 *frame_list;
    size_t frame_list_size;
    struct uhci_qh *qhs[UHCI_QH_COUNT];
    bool destroy_pending;
};

generate_device_io_rw_functions(uhci, struct uhci_controller, io_reg, u16);

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
 * Must be called under the appropriate queue_head's lock.
 */
static void uhci_td_destroy(struct uhci_td *td)
{
    llist_remove(&td->this);
    td->urb = NULL;

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

    td->status = 0;
    llist_add_tail(&qh->tds, &td->this);

    return td;
}

/*
 *
 */
static void uhci_td_dump(const struct uhci_td *td)
{
    u32 token = td->token;
    u32 status = td->status;
    u32 maxlen;

    maxlen = (token & UHCI_TD_TOKEN_MAXLEN_MASK);
    if (maxlen == (u16)UHCI_TD_TOKEN_MAXLEN_MASK)
        maxlen = 0;
    else
        maxlen = (maxlen >> UHCI_TD_TOKEN_MAXLEN_OFFSET) + 1;

    log_info("token:  %s%s (address=%u endpoint=%#02x length=%u)",
             usb_pid_field_name(token & UHCI_TD_TOKEN_PID_MASK),
             (token & UHCI_TD_TOKEN_TOGGLE) ? " TOGGLE" : "",
             (token & UHCI_TD_TOKEN_ADDR_MASK) >> UHCI_TD_TOKEN_ADDR_OFFSET,
             (token & UHCI_TD_TOKEN_EP_MASK) >> UHCI_TD_TOKEN_EP_OFFSET,
             maxlen);

    log_info("status: %s%s%s%s",
             (status & UHCI_TD_STATUS_ACTIVE) ? "ACTIVE " : "",
             (status & UHCI_TD_STATUS_STALL) ? "STALL " : "",
             (status & UHCI_TD_STATUS_NAK) ? "NAK " : "",
             (status & UHCI_TD_STATUS_SPD) ? "SPD " : "");
}

/*
 *
 */
static struct uhci_qh *uhci_qh_new(void)
{
    struct uhci_qh *qh;

    qh = kmalloc_dma(sizeof(*qh));
    if (!qh)
        return NULL;

    INIT_LLIST(qh->tds);
    INIT_SPINLOCK(qh->tds_lock);

    return qh;
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
static void uhci_destroy(struct uhci_controller *uhci)
{
    for (int i = 0; i < UHCI_QH_COUNT; ++i) {
        if (uhci->qhs[i]) {
            uhci_qh_destroy(uhci->qhs[i]);
            uhci->qhs[i] = NULL;
        }
    }

    if (uhci->frame_list)
        kfree_dma(uhci->frame_list);

    kfree(uhci);
}

/*
 *
 */
static inline void uhci_td_fill(const struct usb_device *udev,
                                struct uhci_td *td, enum usb_pid pid,
                                struct usb_pipe *pipe, paddr_t dma_addr,
                                size_t maxlen)
{
    td->buffer_pointer = dma_addr;
    td->status = 3 << UHCI_TD_STATUS_MAXERR_OFFSET;
    td->status |= UHCI_TD_STATUS_IOC;

    if (udev->speed == USB_SPEED_LOW)
        td->status |= UHCI_TD_STATUS_LS;

    td->token = usb_pid_field(pid);
    td->token |= (pipe->endpoint << UHCI_TD_TOKEN_EP_OFFSET) &
                 UHCI_TD_TOKEN_EP_MASK;
    td->token |= (udev->address << UHCI_TD_TOKEN_ADDR_OFFSET) &
                 UHCI_TD_TOKEN_ADDR_MASK;
    td->token |= ((maxlen - 1) << UHCI_TD_TOKEN_MAXLEN_OFFSET) &
                 UHCI_TD_TOKEN_MAXLEN_MASK;
}

#define uhci_link_new_td(qh, td)                           \
    ({                                                     \
        u32 *link_pointer = &td->link_pointer;             \
        struct uhci_td *__new = uhci_td_new(qh);           \
        paddr_t paddr = mmu_find_physical((vaddr_t)__new); \
        *link_pointer &= ~UHCI_PTR_LINK_MASK;              \
        *link_pointer |= paddr & UHCI_PTR_LINK_MASK;       \
        __new;                                             \
    })

/*
 * @ref USB 2.0 - 8.5.3
 */
static error_t
uhci_urb_submit_control(struct uhci_controller *uhci, struct usb_device *udev,
                        struct urb *urb)
{
    struct usb_pipe *pipe = urb->pipe;
    struct uhci_qh *qh;
    struct uhci_td *first_td;
    struct uhci_td *td;
    enum usb_pid pid;
    paddr_t setup;
    paddr_t data;
    size_t off;
    bool toggle;

    /* 8.5.3 - Data stage is optional. */
    if (urb->data_size > 0) {
        data = mmu_find_physical((vaddr_t)urb->data);
        if (IS_ERR(data)) {
            log_warn("%s: unmapped data", device_name(&udev->dev));
            return ERR_FROM_PTR((void *)data);
        }
    }

    setup = mmu_find_physical((vaddr_t)urb->setup);
    if (IS_ERR(setup)) {
        log_warn("%s: unmapped setup packet", device_name(&udev->dev));
        return ERR_FROM_PTR((void *)setup);
    }

    qh = (udev->speed == USB_SPEED_LOW) ? uhci->qhs[UHCI_QH_CONTROL_LOW]
                                        : uhci->qhs[UHCI_QH_CONTROL_FULL];
    spinlock_acquire(&qh->tds_lock);

    /*
     * Setup stage.
     */

    first_td = llist_last_entry(&qh->tds, struct uhci_td, this);
    td = first_td;
    uhci_td_fill(udev, td, USB_PID_SETUP, pipe, setup, USB_SETUP_PACKET_SIZE);

    /*
     * Data stage.
     */

    off = 0;
    toggle = false;
    pid = usb_pipe_is_input(pipe) ? USB_PID_IN : USB_PID_OUT;

    while (off < urb->data_size) {
        size_t size;

        size = pipe->max_packet_size;
        if (size < urb->data_size - off)
            size = urb->data_size - off;

        td = uhci_link_new_td(qh, td);
        uhci_td_fill(udev, td, pid, pipe, data + off, size);
        td->status |= UHCI_TD_STATUS_ACTIVE;
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
    uhci_td_fill(udev, td, pid, pipe, 0, 0);
    td->status |= UHCI_TD_STATUS_ACTIVE;
    td->token |= UHCI_TD_TOKEN_TOGGLE;
    td->urb = urb;

    /*
     * Add a new inactive TD and activate this packet.
     */

    td = uhci_link_new_td(qh, td);
    td->status = 0; /* inactive */
    first_td->status |= UHCI_TD_STATUS_ACTIVE;

    spinlock_release(&qh->tds_lock);

    return E_SUCCESS;
}

/*
 *
 */
static error_t uhci_urb_submit(struct usb_device *device, struct urb *urb)
{
    struct usb_controller *controller = device->controller;
    struct uhci_controller *uhci = controller->priv;

    if (WARN_ON(uhci->destroy_pending))
        return E_IO;

    switch (urb->pipe->type) {
    case USB_XFER_CONTROL:
        return uhci_urb_submit_control(uhci, device, urb);

    case USB_XFER_ISOCHRONOUS:
    case USB_XFER_INTERRUPT:
    case USB_XFER_BULK:
        not_implemented("usb xfer type: %d", urb->pipe->type);
        return E_NOT_IMPLEMENTED;
    }

    return E_NOT_SUPPORTED;
}

/*
 * Check whether this TD is the dummy TD placed at the end of the QH
 * to signify that there are no other "real" TDs left to transfer.
 *
 * @see uhci_init_queue_heads
 */
static inline bool uhci_td_is_dummy(const struct uhci_qh *qh,
                                    const struct uhci_td *td)
{
    return &td->this == llist_last(&qh->tds);
}

/*
 *
 */
static void uhci_process_control_urbs(struct uhci_controller *uhci, int qh_idx)
{
    struct uhci_qh *qh = uhci->qhs[qh_idx];
    struct uhci_td *td;
    struct uhci_td *next;
    paddr_t paddr;

    spinlock_acquire(&qh->tds_lock);
    FOREACH_LLIST_ENTRY_SAFE(td, next, &qh->tds, this) {
        error_t err = E_SUCCESS;

        if (uhci_td_is_dummy(qh, td))
            continue;
        if (td->status & UHCI_TD_STATUS_ACTIVE)
            continue;

        if (td->status & UHCI_TD_STATUS_NAK) {
            log_info("NAK received");
            err = E_IO;
        } else if (td->status & UHCI_TD_STATUS_CRC) {
            switch(usb_pid_field(td->token & UHCI_TD_TOKEN_PID_MASK)) {
            case USB_PID_SETUP:
            case USB_PID_OUT:
                log_info("Timeout error detected");
                err = E_TIMED_OUT;
                break;
            default:
                log_info("CRC error detected");
                err = E_IO;
                break;
            }
        } else if (td->status & UHCI_TD_STATUS_BABBLE) {
            log_info("Babble detected");
            err = E_IO;
        } else if (td->status & UHCI_TD_STATUS_STALL) {
            log_info("Bus stall detected");
            err = E_IO;
        } else if (td->status & UHCI_TD_STATUS_BITSTUFF) {
            log_info("Bit stuffing detected");
            err = E_IO;
        } else if (td->status & UHCI_TD_STATUS_DATABUF) {
            log_info("Data buffer error detected");
            err = E_IO;
        }

        if (err) {
            uhci_td_dump(td);
            if (td->urb)
                urb_cancel(td->urb, err);
        } else {
            if (td->urb)
                urb_complete(td->urb);
        }

        uhci_td_destroy(td);
    }

    td = llist_first_entry(&qh->tds, struct uhci_td, this);
    paddr = mmu_find_physical((vaddr_t)td);
    qh->element_pointer = paddr & UHCI_PTR_LINK_MASK;
    qh->element_pointer |= UHCI_PTR_VF;

    spinlock_release(&qh->tds_lock);
}

/*
 * Handle and re-arm all interrupt transfers.
 *
 * Interrupts transfers are setup once and sent periodically. Once an interrupt
 * transfer has completed, we instantly re-arm it (i.e. mark it as valid) so
 * that it can be sent again the next time it is due.
 *
 * We also need to reset the queue head's element pointer back to the first
 * interrupt transfer since this value is updated by the hardware whenever
 * transfer has been completed.
 */
static void uhci_process_interrupt_urbs(struct uhci_controller *uhci,
                                        enum uhci_qh_prio qh_idx)
{
    struct uhci_qh *qh = uhci->qhs[qh_idx];
    struct uhci_td *td;
    paddr_t paddr;

    spinlock_acquire(&qh->tds_lock);
    FOREACH_LLIST_ENTRY(td, &qh->tds, this) {
        if (uhci_td_is_dummy(qh, td))
            continue;

        td->status |= UHCI_TD_STATUS_ACTIVE;
        if (td->urb)
            urb_complete(td->urb);
    }

    td = llist_first_entry(&qh->tds, struct uhci_td, this);
    paddr = mmu_find_physical((vaddr_t)td);
    qh->element_pointer = paddr & UHCI_PTR_LINK_MASK;
    qh->element_pointer |= UHCI_PTR_VF;

    spinlock_release(&qh->tds_lock);
}

/*
 *
 */
static void uhci_process_urbs(struct uhci_controller *uhci)
{
    /* FIXME: don't take spinlock inside an interrupt handler ! */
    uhci_process_interrupt_urbs(uhci, UHCI_QH_INTERRUPT);
    uhci_process_control_urbs(uhci, UHCI_QH_CONTROL_LOW);
    uhci_process_control_urbs(uhci, UHCI_QH_CONTROL_FULL);
}

/*
 * Interrupt handler for UHCI controller.
 */
static interrupt_return_t
uhci_interrupt_handler(struct usb_controller *controller)
{
    struct uhci_controller *uhci = controller->priv;
    u32 status;

    status = uhci_inw(uhci, UHCI_USBSTS);
    if (!status)
        return INTERRUPT_IGNORED; /* Not for us. */

    if (status & UHCI_USBSTS_HSE)
        log_warn("Host system error detected");
    if (status & UHCI_USBSTS_RESUME_DETECT)
        not_implemented("Resume received from device");

    if (status & UHCI_USBSTS_USBINT)
        uhci_process_urbs(uhci);

    /* Schedule has been paused, we can honor uhci_controller_destroy() */
    if (uhci->destroy_pending && status & UHCI_USBSTS_HCHALTED) {
        u32 val;

        /* reset all devices on the */
        val = uhci_inw(uhci, UHCI_USBCMD);
        val |= UHCI_USBCMD_GLOBAL_RESET;
        uhci_outw(uhci, UHCI_USBCMD, val);

        /* FIXME: do not do this inside an interrupt !!! */
        uhci_destroy(uhci);
    }

    /* Clear interrupt. */
    uhci_outw(uhci, UHCI_USBSTS, status);
    return INTERRUPT_HANDLED;
}

/*
 *
 */
static error_t uhci_init_interrupts(struct uhci_controller *uhci)
{
    uhci_outw(uhci, UHCI_USBINTR,
              UHCI_USBINTR_IOC |
              UHCI_USBINTR_RESUME);

    return E_SUCCESS;
}

/*
 * Allocate and initialize the queue-heads used to schedule the different
 * transfer types.
 *
 * @see uhci_qh_prio
 */
static error_t uhci_init_queue_heads(struct uhci_controller *uhci)
{
    struct uhci_qh *qh;
    error_t err;

    err = E_NOMEM;
    for (int i = UHCI_QH_COUNT - 1; i >= 0; --i) {
        struct uhci_td *td;
        paddr_t paddr;

        qh = uhci_qh_new();
        if (!qh)
            goto exit_error;

        /* Link queue heads together. */
        uhci->qhs[i] = qh;
        if (i + 1 >= UHCI_QH_COUNT) {
            qh->link_pointer = UHCI_PTR_TERMINATE;
        } else {
            paddr = mmu_find_physical((vaddr_t)uhci->qhs[i + 1]);
            qh->link_pointer = paddr & UHCI_PTR_LINK_MASK;
            qh->link_pointer |= UHCI_PTR_QH_SELECT;
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

        paddr = mmu_find_physical((vaddr_t)td);
        qh->element_pointer = paddr & UHCI_PTR_LINK_MASK;
        qh->element_pointer |= UHCI_PTR_VF;
    }

    return E_SUCCESS;

exit_error:
    return err;
}

/*
 * Allocate and initialize the controller's framelist.
 *
 * All framelist entries point to the first queue head in order of priority
 * (interrupt transfers).
 *
 * TODO: This should be changed to support different interrupt frequencies.
 */
static error_t uhci_init_framelist(struct uhci_controller *uhci)
{
    paddr_t paddr;

    uhci->frame_list_size = UHCI_FRAMELIST_SIZE;
    uhci->frame_list = kmalloc_dma(uhci->frame_list_size *
                                   sizeof(*uhci->frame_list));
    if (!uhci->frame_list)
        return E_NOMEM;

    /* all frame list entries point to the interrupt queue head. */
    paddr = mmu_find_physical((vaddr_t)uhci->qhs[UHCI_QH_INTERRUPT]);
    for (size_t i = 0; i < uhci->frame_list_size; ++i) {
        uhci->frame_list[i] = paddr & UHCI_PTR_LINK_MASK;
        uhci->frame_list[i] |= UHCI_PTR_QH_SELECT;
    }

    /* Save framelist address for hardware. */
    paddr = mmu_find_physical((vaddr_t)uhci->frame_list);
    uhci_outl(uhci, UHCI_FRBASEADD, paddr);

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

    val = uhci_inw(uhci, UHCI_USBCMD);
}

/*
 * Reset the USB bus connected to an UHCI controller.
 *
 * All connected devices are put back into the Reset state.
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

static inline u16 uhci_port_feature_mask(u16 feature)
{
    switch (feature) {
    case USB_HUB_FEAT_PORT_SUSPEND:
        return UHCI_PORTSC_SUSPEND;
    case USB_HUB_FEAT_PORT_RESET:
        return UHCI_PORTSC_RESET;
    case USB_HUB_FEAT_PORT_ENABLE:
        return UHCI_PORTSC_PED;
    case USB_HUB_FEAT_C_PORT_CONNECTION:
        return UHCI_PORTSC_CSC;
    case USB_HUB_FEAT_C_PORT_ENABLE:
        return UHCI_PORTSC_PEDC;
    }

    return 0;
}

/*
 *
 */
static error_t
uhci_hub_set_port_feature(struct usb_hub *hub, u16 feature, u8 port)
{
    struct uhci_controller *uhci = hub->udev->controller->priv;
    u16 mask;
    u16 val;

    mask = uhci_port_feature_mask(feature);
    if (!mask)
        return E_NOT_SUPPORTED;

    val = uhci_inw(uhci, UHCI_PORTSC(port));
    val |= mask;
    uhci_outw(uhci, UHCI_PORTSC(port), val);

    return E_SUCCESS;
}

/*
 *
 */
static error_t
uhci_hub_clear_port_feature(struct usb_hub *hub, u16 feature, u8 port)
{
    struct uhci_controller *uhci = hub->udev->controller->priv;
    u16 mask;
    u16 val;

    mask = uhci_port_feature_mask(feature);
    if (!mask)
        return E_NOT_SUPPORTED;

    val = uhci_inw(uhci, UHCI_PORTSC(port));
    val &= ~mask;
    uhci_outw(uhci, UHCI_PORTSC(port), val);

    return E_SUCCESS;
}

/*
 *
 */
static error_t
uhci_hub_get_port_status(struct usb_hub *hub, u8 port, u16 *status, u16 *change)
{
    struct uhci_controller *uhci = hub->udev->controller->priv;
    u16 wPortStatus = 0;
    u16 wPortChange = 0;
    u16 val;

    val = uhci_inw(uhci, UHCI_PORTSC(port));

    if (val & UHCI_PORTSC_CCS) wPortStatus |= USB_HUB_STATUS_PORT_CONNECTION;
    if (val & UHCI_PORTSC_PED) wPortStatus |= USB_HUB_STATUS_PORT_ENABLE;
    if (val & UHCI_PORTSC_SUSPEND) wPortStatus |= USB_HUB_STATUS_PORT_SUSPEND;
    if (val & UHCI_PORTSC_RESET) wPortStatus |= USB_HUB_STATUS_PORT_RESET;
    if (val & UHCI_PORTSC_LSDA) wPortStatus |= USB_HUB_STATUS_PORT_LOW_SPEED;

    if (val & UHCI_PORTSC_CSC) wPortChange |= USB_HUB_STATUS_C_PORT_CONNECTION;
    if (val & UHCI_PORTSC_PEDC) wPortChange |= USB_HUB_STATUS_C_PORT_ENABLE;

    if (status)
        *status = wPortStatus;
    if (change)
        *change = wPortChange;

    return E_SUCCESS;
}

/*
 *
 */
static error_t uhci_init_root_hub(struct usb_controller *controller)
{
    struct usb_hub *hub;
    struct usb_device *udev;
    error_t err;

    udev = usb_device_alloc();
    if (!udev)
        return E_NOMEM;

    err = usb_device_init(udev);
    if (err) {
        usb_device_destroy(udev);
        return err;
    }

    device_set_name(&udev->dev, "uhci_root_hub");
    udev->controller = controller;

    hub = kcalloc(1, sizeof(*hub), KMALLOC_KERNEL);
    if (!hub)
        return E_NOMEM;
    controller->root_hub = hub;

    hub->set_port_feature = uhci_hub_set_port_feature;
    hub->clear_port_feature = uhci_hub_clear_port_feature;
    hub->get_port_status = uhci_hub_get_port_status;

    err = usb_hub_init(hub, udev, UHCI_HUB_PORT_COUNT);
    if (err) {
        usb_device_destroy(udev);
        return err;
    }

    return E_SUCCESS;
}

/*
 *
 */
static error_t uhci_controller_init(struct usb_controller *controller)
{
    struct pci_device *pcidev = controller->pcidev;
    struct uhci_controller *uhci;
    error_t err;

    /* uhci_init() failed, UHCI is unusable. */
    if (!kmem_cache_uhci_td)
        return E_NOT_SUPPORTED;

    uhci = kcalloc(1, sizeof(*uhci), KMALLOC_KERNEL);
    if (!uhci)
        return E_NOMEM;

    controller->priv = uhci;
    uhci->io_reg = pci_device_read_config(pcidev, UHCI_PCI_USBBASE,
                                          UHCI_PCI_USBBASE_SIZE);
    uhci->io_reg &= ~1; /* clear i/o mem indicator. */

    uhci_bus_reset(uhci);

    /* TODO: de-activate legacy PS/2 support */

    err = uhci_init_queue_heads(uhci);
    if (err) {
        log_err("failed to init queue heads: %pe", &err);
        goto fail;
    }

    err = uhci_init_framelist(uhci);
    if (err) {
        log_err("failed to init framelist: %pe", &err);
        goto fail;
    }

    err = uhci_init_interrupts(uhci);
    if (err) {
        log_err("failed to init interrupts: %pe", &err);
        goto fail;
    }

    err = uhci_init_root_hub(controller);
    if (err) {
        log_err("failed to initialize the root hub");
        goto fail;
    }

    uhci_enable_transfer(uhci, true);

    return E_SUCCESS;

fail:
    uhci_destroy(uhci);
    return err;
}

/*
 *
 */
static void uhci_controller_destroy(struct usb_controller *controller)
{
    struct uhci_controller *uhci = controller->priv;

    /* wait for the traffic to stop before freeing the QHs and TDs */
    uhci->destroy_pending = true;
    uhci_enable_transfer(uhci, false);
}

struct usb_controller_ops uhci_controller_ops = {
    .init = uhci_controller_init,
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
