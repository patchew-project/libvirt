/*
 * Copyright (C) 2010-2012 Red Hat, Inc.
 *
 * lxc_monitor.h: client for LXC controller monitor
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virobject.h"
#include "domain_conf.h"
#include "lxc_monitor_protocol.h"

typedef struct _virLXCMonitor virLXCMonitor;
typedef virLXCMonitor *virLXCMonitorPtr;

typedef struct _virLXCMonitorCallbacks virLXCMonitorCallbacks;
typedef virLXCMonitorCallbacks *virLXCMonitorCallbacksPtr;

typedef void (*virLXCMonitorCallbackDestroy)(virLXCMonitorPtr mon,
                                             virDomainObjPtr vm);
typedef void (*virLXCMonitorCallbackEOFNotify)(virLXCMonitorPtr mon,
                                               virDomainObjPtr vm);

typedef void (*virLXCMonitorCallbackExitNotify)(virLXCMonitorPtr mon,
                                                virLXCMonitorExitStatus status,
                                                virDomainObjPtr vm);

typedef void (*virLXCMonitorCallbackInitNotify)(virLXCMonitorPtr mon,
                                                pid_t pid,
                                                virDomainObjPtr vm);

struct _virLXCMonitorCallbacks {
    virLXCMonitorCallbackDestroy destroy;
    virLXCMonitorCallbackEOFNotify eofNotify;
    virLXCMonitorCallbackExitNotify exitNotify;
    virLXCMonitorCallbackInitNotify initNotify;
};

virLXCMonitorPtr virLXCMonitorNew(virDomainObjPtr vm,
                                  const char *socketdir,
                                  virLXCMonitorCallbacksPtr cb);

void virLXCMonitorClose(virLXCMonitorPtr mon);

void virLXCMonitorLock(virLXCMonitorPtr mon);
void virLXCMonitorUnlock(virLXCMonitorPtr mon);
