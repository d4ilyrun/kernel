arch = target_machine.cpu()

kernel_compile_flags = [
  '-fomit-frame-pointer',
  f'-DARCH=@arch@',
]

kernel_source_files = files([
  'src/main.c',
  'src/syscalls.c',
  'src/debug/logger.c',
  'src/debug/symbols.c',
  'src/memory/pmm.c',
])

arch_source_files = {
  'i686': files([
    'arch/i686/src/setup.c',
    'arch/i686/src/gdt.c',
    'arch/i686/src/gdt.S',
    'arch/i686/src/interrupts.c',
    'arch/i686/src/terminal.c',
    'arch/i686/src/devices/pic.c',
    'arch/i686/src/devices/pit.c',
    'arch/i686/src/devices/timer.c',
    'arch/i686/src/devices/uart.c',
    'arch/i686/src/memory/mmu.c',
  ]),
}

arch_nasm_files = {
  'i686': files([
    'arch/i686/src/interrupts.S',
    'arch/i686/src/devices/pic.S',
  ]),
}


kernel_linker_script = 'linker.ld'
kernel_linker_flags = [ '-T', meson.project_source_root() / 'kernel' / kernel_linker_script ]

kernel_headers = include_directories('include')
arch_headers = include_directories(f'arch/@arch@/include')

kernel = executable(
  'kernel.bin',
  [
    f'arch/@arch@/src/crt0.S',
    kernel_source_files,
    arch_source_files.get(arch, []),
    nasm_gen.process(arch_nasm_files.get(arch,[])),
  ],
  include_directories: [kernel_headers, arch_headers],
  c_args: kernel_compile_flags,
  link_depends: [kernel_linker_script],
  link_args: [kernel_linker_flags],
  dependencies: libc_dep,
)

alias_target('kernel', kernel)
