#include <sched.h>
#include <errno.h>
#include "syscall.h"

int sched_setscheduler(pid_t pid, int sched, const struct sched_param *param)
{
#ifdef __wasm__
	return __syscall_ret(__syscall(SYS_sched_setscheduler, pid, sched, param));
#else
	return __syscall_ret(-ENOSYS);
#endif
}
