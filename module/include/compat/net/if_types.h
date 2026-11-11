/* PFLINUX compat: <net/if_types.h> — OpenBSD IFT_* constants
 * (values must match OpenBSD: they travel in pf_rule/pfk_if ioctls). */
#ifndef _PFK_IF_TYPES_H
#define _PFK_IF_TYPES_H
#define IFT_OTHER	0x1
#define IFT_ETHER	0x6
#define IFT_TOKENRING	0xf
#define IFT_FDDI	0x12
#define IFT_LOOP	0x18
#define IFT_SLIP	0x1f
#define IFT_PPPOINT	0x1e
#define IFT_GIF		0xe6
#define IFT_PFLOG	0xf0
#define IFT_PFSYNC	0xf1
#define IFT_CARP	0xf8
#endif
