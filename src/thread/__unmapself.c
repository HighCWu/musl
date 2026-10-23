#include "pthread_impl.h"
#include "atomic.h"
#include "syscall.h"
/* cheat and reuse CRTJMP macro from dynlink code */
#include "dynlink.h"

#ifndef __wasm__
static void *unmap_base;
static size_t unmap_size;
static char shared_stack[256];

static void do_unmap()
{
	__syscall(SYS_munmap, unmap_base, unmap_size);
	__syscall(SYS_exit);
}

void __unmapself(void *base, size_t size)
{
	char *stack = shared_stack + sizeof shared_stack;
	stack -= (uintptr_t)stack % 16;
	unmap_base = base;
	unmap_size = size;
	CRTJMP(do_unmap, stack);
}
#else
#if __SIZEOF_POINTER__ == 8
__asm__(".globaltype __stack_pointer, i64\n");
#else
__asm__(".globaltype __stack_pointer, i32\n");
#endif
/* __pthread_exit holds the thread-list lock until the kernel clears its
 * clear_child_tid word. Thus only one exiting thread can use this stack,
 * including while free publishes the old stack/TLS for immediate reuse. */
static unsigned char exit_stack[DEFAULT_STACK_SIZE] __attribute__((aligned(16)));
static void *exit_base;

static __attribute__((noinline, noreturn)) void free_and_exit(void)
{
	__libc_free(exit_base);
	/* Never return through the old caller, even if a tracer skips exit. */
	for (;;) __syscall(SYS_exit, 0);
}

void __unmapself(void *base, size_t size)
{
	(void)size;
	exit_base = base;
	void *top = exit_stack + sizeof exit_stack;
	__asm__ volatile("local.get %0\n"
			 "global.set __stack_pointer" :: "r"(top) : "memory");
	free_and_exit();
}
#endif
