/* PFLINUX compat: <crypto/siphash.h> — SipHash-2-4 (Aumasson–Bernstein)
 * API compatible with OpenBSD's SIPHASH_CTX/KEY usage in pf_syncookies.c.
 * Implementation: compat/siphash.c (ISC 2026). */
#ifndef _PFK_SIPHASH_H
#define _PFK_SIPHASH_H
#include <linux/types.h>

#define SIPHASH_KEY_LENGTH   16

typedef struct {
	__u64 v[2];
} SIPHASH_KEY;

typedef struct {
	__u64 v[3];
	__u64 total_len;
	unsigned char block[8];
	unsigned char block_len;
} SIPHASH_CTX;

void SipHash24_Init(SIPHASH_CTX *ctx, const SIPHASH_KEY *key);
void SipHash24_Update(SIPHASH_CTX *ctx, const void *in, __u64 inlen);
void SipHash24_Final(void *out, SIPHASH_CTX *ctx);
void SipHash24_Generate(const SIPHASH_KEY *key, const void *in, __u64 inlen,
	unsigned char *out);
/* OpenBSD's one-shot helper: returns the 64-bit hash value */
__u64 SipHash24(const SIPHASH_KEY *key, const void *in, __u64 inlen);

#endif
