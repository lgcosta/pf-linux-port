/* PFLINUX compat: <sys/timeout.h> */
#ifndef _PFK_TIMEOUT_H
#define _PFK_TIMEOUT_H
#include <linux/timer.h>
#include <linux/workqueue.h>
typedef void (timeout_fn)(void *);
struct timeout {
	struct timer_list	timer;
	struct work_struct	work;
	timeout_fn		*fn;
	void			*arg;
	unsigned long		deadline_jif;
	int			period_ms;
	int			pending;
	int			running;
};
#endif
