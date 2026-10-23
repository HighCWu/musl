#define _BSD_SOURCE
#include <unistd.h>
#include <errno.h>
#include "syscall.h"

#ifdef __wasm__
#include "pthread_impl.h"

extern void __heap_end; // synthesised by linker
static void *current = &__heap_end;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static int _brk(void *end) {
	uintptr_t address = (uintptr_t)end;
	size_t pages, present;

	if (address < (uintptr_t)current) goto fail;
	/* Count pages rather than caching a byte-end pointer: 4 GiB is a
	 * valid wasm32 capacity but cannot be represented by that pointer. */
	pages = address / PAGESIZE + !!(address % PAGESIZE);
	present = __builtin_wasm_memory_size(0);
	if (pages > present &&
	    __builtin_wasm_memory_grow(0, pages - present) == SIZE_MAX) goto fail;
	current = end;

	return 0;
fail:
	errno = ENOMEM;
	return -1;
}

int brk(void *end) {
	if (pthread_mutex_lock(&mutex) != 0) a_crash();
	int ret = _brk(end);
	pthread_mutex_unlock(&mutex);
	return ret;
}

void *sbrk(intptr_t inc) {
	if (pthread_mutex_lock(&mutex) != 0) a_crash();
	void *old = current;
	int ret;
	/* This allocator has always been monotonic. Reject shrinking before
	 * unsigned conversion, and reject wrap before constructing a pointer. */
	if (inc < 0 || (uintptr_t)inc > UINTPTR_MAX - (uintptr_t)current) {
		errno = ENOMEM;
		ret = -1;
	} else {
		ret = _brk((void *)((uintptr_t)current + (uintptr_t)inc));
	}
	pthread_mutex_unlock(&mutex);
	return ret == 0 ? old : (void *)-1;
}

#else
int brk(void *end)
{
	return __syscall_ret(-ENOMEM);
}
#endif
