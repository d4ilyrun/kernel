$(eval $(call DEFINE_STATIC_LIBRARY,libalgo, \
	tree/avl.c  \
	tree/tree.c \
	hashtable.c \
))

libalgo_TESTS_LDFLAGS = -lalgo
$(eval $(call DEFINE_CRITERION_TESTSUITE,libalgo,tree/avl))
