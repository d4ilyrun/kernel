#include <kernel/elf32.h>
#include <kernel/error.h>
#include <kernel/logger.h>

#include <stddef.h>

bool elf32_check_header(elf_t elf)
{
    if (elf->e_ident[EI_MAG0] != EI_MAG0 || elf->e_ident[EI_MAG1] != EI_MAG1 ||
        elf->e_ident[EI_MAG2] != EI_MAG2 || elf->e_ident[EI_MAG3] != EI_MAG3) {
        log_err("elf", "invalid E_IDENT magic");
        return false;
    }

    return true;
}

bool elf32_check_supported(elf_t elf)
{
    if (elf->e_machine != EM_386) {
        log_err("elf", "unsupported machine type: %d", elf->e_machine);
        return false;
    }

    if (elf->e_version != EV_CURRENT) {
        log_err("elf", "unsupported version: %d", elf->e_version);
        return false;
    }

    if (elf->e_type != ET_REL && elf->e_type != ET_EXEC) {
        log_err("elf", "unsuppored type: %d", elf->e_type);
        return false;
    }

    return true;
}

static elf32_shdr *elf32_get_section(elf_t elf, size_t index)
{
    elf32_shdr *shdr_table;

    if (index >= elf->e_shnum) {
        return PTR_ERR(E_INVAL);
    }

    shdr_table = (void *)elf + elf->e_shoff;

    return &shdr_table[index];
}

static const char *elf32_section_name(elf_t elf, elf32_shdr *section)
{
    char *section_name_table;

    if (elf->e_shstrndx == SHN_UNDEF)
        return NULL;

    section_name_table =
        (void *)elf + elf32_get_section(elf, elf->e_shstrndx)->sh_offset;

    return section_name_table + section->sh_name;
}

bool elf32_load(elf_t elf)
{
    elf32_shdr *shdr;

    for (size_t i = 0; i < elf->e_shnum; ++i) {
        shdr = elf32_get_section(elf, i);
        if (!IS_ERR(shdr)) {
            log_info("elf", "section: %s", elf32_section_name(elf, shdr));
        }
    }

    return true;
}
