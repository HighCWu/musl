#include <sched.h>
#include <errno.h>
#include "syscall.h"

int sched_getparam(pid_t pid, struct sched_param *param)
{
#ifdef __wasm__
	return __syscall_ret(__syscall(SYS_sched_getparam, pid, param));
#else
	return __syscall_ret(-ENOSYS);
#endif
}
