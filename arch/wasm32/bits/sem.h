/* SPDX-License-Identifier: MIT */
/* ILP32 follows musl's existing riscv32 time64 prefix; LP64 stays generic. */
struct semid_ds {
	struct ipc_perm sem_perm;
#if __SIZEOF_LONG__ == 4
	unsigned long __sem_otime_lo;
	unsigned long __sem_otime_hi;
	unsigned long __sem_ctime_lo;
	unsigned long __sem_ctime_hi;
#else
	time_t sem_otime;
	time_t sem_ctime;
#endif
	unsigned short sem_nsems;
	char __sem_nsems_pad[sizeof(long)-sizeof(short)];
	long __unused3;
	long __unused4;
#if __SIZEOF_LONG__ == 4
	time_t sem_otime;
	time_t sem_ctime;
#endif
};
