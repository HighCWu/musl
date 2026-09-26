#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#ifdef __wasm__
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#endif
#include "syscall.h"

static void dummy(void) { }
weak_alias(dummy, __vm_wait);

#define UNIT SYSCALL_MMAP2_UNIT
#define OFF_MASK ((-0x2000ULL << (8*sizeof(syscall_arg_t)-1)) | (UNIT-1))

#ifdef __wasm__

struct wasm_allocation {
	struct wasm_allocation *next_free;
	size_t references;
};

struct wasm_mapping {
	struct wasm_mapping *next;
	struct wasm_allocation *allocation;
	void *address;
	size_t length;
};

static pthread_mutex_t mappings_lock = PTHREAD_MUTEX_INITIALIZER;
static struct wasm_mapping *mappings;

void *__mmap(void *start, size_t len, int prot, int flags, int fd, off_t off)
{
	const int supported_flags = MAP_PRIVATE | MAP_ANONYMOUS;
	struct wasm_mapping *mapping;
	struct wasm_allocation *allocation;
	uintptr_t allocation_start, address;
	size_t rounded, total;
	int lock_error;

	if (!len || off || fd != -1 || (flags & MAP_TYPE) != MAP_PRIVATE ||
	    !(flags & MAP_ANONYMOUS)) {
		errno = EINVAL;
		return MAP_FAILED;
	}
	if (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	if ((flags & ~supported_flags) ||
	    prot != (PROT_READ | PROT_WRITE)) {
		errno = EINVAL;
		return MAP_FAILED;
	}
	if (len > SIZE_MAX - (PAGESIZE - 1)) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	rounded = (len + PAGESIZE - 1) & -(size_t)PAGESIZE;
	if (rounded > SIZE_MAX - PAGESIZE - sizeof(*allocation)) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	total = rounded + PAGESIZE + sizeof(*allocation);
	allocation = __libc_malloc(total);
	if (!allocation)
		return MAP_FAILED;
	mapping = __libc_malloc(sizeof(*mapping));
	if (!mapping) {
		__libc_free(allocation);
		return MAP_FAILED;
	}

	allocation_start = (uintptr_t)(allocation + 1);
	address = (allocation_start + PAGESIZE - 1) & -(uintptr_t)PAGESIZE;
	allocation->next_free = 0;
	allocation->references = 1;
	mapping->allocation = allocation;
	mapping->address = (void *)address;
	mapping->length = rounded;
	memset(mapping->address, 0, rounded);

	lock_error = pthread_mutex_lock(&mappings_lock);
	if (lock_error) {
		__libc_free(mapping);
		__libc_free(allocation);
		errno = lock_error;
		return MAP_FAILED;
	}
	mapping->next = mappings;
	mappings = mapping;
	(void)pthread_mutex_unlock(&mappings_lock);

	(void)start;
	return mapping->address;
}

hidden int __wasm_munmap(void *start, size_t len)
{
	struct wasm_mapping **cursor, *mapping, *removed = 0;
	struct wasm_mapping *spare;
	struct wasm_allocation *allocation, *released = 0;
	uintptr_t begin = (uintptr_t)start, end;
	size_t rounded;
	int lock_error;

	if ((uintptr_t)start & (PAGESIZE - 1) || !len ||
	    len > SIZE_MAX - (PAGESIZE - 1)) {
		errno = EINVAL;
		return -1;
	}
	rounded = (len + PAGESIZE - 1) & -(size_t)PAGESIZE;
	if (begin > UINTPTR_MAX - rounded) {
		errno = EINVAL;
		return -1;
	}
	end = begin + rounded;
	spare = __libc_malloc(sizeof(*spare));

	lock_error = pthread_mutex_lock(&mappings_lock);
	if (lock_error) {
		__libc_free(spare);
		errno = lock_error;
		return -1;
	}
	for (cursor = &mappings; (mapping = *cursor); ) {
		uintptr_t mapping_begin = (uintptr_t)mapping->address;
		uintptr_t mapping_end = mapping_begin + mapping->length;

		if (end <= mapping_begin || begin >= mapping_end) {
			cursor = &mapping->next;
			continue;
		}
		if (begin <= mapping_begin && end >= mapping_end) {
			*cursor = mapping->next;
			mapping->next = removed;
			removed = mapping;
			allocation = mapping->allocation;
			if (!--allocation->references) {
				allocation->next_free = released;
				released = allocation;
			}
			continue;
		}
		if (begin <= mapping_begin) {
			mapping->address = (void *)end;
			mapping->length = mapping_end - end;
			cursor = &mapping->next;
			continue;
		}
		if (end >= mapping_end) {
			mapping->length = begin - mapping_begin;
			cursor = &mapping->next;
			continue;
		}
		if (!spare) {
			(void)pthread_mutex_unlock(&mappings_lock);
			errno = ENOMEM;
			return -1;
		}
		spare->next = mapping->next;
		spare->allocation = mapping->allocation;
		spare->address = (void *)end;
		spare->length = mapping_end - end;
		mapping->next = spare;
		mapping->length = begin - mapping_begin;
		mapping->allocation->references++;
		spare = 0;
		break;
	}
	(void)pthread_mutex_unlock(&mappings_lock);

	__libc_free(spare);
	while (removed) {
		mapping = removed;
		removed = mapping->next;
		__libc_free(mapping);
	}
	while (released) {
		allocation = released;
		released = allocation->next_free;
		__libc_free(allocation);
	}
	return 0;
}

#else
void *__mmap(void *start, size_t len, int prot, int flags, int fd, off_t off)
{
	long ret;
	if (off & OFF_MASK) {
		errno = EINVAL;
		return MAP_FAILED;
	}
	if (len >= PTRDIFF_MAX) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	if (flags & MAP_FIXED) {
		__vm_wait();
	}
#ifdef SYS_mmap2
	ret = __syscall(SYS_mmap2, start, len, prot, flags, fd, off/UNIT);
#else
	ret = __syscall(SYS_mmap, start, len, prot, flags, fd, off);
#endif
	/* Fixup incorrect EPERM from kernel. */
	if (ret == -EPERM && !start && (flags&MAP_ANON) && !(flags&MAP_FIXED))
		ret = -ENOMEM;
	return (void *)__syscall_ret(ret);
}
#endif

weak_alias(__mmap, mmap);
