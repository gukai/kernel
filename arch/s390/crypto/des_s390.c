/*
 * Cryptographic API.
 *
 * s390 implementation of the DES Cipher Algorithm.
 *
 * Copyright IBM Corp. 2003,2011
 * Author(s): Thomas Spatzier
 *	      Jan Glauber (jan.glauber@de.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <crypto/algapi.h>
#include <linux/init.h>
#include <linux/module.h>

#include "crypt_s390.h"
#include "crypto_des.h"

#define DES_BLOCK_SIZE 8
#define DES_KEY_SIZE 8

#define DES3_128_KEY_SIZE	(2 * DES_KEY_SIZE)
#define DES3_128_BLOCK_SIZE	DES_BLOCK_SIZE

#define DES3_192_KEY_SIZE	(3 * DES_KEY_SIZE)
#define DES3_192_BLOCK_SIZE	DES_BLOCK_SIZE

#define DES3_KEY_SIZE           (3 * DES_KEY_SIZE)

static u8 *ctrblk;
static DEFINE_SPINLOCK(ctrblk_lock);

struct crypt_s390_des_ctx {
	u8 iv[DES_BLOCK_SIZE];
	u8 key[DES3_KEY_SIZE];
};

static int des_setkey(struct crypto_tfm *tfm, const u8 *key,
		      unsigned int keylen)
{
	struct crypt_s390_des_ctx *dctx = crypto_tfm_ctx(tfm);
	u32 *flags = &tfm->crt_flags;
	int ret;

	/* test if key is valid (not a weak key) */
	ret = crypto_des_check_key(key, keylen, flags);
	if (ret == 0)
		memcpy(dctx->key, key, keylen);
	return ret;
}

static void des_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct crypt_s390_des_ctx *dctx = crypto_tfm_ctx(tfm);

	crypt_s390_km(KM_DEA_ENCRYPT, dctx->key, out, in, DES_BLOCK_SIZE);
}

static void des_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct crypt_s390_des_ctx *dctx = crypto_tfm_ctx(tfm);

	crypt_s390_km(KM_DEA_DECRYPT, dctx->key, out, in, DES_BLOCK_SIZE);
}

static struct crypto_alg des_alg = {
	.cra_name		=	"des",
	.cra_driver_name	=	"des-s390",
	.cra_priority		=	CRYPT_S390_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypt_s390_des_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(des_alg.cra_list),
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	DES_KEY_SIZE,
			.cia_max_keysize	=	DES_KEY_SIZE,
			.cia_setkey		=	des_setkey,
			.cia_encrypt		=	des_encrypt,
			.cia_decrypt		=	des_decrypt,
		}
	}
};

static int ecb_desall_crypt(struct blkcipher_desc *desc, long func,
			    void *param, struct blkcipher_walk *walk)
{
	int ret = blkcipher_walk_virt(desc, walk);
	unsigned int nbytes;

	while ((nbytes = walk->nbytes)) {
		/* only use complete blocks */
		unsigned int n = nbytes & ~(DES_BLOCK_SIZE - 1);
		u8 *out = walk->dst.virt.addr;
		u8 *in = walk->src.virt.addr;

		ret = crypt_s390_km(func, param, out, in, n);
		BUG_ON((ret < 0) || (ret != n));

		nbytes &= DES_BLOCK_SIZE - 1;
		ret = blkcipher_walk_done(desc, walk, nbytes);
	}

	return ret;
}

static int cbc_desall_crypt(struct blkcipher_desc *desc, long func,
			    struct blkcipher_walk *walk)
{
	struct crypt_s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	int ret = blkcipher_walk_virt(desc, walk);
	unsigned int nbytes = walk->nbytes;
	struct {
		u8 iv[DES_BLOCK_SIZE];
		u8 key[DES3_KEY_SIZE];
	} param;

	if (!nbytes)
		goto out;

	memcpy(param.iv, walk->iv, DES_BLOCK_SIZE);
	memcpy(param.key, ctx->key, DES3_KEY_SIZE);
	do {
		/* only use complete blocks */
		unsigned int n = nbytes & ~(DES_BLOCK_SIZE - 1);
		u8 *out = walk->dst.virt.addr;
		u8 *in = walk->src.virt.addr;

		ret = crypt_s390_kmc(func, &param, out, in, n);
		BUG_ON((ret < 0) || (ret != n));

		nbytes &= DES_BLOCK_SIZE - 1;
		ret = blkcipher_walk_done(desc, walk, nbytes);
	} while ((nbytes = walk->nbytes));
	memcpy(walk->iv, param.iv, DES_BLOCK_SIZE);

out:
	return ret;
}

static int ecb_des_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct crypt_s390_des_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_desall_crypt(desc, KM_DEA_ENCRYPT, sctx->key, &walk);
}

static int ecb_des_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct crypt_s390_des_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_desall_crypt(desc, KM_DEA_DECRYPT, sctx->key, &walk);
}

static struct crypto_alg ecb_des_alg = {
	.cra_name		=	"ecb(des)",
	.cra_driver_name	=	"ecb-des-s390",
	.cra_priority		=	CRYPT_S390_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypt_s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(ecb_des_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	DES_KEY_SIZE,
			.max_keysize		=	DES_KEY_SIZE,
			.setkey			=	des_setkey,
			.encrypt		=	ecb_des_encrypt,
			.decrypt		=	ecb_des_decrypt,
		}
	}
};

static int cbc_des_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_desall_crypt(desc, KMC_DEA_ENCRYPT, &walk);
}

static int cbc_des_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_desall_crypt(desc, KMC_DEA_DECRYPT, &walk);
}

static struct crypto_alg cbc_des_alg = {
	.cra_name		=	"cbc(des)",
	.cra_driver_name	=	"cbc-des-s390",
	.cra_priority		=	CRYPT_S390_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypt_s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(cbc_des_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	DES_KEY_SIZE,
			.max_keysize		=	DES_KEY_SIZE,
			.ivsize			=	DES_BLOCK_SIZE,
			.setkey			=	des_setkey,
			.encrypt		=	cbc_des_encrypt,
			.decrypt		=	cbc_des_decrypt,
		}
	}
};

static unsigned int __ctrblk_init(u8 *ctrptr, unsigned int nbytes)
{
	unsigned int i, n;

	/* align to block size, max. PAGE_SIZE */
	n = (nbytes > PAGE_SIZE) ? PAGE_SIZE : nbytes & ~(DES_BLOCK_SIZE - 1);
	for (i = DES_BLOCK_SIZE; i < n; i += DES_BLOCK_SIZE) {
		memcpy(ctrptr + i, ctrptr + i - DES_BLOCK_SIZE, DES_BLOCK_SIZE);
		crypto_inc(ctrptr + i, DES_BLOCK_SIZE);
	}
	return n;
}

static int ctr_desall_crypt(struct blkcipher_desc *desc, long func,
			    struct crypt_s390_des_ctx *ctx,
			    struct blkcipher_walk *walk)
{
	int ret = blkcipher_walk_virt_block(desc, walk, DES_BLOCK_SIZE);
	unsigned int n, nbytes;
	u8 buf[DES_BLOCK_SIZE], ctrbuf[DES_BLOCK_SIZE];
	u8 *out, *in, *ctrptr = ctrbuf;

	if (!walk->nbytes)
		return ret;

	if (spin_trylock(&ctrblk_lock))
		ctrptr = ctrblk;

	memcpy(ctrptr, walk->iv, DES_BLOCK_SIZE);
	while ((nbytes = walk->nbytes) >= DES_BLOCK_SIZE) {
		out = walk->dst.virt.addr;
		in = walk->src.virt.addr;
		while (nbytes >= DES_BLOCK_SIZE) {
			if (ctrptr == ctrblk)
				n = __ctrblk_init(ctrptr, nbytes);
			else
				n = DES_BLOCK_SIZE;
			ret = crypt_s390_kmctr(func, ctx->key, out, in,
					       n, ctrptr);
			BUG_ON((ret < 0) || (ret != n));
			if (n > DES_BLOCK_SIZE)
				memcpy(ctrptr, ctrptr + n - DES_BLOCK_SIZE,
				       DES_BLOCK_SIZE);
			crypto_inc(ctrptr, DES_BLOCK_SIZE);
			out += n;
			in += n;
			nbytes -= n;
		}
		ret = blkcipher_walk_done(desc, walk, nbytes);
	}
	if (ctrptr == ctrblk) {
		if (nbytes)
			memcpy(ctrbuf, ctrptr, DES_BLOCK_SIZE);
		else
			memcpy(walk->iv, ctrptr, DES_BLOCK_SIZE);
		spin_unlock(&ctrblk_lock);
	} else {
		if (!nbytes)
			memcpy(walk->iv, ctrptr, DES_BLOCK_SIZE);
	}
	/* final block may be < DES_BLOCK_SIZE, copy only nbytes */
	if (nbytes) {
		out = walk->dst.virt.addr;
		in = walk->src.virt.addr;
		ret = crypt_s390_kmctr(func, ctx->key, buf, in,
				       DES_BLOCK_SIZE, ctrbuf);
		BUG_ON(ret < 0 || ret != DES_BLOCK_SIZE);
		memcpy(out, buf, nbytes);
		crypto_inc(ctrbuf, DES_BLOCK_SIZE);
		ret = blkcipher_walk_done(desc, walk, 0);
		memcpy(walk->iv, ctrbuf, DES_BLOCK_SIZE);
	}
	return ret;
}

static int ctr_des_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct crypt_s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ctr_desall_crypt(desc, KMCTR_DEA_ENCRYPT, ctx, &walk);
}

static int ctr_des_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct crypt_s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ctr_desall_crypt(desc, KMCTR_DEA_DECRYPT, ctx, &walk);
}

static struct crypto_alg ctr_des_alg = {
	.cra_name		=	"ctr(des)",
	.cra_driver_name	=	"ctr-des-s390",
	.cra_priority		=	CRYPT_S390_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	1,
	.cra_ctxsize		=	sizeof(struct crypt_s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(ctr_des_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	DES_KEY_SIZE,
			.max_keysize		=	DES_KEY_SIZE,
			.ivsize			=	DES_BLOCK_SIZE,
			.setkey			=	des_setkey,
			.encrypt		=	ctr_des_encrypt,
			.decrypt		=	ctr_des_decrypt,
		}
	}
};

/*
 * RFC2451:
 *
 *   For DES-EDE3, there is no known need to reject weak or
 *   complementation keys.  Any weakness is obviated by the use of
 *   multiple keys.
 *
 *   However, if the two  independent 64-bit keys are equal,
 *   then the DES3 operation is simply the same as DES.
 *   Implementers MUST reject keys that exhibit this property.
 *
 */
static int des3_128_setkey(struct crypto_tfm *tfm, const u8 *key,
			   unsigned int keylen)
{
	int i, ret;
	struct crypt_s390_des_ctx *dctx = crypto_tfm_ctx(tfm);
	const u8 *temp_key = key;
	u32 *flags = &tfm->crt_flags;

	if (!(memcmp(key, &key[DES_KEY_SIZE], DES_KEY_SIZE)) &&
	    (*flags & CRYPTO_TFM_REQ_WEAK_KEY)) {
		*flags |= CRYPTO_TFM_RES_WEAK_KEY;
		return -EINVAL;
	}
	for (i = 0; i < 2; i++, temp_key += DES_KEY_SIZE) {
		ret = crypto_des_check_key(temp_key, DES_KEY_SIZE, flags);
		if (ret < 0)
			return ret;
	}
	memcpy(dctx->key, key, keylen);
	return 0;
}

static void des3_128_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct crypt_s390_des_ctx *dctx = crypto_tfm_ctx(tfm);

	crypt_s390_km(KM_TDEA_128_ENCRYPT, dctx->key, dst, (void*)src,
		      DES3_128_BLOCK_SIZE);
}

static void des3_128_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct crypt_s390_des_ctx *dctx = crypto_tfm_ctx(tfm);

	crypt_s390_km(KM_TDEA_128_DECRYPT, dctx->key, dst, (void*)src,
		      DES3_128_BLOCK_SIZE);
}

static struct crypto_alg des3_128_alg = {
	.cra_name		=	"des3_ede128",
	.cra_driver_name	=	"des3_ede128-s390",
	.cra_priority		=	CRYPT_S390_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES3_128_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypt_s390_des_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(des3_128_alg.cra_list),
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	DES3_128_KEY_SIZE,
			.cia_max_keysize	=	DES3_128_KEY_SIZE,
			.cia_setkey		=	des3_128_setkey,
			.cia_encrypt		=	des3_128_encrypt,
			.cia_decrypt		=	des3_128_decrypt,
		}
	}
};

static int ecb_des3_128_encrypt(struct blkcipher_desc *desc,
				struct scatterlist *dst,
				struct scatterlist *src, unsigned int nbytes)
{
	struct crypt_s390_des_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_desall_crypt(desc, KM_TDEA_128_ENCRYPT, sctx->key, &walk);
}

static int ecb_des3_128_decrypt(struct blkcipher_desc *desc,
				struct scatterlist *dst,
				struct scatterlist *src, unsigned int nbytes)
{
	struct crypt_s390_des_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_desall_crypt(desc, KM_TDEA_128_DECRYPT, sctx->key, &walk);
}

static struct crypto_alg ecb_des3_128_alg = {
	.cra_name		=	"ecb(des3_ede128)",
	.cra_driver_name	=	"ecb-des3_ede128-s390",
	.cra_priority		=	CRYPT_S390_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	DES3_128_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypt_s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(
						ecb_des3_128_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	DES3_128_KEY_SIZE,
			.max_keysize		=	DES3_128_KEY_SIZE,
			.setkey			=	des3_128_setkey,
			.encrypt		=	ecb_des3_128_encrypt,
			.decrypt		=	ecb_des3_128_decrypt,
		}
	}
};

static int cbc_des3_128_encrypt(struct blkcipher_desc *desc,
				struct scatterlist *dst,
				struct scatterlist *src, unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_desall_crypt(desc, KMC_TDEA_128_ENCRYPT, &walk);
}

static int cbc_des3_128_decrypt(struct blkcipher_desc *desc,
				struct scatterlist *dst,
				struct scatterlist *src, unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_desall_crypt(desc, KMC_TDEA_128_DECRYPT, &walk);
}

static struct crypto_alg cbc_des3_128_alg = {
	.cra_name		=	"cbc(des3_ede128)",
	.cra_driver_name	=	"cbc-des3_ede128-s390",
	.cra_priority		=	CRYPT_S390_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	DES3_128_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypt_s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(
						cbc_des3_128_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	DES3_128_KEY_SIZE,
			.max_keysize		=	DES3_128_KEY_SIZE,
			.ivsize			=	DES3_128_BLOCK_SIZE,
			.setkey			=	des3_128_setkey,
			.encrypt		=	cbc_des3_128_encrypt,
			.decrypt		=	cbc_des3_128_decrypt,
		}
	}
};

/*
 * RFC2451:
 *
 *   For DES-EDE3, there is no known need to reject weak or
 *   complementation keys.  Any weakness is obviated by the use of
 *   multiple keys.
 *
 *   However, if the first two or last two independent 64-bit keys are
 *   equal (k1 == k2 or k2 == k3), then the DES3 operation is simply the
 *   same as DES.  Implementers MUST reject keys that exhibit this
 *   property.
 *
 */
static int des3_192_setkey(struct crypto_tfm *tfm, const u8 *key,
			   unsigned int keylen)
{
	int i, ret;
	struct crypt_s390_des_ctx *dctx = crypto_tfm_ctx(tfm);
	const u8 *temp_key = key;
	u32 *flags = &tfm->crt_flags;

	if (!(memcmp(key, &key[DES_KEY_SIZE], DES_KEY_SIZE) &&
	    memcmp(&key[DES_KEY_SIZE], &key[DES_KEY_SIZE * 2],
		   DES_KEY_SIZE)) &&
	    (*flags & CRYPTO_TFM_REQ_WEAK_KEY)) {
		*flags |= CRYPTO_TFM_RES_WEAK_KEY;
		return -EINVAL;
	}
	for (i = 0; i < 3; i++, temp_key += DES_KEY_SIZE) {
		ret = crypto_des_check_key(temp_key, DES_KEY_SIZE, flags);
		if (ret < 0)
			return ret;
	}
	memcpy(dctx->key, key, keylen);
	return 0;
}

static void des3_192_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct crypt_s390_des_ctx *dctx = crypto_tfm_ctx(tfm);

	crypt_s390_km(KM_TDEA_192_ENCRYPT, dctx->key, dst, (void*)src,
		      DES3_192_BLOCK_SIZE);
}

static void des3_192_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct crypt_s390_des_ctx *dctx = crypto_tfm_ctx(tfm);

	crypt_s390_km(KM_TDEA_192_DECRYPT, dctx->key, dst, (void*)src,
		      DES3_192_BLOCK_SIZE);
}

static struct crypto_alg des3_192_alg = {
	.cra_name		=	"des3_ede",
	.cra_driver_name	=	"des3_ede-s390",
	.cra_priority		=	CRYPT_S390_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES3_192_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypt_s390_des_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(des3_192_alg.cra_list),
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	DES3_192_KEY_SIZE,
			.cia_max_keysize	=	DES3_192_KEY_SIZE,
			.cia_setkey		=	des3_192_setkey,
			.cia_encrypt		=	des3_192_encrypt,
			.cia_decrypt		=	des3_192_decrypt,
		}
	}
};

static int ecb_des3_192_encrypt(struct blkcipher_desc *desc,
				struct scatterlist *dst,
				struct scatterlist *src, unsigned int nbytes)
{
	struct crypt_s390_des_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_desall_crypt(desc, KM_TDEA_192_ENCRYPT, sctx->key, &walk);
}

static int ecb_des3_192_decrypt(struct blkcipher_desc *desc,
				struct scatterlist *dst,
				struct scatterlist *src, unsigned int nbytes)
{
	struct crypt_s390_des_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_desall_crypt(desc, KM_TDEA_192_DECRYPT, sctx->key, &walk);
}

static struct crypto_alg ecb_des3_192_alg = {
	.cra_name		=	"ecb(des3_ede)",
	.cra_driver_name	=	"ecb-des3_ede-s390",
	.cra_priority		=	CRYPT_S390_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	DES3_192_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypt_s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(
						ecb_des3_192_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	DES3_192_KEY_SIZE,
			.max_keysize		=	DES3_192_KEY_SIZE,
			.setkey			=	des3_192_setkey,
			.encrypt		=	ecb_des3_192_encrypt,
			.decrypt		=	ecb_des3_192_decrypt,
		}
	}
};

static int cbc_des3_192_encrypt(struct blkcipher_desc *desc,
				struct scatterlist *dst,
				struct scatterlist *src, unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_desall_crypt(desc, KMC_TDEA_192_ENCRYPT, &walk);
}

static int cbc_des3_192_decrypt(struct blkcipher_desc *desc,
				struct scatterlist *dst,
				struct scatterlist *src, unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_desall_crypt(desc, KMC_TDEA_192_DECRYPT, &walk);
}

static struct crypto_alg cbc_des3_192_alg = {
	.cra_name		=	"cbc(des3_ede)",
	.cra_driver_name	=	"cbc-des3_ede-s390",
	.cra_priority		=	CRYPT_S390_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	DES3_192_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypt_s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(
						cbc_des3_192_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	DES3_192_KEY_SIZE,
			.max_keysize		=	DES3_192_KEY_SIZE,
			.ivsize			=	DES3_192_BLOCK_SIZE,
			.setkey			=	des3_192_setkey,
			.encrypt		=	cbc_des3_192_encrypt,
			.decrypt		=	cbc_des3_192_decrypt,
		}
	}
};

static int ctr_des3_encrypt(struct blkcipher_desc *desc,
			    struct scatterlist *dst, struct scatterlist *src,
			    unsigned int nbytes)
{
	struct crypt_s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ctr_desall_crypt(desc, KMCTR_TDEA_192_ENCRYPT, ctx, &walk);
}

static int ctr_des3_decrypt(struct blkcipher_desc *desc,
			    struct scatterlist *dst, struct scatterlist *src,
			    unsigned int nbytes)
{
	struct crypt_s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ctr_desall_crypt(desc, KMCTR_TDEA_192_DECRYPT, ctx, &walk);
}

static struct crypto_alg ctr_des3_alg = {
	.cra_name		=	"ctr(des3_ede)",
	.cra_driver_name	=	"ctr-des3_ede-s390",
	.cra_priority		=	CRYPT_S390_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	1,
	.cra_ctxsize		=	sizeof(struct crypt_s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(ctr_des3_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	DES3_192_KEY_SIZE,
			.max_keysize		=	DES3_192_KEY_SIZE,
			.ivsize			=	DES_BLOCK_SIZE,
			.setkey			=	des3_192_setkey,
			.encrypt		=	ctr_des3_encrypt,
			.decrypt		=	ctr_des3_decrypt,
		}
	}
};

static int des_s390_init(void)
{
	int ret = 0;

	if (!crypt_s390_func_available(KM_DEA_ENCRYPT, CRYPT_S390_MSA) ||
	    !crypt_s390_func_available(KM_TDEA_128_ENCRYPT, CRYPT_S390_MSA) ||
	    !crypt_s390_func_available(KM_TDEA_192_ENCRYPT, CRYPT_S390_MSA))
		return -EOPNOTSUPP;

	ret = crypto_register_alg(&des_alg);
	if (ret)
		goto des_err;
	ret = crypto_register_alg(&ecb_des_alg);
	if (ret)
		goto ecb_des_err;
	ret = crypto_register_alg(&cbc_des_alg);
	if (ret)
		goto cbc_des_err;

	ret = crypto_register_alg(&des3_128_alg);
	if (ret)
		goto des3_128_err;
	ret = crypto_register_alg(&ecb_des3_128_alg);
	if (ret)
		goto ecb_des3_128_err;
	ret = crypto_register_alg(&cbc_des3_128_alg);
	if (ret)
		goto cbc_des3_128_err;

	ret = crypto_register_alg(&des3_192_alg);
	if (ret)
		goto des3_192_err;
	ret = crypto_register_alg(&ecb_des3_192_alg);
	if (ret)
		goto ecb_des3_192_err;
	ret = crypto_register_alg(&cbc_des3_192_alg);
	if (ret)
		goto cbc_des3_192_err;

	if (crypt_s390_func_available(KMCTR_DEA_ENCRYPT,
			CRYPT_S390_MSA | CRYPT_S390_MSA4) &&
	    crypt_s390_func_available(KMCTR_TDEA_192_ENCRYPT,
			CRYPT_S390_MSA | CRYPT_S390_MSA4)) {
		ret = crypto_register_alg(&ctr_des_alg);
		if (ret)
			goto ctr_des_err;
		ret = crypto_register_alg(&ctr_des3_alg);
		if (ret)
			goto ctr_des3_err;
		ctrblk = (u8 *) __get_free_page(GFP_KERNEL);
		if (!ctrblk) {
			ret = -ENOMEM;
			goto ctr_mem_err;
		}
	}

out:
	return ret;

ctr_mem_err:
	crypto_unregister_alg(&ctr_des3_alg);
ctr_des3_err:
	crypto_unregister_alg(&ctr_des_alg);
ctr_des_err:
	crypto_unregister_alg(&cbc_des3_192_alg);
cbc_des3_192_err:
	crypto_unregister_alg(&ecb_des3_192_alg);
ecb_des3_192_err:
	crypto_unregister_alg(&des3_192_alg);
des3_192_err:
	crypto_unregister_alg(&cbc_des3_128_alg);
cbc_des3_128_err:
	crypto_unregister_alg(&ecb_des3_128_alg);
ecb_des3_128_err:
	crypto_unregister_alg(&des3_128_alg);
des3_128_err:
	crypto_unregister_alg(&cbc_des_alg);
cbc_des_err:
	crypto_unregister_alg(&ecb_des_alg);
ecb_des_err:
	crypto_unregister_alg(&des_alg);
des_err:
	goto out;
}

static void __exit des_s390_fini(void)
{
	if (ctrblk) {
		crypto_unregister_alg(&ctr_des_alg);
		crypto_unregister_alg(&ctr_des3_alg);
		free_page((unsigned long) ctrblk);
	}
	crypto_unregister_alg(&cbc_des3_192_alg);
	crypto_unregister_alg(&ecb_des3_192_alg);
	crypto_unregister_alg(&des3_192_alg);
	crypto_unregister_alg(&cbc_des3_128_alg);
	crypto_unregister_alg(&ecb_des3_128_alg);
	crypto_unregister_alg(&des3_128_alg);
	crypto_unregister_alg(&cbc_des_alg);
	crypto_unregister_alg(&ecb_des_alg);
	crypto_unregister_alg(&des_alg);
}

module_init(des_s390_init);
module_exit(des_s390_fini);

MODULE_ALIAS("des");
MODULE_ALIAS("des3_ede");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DES & Triple DES EDE Cipher Algorithms");
