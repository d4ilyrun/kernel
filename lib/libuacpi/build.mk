LIB_NAME        := libuacpi
LIB_TARGET      := kernel
LIB_SRC_DIR     := $(LIB_DIR)/libuacpi/uACPI/source
LIB_INCLUDE_DIR := $(LIB_DIR)/libuacpi/uACPI/include

LIB_SOURCES := \
    tables.c \
    types.c \
    uacpi.c \
    utilities.c \
    interpreter.c \
    opcodes.c \
    namespace.c \
    stdlib.c \
    shareable.c \
    opregion.c \
    default_handlers.c \
    io.c \
    notify.c \
    sleep.c \
    registers.c \
    resources.c \
    event.c \
    mutex.c \
    osi.c \

include $(REPO_ROOT)/lib/lib.mk
