struct kstat {
	dev_t st_dev;
	ino_t st_ino;
	mode_t st_mode;
	/* Linux asm-generic stat/stat64 use 32-bit fields on both profiles,
	 * independently of the public musl nlink_t and blksize_t widths. */
	unsigned int st_nlink;
	uid_t st_uid;
	gid_t st_gid;
	dev_t st_rdev;
	unsigned long long __pad;
	off_t st_size;
	int st_blksize;
	int __pad2;
	blkcnt_t st_blocks;
	long st_atime_sec;
	long st_atime_nsec;
	long st_mtime_sec;
	long st_mtime_nsec;
	long st_ctime_sec;
	long st_ctime_nsec;
	unsigned __unused[2];
};
