/* PFLINUX compat: <net/toeplitz.h> — software Toeplitz hash (RFC 3305)
 * with the OpenBSD API shape used by PF (global cache + stoeplitz_* macros).
 * Implementation: compat/toeplitz.c (uses kernel crypto_shash "sha256"). */
#ifndef _PFK_TOEPLITZ_H
#define _PFK_TOEPLITZ_H
#include <linux/types.h>

#define TOEPLITZ_KEY_LEN	40

struct stoeplitz_cache {
	u8	key[TOEPLITZ_KEY_LEN];
	u8	keyhash[32];		/* SHA256(key) */
	int	initialized;
};

extern struct stoeplitz_cache *stoeplitz_cache;

void		stoeplitz_init(void);
void		stoeplitz_cache_init(struct stoeplitz_cache *, const u8 *key);
uint16_t	stoeplitz_hash_ip4(const struct stoeplitz_cache *,
		    const struct in_addr *, const struct in_addr *);
uint16_t	stoeplitz_hash_ip4port(const struct stoeplitz_cache *,
		    const struct in_addr *, const struct in_addr *,
		    u16, u16);
uint16_t	stoeplitz_hash_ip6(const struct stoeplitz_cache *,
		    const struct in6_addr *, const struct in6_addr *);
uint16_t	stoeplitz_hash_ip6port(const struct stoeplitz_cache *,
		    const struct in6_addr *, const struct in6_addr *,
		    u16, u16);
uint16_t	stoeplitz_hash_eaddr(const struct stoeplitz_cache *,
		    const u8 *);
uint16_t	stoeplitz_hash_n16(const struct stoeplitz_cache *, u16);
uint16_t	stoeplitz_hash_n32(const struct stoeplitz_cache *, u32);
uint16_t	stoeplitz_hash_h16(const struct stoeplitz_cache *, u16);
uint16_t	stoeplitz_hash_h32(const struct stoeplitz_cache *, u32);
uint16_t	stoeplitz_hash_h64(const struct stoeplitz_cache *, u64);

#define stoeplitz_n32(_n32)	stoeplitz_hash_n32(stoeplitz_cache, (_n32))
#define stoeplitz_h16(_h16)	stoeplitz_hash_h16(stoeplitz_cache, (_h16))
#define stoeplitz_h32(_h32)	stoeplitz_hash_h32(stoeplitz_cache, (_h32))
#define stoeplitz_h64(_h64)	stoeplitz_hash_h64(stoeplitz_cache, (_h64))
#define stoeplitz_ip4(_sa4, _da4) \
	stoeplitz_hash_ip4(stoeplitz_cache, (_sa4), (_da4))
#define stoeplitz_ip4port(_sa4, _da4, _sp, _dp) \
	stoeplitz_hash_ip4port(stoeplitz_cache, (_sa4), (_da4), (_sp), (_dp))
#define stoeplitz_ip6(_sa6, _da6) \
	stoeplitz_hash_ip6(stoeplitz_cache, (_sa6), (_da6))
#define stoeplitz_ip6port(_sa6, _da6, _sp, _dp) \
	stoeplitz_hash_ip6port(stoeplitz_cache, (_sa6), (_da6), (_sp), (_dp))
#define stoeplitz_eaddr(_ea) \
	stoeplitz_hash_eaddr(stoeplitz_cache, (_ea))

#endif
