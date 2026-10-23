#define _GNU_SOURCE
#include <features.h>
#include <signal.h>
#include <stdint.h>
#include "pthread_impl.h"

__attribute__((import_module("linux"), import_name("copy_siginfo")))
int __wasm_copy_siginfo(siginfo_t *);

__attribute__((import_module("linux_signal_v1"), import_name("frame")))
int __wasm_copy_signal_frame(stack_t *, sigset_t *);
__attribute__((import_module("linux_signal_v1"), import_name("finish")))
int __wasm_finish_signal_frame(const stack_t *, const sigset_t *);

/* Existing C-private restorer, exported only as a versioned execution entry.
 * No public header or application-facing function is introduced. */
__attribute__((export_name("__wasm_signal_handler_v2")))
hidden void __restore(uintptr_t fn, int sig)
{
	struct pthread *self = __pthread_self();
	void *point = self->wasm_cancelpoint;
	void *interrupted = self->wasm_interrupted_cancelpoint;
	self->wasm_cancelpoint = 0;
	self->wasm_interrupted_cancelpoint = point;
	((void (*)(int))fn)(sig);
	self->wasm_cancelpoint = point;
	self->wasm_interrupted_cancelpoint = interrupted;
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

	struct pthread *self = __pthread_self();
	void *point = self->wasm_cancelpoint;
	void *interrupted = self->wasm_interrupted_cancelpoint;
	/* A normal handler is not part of the interrupted cancellation point.
	 * Its own cancellation points can establish a new marker. If it escapes
	 * via SjLj, leaving the current marker clear is intentional. */
	self->wasm_cancelpoint = 0;
	self->wasm_interrupted_cancelpoint = point;
	handler(sig, &info, &context);
	self->wasm_cancelpoint = point;
	self->wasm_interrupted_cancelpoint = interrupted;
	/* Snapshot while context is alive. SjLj skips this just like sigreturn. */
	if (!result) __wasm_finish_signal_frame(&context.uc_stack, &context.uc_sigmask);
}
