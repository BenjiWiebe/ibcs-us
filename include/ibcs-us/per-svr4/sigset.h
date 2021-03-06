#ifndef _IBCS_US_SVR4_SIGSET_H
#define _IBCS_US_SVR4_SIGSET_H

#include <ibcs-us/linux26-compat/linux/types.h>

typedef void (*svr4_sig_t)(int, void *, void *);
typedef struct svr4_sigset {
	u_int	setbits[4];
} svr4_sigset_t;

#endif /* _IBCS_US_SVR4_SIGSET_H */
