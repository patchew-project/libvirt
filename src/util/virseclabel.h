/*
 * virseclabel.h: security label utility functions
 *
 * Copyright (C) 2006-2014 Red Hat, Inc.
 * Copyright (C) 2006-2008 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

typedef enum {
    VIR_DOMAIN_SECLABEL_DEFAULT,
    VIR_DOMAIN_SECLABEL_NONE,
    VIR_DOMAIN_SECLABEL_DYNAMIC,
    VIR_DOMAIN_SECLABEL_STATIC,

    VIR_DOMAIN_SECLABEL_LAST
} virDomainSeclabelType;

/* Security configuration for domain */
typedef struct _virSecurityLabelDef virSecurityLabelDef;
typedef virSecurityLabelDef *virSecurityLabelDefPtr;
struct _virSecurityLabelDef {
    char *model;        /* name of security model */
    char *label;        /* security label string */
    char *imagelabel;   /* security image label string */
    char *baselabel;    /* base name of label string */
    int type;           /* virDomainSeclabelType */
    bool relabel;       /* true (default) for allowing relabels */
    bool implicit;      /* true if seclabel is auto-added */
};


/* Security configuration for device */
typedef struct _virSecurityDeviceLabelDef virSecurityDeviceLabelDef;
typedef virSecurityDeviceLabelDef *virSecurityDeviceLabelDefPtr;
struct _virSecurityDeviceLabelDef {
    char *model;
    char *label;        /* image label string */
    bool relabel;       /* true (default) for allowing relabels */
    bool labelskip;     /* live-only; true if skipping failed label attempt */
};

virSecurityLabelDefPtr
virSecurityLabelDefNew(const char *model);

virSecurityDeviceLabelDefPtr
virSecurityDeviceLabelDefNew(const char *model);

virSecurityDeviceLabelDefPtr
virSecurityDeviceLabelDefCopy(const virSecurityDeviceLabelDef *src)
    ATTRIBUTE_NONNULL(1);

void virSecurityLabelDefFree(virSecurityLabelDefPtr def);
void virSecurityDeviceLabelDefFree(virSecurityDeviceLabelDefPtr def);
