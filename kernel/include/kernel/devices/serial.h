#ifndef KERNEL_DEVICES_SERIAL_H
#define KERNEL_DEVICES_SERIAL_H

#include <stdint.h>
#include <utils/compiler.h>

/* Write a single byte at a given I/O port address. */
static inline void outb(uint16_t port, uint8_t val)
{
    ASM("out %0,%1" : : "a"(val), "Nd"(port) : "memory");
}

/* Write 2 bytes at a given I/O port address. */
static inline void outw(uint16_t port, uint16_t val)
{
    ASM("out %0,%1" : : "a"(val), "Nd"(port) : "memory");
}

/* Write 4 bytes at a given I/O port address. */
static inline void outl(uint16_t port, uint32_t val)
{
    ASM("out %0,%1" : : "a"(val), "Nd"(port) : "memory");
}

/* Read a single byte from a given I/O port address. */
static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    ASM("in %1, %0" : "=a"(val) : "Nd"(port) : "memory");
    return val;
}

/* Read 2 bytes from a given I/O port address. */
static inline uint16_t inw(uint16_t port)
{
    uint16_t val;
    ASM("in %1, %0" : "=a"(val) : "Nd"(port) : "memory");
    return val;
}

/* Read 4 bytes from a given I/O port address. */
static inline uint32_t inl(uint16_t port)
{
    uint32_t val;
    ASM("in %1,%0" : "=a"(val) : "Nd"(port) : "memory");
    return val;
}

#endif /* KERNEL_DEVICES_SERIAL_H */
