criterion_dir = 'criterion'
criterion_incdir = include_directories(join_paths(criterion_dir, 'include'), is_system: true)

test_dep = declare_dependency(
  include_directories: [ criterion_incdir ],
  link_args: [ '-lcriterion' ]
)
