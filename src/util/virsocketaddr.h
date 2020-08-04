/*
 * Copyright (C) 2009-2013, 2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virsocket.h"

#define VIR_LOOPBACK_IPV4_ADDR "127.0.0.1"

typedef struct {
    union {
        struct sockaddr sa;
        struct sockaddr_storage stor;
        struct sockaddr_in inet4;
        struct sockaddr_in6 inet6;
#ifndef WIN32
        struct sockaddr_un un;
#endif
    } data;
    socklen_t len;
} virSocketAddr;

#define VIR_SOCKET_ADDR_VALID(s) \
    ((s)->data.sa.sa_family != AF_UNSPEC)

#define VIR_SOCKET_ADDR_IS_FAMILY(s, f) \
    ((s)->data.sa.sa_family == f)

#define VIR_SOCKET_ADDR_FAMILY(s) \
    ((s)->data.sa.sa_family)

#define VIR_SOCKET_ADDR_IPV4_ALL "0.0.0.0"
#define VIR_SOCKET_ADDR_IPV6_ALL "::"

#define VIR_SOCKET_ADDR_IPV4_ARPA "in-addr.arpa"
#define VIR_SOCKET_ADDR_IPV6_ARPA "ip6.arpa"

typedef virSocketAddr *virSocketAddrPtr;

typedef struct _virSocketAddrRange virSocketAddrRange;
typedef virSocketAddrRange *virSocketAddrRangePtr;
struct _virSocketAddrRange {
    virSocketAddr start;
    virSocketAddr end;
};

typedef struct _virPortRange virPortRange;
typedef virPortRange *virPortRangePtr;
struct _virPortRange {
    unsigned int start;
    unsigned int end;
};

int virSocketAddrParse(virSocketAddrPtr addr,
                       const char *val,
                       int family);

int virSocketAddrParseAny(virSocketAddrPtr addr,
                          const char *val,
                          int family,
                          bool reportError);

int virSocketAddrParseIPv4(virSocketAddrPtr addr,
                           const char *val);

int virSocketAddrParseIPv6(virSocketAddrPtr addr,
                           const char *val);

int virSocketAddrResolveService(const char *service);

void virSocketAddrSetIPv4AddrNetOrder(virSocketAddrPtr addr, uint32_t val);
void virSocketAddrSetIPv4Addr(virSocketAddrPtr addr, uint32_t val);
void virSocketAddrSetIPv6AddrNetOrder(virSocketAddrPtr addr, uint32_t val[4]);
void virSocketAddrSetIPv6Addr(virSocketAddrPtr addr, uint32_t val[4]);

char *virSocketAddrFormat(const virSocketAddr *addr);
char *virSocketAddrFormatFull(const virSocketAddr *addr,
                              bool withService,
                              const char *separator);

char *virSocketAddrGetPath(virSocketAddrPtr addr);

int virSocketAddrSetPort(virSocketAddrPtr addr, int port);

int virSocketAddrGetPort(virSocketAddrPtr addr);

int virSocketAddrGetRange(virSocketAddrPtr start,
                          virSocketAddrPtr end,
                          virSocketAddrPtr network,
                          int prefix);

int virSocketAddrIsNetmask(virSocketAddrPtr netmask);

int virSocketAddrCheckNetmask(virSocketAddrPtr addr1,
                              virSocketAddrPtr addr2,
                              virSocketAddrPtr netmask);
int virSocketAddrMask(const virSocketAddr *addr,
                      const virSocketAddr *netmask,
                      virSocketAddrPtr network);
int virSocketAddrMaskByPrefix(const virSocketAddr *addr,
                              unsigned int prefix,
                              virSocketAddrPtr network);
int virSocketAddrBroadcast(const virSocketAddr *addr,
                           const virSocketAddr *netmask,
                           virSocketAddrPtr broadcast);
int virSocketAddrBroadcastByPrefix(const virSocketAddr *addr,
                                   unsigned int prefix,
                                   virSocketAddrPtr broadcast);

int virSocketAddrGetNumNetmaskBits(const virSocketAddr *netmask);
int virSocketAddrPrefixToNetmask(unsigned int prefix,
                                 virSocketAddrPtr netmask,
                                 int family);
int virSocketAddrGetIPPrefix(const virSocketAddr *address,
                             const virSocketAddr *netmask,
                             int prefix);
bool virSocketAddrEqual(const virSocketAddr *s1,
                        const virSocketAddr *s2);
bool virSocketAddrIsPrivate(const virSocketAddr *addr);

bool virSocketAddrIsWildcard(const virSocketAddr *addr);

int virSocketAddrNumericFamily(const char *address);

bool virSocketAddrIsNumericLocalhost(const char *addr);

int virSocketAddrPTRDomain(const virSocketAddr *addr,
                           unsigned int prefix,
                           char **ptr)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(3);

void virSocketAddrFree(virSocketAddrPtr addr);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virSocketAddr, virSocketAddrFree);
