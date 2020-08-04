/*
 * bhyve_utils.h: bhyve utils
 *
 * Copyright (C) 2014 Roman Bogorodskiy
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "driver.h"
#include "domain_event.h"
#include "configmake.h"
#include "virdomainobjlist.h"
#include "virthread.h"
#include "hypervisor/virclosecallbacks.h"
#include "virportallocator.h"

#define BHYVE_AUTOSTART_DIR    SYSCONFDIR "/libvirt/bhyve/autostart"
#define BHYVE_CONFIG_DIR       SYSCONFDIR "/libvirt/bhyve"
#define BHYVE_STATE_DIR        RUNSTATEDIR "/libvirt/bhyve"
#define BHYVE_LOG_DIR          LOCALSTATEDIR "/log/libvirt/bhyve"

typedef struct _virBhyveDriverConfig virBhyveDriverConfig;
typedef struct _virBhyveDriverConfig *virBhyveDriverConfigPtr;

struct _virBhyveDriverConfig {
    virObject parent;

    char *firmwareDir;
};

struct _bhyveConn {
    virMutex lock;

    virBhyveDriverConfigPtr config;

    /* pid file FD, ensures two copies of the driver can't use the same root */
    int lockFD;

    virDomainObjListPtr domains;
    virCapsPtr caps;
    virDomainXMLOptionPtr xmlopt;
    char *pidfile;
    virSysinfoDefPtr hostsysinfo;

    virObjectEventStatePtr domainEventState;

    virCloseCallbacksPtr closeCallbacks;

    virPortAllocatorRangePtr remotePorts;

    unsigned bhyvecaps;
    unsigned grubcaps;
};

typedef struct _bhyveConn bhyveConn;
typedef struct _bhyveConn *bhyveConnPtr;

struct bhyveAutostartData {
    bhyveConnPtr driver;
    virConnectPtr conn;
};

void bhyveDriverLock(bhyveConnPtr driver);
void bhyveDriverUnlock(bhyveConnPtr driver);
