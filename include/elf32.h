/*
 * @file elf32.h
 * @brief Standard ELF32 structures definitions
 *
 * @see https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-46512.html
 *
 * TODO: relocation
 */

#ifndef ELF32_H
#define ELF32_H

#include <stdint.h>

typedef uint16_t elf32_half;
typedef uint32_t elf32_word;
typedef int32_t elf32_sword;
typedef uint32_t elf32_off;
typedef uint32_t elf32_addr;

/** ELF file header */
struct elf32_ehdr {
    uint8_t e_ident[16];
    elf32_half e_type;      ///< Object file type @see e_type
    elf32_half e_machine;   ///< Machine type
    elf32_word e_version;   ///< Must contain EV_CURRENT
    elf32_word e_entry;     ///< Virtual address of the program's entrypoint
    elf32_off e_phoff;      ///< Offset to the program header table
    elf32_off e_shoff;      ///< Offset to the section header table
    elf32_word e_flags;     ///< Processor specific flags
    elf32_half e_ehsize;    ///< The size of this header
    elf32_half e_phentsize; ///< Size of a entry inside the program header table
    elf32_half e_phnum; ///< Number of entries inside the program header table
    elf32_half e_shentsize; ///< Size of a section table entry
    elf32_half e_shnum;     ///< Number of sections
    elf32_half e_shstrndx;  ///< Section header index of the string table for
                            ///< section header names
};

/** Indexes inside e_ident */
typedef enum {
    EI_MAG0 = 0,       // 0x7F
    EI_MAG1 = 1,       // 'E'
    EI_MAG2 = 2,       // 'L'
    EI_MAG3 = 3,       // 'F'
    EI_CLASS = 4,      // Architecture (32/64)
    EI_DATA = 5,       // Byte Order
    EI_VERSION = 6,    // ELF Version
    EI_OSABI = 7,      // OS Specific
    EI_ABIVERSION = 8, // OS Specific
    EI_PAD = 9,        // Padding
    EI_NIDENT = 16,
} e_ident;

/** Available object file types */
typedef enum {
    ET_NONE = 0, ///< Unkown Type
    ET_REL = 1,  ///< Relocatable File
    ET_EXEC = 2, ///< Executable File
    ET_DYN = 3,  ///< Shared object File
    ET_CORE = 4  ///< Core File
} e_type;

/** Values for e_ident[EI_MAGn] */
typedef enum {
    EI_MAG_0 = 0x7f,
    EI_MAG_1 = 'E',
    EI_MAG_2 = 'L',
    EI_MAG_3 = 'F',
} ei_mag;

/** Values for e_ident[EI_CLASS] */
typedef enum {
    EI_CLASS_32 = 1, ///< 32-bit objects
    EI_CLASS_64 = 2, ///< 64-bit objects
} ei_class;

/** Values for e_ident[EI_DATA] */
typedef enum {
    EI_DATA_LSB = 1, ///< File uses little endian
    EI_DATA_MSB = 2, ///< File uses big endian
} ei_data;

/** Available machine types */
typedef enum {
    EM_NONE = 0, ///< No machine
    EM_386 = 3,  ///< Intel 30386
} e_machine;

/***/
typedef enum {
    EV_NONE = 0,    ///< Invalid version
    EV_CURRENT = 1, ///< Current version
} e_version;

/** ELF section header
 *
 *  A table of all section headers is always present inside the ELF file.
 *  This table can be located using its offset from the start of the file,
 *  present inside the ELF header's e_shoff field.
 */
struct elf32_shdr {
    elf32_word sh_name; ///< Offset to this section's name inside the name table
    elf32_word sh_type; ///< Section type
    elf32_word sh_flags; ///< 1-bit flag that describes the section's attributes
    elf32_addr sh_addr;  ///< Offset to the section's location inside memory
    elf32_off sh_offset; ///< Offset to the section's location inside the file
    elf32_word sh_size;  ///< Size of the section
    elf32_word sh_link;
    elf32_word sh_info;
    elf32_word sh_addralign; ///< Alignment requirement for the section's
                             ///< starting address
    elf32_word sh_entsize;
};

/** Special section indexes */
typedef enum {
    SHN_UNDEF = 0, ///< Undefined/Not present
} sh_index;

typedef enum {
    SHT_NULL = 0,     ///< Null section
    SHT_PROGBITS = 1, ///< Program information
    SHT_SYMTAB = 2,   ///< Symbol table
    SHT_STRTAB = 3,   ///< String table
    SHT_RELA = 4,     ///< Relocation (w/ addend)
    SHT_NOBITS = 8,   ///< Not present in file
    SHT_REL = 9,      ///< Relocation (no addend)
} sh_type;

typedef enum {
    SHF_WRITE = 0x1,           ///< Section contains writable data
    SHF_ALLOC = 0x2,           ///< Section is present in memory
    SHF_EXECINSTR = 0x4,       ///< Section contains executable machine code
    SHF_MASKPROC = 0xf0000000, ///< Mask for processor specific semantics
} sh_flags;

/** Symbol table entry
 *
 *  A symbol table is a section with an sh_type of SHT_SYMTAB.
 */
struct elf32_sym {
    elf32_word st_name; ///< Symbol name's offset inside the symbol string table
    elf32_addr st_value;   ///< Can be an absolute value, address, etc (context)
    elf32_word st_size;    ///< Data object's size (if any)
    unsigned char st_info; ///< Symbol's type and binding attributes
    unsigned char st_other; ///< No meaning. Always 0.
    elf32_half st_shndx;    ///< Index of the section containing the symbol
};

#define ELF32_ST_BIND(info) ((info) >> 4)
#define ELF32_ST_TYPE(info) ((info)&0xf)
#define ELF32_ST_INFO(bind, type) (((bind) << 4) | ((type)&0xf))

typedef enum {
    STB_LOCAL = 0,  ///< Invisible outside of the file containing the definition
    STB_GLOBAL = 1, ///< Visible to all object files being combined
    STB_WEAK = 2, ///< Same as global, but may be overwritten by another symbol
                  ///< of the same name
} st_bind;

typedef enum {
    STT_NOTYPE = 0,  ///< Undefined type
    STT_OBJECT = 1,  ///< Data object (variable, array, ...)
    STT_FUNC = 2,    ///< Function or executable code
    STT_SECTION = 3, ///< Section
    STT_FILE = 4,    ///< Source file
} st_type;

/** ELF Program header entry (segment)
 *
 *  An executable or shared object file’s program header table is an array of
 *  structures, each describing a segment or other information the system needs
 *  to prepare the program for execution. An object file segment contains one or
 *  more sections.
 *
 *  Program headers are meaningful only for executable and shared object files.
 *
 *  The size of the program header size is specified in the ELF header's
 *  e_phentsize and e_phnum members (size * num).
 */
struct elf32_phdr {
    elf32_word p_type;   ///< The segment's type
    elf32_off p_offset;  ///< Offset to the segment's first byte in the file
    elf32_addr p_vaddr;  ///< Segment's virtual address in memory
    elf32_addr p_paddr;  ///< Segment's physical address in memory
    elf32_word p_filesz; ///< Size of this segment inside the file
    elf32_word p_memsz;  ///< Size of this segment inside memory
    elf32_word p_pflags; ///< A combination of @ref p_pflags
    elf32_word p_align;  ///< Alignment requirement for the virtual load address
};

/** Available segment types */
typedef enum {
    PT_NULL = 0,    ///< Unused segment
    PT_LOAD = 1,    ///< Loadable in memory
    PT_DYNAMIC = 2, ///< Dynamic linking information
    PT_INTERP = 3,  ///< Path invoked as an interpreter
    PT_NOTE = 4,    ///< Auxiliary information
    PT_SHLIB = 5,   ///< Reserved
    PT_PHDR = 6,    ///< Program header segment
} p_type;

/** Program header permission flags */
typedef enum {
    PF_X = 0x1, ///< Executable segment
    PF_W = 0x2, ///< Writable segment
    PF_R = 0x4, ///< Readable segment
} p_flags;

#endif /* ELF32_H */
