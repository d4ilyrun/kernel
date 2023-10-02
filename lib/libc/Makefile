CC ?= i686-elf-gcc

CFLAGS 		= -nostdlib
CPPFLAGS 	= -I./include
LDFLAGS 	=

LIB = libc.a

OBJS = $(addprefix src/, \
	   strlen.o \
	   )

all: library
library: $(LIB)

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

clean:
	$(RM) $(LIB) $(OBJS)
