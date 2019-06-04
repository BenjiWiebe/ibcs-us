/*
 * IBCS used to be a kernel module, and is still written that way.
 * This file does what the Linux 2.6.32 include file <linux/smp_lock.h>
 * did, but does it by emulating the same functions in user user space.
 *
 * These files are mostly derived / copied from the linux kernel source,
 * and so are covered by the same licence.
 *
 * Author: Russell Stuart <russell+ibcs@stuart.id.au>
 */
#ifndef _IBCS_US_LINUX26_INCLUDE_LINUX_SMP_LOCK_H
#define _IBCS_US_LINUX26_INCLUDE_LINUX_SMP_LOCK_H

#define lock_kernel()	do { } while(0)
#define unlock_kernel()	do { } while(0)

#endif