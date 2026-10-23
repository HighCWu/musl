#include <sched.h>
#include <errno.h>
#include "syscall.h"

int sched_setparam(pid_t pid, const struct sched_param *param)
{
#ifdef __wasm__
	return __syscall_ret(__syscall(SYS_sched_setparam, pid, param));
#else
	return __syscall_ret(-ENOSYS);
#endif
}
