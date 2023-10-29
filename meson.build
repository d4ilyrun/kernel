project('kernel', ['c'],
  default_options : [
    'warning_level=3',
    'c_std=c99',
    'c_args=-ffreestanding',
    'c_link_args=-nostdlib -lgcc',
    'default_library=static',
    'werror=true',
    'b_pie=false',
  ]
)

project_description = 'My very own x86 kernel from scratch'

compiler = meson.get_compiler('c')

# Make sure that we we have the necessary headers, should always be the case
foreach header: [ 'stddef.h', 'stdint.h', 'stdarg.h' ]
  compiler.has_header(header, required: true)
endforeach

if compiler.has_header('string.h')
  warning('Headers from the glibc are still accessible')
endif

subdir('lib/libc')
subdir('kernel')

iso = alias_target('iso',
  run_target('kernel.iso',
    command: [ 'scripts/generate_iso.sh', kernel, '@OUTPUT@' ],
    depends: kernel
  )
)
