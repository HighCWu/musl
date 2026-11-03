#define _BSD_SOURCE
#include <unistd.h>
#include <errno.h>
#include "syscall.h"

#ifdef __wasm__
#include "pthread_impl.h"

extern void __heap_end; // synthesised by linker
extern void __global_base, __data_end;
static void *current = &__heap_end;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static int wasm_set_break(void *end) {
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
	return -1;
}

#ifndef WASM_BRK_SYSCALL
static int _brk(void *end)
{
	int result = wasm_set_break(end);
	if (result) errno = ENOMEM;
	return result;
}
#endif

/* Private Wasm execution callbacks, not libc declarations or application APIs.
 * Immutable pointer-width slots describe offsets in this process memory.
 * Keep the original allocator's monotonic rule: mmap borrows this heap arena.
 */
__attribute__((export_name("__wasm_brk_layout_v1")))
uintptr_t __wasm_brk_layout_v1(void)
{
	static void *const layout[3] = { &__heap_end, &__global_base, &__data_end };
	return (uintptr_t)layout;
}

__attribute__((export_name("__wasm_brk_update_v1")))
uintptr_t __wasm_brk_update_v1(uintptr_t requested)
{
	if (pthread_mutex_lock(&mutex)) return (uintptr_t)current;
	if (requested) (void)wasm_set_break((void *)requested);
	uintptr_t result = (uintptr_t)current;
	pthread_mutex_unlock(&mutex);
	return result;
}

#ifdef WASM_BRK_SYSCALL
/* Separate API serialization from the callback's state lock: the syscall
 * synchronously re-enters the same module to query/update its allocator. */
static pthread_mutex_t api_mutex = PTHREAD_MUTEX_INITIALIZER;

int brk(void *end)
{
	if (pthread_mutex_lock(&api_mutex)) a_crash();
	uintptr_t result = (uintptr_t)__syscall(SYS_brk, end);
	pthread_mutex_unlock(&api_mutex);
	if (result == (uintptr_t)end) return 0;
	errno = ENOMEM;
	return -1;
}

void *sbrk(intptr_t inc)
{
	if (pthread_mutex_lock(&api_mutex)) a_crash();
	uintptr_t old = (uintptr_t)__syscall(SYS_brk, 0);
	int failed = old >= (uintptr_t)-4095 || inc < 0 ||
		(uintptr_t)inc > UINTPTR_MAX - old;
	if (!failed && inc)
		failed = (uintptr_t)__syscall(SYS_brk, old + (uintptr_t)inc) !=
			old + (uintptr_t)inc;
	pthread_mutex_unlock(&api_mutex);
	if (!failed) return (void *)old;
	errno = ENOMEM;
	return (void *)-1;
}
#else

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
#endif

#else
int brk(void *end)
{
	return __syscall_ret(-ENOMEM);
}
#endif
