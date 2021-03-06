/*
 * Copyright (c) 2001 Caldera Deutschland GmbH.
 * Copyright (c) 2001 Christoph Hellwig.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef _IBCS_US_SVR4_TYPES_H
#define _IBCS_US_SVR4_TYPES_H

//#ident "%W% %G%"

/*
 * SVR4 type declarations.
 */
#include <ibcs-us/linux26-compat/linux/highuid.h>
#include <ibcs-us/linux26-compat/linux/kdev_t.h>
#include <ibcs-us/linux26-compat/linux/personality.h>
#include <ibcs-us/linux26-compat/linux/sched.h>
#include <ibcs-us/linux26-compat/linux/time.h>
#include <ibcs-us/linux26-compat/linux/types.h>

/*
 * XXX	this will need additions if we support emulation of
 * XXX	64bit SVR4 on 64bit Linux
 */

/* SVR4 (32bit) data types */
#define SVR4_MINORBITS 18
#define SVR4_MINORMASK ((1U << SVR4_MINORBITS) - 1)
#define SVR4_MAJOR(dev) ((unsigned int) ((dev) >> SVR4_MINORBITS))
#define SVR4_MINOR(dev) ((unsigned int) ((dev) & SVR4_MINORBITS))
#define SVR4_MKDEV(ma,mi) (((ma) << SVR4_MINORBITS) | (mi))
typedef u_int32_t	svr4_dev_t; /* Major:Minor = 14:18 */
typedef u_int32_t	svr4_ino_t;
typedef u_int32_t	svr4_mode_t;
typedef u_int32_t	svr4_nlink_t;
typedef int32_t		svr4_uid_t;
typedef int32_t		svr4_gid_t;
typedef int32_t		svr4_off_t;
typedef int32_t		svr4_time_t;
typedef struct timeval	svr4_timestruc_t;

/* SVR4 old == SVR3 data types */
#define SVR4_O_MINORBITS 8
#define SVR4_O_MINORMASK ((1U << SVR4_O_MINORBITS) - 1)
#define SVR4_O_MAJOR(dev) ((unsigned int) ((dev) >> SVR4_O_MINORBITS))
#define SVR4_O_MINOR(dev) ((unsigned int) ((dev) & SVR4_O_MINORBITS))
#define SVR4_O_MKDEV(ma,mi) (((ma) << SVR4_O_MINORBITS) | (mi))
typedef int16_t		svr4_o_dev_t; /* Major:Minor = 8:8 */
typedef int16_t		svr4_o_pid_t;
typedef u_int16_t	svr4_o_ino_t;
typedef u_int16_t	svr4_o_mode_t;
typedef int16_t		svr4_o_nlink_t;
typedef u_int16_t	svr4_o_uid_t;
typedef u_int16_t	svr4_o_gid_t;

/*
 * Convert a linux dev number into the SVR4 equivalent.
 */
static __inline svr4_dev_t
linux_to_svr4_dev_t(dev_t dev)
{
	return SVR4_MKDEV (MAJOR (dev), MINOR (dev));
}

/*
 * SVR4 old (=SVR3) dev_t
 * This is the same as linux 2.4.xx but different from linux 2.6.xx.
 */
static __inline svr4_o_dev_t
linux_to_svr4_o_dev_t(dev_t dev)
{
	return SVR4_O_MKDEV (MAJOR (dev), MINOR (dev));
}


#ifdef CONFIG_ABI_SHINOMAP
extern ino_t short_inode_map(ino_t);
#endif

/*
 * If we thought we were in a short inode environment we are
 * probably already too late - getdents() will have likely
 * already assumed short inodes and "fixed" anything with
 * a zero low word (because it must match stat() which must
 * match read() on a directory).
 *
 * We will just have to go along with it.
 */
/*
 * UPDATE: if ibcs was compiles with ABI_SHINOMAP=Y and
 * abi_util was loaded with short_inode_mapping=1,
 * we will use short_inode_map() to return conflict free 
 * 16-bit inode numbers under optimum conditions (less than
 * 65536 different files opened since module load).
 */

static __inline svr4_ino_t
linux_to_svr4_ino_t(ino_t ino)
{
	if ( !(current->personality & SHORT_INODE) )
		return ino;

#ifdef CONFIG_ABI_SHINOMAP
	return (svr4_ino_t)short_inode_map(ino);
#else
	if ((u_long)ino & 0xffff)
		return ino;
	return 0xfffffffe;
#endif
}

/*
 * Old SVR4 ino_t _must_ be in a short inode enviroment.
 */
static __inline svr4_o_ino_t
linux_to_svr4_o_ino_t(ino_t ino)
{
#ifdef CONFIG_ABI_SHINOMAP
	return (svr4_ino_t)short_inode_map(ino);
#else
	if ((u_long)ino & 0xffff)
		return (svr4_o_ino_t)ino;
	return 0xfffe;
#endif
}

/*
 * SVR4 UIDs/GIDs are the same as current Linux ones,
 * old (SVR3) ones are the same as old Linux ones.
 */
static __inline svr4_uid_t
linux_to_svr4_uid_t(uid_t uid)
{
	return uid;
}

static __inline svr4_o_uid_t
linux_to_svr4_o_uid_t(uid_t uid)
{
	return high2lowuid(uid);
}

static __inline svr4_gid_t
linux_to_svr4_gid_t(gid_t gid)
{
	return gid;
}

static __inline svr4_o_gid_t
linux_to_svr4_o_gid_t(gid_t gid)
{
	return high2lowgid(gid);
}

#endif /* _IBCS_US_SVR4_TYPES_H */
