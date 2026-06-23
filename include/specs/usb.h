#ifndef _SPECS_USB_H
#define _SPECS_USB_H

#include <utils/compiler.h>

#include <stdint.h>

/*
 * USB setup packet's format.
 * @see USB 2.0 - 9.3
 */
struct usb_setup {
    uint8_t bRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} PACKED;

/* 5.5.3 - A setup packet is always eight bytes. */
#define USB_SETUP_PACKET_SIZE 8
static_assert(sizeof(struct usb_setup) == USB_SETUP_PACKET_SIZE);

/* Address used by a USB device when it is first powered or reset. */
#define USB_DEFAULT_ADDRESS 0

/* Maximum number of devices on a single bus. */
#define USB_MAX_DEVICE 127
#define USB_MAX_ADDRESS 127

/*
 * Format of a setup packet's bmRequestType field.
 */

/* Data transfer direction */
#define USB_SETUP_REQ_TYPE_TO_DEVICE 0
#define USB_SETUP_REQ_TYPE_TO_HOST   BIT(7)
/* Request type */
#define USB_SETUP_REQ_TYPE_STANDARD (0 << 5)
#define USB_SETUP_REQ_TYPE_CLASS    (1 << 5)
#define USB_SETUP_REQ_TYPE_VENDOR   (2 << 5)
/* Recipient */
#define USB_SETUP_REQ_TYPE_FOR_DEVICE (0 << 0)
#define USB_SETUP_REQ_TYPE_FOR_IFACE  (1 << 0)
#define USB_SETUP_REQ_TYPE_FOR_EP     (2 << 0)
#define USB_SETUP_REQ_TYPE_FOR_PORT   (3 << 0) // Used by HUB specific requests.

/*
 * USB Packet identifiers.
 *
 *  @see USB 2.0 - 8.3.1.
 */
enum usb_pid {
    // Token
    USB_PID_OUT = 0b0001,
    USB_PID_IN = 0b1001,
    USB_PID_SOF = 0b0101,
    USB_PID_SETUP = 0b1101,
    // Data
    USB_PID_DATA0 = 0b0011,
    USB_PID_DATA1 = 0b1011,
    USB_PID_DATA2 = 0b0111,
    USB_PID_MDATA = 0b1111,
    // Handshake
    USB_PID_ACK = 0b0010,
    USB_PID_NAK = 0b1010,
    USB_PID_STALL = 0b1110,
    USB_PID_NYET = 0b0110,
    // Special
    USB_PID_PRE = 0b1100,
    USB_PID_ERR = 0b1100,
    USB_PID_SPLIT = 0b1000,
    USB_PID_PING = 0b0100,
    USB_PID_RESERVED = 0b0000,
};

/* Compute the whole 8b PID field value as described in USB 2.0 - 8.3.1. */
static inline uint8_t usb_pid_field(enum usb_pid pid)
{
    return (~pid << 4) | (pid & 0xf);
}

/*
 * USB device requests.
 *
 * @see USB 2.0 - 9.4
 */

/*
 * Standard USB request codes.
 */
#define USB_REQUEST_GET_STATUS        0
#define USB_REQUEST_CLEAR_FEATURE     1
#define USB_REQUEST_SET_FEATURE       3
#define USB_REQUEST_SET_ADDRESS       5
#define USB_REQUEST_GET_DESCRIPTOR    6
#define USB_REQUEST_SET_DESCRIPTOR    7
#define USB_REQUEST_GET_CONFIGURATION 8
#define USB_REQUEST_SET_CONFIGURATION 9
#define USB_REQUEST_GET_INTERFACE     10
#define USB_REQUEST_SET_INTERFACE     11
#define USB_REQUEST_SYNCH_FRAME       12

/*
 * USB Descriptor types.
 */
#define USB_DESCRIPTOR_DEVICE           1
#define USB_DESCRIPTOR_CONFIGURATION    2
#define USB_DESCRIPTOR_STRING           3
#define USB_DESCRIPTOR_INTERFACE        4
#define USB_DESCRIPTOR_ENDPOINT         5
#define USB_DESCRIPTOR_DEVICE_QUALIFIER 6
#define USB_DESCRIPTOR_OTHER_SPEED_CFG  7
#define USB_DESCRIPTOR_INTERFACE_POWER  8

/*
 * Standard Feature Selectors
 * @see USB 2.0 - table 9-6
 */
#define USB_FEAT_DEVICE_REMOTE_WAKEUP 1
#define USB_FEAT_ENDPOINT_HALT 0
#define USB_FEAT_TEST_MODE 2

/*
 * Hub Class Feature Selectors
 * @see USB 2.0 - table 11-17
 */
#define USB_HUB_FEAT_C_HUB_LOCAL_POWER   0
#define USB_HUB_FEAT_C_HUB_OVER_CURRENT  1
#define USB_HUB_FEAT_PORT_CONNECTION     0
#define USB_HUB_FEAT_PORT_ENABLE         1
#define USB_HUB_FEAT_PORT_SUSPEND        2
#define USB_HUB_FEAT_PORT_OVER_CURRENT   3
#define USB_HUB_FEAT_PORT_RESET          4
#define USB_HUB_FEAT_PORT_POWER          8
#define USB_HUB_FEAT_PORT_LOW_SPEED      9
#define USB_HUB_FEAT_C_PORT_CONNECTION   16
#define USB_HUB_FEAT_C_PORT_ENABLE       17
#define USB_HUB_FEAT_C_PORT_SUSPEND      18
#define USB_HUB_FEAT_C_PORT_OVER_CURRENT 19
#define USB_HUB_FEAT_C_PORT_RESET        20
#define USB_HUB_FEAT_PORT_TEST           21
#define USB_HUB_FEAT_PORT_INDICATOR      22

/*
 * Port Status Field (wPortStatus)
 * @see USB 2.0 - table 11-21
 */
#define USB_HUB_STATUS_PORT_CONNECTION   BIT(0)
#define USB_HUB_STATUS_PORT_ENABLE       BIT(1)
#define USB_HUB_STATUS_PORT_SUSPEND      BIT(2)
#define USB_HUB_STATUS_PORT_OVER_CURRENT BIT(3)
#define USB_HUB_STATUS_PORT_RESET        BIT(4)
#define USB_HUB_STATUS_PORT_POWER        BIT(8)
#define USB_HUB_STATUS_PORT_LOW_SPEED    BIT(9)
#define USB_HUB_STATUS_PORT_HIGH_SPEED   BIT(10)
#define USB_HUB_STATUS_PORT_TEST         BIT(11)
#define USB_HUB_STATUS_PORT_INDICATOR    BIT(12)

/*
 * Port Change Field (wPortChange)
 * @see USB 2.0 - table 11-22
 */
#define USB_HUB_STATUS_C_PORT_CONNECTION   BIT(0)
#define USB_HUB_STATUS_C_PORT_ENABLE       BIT(1)
#define USB_HUB_STATUS_C_PORT_SUSPEND      BIT(2)
#define USB_HUB_STATUS_C_PORT_OVER_CURRENT BIT(3)
#define USB_HUB_STATUS_C_PORT_RESET        BIT(4)

/*
 * Standard USB Device Descriptor.
 * @see USB 2.0 - table 9-8
 */
struct usb_device_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize;
    uint16_t idVendor;
    uint16_t idProduct;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
};

/*
 * @see USB 2.0 - 9.6.6
 */
#define USB_ENDPOINT_MAX 16
#define USB_ENDPOINT_OUT 0x00
#define USB_ENDPOINT_IN  0x80

#define USB_CONTROL_ENDPOINT 0

#endif // !_SPECS_USB_H
