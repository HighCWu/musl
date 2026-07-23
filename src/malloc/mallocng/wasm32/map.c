#define _BSD_SOURCE
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "../meta.h"

struct free_map {
	struct free_map *next;
	size_t len;
};

/*
 * mallocng normally gets these regions from mmap. wasm instead owns one
 * sbrk-backed arena whose free extents are address-ordered and coalesced.
 * Every entry point is called with mallocng's global lock held.
 */
static struct free_map *free_maps;

void *__malloc_map_alloc(size_t len)
{
	struct free_map **best = 0;

	assert(len >= sizeof(struct free_map));
	assert(!(len & 4095));

	for (struct free_map **link = &free_maps; *link;
	     link = &(*link)->next) {
		if ((*link)->len < len)
			continue;
		if (!best || (*link)->len < (*best)->len)
			best = link;
	}

	if (!best) {
		/* Fresh wasm linear memory has anonymous mmap's zero-fill semantics. */
		if (len > INTPTR_MAX) {
			errno = ENOMEM;
			return (void *)-1;
		}
		return sbrk((intptr_t)len);
	}

	struct free_map *map = *best;
	void *result;
	if (map->len == len) {
		*best = map->next;
		result = map;
	} else {
		map->len -= len;
		result = (char *)map + map->len;
	}

	/* mallocng expects mapped groups to start with anonymous mmap's zeroes. */
	memset(result, 0, len);
	return result;
}

void __malloc_map_free(void *base, size_t len)
{
	uintptr_t start = (uintptr_t)base;
	uintptr_t end = start + len;
	struct free_map **link = &free_maps;
	struct free_map *previous = 0;
	struct free_map *next;
	struct free_map *map;

	assert(len >= sizeof(struct free_map));
	assert(!(len & 4095));
	assert(!(start & (UNIT-1)));
	assert(end >= start);

	while (*link && (uintptr_t)*link < start) {
		previous = *link;
		link = &previous->next;
	}
	next = *link;

	assert(!previous ||
		(uintptr_t)previous + previous->len <= start);
	assert(!next || end <= (uintptr_t)next);

	if (previous && (uintptr_t)previous + previous->len == start) {
		map = previous;
		map->len += len;
	} else {
		map = base;
		map->next = next;
		map->len = len;
		*link = map;
	}

	if (next && (uintptr_t)map + map->len == (uintptr_t)next) {
		map->next = next->next;
		map->len += next->len;
	}
}
