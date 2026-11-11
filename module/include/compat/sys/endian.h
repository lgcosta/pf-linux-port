/* PFLINUX compat: <sys/endian.h> */
#ifndef _PFK_SYS_ENDIAN_H
#define _PFK_SYS_ENDIAN_H
#include <linux/byteorder/generic.h>
#include <linux/byteorder/little_endian.h>
#include <linux/stddef.h>
/* BSD byte-order names used by ported code */
#define LITTLE_ENDIAN	1234
#define BIG_ENDIAN	4321
#define PDP_ENDIAN	3412
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BYTE_ORDER	BIG_ENDIAN
#else
#define BYTE_ORDER	LITTLE_ENDIAN
#endif
#define swap16(x)	swab16(x)
#define swap32(x)	swab32(x)
#define swap64(x)	swab64(x)
#endif
