/*
 * xen_common.h: Parsing and formatting functions for config common
 *
 * Copyright (C) 2014 SUSE LINUX Products GmbH, Nuernberg, Germany.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virconf.h"
#include "domain_conf.h"

#define XEN_CONFIG_FORMAT_XL    "xen-xl"
#define XEN_CONFIG_FORMAT_XM    "xen-xm"
#define XEN_CONFIG_FORMAT_SEXPR "xen-sxpr"

int xenConfigGetString(virConfPtr conf,
                       const char *name,
                       char **value,
                       const char *def);

int xenConfigGetBool(virConfPtr conf, const char *name, int *value, int def);

int xenConfigSetInt(virConfPtr conf, const char *name, long long value);

int xenConfigSetString(virConfPtr conf, const char *setting, const char *value);

int xenConfigGetULong(virConfPtr conf,
                      const char *name,
                      unsigned long *value,
                      unsigned long def);

int
xenConfigCopyString(virConfPtr conf,
                    const char *name,
                    char **value);

int xenConfigCopyStringOpt(virConfPtr conf,
                           const char *name,
                           char **value);

int xenParseConfigCommon(virConfPtr conf,
                         virDomainDefPtr def,
                         virCapsPtr caps,
                         const char *nativeFormat,
                         virDomainXMLOptionPtr xmlopt);

int xenFormatConfigCommon(virConfPtr conf,
                          virDomainDefPtr def,
                          virConnectPtr conn,
                          const char *nativeFormat);

char *xenMakeIPList(virNetDevIPInfoPtr guestIP);

int xenDomainDefAddImplicitInputDevice(virDomainDefPtr def);
