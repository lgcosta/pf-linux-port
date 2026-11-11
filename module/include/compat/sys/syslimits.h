/* PFLINUX compat: <sys/syslimits.h> */
#ifndef _PFK_SYSLIMITS_H
#define _PFK_SYSLIMITS_H
#include <linux/limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define MAXSYMLINKS 8
#endif
