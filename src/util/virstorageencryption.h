/*
 * virstorageencryption.h: volume encryption information
 *
 * Copyright (C) 2009-2011, 2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virbuffer.h"
#include "virsecret.h"
#include "virenum.h"

#include <libxml/tree.h>

typedef enum {
    VIR_STORAGE_ENCRYPTION_SECRET_TYPE_PASSPHRASE = 0,

    VIR_STORAGE_ENCRYPTION_SECRET_TYPE_LAST
} virStorageEncryptionSecretType;
VIR_ENUM_DECL(virStorageEncryptionSecret);

typedef struct _virStorageEncryptionSecret virStorageEncryptionSecret;
typedef virStorageEncryptionSecret *virStorageEncryptionSecretPtr;
struct _virStorageEncryptionSecret {
    int type; /* virStorageEncryptionSecretType */
    virSecretLookupTypeDef seclookupdef;
};

/* It's possible to dictate the cipher and if necessary iv */
typedef struct _virStorageEncryptionInfoDef virStorageEncryptionInfoDef;
typedef virStorageEncryptionInfoDef *virStorageEncryptionInfoDefPtr;
struct _virStorageEncryptionInfoDef {
    unsigned int cipher_size;
    char *cipher_name;
    char *cipher_mode;
    char *cipher_hash;
    char *ivgen_name;
    char *ivgen_hash;
};

typedef enum {
    /* "default" is only valid for volume creation */
    VIR_STORAGE_ENCRYPTION_FORMAT_DEFAULT = 0,
    VIR_STORAGE_ENCRYPTION_FORMAT_QCOW, /* Both qcow and qcow2 */
    VIR_STORAGE_ENCRYPTION_FORMAT_LUKS,

    VIR_STORAGE_ENCRYPTION_FORMAT_LAST,
} virStorageEncryptionFormatType;
VIR_ENUM_DECL(virStorageEncryptionFormat);

typedef struct _virStorageEncryption virStorageEncryption;
typedef virStorageEncryption *virStorageEncryptionPtr;
struct _virStorageEncryption {
    int format; /* virStorageEncryptionFormatType */
    int payload_offset;

    size_t nsecrets;
    virStorageEncryptionSecretPtr *secrets;

    virStorageEncryptionInfoDef encinfo;
};

virStorageEncryptionPtr virStorageEncryptionCopy(const virStorageEncryption *src)
    ATTRIBUTE_NONNULL(1);

void virStorageEncryptionFree(virStorageEncryptionPtr enc);

virStorageEncryptionPtr virStorageEncryptionParseNode(xmlNodePtr node,
                                                      xmlXPathContextPtr ctxt);
int virStorageEncryptionFormat(virBufferPtr buf,
                               virStorageEncryptionPtr enc);

/* A helper for VIR_STORAGE_ENCRYPTION_FORMAT_QCOW */
enum {
  VIR_STORAGE_QCOW_PASSPHRASE_SIZE = 16
};

int virStorageGenerateQcowPassphrase(unsigned char *dest);
