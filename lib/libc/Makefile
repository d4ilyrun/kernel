CC ?= i686-elf-gcc

CFLAGS 		= -nostdlib
CPPFLAGS 	= -I./include
LDFLAGS 	=

LIB = libc.a

FUNCS = strlen memcpy memset
OBJS = $(addsuffix .o, $(addprefix src/, $(FUNCS)))

all: library
library: $(LIB)

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

clean:
	$(RM) $(LIB) $(OBJS)
