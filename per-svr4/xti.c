/*
 * Copyright 1995, 1996  Mike Jagdis (jaggy@purplet.demon.co.uk)
 */
#include <ibcs-us/ibcs/sysent.h>
#include <ibcs-us/ibcs/trace.h>
#include <ibcs-us/ibcs/linux26-compat.h>
#include <ibcs-us/per-svr4/sockio.h>
#include <ibcs-us/per-svr4/socksys.h> /* for socksys_fdinit */
#include <ibcs-us/per-svr4/stream.h>
#include <ibcs-us/per-svr4/tli.h>

#include <ibcs-us/linux26-compat/asm/uaccess.h>
#include <ibcs-us/linux26-compat/linux/errno.h>
#include <ibcs-us/linux26-compat/linux/fcntl.h>
#include <ibcs-us/linux26-compat/linux/fdtable.h>
#include <ibcs-us/linux26-compat/linux/file.h>
#include <ibcs-us/linux26-compat/linux/fs.h>
#include <ibcs-us/linux26-compat/linux/gfp.h>
#include <ibcs-us/linux26-compat/linux/in.h>
#include <ibcs-us/linux26-compat/linux/kernel.h>
#include <ibcs-us/linux26-compat/linux/mm.h>
#include <ibcs-us/linux26-compat/linux/personality.h>
#include <ibcs-us/linux26-compat/linux/poll.h>
#include <ibcs-us/linux26-compat/linux/sched.h>
#include <ibcs-us/linux26-compat/linux/slab.h>
#include <ibcs-us/linux26-compat/linux/socket.h>
#include <ibcs-us/linux26-compat/linux/types.h>
#include <ibcs-us/linux26-compat/linux/un.h>
#include <ibcs-us/linux26-compat/net/sock.h>


/*
 * This is because TLI and XTI options buffers are incompatible and there
 * is no clear way to detect which format we are dealing with here.
 * Existing systems appear to have TLI options management implemented
 * but return TNOTSUPPORT for XTI requests.
 */
#if defined(CONFIG_ABI_XTI_OPTMGMT) && defined(CONFIG_ABI_TLI_OPTMGMT)
# error "unable to support _both_ TLI and XTI option management"
#endif


#if defined(CONFIG_ABI_TRACE)
static char *const xti_tab[] = {
	"T_CONN_REQ", "T_CONN_RES",
	"T_DISCON_REQ",	"T_DATA_REQ",
	"T_EXDATA_REQ", "T_INFO_REQ",
	"T_BIND_REQ", "T_UNBIND_REQ",
	"T_UNITDATA_REQ", "T_OPTMGMT_REQ",
	"T_ORDREL_REQ","T_CONN_IND",
	"T_CONN_CON", "T_DISCON_IND",
	"T_DATA_IND", "T_EXDATA_IND",
	"T_INFO_ACK", "T_BIND_ACK",
	"T_ERROR_ACK","T_OK_ACK",
	"T_UNITDATA_IND", "T_UDERROR_IND",
	"T_OPTMGMT_ACK", "T_ORDREL_IND"
};
static char xti_unknown[] = "<unknown>";

static char *
xti_prim(int n)
{
	if (n < 0 || n >= ARRAY_SIZE(xti_tab))
		return xti_unknown;
	return xti_tab[n];
}
#endif


#define timod_mkctl(len) kmalloc(sizeof(struct T_primsg)-sizeof(long)+len, \
					GFP_KERNEL)


static void
timod_socket_wakeup(struct file *fp)
{
#ifndef	_KSL_IBCS_US
	struct socket		*sock;

	sock = SOCKET_I(fp->f_dentry->d_inode);
#if	_KSL >= 35
	wake_up_interruptible(&sock->wq->wait);
#else
	wake_up_interruptible(&sock->wait);
#endif

	read_lock(&sock->sk->sk_callback_lock);
#if	_KSL >= 35
	if (sock->wq->fasync_list && !test_bit(SOCK_ASYNC_WAITDATA, &sock->flags))
		kill_fasync(&sock->wq->fasync_list, SIGIO, POLL_IN);
#else
	if (sock->fasync_list && !test_bit(SOCK_ASYNC_WAITDATA, &sock->flags))
		kill_fasync(&sock->fasync_list, SIGIO, POLL_IN);
#endif
	read_unlock(&sock->sk->sk_callback_lock);
#endif	/* _KSL_IBCS_US */
}


static void
timod_ok(int fd, int prim)
{
	struct file		*fp;
	struct T_primsg		*it;
	struct T_ok_ack 	*ok;

#if defined(CONFIG_ABI_TRACE)
	abi_trace(ABI_TRACE_STREAMS, "TI: %u ok ack prim=%d\n", fd, prim);
#endif

	fp = fcheck(fd);
	it = timod_mkctl(sizeof(struct T_ok_ack));
	if (!it)
		return;

	ok = (struct T_ok_ack *)&it->type;
	ok->PRIM_type = T_OK_ACK;
	ok->CORRECT_prim = prim;

	it->pri = MSG_HIPRI;
	it->length = sizeof(struct T_ok_ack);
	it->next = Priv(fp)->pfirst;

	Priv(fp)->pfirst = it;
	if (!Priv(fp)->plast)
		Priv(fp)->plast = it;
	timod_socket_wakeup(fp);
}

static void
timod_error(int fd, int prim, int terr, int uerr)
{
	struct file		*fp;
	struct T_primsg		*it;
	struct T_error_ack	*err;

#if defined(CONFIG_ABI_TRACE)
	abi_trace(ABI_TRACE_STREAMS, "TI: %u error prim=%d, TLI=%d, UNIX=%d\n",
			fd, prim, terr, uerr);
#endif

	fp = fcheck(fd);
	it = timod_mkctl(sizeof(struct T_error_ack));
	if (!it)
		return;

	err = (struct T_error_ack *)&it->type;
	err->PRIM_type = T_ERROR_ACK;
	err->ERROR_prim = prim;
	err->TLI_error = terr;
	err->UNIX_error = iABI_errors(uerr);

	it->pri = MSG_HIPRI;
	it->length = sizeof(struct T_error_ack);
	it->next = Priv(fp)->pfirst;

	Priv(fp)->pfirst = it;
	if (!Priv(fp)->plast)
		Priv(fp)->plast = it;
	timod_socket_wakeup(fp);
}


#if defined(CONFIG_ABI_XTI_OPTMGMT) || defined(CONFIG_ABI_TLI_OPTMGMT)
/*
 * XXX: this function is a _horrible_ mess.
 */
static int
timod_optmgmt(int fd, struct pt_regs * regs, int flag,
	      char * opt_buf, int opt_len, int do_ret)
{
	struct file * fp = fcheck(fd);
	char *ret_buf, *ret_base;
	u_int old_esp, *tsp;
	int is_tli, error, failed;
	int ret_len, ret_space;
error=0;
	if (opt_buf && opt_len > 0) {
		if (!access_ok(VERIFY_READ, opt_buf, opt_len))
			return -EFAULT;
	}

	/*
	 * FIXME:
	 *   We should be able to detect the difference between
	 *   TLI and XTI requests at run time?
	 */
#ifdef CONFIG_ABI_TLI_OPTMGMT
	is_tli = 1;
#else
	is_tli = 0;
#endif

	if (!do_ret && (!opt_buf || opt_len <= 0))
		return 0;

	/*
	 * Grab some space on the user stack to work with. We need 6 longs
	 * to build an argument frame for [gs]etsockopt calls. We also
	 * need space to build the return buffer. This will be at least
	 * as big as the given options buffer but the given options
	 * buffer may not include space for option values so we allow two
	 * longs for each option multiple of the option header size
	 * and hope that big options will not exhaust our space and
	 * trash the stack.
	 */
	ret_space = 1024 + opt_len
		+ 2*sizeof(long)*(opt_len / (is_tli ? sizeof(struct opthdr) : sizeof(struct t_opthdr)));
	ret_buf = ret_base = (char *)(_SP(regs) - ret_space);
	ret_len = 0;

	old_esp = _SP(regs);
	_SP(regs) -= ret_space + 6*sizeof(long);
	tsp = (unsigned int *)_SP(regs);
	if (!access_ok(VERIFY_WRITE, tsp, 6*sizeof(long))) {
		_SP(regs) = old_esp;
		return -EFAULT;
	}

	failed = 0;

#ifndef CONFIG_ABI_TLI_OPTMGMT
	if (is_tli) {
		printk(KERN_WARNING
			"%d iBCS: TLI optmgmt requested but not supported\n",
			current->pid);
	}
#else
	if (is_tli)
		while (opt_len >= sizeof(struct opthdr)) {
		struct opthdr opt;

#if defined(CONFIG_ABI_TRACE)
		abi_trace(ABI_TRACE_STREAMS, "TLI optmgmt opt_len=%d, "
				"ret_buf=0x%08lx, ret_len=%d, ret_space=%d\n",
				opt_len, (unsigned long)ret_buf,
				ret_len, ret_space);
#endif

		if (copy_from_user(&opt, opt_buf, sizeof(struct opthdr)))
			return -EFAULT;

		/* Idiot check... */
		if (opt.len > opt_len) {
			failed = TBADOPT;
			break;
		}

#if defined(CONFIG_ABI_TRACE)
		if (abi_traced(ABI_TRACE_STREAMS)) {
			unsigned long v;
			get_user(v, (unsigned long *)(opt_buf+sizeof(struct opthdr)));
			__abi_trace("TLI optmgmt fd=%d, level=%ld, "
					"name=%ld, value=%ld\n",
					fd, opt.level, opt.name, v);
		}
#endif
		/* Check writable space in the return buffer. */
		if (!access_ok(VERIFY_WRITE, ret_buf, sizeof(struct opthdr))) {
			failed = TSYSERR;
			break;
		}

		/* Flag values:
		 * T_NEGOTIATE means try and set it.
		 * T_DEFAULT means get the default value.
		 *           (return the current for now)
		 * T_CHECK means get the current value.
		 */
		error = 0;
		if (flag == T_NEGOTIATE) {
			put_user(fd, tsp);
			put_user(opt.level, tsp+1);
			put_user(opt.name, tsp+2);
			put_user((long)opt_buf+sizeof(struct opthdr), tsp+3);
			put_user(opt.len, tsp+4);
			error = abi_do_setsockopt(tsp);

			if (error) {
#if defined(CONFIG_ABI_TRACE)
				abi_trace(ABI_TRACE_STREAMS,
					"setsockopt failed: %d\n", error);
#endif
				failed = TBADOPT;
				break;
			}
		}
		if (!error) {
			int len;

			put_user(fd, tsp);
			put_user(opt.level, tsp+1);
			put_user(opt.name, tsp+2);
			put_user((long)ret_buf+sizeof(struct opthdr), tsp+3);
			put_user((long)(tsp+5), tsp+4);
			put_user(ret_space, tsp+5);
			error = abi_do_getsockopt(tsp);

			if (error) {
#if defined(CONFIG_ABI_TRACE)
				abi_trace(ABI_TRACE_STREAMS,
					"getsockopt failed: %d\n", error);
#endif
				failed = TBADOPT;
				break;
			}

			get_user(len, tsp+5);
			if (copy_to_user(ret_buf, &opt, sizeof(opt)))
				return -EFAULT;
			put_user(len,
				&((struct opthdr *)opt_buf)->len);
			ret_space -= sizeof(struct opthdr) + len;
			ret_len += sizeof(struct opthdr) + len;
			ret_buf += sizeof(struct opthdr) + len;
		}

		opt_len -= sizeof(struct opthdr) + opt.len;
		opt_buf += sizeof(struct opthdr) + opt.len;
	}
#endif /* CONFIG_ABI_TLI_OPTMGMT */
#ifndef CONFIG_ABI_XTI_OPTMGMT
	else {
		printk(KERN_WARNING
			"%d iBCS: XTI optmgmt requested but not supported\n",
			current->pid);
	}
#else
	else while (opt_len >= sizeof(struct t_opthdr)) {
		struct t_opthdr opt;

		if (copy_from_user(&opt, opt_buf, sizeof(struct t_opthdr)))
			return -EFAULT;
		if (opt.len > opt_len) {
			failed = 1;
			break;
		}

#if defined(CONFIG_ABI_TRACE)
		if (abi_traced(ABI_TRACE_STREAMS)) {
			unsigned long v;
			get_user(v, (unsigned long *)(opt_buf+sizeof(struct t_opthdr)));
			__abi_trace("XTI optmgmt fd=%d, level=%ld, "
					"name=%ld, value=%ld\n",
					fd, opt.level, opt.name, v);
		}
#endif
		/* Check writable space in the return buffer. */
		if (!access_ok(VERIFY_WRITE, ret_buf, sizeof(struct t_opthdr))) {
			failed = 1;
			break;
		}

		/* Flag values:
		 * T_NEGOTIATE means try and set it.
		 * T_CHECK means see if we could set it.
		 *         (so we just set it for now)
		 * T_DEFAULT means get the default value.
		 *           (return the current for now)
		 * T_CURRENT means get the current value (SCO xti.h has
		 * no T_CURRENT???).
		 */
		error = 0;
		if (flag == T_NEGOTIATE || flag == T_CHECK) {
			put_user(fd, tsp);
			put_user(opt.level, tsp+1);
			put_user(opt.name, tsp+2);
			put_user(opt_buf+sizeof(struct t_opthdr), tsp+3);
			put_user(opt.len-sizeof(struct t_opthdr), tsp+4);
			error = abi_do_setsockopt(tsp);
		}
		if (!error) {
			put_user(fd, tsp);
			put_user(opt.level, tsp+1);
			put_user(opt.name, tsp+2);
			put_user(ret_buf+sizeof(struct t_opthdr), tsp+3);
			put_user(tsp+5, tsp+4);
			put_user(ret_space, tsp+5);
			error = abi_do_getsockopt(tsp);
			if (!error) {
				int len;
				get_user(len, tsp+5);
				/* FIXME: opt.status should be set... */
				if (copy_to_user(ret_buf, &opt, sizeof(opt)))
					return -EFAULT;
				put_user(len+sizeof(struct t_opthdr),
					&((struct t_opthdr *)opt_buf)->len);
				ret_space -= sizeof(struct t_opthdr) + len;
				ret_len += sizeof(struct t_opthdr) + len;
				ret_buf += sizeof(struct t_opthdr) + len;
			}
		}

		failed |= error;
		opt_len -= opt.len;
		opt_buf += opt.len;
	}
#endif /* CONFIG_ABI_XTI_OPTMGMT */

#if 0
	/* If there is left over data the supplied options buffer was
	 * formatted incorrectly. But we might have done some work so
	 * we must fall through and return an acknowledgement I think.
	 */
	if (opt_len) {
		regs->esp = old_esp;
		return -EINVAL;
	}
#endif

	if (do_ret) {
		struct T_primsg *it;

		if (failed) {
			timod_error(fd, T_OPTMGMT_REQ, failed, -error);
			_SP(regs) = old_esp;
			return 0;
		}

#if defined(CONFIG_ABI_TRACE)
		abi_trace(ABI_TRACE_STREAMS,
			"optmgmt returns %d bytes, failed=%d\n",
			ret_len, failed);
#endif

		/* Convert the return buffer in the user stack to a
		 * T_OPTMGMT_ACK
		 * message and queue it.
		 */
		it = timod_mkctl(sizeof(struct T_optmgmt_ack) + ret_len);
		if (it) {
			struct T_optmgmt_ack *ack
				= (struct T_optmgmt_ack *)&it->type;
			ack->PRIM_type = T_OPTMGMT_ACK;
			ack->OPT_length = ret_len;
			ack->OPT_offset = sizeof(struct T_optmgmt_ack);
			ack->MGMT_flags = (failed ? T_FAILURE : flag);
			if (copy_from_user(((char *)ack)+sizeof(struct T_optmgmt_ack),
				ret_base, ret_len))
				return -EFAULT;
			it->pri = MSG_HIPRI;
			it->length = sizeof(struct T_optmgmt_ack) + ret_len;
			it->next = Priv(fp)->pfirst;
			Priv(fp)->pfirst = it;
			if (!Priv(fp)->plast)
				Priv(fp)->plast = it;
			timod_socket_wakeup(fp);
		}
	}

	_SP(regs) = old_esp;
	return 0;
}

#else /* no CONFIG_ABI_XTI_OPTMGMT or CONFIG_ABI_TLI_OPTMGMT */

static int
timod_optmgmt(int fd, struct pt_regs * regs, int flag,
	      char * opt_buf, int opt_len, int do_ret)
{
	return -EINVAL;
}

#endif /* CONFIG_ABI_XTI_OPTMGMT or CONFIG_ABI_TLI_OPTMGMT */

#define T_PRIV(fp)	Priv(fp)

int
timod_update_socket(int fd, struct file * fp, struct pt_regs * regs)
{
	struct socket * sock;
	struct T_private * priv;
	struct T_primsg * it;
	struct T_conn_ind * ind;
	u_long old_esp, * tsp, alen;
	u_short oldflags;
	int error = 0;

	sock = SOCKET_I(fp->f_dentry->d_inode);
	priv = T_PRIV(fp);

	/*
	 * If this a SOCK_STREAM and is in the TS_WRES_CIND state
	 * we are supposed to be looking for an incoming connection.
	 */
	if (sock->type != SOCK_STREAM || !priv || priv->state != TS_WRES_CIND)
		goto out;

	old_esp = _SP(regs);
	_SP(regs) -= 1024;
	tsp = (unsigned long *)_SP(regs);
	if (!access_ok(VERIFY_WRITE, tsp,
			3*sizeof(long)+sizeof(struct sockaddr))) {
		_SP(regs) = old_esp;
		error = -EFAULT;
		goto out;
	}

	put_user(fd, tsp);
	put_user((unsigned long)(tsp+4), tsp+1);
	put_user((unsigned long)(tsp+3), tsp+2);
	put_user(sizeof(struct sockaddr), tsp+3);

	/*
	 * We don't want to block in the accept(). Any
	 * blocking necessary must be handled earlier.
	 */
#ifdef _KSL_IBCS_US
	oldflags = file_get_f_flags(fp);
	file_set_f_flags(fp, oldflags | O_NONBLOCK);
#else
	oldflags = fp->f_flags;
	fp->f_flags |= O_NONBLOCK;
#endif
#ifdef CONFIG_65BIT
	error = SYS(accept, tsp[0], tsp[1], tsp[2]);
#else
	error = SYS(socketcall,SYS_ACCEPT, tsp);
#endif
#ifdef _KSL_IBCS_US
	file_set_f_flags(fp, oldflags);
#else
	fp->f_flags = oldflags;
#endif

	if (error < 0)
		goto out_set;

	/* The new fd needs to be fixed up
	 * with the iBCS file functions and a
	 * timod state block.
	 */
	inherit_socksys_funcs(error, TS_DATA_XFER);

	/* Generate a T_CONN_IND and queue it. */
	get_user(alen, tsp+3);
	it = timod_mkctl(sizeof(struct T_conn_ind) + alen);
	if (!it) {
		/* Oops, just drop the connection I guess. */
		SYS(close,error);
		goto out_set;
	}

	ind = (struct T_conn_ind *)&it->type;
	ind->PRIM_type = T_CONN_IND;
	ind->SRC_length = alen;
	ind->SRC_offset = sizeof(struct T_conn_ind);
	ind->OPT_length = ind->OPT_offset = 0;
	ind->SEQ_number = error;

	if (copy_from_user(((char *)ind)+sizeof(struct T_conn_ind), tsp+4, alen))
		return -EFAULT;
#if 0
	it->pri = MSG_HIPRI;
#endif
	it->length = sizeof(struct T_conn_ind) + alen;
	it->next = Priv(fp)->pfirst;
	Priv(fp)->pfirst = it;
	if (!Priv(fp)->plast)
		Priv(fp)->plast = it;
	timod_socket_wakeup(fp);

out_set:
	_SP(regs) = old_esp;
out:
	return (error);
}


int
do_getmsg(int fd, struct pt_regs *regs, char *ctl_buf,
	  int ctl_maxlen, int *ctl_len, char *dat_buf,
	  int dat_maxlen, int *dat_len, int *flags_p)
{
	int error;
	long old_esp;
	unsigned long *tsp;
	unsigned short oldflags;
	struct T_unitdata_ind udi;
	struct file *filep;

	/*
	 * It may not be obvious but we are always holding an fget(fd)
	 * at this point so we can use fcheck(fd) rather than fget...fput.
	 */
	filep = fcheck(fd);

	if (!Priv(filep) || Priv(filep)->magic != XTI_MAGIC) {
		printk("getmsg on non-STREAMS fd %d by %s\n",fd, current->comm);
		return -EINVAL;
	}

#if defined(CONFIG_ABI_TRACE)
	abi_trace(ABI_TRACE_STREAMS,
			"getmsg %d, 0x%lx[%d], 0x%lx[%d], %x\n",
			fd, (u_long)ctl_buf, ctl_maxlen,
			(u_long)dat_buf, dat_maxlen, *flags_p);
#endif

	/*
	 * We need some user space to build syscall argument vectors
	 * later. Set it up now and page it in if necessary. This will
	 * avoid (most?) potential blocking after the select().
	 */
	old_esp = _SP(regs);
	_SP(regs) -= 1024;
	tsp = (unsigned long *)_SP(regs);
	_SP(regs) = old_esp;
	if (!access_ok(VERIFY_WRITE, tsp, 6*sizeof(long)))
		return -EFAULT;

	/*
	 * If the TEP is not non-blocking we must test for
	 * something to do. We don't necessarily know what order
	 * events will be happening on the socket so we have to
	 * watch for evrything at once.
	 * N.B. If we weren't asked for data we should only be looking
	 * for connection requests. There are socket type issues to
	 * consider here.
	 */
#if _KSL_IBCS_US
	oldflags = file_get_f_flags(filep);
#else
	oldflags = filep->f_flags;
#endif
	if (!(oldflags & O_NONBLOCK)) {
                struct poll_wqueues wait_queue;
		unsigned long mask = (POLLIN | POLLRDNORM | POLLHUP | POLLERR);

		if (*flags_p == MSG_HIPRI)
			mask |= POLLPRI;

		poll_initwait(&wait_queue);

		/*
		 * N.B. We need to be sure to recheck after a schedule()
		 * so that when we proceed it is because there is
		 * something to do and nothing else can get there
		 * before us.
		 */
#ifdef _KSL_IBCS_US
		error = linux26_poll_wait(filep, mask);
		if (error) {
			return error;
		}
#else
		poll_table *wait = &wait_queue.pt;
		while (!(filep->f_op->poll(filep, wait) & mask)
				&& !signal_pending(current)) {
			current->state = TASK_INTERRUPTIBLE;
			wait = NULL;
			schedule();
		}
		current->state = TASK_RUNNING;
#endif
		poll_freewait(&wait_queue);

#ifndef	_KSL_IBCS_US
		if (signal_pending(current))
			return -EINTR;
#endif
	}

	if (ctl_maxlen >= 0 && !Priv(filep)->pfirst)
		timod_update_socket(fd, filep, regs);

	/*
	 * If we were asked for a control part and there is an outstanding
	 * message queued as a result of some other operation we'll
	 * return that.
	 */
	if (ctl_maxlen >= 0 && Priv(filep)->pfirst) {
		int l = ctl_maxlen <= Priv(filep)->pfirst->length
				? ctl_maxlen : Priv(filep)->pfirst->length;
		if (!access_ok(VERIFY_WRITE, ctl_buf, l))
			return -EFAULT;

#if defined(CONFIG_ABI_TRACE)
		abi_trace(ABI_TRACE_STREAMS,
			"priority message %ld %s\n",
			Priv(filep)->pfirst->type,
			xti_prim(Priv(filep)->pfirst->type));
#endif

		if (copy_to_user(ctl_buf, ((char *)&Priv(filep)->pfirst->type)
					+ Priv(filep)->offset, l))
			return -EFAULT;
		put_user(l, ctl_len);
		if (dat_maxlen >= 0)
			put_user(0, dat_len);
		*flags_p = Priv(filep)->pfirst->pri;
		Priv(filep)->pfirst->length -= l;

#if defined(CONFIG_ABI_TRACE)
		if (abi_traced(ABI_TRACE_STREAMS) && ctl_buf && l > 0) {
			int i = -1;

			for (i = 0; i < l && i < 64; i += 4) {
				u_long v;

				get_user(v, (u_long *)(ctl_buf + i));
				__abi_trace("ctl: 0x%08lx\n", v);
			}
			if (i != l)
				__abi_trace("ctl: ...\n");
		}
#endif

		if (Priv(filep)->pfirst->length) {
			Priv(filep)->offset += l;

#if defined(CONFIG_ABI_TRACE)
			abi_trace(ABI_TRACE_STREAMS,
					"MORECTL %d bytes",
					Priv(filep)->pfirst->length);
#endif
			return MORECTL;
		} else {
			struct T_primsg *it = Priv(filep)->pfirst;
			Priv(filep)->pfirst = it->next;
			if (!Priv(filep)->pfirst)
				Priv(filep)->plast = NULL;
			kfree(it);
			Priv(filep)->offset = 0;
			return 0;
		}
	}

	*flags_p = 0;

	/* If we weren't asked for data there is nothing more to do. */
	if (dat_maxlen <= 0) {
		if (dat_maxlen == 0)
			put_user(0, dat_len);
		if (ctl_maxlen >= 0)
			put_user(0, ctl_len);
		return -EAGAIN;
	}

	/* If the select() slept we may have had our temp space paged
	 * out. The re-verify_area is only really needed for pre-486
	 * chips which don't handle write faults from kernel mode.
	 */
	_SP(regs) = (unsigned long)tsp;
	if (!access_ok(VERIFY_WRITE, tsp, 6*sizeof(long))) {
		_SP(regs) = old_esp;
		return -EFAULT;
	}
	put_user(fd, tsp);
	put_user((unsigned long)dat_buf, tsp+1);
	put_user((dat_maxlen < 0 ? 0 : dat_maxlen), tsp+2);
	put_user(0, tsp+3);
	if (ctl_maxlen > (int)sizeof(udi) && Priv(filep)->state == TS_IDLE) {
		put_user((unsigned long)ctl_buf+sizeof(udi), tsp+4);
		put_user(ctl_maxlen-sizeof(udi), ctl_len);
		put_user((long)ctl_len, tsp+5);
	} else {
		put_user(0, tsp+4);
		put_user(0, ctl_len);
		put_user((long)ctl_len, tsp+5);
	}

	/* We don't want to block in the recvfrom(). Any blocking is
	 * handled by the select stuff above.
	 */
#ifdef _KSL_IBCS_US
	file_set_f_flags(filep, oldflags | O_NONBLOCK);
#else
	filep->f_flags |= O_NONBLOCK;
#endif
#ifdef CONFIG_65BIT
	error = SYS(recvfrom, tsp[0], tsp[1], tsp[2], tsp[3], tsp[4], tsp[5]);
#else
	error = SYS(socketcall,SYS_RECVFROM, tsp);
#endif
#ifdef _KSL_IBCS_US
	file_set_f_flags(filep, oldflags);
#else
	filep->f_flags = oldflags;
#endif

	_SP(regs) = old_esp;
	if (error < 0)
		return error;
	if (error
	&& ctl_maxlen > (int)sizeof(udi)
	&& Priv(filep)->state == TS_IDLE) {
		udi.PRIM_type = T_UNITDATA_IND;
		get_user(udi.SRC_length, ctl_len);
		udi.SRC_offset = sizeof(udi);
		udi.OPT_length = udi.OPT_offset = 0;
		if (copy_to_user(ctl_buf, &udi, (int)sizeof(udi)))
			return -EFAULT;
		put_user(sizeof(udi)+udi.SRC_length, ctl_len);
#if 0
#if defined(CONFIG_ABI_TRACE)
		if (abi_traced(ABI_TRACE_STREAMS) &&
		    ctl_buf && udi.SRC_length > 0) {
			char * buf = ctl_buf + sizeof(udi);
			int i = -1;

			for (i = 0; i < udi.SRC_length &&
			     i < 64; i += 4) {
				u_long v;

				get_user(v, (u_long *)(buf+i));
				__abi_trace("dat: 0x%08lx\n", v);
			}
			if (i != udi.SRC_length)
				__abi_trace("dat: ...\n");
		}
#endif
#endif
	} else {
		put_user(0, ctl_len);
	}
	put_user(error, dat_len);

	return 0;
}


int
do_putmsg(int fd, struct pt_regs *regs, char *ctl_buf, int ctl_len,
	char *dat_buf, int dat_len, int flags)
{
	struct file *filep;
	int error, terror;
	unsigned long cmd;
error = 0;
	/* It may not be obvious but we are always holding an fget(fd)
	 * at this point so we can use fcheck(fd) rather than fget...fput.
	 */
	filep = fcheck(fd);

	if (!Priv(filep) || Priv(filep)->magic != XTI_MAGIC) {
		printk("putmsg on non-STREAMS fd %d by %s\n",fd, current->comm);
		return -EINVAL;
	}

#if defined(CONFIG_ABI_TRACE)
	if (abi_traced(ABI_TRACE_STREAMS)) {
		u_long v;
		__abi_trace("putmsg %d, 0x%lx[%d], 0x%lx[%d], %x\n",
			fd, (u_long)ctl_buf, ctl_len,
			(u_long)dat_buf, dat_len, flags);

		get_user(v, ctl_buf);
		__abi_trace("putmsg prim: %ld %s\n", v, xti_prim(v));

		if (ctl_buf && ctl_len > 0) {
			int i = -1;

			for (i = 0; i < ctl_len && i < 64; i += 4) {
				get_user(v, (u_long *)(ctl_buf + i));
				__abi_trace("ctl: 0x%08lx\n", v);
			}
			if (i != ctl_len)
				__abi_trace("ctl: ...\n");
		}

		if (dat_buf && dat_len > 0) {
			int i = -1;

			for (i = 0; i < dat_len && i < 64; i += 4) {
				get_user(v, (u_long *)(dat_buf + i));
				__abi_trace("dat: 0x%08lx\n", v);
			}
			if (i != dat_len)
				__abi_trace("dat: ...");
		}
	}
#endif

	error = get_user(cmd, (unsigned long *)ctl_buf);
	if (error)
		return error;

	switch (cmd) {
		case T_BIND_REQ: {
			struct T_bind_req req;
			long old_esp;
			unsigned long *tsp;

#if defined(CONFIG_ABI_TRACE)
			abi_trace(ABI_TRACE_STREAMS, "%u bind req\n", fd);
#endif
			if (!access_ok(VERIFY_READ, ctl_buf, sizeof(req)))
				return -EFAULT;

			if (Priv(filep)->state != TS_UNBND) {
				timod_error(fd, T_BIND_REQ, TOUTSTATE, 0);
				return 0;
			}

			old_esp = _SP(regs);
			_SP(regs) -= 1024;
			tsp = (unsigned long *)_SP(regs);
			if (!access_ok(VERIFY_WRITE, tsp, 3*sizeof(long))) {
				timod_error(fd, T_BIND_REQ, TSYSERR, -EFAULT);
				_SP(regs) = old_esp;
				return 0;
			}

			if (copy_from_user(&req, ctl_buf, sizeof(req)))
				return -EFAULT;
			if (req.ADDR_offset && req.ADDR_length) {
				struct sockaddr_in *sin;
				unsigned short family;

#if 1				/* Wheee... Kludge time... */
				sin = (struct sockaddr_in *)(ctl_buf
					+ req.ADDR_offset);
				get_user(family, &sin->sin_family);

				/* Sybase seems to have set up the address
				 * struct with sa->sa_family = htons(AF_?)
				 * which is bollocks. I have no idea why it
				 * apparently works on SCO?!?
				 */
				if (family && !(family & 0x00ff))
					put_user(ntohs(family), &sin->sin_family);
#endif

				put_user(fd, tsp);
				put_user((unsigned long)ctl_buf
						+ req.ADDR_offset, tsp+1);
				/* For TLI/XTI the length may be the 8 *used*
				 * bytes, for (IP?) sockets it must be the 16
				 * *total* bytes in a sockaddr_in.
				 */
				put_user(req.ADDR_length == 8
					? 16 : req.ADDR_length,
					tsp+2);
#ifdef CONFIG_65BIT
				error = SYS(bind, tsp[0], tsp[1], tsp[2]);
#else
				error = SYS(socketcall,SYS_BIND, tsp);
#endif

				if (!error) {
					if (req.CONIND_number) {

#if defined(CONFIG_ABI_TRACE)
						abi_trace(ABI_TRACE_STREAMS,
							"%u listen backlog=%lu\n",
							fd, req.CONIND_number);
#endif

						put_user(fd, tsp);
						put_user(req.CONIND_number, tsp+1);
#ifdef CONFIG_65BIT
						SYS(listen, tsp[0], tsp[1]);
#else
						SYS(socketcall,SYS_LISTEN, tsp);
#endif
						Priv(filep)->state = TS_WRES_CIND;
					} else {
						Priv(filep)->state = TS_IDLE;
					}
				}
			} else {
				error = 0;
			}

			_SP(regs) = old_esp;

			if (!error) {
				struct T_primsg *it;
				it = timod_mkctl(ctl_len);
				if (it) {
					struct T_bind_ack *ack = (struct T_bind_ack *)&it->type;
					if (copy_from_user(ack, ctl_buf, ctl_len))
				                return -EFAULT;
					ack->PRIM_type = T_BIND_ACK;
					it->pri = MSG_HIPRI;
					it->length = ctl_len;
					it->next = NULL;
					timod_ok(fd, T_BIND_REQ);
					Priv(filep)->plast->next = it;
					Priv(filep)->plast = it;
					return 0;
				}
			}
			switch (error) {
				case -EINVAL:
					terror = TOUTSTATE;
					error = 0;
					break;
				case -EACCES:
					terror = TACCES;
					error = 0;
					break;
				case -EADDRNOTAVAIL:
				case -EADDRINUSE:
					terror = TNOADDR;
					error = 0;
					break;
				default:
					terror = TSYSERR;
					break;
			}
			timod_error(fd, T_BIND_REQ, terror, -error);
			return 0;
		}
		case T_CONN_RES: {
			struct T_conn_res *res = (struct T_conn_res *)ctl_buf;
			unsigned int conn_fd;

			error = get_user(conn_fd, &res->SEQ_number);
			if (error)
				return error;

#if defined(CONFIG_ABI_TRACE)
			abi_trace(ABI_TRACE_STREAMS,
					"%u accept: conn fd=%u, use fd=%u\n",
					fd, conn_fd, flags);
#endif

			if (conn_fd != flags) {
				error = SYS(dup2,conn_fd, flags);
				SYS(close,conn_fd);
				if (error < 0)
					return error;
			}
			timod_ok(fd, T_CONN_RES);
			return 0;
		}
		case T_CONN_REQ: {
			struct T_conn_req req;
			long old_esp;
			unsigned short oldflags;
			unsigned long *tsp;
			struct T_primsg *it;
			struct sockaddr_in *sin;
			unsigned short family;

#if defined(CONFIG_ABI_TRACE)
			abi_trace(ABI_TRACE_STREAMS, "%u connect req\n", fd);
#endif
			if (!access_ok(VERIFY_READ, ctl_buf, sizeof(req)))
				return -EFAULT;

			if (Priv(filep)->state != TS_UNBND
			&& Priv(filep)->state != TS_IDLE) {
				timod_error(fd, T_CONN_REQ, TOUTSTATE, 0);
				return 0;
			}

			old_esp = _SP(regs);
			_SP(regs) -= 1024;
			tsp = (unsigned long *)_SP(regs);
			if (!access_ok(VERIFY_WRITE, tsp, 3*sizeof(long))) {
				timod_error(fd, T_CONN_REQ, TSYSERR, -EFAULT);
				_SP(regs) = old_esp;
				return 0;
			}
			if (copy_from_user(&req, ctl_buf, sizeof(req)))
				return -EFAULT;
			put_user(fd, tsp);
			put_user((unsigned long)ctl_buf + req.DEST_offset, tsp+1);
			/* For TLI/XTI the length may be the 8 *used*
			 * bytes, for (IP?) sockets it must be the 16
			 * *total* bytes in a sockaddr_in.
			 */
			put_user(req.DEST_length == 8
				? 16 : req.DEST_length,
				tsp+2);

#if 1			/* Wheee... Kludge time... */
			sin = (struct sockaddr_in *)(ctl_buf
				+ req.DEST_offset);
			get_user(family, &sin->sin_family);

			/* Sybase seems to have set up the address
			 * struct with sa->sa_family = htons(AF_?)
			 * which is bollocks. I have no idea why it
			 * apparently works on SCO?!?
			 */
			if (family && !(family & 0x00ff)) {
				family = ntohs(family);
				put_user(family, &sin->sin_family);
			}

			/* Sheesh... ISC telnet seems to give the port
			 * number low byte first as I expected but the
			 * X programs seem to be giving high byte first.
			 * One is broken of course but clearly both
			 * should work. No, I don't understand this
			 * either but I can at least try...
			 * A better solution would be for you to change
			 * the definition of xserver0 in ISC's /etc/services
			 * but then it wouldn't work out of the box...
			 */
			if (current->personality == PER_SVR4 && family == AF_INET) {
				get_user(family, &sin->sin_port);
				if (family == 0x1770)
					put_user(htons(family),
						&sin->sin_port);
			}
#endif
			/* FIXME: We should honour non-blocking mode
			 * here but that means that the select probe
			 * needs to know that if select returns ok and
			 * we are in T_OUTCON we have a connection
			 * completion. This isn't so bad but the real
			 * problem is that the connection acknowledgement
			 * is supposed to contain the destination
			 * address.
			 */
#ifdef _KSL_IBCS_US
			oldflags = file_get_f_flags(filep);
			file_set_f_flags(filep, oldflags | O_NONBLOCK);
#else
			oldflags = filep->f_flags;
			filep->f_flags &= ~O_NONBLOCK;
#endif
#ifdef CONFIG_65BIT
			error = SYS(connect, tsp[0], tsp[1], tsp[2]);
#else
			error = SYS(socketcall,SYS_CONNECT, tsp);
#endif
#ifdef _KSL_IBCS_US
			file_set_f_flags(filep, oldflags);
#else
			filep->f_flags = oldflags;
#endif
			_SP(regs) = old_esp;

			if (!error) {
				struct T_conn_con *con;

				it = timod_mkctl(ctl_len);
				if (!it)
					return -ENOMEM;
				it->length = ctl_len;
				con = (struct T_conn_con *)&it->type;
				if (copy_from_user(con, ctl_buf, ctl_len))
					return -EFAULT;
				con->PRIM_type = T_CONN_CON;
				Priv(filep)->state = TS_DATA_XFER;
			} else {
				struct T_discon_ind *dis;

#if defined(CONFIG_ABI_TRACE)
				abi_trace(ABI_TRACE_STREAMS,
						"%u connect failed (errno=%d)\n",
						fd, error);
#endif

				it = timod_mkctl(sizeof(struct T_discon_ind));
				if (!it)
					return -ENOMEM;
				it->length = sizeof(struct T_discon_ind);
				dis = (struct T_discon_ind *)&it->type;
				dis->PRIM_type = T_DISCON_IND;
				dis->DISCON_reason = iABI_errors(-error);
				dis->SEQ_number = 0;
			}
			timod_ok(fd, T_CONN_REQ);
			it->pri = 0;
			it->next = NULL;
			Priv(filep)->plast->next = it;
			Priv(filep)->plast = it;
			return 0;
		}

		case T_DISCON_REQ: {
			struct T_discon_req *req;

			req = (struct T_discon_req *)ctl_buf;
			error = get_user(fd, &req->SEQ_number);
			if (error)
				return error;

#if defined(CONFIG_ABI_TRACE)
			abi_trace(ABI_TRACE_STREAMS, "disconnect %u\n", fd);
#endif
			/* Fall through... */
			__attribute__((fallthrough));
		}
		case T_ORDREL_REQ: {
			SYS(close,fd);
			return 0;
		}

		case T_DATA_REQ: {
			long old_esp;
			unsigned long *tsp;

#if defined(CONFIG_ABI_TRACE)
			abi_trace(ABI_TRACE_STREAMS, "%u data req\n", fd);
#endif

			if (Priv(filep)->state != TS_DATA_XFER) {
				return 0;
			}

			old_esp = _SP(regs);
			_SP(regs) -= 1024;
			tsp = (unsigned long *)_SP(regs);
			if (!access_ok(VERIFY_WRITE, tsp, 6*sizeof(long))) {
				_SP(regs) = old_esp;
				return 0;
			}
			put_user(fd, tsp);
			put_user((unsigned long)dat_buf, tsp+1);
			put_user(dat_len, tsp+2);
			put_user(0, tsp+3);
#ifdef CONFIG_65BIT
			error = SYS(sendto, tsp[0], tsp[1], tsp[2], tsp[3], 0, 0);
#else
			error = SYS(socketcall,SYS_SEND, tsp);
#endif
			_SP(regs) = old_esp;
			return error;
		}

		case T_UNITDATA_REQ: {
			struct T_unitdata_req req;
			long old_esp;
			unsigned long *tsp;

#if defined(CONFIG_ABI_TRACE)
			abi_trace(ABI_TRACE_STREAMS, "%u unitdata req\n", fd);
#endif
			if (!access_ok(VERIFY_READ, ctl_buf, sizeof(req)))
				return -EFAULT;

			if (Priv(filep)->state != TS_IDLE
			&& Priv(filep)->state != TS_DATA_XFER) {
				timod_error(fd, T_UNITDATA_REQ, TOUTSTATE, 0);
				return 0;
			}

			old_esp = _SP(regs);
			_SP(regs) -= 1024;
			tsp = (unsigned long *)_SP(regs);
			if (!access_ok(VERIFY_WRITE, tsp, 6*sizeof(long))) {
				timod_error(fd, T_UNITDATA_REQ, TSYSERR, -EFAULT);
				_SP(regs) = old_esp;
				return 0;
			}
			put_user(fd, tsp);
			put_user((unsigned long)dat_buf, tsp+1);
			put_user(dat_len, tsp+2);
			put_user(0, tsp+3);
			if (copy_from_user(&req, ctl_buf, sizeof(req)))
				return -EFAULT;
			if (req.DEST_length > 0) {
				put_user((unsigned long)(ctl_buf+req.DEST_offset), tsp+4);
				put_user(req.DEST_length, tsp+5);
#ifdef CONFIG_65BIT
				error = SYS(sendto, tsp[0], tsp[1], tsp[2], tsp[3], tsp[4], tsp[5]);
#else
				error = SYS(socketcall,SYS_SENDTO, tsp);
#endif
				_SP(regs) = old_esp;
				return error;
			}
#ifdef CONFIG_65BIT
			error = SYS(sendto, tsp[0], tsp[1], tsp[2], tsp[3], 0, 0);
#else
			error = SYS(socketcall,SYS_SEND, tsp);
#endif
			_SP(regs) = old_esp;
			return error;
		}

		case T_UNBIND_REQ:
			Priv(filep)->state = TS_UNBND;
			timod_ok(fd, T_UNBIND_REQ);
			return 0;

		case T_OPTMGMT_REQ: {
			struct T_optmgmt_req req;

#if defined(CONFIG_ABI_TRACE)
			abi_trace(ABI_TRACE_STREAMS, "%u optmgmt req\n", fd);
#endif
			if (!access_ok(VERIFY_READ, ctl_buf, sizeof(req)))
				return -EFAULT;
			if (copy_from_user(&req, ctl_buf, sizeof(req)))
				return -EFAULT;

			return timod_optmgmt(fd, regs, req.MGMT_flags,
					req.OPT_offset > 0
						? ctl_buf+req.OPT_offset
						: NULL,
					req.OPT_length,
					1);
		}
	}

#if defined(CONFIG_ABI_TRACE)
      if (abi_traced(ABI_TRACE_STREAMS))
      {

	if (ctl_buf && ctl_len > 0) {
		int		i;

		for (i = 0; i < ctl_len && i < 32; i += 4) {
			u_long	v;

			get_user(v, (u_long *)(ctl_buf + i));
			__abi_trace("ctl: 0x%08lx\n", v);
		}
		if (i != ctl_len)
			__abi_trace("ctl: ...\n");
	}
	if (dat_buf && dat_len > 0) {
		int		i;
		for (i = 0; i < dat_len && i < 32; i += 4) {
			u_long	v;

			get_user(v, (u_long *)(dat_buf + i));
			__abi_trace("dat: 0x%08lx\n", v);
		}
		if (i != dat_len)
			__abi_trace("dat: ...\n");
	}
      }
#endif
	return -EINVAL;
}

/* this function needs to be cleaned up badly.  --hch */
int
timod_ioctl(struct pt_regs *regs,
	int fd, unsigned int func, void *arg, int len, int *len_p)
{
	struct file *filep;
	struct inode *ino;
	int error;

	func &= 0xff;

	filep = fget(fd);
	if (!filep)
		return TBADF;

	if (!access_ok(VERIFY_WRITE, len_p, sizeof(int))) {
		fput(filep);
		return (int)((unsigned)-EFAULT << 8) | TSYSERR;
	}

	ino = filep->f_dentry->d_inode;

	/* SCO/SVR3 starts at 100, ISC/SVR4 starts at 140. */
	switch (func >= 140 ? func-140 : func-100) {
		case 0: /* TI_GETINFO */
		{
			struct T_info_ack it;
			unsigned long v;

#if defined(CONFIG_ABI_TRACE)
			abi_trace(ABI_TRACE_STREAMS, "%u getinfo\n", fd);
#endif
			/* The pre-SVR4 T_info_ack structure didn't have
			 * the PROVIDER_flag on the end.
			 */
			if (!access_ok(VERIFY_WRITE, arg,
				func == 140
				? sizeof(struct T_info_ack)
				: sizeof(struct T_info_ack)-sizeof(long))) {
				fput(filep);
				return (int)((unsigned)-EFAULT << 8) | TSYSERR;
			}

			if (!Priv(filep)) {
				fput(filep);
				return TBADF;
			}
			__get_user(v, &((struct T_info_req *)arg)->PRIM_type);
			if (v != T_INFO_REQ) {
				fput(filep);
				return (EINVAL << 8) | TSYSERR;
			}

			it.PRIM_type = T_INFO_ACK;
			it.CURRENT_state = Priv(filep)->state;
			it.CDATA_size = -2;
			it.DDATA_size = -2;
			it.OPT_size = -1;
			it.TIDU_size = 16384;
			switch ((MINOR(ino->i_rdev)>>4) & 0x0f) {
				case AF_UNIX:
					it.ADDR_size = sizeof(struct sockaddr_un);
					break;
				case AF_INET:
					it.ADDR_size = sizeof(struct sockaddr_in);
					break;
				default:
					/* Uh... dunno... play safe(?) */
					it.ADDR_size = 1024;
					break;
			}
			switch (SOCKET_I(ino)->type) {
				case SOCK_STREAM:
					it.ETSDU_size = 1;
					it.TSDU_size = 0;
					it.SERV_type = 2;
					break;
				default:
					it.ETSDU_size = -2;
					it.TSDU_size = 16384;
					it.SERV_type = 3;
					break;
			}

			fput(filep);

			/* The pre-SVR4 T_info_ack structure didn't have
			 * the PROVIDER_flag on the end.
			 */
			if (func == 140) {
				it.PROVIDER_flag = 0;
				if (copy_to_user(arg, &it, sizeof(it)))
					return -EFAULT;
				put_user(sizeof(it), len_p);
				return 0;
			}
			if (copy_to_user(arg, &it, sizeof(it)-sizeof(long)))
				return -EFAULT;
			put_user(sizeof(it)-sizeof(long), len_p);
			return 0;
		}

		case 2: /* TI_BIND */
		{
			int i;
			long prim;

#if defined(CONFIG_ABI_TRACE)
			abi_trace(ABI_TRACE_STREAMS, "%u bind\n", fd);
#endif
			error = do_putmsg(fd, regs, arg, len,
					NULL, -1, 0);
			if (error) {
				fput(filep);
				return (-error << 8) | TSYSERR;
			}

			/* Get the response. This should be either
			 * T_OK_ACK or T_ERROR_ACK.
			 */
			i = MSG_HIPRI;
			error = do_getmsg(fd, regs,
					arg, len, len_p,
					NULL, -1, NULL,
					&i);
			if (error) {
				fput(filep);
				return (-error << 8) | TSYSERR;
			}

			get_user(prim, (unsigned long *)arg);
			if (prim == T_ERROR_ACK) {
				unsigned long a, b;
				fput(filep);
				get_user(a, ((unsigned long *)arg)+3);
				get_user(b, ((unsigned long *)arg)+2);
				return (a << 8) | b;
			}
			if (prim != T_OK_ACK) {
				fput(filep);
				return TBADSEQ;
			}

			/* Get the response to the bind request. */
			i = MSG_HIPRI;
			error = do_getmsg(fd, regs,
					arg, len, len_p,
					NULL, -1, NULL,
					&i);
			fput(filep);
			if (error)
				return (-error << 8) | TSYSERR;

			return 0;
		}

		case 3: /* TI_UNBIND */
			if (!Priv(filep) || Priv(filep)->state != TS_IDLE) {
				fput(filep);
				return TOUTSTATE;
			}
			Priv(filep)->state = TS_UNBND;
			fput(filep);
			return 0;

		case 1: { /* TI_OPTMGMT */
#if defined(CONFIG_ABI_XTI_OPTMGMT) || defined(CONFIG_ABI_TLI_OPTMGMT)
			int i;
			long prim;

#if defined(CONFIG_ABI_TRACE)
			abi_trace(ABI_TRACE_STREAMS, "%u optmgmt\n", fd);
#endif
			error = do_putmsg(fd, regs, arg, len,
					NULL, -1, 0);
			if (error) {
				fput(filep);
				return (-error << 8) | TSYSERR;
			}

			/* Get the response to the optmgmt request. */
			i = MSG_HIPRI;
			error = do_getmsg(fd, regs,
					arg, len, len_p,
					NULL, -1, NULL,
					&i);
			if (error > 0 && Priv(filep)) {
				/* If there is excess data in the response
				 * our buffer is too small which implies
				 * the application is broken. SO_LINGER
				 * is a common fault. Because it works
				 * on other systems we attempt to recover
				 * by discarding the excess.
				 */
				struct T_primsg *it = Priv(filep)->pfirst;
				Priv(filep)->pfirst = it->next;
				if (!Priv(filep)->pfirst)
					Priv(filep)->plast = NULL;
				kfree(it);
				Priv(filep)->offset = 0;

#if defined(CONFIG_ABI_TRACE)
				abi_trace(ABI_TRACE_STREAMS,
						"excess discarded\n");
#endif
			}

			fput(filep);

			if (error < 0)
				return (-error << 8) | TSYSERR;

			__get_user(prim, (unsigned long *)arg);
			if (prim == T_ERROR_ACK) {
				unsigned long a, b;
				__get_user(a, ((unsigned long *)arg)+3);
				__get_user(b, ((unsigned long *)arg)+2);
				return (a << 8) | b;
			}

			return 0;
#else /* no CONFIG_ABI_XTI_OPTMGMT or CONFIG_ABI_TLI_OPTMGMT */
			fput(filep);
			return TNOTSUPPORT;
#endif /* CONFIG_ABI_XTI_OPTMGMT or CONFIG_ABI_TLI_OPTMGMT */
		}

		case 4: /* TI_GETMYNAME */
		case 5: /* TI_SETPEERNAME */
		case 6: /* TI_GETMYNAME */
		case 7: /* TI_SETPEERNAME */
                        break;
	}

#if defined(CONFIG_ABI_TRACE)
	abi_trace(ABI_TRACE_STREAMS,
			"STREAMS timod op %d not supported\n", func);
#endif
	fput(filep);
	return TNOTSUPPORT;
}


int
svr4_sockmod_ioctl(int fd, u_int cmd, caddr_t data)
{
	struct file		*fp;
	struct inode		*ip;
	int			error;

	fp = fget(fd);
	if (!fp)
		return (TBADF);

	ip = fp->f_dentry->d_inode;
	if (MAJOR(ip->i_rdev) == SOCKSYS_MAJOR) {
		error = socksys_fdinit(fd, 0, NULL, NULL);
		if (error < 0)
			return -error;
		fput(fp);
		fp = fget(fd);
		if (!fp)
			return TBADF;
		ip = fp->f_dentry->d_inode;
	}

	switch (cmd) {
	case 101: { /* SI_GETUDATA */
		struct {
			int tidusize, addrsize, optsize, etsdusize;
			int servtype, so_state, so_options;
		} *it = (void *)data;

#if defined(CONFIG_ABI_TRACE)
		abi_trace(ABI_TRACE_STREAMS, "%u getudata\n", fd);
#endif
		if (!access_ok(VERIFY_WRITE, it, sizeof(*it))) {
			fput(fp);
			return (int)((unsigned)-EFAULT << 8) | TSYSERR;
		}

		__put_user(16384, &it->tidusize);
		__put_user(sizeof(struct sockaddr), &it->addrsize);
		__put_user(-1, &it->optsize);
		__put_user(0, &it->so_state);
		__put_user(0, &it->so_options);

		switch (SOCKET_I(ip)->type) {
		case SOCK_STREAM:
			__put_user(1, &it->etsdusize);
			__put_user(2, &it->servtype);
			break;
		default:
			__put_user(-2, &it->etsdusize);
			__put_user(3, &it->servtype);
			break;
		}
		fput(fp);
		return 0;
	}

	case 102: /* SI_SHUTDOWN */
	case 103: /* SI_LISTEN */
	case 104: /* SI_SETMYNAME */
	case 105: /* SI_SETPEERNAME */
	case 106: /* SI_GETINTRANSIT */
	case 107: /* SI_TCL_LINK */
	case 108: /* SI_TCL_UNLINK */
                break;
	}

#if defined(CONFIG_ABI_TRACE)
	abi_trace(ABI_TRACE_STREAMS,
			"STREAMS sockmod op %d not supported\n", cmd);
#endif
	fput(fp);
	return TNOTSUPPORT;
}

EXPORT_SYMBOL(svr4_sockmod_ioctl);
