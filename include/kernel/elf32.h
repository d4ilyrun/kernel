#ifndef KERNEL_ELF32_H
#define KERNEL_ELF32_H

#include <kernel/types.h>

#include <elf32.h>

/** Check wheter a data block is the content of an ELF file */
bool el32_is_elf(void *);

/** Check wether an ELF file can be loaded */
bool elf32_is_loadable(struct elf32_ehdr *elf);

/** Load an elf file in memory */
bool elf32_load(struct elf32_ehdr *elf);

#endif /* KERNEL_ELF32_H */
