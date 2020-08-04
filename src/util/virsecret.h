/*
 * virsecret.h: secret utility functions
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"

#include "virxml.h"
#include "virenum.h"

VIR_ENUM_DECL(virSecretUsage);

typedef enum {
    VIR_SECRET_LOOKUP_TYPE_NONE,
    VIR_SECRET_LOOKUP_TYPE_UUID,
    VIR_SECRET_LOOKUP_TYPE_USAGE,

    VIR_SECRET_LOOKUP_TYPE_LAST
} virSecretLookupType;

typedef struct _virSecretLookupTypeDef virSecretLookupTypeDef;
typedef virSecretLookupTypeDef *virSecretLookupTypeDefPtr;
struct _virSecretLookupTypeDef {
    int type;   /* virSecretLookupType */
    union {
        unsigned char uuid[VIR_UUID_BUFLEN];
        char *usage;
    } u;

};

void virSecretLookupDefClear(virSecretLookupTypeDefPtr def);
void virSecretLookupDefCopy(virSecretLookupTypeDefPtr dst,
                            const virSecretLookupTypeDef *src);
int virSecretLookupParseSecret(xmlNodePtr secretnode,
                               virSecretLookupTypeDefPtr def);
void virSecretLookupFormatSecret(virBufferPtr buf,
                                 const char *secrettype,
                                 virSecretLookupTypeDefPtr def);

int virSecretGetSecretString(virConnectPtr conn,
                             virSecretLookupTypeDefPtr seclookupdef,
                             virSecretUsageType secretUsageType,
                             uint8_t **ret_secret,
                             size_t *ret_secret_size)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(4)
    ATTRIBUTE_NONNULL(5) G_GNUC_WARN_UNUSED_RESULT;
