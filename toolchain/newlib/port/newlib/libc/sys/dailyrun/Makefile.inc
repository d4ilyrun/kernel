# syscalls and stubs/wrappers
libc_a_SOURCES += %D%/syscalls.c
libc_a_SOURCES += %D%/killpg.c
libc_a_SOURCES += %D%/vfork.c
libc_a_SOURCES += %D%/wait3.c
libc_a_SOURCES += %D%/getgroups.c
libc_a_SOURCES += %D%/getcwd.c

libc_a_SOURCES += %D%/isatty.c

# DIR
libc_a_SOURCES += %D%/dirfd.c
libc_a_SOURCES += %D%/opendir.c
libc_a_SOURCES += %D%/readdir.c
libc_a_SOURCES += %D%/seekdir.c
libc_a_SOURCES += %D%/closedir.c

# malloc()
libc_a_SOURCES += %D%/liballoc.c
libc_a_SOURCES += %D%/malloc.c
