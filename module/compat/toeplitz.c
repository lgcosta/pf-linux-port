/*
 * Software Toeplitz hash (RFC 3305) for PF flow ids — PFLINUX.
 * The key window is SHA256(key) || key; the 16-bit result is the XOR of
 * the 16-bit windows selected by the set bits of the input.
 * ISC (c) 2026 PF-on-Linux contributors.
 */
#include <pfk.h>
#include <crypto/hash.h>
#include "compat/net/toeplitz.h"

struct stoeplitz_cache *stoeplitz_cache;

static int
toeplitz_keyhash(const u8 *key, u8 *out /*32*/)
{
	struct crypto_shash *tfm;
	char descbuf[sizeof(struct shash_desc) + 512];
	struct shash_desc *desc = (struct shash_desc *)descbuf;
	int err;

	tfm = crypto_alloc_shash("sha256", 0, 0);
	if (!tfm)
		return -ENOMEM;
	desc->tfm = tfm;
	err = crypto_shash_digest(desc, key, TOEPLITZ_KEY_LEN, out);
	crypto_free_shash(tfm);
	return err;
}

/* core: hash `inlen` bytes of `in` (MSB-first bit order) into 16 bits */
static uint16_t
toeplitz_bits(const struct stoeplitz_cache *sc, const u8 *in, int inlen)
{
	u8 window[64 + 32];	/* keyhash(32) || key(40) */
	int i, bit, k;
	uint16_t res = 0;

	memcpy(window, sc->keyhash, 32);
	memcpy(window + 32, sc->key, TOEPLITZ_KEY_LEN);

	for (i = 0; i < inlen; i++) {
		for (bit = 0; bit < 8; bit++) {
			if (!(in[i] & (0x80 >> bit)))
				continue;
			for (k = 0; k < 16; k++) {
				int pos = i * 8 + bit + k;
				int byte = pos / 8;
				int b = 7 - (pos % 8);

				res ^= (uint16_t)((window[byte] >> b) & 1) << k;
			}
		}
	}
	return res;
}

void
stoeplitz_cache_init(struct stoeplitz_cache *sc, const u8 *key)
{
	memset(sc, 0, sizeof(*sc));
	memcpy(sc->key, key, TOEPLITZ_KEY_LEN);
	toeplitz_keyhash(key, sc->keyhash);
	sc->initialized = 1;
}

void
stoeplitz_init(void)
{
	static u8 key[TOEPLITZ_KEY_LEN];
	int i;

	if (stoeplitz_cache && stoeplitz_cache->initialized)
		return;
	for (i = 0; i < TOEPLITZ_KEY_LEN; i++)
		key[i] = get_random_u8();
	stoeplitz_cache = kzalloc(sizeof(*stoeplitz_cache), GFP_KERNEL);
	if (!stoeplitz_cache)
		return;
	stoeplitz_cache_init(stoeplitz_cache, key);
}

uint16_t
stoeplitz_hash_n16(const struct stoeplitz_cache *sc, u16 n16)
{
	u8 b[2] = { (u8)(n16 >> 8), (u8)n16 };

	return toeplitz_bits(sc, b, sizeof(b));
}

uint16_t
stoeplitz_hash_n32(const struct stoeplitz_cache *sc, u32 n32)
{
	return stoeplitz_hash_n16(sc, (u16)(n32 ^ (n32 >> 16)));
}

uint16_t
stoeplitz_hash_h16(const struct stoeplitz_cache *sc, u16 h16)
{
	u8 b[2] = { (u8)(h16 >> 8), (u8)h16 };

	return toeplitz_bits(sc, b, sizeof(b));
}

uint16_t
stoeplitz_hash_h32(const struct stoeplitz_cache *sc, u32 h32)
{
	return stoeplitz_hash_h16(sc, (u16)(h32 ^ (h32 >> 16)));
}

uint16_t
stoeplitz_hash_h64(const struct stoeplitz_cache *sc, u64 h64)
{
	return stoeplitz_hash_h32(sc, (u32)(h64 ^ (h64 >> 32)));
}

uint16_t
stoeplitz_hash_ip4(const struct stoeplitz_cache *sc,
	const struct in_addr *sa, const struct in_addr *da)
{
	u8 buf[8];

	memcpy(buf, &sa->s_addr, 4);
	memcpy(buf + 4, &da->s_addr, 4);
	return toeplitz_bits(sc, buf, sizeof(buf));
}

uint16_t
stoeplitz_hash_ip4port(const struct stoeplitz_cache *sc,
	const struct in_addr *sa, const struct in_addr *da, u16 sp, u16 dp)
{
	u8 buf[12];

	memcpy(buf, &sa->s_addr, 4);
	memcpy(buf + 4, &da->s_addr, 4);
	buf[8] = (u8)(sp >> 8); buf[9] = (u8)sp;
	buf[10] = (u8)(dp >> 8); buf[11] = (u8)dp;
	return toeplitz_bits(sc, buf, sizeof(buf));
}

uint16_t
stoeplitz_hash_ip6(const struct stoeplitz_cache *sc,
	const struct in6_addr *sa, const struct in6_addr *da)
{
	u8 buf[32];

	memcpy(buf, sa, 16);
	memcpy(buf + 16, da, 16);
	return toeplitz_bits(sc, buf, sizeof(buf));
}

uint16_t
stoeplitz_hash_ip6port(const struct stoeplitz_cache *sc,
	const struct in6_addr *sa, const struct in6_addr *da, u16 sp, u16 dp)
{
	u8 buf[36];

	memcpy(buf, sa, 16);
	memcpy(buf + 16, da, 16);
	buf[32] = (u8)(sp >> 8); buf[33] = (u8)sp;
	buf[34] = (u8)(dp >> 8); buf[35] = (u8)dp;
	return toeplitz_bits(sc, buf, sizeof(buf));
}

uint16_t
stoeplitz_hash_eaddr(const struct stoeplitz_cache *sc, const u8 *ea)
{
	return toeplitz_bits(sc, ea, 6);
}
