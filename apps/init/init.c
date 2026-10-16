#include <unistd.h>
#include <stdio.h>

int main(void)
{
	const char *argv[] = { "/bin/dash", NULL };
	const char *envp[] = { NULL };

	execve(argv[0], (char *const *)&argv, (char *const *)&envp);
}
