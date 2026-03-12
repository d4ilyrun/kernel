#ifndef _DAILYRUN_SYSCALLS_H
#define _DAILYRUN_SYSCALLS_H

/*
 *    name,					vec,	argc,	sys_type,		ret_type,	args...
 */
 #define DEFINE_SYSCALLS(F)                                                                                                                          \
    F(exit,					0,		1,		noreturn,		void,		int)                                                                         \
    F(fork,					1,		0,		default,		int,		void)                                                                        \
    F(execve,				2,		3,		default,		int,		char *, char *const *, char *const *)                                        \
    F(read,					3,		3,		default,		int,		int, char *, int)                                                            \
    F(write,				4,		3,		default,		int,		int, const char *, int)                                                      \
    F(open,					5,		2,		default,		int,		const char *, int, ...)                                                      \
    F(close,				6,		1,		default,		int,		int)                                                                         \
    F(stat,					7,		2,		default,		int,		const char *, struct stat *)                                                 \
    F(lstat,				8,		2,		default,		int,		const char *, struct stat *)                                                 \
    F(fstat,				9,		2,		default,		int,		int, struct stat *)                                                          \
    F(lseek,				10,		3,		default,		int,		int, int, int)                                                               \
    F(waitpid,				11,		3,		default,		pid_t,		pid_t, int *, int)                                                           \
    F(getpid,				12,		0,		default,		int,		void)                                                                        \
    F(brk,					13,		1,		default,		int,		void *)                                                                      \
    F(sbrk,					14,		1,		default,		void *,		intptr_t)                                                                    \
    F(setuid,				15,		1,		default,		int,		uid_t)                                                                       \
    F(seteuid,				16,		1,		default,		int,		uid_t)                                                                       \
    F(setreuid,				17,		2,		default,		int,		uid_t, uid_t)                                                                \
    F(getuid,				18,		0,		default,		uid_t,		void)                                                                        \
    F(setresuid,			19,		3,		default,		int,		uid_t, uid_t, uid_t)                                                         \
    F(geteuid,				20,		0,		default,		uid_t,		void)                                                                        \
    F(setgid,				21,		1,		default,		int,		gid_t)                                                                       \
    F(setegid,				22,		1,		default,		int,		gid_t)                                                                       \
    F(setregid,				23,		2,		default,		int,		gid_t, gid_t)                                                                \
    F(setresgid,			24,		3,		default,		int,		gid_t, gid_t, gid_t)                                                         \
    F(getgid,				25,		0,		default,		gid_t,		void)                                                                        \
    F(getegid,				26,		0,		default,		gid_t,		void)                                                                        \
    F(kill,					27,		2,		default,		int,		pid_t, int)                                                                  \
    F(sigaction,			28,		3,		default,		int,		int, const struct sigaction *, struct sigaction *)                           \
    F(sigprocmask,			29,		4,		default,		int,		int, const sigset_t *, sigset_t *, size_t)                                   \
    F(sigpending,			30,		1,		default,		int,		sigset_t *)                                                                  \
    F(sigsethandler,		31, 	1,		default,		int,		sig_sa_sigaction_t)                                                          \
    F(sigreturn,			32, 	1,		default,		int,		ucontext_t *)                                                                \
    F(getdents,				33, 	4,		default,		ssize_t,	int, void *, size_t, int)                                                    \

/*
 * The list of available syscall vectors.
 */
enum syscall_nr {
#define SYSCALL_NUMBER(name, vector, ...) SYS_##name = vector,
    DEFINE_SYSCALLS(SYSCALL_NUMBER) /*  */
	SYSCALL_COUNT
#undef SYSCALL_NUMBER
};

#endif /* _DAILYRUN_SYSCALLS_H */

// vi: ft=c tabstop=4 shiftwidth=4 noexpandtab
