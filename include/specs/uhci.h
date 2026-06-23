#ifndef _SPECS_UHCI_H
#define _SPECS_UHCI_H

#include <utils/bits.h>

#define UHCI_PCI_USBBASE 0x20
#define UHCI_PCI_USBBASE_SIZE sizeof(u32)

#define UHCI_HUB_PORT_COUNT 2

/*
 * UHCI control I/O registers (16-bits).
 */

#define UHCI_USBCMD                  0x00
#define   UHCI_USBCMD_RUN            BIT(0)  /* Run/Stop */
#define   UHCI_USBCMD_HC_RESET       BIT(1)  /* Host Controller Reset */
#define   UHCI_USBCMD_GLOBAL_RESET   BIT(2)  /* Global Reset */
#define   UHCI_USBCMD_GLOBAL_SUSPEND BIT(3)  /* Enter Global Suspend */
#define   UHCI_USBCMD_GLOBAL_RESUME  BIT(4)  /* Force Global Resume */
#define   UHCI_USBCMD_SW_DEBUG       BIT(5)
#define   UHCI_USBCMD_CF             BIT(6)  /* Config Flag */
#define   UHCI_USBCMD_MAX_PACKET_64  BIT(7)

#define UHCI_USBSTS                  0x02
#define   UHCI_USBSTS_USBINT         BIT(0)
#define   UHCI_USBSTS_ERROR          BIT(1)
#define   UHCI_USBSTS_RESUME_DETECT  BIT(2)
#define   UHCI_USBSTS_HSE            BIT(3)  /* Host System Error */
#define   UHCI_USBSTS_HCPE           BIT(4)  /* Host Controller Process Error */
#define   UHCI_USBSTS_HCHALTED       BIT(5)

#define UHCI_USBINTR                 0x04
#define   UHCI_USBINTR_TIMEOUT_CRC   BIT(0)
#define   UHCI_USBINTR_RESUME        BIT(1)
#define   UHCI_USBINTR_IOC           BIT(2)
#define   UHCI_USBINTR_SHORT_PACKET  BIT(3)

#define UHCI_FRNUM                   0x06
#define   UHCI_FRNUM_MASK            0x03ff

#define UHCI_FRBASEADD               0x08

#define UHCI_SOF                     0x0c
#define   UHCI_SOF_MASK              0x7f
#define   UHCI_SOF_DEFAULT           64

/* UHCI root hub port status & control */
#define UHCI_PORTSC(n)               (0x10 + (n * 2))
#define   UHCI_PORTSC_CCS            BIT(0)  /* Current Connect Status */
#define   UHCI_PORTSC_CSC            BIT(1)  /* Connect Status Change */
#define   UHCI_PORTSC_PED            BIT(2)  /* Port Enable */
#define   UHCI_PORTSC_PEDC           BIT(3)  /* Port Enable Change */
#define   UHCI_PORTSC_LSDA           BIT(8)  /* Low Speed Device Attached */
#define   UHCI_PORTSC_RESET          BIT(9)
#define   UHCI_PORTSC_SUSPEND        BIT(12)
#define   UHCI_PORTSC_RESUME         BIT(13)

/*
 * UHCI data structures.
 */

#define UHCI_FRAMELIST_SIZE  1024
#define UHCI_FRAMELIST_ALIGN 4096
#define UHCI_QH_ALIGN        16
#define UHCI_TD_ALIGN        16

/*
 * Link pointer format.
 *
 * Used for frame list pointers (3.1), TD link pointers (3.2.1)
 * and queue head link pointers (3.3.1, 3.3.2).
 */
#define UHCI_PTR_TERMINATE   BIT(0)
#define UHCI_PTR_QH_SELECT   BIT(1)
#define UHCI_PTR_VF          BIT(2)
#define UHCI_PTR_LINK_OFFSET 4
#define UHCI_PTR_LINK_MASK   0xFFFFFFF0

/*
 * Transfer descriptor status.
 */
#define UHCI_TD_STATUS_ACTLEN_MASK   (0x7FF << 0)
#define UHCI_TD_STATUS_ACTLEN_OFFSET 0
#define UHCI_TD_STATUS_BITSTUFF      BIT(17) /* Bitstuff error */
#define UHCI_TD_STATUS_CRC           BIT(18) /* CRC error */
#define UHCI_TD_STATUS_NAK           BIT(19) /* NAK received */
#define UHCI_TD_STATUS_BABBLE        BIT(20) /* Babble detected */
#define UHCI_TD_STATUS_DATABUF       BIT(21) /* Data buffer error */
#define UHCI_TD_STATUS_STALL         BIT(22) /* Stall detected */
#define UHCI_TD_STATUS_ACTIVE        BIT(23) /* Active */
#define UHCI_TD_STATUS_IOC           BIT(24) /* Interrupt on complete */
#define UHCI_TD_STATUS_ISO           BIT(25) /* Isochronous transfer */
#define UHCI_TD_STATUS_LS            BIT(26) /* Low speed device */
#define UHCI_TD_STATUS_MAXERR_MASK   (0x3 << 27)
#define UHCI_TD_STATUS_MAXERR_OFFSET 27
#define UHCI_TD_STATUS_SPD           BIT(29) /* Short packet detect */

/*
 * Transfer descriptor token.
 */
#define UHCI_TD_TOKEN_PID_MASK      (0xFF << 0)
#define UHCI_TD_TOKEN_PID_OFFSET    0
#define UHCI_TD_TOKEN_ADDR_MASK     (0x7F << 8)
#define UHCI_TD_TOKEN_ADDR_OFFSET   8
#define UHCI_TD_TOKEN_EP_MASK       (0xF << 15)
#define UHCI_TD_TOKEN_EP_OFFSET     15
#define UHCI_TD_TOKEN_TOGGLE        BIT(19)
#define UHCI_TD_TOKEN_MAXLEN_MASK   (0x7FF << 21)
#define UHCI_TD_TOKEN_MAXLEN_OFFSET 21

#endif /* _SPECS_UHCI_H */
