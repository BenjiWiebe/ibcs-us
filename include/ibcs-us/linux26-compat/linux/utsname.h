/*
 * IBCS used to be a kernel module, and is still written that way.
 * This file does what the Linux 2.6.32 include file <linux/utsname.h>
 * did, but does it by emulating the same functions in user user space.
 *
 * These files are mostly derived / copied from the linux kernel source,
 * and so are covered by the same licence.
 *
 * Author: Russell Stuart <russell+ibcs@stuart.id.au>
 */
#ifndef _IBCS_US_LINUX26_INCLUDE_LINUX_UTSNAME_H
#define _IBCS_US_LINUX26_INCLUDE_LINUX_UTSNAME_H
#include <linux/utsname.h>

#include <ibcs-us/linux26-compat/linux/rwsem.h>


struct uts_namespace
{
    struct new_utsname name;
};

extern struct uts_namespace init_uts_ns;

extern struct rw_semaphore uts_sem;
#endif
