/*
 * Copyright (C) 2007-2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virsocketaddr.h"

typedef struct _virNetDevIPAddr virNetDevIPAddr;
typedef virNetDevIPAddr *virNetDevIPAddrPtr;
struct _virNetDevIPAddr {
    virSocketAddr address; /* ipv4 or ipv6 address */
    virSocketAddr peer;    /* ipv4 or ipv6 address of peer */
    unsigned int prefix;   /* number of 1 bits in the netmask */
};

typedef struct _virNetDevIPRoute virNetDevIPRoute;
typedef virNetDevIPRoute *virNetDevIPRoutePtr;
struct _virNetDevIPRoute {
    char *family;               /* ipv4 or ipv6 - default is ipv4 */
    virSocketAddr address;      /* Routed Network IP address */

    /* One or the other of the following two will be used for a given
     * Network address, but never both. The parser guarantees this.
     * The virSocketAddrGetIPPrefix() can be used to get a
     * valid prefix.
     */
    virSocketAddr netmask;      /* ipv4 - either netmask or prefix specified */
    unsigned int prefix;        /* ipv6 - only prefix allowed */
    bool has_prefix;            /* prefix= was specified */
    unsigned int metric;        /* value for metric (defaults to 1) */
    bool has_metric;            /* metric= was specified */
    virSocketAddr gateway;      /* gateway IP address for ip-route */
};

/* A full set of all IP config info for a network device */
typedef struct _virNetDevIPInfo virNetDevIPInfo;
typedef virNetDevIPInfo *virNetDevIPInfoPtr;
 struct _virNetDevIPInfo {
    size_t nips;
    virNetDevIPAddrPtr *ips;
    size_t nroutes;
    virNetDevIPRoutePtr *routes;
};

/* manipulating/querying the netdev */
int virNetDevIPAddrAdd(const char *ifname,
                       virSocketAddr *addr,
                       virSocketAddr *peer,
                       unsigned int prefix)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NO_INLINE;
int virNetDevIPRouteAdd(const char *ifname,
                        virSocketAddrPtr addr,
                        unsigned int prefix,
                        virSocketAddrPtr gateway,
                        unsigned int metric)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(4)
    G_GNUC_WARN_UNUSED_RESULT;
int virNetDevIPAddrDel(const char *ifname,
                       virSocketAddr *addr,
                       unsigned int prefix)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;
int virNetDevIPAddrGet(const char *ifname, virSocketAddrPtr addr)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;
int virNetDevIPWaitDadFinish(virSocketAddrPtr *addrs, size_t count)
    ATTRIBUTE_NONNULL(1);
bool virNetDevIPCheckIPv6Forwarding(void);
void virNetDevIPAddrFree(virNetDevIPAddrPtr ip);

/* virNetDevIPRoute object */
void virNetDevIPRouteFree(virNetDevIPRoutePtr def);
virSocketAddrPtr virNetDevIPRouteGetAddress(virNetDevIPRoutePtr def);
int virNetDevIPRouteGetPrefix(virNetDevIPRoutePtr def);
unsigned int virNetDevIPRouteGetMetric(virNetDevIPRoutePtr def);
virSocketAddrPtr virNetDevIPRouteGetGateway(virNetDevIPRoutePtr def);

/* virNetDevIPInfo object */
void virNetDevIPInfoClear(virNetDevIPInfoPtr ip);
int virNetDevIPInfoAddToDev(const char *ifname,
                            virNetDevIPInfo const *ipInfo);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virNetDevIPAddr, virNetDevIPAddrFree);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(virNetDevIPRoute, virNetDevIPRouteFree);
