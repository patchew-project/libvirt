/*
 * virseclabel.c: security label utility functions
 *
 * Copyright (C) 2006-2014 Red Hat, Inc.
 * Copyright (C) 2006-2008 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "internal.h"
#include "viralloc.h"
#include "virseclabel.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NONE


void
virSecurityLabelDefFree(virSecurityLabelDefPtr def)
{
    if (!def)
        return;
    VIR_FREE(def->model);
    VIR_FREE(def->label);
    VIR_FREE(def->imagelabel);
    VIR_FREE(def->baselabel);
    VIR_FREE(def);
}


void
virSecurityDeviceLabelDefFree(virSecurityDeviceLabelDefPtr def)
{
    if (!def)
        return;
    VIR_FREE(def->model);
    VIR_FREE(def->label);
    VIR_FREE(def);
}


virSecurityLabelDefPtr
virSecurityLabelDefNew(const char *model)
{
    virSecurityLabelDefPtr seclabel = NULL;

    if (VIR_ALLOC(seclabel) < 0) {
        virSecurityLabelDefFree(seclabel);
        return NULL;
    }

    seclabel->model = g_strdup(model);

    seclabel->relabel = true;

    return seclabel;
}

virSecurityDeviceLabelDefPtr
virSecurityDeviceLabelDefNew(const char *model)
{
    virSecurityDeviceLabelDefPtr seclabel = NULL;

    if (VIR_ALLOC(seclabel) < 0) {
        virSecurityDeviceLabelDefFree(seclabel);
        return NULL;
    }

    seclabel->model = g_strdup(model);

    return seclabel;
}


virSecurityDeviceLabelDefPtr
virSecurityDeviceLabelDefCopy(const virSecurityDeviceLabelDef *src)
{
    virSecurityDeviceLabelDefPtr ret;

    if (VIR_ALLOC(ret) < 0)
        return NULL;

    ret->relabel = src->relabel;
    ret->labelskip = src->labelskip;

    ret->model = g_strdup(src->model);
    ret->label = g_strdup(src->label);

    return ret;
}
