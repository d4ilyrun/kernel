public_headers = include_directories('include')
kernel_headers = include_directories('../../include')

sources = [
  'src/strlen.c',
  'src/memcpy.c',
  'src/memset.c',
  'src/printf.c',
]

libc = library('libc', sources,
  include_directories : [public_headers, kernel_headers],
  gnu_symbol_visibility : 'hidden',
)

# Make this library usable as a Meson subproject.
libc_dep = declare_dependency(
  include_directories: public_headers,
  link_with: libc
)
