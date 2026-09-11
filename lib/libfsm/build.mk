LIB_NAME        := libfsm
LIB_TARGET      := both
LIB_SRC_DIR     := $(LIB_DIR)/libfsm/src
LIB_INCLUDE_DIR := $(LIB_DIR)/libfsm/include

LIB_SOURCES := fsm.c

include $(REPO_ROOT)/lib/lib.mk
