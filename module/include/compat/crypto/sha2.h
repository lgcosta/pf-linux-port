/* PFLINUX compat: <crypto/sha2.h> — declarations matching OpenBSD's
 * sys/crypto/sha2.h API used by PF (SHA2_CTX union + SHA256/384/512).
 * Implementation: compat/sha2.c (vendored OpenBSD 8.0 source). */
#ifndef _PFK_SHA2_H
#define _PFK_SHA2_H

#define SHA256_BLOCK_LENGTH	64
#define SHA256_DIGEST_LENGTH	32
#define SHA384_BLOCK_LENGTH	128
#define SHA384_DIGEST_LENGTH	48
#define SHA512_BLOCK_LENGTH	128
#define SHA512_DIGEST_LENGTH	64

typedef struct _SHA2_CTX {
	union {
		u_int32_t	st32[8];
		u_int64_t	st64[8];
	} state;
	u_int64_t	bitcount[2];
	u_int8_t	buffer[SHA512_BLOCK_LENGTH];
} SHA2_CTX;

void SHA256Init(SHA2_CTX *);
void SHA256Update(SHA2_CTX *, const void *, size_t);
void SHA256Final(u_int8_t *, SHA2_CTX *);
void SHA384Init(SHA2_CTX *);
void SHA384Update(SHA2_CTX *, const void *, size_t);
void SHA384Final(u_int8_t *, SHA2_CTX *);
void SHA512Init(SHA2_CTX *);
void SHA512Update(SHA2_CTX *, const void *, size_t);
void SHA512Final(u_int8_t *, SHA2_CTX *);

#endif /* _PFK_SHA2_H */
