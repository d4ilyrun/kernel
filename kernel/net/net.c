#include <kernel/net.h>

/** Reference implementation of the Internet checksum algorithm */
u16 net_internet_checksum(const u16 *addr, size_t size)
{
    u32 sum = 0;

    while (size > sizeof(u8)) {
        sum += *addr++;
        size -= sizeof(u16);
    }

    /*  Add left-over byte, if any */
    if (size > 0)
        sum += *(const u8 *)addr;

    /*  Fold 32-bit sum to 16 bits */
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    return ~sum;
}
