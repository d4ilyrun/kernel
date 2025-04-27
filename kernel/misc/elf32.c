#define LOG_DOMAIN "elf"

#include <kernel/elf32.h>
#include <kernel/error.h>
#include <kernel/execfmt.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/memory.h>
#include <kernel/mmu.h>
#include <kernel/vmm.h>

#include <utils/macro.h>
#include <utils/math.h>

#include <stddef.h>
#include <string.h>

static inline void *
elf32_at_offset(const struct elf32_ehdr *elf, elf32_off offset)
{
    return (void *)elf + offset;
}

static inline const struct elf32_phdr *
elf32_program_header(const struct elf32_ehdr *elf)
{
    return elf32_at_offset(elf, elf->e_phoff);
}

static inline const struct elf32_shdr *
elf32_section_table(const struct elf32_ehdr *elf)
{
    return elf32_at_offset(elf, elf->e_shoff);
}

static const struct elf32_shdr *
elf32_section(const struct elf32_ehdr *elf, size_t index)
{
    const struct elf32_shdr *shdr_table;

    if (index >= elf->e_shnum)
        return PTR_ERR(E_INVAL);

    shdr_table = elf32_section_table(elf);

    return &shdr_table[index];
}

static inline const char *elf32_section_name(const struct elf32_ehdr *elf,
                                             const struct elf32_shdr *section)
{
    const struct elf32_shdr *name_table;

    if (elf->e_shstrndx == SHN_UNDEF)
        return NULL;

    name_table = elf32_section(elf, elf->e_shstrndx);

    return elf32_at_offset(elf, name_table->sh_offset + section->sh_name);
}

static inline const char *
elf32_symbol_name(const struct elf32_ehdr *elf,
                  const struct elf32_shdr *symbol_table,
                  const struct elf32_sym *symbol)
{
    const struct elf32_shdr *string_table;

    string_table = elf32_section(elf, symbol_table->sh_link);

    return elf32_at_offset(elf, string_table->sh_offset + symbol->st_name);
}

static bool is_elf32(const void *data)
{
    const struct elf32_ehdr *hdr = data;

    /* Check for the presence of magic values inside the ELF header */
    return hdr->e_ident[EI_MAG0] == EI_MAG_0 &&
           hdr->e_ident[EI_MAG1] == EI_MAG_1 &&
           hdr->e_ident[EI_MAG2] == EI_MAG_2 &&
           hdr->e_ident[EI_MAG3] == EI_MAG_3;
}

bool elf32_is_loadable(const struct elf32_ehdr *elf)
{
    if (elf->e_machine != EM_386) {
        log_warn("unsupported machine type: %d", elf->e_machine);
        return false;
    }

    if (elf->e_version != EV_CURRENT) {
        log_warn("unsupported version: %d", elf->e_version);
        return false;
    }

    if (elf->e_type != ET_EXEC) {
        log_warn("unsuppored type: %d", elf->e_type);
        return false;
    }

    return true;
}

MAYBE_UNUSED static elf32_word elf32_base_address(const struct elf32_ehdr *elf)
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

static error_t elf32_load_segment(const struct elf32_ehdr *elf,
                                  const struct elf32_phdr *segment)
{
    void *in_memory;
    void *allocated;
    void *in_file;
    size_t size;
    int prot = 0;

    if (segment->p_type != PT_LOAD)
        return E_SUCCESS;

    /* Values 0 and 1 mean no alignment is required */
    if (segment->p_align > 1) {
        if ((segment->p_vaddr % segment->p_align) !=
            (segment->p_offset % segment->p_align)) {
            log_dbg("p_vaddr and p_offset should be congruent modulo p_align "
                    "(%x === %x %% %x)",
                    segment->p_vaddr, segment->p_offset, segment->p_align);
            return E_INVAL;
        }
    }

    if (segment->p_memsz < segment->p_filesz) {
        log_dbg("size in memory inferior to size in file (%d < %d)",
                segment->p_memsz, segment->p_filesz);
        return E_INVAL;
    }

    if (segment->p_pflags & PF_R)
        prot |= PROT_READ;
    if (segment->p_pflags & PF_W)
        prot |= PROT_WRITE;
    if (segment->p_pflags & PF_X)
        prot |= PROT_EXEC;

    /* NOTE: We also alocate the start of the containing page when vaddr is not
     * aligned. This is necessary because mmap will automatically align up the
     * start address to the next page. We do not verify, though, whether another
     * segment has already allocated the current page, but I don't know if this
     * is even allowed. If returning with E_NOMEM one day, this may be the
     * cause, so check the segment boundaries first I guess ...
     */

    in_memory = (void *)align_down(segment->p_vaddr, PAGE_SIZE);
    size = segment->p_memsz + (segment->p_vaddr % PAGE_SIZE);
    size = align_up(size, PAGE_SIZE);

    log_dbg("allocating segment @ %p (size=%#04x, start=%p, alloc_size=%#04lx, flags=%x)",
            (void *)segment->p_vaddr, segment->p_memsz, in_memory, size, prot);

    /* The file must be mapped 1:1 any other
     * TODO: MAP_FIXED
     */
    allocated = mmap(in_memory, size, prot, 0);
    if (allocated != in_memory) {
        munmap(allocated, size);
        return E_NOMEM;
    }

    in_file = elf32_at_offset(elf, segment->p_offset);
    memcpy((void *)segment->p_vaddr, in_file, segment->p_filesz);

    return E_SUCCESS;
}

static error_t
elf32_load_symbol(const struct elf32_ehdr *elf, const struct elf32_sym *symbol)
{
    UNUSED(elf);
    UNUSED(symbol);

    /* TODO: Support relocatable ELF files */
    return E_SUCCESS;
}

static error_t elf32_load_symbol_section(const struct elf32_ehdr *elf,
                                         const struct elf32_shdr *section)
{
    struct elf32_sym *symbol_table;
    size_t symbol_count;
    error_t err;

    symbol_table = elf32_at_offset(elf, section->sh_offset);
    if (IS_ERR(symbol_table))
        return ERR_FROM_PTR(symbol_table);

    symbol_count = section->sh_size / section->sh_entsize;

    /* Entry 0 is reserved and contains no symbol */
    for (size_t i = 1; i < symbol_count; ++i) {
        err = elf32_load_symbol(elf, &symbol_table[i]);
        if (err)
            log_warn("failed to load symbol '%s': %s",
                     elf32_symbol_name(elf, section, &symbol_table[i]),
                     err_to_str(err));
    }

    return E_SUCCESS;
}

static error_t elf32_load_section(const struct elf32_ehdr *elf,
                                  const struct elf32_shdr *section)
{
    void *start = (void *)section->sh_addr;

    if (section->sh_type == SHT_NULL)
        return E_SUCCESS;

    log_dbg("loading section %s @ %p", elf32_section_name(elf, section), start);

    if (section->sh_type == SHT_SYMTAB)
        return elf32_load_symbol_section(elf, section);

    if (!(section->sh_flags & SHF_ALLOC))
        return E_SUCCESS;

    /* Values 0 and 1 mean the section has no alignment constraints */
    if (section->sh_addralign > 1) {
        if (!is_aligned(section->sh_addr, section->sh_addralign)) {
            log_dbg("invalid alignment (%d %% %d)", section->sh_addr,
                    section->sh_addralign);
            return E_INVAL;
        }
    }

    switch (section->sh_type) {
    case SHT_PROGBITS:
        memcpy(start, elf32_at_offset(elf, section->sh_offset),
               section->sh_size);
        break;
    case SHT_NOBITS:
        memset(start, 0, section->sh_size);
        break;
    default:
        return E_SUCCESS;
    }

    return E_SUCCESS;
}

static error_t elf32_load(struct executable *executable, void *data)
{
    const struct elf32_ehdr *elf = data;
    const struct elf32_phdr *phdr = elf32_program_header(elf);
    const struct elf32_shdr *shdr_table = elf32_section_table(elf);
    error_t err;

    if (!elf32_is_loadable(elf))
        return E_INVAL;

    executable->entrypoint = (void *)elf->e_entry;

    for (size_t i = 0; i < elf->e_phnum; ++i) {
        err = elf32_load_segment(elf, &phdr[i]);
        if (err) {
            log_dbg("failed to load segment %ld: %s", i, err_to_str(err));
            return err;
        }
    }

    for (size_t i = 0; i < elf->e_shnum; ++i) {
        err = elf32_load_section(elf, &shdr_table[i]);
        if (err) {
            log_dbg("failed to load section '%s': %s",
                    elf32_section_name(elf, &shdr_table[i]), err_to_str(err));
            return err;
        }
    }

    return E_SUCCESS;
}

static bool elf32_match(const void *data)
{
    return is_elf32(data);
}

static struct execfmt elf32_execfmt = {
    .name = "ELF32",
    .load = elf32_load,
    .match = elf32_match,
    /* TODO: Unallocate allocated memory chunks */
};

error_t elf32_init(void)
{
    return execfmt_register(&elf32_execfmt);
}
