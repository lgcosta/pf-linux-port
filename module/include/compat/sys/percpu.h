/* PFLINUX compat: <sys/percpu.h> — OpenBSD cpumem over a
 * per-CPU-slot array (preempt disabled during access; counters only). */
#ifndef _PFK_SYSPERCPU_H
#define _PFK_SYSPERCPU_H

struct cpumem {
	void	*cm_base;
	size_t	cm_item;
	int	cm_ncpu;
};

struct cpumem_iter {
	unsigned int	cpumem_iter_num;
	unsigned int	cpumem_iter_cpus;
	unsigned int	cpumem_iter_cpu;
	unsigned long	cpumem_iter_obj;
	struct cpumem	*cm;
};

static inline struct cpumem *
cpumem_malloc(size_t item, int flags)
{
	struct cpumem *cm;
	gfp_t g = (flags & M_WAITOK) ? GFP_KERNEL : GFP_ATOMIC;

	cm = kmalloc(sizeof(*cm), g);
	if (!cm)
		return NULL;
	cm->cm_ncpu = num_possible_cpus();
	cm->cm_base = vzalloc((size_t)cm->cm_ncpu * item);
	if (!cm->cm_base) {
		kfree(cm);
		return NULL;
	}
	cm->cm_item = item;
	return cm;
}

static inline void *
cpumem_enter(struct cpumem *cm)
{
	preempt_disable();
	return (char *)cm->cm_base + (size_t)smp_processor_id() * cm->cm_item;
}

static inline void
cpumem_leave(struct cpumem *cm, void *p)
{
	preempt_enable();
}

static inline void
cpumem_free(struct cpumem *cm, int size)
{
	if (!cm)
		return;
	vfree(cm->cm_base);
	kfree(cm);
}

/* BSD CPUMEM_FOREACH: iterate the per-CPU slots of a cpumem area. */
static inline void *
pfk_cpumem_at(struct cpumem *cm, unsigned int cpu)
{
	if (!cm || !cm->cm_base || cpu >= (unsigned int)cm->cm_ncpu)
		return NULL;
	return (char *)cm->cm_base + (size_t)cpu * cm->cm_item;
}

static inline void *
pfk_cpumem_first(struct cpumem_iter *it, struct cpumem *cm)
{
	it->cpumem_iter_num = 0;
	it->cm = cm;
	return pfk_cpumem_at(cm, 0);
}

static inline void *
pfk_cpumem_next(struct cpumem_iter *it, struct cpumem *cm)
{
	it->cpumem_iter_num++;
	if (it->cpumem_iter_num >= (unsigned int)nr_cpu_ids)
		return NULL;
	return pfk_cpumem_at(cm, it->cpumem_iter_num);
}

#define CPUMEM_FOREACH(var, it, cm)					\
	for ((var) = pfk_cpumem_first((it), (cm));			\
	     (var) != NULL;						\
	     (var) = pfk_cpumem_next((it), (cm)))

/* BSD cpumem_ref/cpumem_modify aliases */
static inline void *
cpumem_ref(struct cpumem *cm)
{
	preempt_disable();
	return (char *)cm->cm_base + (size_t)smp_processor_id() * cm->cm_item;
}

static inline void
cpumem_drop(struct cpumem *cm, void *p)
{
	preempt_enable();
}

#endif
