#define LOG_DOMAIN "elf"

#include <kernel/elf32.h>
#include <kernel/error.h>
#include <kernel/logger.h>
#include <kernel/memory.h>

#include <utils/macro.h>
#include <utils/math.h>

#include <stddef.h>

static void *elf32_at_offset(struct elf32_ehdr *elf, elf32_off offset)
{
    return (void *)elf + offset;
}

static const struct elf32_shdr *
elf32_section(struct elf32_ehdr *elf, size_t index)
{
    struct elf32_shdr *shdr_table;

    if (index >= elf->e_shnum)
        return PTR_ERR(E_INVAL);

    shdr_table = elf32_at_offset(elf, elf->e_shoff);

    return &shdr_table[index];
}

static const char *
elf32_section_name(struct elf32_ehdr *elf, struct elf32_shdr *section)
{
    const struct elf32_shdr *name_table;

    if (elf->e_shstrndx == SHN_UNDEF)
        return NULL;

    name_table = elf32_section(elf, elf->e_shstrndx);
    return elf32_at_offset(elf, name_table->sh_offset + section->sh_name);
}

bool el32_is_elf(void *data)
{
    struct elf32_ehdr *hdr = data;

    /* Check for the presence of magic values inside the ELF header */
    return hdr->e_ident[EI_MAG0] != EI_MAG_0 ||
           hdr->e_ident[EI_MAG1] != EI_MAG_1 ||
           hdr->e_ident[EI_MAG2] != EI_MAG_2 ||
           hdr->e_ident[EI_MAG3] != EI_MAG_3;
}

bool elf32_is_loadable(struct elf32_ehdr *elf)
{
    if (elf->e_machine != EM_386) {
        log_warn("unsupported machine type: %d", elf->e_machine);
        return false;
    }

    if (elf->e_version != EV_CURRENT) {
        log_warn("unsupported version: %d", elf->e_version);
        return false;
    }

    if (elf->e_type != ET_REL && elf->e_type != ET_EXEC) {
        log_warn("unsuppored type: %d", elf->e_type);
        return false;
    }

    return true;
}

static const struct elf32_phdr *elf32_program_header(struct elf32_ehdr *elf)
{
    return elf32_at_offset(elf, elf->e_phoff);
}

static elf32_word elf32_base_address(struct elf32_ehdr *elf)
{
    const struct elf32_phdr *phdr = elf32_program_header(elf);
    elf32_word base_address = 0;

    /* Determine the lowest p_vaddr value for a PT_LOAD segment */
    for (size_t i = 0; i < elf->e_phnum; ++i) {
        if (phdr[i].p_type == PT_LOAD && phdr[i].p_vaddr > base_address)
            base_address = phdr[i].p_vaddr;
    }

    /* Truncate the memory address to the nearest multiple of the page size */
    return align_down(base_address, PAGE_SIZE);
}

static void
elf32_load_section(struct elf32_ehdr *elf, struct elf32_shdr *section)
{
    log_info("section: %s", elf32_section_name(elf, section));
}

bool elf32_load(struct elf32_ehdr *elf)
{
    return true;
}
