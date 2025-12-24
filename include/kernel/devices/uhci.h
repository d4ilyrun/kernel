#ifndef KERNEL_DEVICES_UHCI_H
#define KERNEL_DEVICES_UHCI_H

#include <kernel/devices/pci.h>

#include <utils/bits.h>

/***/
struct usb_controller *uhci_controller_new(struct pci_device *);

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
#define   UHCI_USBINTR_IOC          BIT(2)
#define   UHCI_USBINTR_SHORT_PACKET BIT(3)
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

#define UHCI_FRAMELIST_SIZE 1024

/*
 * Link pointer format.
 *
 * Used for frame list pointers (3.1), TD link pointers (3.2.1)
 * and queue head link pointers (3.3.1, 3.3.2).
 */
#define UHCI_PTR_TERMINATE BIT(0)
#define UHCI_PTR_QH_SELECT BIT(1)
#define UHCI_PTR_VF BIT(2)
#define UHCI_PTR_LINK_OFFSET 4
#define UHCI_PTR_LINK_MASK 0x0FFFFFFF

/*
 * Transfer descriptor status.
 */
#define UHCI_TD_STATUS_ACTLEN_OFFSET 0
#define UHCI_TD_STATUS_ACTLEN_MASK 0x7FF
#define UHCI_TD_STATUS_BITSTUFF BIT(17) /* Bitstuff error */
#define UHCI_TD_STATUS_CRC BIT(18) /* CRC error */
#define UHCI_TD_STATUS_NAK BIT(19) /* NAK received */
#define UHCI_TD_STATUS_BABBLE BIT(20) /* Babble detected */
#define UHCI_TD_STATUS_DATABUF BIT(21) /* Data buffer error */
#define UHCI_TD_STATUS_STALL BIT(22) /* Stall detected */
#define UHCI_TD_STATUS_ACTIVE BIT(23) /* Active */
#define UHCI_TD_STATUS_IOC BIT(24) /* Interrupt on complete */
#define UHCI_TD_STATUS_ISO BIT(25) /* Isochronous transfer */
#define UHCI_TD_STATUS_LS BIT(26)  /* Low speed device */
#define UHCI_TD_STATUS_MAXERR_OFFSET 27
#define UHCI_TD_STATUS_MAXERR_MASK 0x3
#define UHCI_TD_STATUS_SPD BIT(29) /* Short packet detect */

/*
 * Transfer descriptor token.
 */
#define UHCI_TD_TOKEN_MAXLEN_OFFSET 21
#define UHCI_TD_TOKEN_MAXLEN_MASK 0x7FF
#define UHCI_TD_TOKEN_TOGGLE BIT(19)
#define UHCI_TD_TOKEN_EP_MASK 0xF
#define UHCI_TD_TOKEN_EP_OFFSET 15
#define UHCI_TD_TOKEN_ADDR_MASK 0x7F
#define UHCI_TD_TOKEN_ADDR_OFFSET 8
#define UHCI_TD_TOKEN_PID_MASK 0xFF
#define UHCI_TD_TOKEN_PID_OFFSET 0

#endif /* KERNEL_DEVICES_UHCI_H */
