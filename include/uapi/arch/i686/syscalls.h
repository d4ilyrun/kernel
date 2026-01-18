#ifndef UAPI_ARCH_I686_SYSCALLS_H
#define UAPI_ARCH_I686_SYSCALLS_H

/** The interrupt number used to trigger a syscall */
#define SYSCALL_INTERRUPT_NR 0x80

/*
 * (name, vector number, args count, syscall type, return type, arguments
 */
#define DEFINE_SYSCALLS(F) \
    F(exit		, 1		, 1	, noreturn	, void		, int) \
    F(fork		, 2		, 0	, default	, int		, void) \
    F(read		, 3		, 3	, default	, int		, int, char *, int) \
    F(write		, 4		, 3	, default	, int		, int, const char *, int) \
    F(open		, 5		, 2	, default	, int		, const char *, int, ...) \
    F(close		, 6		, 1	, default	, int		, int) \
    F(waitpid	, 7		, 3	, default	, pid_t		, pid_t, int *, int) \
    F(execve	, 11	, 3	, default	, int		, char *, char *const *, char *const *) \
    F(lseek		, 19	, 3	, default	, int		, int, int, int) \
    F(getpid	, 20	, 0	, default	, int		, void) \
    F(kill		, 37	, 2	, default	, int		, pid_t, int) \
    F(brk		, 45	, 1	, default	, int		, void *) \
    F(stat		, 106	, 2	, default	, int		, const char *, struct stat *) \
    F(lstat		, 107	, 2	, default	, int		, const char *, struct stat *) \
    F(fstat		, 108	, 2	, default	, int		, int, struct stat *) \
    F(sbrk		, 463	, 1	, default	, void *	, intptr_t) \

#endif /* UAPI_ARCH_I686_SYSCALLS_H */
