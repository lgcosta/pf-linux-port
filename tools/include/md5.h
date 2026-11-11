/* PFLINUX: <md5.h> — userland copy of OpenBSD 8.0 <sys/crypto/md5.h>
 * (from _vendor/upstream/sys/crypto/md5.h) with the kernel-only
 * __bounded__ attribute annotations dropped (glibc sys/cdefs.h has no
 * __bounded__) and <sys/types.h> added.  Algorithm and constants are
 * unchanged; PF_MD5_DIGEST_LENGTH checks in pfvar.h rely on
 * MD5_DIGEST_LENGTH == 16. */
#ifndef _MD5_H_
#define _MD5_H_

#include <sys/types.h>

#define	MD5_BLOCK_LENGTH		64
#define	MD5_DIGEST_LENGTH		16

typedef struct MD5Context {
	u_int32_t state[4];			/* state */
	u_int64_t count;			/* number of bits, mod 2^64 */
	u_int8_t buffer[MD5_BLOCK_LENGTH];	/* input buffer */
} MD5_CTX;

__BEGIN_DECLS
void	 MD5Init(MD5_CTX *);
void	 MD5Update(MD5_CTX *, const void *, size_t);
void	 MD5Final(u_int8_t [MD5_DIGEST_LENGTH], MD5_CTX *);
void	 MD5Transform(u_int32_t [4], const u_int8_t [MD5_BLOCK_LENGTH]);
__END_DECLS

#endif /* _MD5_H_ */
