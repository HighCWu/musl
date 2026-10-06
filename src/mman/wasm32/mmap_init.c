/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include <errno.h>
#include <limits.h>

extern long __wasm_mmap_initialized(size_t,
		int (*)(void *, size_t, void *), void *);

__attribute__((import_module("linux_mmap_init_v1"), import_name("copy")))
extern int __wasm_mmap_copy_init_v1(void *, size_t);

static int copy_staging(void *candidate, size_t rounded, void *argument)
{
	size_t length = *(size_t *)argument;
	(void)rounded;
	return __wasm_mmap_copy_init_v1(candidate, length);
}

/* Optional experimental execution export, deliberately in a separate object:
 * linking the ordinary mmap allocator must not require the new host import.
 * The host grants one synchronous staging copy; this wrapper checks its result
 * before the allocator publishes fresh, zero-initialized backing. */
long __wasm_mmap_init_v1(size_t rounded, size_t length)
{
	if (!rounded || (rounded & (PAGESIZE - 1)) || length > rounded)
		return -EINVAL;
	return __wasm_mmap_initialized(rounded, copy_staging, &length);
}
