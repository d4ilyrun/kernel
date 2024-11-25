arch = target_machine.cpu()

kernel_compile_flags = [
  f'-DARCH=@arch@',
  f'-DUACPI_FORMATTED_LOGGING',
]

kernel_source_files = files([
  'main.c',
  'fs/vfs.c',
  'fs/tar.c',
  'sys/device.c',
  'sys/sched.c',
  'sys/syscalls.c',
  'sys/process.c',
  'sys/pci.c',
  'misc/logger.c',
  'misc/symbols.c',
  'misc/error.c',
  'memory/pmm.c',
  'memory/vmm.c',
  'net/packet.c',
  'memory/kmalloc.c',
  'devices/driver.c',
  'devices/uacpi.c',
  'devices/acpi.c',
  'devices/pci.c',
  'devices/rtl8139.c',
])

arch_source_files = {
  'i686': files([
    'arch/i686/gdt.S',
    'arch/i686/gdt.c',
    'arch/i686/interrupts.c',
    'arch/i686/process.c',
    'arch/i686/process.S',
    'arch/i686/syscalls.c',
    'arch/i686/mmu.c',
    'arch/i686/setup.c',
    'arch/i686/terminal.c',
    'arch/i686/panic.c',
    'arch/i686/devices/pic.c',
    'arch/i686/devices/pit.c',
    'arch/i686/devices/timer.c',
    'arch/i686/devices/uart.c',
  ]),
}

arch_nasm_files = {
  'i686': files([
    'arch/i686/interrupts.S',
  ]),
}

uacpi_source_files = uacpi.get_variable('sources')

kernel_linker_script = 'linker.ld'
kernel_linker_flags = [ '-T', meson.project_source_root() / 'kernel' / kernel_linker_script ]

kernel_headers = include_directories('../include')
uacpi_headers = uacpi.get_variable('includes')

kernel = executable(
  'kernel.bin',
  [
    f'arch/@arch@/crt0.S',
    kernel_source_files,
    uacpi_source_files,
    arch_source_files.get(arch, []),
    nasm_gen.process(arch_nasm_files.get(arch,[])),
  ],
  include_directories: [kernel_headers, uacpi_headers],
  c_args: kernel_compile_flags,
  link_depends: [kernel_linker_script],
  link_args: [kernel_linker_flags],
  dependencies: [libc_dep, libalgo_dep, libpath_dep],
)

alias_target('kernel', kernel)
