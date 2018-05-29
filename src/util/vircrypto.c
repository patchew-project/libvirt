/*
 * vircrypto.c: cryptographic helper APIs
 *
 * Copyright (C) 2014, 2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include "vircrypto.h"
#include "virlog.h"
#include "virerror.h"
#include "viralloc.h"
#include "virrandom.h"

#ifdef WITH_GNUTLS
# include <gnutls/gnutls.h>
# include <gnutls/crypto.h>
#endif

VIR_LOG_INIT("util.crypto");

#define VIR_FROM_THIS VIR_FROM_CRYPTO

static const char hex[] = "0123456789abcdef";

#define VIR_CRYPTO_LARGEST_DIGEST_SIZE VIR_CRYPTO_HASH_SIZE_SHA256

#if WITH_GNUTLS

struct virHashInfo {
    gnutls_digest_algorithm_t algorithm;
    size_t hashlen;
} hashinfo[] = {
    { GNUTLS_DIG_MD5, VIR_CRYPTO_HASH_SIZE_MD5 },
    { GNUTLS_DIG_SHA256, VIR_CRYPTO_HASH_SIZE_SHA256 },
};


verify(ARRAY_CARDINALITY(hashinfo) == VIR_CRYPTO_HASH_LAST);

ssize_t
virCryptoHashBuf(virCryptoHash hash,
                 const char *input,
                 unsigned char *output)
{
    int rc;
    if (hash >= VIR_CRYPTO_HASH_LAST) {
        virReportError(VIR_ERR_INVALID_ARG,
                       _("Unknown crypto hash %d"), hash);
        return -1;
    }

    rc = gnutls_hash_fast(hashinfo[hash].algorithm, input, strlen(input), output);
    if (rc < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unable to compute hash of data: %s"),
                       gnutls_strerror(rc));
        return -1;
    }

    return hashinfo[hash].hashlen;
}
#else
ssize_t
virCryptoHashBuf(virCryptoHash hash,
                 const char *input ATTRIBUTE_UNUSED,
                 unsigned char *output ATTRIBUTE_UNUSED)
{
    virReportError(VIR_ERR_INVALID_ARG,
                   _("algorithm=%d is not supported"), hash);
    return -1;
}
#endif

int
virCryptoHashString(virCryptoHash hash,
                    const char *input,
                    char **output)
{
    unsigned char buf[VIR_CRYPTO_LARGEST_DIGEST_SIZE];
    ssize_t rc;
    size_t hashstrlen;
    size_t i;

    if ((rc = virCryptoHashBuf(hash, input, buf)) < 0)
        return -1;

    hashstrlen = (rc * 2) + 1;

    if (VIR_ALLOC_N(*output, hashstrlen) < 0)
        return -1;

    for (i = 0; i < rc; i++) {
        (*output)[i * 2] = hex[(buf[i] >> 4) & 0xf];
        (*output)[(i * 2) + 1] = hex[buf[i] & 0xf];
    }

    return 0;
}


/* virCryptoHaveCipher:
 * @algorithm: Specific cipher algorithm desired
 *
 * Expected to be called prior to virCryptoEncryptData in order
 * to determine whether the requested encryption option is available,
 * so that "other" alternatives can be taken if the algorithm is
 * not available.
 *
 * Returns true if we can support the encryption.
 */
bool
virCryptoHaveCipher(virCryptoCipher algorithm)
{
    switch (algorithm) {

    case VIR_CRYPTO_CIPHER_AES256CBC:
#ifdef WITH_GNUTLS
        return true;
#else
        return false;
#endif

    case VIR_CRYPTO_CIPHER_NONE:
    case VIR_CRYPTO_CIPHER_LAST:
        break;
    };

    return false;
}


#ifdef WITH_GNUTLS
/* virCryptoEncryptDataAESgntuls:
 *
 * Performs the AES gnutls encryption
 *
 * Same input as virCryptoEncryptData, except the algorithm is replaced
 * by the specific gnutls algorithm.
 *
 * Encrypts the @data buffer using the @enckey and if available the @iv
 *
 * Returns 0 on success with the ciphertext being filled. It is the
 * caller's responsibility to clear and free it. Returns -1 on failure
 * w/ error set.
 */
static int
virCryptoEncryptDataAESgnutls(gnutls_cipher_algorithm_t gnutls_enc_alg,
                              uint8_t *enckey,
                              size_t enckeylen,
                              uint8_t *iv,
                              size_t ivlen,
                              uint8_t *data,
                              size_t datalen,
                              uint8_t **ciphertextret,
                              size_t *ciphertextlenret)
{
    int rc;
    size_t i;
    gnutls_cipher_hd_t handle = NULL;
    gnutls_datum_t enc_key;
    gnutls_datum_t iv_buf;
    uint8_t *ciphertext;
    size_t ciphertextlen;

    /* Allocate a padded buffer, copy in the data.
     *
     * NB, we must *always* have at least 1 byte of
     * padding - we can't skip it on multiples of
     * 16, otherwise decoder can't distinguish padded
     * data from non-padded data. Hence datalen + 1
     */
    ciphertextlen = VIR_ROUND_UP(datalen + 1, 16);
    if (VIR_ALLOC_N(ciphertext, ciphertextlen) < 0)
        return -1;
    memcpy(ciphertext, data, datalen);

     /* Fill in the padding of the buffer with the size of the padding
      * which is required for decryption. */
    for (i = datalen; i < ciphertextlen; i++)
        ciphertext[i] = ciphertextlen - datalen;

    /* Initialize the gnutls cipher */
    enc_key.size = enckeylen;
    enc_key.data = enckey;
    if (iv) {
        iv_buf.size = ivlen;
        iv_buf.data = iv;
    }
    if ((rc = gnutls_cipher_init(&handle, gnutls_enc_alg,
                                 &enc_key, &iv_buf)) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("failed to initialize cipher: '%s'"),
                       gnutls_strerror(rc));
        goto error;
    }

    /* Encrypt the data and free the memory for cipher operations */
    rc = gnutls_cipher_encrypt(handle, ciphertext, ciphertextlen);
    gnutls_cipher_deinit(handle);
    memset(&enc_key, 0, sizeof(gnutls_datum_t));
    memset(&iv_buf, 0, sizeof(gnutls_datum_t));
    if (rc < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("failed to encrypt the data: '%s'"),
                       gnutls_strerror(rc));
        goto error;
    }

    *ciphertextret = ciphertext;
    *ciphertextlenret = ciphertextlen;
    return 0;

 error:
    VIR_DISPOSE_N(ciphertext, ciphertextlen);
    memset(&enc_key, 0, sizeof(gnutls_datum_t));
    memset(&iv_buf, 0, sizeof(gnutls_datum_t));
    return -1;
}


/* virCryptoEncryptData:
 * @algorithm: algoritm desired for encryption
 * @enckey: encryption key
 * @enckeylen: encription key length
 * @iv: initialization vector
 * @ivlen: length of initialization vector
 * @data: data to encrypt
 * @datalen: length of data
 * @ciphertext: stream of bytes allocated to store ciphertext
 * @ciphertextlen: size of the stream of bytes
 *
 * If available, attempt and return the requested encryption type
 * using the parameters passed.
 *
 * Returns 0 on success, -1 on failure with error set
 */
int
virCryptoEncryptData(virCryptoCipher algorithm,
                     uint8_t *enckey,
                     size_t enckeylen,
                     uint8_t *iv,
                     size_t ivlen,
                     uint8_t *data,
                     size_t datalen,
                     uint8_t **ciphertext,
                     size_t *ciphertextlen)
{
    switch (algorithm) {
    case VIR_CRYPTO_CIPHER_AES256CBC:
        if (enckeylen != 32) {
            virReportError(VIR_ERR_INVALID_ARG,
                           _("AES256CBC encryption invalid keylen=%zu"),
                           enckeylen);
            return -1;
        }

        if (ivlen != 16) {
            virReportError(VIR_ERR_INVALID_ARG,
                           _("AES256CBC initialization vector invalid len=%zu"),
                           ivlen);
            return -1;
        }

        /*
         * Encrypt the data buffer using an encryption key and
         * initialization vector via the gnutls_cipher_encrypt API
         * for GNUTLS_CIPHER_AES_256_CBC.
         */
        return virCryptoEncryptDataAESgnutls(GNUTLS_CIPHER_AES_256_CBC,
                                             enckey, enckeylen, iv, ivlen,
                                             data, datalen,
                                             ciphertext, ciphertextlen);

    case VIR_CRYPTO_CIPHER_NONE:
    case VIR_CRYPTO_CIPHER_LAST:
        break;
    }

    virReportError(VIR_ERR_INVALID_ARG,
                   _("algorithm=%d is not supported"), algorithm);
    return -1;
}

#else

int
virCryptoEncryptData(virCryptoCipher algorithm,
                     uint8_t *enckey ATTRIBUTE_UNUSED,
                     size_t enckeylen ATTRIBUTE_UNUSED,
                     uint8_t *iv ATTRIBUTE_UNUSED,
                     size_t ivlen ATTRIBUTE_UNUSED,
                     uint8_t *data ATTRIBUTE_UNUSED,
                     size_t datalen ATTRIBUTE_UNUSED,
                     uint8_t **ciphertext ATTRIBUTE_UNUSED,
                     size_t *ciphertextlen ATTRIBUTE_UNUSED)
{
    virReportError(VIR_ERR_INVALID_ARG,
                   _("algorithm=%d is not supported"), algorithm);
    return -1;
}
#endif

/* virCryptoGenerateRandom:
 * @buf: Pointer to location to store bytes
 * @buflen: Number of bytes to store
 *
 * Generate a random stream of @buflen length and store it into @buf.
 *
 * Since the gnutls_rnd could be missing, provide an alternate less
 * secure mechanism to at least have something.
 *
 * Returns 0 on success or -1 on failure (with error reported)
 */
int
virCryptoGenerateRandom(unsigned char *buf,
                        size_t buflen)
{
    int rv;

#if WITH_GNUTLS
    /* Generate the byte stream using gnutls_rnd() if possible */
    if ((rv = gnutls_rnd(GNUTLS_RND_RANDOM, buf, buflen)) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("failed to generate byte stream: %s"),
                       gnutls_strerror(rv));
        return -1;
    }
#else
    /* If we don't have gnutls_rnd(), we will generate a less cryptographically
     * strong master buf from /dev/urandom.
     */
    if ((rv = virRandomBytes(buf, buflen)) < 0) {
        virReportSystemError(-rv, "%s", _("failed to generate byte stream"));
        return -1;
    }
#endif

    return 0;
}
