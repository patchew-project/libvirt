/*
 * openvz_conf.h: config information for OpenVZ VPSs
 *
 * Copyright (C) 2010 Red Hat, Inc.
 * Copyright (C) 2006, 2007 Binary Karma.
 * Copyright (C) 2006 Shuveb Hussain
 * Copyright (C) 2007 Anoop Joe Cyriac
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virdomainobjlist.h"
#include "virthread.h"


/* OpenVZ commands - Replace with wrapper scripts later? */
#define VZLIST         "/usr/sbin/vzlist"
#define VZCTL          "/usr/sbin/vzctl"
#define VZMIGRATE      "/usr/sbin/vzmigrate"
#define VZ_CONF_FILE   "/etc/vz/vz.conf"

#define VZCTL_BRIDGE_MIN_VERSION ((3 * 1000 * 1000) + (0 * 1000) + 22 + 1)

struct openvz_driver {
    virMutex lock;

    virCapsPtr caps;
    virDomainXMLOptionPtr xmlopt;
    virDomainObjListPtr domains;
    int version;
};

typedef int (*openvzLocateConfFileFunc)(int vpsid, char **conffile, const char *ext);

/* this allows the testsuite to replace the conf file locator function */
extern openvzLocateConfFileFunc openvzLocateConfFile;

int openvz_readline(int fd, char *ptr, int maxlen);
int openvzExtractVersion(struct openvz_driver *driver);
int openvzReadVPSConfigParam(int vpsid, const char *param, char **value);
int openvzWriteVPSConfigParam(int vpsid, const char *param, const char *value);
int openvzReadConfigParam(const char *conf_file, const char *param, char **value);
int openvzCopyDefaultConfig(int vpsid);
virCapsPtr openvzCapsInit(void);
int openvzLoadDomains(struct openvz_driver *driver);
void openvzFreeDriver(struct openvz_driver *driver);
int strtoI(const char *str);
int openvzSetDefinedUUID(int vpsid, unsigned char *uuid);
int openvzGetVEID(const char *name);
int openvzReadNetworkConf(virDomainDefPtr def, int veid);
virDomainXMLOptionPtr openvzXMLOption(struct openvz_driver *driver);
