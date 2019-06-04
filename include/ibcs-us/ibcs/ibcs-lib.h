/*
 * Utility stuff ibcs-us provides that isn't provided by the linux 2.6 kernel.
 *
 * Copyright (c) 2019 Russell Stuart <russell+ibcs-us@stuart.id.au>
 * Licensed under the same terms as the Linux 2.6.32 kernel.
 */
#ifndef _IBCS_US_IBCS_LIB_LIB_H
#define _IBCS_US_IBCS_LIB_LIB_H
/*
 * This should be "#include <linux/unistd.h>", but that is broken for gcc
 * 6.3.0 on Debian stretch.
 */
#include <i386-linux-gnu/asm/unistd_32.h>

#include <stdarg.h>
#include <stddef.h>

#include <ibcs-us/linux26-compat/linux/types.h>

#define ARRAY_END(x)	(&(x)[sizeof(x) / sizeof(*(x))])
#define	IBCS_SYSCALL(name, args...) ibcs_syscall(__NR_##name, ##args)
#define	IBCS_IS_ERR(x)	((size_t)(x) > (size_t)-4096)

extern unsigned long	abi_personality(const char *);
extern void		ibcs_fatal(const char* message, ...);
extern void		ibcs_fatal_syscall(int retval, const char* message, ...);
extern void		ibcs_free(void* blk);
extern void*		ibcs_malloc(size_t size);
extern long long	ibcs_syscall(int syscall, ...);
extern int		ibcs_vfmt(char* out, size_t size, const char* fmt, va_list list);
extern long long	ibcs_vsyscall(int syscall, va_list list);
extern int		ibcs_vwritef(int fd, const char* fmt, va_list list);
extern int		ibcs_writef(int fd, const char* fmt, ...);
#endif