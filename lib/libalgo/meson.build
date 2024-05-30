public_headers = include_directories('include')
kernel_headers = include_directories('../../include')

sources = [
  'src/tree/avl.c',
  'src/tree/tree.c',
]

libalgo = library('libalgo', sources,
  include_directories : [public_headers, kernel_headers],
  gnu_symbol_visibility : 'hidden',
)

libalgo_native = library('libalgo_native', sources,
  include_directories : [public_headers, kernel_headers],
  gnu_symbol_visibility : 'hidden',
  c_args: [ '-Wno-pedantic' ],
  native: true,
)

# Make this library usable as a Meson subproject.
libalgo_dep = declare_dependency(
  include_directories: public_headers,
  link_with: libalgo
)

subdir('tests')
