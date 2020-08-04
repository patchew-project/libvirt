/*
 * virnwfilterbindingdef.h: network filter binding XML processing
 *
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "virmacaddr.h"
#include "virhash.h"
#include "virbuffer.h"
#include "virxml.h"

typedef struct _virNWFilterBindingDef virNWFilterBindingDef;
typedef virNWFilterBindingDef *virNWFilterBindingDefPtr;

struct _virNWFilterBindingDef {
    char *ownername;
    unsigned char owneruuid[VIR_UUID_BUFLEN];
    char *portdevname;
    char *linkdevname;
    virMacAddr mac;
    char *filter;
    virHashTablePtr filterparams;
};


void
virNWFilterBindingDefFree(virNWFilterBindingDefPtr binding);
virNWFilterBindingDefPtr
virNWFilterBindingDefCopy(virNWFilterBindingDefPtr src);

virNWFilterBindingDefPtr
virNWFilterBindingDefParseNode(xmlDocPtr xml,
                               xmlNodePtr root);

virNWFilterBindingDefPtr
virNWFilterBindingDefParseString(const char *xml);

virNWFilterBindingDefPtr
virNWFilterBindingDefParseFile(const char *filename);

char *
virNWFilterBindingDefFormat(const virNWFilterBindingDef *def);

int
virNWFilterBindingDefFormatBuf(virBufferPtr buf,
                               const virNWFilterBindingDef *def);
