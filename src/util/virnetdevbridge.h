/*
 * Copyright (C) 2007-2012, 2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virmacaddr.h"

int virNetDevBridgeCreate(const char *brname,
                          const virMacAddr *mac)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
int virNetDevBridgeDelete(const char *brname)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;

int virNetDevBridgeAddPort(const char *brname,
                           const char *ifname)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;

int virNetDevBridgeRemovePort(const char *brname,
                              const char *ifname)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;

int virNetDevBridgeSetSTPDelay(const char *brname,
                               int delay)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
int virNetDevBridgeGetSTPDelay(const char *brname,
                               int *delay)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;
int virNetDevBridgeSetSTP(const char *brname,
                          bool enable)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
int virNetDevBridgeGetSTP(const char *brname,
                          bool *enable)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;

int virNetDevBridgeSetVlanFiltering(const char *brname,
                                    bool enable)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
int virNetDevBridgeGetVlanFiltering(const char *brname,
                                    bool *enable)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;

int virNetDevBridgePortGetLearning(const char *brname,
                                   const char *ifname,
                                   bool *enable)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3)
    G_GNUC_WARN_UNUSED_RESULT;
int virNetDevBridgePortSetLearning(const char *brname,
                                   const char *ifname,
                                   bool enable)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;
int virNetDevBridgePortGetUnicastFlood(const char *brname,
                                       const char *ifname,
                                       bool *enable)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3)
    G_GNUC_WARN_UNUSED_RESULT;
int virNetDevBridgePortSetUnicastFlood(const char *brname,
                                       const char *ifname,
                                       bool enable)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;
int virNetDevBridgePortGetIsolated(const char *brname,
                                   const char *ifname,
                                   bool *enable)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3)
    G_GNUC_WARN_UNUSED_RESULT;
int virNetDevBridgePortSetIsolated(const char *brname,
                                   const char *ifname,
                                   bool enable)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;

typedef enum {
    VIR_NETDEVBRIDGE_FDB_FLAG_ROUTER    = (1 << 0),
    VIR_NETDEVBRIDGE_FDB_FLAG_SELF      = (1 << 1),
    VIR_NETDEVBRIDGE_FDB_FLAG_MASTER    = (1 << 2),

    VIR_NETDEVBRIDGE_FDB_FLAG_PERMANENT = (1 << 3),
    VIR_NETDEVBRIDGE_FDB_FLAG_TEMP      = (1 << 4),
} virNetDevBridgeFDBFlags;

int virNetDevBridgeFDBAdd(const virMacAddr *mac, const char *ifname,
                          unsigned int flags)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;
int virNetDevBridgeFDBDel(const virMacAddr *mac, const char *ifname,
                          unsigned int flags)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;
