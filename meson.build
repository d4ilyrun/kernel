project('kernel', ['c'],
  default_options : [
    'warning_level=3',
    'c_std=c11',
    'c_args=-ffreestanding',
    'c_link_args=-nostdlib -lgcc',
    'default_library=static',
    'werror=true',
    'b_pie=false',
  ]
)

project_description = 'My very own x86 kernel from scratch'

compiler = meson.get_compiler('c')

compiler.sizeof('char')
compiler.sizeof('short')
compiler.sizeof('int')
compiler.sizeof('long')
compiler.sizeof('long long')
compiler.sizeof('float')
compiler.sizeof('double')

# Make sure that we we have the necessary headers, should always be the case
foreach header: [ 'stddef.h', 'stdint.h', 'stdarg.h' ]
  compiler.has_header(header, required: true)
endforeach

if compiler.has_header('string.h')
  warning('Headers from the glibc are still accessible')
endif

nasm = find_program('nasm', required: true)
nasm_gen = generator(nasm,
  output : '@BASENAME@.o',
  arguments : [
    '-f', 'elf32',
    '@INPUT@',
    '-o', '@OUTPUT@']
)

### Build targets: build kernel and required libraries

if get_option('buildtype') == 'debug'
  add_project_arguments('-g3', '-DNDEBUG', language : 'c')
  add_project_link_arguments('-g3', language : 'c')
endif

subdir('lib/libc')
subdir('kernel')

iso = custom_target('iso',
  output: 'kernel.iso',
  command: [ 'scripts/generate_iso.sh', kernel, '@OUTPUT@' ],
  depends: kernel
)

alias_target('iso', iso)

### Run targets: run the kernel using qemu

qemu = find_program('qemu-system-i386')

run_target('qemu',
  command: [qemu, '-cdrom', iso, '-serial', 'stdio']
)

if get_option('buildtype') == 'debug'

gdb = find_program('gdb')

# We need to use a custom target to be able to daemonize appropriately
custom_target('qemu-server',
  input: [],
  output: ['qemu-server'],
  command: [qemu, '-cdrom', iso, '-daemonize', '-s', '-S'],
)

endif
