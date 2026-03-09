#include_next <sys/signal.h>

/*
 * Signature of both signal handler functions inside struct sigaction.
 */
typedef void (*sig_sa_handler_t)(int);
typedef void (*sig_sa_sigaction_t)(int, siginfo_t *, void *);

#ifdef __i686__
#include <dailyrun/arch/i686/signal.h>
#endif

int sigsethandler(sig_sa_sigaction_t);
int sigreturn(ucontext_t *);
