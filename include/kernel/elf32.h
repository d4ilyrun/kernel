/**
 * @defgroup kernel_binfmt_elf Executable and Linkable Format (ELF)
 * @ingroup kernel_execfmt
 * @{
 */
#ifndef KERNEL_ELF32_H
#define KERNEL_ELF32_H

#include <kernel/error.h>
#include <kernel/types.h>

#include <elf32.h>

error_t elf32_init(void);

#endif /* KERNEL_ELF32_H */

/** @} */
