/* PFLINUX compat: <sys/cdefs.h> minimal for queue.h/tree.h */
#ifndef _PFK_CDEFS_H
#define _PFK_CDEFS_H
#include <linux/compiler.h>
#define __BEGIN_DECLS
#define __END_DECLS
#ifndef __aligned
#define __aligned(x)	__attribute__((__aligned__(x)))
#endif
#ifndef __packed
#define __packed	__attribute__((__packed__))
#endif
/* NOTE: the kernel redefines 'inline' as a macro that expands to
 * 'inline __gnu_inline __inline_maybe_unused notrace'; any attribute placed
 * between those tokens breaks the parse. Therefore __unused expands to
 * nothing here (unused *static inline* functions do not warn in GCC). */
#ifndef __unused
#define __unused
#endif
#ifndef __dead
#define __dead		__attribute__((__noreturn__))
#endif
#ifndef __predict_false
#define __predict_false(x)	unlikely(x)
#endif
#ifndef __predict_true
#define __predict_true(x)	likely(x)
#endif
#ifndef __arraycount
#define __arraycount(a)	(sizeof(a)/sizeof((a)[0]))
#endif
#ifndef __predict
#define __predict(x, y)	((y) ? likely(x) : unlikely(x))
#endif
#ifndef __restrict
#define __restrict
#endif
#endif
