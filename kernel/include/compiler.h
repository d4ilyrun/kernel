#ifndef COMPILER_H
#define COMPILER_H

#define ASM __asm__ volatile

/* Clear the nth bit */
#define BIT_MASK(_x, _n) ((_x) & ~(1 << (_n)))

/* Set the nth bit */
#define BIT_SET(_x, _n) ((_x) | (1 << (_n)))

#endif /* COMPILER_H */
