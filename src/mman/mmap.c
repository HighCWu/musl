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
	void *address;
	size_t length;
};

struct wasm_mapping {
	struct wasm_mapping *next;
	struct wasm_allocation *allocation;
	void *address;
	size_t length;
};

static pthread_mutex_t mappings_lock = PTHREAD_MUTEX_INITIALIZER;
static struct wasm_mapping *mappings;

static int wasm_try_mmap_hint(uintptr_t requested, size_t rounded,
			      int exact, struct wasm_mapping *mapping,
			      long *result)
{
	struct wasm_allocation *allocation = 0;
	struct wasm_mapping *current;
	uintptr_t address, end;
	int lock_error;

	if (!requested)
		return exact ? -ENOMEM : 0;
	address = requested & -(uintptr_t)PAGESIZE;
	if (address >= (uintptr_t)-4095 || rounded > UINTPTR_MAX - address)
		return exact ? -ENOMEM : 0;
	end = address + rounded;

	lock_error = pthread_mutex_lock(&mappings_lock);
	if (lock_error)
		return -lock_error;
	for (current = mappings; current; current = current->next) {
		struct wasm_allocation *candidate = current->allocation;
		uintptr_t allocation_begin = (uintptr_t)candidate->address;

		if (address >= allocation_begin && rounded <= candidate->length &&
		    address - allocation_begin <= candidate->length - rounded) {
			allocation = candidate;
			break;
		}
	}
	if (!allocation) {
		(void)pthread_mutex_unlock(&mappings_lock);
		return exact ? -ENOMEM : 0;
	}
	for (current = mappings; current; current = current->next) {
		uintptr_t mapping_begin = (uintptr_t)current->address;
		uintptr_t mapping_end = mapping_begin + current->length;

		if (address < mapping_end && mapping_begin < end) {
			(void)pthread_mutex_unlock(&mappings_lock);
			return exact ? -EEXIST : 0;
		}
	}

	memset((void *)address, 0, rounded);
	allocation->references++;
	mapping->allocation = allocation;
	mapping->address = (void *)address;
	mapping->length = rounded;
	mapping->next = mappings;
	mappings = mapping;
	(void)pthread_mutex_unlock(&mappings_lock);

	*result = (long)mapping->address;
	return 1;
}

/*
 * The Linux syscall wrapper validates the public mmap arguments, then calls
 * this process-local allocator through the Wasm execution ABI.  Keeping the
 * metadata in user memory makes fork's existing linear-memory copy sufficient
 * and, crucially, shares the same brk-backed malloc arena instead of creating
 * a second allocator that could overlap it.
 */
static long wasm_mmap_direct(uintptr_t requested, size_t rounded, int exact)
{
	struct wasm_mapping *mapping;
	struct wasm_allocation *allocation;
	uintptr_t allocation_start, address;
	size_t total;
	long result;
	int hint_status, lock_error;

	if (!rounded || (rounded & (PAGESIZE - 1)) ||
	    rounded > SIZE_MAX - PAGESIZE - sizeof(*allocation))
		return -ENOMEM;
	mapping = __libc_malloc(sizeof(*mapping));
	if (!mapping)
		return errno ? -errno : -ENOMEM;
	hint_status = wasm_try_mmap_hint(requested, rounded, exact, mapping,
					 &result);
	if (hint_status > 0)
		return result;
	if (hint_status < 0) {
		__libc_free(mapping);
		return hint_status;
	}

	total = rounded + PAGESIZE + sizeof(*allocation);
	allocation = __libc_malloc(total);
	if (!allocation) {
		__libc_free(mapping);
		return errno ? -errno : -ENOMEM;
	}

	allocation_start = (uintptr_t)(allocation + 1);
	address = (allocation_start + PAGESIZE - 1) & -(uintptr_t)PAGESIZE;
	if (address >= (uintptr_t)-4095) {
		__libc_free(mapping);
		__libc_free(allocation);
		return -ENOMEM;
	}
	allocation->next_free = 0;
	allocation->references = 1;
	allocation->address = (void *)address;
	allocation->length = rounded;
	mapping->allocation = allocation;
	mapping->address = (void *)address;
	mapping->length = rounded;
	memset(mapping->address, 0, rounded);

	lock_error = pthread_mutex_lock(&mappings_lock);
	if (lock_error) {
		__libc_free(mapping);
		__libc_free(allocation);
		return -lock_error;
	}
	mapping->next = mappings;
	mappings = mapping;
	(void)pthread_mutex_unlock(&mappings_lock);

	return (long)mapping->address;
}

long __wasm_mmap(size_t rounded)
{
	return wasm_mmap_direct(0, rounded, 0);
}

/*
 * Version 2 carries the complete mapping request across the execution ABI.
 * The current allocator still implements only the direct anonymous subset
 * validated by Linux.  A non-fixed address is used when it selects a free
 * page range in backing already reserved by this allocator; otherwise it
 * remains advisory and allocation falls back to the normal path.
 * MAP_FIXED_NOREPLACE uses the same safe range but requires an exact match,
 * returning EEXIST for a live mapping and ENOMEM for an unreserved range.
 * Keeping the old export preserves execution of modules built against the
 * first ABI.
 */
long __wasm_mmap_v2(uintptr_t address, size_t rounded, int prot, int flags,
		    int fd, uintptr_t pgoff)
{
	(void)prot;
	(void)fd;
	(void)pgoff;
	return wasm_mmap_direct(address, rounded,
				flags & MAP_FIXED_NOREPLACE);
}

long __wasm_munmap(uintptr_t begin, size_t rounded)
{
	struct wasm_mapping **cursor, *mapping, *removed = 0;
	struct wasm_mapping *spare;
	struct wasm_allocation *allocation, *released = 0;
	uintptr_t end;
	int lock_error;

	if ((begin & (PAGESIZE - 1)) || !rounded ||
	    (rounded & (PAGESIZE - 1)) || begin > UINTPTR_MAX - rounded)
		return -EINVAL;
	end = begin + rounded;
	spare = __libc_malloc(sizeof(*spare));

	lock_error = pthread_mutex_lock(&mappings_lock);
	if (lock_error) {
		__libc_free(spare);
		return -lock_error;
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
			return -ENOMEM;
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

#endif

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

weak_alias(__mmap, mmap);
