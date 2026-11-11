/*
 * PF-on-Linux: pfk module core — load/exit, /dev/pf misc device,
 * NET_LOCK bring-up, engine ioctl dispatch. (ISC 2026)
 *
 * Restored during the M1 compile-loop: this file had been dropped from
 * the tree by commit 1fcd7b3 while its objects still linked from stale
 * build artifacts; the /dev/pf path now dispatches into the staged
 * pf_ioctl.c (pfioctl()) instead of the old minimal placeholder.
 */
#include <pfk.h>
#include <pfk_api.h>
#include <net/pfvar.h>
#include <net/pfvar_priv.h>

extern void pfk_mbuf_exit(void);
extern int pfk_if_init(void);
extern void pfk_if_exit(void);
extern int pfk_pcb_init(void);
extern void pfk_pcb_exit(void);
int pfk_hooks_register(void);
void pfk_hooks_unregister(void);

extern int pfioctl(dev_t, u_long, caddr_t, int, struct proc *);

static int pfk_open(struct inode *inode, struct file *filp)
{
	if (filp->f_mode & FMODE_WRITE)
		return capable(CAP_NET_ADMIN) ? 0 : -EPERM;
	return 0;
}

/*
 * PFLINUX: OpenBSD's kernel ioctl layer copies the parameter structure
 * (size taken from the command encoding) before dispatching to pfioctl();
 * the port does the same here, using the OpenBSD _IOR/_IOW size field
 * that pfk.h preserves bit-for-bit.
 */
static long
pfk_ioctl_io(struct file *filp, unsigned int cmd, unsigned long arg)
{
	size_t sz = PFKIOC_SIZE(cmd);
	unsigned int dir = PFKIOC_DIR(cmd);
	void __user *uarg = (void __user *)arg;
	struct pfk_ucred cred;
	struct pfk_proc_p pp;
	struct proc p;
	caddr_t kdata;
	int err, err2;

	cred.uid = from_kuid(&init_user_ns, current_euid());
	cred.gid = from_kgid(&init_user_ns, current_egid());
	pp.pid = task_pid_nr(current);
	p.p_flags = 0;
	p.p_ucred = &cred;
	p.p_p = &pp;

	if (cmd == DIOCADDRULE)
		pr_info("pfkE: ioctl sz=%d kern_size=%d rule=%d\n", (int)sz,
		    (int)sizeof(struct pfioc_rule), (int)sizeof(struct pf_rule));
	kdata = kzalloc(sz ? sz : sizeof(long), GFP_KERNEL);
	if (!kdata)
		return -ENOMEM;

	if ((dir & PFKXIOC_WRITE) && sz) {
		if (copy_from_user(kdata, uarg, sz)) {
			kfree(kdata);
			return -EFAULT;
		}
	}

	err = pfioctl(0, (u_long)cmd, kdata,
	    (filp->f_mode & FMODE_WRITE) ? FWRITE : 0, &p);

	err2 = 0;
	if (!err && (dir & PFKXIOC_READ) && sz) {
		if (copy_to_user(uarg, kdata, sz))
			err2 = EFAULT;
	}
	kfree(kdata);
	return err ? -err : (err2 ? -err2 : 0);
}

static const struct file_operations pfk_fops = {
	.owner		= THIS_MODULE,
	.open		= pfk_open,
	.unlocked_ioctl	= pfk_ioctl_io,
	.compat_ioctl	= compat_ptr_ioctl,
};

static struct miscdevice pfk_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "pf",
	.fops	= &pfk_fops,
	.mode	= 0600,
};

extern void pf_status_init(void);
extern int pfattach(int);

static void
pfk_pfi_event(int added, struct pfk_ifnet *ifp)
{
	if (added)
		pfi_attach_ifnet(ifp);
	else
		pfi_detach_ifnet(ifp);
}

static void
pfk_pfi_attach_one(struct pfk_ifnet *ifp)
{
	pfi_attach_ifnet(ifp);
}

static int __init pfk_init(void)
{
	int err;

	pfk_net_lock_init();
	err = pfk_mbuf_init();
	if (err)
		return err;
	err = pfk_pcb_init();
	if (err)
		goto err_mbuf;
	err = pfk_if_init();
	if (err)
		goto err_pcb;
	/* OpenBSD bring-up order: status/pools/rulesets, then kifs.
	 * No NET_LOCK here: pfattach/pfi take what locks they need (PF_LOCK),
	 * and control ioctls serialize with NET_LOCK themselves. */
	pf_status_init();
	stoeplitz_init();	/* pf.c flow-id hashing cache (was never called) */
	pfattach(0);
	pfk_if_set_event_cb(pfk_pfi_event);
	pfk_if_iterate(pfk_pfi_attach_one);
	err = pfk_hooks_register();
	if (err)
		goto err_if;
	err = misc_register(&pfk_dev);
	if (err)
		goto err_hooks;
	pr_info("pfk: PF-on-Linux engine loaded (OpenBSD 8.0 base)\n");
	return 0;
err_hooks:
	pfk_hooks_unregister();
err_if:
	pfk_if_exit();
err_pcb:
	pfk_pcb_exit();
err_mbuf:
	pfk_mbuf_exit();
	return err;
}

extern struct timeout pf_purge_to, pf_purge_states_to;

static void __exit pfk_exit(void)
{
	/* stop sources before flushing workqueues: the purge timer
	 * self-rearms (pf_purge re-arms on every run) and would keep the
	 * workqueues non-empty forever (observed rmmod hang). */
	misc_deregister(&pfk_dev);
	pfk_net_lock();
	pf_status.running = 0;
	pfk_net_unlock();
	timeout_del(&pf_purge_to);
	timeout_del(&pf_purge_states_to);
	timeout_del_sync(&pf_purge_to);
	timeout_del_sync(&pf_purge_states_to);
	pfk_hooks_unregister();
	pfk_if_exit();
	pfk_pcb_exit();
	pfk_mbuf_exit();
	pr_info("pfk: unloaded\n");
}

module_init(pfk_init);
module_exit(pfk_exit);
MODULE_LICENSE("Dual MIT/GPL");	/* ISC/BSD code; Dual MIT/GPL is the kernel-
				 * recognised tag for permissive BSD-licensed
				 * modules linking GPL exports (see LICENSE). */
MODULE_DESCRIPTION("PF packet filter (ported from OpenBSD 8.0) for Linux");
MODULE_AUTHOR("PF-on-Linux contributors");
