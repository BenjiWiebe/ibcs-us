/*
 *     abi/uw7/context.c
 *
 *  This software is under GPL
 */
#include <ibcs-us/ibcs/linux26-compat.h>
#include <ibcs-us/ibcs/sysent.h>
#include <ibcs-us/ibcs/trace.h>
#include <ibcs-us/per-uw7/context.h>

#include <ibcs-us/linux26-compat/asm/uaccess.h>
#include <ibcs-us/linux26-compat/linux/errno.h>
#include <ibcs-us/linux26-compat/linux/ptrace.h>
#include <ibcs-us/linux26-compat/linux/signal.h>



int
uw7_sigaltstack(const uw7_stack_t *uw7_ss, uw7_stack_t *uw7_oss)
{
	stack_t ss, oss, *ssp = NULL, *ossp = NULL;
	int error;
	mm_segment_t old_fs;

	if (uw7_ss) {
		if (!access_ok(VERIFY_READ, uw7_ss, sizeof(uw7_stack_t)))
			return -EFAULT;
		__get_user(ss.ss_sp, &uw7_ss->ss_sp);
		__get_user(ss.ss_size, &uw7_ss->ss_size);
		__get_user(ss.ss_flags, &uw7_ss->ss_flags);
		ssp = &ss;
	}

	if (uw7_oss) {
		if (!access_ok(VERIFY_WRITE, uw7_oss, sizeof(uw7_stack_t)))
			return -EFAULT;
		__get_user(oss.ss_sp, &uw7_oss->ss_sp);
		__get_user(oss.ss_size, &uw7_oss->ss_size);
		__get_user(oss.ss_flags, &uw7_oss->ss_flags);
		ossp = &oss;
	}

	old_fs = get_fs();
	set_fs(get_ds());
	error = SYS_NATIVE(sigaltstack,ssp, ossp);
	set_fs(old_fs);

	if (ossp) {
		__put_user(ossp->ss_sp, &uw7_oss->ss_sp);
		__put_user(ossp->ss_size, &uw7_oss->ss_size);
		__put_user(ossp->ss_flags, &uw7_oss->ss_flags);
	}
	return error;
}

static int
getcontext(uw7_context_t * uc, struct pt_regs * regs)
{
	uw7_context_t tmp = { 0 };

	return copy_to_user(uc, &tmp, sizeof(uw7_context_t)) ? -EFAULT : 0;
}

static int
getxcontext(uw7_context_t * uc, struct pt_regs * regs)
{
	return 0;
}

static int
setcontext(uw7_context_t * uc, struct pt_regs * regs)
{
	if (!uc) /* SVR4 says setcontext(NULL) => exit(0) */
		SYS(exit,0);
	return 0;
}

int
uw7_context(struct pt_regs * regs)
{
	int fcn = get_syscall_parameter(regs, 0);
	uw7_context_t * uc = (uw7_context_t *) get_syscall_parameter(regs, 1);

	switch (fcn) {
		case UW7_GETCONTEXT:
			return getcontext(uc, regs);

		case UW7_GETXCONTEXT:
			return getxcontext(uc, regs);

		case UW7_SETCONTEXT:
			return setcontext(uc, regs);
	}
	return -EINVAL;
}
