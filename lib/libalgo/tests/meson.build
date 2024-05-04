libalgo_native_dep = declare_dependency(
  include_directories: public_headers,
  link_with: libalgo_native
)

avl_test = executable('avl_test', 'tree/avl.c',
  dependencies: [libalgo_native_dep, test_dep],
  include_directories: kernel_headers,
  c_args: [ '-Wno-pedantic' ],
  native: true,
)

test('avl', avl_test, args: [ '--color=always' ], suite: 'libalgo')
