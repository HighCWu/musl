#include <sys/ptrace.h>
#include <stdarg.h>
#include <unistd.h>
#include "syscall.h"

long ptrace(int req, ...)
{
	va_list ap;
	pid_t pid;
	void *addr, *data, *addr2 = 0;
	long ret, result;

	va_start(ap, req);
	pid = va_arg(ap, pid_t);
	addr = va_arg(ap, void *);
	data = va_arg(ap, void *);
	/* PTRACE_{READ,WRITE}{DATA,TEXT} (16...19) are specific to SPARC. */
#ifdef PTRACE_READDATA
	if ((unsigned)req - PTRACE_READDATA < 4)
		addr2 = va_arg(ap, void *);
#endif
	va_end(ap);

	if (req-1U < 3) data = &result;
#ifdef __wasm__
	/* The imported Wasm syscall has machine-word arguments. In particular,
	 * wasm64 cannot recover promoted int arguments from a long va_list. */
	ret = __syscall_ret(__syscall(SYS_ptrace, req, pid, addr, data, addr2));
#else
	ret = syscall(SYS_ptrace, req, pid, addr, data, addr2);
#endif

	if (ret < 0 || req-1U >= 3) return ret;
	return result;
}
