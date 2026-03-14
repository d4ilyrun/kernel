/*
 * Modified version of the original sys/signal.h provided by newlib.
 *
 * Modifications:
 * - Remove CYGWIN and target specific ifdefs
 * - Remove _POSIX_THREAD ifdefs
 * - Remove _POSIX_REALTIME_SIGNALS ifdefs
 * - Remove sigalstack() (not implemented);
 * - Add sa_sigaction to struct sigaction (even without _POSIX_REALTIME_SIGNALS)
 * - Remove unimplemented sa_flags and si_code values
 */

#ifndef _SYS_SIGNAL_H
#define _SYS_SIGNAL_H

#include <stdint.h>
#include <sys/_sigset.h>
#include <sys/_timespec.h>
#include <sys/cdefs.h>
#include <sys/features.h>
#include <sys/types.h>

#include "_ansi.h"

#if !defined(_SIGSET_T_DECLARED)
#define _SIGSET_T_DECLARED
typedef __sigset_t sigset_t;
#endif

union sigval {
    int sival_int;   /* Integer signal value */
    void *sival_ptr; /* Pointer signal value */
};

#define SI_USER 1  /* Sent by a user. kill(), abort(), etc */
#define SI_QUEUE 2 /* Sent by sigqueue() */

typedef struct {
    int si_signo;          /* Signal number */
    int si_code;           /* Cause of the signal */
    union sigval si_value; /* Signal value */
} siginfo_t;

#define SA_NOCLDWAIT 0x1 /* Children dont' generate SIGCHLD in _exit(). */

typedef void (*_sig_func_ptr)(int);
typedef void (*sig_sa_handler_t)(int);
typedef void (*sig_sa_sigaction_t)(int, siginfo_t *, void *);

struct sigaction {
    int sa_flags;     /* Special flags to affect behavior of signal */
    sigset_t sa_mask; /* Additional set of signals to be blocked */
                      /*   during execution of signal-catching */
                      /*   function. */
    union {
        sig_sa_handler_t sa_handler; /* SIG_DFL, SIG_IGN, or pointer to a
                                        function */
        sig_sa_sigaction_t sa_sigaction;
    };
};

#ifdef __POSIX_VISIBLE
#define SIG_SETMASK 0 /* set mask with sigprocmask() */
#define SIG_BLOCK 1   /* set of signals to block */
#define SIG_UNBLOCK 2 /* set of signals to, well, unblock */

int sigprocmask(int, const sigset_t *, sigset_t *);
#endif

#if __POSIX_VISIBLE >= 199506
int pthread_sigmask(int, const sigset_t *, sigset_t *);
#endif

#ifdef _LIBC
int _kill(pid_t, int);
#endif /* _LIBC */

#if __POSIX_VISIBLE
int kill(pid_t, int);
#endif

#if __BSD_VISIBLE || __XSI_VISIBLE >= 4
int killpg(pid_t, int);
#endif

#if __POSIX_VISIBLE

int sigaction(int, const struct sigaction *, struct sigaction *);
int sigaddset(sigset_t *, const int);
int sigdelset(sigset_t *, const int);
int sigismember(const sigset_t *, int);
int sigfillset(sigset_t *);
int sigemptyset(sigset_t *);
int sigpending(sigset_t *);
int sigsuspend(const sigset_t *);
int sigwait(const sigset_t *, int *);

/* These depend upon the type of sigset_t, which right now
   is always a long.. They're in the POSIX namespace, but
   are not ANSI. */
#define sigaddset(what, sig) (*(what) |= (1 << (sig)), 0)
#define sigdelset(what, sig) (*(what) &= ~(1 << (sig)), 0)
#define sigemptyset(what) (*(what) = 0, 0)
#define sigfillset(what) (*(what) = ~(0), 0)
#define sigismember(what, sig) (((*(what)) & (1 << (sig))) != 0)

#endif /* __POSIX_VISIBLE */

/* There are two common sigpause variants, both of which take an int argument.
   If you request _XOPEN_SOURCE or _GNU_SOURCE, you get the System V version,
   which removes the given signal from the process's signal mask; otherwise
   you get the BSD version, which sets the process's signal mask to the given
   value. */
#if __XSI_VISIBLE
int sigpause(int);
#endif

#if __POSIX_VISIBLE >= 199506
int pthread_kill(pthread_t, int);
#endif

#if __POSIX_VISIBLE >= 199309

/*  3.3.8 Synchronously Accept a Signal, P1003.1b-1993, p. 76
    NOTE: P1003.1c/D10, p. 39 adds sigwait().  */

int sigwaitinfo(const sigset_t *, siginfo_t *);
int sigtimedwait(const sigset_t *, siginfo_t *, const struct timespec *);
/*  3.3.9 Queue a Signal to a Process, P1003.1b-1993, p. 78 */
int sigqueue(pid_t, int, const union sigval);

#endif /* __POSIX_VISIBLE >= 199309 */

/* Using __MISC_VISIBLE until POSIX Issue 8 is officially released */
#if __MISC_VISIBLE

/* POSIX Issue 8 adds sig2str() and str2sig() */

#if __SIZEOF_INT__ >= 4
#define SIG2STR_MAX 17 /* (sizeof("RTMAX+") + sizeof("4294967295") - 1) */
#else
#define SIG2STR_MAX 12 /* (sizeof("RTMAX+") + sizeof("65535") - 1) */
#endif

int sig2str(int, char *);
int str2sig(const char *__restrict, int *__restrict);

#endif /* __MISC_VISIBLE */

#define SIGHUP 1      /* hangup */
#define SIGINT 2      /* interrupt */
#define SIGQUIT 3     /* quit */
#define SIGILL 4      /* illegal instruction (not reset when caught) */
#define SIGTRAP 5     /* trace trap (not reset when caught) */
#define SIGIOT 6      /* IOT instruction */
#define SIGABRT 6     /* used by abort, replace SIGIOT in the future */
#define SIGEMT 7      /* EMT instruction */
#define SIGFPE 8      /* floating point exception */
#define SIGKILL 9     /* kill (cannot be caught or ignored) */
#define SIGBUS 10     /* bus error */
#define SIGSEGV 11    /* segmentation violation */
#define SIGSYS 12     /* bad argument to system call */
#define SIGPIPE 13    /* write on a pipe with no one to read it */
#define SIGALRM 14    /* alarm clock */
#define SIGTERM 15    /* software termination signal from kill */
#define SIGURG 16     /* urgent condition on IO channel */
#define SIGSTOP 17    /* sendable stop signal not from tty */
#define SIGTSTP 18    /* stop signal from tty */
#define SIGCONT 19    /* continue a stopped process */
#define SIGCHLD 20    /* to parent on child stop or exit */
#define SIGCLD 20     /* System V name for SIGCHLD */
#define SIGTTIN 21    /* to readers pgrp upon background tty read */
#define SIGTTOU 22    /* like TTIN for output if (tp->t_local&LTOSTOP) */
#define SIGIO 23      /* input/output possible signal */
#define SIGPOLL SIGIO /* System V name for SIGIO */
#define SIGXCPU 24    /* exceeded CPU time limit */
#define SIGXFSZ 25    /* exceeded file size limit */
#define SIGVTALRM 26  /* virtual time alarm */
#define SIGPROF 27    /* profiling time alarm */
#define SIGWINCH 28   /* window changed */
#define SIGLOST 29    /* resource lost (eg, record-lock lost) */
#define SIGUSR1 30    /* user defined signal 1 */
#define SIGUSR2 31    /* user defined signal 2 */
#define NSIG 32       /* signal 0 implied */

#ifndef _SIGNAL_H_
/* Some applications take advantage of the fact that <sys/signal.h>
 * and <signal.h> are equivalent in glibc.  Allow for that here.  */
#include <signal.h>
#endif

/*
 * Signature of both signal handler functions inside struct sigaction.
 */

#ifdef __i686__
#include <dailyrun/arch/i686/signal.h>
#endif

/*
 *
 */
int sigsethandler(sig_sa_sigaction_t);
int sigreturn(ucontext_t *);

#endif /* _SYS_SIGNAL_H */
