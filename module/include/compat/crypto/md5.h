/* PFLINUX compat: <crypto/md5.h> — MD5 as used by pf_ctl ruleset checksum.
 * Implementation: compat/md5.c (vendored OpenBSD 8.0 source). */
#ifndef _PFK_MD5_H
#define _PFK_MD5_H

#define MD5_BLOCK_LENGTH	64
#define MD5_DIGEST_LENGTH	16
#define MD5_DIGEST_STRING_LENGTH	(MD5_DIGEST_LENGTH * 2 + 1)

typedef struct MD5Context {
	u_int32_t state[4];
	u_int64_t count;
	u_int8_t buffer[MD5_BLOCK_LENGTH];
} MD5_CTX;

void MD5Init(MD5_CTX *);
void MD5Update(MD5_CTX *, const void *, size_t);
void MD5Final(u_int8_t[MD5_DIGEST_LENGTH], MD5_CTX *);
void MD5Pad(MD5_CTX *);
void MD5Transform(u_int32_t[4], const u_int8_t[MD5_BLOCK_LENGTH]);
char *MD5End(MD5_CTX *, char *);

#endif /* _PFK_MD5_H */
