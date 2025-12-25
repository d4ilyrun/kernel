#ifndef KERNEL_DEVICES_UHCI_H
#define KERNEL_DEVICES_UHCI_H

#include <kernel/devices/pci.h>

#include <utils/bits.h>

/***/
struct usb_controller *uhci_init_controller(struct pci_device *);

/*
 * UHCI control I/O registers (16-bits).
 */

#define UHCI_USBCMD                 0x00
#define   UHCI_USBCMD_RUN           BIT(0)
#define   UHCI_USBCMD_HC_RESET      BIT(1)
#define   UHCI_USBCMD_GLOBAL_RESET  BIT(2)
#define   UHCI_USBCMD_CF            BIT(6)
#define UHCI_USBSTS                 0x02
#define UHCI_USBINTR                0x04
#define   UHCI_USBINTR_TIMEOUT_CRC  BIT(0)
#define   UHCI_USBINTR_RESUME       BIT(1)
#define   UHCI_USBINTR_COMPLETE     BIT(2)
#define   UHCI_USBINTR_SOHRT_PACKET BIT(3)
#define UHCI_FRNUM                  0x06
#define UHCI_FLBASEADD              0x08
#define UHCI_SOF                    0x0c
#define UHCI_PORTSC1                0x10
#define UHCI_PORTSC2                0x12

#define UHCI_PCI_USBBASE 0x20
#define UHCI_PCI_USBBASE_SIZE sizeof(u32)

/*
 * UHCI data structures.
 */

/**
 * Used by:
 * - Frame list pointer (3.1)
 * - Transfer descriptor link pointer (3.2.1)
 * - Queue head link pointer pointer (3.3.1)
 * - Queue head element link pointer (3.3.2)
 */
#define UHCI_POINTER_TERMINATE
struct uhci_pointer {
    u32 terminate : 1; /* Item contains no valid entry (frame, queue). */
    u32 qh_td_sel : 1; /* Next item (pointer) is a QH if 1, TD if 0. */
    u32 depth_sel : 1; /* Set to 1 to use depth first seach (TD only). */
    u32 reserved : 1;
    u32 pointer : 28;
} PACKED;

/** Transfer Descriptor (3.2). */
struct uhci_td {
    struct uhci_pointer link_pointer;
    u32 status;
    u32 token;
} PACKED;

#endif /* KERNEL_DEVICES_UHCI_H */
