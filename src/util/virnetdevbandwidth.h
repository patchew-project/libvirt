/*
 * Copyright (C) 2009-2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virmacaddr.h"

typedef struct _virNetDevBandwidthRate virNetDevBandwidthRate;
typedef virNetDevBandwidthRate *virNetDevBandwidthRatePtr;
struct _virNetDevBandwidthRate {
    unsigned long long average;  /* kbytes/s */
    unsigned long long peak;     /* kbytes/s */
    unsigned long long floor;    /* kbytes/s */
    unsigned long long burst;    /* kbytes */
};

typedef struct _virNetDevBandwidth virNetDevBandwidth;
typedef virNetDevBandwidth *virNetDevBandwidthPtr;
struct _virNetDevBandwidth {
    virNetDevBandwidthRatePtr in, out;
};

void virNetDevBandwidthFree(virNetDevBandwidthPtr def);

int virNetDevBandwidthSet(const char *ifname,
                          const virNetDevBandwidth *bandwidth,
                          bool hierarchical_class,
                          bool swapped)
    G_GNUC_WARN_UNUSED_RESULT;
int virNetDevBandwidthClear(const char *ifname);
int virNetDevBandwidthCopy(virNetDevBandwidthPtr *dest,
                           const virNetDevBandwidth *src)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;

bool virNetDevBandwidthEqual(const virNetDevBandwidth *a, const virNetDevBandwidth *b);

int virNetDevBandwidthPlug(const char *brname,
                           virNetDevBandwidthPtr net_bandwidth,
                           const virMacAddr *ifmac_ptr,
                           virNetDevBandwidthPtr bandwidth,
                           unsigned int id)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(3) ATTRIBUTE_NONNULL(4)
    G_GNUC_WARN_UNUSED_RESULT;

int virNetDevBandwidthUnplug(const char *brname,
                             unsigned int id)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;

int virNetDevBandwidthUpdateRate(const char *ifname,
                                 unsigned int id,
                                 virNetDevBandwidthPtr bandwidth,
                                 unsigned long long new_rate)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;

int virNetDevBandwidthUpdateFilter(const char *ifname,
                                   const virMacAddr *ifmac_ptr,
                                   unsigned int id)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2)
    G_GNUC_WARN_UNUSED_RESULT;
