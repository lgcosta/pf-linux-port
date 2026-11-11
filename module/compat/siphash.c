/*
 * SipHash-2-4 implementation (PFLINUX) — reference algorithm by Jean-Philippe
 * Aumasson & Daniel J. Bernstein, public domain reference semantics.
 * ISC (c) 2026 PF-on-Linux contributors.
 */
#include <pfk.h>
#include "compat/crypto/siphash.h"

#define ROTL64(x, b) (u64)(((x) << (b)) | ((x) >> (64 - (b))))

#define SIPROUND \
	do { \
		v0 += v1; v1 = ROTL64(v1, 13); v1 ^= v0; v0 = ROTL64(v0, 32); \
		v2 += v3; v3 = ROTL64(v3, 16); v3 ^= v2; \
		v0 += v3; v3 = ROTL64(v3, 21); v3 ^= v0; \
		v2 += v1; v1 = ROTL64(v1, 17); v1 ^= v2; v2 = ROTL64(v2, 32); \
	} while (0)

#define U8TO64_LE(p) \
	(((u64)((p)[0])) | ((u64)((p)[1]) << 8) | ((u64)((p)[2]) << 16) | \
	 ((u64)((p)[3]) << 24) | ((u64)((p)[4]) << 32) | ((u64)((p)[5]) << 40) | \
	 ((u64)((p)[6]) << 48) | ((u64)((p)[7]) << 56))

#define U64TO8_LE(p, v) \
	do { \
		(p)[0] = (u8)((v) & 0xff); (p)[1] = (u8)((v) >> 8); \
		(p)[2] = (u8)((v) >> 16); (p)[3] = (u8)((v) >> 24); \
		(p)[4] = (u8)((v) >> 32); (p)[5] = (u8)((v) >> 40); \
		(p)[6] = (u8)((v) >> 48); (p)[7] = (u8)((v) >> 56); \
	} while (0)

void
SipHash24_Init(SIPHASH_CTX *ctx, const SIPHASH_KEY *key)
{
	ctx->v[0] = 0x736f6d6570736575ULL ^ key->v[0];
	ctx->v[1] = 0x646f72616e646f6dULL ^ key->v[1];
	ctx->v[2] = 0x6c7967656e657261ULL ^ key->v[0];
	ctx->v[3] = 0x7465646279746573ULL ^ key->v[1];
	ctx->total_len = 0;
	ctx->block_len = 0;
	memset(ctx->block, 0, sizeof(ctx->block));
}

void
SipHash24_Update(SIPHASH_CTX *ctx, const void *in, __u64 inlen)
{
	const unsigned char *ni = in;
	u64 total = ctx->total_len + inlen;
	int left = ctx->block_len;
	u64 i;

	ctx->total_len = total;
	if (left) {
		unsigned int take = 8 - left;

		if (take > inlen)
			take = inlen;
		memcpy(ctx->block + left, ni, take);
		left += take;
		ni += take;
		inlen -= take;
		if (left == 8) {
			u64 m = U8TO64_LE(ctx->block);
			u64 v0 = ctx->v[0], v1 = ctx->v[1];
			u64 v2 = ctx->v[2], v3 = ctx->v[3];

			v3 ^= m;
			SIPROUND; SIPROUND;
			v0 ^= m;
			ctx->v[0] = v0; ctx->v[1] = v1;
			ctx->v[2] = v2; ctx->v[3] = v3;
			ctx->block_len = 0;
		}
	}
	while (inlen >= 8) {
		u64 m = U8TO64_LE(ni);
		u64 v0 = ctx->v[0], v1 = ctx->v[1];
		u64 v2 = ctx->v[2], v3 = ctx->v[3];

		v3 ^= m;
		SIPROUND; SIPROUND;
		v0 ^= m;
		ctx->v[0] = v0; ctx->v[1] = v1;
		ctx->v[2] = v2; ctx->v[3] = v3;
		ni += 8; inlen -= 8;
	}
	if (inlen) {
		memcpy(ctx->block, ni, inlen);
		ctx->block_len = inlen;
	}
	(void)i;
}

void
SipHash24_Final(SIPHASH_CTX *ctx, unsigned char *out)
{
	u64 b = ((u64)ctx->total_len) << 56;
	u64 v0 = ctx->v[0], v1 = ctx->v[1], v2 = ctx->v[2], v3 = ctx->v[3];
	unsigned char t[8];
	int i;

	U64TO8_LE(t, b);
	for (i = 0; i < ctx->block_len; i++)
		t[i] = ctx->block[i];

	v3 ^= b;
	SIPROUND; SIPROUND;
	v0 ^= b;
	v2 ^= 0xff;
	SIPROUND; SIPROUND; SIPROUND; SIPROUND;
	U64TO8_LE(out, v0 ^ v1 ^ v2 ^ v3);
	/* wipe */
	ctx->v[0] = ctx->v[1] = ctx->v[2] = ctx->v[3] = 0;
}

void
SipHash24_Generate(const SIPHASH_KEY *key, const void *in, __u64 inlen,
	unsigned char *out)
{
	u64 k0 = key->v[0], k1 = key->v[1];
	u64 v0, v1, v2, v3;

	v0 = 0x736f6d6570736575ULL ^ k0;
	v1 = 0x646f72616e646f6dULL ^ k1;
	v2 = 0x6c7967656e657261ULL ^ k0;
	v3 = 0x7465646279746573ULL ^ k1;

	while (inlen >= 8) {
		u64 m = U8TO64_LE((const unsigned char *)in);

		v3 ^= m;
		SIPROUND; SIPROUND;
		v0 ^= m;
		in = (const unsigned char *)in + 8;
		inlen -= 8;
	}
	{
		unsigned char t[8];
		u64 b = ((u64)inlen) << 56;
		int i;

		memset(t, 0, sizeof(t));
		memcpy(t, in, inlen);
		for (i = 0; i < 8; i++)
			t[i] ^= (unsigned char)(b >> (8 * i));
		{
			u64 m = U8TO64_LE(t);

			v3 ^= m;
			SIPROUND; SIPROUND;
			v0 ^= m;
		}
		v2 ^= 0xff;
		SIPROUND; SIPROUND; SIPROUND; SIPROUND;
		U64TO8_LE(out, v0 ^ v1 ^ v2 ^ v3);
	}
}
