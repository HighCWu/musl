#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <malloc.h>
#include <unistd.h>
#ifdef __wasm__
#include <errno.h>
#include <stdint.h>
#endif
#include "syscall.h"

#ifdef __wasm__
const char unsigned *__map_file(const char *pathname, size_t *size)
{
	struct stat st;
	const unsigned char *map = MAP_FAILED;
	int fd = sys_open(pathname, O_RDONLY|O_CLOEXEC|O_NONBLOCK);
	if (fd < 0) return 0;
	if (!__fstat(fd, &st) && st.st_size > 0 &&
	    (uintmax_t)st.st_size <= SIZE_MAX) {
		/* Direct NOMMU file mappings are private copies. Shared aliases
		 * remain unsupported. mmap/munmap are not cancellation points;
		 * preserve the existing retry policy while holding locale locks.
		 */
		do {
			map = __mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		} while (map == MAP_FAILED && errno == EINTR);
		if (map != MAP_FAILED) *size = st.st_size;
	}
	__syscall(SYS_close, fd);
	return map == MAP_FAILED ? 0 : map;
}
void __unmap_file(const char *map, size_t size)
{
	__munmap((void *)map, size);
}
#else
const char unsigned *__map_file(const char *pathname, size_t *size)
{
	struct stat st;
	const unsigned char *map = MAP_FAILED;
	int fd = sys_open(pathname, O_RDONLY|O_CLOEXEC|O_NONBLOCK);
	if (fd < 0) return 0;
	if (!__fstat(fd, &st)) {
		map = __mmap(0, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
		*size = st.st_size;
	}
	__syscall(SYS_close, fd);
	return map == MAP_FAILED ? 0 : map;
}
void __unmap_file(const char *map, size_t size)
{
	__munmap(map, size);
}
#endif


