/*
 * misc.c - misc cxenix() subcalls
 *
 * Copyright (c) 1993,1994 Drew Sullivan
 * Copyright (c) 1994-1996 Mike Jagdis
 */
#include <ibcs-us/ibcs/sysent.h>
#include <ibcs-us/ibcs/trace.h>
#include <ibcs-us/ibcs/linux26-compat.h>
#include <ibcs-us/per-svr4/sigaction.h>
#include <ibcs-us/per-svr4/y2k.h>

#include <ibcs-us/linux26-compat/asm/uaccess.h>
#include <ibcs-us/linux26-compat/linux/errno.h>
#include <ibcs-us/linux26-compat/linux/fcntl.h>
#if _KSL > 25
#include <ibcs-us/linux26-compat/linux/fdtable.h>
#endif
#include <ibcs-us/linux26-compat/linux/file.h>
#include <ibcs-us/linux26-compat/linux/fs.h>
#include <ibcs-us/linux26-compat/linux/kernel.h>
#include <ibcs-us/linux26-compat/linux/module.h>
#include <ibcs-us/linux26-compat/linux/param.h>
#include <ibcs-us/linux26-compat/linux/signal.h>
#include <ibcs-us/linux26-compat/linux/termios.h>
#include <ibcs-us/linux26-compat/linux/time.h>
#include <ibcs-us/linux26-compat/linux/types.h>


struct timeb {
	time_t	time;
	u_short	millitm;
	short	timezone;
	short	dstflag;
};

enum {
	XF_UNLCK =	0,
	XF_WRLCK =	1,
	XF_RDLCK =	3,
};

struct ibcs_flock {
	short l_type;
	short l_whence;
	off_t l_start;
	off_t l_len;
	short l_sysid;
	short l_pid;
};


/*
 * locking() requires mandatory locking. Processes that attempt to
 * read or write a region locked with locking() are required to block.
 * You need to build a kernel with mandatory locking support and set
 * the permissions on the required file to setgid, no group execute.
 */
int
xnx_locking(int fd, int mode, unsigned long size)
{
	struct flock fl;
	mm_segment_t old_fs;
	int error;

	if ((mode < 0 || mode > 7) && mode != 20) {
#if defined(CONFIG_ABI_TRACE)
		abi_trace(ABI_TRACE_API,
				"unsupported locking() mode=0x%x\n", mode);
#endif
		return -EINVAL;
	}

	/*
	 * Modes 5, 6 & 7 are very like the fcntl mechanism but
	 * we can't just punt to that because the type values are
	 * different.
	 */
	if (mode > 4 && mode < 8) {
		struct ibcs_flock *ifl = (struct ibcs_flock *)size;
		short t;

		if (!access_ok(VERIFY_READ, ifl, sizeof(*ifl)))
			return -EFAULT;

		get_user(t, &ifl->l_type);
		switch (t) {
			case XF_UNLCK:	t = F_UNLCK; break;
			case XF_WRLCK:	t = F_WRLCK; break;
			case XF_RDLCK:	t = F_RDLCK; break;
			default:	return -EINVAL;
		}
		put_user(t, &ifl->l_type);

		error = SYS(fcntl,fd, mode, (u_long)ifl);

		get_user(t, &ifl->l_type);
		switch (t) {
			case F_UNLCK:	t = XF_UNLCK; break;
			case F_WRLCK:	t = XF_WRLCK; break;
			case F_RDLCK:	t = XF_RDLCK; break;
		}
		put_user(t, &ifl->l_type);

		get_user(t, &ifl->l_sysid);
		put_user(t, &ifl->l_pid);
		put_user(0, &ifl->l_sysid);
		return error;
	}

	fl.l_type = (mode == 0 ? F_UNLCK
			: ((mode <= 2 || mode == 20) ? F_WRLCK
			: F_RDLCK));
	fl.l_whence = 1;
	fl.l_start = 0;
	fl.l_len = size;

	old_fs = get_fs();
	set_fs (get_ds());
#ifdef _KSL_IBCS_US
	/*
	 * According to Johns below this doesn't work in all cases but
	 * IBCS_US can't change the read/write mode so this is going to
	 * have to do.
	 */
	error = SYS(fcntl,fd, (mode == 5) ? F_GETLK
			: (!(mode % 2) ? F_SETLK : F_SETLKW), (u_long)&fl);
#else 
 // Johns: Xenix locking didn't work with just created files 
 // Taken from old ibcs2 code, adjusted for 2.6 
	{ 
	struct file *filep; 
	error = -EBADF; 
	if ((filep = fget(fd))) { 
		mode_t old_mode = filep->f_mode; 
 
		filep->f_mode |= FMODE_WRITE | FMODE_READ; 
		error = SYS(fcntl,fd, (mode == 5) ? F_GETLK 
			: (!(mode % 2) ? F_SETLK : F_SETLKW), (u_long)&fl); 
		filep->f_mode = old_mode; 
	} 
 	} 
#endif 
	set_fs(old_fs);
	return error;
}


/* Check if input is available */
int
xnx_rdchk(int fd)
{
	int error, nbytes;
	mm_segment_t old_fs;

	old_fs = get_fs();
	set_fs (get_ds());
	error = SYS_NATIVE(ioctl,fd, FIONREAD, (long)&nbytes);
	set_fs(old_fs);

	if (error < 0) return error;
	return nbytes ? 1 : 0;
}

/*
 * Linux has a stub sys_ftime. Perhaps this should be there? On the other
 * hand it's an old call that probably shouldn't be used by most modern
 * applications so perhaps it's better here where it needn't bloat the
 * base kernel.
 */
int
xnx_ftime(struct timeb *tp)
{
	struct timeval tv;
	struct timezone tz;
	int error;
	mm_segment_t old_fs;

	if (!access_ok(VERIFY_WRITE, tp, sizeof(struct timeb)))
		return -EFAULT;

	old_fs = get_fs();
	set_fs (get_ds());
	error = SYS_NATIVE(gettimeofday,&tv, &tz);
	set_fs(old_fs);
	if (error) return error;

	tv.tv_sec = svr4_y2k_send(tv.tv_sec);
	put_user(tv.tv_sec, &tp->time);
	put_user((unsigned short)(tv.tv_usec/1000), &tp->millitm);
	put_user((short)tz.tz_minuteswest, &tp->timezone);
	put_user((short)tz.tz_dsttime, &tp->dstflag);

	return 0;
}

#define USE_NEW_NAP_CODE

#ifndef USE_NEW_NAP_CODE
static __inline __sighandler_t
sigaction(int sig, __sighandler_t handler)
{
	struct k_sigaction *k = &current->sighand->action[sig-1];
	__sighandler_t old_handler;

	spin_lock(&current->sighand->siglock);
	old_handler = k->sa.sa_handler;
	k->sa.sa_handler = handler;
	spin_unlock(&current->sighand->siglock);

	return old_handler;
}
#endif

/* go to sleep for period milliseconds */
/* - returns either an EINTR error or returns the elapsed time */
/* Note:
   for SCO OpenServer 5.0.6 the original nap was fixed so that it
   no longer waits a minimum of 2 tick (20ms)
   but fewer time with a 10 ms granularity */
long
xnx_nap(long period)
{
#ifdef USE_NEW_NAP_CODE
	// Hz means the number of jiffies per second.
	// the below code needs HZ to be 1 <= HZ <= 1000
	// in order to work correctly in any case.
#if HZ > 1000
#error this code only works with HZ <= 1000
#endif
	struct timeval tv1, tv2;
	struct timezone tz;
	mm_segment_t oldfs;
	long period_s; // seconds part
	long period_ms_hz; // milli seconds part, scaled to a base of HZ
	long period_j; // jiffies
	int error;

	if (!period)
		return 0; // zereo request, zero reply

	oldfs = get_fs();
	set_fs(get_ds());
	SYS_NATIVE(gettimeofday,&tv1, &tz);

	period_s = period / 1000;
	period_ms_hz = (period - period_s * 1000) * HZ;
	period_j = period_s * HZ + period_ms_hz / 1000;
	// take care of rounding errors, round up
	if (period > period_j * (1000 / HZ)) period_j++;

#if _KSL_IBCS_US
	error = linux26_schedule_timeout(period_j);
#else
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout (period_j);
	error = signal_pending(current) ? -EINTR : 0;
#endif

	SYS_NATIVE(gettimeofday,&tv2, &tz);
	set_fs(oldfs);

	if (error < 0) {
		return error;
	}
	return (tv2.tv_sec - tv1.tv_sec) * 1000
		+ (tv2.tv_usec - tv1.tv_usec + 500) / 1000;
#else
	__sighandler_t old_handler;
	struct itimerval it;
	struct timeval tv1, tv2;
	struct timezone tz;
	mm_segment_t oldfs;

	if (!period)
		return 0;

	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = 0;
	it.it_value.tv_sec = 0;
	it.it_value.tv_usec = period * 1000;

	oldfs = get_fs();
	set_fs(get_ds());

	SYS_NATIVE(gettimeofday,&tv1, &tz);
	old_handler = sigaction(SIGALRM, SIG_DFL); // SIG_DFL -> terminate
	SYS_NATIVE(setitimer,ITIMER_REAL, &it, NULL);
	SYS(pause);
	sigaction(SIGALRM, old_handler);
	SYS_NATIVE(gettimeofday,&tv2, &tz);
	set_fs(oldfs);

	deactivate_signal(current, SIGALRM);

	if (signal_pending(current))
		return -EINTR;
	return ((tv2.tv_sec - tv1.tv_sec) * 1000000
			+ (tv2.tv_usec - tv1.tv_usec)) / 1000;
#endif
}

/*
 * eaccess() checks access to the given path using the effective
 * uid/gid rather than the real uid/gid.
 */
int
xnx_eaccess(char *path, int mode)
{
#if _KSL_IBCS_US
  	return (int)SYS(faccessat, AT_FDCWD, path, mode, AT_EACCESS);
#else
	uid_t		ouid;
	gid_t		ogid;
	int		err;
#if _KSL > 28
        struct cred *cred = (struct cred *)(current->cred);
#else
        struct task_struct *cred = current;
#endif

	ouid = cred->uid;
	ogid = cred->gid;
	cred->uid = cred->euid;
	cred->gid = cred->egid;

	err = SYS(access,path, mode);

	cred->uid = ouid;
	cred->gid = ogid;

	return err;
#endif
}
