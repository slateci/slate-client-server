/*-
 * Copyright 2009 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file was originally written by Colin Percival as part of the Tarsnap
 * online backup system.
 */

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scrypt/crypto/crypto_aes.h"
#include "scrypt/crypto/crypto_aesctr.h"
#include "scrypt/crypto/crypto_entropy.h"
#include "scrypt/util/insecure_memzero.h"
#include "scrypt/alg/sha256.h"
#include "scrypt/util/sysendian.h"

#include "scrypt/crypto/crypto_scrypt.h"

#include "scrypt/scryptenc/scryptenc.h"

#define ENCBLOCK 65536

static int
scryptenc_setup(uint8_t header[96], uint8_t dk[64],
    const uint8_t * passwd, size_t passwdlen,
    int logN, uint64_t r, uint64_t p)
{
	uint8_t salt[32];
	uint8_t hbuf[32];
	uint64_t N;
	SHA256_CTX ctx;
	uint8_t * key_hmac = &dk[32];
	HMAC_SHA256_CTX hctx;
	int rc;

	/* Sanity check. */
	assert((logN > 0) && (logN < 256));
	
	N = (uint64_t)(1) << logN;

	/* Get some salt. */
	if (crypto_entropy_read(salt, 32))
		return (4);

	/* Generate the derived keys. */
	if (crypto_scrypt(passwd, passwdlen, salt, 32, N, r, p, dk, 64))
		return (3);

	/* Construct the file header. */
	memcpy(header, "scrypt", 6);
	header[6] = 0;
	header[7] = logN & 0xff;
	be32enc(&header[8], r);
	be32enc(&header[12], p);
	memcpy(&header[16], salt, 32);

	/* Add header checksum. */
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, header, 48);
	SHA256_Final(hbuf, &ctx);
	memcpy(&header[48], hbuf, 16);

	/* Add header signature (used for verifying password). */
	HMAC_SHA256_Init(&hctx, key_hmac, 32);
	HMAC_SHA256_Update(&hctx, header, 64);
	HMAC_SHA256_Final(hbuf, &hctx);
	memcpy(&header[64], hbuf, 32);

	/* Success! */
	return (0);
}

static int
scryptdec_setup(const uint8_t header[96], uint8_t dk[64],
    const uint8_t * passwd, size_t passwdlen)
{
	uint8_t salt[32];
	uint8_t hbuf[32];
	int logN;
	uint32_t r;
	uint32_t p;
	uint64_t N;
	SHA256_CTX ctx;
	uint8_t * key_hmac = &dk[32];
	HMAC_SHA256_CTX hctx;
	int rc;

	/* Parse N, r, p, salt. */
	logN = header[7];
	r = be32dec(&header[8]);
	p = be32dec(&header[12]);
	memcpy(salt, &header[16], 32);

	/* Verify header checksum. */
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, header, 48);
	SHA256_Final(hbuf, &ctx);
	if (memcmp(&header[48], hbuf, 16))
		return (7);

	/* Sanity check. */
	assert((logN > 0) && (logN < 256));
	
	/* Compute the derived keys. */
	N = (uint64_t)(1) << logN;
	if (crypto_scrypt(passwd, passwdlen, salt, 32, N, r, p, dk, 64))
		return (3);

	/* Check header signature (i.e., verify password). */
	HMAC_SHA256_Init(&hctx, key_hmac, 32);
	HMAC_SHA256_Update(&hctx, header, 64);
	HMAC_SHA256_Final(hbuf, &hctx);
	if (memcmp(hbuf, &header[64], 32))
		return (11);

	/* Success! */
	return (0);
}

/**
 * scryptenc_buf(inbuf, inbuflen, outbuf, passwd, passwdlen,
 *     logN, r, p):
 * Encrypt inbuflen bytes from inbuf, writing the resulting inbuflen + 128
 * bytes to outbuf.
 */
int
scryptenc_buf(const uint8_t * inbuf, size_t inbuflen, uint8_t * outbuf,
    const uint8_t * passwd, size_t passwdlen,
    int logN, uint64_t r, uint64_t p)
{
	uint8_t dk[64];
	uint8_t hbuf[32];
	uint8_t header[96];
	uint8_t * key_enc = dk;
	uint8_t * key_hmac = &dk[32];
	int rc;
	HMAC_SHA256_CTX hctx;
	struct crypto_aes_key * key_enc_exp;
	struct crypto_aesctr * AES;

	/* Generate the header and derived key. */
	if ((rc = scryptenc_setup(header, dk, passwd, passwdlen,
	    logN, r, p)) != 0)
		return (rc);

	/* Copy header into output buffer. */
	memcpy(outbuf, header, 96);

	/* Encrypt data. */
	if ((key_enc_exp = crypto_aes_key_expand(key_enc, 32)) == NULL)
		return (5);
	if ((AES = crypto_aesctr_init(key_enc_exp, 0)) == NULL)
		return (6);
	crypto_aesctr_stream(AES, inbuf, &outbuf[96], inbuflen);
	crypto_aesctr_free(AES);
	crypto_aes_key_free(key_enc_exp);

	/* Add signature. */
	HMAC_SHA256_Init(&hctx, key_hmac, 32);
	HMAC_SHA256_Update(&hctx, outbuf, 96 + inbuflen);
	HMAC_SHA256_Final(hbuf, &hctx);
	memcpy(&outbuf[96 + inbuflen], hbuf, 32);

	/* Zero sensitive data. */
	insecure_memzero(dk, 64);

	/* Success! */
	return (0);
}

/**
 * scryptdec_buf(inbuf, inbuflen, outbuf, outlen, passwd, passwdlen,
 *     logN, r, p):
 * Decrypt inbuflen bytes from inbuf, writing the result into outbuf and the
 * decrypted data length to outlen.  The allocated length of outbuf must
 * be at least inbuflen.  
 */
int
scryptdec_buf(const uint8_t * inbuf, size_t inbuflen, uint8_t * outbuf,
    size_t * outlen, const uint8_t * passwd, size_t passwdlen)
{
	uint8_t hbuf[32];
	uint8_t dk[64];
	uint8_t * key_enc = dk;
	uint8_t * key_hmac = &dk[32];
	int rc;
	HMAC_SHA256_CTX hctx;
	struct crypto_aes_key * key_enc_exp;
	struct crypto_aesctr * AES;

	/*
	 * All versions of the scrypt format will start with "scrypt" and
	 * have at least 7 bytes of header.
	 */
	if ((inbuflen < 7) || (memcmp(inbuf, "scrypt", 6) != 0))
		return (7);

	/* Check the format. */
	if (inbuf[6] != 0)
		return (8);

	/* We must have at least 128 bytes. */
	if (inbuflen < 128)
		return (7);

	/* Parse the header and generate derived keys. */
	if ((rc = scryptdec_setup(inbuf, dk, passwd, passwdlen)) != 0)
		return (rc);

	/* Decrypt data. */
	if ((key_enc_exp = crypto_aes_key_expand(key_enc, 32)) == NULL)
		return (5);
	if ((AES = crypto_aesctr_init(key_enc_exp, 0)) == NULL)
		return (6);
	crypto_aesctr_stream(AES, &inbuf[96], outbuf, inbuflen - 128);
	crypto_aesctr_free(AES);
	crypto_aes_key_free(key_enc_exp);
	*outlen = inbuflen - 128;

	/* Verify signature. */
	HMAC_SHA256_Init(&hctx, key_hmac, 32);
	HMAC_SHA256_Update(&hctx, inbuf, inbuflen - 32);
	HMAC_SHA256_Final(hbuf, &hctx);
	if (memcmp(hbuf, &inbuf[inbuflen - 32], 32))
		return (7);

	/* Zero sensitive data. */
	insecure_memzero(dk, 64);

	/* Success! */
	return (0);
}
