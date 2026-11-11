/*
 * PF-on-Linux: pfk module core — load/exit, /dev/pf misc device,
 * NET_LOCK bring-up, staged upstream bring-up hooks. (ISC 2026)
 */
#include <pfk.h>
#include <pfk_api.h>
#include <net/pfvar.h>
#include <net/pfvar_priv.h>

extern void pfk_mbuf_exit(void);
extern int pfk_if_init(void);
extern void pfk_if_exit(void);

static int pfk_open(struct inode *inode, struct file *filp)
{
	if (filp->f_mode & FMODE_WRITE)
		return capable(CAP_NET_ADMIN) ? 0 : -EPERM;
	return 0;
}

static long pfk_ioctl_io(struct file *filp, unsigned int cmd,
	unsigned long arg)
{
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;
	return pfk_ioctl(filp, cmd, arg);
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

static int __init pfk_init(void)
{
	int err;

	pfk_net_lock_init();
	err = pfk_mbuf_init();
	if (err)
		return err;
	err = pfk_if_init();
	if (err)
		goto err_mbuf;
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
err_mbuf:
	pfk_mbuf_exit();
	return err;
}

static void __exit pfk_exit(void)
{
	misc_deregister(&pfk_dev);
	pfk_hooks_unregister();
	pfk_if_exit();
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
