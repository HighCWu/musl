/* SPDX-License-Identifier: MIT */
/* ILP32 follows musl's existing riscv32 time64 prefix; LP64 stays generic. */
struct msqid_ds {
	struct ipc_perm msg_perm;
#if __SIZEOF_LONG__ == 4
	unsigned long __msg_stime_lo;
	unsigned long __msg_stime_hi;
	unsigned long __msg_rtime_lo;
	unsigned long __msg_rtime_hi;
	unsigned long __msg_ctime_lo;
	unsigned long __msg_ctime_hi;
#else
	time_t msg_stime;
	time_t msg_rtime;
	time_t msg_ctime;
#endif
	unsigned long msg_cbytes;
	msgqnum_t msg_qnum;
	msglen_t msg_qbytes;
	pid_t msg_lspid;
	pid_t msg_lrpid;
	unsigned long __unused[2];
#if __SIZEOF_LONG__ == 4
	time_t msg_stime;
	time_t msg_rtime;
	time_t msg_ctime;
#endif
};
