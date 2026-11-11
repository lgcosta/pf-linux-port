/* PFLINUX internal API (module-internal prototypes) */
#ifndef _PFK_API_H
#define _PFK_API_H
#include <pfk.h>
#include <net/pfvar.h>

long pfk_ioctl(struct file *f, unsigned int cmd, unsigned long addr);
int  pfk_enable(int on);
void pfk_status(struct pf_status *s);
#define PFK_SKIP_PF	0x50460040u	/* skb->mark: reinjected, bypass pf */

int  pfk_hooks_register(void);
void pfk_hooks_unregister(void);
/* AF_DIVERT family (module-internal socket protocol, PF_DIVERT=32) */
int  pfk_divert_sock_register(void);
void pfk_divert_sock_unregister(void);
void divert_packet(struct mbuf *, int, u_int16_t);
void divert6_packet(struct mbuf *, int, u_int16_t);
void pfk_hooks_unregister(void);

#endif
