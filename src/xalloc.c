#include "xalloc.h"
#include "util.h"
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

static void die(const char *fmt, ...)
	ATTRIBUTE(format(printf, 1, 2));

void *xmalloc(size_t size)
{
	void *ptr = malloc(size);
	if (!ptr && size != 0) {
		die("malloc(%zu) failed. Aborting.\n", size);
	}
	return ptr;
}

void *xcalloc(size_t count, size_t size)
{
	void *ptr = calloc(count, size);
	if (!ptr && count * size != 0) {
		die("calloc(%zu, %zu) failed. Aborting.\n", count, size);
	}
	return ptr;
}

void *xrealloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (!ptr && size != 0) {
		die("realloc(%p, %zu) failed. Aborting.\n", ptr, size);
	}
	return ptr;
}

static void die(const char *fmt, ...)
{
	// Probably thread-unsafety is OK.
	static char msgbuf[128];
	va_list va;
	va_start(va, fmt);
	// Hopefully snprintf doesn't allocate.
	int l = vsnprintf(msgbuf, sizeof(msgbuf), fmt, va);
	va_end(va);
	if (l > 0) {
		ssize_t UNUSED_VAR(w) = write(STDERR_FILENO, msgbuf, (size_t)l);
	} else {
		static const char msg[] = "Out of memory. Aborting.\n";
		ssize_t UNUSED_VAR(w) = write(STDERR_FILENO, msg, sizeof(msg));
	}
	abort();
}

#if CTF_TESTS_ENABLED

#	include "libctf.h"
#	include <assert.h>

CTF_TEST(xmalloc_0,
	xmalloc(0);
)

CTF_TEST(xcalloc_0,
	xcalloc(0, 0);
)

CTF_TEST(xrealloc_0,
	xrealloc(NULL, 0);
)

#endif /* CTF_TESTS_ENABLED */
