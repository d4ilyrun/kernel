public_headers = include_directories('include')
kernel_headers = include_directories('../../include')

sources = [
  'src/path.c',
]

libpath = library('libpath', sources,
  include_directories : [public_headers, kernel_headers],
  gnu_symbol_visibility : 'hidden',
)

libpath_native = library('libpath_native', sources,
  include_directories : [public_headers, kernel_headers],
  gnu_symbol_visibility : 'hidden',
  native: true,
)

# Make this library usable as a Meson subproject.
libpath_dep = declare_dependency(
  include_directories: public_headers,
  link_with: libpath
)

subdir('tests')
