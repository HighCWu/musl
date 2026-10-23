#define _GNU_SOURCE
#include <string.h>
#include "pthread_impl.h"
#include "syscall.h"
#ifdef __wasm__
#include <setjmp.h>
#endif

hidden long __cancel(), __syscall_cp_c();
#ifdef __wasm__
__attribute__((import_module("linux_signal_cancel_v1"), import_name("abi")))
hidden int __wasm_signal_cancel_abi(void);
#endif

long __cancel()
{
	pthread_t self = __pthread_self();
	if (self->canceldisable == PTHREAD_CANCEL_ENABLE || self->cancelasync)
		pthread_exit(PTHREAD_CANCELED);
	self->canceldisable = PTHREAD_CANCEL_DISABLE;
	return -ECANCELED;
}

#ifdef __wasm__
/* Keep SjLj setup out of disabled cancellation (including malloc's locks).
 * LLVM may allocate activation metadata for a setjmp-containing function. */
static __attribute__((noinline)) long wasm_cancellable_syscall(pthread_t self,
	syscall_arg_t nr, syscall_arg_t u, syscall_arg_t v, syscall_arg_t w,
	syscall_arg_t x, syscall_arg_t y, syscall_arg_t z)
{
	void *previous = self->wasm_cancelpoint;
	jmp_buf cancel_jump;
	if (setjmp(cancel_jump)) {
		self->wasm_cancelpoint = previous;
		return __cancel();
	}
	/* Cancellation may have arrived during the SjLj runtime's setup. */
	if (self->cancel) return __cancel();
	self->wasm_cancelpoint = nr == SYS_close ? 0 : cancel_jump;
	long result = __syscall(nr, u, v, w, x, y, z);
	self->wasm_cancelpoint = previous;
	return result;
}
#endif

#ifndef __wasm__
hidden long __syscall_cp_asm();

long __syscall_cp_asm(volatile void *, syscall_arg_t,
                      syscall_arg_t, syscall_arg_t, syscall_arg_t,
                      syscall_arg_t, syscall_arg_t, syscall_arg_t);
#endif

long __syscall_cp_c(syscall_arg_t nr,
                    syscall_arg_t u, syscall_arg_t v, syscall_arg_t w,
                    syscall_arg_t x, syscall_arg_t y, syscall_arg_t z)
{
	pthread_t self;
	long r;
	int st;

	if ((st=(self=__pthread_self())->canceldisable)
	    && (st==PTHREAD_CANCEL_DISABLE || nr==SYS_close))
		return __syscall(nr, u, v, w, x, y, z);

#ifdef __wasm__
	if (self->cancel) return __cancel();
	/* The host must also bracket one-argument handlers. Older hosts reject
	 * this private import rather than silently exposing an unsafe marker. */
	if (__wasm_signal_cancel_abi() != 1) return -ENOSYS;
	r = nr == SYS_close ? __syscall(nr, u, v, w, x, y, z) :
		wasm_cancellable_syscall(self, nr, u, v, w, x, y, z);
	if (nr!=SYS_close && self->cancel &&
	    self->canceldisable != PTHREAD_CANCEL_DISABLE)
		r = __cancel();
#else
	r = __syscall_cp_asm(&self->cancel, nr, u, v, w, x, y, z);
	if (r==-EINTR && nr!=SYS_close && self->cancel &&
	    self->canceldisable != PTHREAD_CANCEL_DISABLE)
		r = __cancel();
#endif
	return r;
}

static void _sigaddset(sigset_t *set, int sig)
{
	unsigned s = sig-1;
	set->__bits[s/8/sizeof *set->__bits] |= 1UL<<(s&8*sizeof *set->__bits-1);
}

static void cancel_handler(int sig, siginfo_t *si, void *ctx)
{
	pthread_t self = __pthread_self();
	ucontext_t *uc = ctx;
#ifndef __wasm__
	uintptr_t pc = uc->uc_mcontext.MC_PC;
#endif

	a_barrier();
	if (!self->cancel || self->canceldisable == PTHREAD_CANCEL_DISABLE) return;

	_sigaddset(&uc->uc_sigmask, SIGCANCEL);

	if (self->cancelasync) {
		pthread_sigmask(SIG_SETMASK, &uc->uc_sigmask, 0);
		__cancel();
	}

#ifdef __wasm__
	/* Return to the cancellation wrapper before SA_RESTART can restart a
	 * blocked call. Masked cancellation (cond_wait) must return ECANCELED
	 * there, not exit from this handler before reacquiring its mutex. */
	if (self->wasm_interrupted_cancelpoint) {
		pthread_sigmask(SIG_SETMASK, &uc->uc_sigmask, 0);
		longjmp(self->wasm_interrupted_cancelpoint, 1);
	}
	return;
#else
	extern hidden const char __cp_begin[1], __cp_end[1], __cp_cancel[1];

	if (pc >= (uintptr_t)__cp_begin && pc < (uintptr_t)__cp_end) {
		uc->uc_mcontext.MC_PC = (uintptr_t)__cp_cancel;
#ifdef CANCEL_GOT
		uc->uc_mcontext.MC_GOT = CANCEL_GOT;
#endif
		return;
	}

	__syscall(SYS_tkill, self->tid, SIGCANCEL);
#endif
}

void __testcancel()
{
	pthread_t self = __pthread_self();
	if (self->cancel && !self->canceldisable)
		__cancel();
}

static void init_cancellation()
{
	struct sigaction sa = {
		.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK,
		.sa_sigaction = cancel_handler
	};
	memset(&sa.sa_mask, -1, _NSIG/8);
	__libc_sigaction(SIGCANCEL, &sa, 0);
}

int pthread_cancel(pthread_t t)
{
	static int init;
	if (!init) {
		init_cancellation();
		init = 1;
	}
	a_store(&t->cancel, 1);
	if (t == pthread_self()) {
		if (t->canceldisable == PTHREAD_CANCEL_ENABLE && t->cancelasync)
			pthread_exit(PTHREAD_CANCELED);
		return 0;
	}
	return pthread_kill(t, SIGCANCEL);
}
