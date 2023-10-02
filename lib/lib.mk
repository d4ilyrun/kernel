# To be included by the main Makefile at the root of the project

LIBC_ROOT = $(LIB_ROOT)/libc

# Available libraries
LIBS = libc

CLEAN_RECURSIVE += $(addprefix $(LIB_ROOT)/,$(LIBS))

libs: $(LIBS)
$(LIBS):
	@$(MAKE) -C $(LIB_ROOT)/$@ library
