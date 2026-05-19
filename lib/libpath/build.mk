LIB_NAME        := libpath
LIB_TARGET      := kernel
LIB_INCLUDE_DIR := $(LIB_DIR)/libpath/include
LIB_SRC_DIR     := $(LIB_DIR)/libpath/src
LIB_SOURCES     := path.c

include $(REPO_ROOT)/lib/lib.mk
