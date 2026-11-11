/* PFLINUX internal API (module-internal prototypes) */
#ifndef _PFK_API_H
#define _PFK_API_H
#include <pfk.h>
#include <net/pfvar.h>

long pfk_ioctl(struct file *f, unsigned int cmd, unsigned long addr);
int  pfk_enable(int on);
void pfk_status(struct pf_status *s);
int  pfk_hooks_register(void);
void pfk_hooks_unregister(void);

#endif
