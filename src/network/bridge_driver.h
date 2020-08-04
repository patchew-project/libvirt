/*
 * bridge_driver.h: core driver methods for managing networks
 *
 * Copyright (C) 2006-2016 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "domain_conf.h"
#include "vircommand.h"
#include "virdnsmasq.h"
#include "virnetworkobj.h"

virNetworkXMLOptionPtr
networkDnsmasqCreateXMLConf(void);

int
networkRegister(void);

int
networkDnsmasqConfContents(virNetworkObjPtr obj,
                           const char *pidfile,
                           char **configstr,
                           char **hostsfilestr,
                           dnsmasqContext *dctx,
                           dnsmasqCapsPtr caps);
