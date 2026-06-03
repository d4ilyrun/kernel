#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

#define PROT_NONE     0x0
#define PROT_EXEC     0x1
#define PROT_READ     0x2
#define PROT_WRITE    0x4

#ifdef KERNEL
#define PROT_KERNEL 0x8
#define PROT_MASK   0xf
#else
#define PROT_MASK   0x7
#endif

#endif /* _SYS_MMAN_H */
