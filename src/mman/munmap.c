#include <sys/mman.h>
#include "syscall.h"

static void dummy(void) { }
weak_alias(dummy, __vm_wait);

#ifdef __wasm__
hidden int __wasm_munmap(void *, size_t);

int __munmap(void *start, size_t len)
{
	return __wasm_munmap(start, len);
}
#else
int __munmap(void *start, size_t len)
{
	__vm_wait();
	return syscall(SYS_munmap, start, len);
}
#endif

weak_alias(__munmap, munmap);
