LIB_NAME        := libinput
LIB_TARGET      := user
LIB_SRC_DIR     := $(LIB_DIR)/libinput/src
LIB_INCLUDE_DIR := $(LIB_DIR)/libinput/include

LIB_SOURCES := libinput.c

include $(REPO_ROOT)/lib/lib.mk

