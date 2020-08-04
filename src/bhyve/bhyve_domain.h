/*
 * bhyve_domain.h: bhyve domain private state headers
 *
 * Copyright (C) 2014 Roman Bogorodskiy
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "domain_addr.h"
#include "domain_conf.h"

#include "bhyve_monitor.h"

typedef struct _bhyveDomainObjPrivate bhyveDomainObjPrivate;
typedef bhyveDomainObjPrivate *bhyveDomainObjPrivatePtr;
struct _bhyveDomainObjPrivate {
    virDomainPCIAddressSetPtr pciaddrs;
    bool persistentAddrs;

    bhyveMonitorPtr mon;
};

virDomainXMLOptionPtr virBhyveDriverCreateXMLConf(bhyveConnPtr);

extern virDomainXMLPrivateDataCallbacks virBhyveDriverPrivateDataCallbacks;
extern virDomainDefParserConfig virBhyveDriverDomainDefParserConfig;
extern virXMLNamespace virBhyveDriverDomainXMLNamespace;

bool bhyveDomainDefNeedsISAController(virDomainDefPtr def);
