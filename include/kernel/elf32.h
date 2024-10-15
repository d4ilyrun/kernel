/**
 * @see https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-46512.html
 */

#pragma once

#include <kernel/types.h>

typedef uint16_t elf32_half;
typedef uint32_t elf32_word;
typedef int32_t elf32_sword;
typedef uint32_t elf32_off;
typedef uint32_t elf32_addr;

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

/** @brief ELF header */
typedef struct {
    uint8_t e_ident[EI_NIDENT];
    elf32_half e_type;
    elf32_half e_machine;
    elf32_word e_version;
    elf32_word e_entry;
    elf32_off e_phoff;
    elf32_off e_shoff; ///< Offset to the section header table
    elf32_word e_flags;
    elf32_half e_ehsize;
    elf32_half e_phentsize;
    elf32_half e_phnum;
    elf32_half e_shentsize;
    elf32_half e_shnum; ///< Number of sections
    elf32_half e_shstrndx;
} elf32_ehdr;

typedef elf32_ehdr *elf_t;

/** Values for e_ident[EI_MAGn] */
typedef enum {
    EI_MAG_0 = 0x7f,
    EI_MAG_1 = 'E',
    EI_MAG_2 = 'L',
    EI_MAG_3 = 'F',
} ei_mag;

/** Values for e_ident[EI_CLASS] */
typedef enum {
    EI_CLASS_32 = 1,
    EI_CLASS_64 = 2,
} ei_class;

/** Values for e_ident[EI_DATA] */
typedef enum {
    EI_DATA_LSB = 1,
    EI_DATA_MSB = 2,
} ei_data;

typedef enum {
    ET_NONE = 0, ///< Unkown Type
    ET_REL = 1,  ///< Relocatable File
    ET_EXEC = 2, ///< Executable File
    ET_DYN = 3,  ///< Shared object File
    ET_CORE = 4  ///< Core File
} e_type;

typedef enum {
    EM_NONE = 0,
    EM_386 = 3,
} e_machine;

typedef enum {
    EV_NONE = 0,
    EV_CURRENT = 1,
} e_version;

/** @brief ELF section header
 *  The list of all section headers is stored in the section header table.
 */
typedef struct {
    elf32_word sh_name; ///< Offset to the name inside the section name table
    elf32_word sh_type;
    elf32_word sh_flags;
    elf32_addr sh_addr;
    elf32_off sh_offset;
    elf32_word sh_size;
    elf32_word sh_link;
    elf32_word sh_info;
    elf32_word sh_addralign;
    elf32_word sh_entsize;
} elf32_shdr;

#define SHN_UNDEF (0x00) // Undefined/Not present

typedef enum {
    SHT_NULL = 0,     ///< Null section
    SHT_PROGBITS = 1, ///< Program information
    SHT_SYMTAB = 2,   ///< Symbol table
    SHT_STRTAB = 3,   ///< String table
    SHT_RELA = 4,     ///< Relocation (w/ addend)
    SHT_NOBITS = 8,   ///< Not present in file
    SHT_REL = 9,      ///< Relocation (no addend)
} sh_type;

/** Check wether the ELF header is valid. */
bool elf32_check_header(elf_t elf);

/** Check wether the ELF file's format is supported */
bool elf32_check_supported(elf_t elf);

bool elf32_load(elf_t elf);
