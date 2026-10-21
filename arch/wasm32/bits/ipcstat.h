/* SPDX-License-Identifier: MIT */
/* Existing musl time64 IPC conversion convention on 32-bit targets. */
#if __SIZEOF_LONG__ == 4
#define IPC_STAT 0x102
#else
#define IPC_STAT 2
#endif
