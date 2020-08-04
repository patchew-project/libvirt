/*
 * Copyright (C) 2009-2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virenum.h"

typedef enum {
    VIR_NATIVE_VLAN_MODE_DEFAULT = 0,
    VIR_NATIVE_VLAN_MODE_TAGGED,
    VIR_NATIVE_VLAN_MODE_UNTAGGED,

    VIR_NATIVE_VLAN_MODE_LAST
} virNativeVlanMode;

VIR_ENUM_DECL(virNativeVlanMode);

typedef struct _virNetDevVlan virNetDevVlan;
typedef virNetDevVlan *virNetDevVlanPtr;
struct _virNetDevVlan {
    bool trunk;        /* true if this is a trunk */
    int nTags;          /* number of tags in array */
    unsigned int *tag; /* pointer to array of tags */
    int nativeMode;    /* enum virNativeVlanMode */
    unsigned int nativeTag;
};

void virNetDevVlanClear(virNetDevVlanPtr vlan);
void virNetDevVlanFree(virNetDevVlanPtr vlan);
int virNetDevVlanEqual(const virNetDevVlan *a, const virNetDevVlan *b);
int virNetDevVlanCopy(virNetDevVlanPtr dst, const virNetDevVlan *src);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virNetDevVlan, virNetDevVlanFree);
