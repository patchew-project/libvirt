/*
 * vircrypto.h: cryptographic helper APIs
 *
 * Copyright (C) 2014, 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

#define VIR_CRYPTO_HASH_SIZE_MD5 16
#define VIR_CRYPTO_HASH_SIZE_SHA256 32

typedef enum {
    VIR_CRYPTO_HASH_MD5, /* Don't use this except for historic compat */
    VIR_CRYPTO_HASH_SHA256,

    VIR_CRYPTO_HASH_LAST
} virCryptoHash;


typedef enum {
    VIR_CRYPTO_CIPHER_NONE = 0,
    VIR_CRYPTO_CIPHER_AES256CBC,

    VIR_CRYPTO_CIPHER_LAST
} virCryptoCipher;

ssize_t
virCryptoHashBuf(virCryptoHash hash,
                 const char *input,
                 unsigned char *output)
    ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3)
    G_GNUC_WARN_UNUSED_RESULT;

int
virCryptoHashString(virCryptoHash hash,
                    const char *input,
                    char **output)
    ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3)
    G_GNUC_WARN_UNUSED_RESULT;

bool virCryptoHaveCipher(virCryptoCipher algorithm);

int virCryptoEncryptData(virCryptoCipher algorithm,
                         uint8_t *enckey, size_t enckeylen,
                         uint8_t *iv, size_t ivlen,
                         uint8_t *data, size_t datalen,
                         uint8_t **ciphertext, size_t *ciphertextlen)
    ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(6)
    ATTRIBUTE_NONNULL(8) ATTRIBUTE_NONNULL(9) G_GNUC_WARN_UNUSED_RESULT;
