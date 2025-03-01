$(eval $(call DEFINE_STATIC_LIBRARY,libpath,path.c))

libpath_TESTS_LDFLAGS := -lpath
$(eval $(call DEFINE_CRITERION_TESTSUITE,libpath,path))
