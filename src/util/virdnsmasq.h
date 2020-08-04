/*
 * virdnsmasq.h: Helper APIs for managing dnsmasq
 *
 * Copyright (C) 2007-2012 Red Hat, Inc.
 * Copyright (C) 2010 Satoru SATOH <satoru.satoh@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * based on iptables.h
 */

#pragma once

#include "virobject.h"
#include "virsocketaddr.h"

typedef struct
{
    /*
     * Each entry holds a string, "<mac_addr>,<hostname>,<ip_addr>" such as
     * "01:23:45:67:89:0a,foo,10.0.0.3".
     */
    char *host;

} dnsmasqDhcpHost;

typedef struct
{
    unsigned int     nhosts;
    dnsmasqDhcpHost *hosts;

    char            *path;  /* Absolute path of dnsmasq's hostsfile. */
} dnsmasqHostsfile;

typedef struct
{
    unsigned int    nhostnames;
    char            *ip;
    char            **hostnames;

} dnsmasqAddnHost;

typedef struct
{
    unsigned int     nhosts;
    dnsmasqAddnHost *hosts;

    char            *path;  /* Absolute path of dnsmasq's hostsfile. */
} dnsmasqAddnHostsfile;

typedef struct
{
    char                 *config_dir;
    dnsmasqHostsfile     *hostsfile;
    dnsmasqAddnHostsfile *addnhostsfile;
} dnsmasqContext;

typedef enum {
   DNSMASQ_CAPS_BIND_DYNAMIC = 0, /* support for --bind-dynamic */
   DNSMASQ_CAPS_BINDTODEVICE = 1, /* uses SO_BINDTODEVICE for --bind-interfaces */
   DNSMASQ_CAPS_RA_PARAM = 2,     /* support for --ra-param */

   DNSMASQ_CAPS_LAST,             /* this must always be the last item */
} dnsmasqCapsFlags;

typedef struct _dnsmasqCaps dnsmasqCaps;
typedef dnsmasqCaps *dnsmasqCapsPtr;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(dnsmasqCaps, virObjectUnref);


dnsmasqContext * dnsmasqContextNew(const char *network_name,
                                   const char *config_dir);
void             dnsmasqContextFree(dnsmasqContext *ctx);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(dnsmasqContext, dnsmasqContextFree);

int              dnsmasqAddDhcpHost(dnsmasqContext *ctx,
                                    const char *mac,
                                    virSocketAddr *ip,
                                    const char *name,
                                    const char *id,
                                    const char *leasetime,
                                    bool ipv6);
int              dnsmasqAddHost(dnsmasqContext *ctx,
                                virSocketAddr *ip,
                                const char *name);
int              dnsmasqSave(const dnsmasqContext *ctx);
int              dnsmasqDelete(const dnsmasqContext *ctx);
int              dnsmasqReload(pid_t pid);

dnsmasqCapsPtr dnsmasqCapsNewFromBuffer(const char *buf,
                                        const char *binaryPath);
dnsmasqCapsPtr dnsmasqCapsNewFromFile(const char *dataPath,
                                      const char *binaryPath);
dnsmasqCapsPtr dnsmasqCapsNewFromBinary(const char *binaryPath);
int dnsmasqCapsRefresh(dnsmasqCapsPtr *caps, const char *binaryPath);
bool dnsmasqCapsGet(dnsmasqCapsPtr caps, dnsmasqCapsFlags flag);
const char *dnsmasqCapsGetBinaryPath(dnsmasqCapsPtr caps);
unsigned long dnsmasqCapsGetVersion(dnsmasqCapsPtr caps);
char *dnsmasqDhcpHostsToString(dnsmasqDhcpHost *hosts,
                               unsigned int nhosts);

#define DNSMASQ_DHCPv6_MAJOR_REQD 2
#define DNSMASQ_DHCPv6_MINOR_REQD 64
#define DNSMASQ_RA_MAJOR_REQD 2
#define DNSMASQ_RA_MINOR_REQD 64

#define DNSMASQ_DHCPv6_SUPPORT(CAPS) \
    (dnsmasqCapsGetVersion(CAPS) >= \
     (DNSMASQ_DHCPv6_MAJOR_REQD * 1000000) + \
     (DNSMASQ_DHCPv6_MINOR_REQD * 1000))
#define DNSMASQ_RA_SUPPORT(CAPS) \
    (dnsmasqCapsGetVersion(CAPS) >= \
     (DNSMASQ_RA_MAJOR_REQD * 1000000) + \
     (DNSMASQ_RA_MINOR_REQD * 1000))
