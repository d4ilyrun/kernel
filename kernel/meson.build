arch = target_machine.cpu()

kernel_compile_flags = [
  f'-DARCH=@arch@',
]

kernel_source_files = files([
  'main.c',
  'sys/syscalls.c',
  'misc/logger.c',
  'misc/symbols.c',
  'memory/pmm.c',
  'memory/vmm.c',
  'memory/kmalloc.c',
])

arch_source_files = {
  'i686': files([
    'arch/i686/gdt.S',
    'arch/i686/gdt.c',
    'arch/i686/interrupts.c',
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


kernel_linker_script = 'linker.ld'
kernel_linker_flags = [ '-T', meson.project_source_root() / 'kernel' / kernel_linker_script ]

kernel_headers = include_directories('../include')

kernel = executable(
  'kernel.bin',
  [
    f'arch/@arch@/crt0.S',
    kernel_source_files,
    arch_source_files.get(arch, []),
    nasm_gen.process(arch_nasm_files.get(arch,[])),
  ],
  include_directories: [kernel_headers],
  c_args: kernel_compile_flags,
  link_depends: [kernel_linker_script],
  link_args: [kernel_linker_flags],
  dependencies: [libc_dep, libalgo_dep],
)

alias_target('kernel', kernel)
