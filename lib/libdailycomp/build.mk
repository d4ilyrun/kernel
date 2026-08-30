LIB_NAME        := libdailycomp
LIB_TARGET      := user
LIB_SRC_DIR     := $(LIB_DIR)/libdailycomp/src
LIB_INCLUDE_DIR := $(LIB_DIR)/libdailycomp/include

LIB_SOURCES := client.c strings.c

include $(REPO_ROOT)/lib/lib.mk
