$(eval $(call DEFINE_STATIC_LIBRARY_AT,libuacpi,uacpi/uACPI,include,source,\
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
))
