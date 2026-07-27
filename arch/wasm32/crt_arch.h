#include "process_args.h"
#include "syscall.h"

// The wasm process ABI caps the complete argument blob at four 64 KiB pages.
static char args_buf[65536 * 4];
hidden struct wasm_process_args *__wasm_process_args = (void*)args_buf;

#ifdef START_is_dlstart
hidden void _dlstart_c(size_t *sp, size_t *dynv);
hidden void _dlstart(void) {
	_dlstart_c(0, 0);
}
#endif

#ifdef START_is_start
void __wasm_call_ctors(void);
int __main_void(void);
hidden void _start_c(long *p);
hidden void _start(void) {
	if (__syscall(SYS_wasm_get_args, __wasm_process_args,
	    sizeof(args_buf)) < 0) __builtin_trap();

	__init_libc(__wasm_process_args->envp, __wasm_process_args->argv[0]);
	__libc_start_init();
	__wasm_call_ctors();

	exit(__main_void());
}
#endif
