/*
 * Copyright (C) 2009-2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "internal.h"
#include "virerror.h"
#include "virnetdevvlan.h"
#include "viralloc.h"

#define VIR_FROM_THIS VIR_FROM_NONE

void
virNetDevVlanClear(virNetDevVlanPtr vlan)
{
    VIR_FREE(vlan->tag);
    vlan->nTags = 0;
    vlan->nativeMode = 0;
    vlan->nativeTag = 0;
}

void
virNetDevVlanFree(virNetDevVlanPtr vlan)
{
    if (vlan)
        virNetDevVlanClear(vlan);
    VIR_FREE(vlan);
}

int
virNetDevVlanEqual(const virNetDevVlan *a, const virNetDevVlan *b)
{
    int ai, bi;

    if (!(a || b))
        return true;
    if (!a || !b)
        return false;

    if (a->trunk != b->trunk ||
        a->nTags != b->nTags ||
        a->nativeMode != b->nativeMode ||
        a->nativeTag != b->nativeTag) {
        return false;
    }

    for (ai = 0; ai < a->nTags; ai++) {
        for (bi = 0; bi < b->nTags; bi++) {
            if (a->tag[ai] == b->tag[bi])
                break;
        }
        if (bi >= b->nTags) {
            /* no matches for a->tag[ai] anywhere in b->tag */
            return false;
        }
    }
    return true;
}

/*
 * virNetDevVlanCopy - copy from src into (already existing) dst.
 *                     If src is NULL, dst will have nTags set to 0.
 *                     dst is assumed to be empty on entry.
 */
int
virNetDevVlanCopy(virNetDevVlanPtr dst, const virNetDevVlan *src)
{
    if (!src || src->nTags == 0)
        return 0;

    if (VIR_ALLOC_N(dst->tag, src->nTags) < 0)
        return -1;

    dst->trunk = src->trunk;
    dst->nTags = src->nTags;
    dst->nativeMode = src->nativeMode;
    dst->nativeTag = src->nativeTag;
    memcpy(dst->tag, src->tag, src->nTags * sizeof(*src->tag));
    return 0;
}
