#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <limits.h>

extern int _getcwd(char *buf, size_t size);

/*
 *
 */
char *getcwd(char *buf, size_t size)
{
	char *out_buf;
	int ret;

	if (size == 0) {
		errno = EINVAL;
		return NULL;
	}

	/* mimic glibc's behaviour for portability. */
	if (buf == NULL) {
		out_buf = malloc(size);
		if (out_buf == NULL)
			return NULL;
	} else
		out_buf = buf;

	ret = _getcwd(out_buf, size);
	if (ret < 0) {
		if (buf == NULL)
			free(out_buf);
		return NULL;
	}

	return out_buf;
}

/*
 * TODO: return $PWD if set.
 */
char *get_current_dir_name(void)
{
	return getcwd(NULL, PATH_MAX);
}
