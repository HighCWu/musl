#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <malloc.h>
#include <unistd.h>
#ifdef __wasm__
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#endif
#include "syscall.h"

#ifdef __wasm__
const char unsigned *__map_file(const char *pathname, size_t *size)
{
	struct stat st;
	unsigned char *buf = 0;
	int fd = sys_open(pathname, O_RDONLY|O_CLOEXEC|O_NONBLOCK);
	if (fd < 0) return 0;
	if (__fstat(fd, &st) || st.st_size <= 0 || (uintmax_t)st.st_size > SIZE_MAX)
		goto out;
	size_t length = st.st_size, done = 0;
	buf = malloc(length);
	if (!buf) goto out;
	while (done < length) {
		size_t count = length - done;
		if (count > SSIZE_MAX) count = SSIZE_MAX;
		/* Like the mmap path, loading under the timezone lock must not
		 * introduce a cancellation point. Retry interrupted/short reads.
		 */
		long n = __syscall(SYS_read, fd, buf + done, count);
		if (n == -EINTR) continue;
		if (n <= 0) {
			free(buf);
			buf = 0;
			goto out;
		}
		done += n;
	}
	*size = length;
out:
	__syscall(SYS_close, fd);
	return buf;
}
void __unmap_file(const char *map, size_t size)
{
	free((void *)map);
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


