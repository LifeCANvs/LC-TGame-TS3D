#include "xalloc.h"
#include <stdio.h>
#include <unistd.h>

void *xmalloc(size_t size)
{
	void *ptr = malloc(size);
	if (!ptr) {
		char buf[128];
		write(STDERR_FILENO, buf, snprintf(buf, sizeof(buf),
			"malloc(%lu) failed. Aborting.\n", size));
		abort();
	}
	return ptr;
}

void *xcalloc(size_t count, size_t size)
{
	void *ptr = calloc(count, size);
	if (!ptr) {
		char buf[128];
		write(STDERR_FILENO, buf, snprintf(buf, sizeof(buf),
			"calloc(%lu, %lu) failed. Aborting.\n", count, size));
		abort();
	}
	return ptr;
}

void *xrealloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (!ptr) {
		char buf[128];
		write(STDERR_FILENO, buf, snprintf(buf, sizeof(buf),
			"realloc(%p, %lu) failed. Aborting.\n", ptr, size));
		abort();
	}
	return ptr;
}
