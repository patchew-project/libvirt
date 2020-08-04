/*
 * secret_conf.h: internal <secret> XML handling API
 *
 * Copyright (C) 2009-2010, 2013-2014, 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

typedef struct _virSecretDef virSecretDef;
typedef virSecretDef *virSecretDefPtr;
struct _virSecretDef {
    bool isephemeral;
    bool isprivate;
    unsigned char uuid[VIR_UUID_BUFLEN];
    char *description;          /* May be NULL */
    int usage_type;  /* virSecretUsageType */
    char *usage_id; /* May be NULL */
};

void virSecretDefFree(virSecretDefPtr def);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(virSecretDef, virSecretDefFree);

virSecretDefPtr virSecretDefParseString(const char *xml);
virSecretDefPtr virSecretDefParseFile(const char *filename);
char *virSecretDefFormat(const virSecretDef *def);

#define VIR_CONNECT_LIST_SECRETS_FILTERS_EPHEMERAL \
                (VIR_CONNECT_LIST_SECRETS_EPHEMERAL     | \
                 VIR_CONNECT_LIST_SECRETS_NO_EPHEMERAL)

#define VIR_CONNECT_LIST_SECRETS_FILTERS_PRIVATE \
                (VIR_CONNECT_LIST_SECRETS_PRIVATE     | \
                 VIR_CONNECT_LIST_SECRETS_NO_PRIVATE)

#define VIR_CONNECT_LIST_SECRETS_FILTERS_ALL \
                (VIR_CONNECT_LIST_SECRETS_FILTERS_EPHEMERAL  | \
                 VIR_CONNECT_LIST_SECRETS_FILTERS_PRIVATE)
