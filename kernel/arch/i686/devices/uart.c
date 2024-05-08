/* Interactions with the UART 16550.
 *
 * The UART is connected through COM1, at 38400bps.
 *
 * These functions follow the libc's equivalent functions'
 * specifications.
 */

#include <kernel/cpu.h>
#include <kernel/devices/uart.h>

#include <utils/bits.h>
#include <utils/compiler.h>
#include <utils/macro.h>

#include <stdint.h>

#define UART_BAUDRATE 38400
#define UART_CLOCK_HZ 115200 /* UART clock at 115.2KHz */

#define UART_COM1_PORT 0x03F8
#define UART_COM2_PORT 0x02F8
#define UART_COM3_PORT 0x03E8
#define UART_COM4_PORT 0x02E8

#ifndef UART_PORT
#define UART_PORT UART_COM1_PORT
#endif

/* Register offsets from base address */
#define THR 0 /* Transmitter Holding Buffer */
#define RBR 0 /* Reciver Buffer */
#define DLL 0 /* Divisor Latch Low Byte */
#define IER 1 /* Interrupt Enable */
#define DLH 1 /* Divisor Latch High */
#define IIR 2 /* Interrupt Identification */
#define FCR 2 /* FIFO Control */
#define LCR 3 /* Line Control */
#define MCR 4 /* Modem control */
#define LSR 5 /* Line Status */
#define MSR 6 /* Modem Status */
#define SR 7  /* Scratch */

/* Register addres from offset */
#define UART_REG(_reg) ((UART_PORT) + (_reg))

static ALWAYS_INLINE uint16_t uart_div_latch_value(const uint16_t baudrate)
{
    return (UART_CLOCK_HZ / baudrate);
}

void uart_reset()
{
    /* Clear interrupts */
    outb(UART_REG(IER), 0x00);

    /* Set baudrate */
    const uint16_t div_latch = uart_div_latch_value(UART_BAUDRATE);
    outb(UART_REG(LCR), BIT_SET(0x00, 7)); // temporary div_latch access
    outb(UART_REG(DLH), MSB(div_latch));
    outb(UART_REG(DLL), LSB(div_latch));

    /* Turn off div_latch access, set default LineControl values:
     * 8bits, no parity, one stop bit
     */
    outb(UART_REG(LCR), 0x03);

    /* Clear and enable FIFOs (interrupt triggered when 14B inside buffer) */
    outb(UART_REG(FCR), 0xC7);
    outb(UART_REG(IER), 0x01);
}

int uart_putc(const char c)
{
    /* Wait until transfer buffer is empty */
    WAIT_FOR(BIT_READ(inb(UART_REG(LSR)), 5));

    outb(UART_REG(THR), c);
    return 0;
}

int uart_write(const char *buf, size_t length)
{
    for (size_t i = 0; i < length; i++)
        uart_putc(buf[i]);
    return length;
}

char uart_getc()
{
    /* Wait until data is available to be read */
    WAIT_FOR(BIT_READ(inb(UART_REG(LSR)), 0));

    return inb(UART_REG(THR));
}

size_t uart_read(char *buf, size_t length)
{
    for (size_t i = 0; i < length; i++)
        buf[i] = uart_getc();
    return length;
}
