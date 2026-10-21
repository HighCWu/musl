#define _GNU_SOURCE
#include <features.h>
#include <signal.h>
#include <stdint.h>

__attribute__((import_module("linux"), import_name("copy_siginfo")))
int __wasm_copy_siginfo(siginfo_t *);

__attribute__((import_module("linux_signal_v1"), import_name("frame")))
int __wasm_copy_signal_frame(stack_t *, sigset_t *);

hidden void __restore(void)
{
}

/* Signal delivery is a synchronous host callback, so there is no native
 * signal frame to restore. Instead, the kernel calls this restorer as the
 * userspace trampoline. It materializes the public objects before invoking
 * the application's three-argument handler. */
hidden void __restore_rt(uintptr_t fn, int sig)
{
	void (*handler)(int, siginfo_t *, void *) = (void *)fn;
	siginfo_t info;

	/* The engine's register state is opaque. The versioned execution ABI
	 * nevertheless supplies the interrupted mask and stack registration;
	 * do not present fabricated native registers to the application. */
	ucontext_t context = {0};

	if (__wasm_copy_siginfo(&info)) return;
	/* Older kernels keep the previous intentionally minimal context. */
	int result = __wasm_copy_signal_frame(&context.uc_stack, &context.uc_sigmask);
	if (result && result != -95) return;

	handler(sig, &info, &context);
}
