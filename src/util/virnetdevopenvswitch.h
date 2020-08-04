/*
 * Copyright (C) 2013 Red Hat, Inc.
 * Copyright (C) 2012 Nicira, Inc.
 * Copyright (C) 2017 IBM Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virnetdevvportprofile.h"
#include "virnetdevvlan.h"

#define VIR_NETDEV_OVS_DEFAULT_TIMEOUT 5

void virNetDevOpenvswitchSetTimeout(unsigned int timeout);

int virNetDevOpenvswitchAddPort(const char *brname,
                                const char *ifname,
                                const virMacAddr *macaddr,
                                const unsigned char *vmuuid,
                                const virNetDevVPortProfile *ovsport,
                                const virNetDevVlan *virtVlan)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3)
    G_GNUC_WARN_UNUSED_RESULT;

int virNetDevOpenvswitchRemovePort(const char *brname, const char *ifname)
    ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;

int virNetDevOpenvswitchInterfaceGetMaster(const char *ifname, char **master)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;

int virNetDevOpenvswitchGetMigrateData(char **migrate, const char *ifname)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;

int virNetDevOpenvswitchSetMigrateData(char *migrate, const char *ifname)
    ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;

int virNetDevOpenvswitchInterfaceParseStats(const char *json,
                                            virDomainInterfaceStatsPtr stats)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;

int virNetDevOpenvswitchInterfaceStats(const char *ifname,
                                       virDomainInterfaceStatsPtr stats)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;

int virNetDevOpenvswitchInterfaceGetMaster(const char *ifname, char **master)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;

int virNetDevOpenvswitchGetVhostuserIfname(const char *path,
                                           char **ifname)
    ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NO_INLINE;

int virNetDevOpenvswitchUpdateVlan(const char *ifname,
                                   const virNetDevVlan *virtVlan)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
