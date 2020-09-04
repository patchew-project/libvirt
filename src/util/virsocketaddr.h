/*
 * Copyright (C) 2009-2013, 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "virbuffer.h"
#include "virsocket.h"

#define VIR_LOOPBACK_IPV4_ADDR "127.0.0.1"

typedef struct _virSocketAddr virSocketAddr;
typedef virSocketAddr *virSocketAddrPtr;
struct _virSocketAddr {
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
};

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

int virSocketAddrParseXML(const char *val,
                          virSocketAddrPtr addr,
                          const char *instname,
                          void *parent,
                          void *opaque);

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

int virSocketAddrFormatBuf(virBufferPtr buf,
                           const char *fmt,
                           const virSocketAddr *addr,
                           const void *parent,
                           void *opaque);

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
void virSocketAddrClear(virSocketAddrPtr addr);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virSocketAddr, virSocketAddrFree);

bool virSocketAddrCheck(const virSocketAddr *addr,
                        const void *parent,
                        void *opaque);
