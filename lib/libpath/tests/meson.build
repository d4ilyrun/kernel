libpath_native_dep = declare_dependency(
  include_directories: public_headers,
  link_with: libpath_native
)

path_test = executable('libpath_test', 'path.c',
  dependencies: [libpath_native_dep, test_dep],
  include_directories: kernel_headers,
  native: true,
)

test('path', path_test, args: [ '--color=always' ], suite: 'libpath')
