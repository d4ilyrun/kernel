LIB_NAME        := libalgo
LIB_TARGET      := kernel
LIB_SRC_DIR     := $(LIB_DIR)/libalgo/src
LIB_INCLUDE_DIR := $(LIB_DIR)/libalgo/include

LIB_SOURCES := \
	tree/avl.c  \
	tree/tree.c \
	hashtable.c \
	ringbuffer.c \

include $(REPO_ROOT)/lib/lib.mk
