arch = target_machine.cpu()

kernel_compile_flags = [
  '-DSTACK_SIZE=16384',
  '-fomit-frame-pointer',
]

kernel_source_files = files([
  'src/main.c',
  'src/syscalls.c',
  'src/logger.c',
  'src/devices/pic.c',
  'src/devices/uart.c',
  'src/devices/timer.c',
])

kernel_nasm_files = files([
  'src/asm/pic.S',
])

arch_source_files = {
  'i686': files([
    'arch/i686/setup.c',
    'arch/i686/gdt.c',
    'arch/i686/asm/gdt.S',
    'arch/i686/interrupts.c',
    'arch/i686/terminal.c',
  ]),
}

arch_nasm_files = {
  'i686': files([
    'arch/i686/asm/interrupts.S',
  ]),
}


kernel_linker_script = f'arch/@arch@/linker.ld'
kernel_linker_flags = [ '-T', meson.project_source_root() / 'kernel' / kernel_linker_script ]

kernel_headers = include_directories('include')
arch_headers = include_directories(f'arch/@arch@/include')

kernel = executable(
  'kernel.bin',
  [
    'src/crt0.S',
    kernel_source_files,
    arch_source_files.get(arch, []),
    nasm_gen.process(kernel_nasm_files),
    nasm_gen.process(arch_nasm_files.get(arch,[])),
  ],
  include_directories: [kernel_headers, arch_headers],
  c_args: kernel_compile_flags,
  link_depends: [kernel_linker_script],
  link_args: [kernel_linker_flags],
  dependencies: libc_dep,
)

alias_target('kernel', kernel)
