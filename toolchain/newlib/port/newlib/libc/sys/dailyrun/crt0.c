#include <dailyrun/../sys/signal.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern int main(int argc, char **argv, char **envp);

extern char _edata;
extern char _end;

/*
 * Signal handler trampoline.
 *
 * This function calls the actual signal handler and makes sure
 * the process exits from the signal correctly.
 *
 * Must be installed using sigsethandler() first before installing
 * custom signal handlers.
 */
static void handle_signal(int signal, siginfo_t *info, void *data)
{
    ucontext_t *ucontext = data;
    sig_sa_sigaction_t handler = ucontext->sa_handler;

    handler(signal, info, ucontext);
    sigreturn(ucontext);
}

void _start(int argc, char **argv, char **envp)
{
    char *bss;
    int ret;

    /* Fill bss with zeros */
    bss = &_edata + 1;
    while (bss < &_end)
        *bss++ = 0;

    /* Install signal handler trampoline. */
    ret = sigsethandler(handle_signal);
    if (ret) {
        fprintf(stdout, "crt0: sigsethandler failed: %s\n", strerror(errno));
        goto out;
    }

    ret = main(argc, argv, envp);

out:
    _exit(ret);
}
