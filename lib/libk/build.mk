LIB_NAME        := libk
LIB_TARGET      := kernel
LIB_SRC_DIR     := $(LIB_DIR)/libk/src
LIB_INCLUDE_DIR := $(LIB_DIR)/libk/include

LIB_SOURCES := \
  string.c \
  memcpy.c \
  memset.c \

include $(REPO_ROOT)/lib/lib.mk
