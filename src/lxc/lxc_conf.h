/*
 * Copyright (C) 2010, 2013 Red Hat, Inc.
 * Copyright IBM Corp. 2008
 *
 * lxc_conf.h: header file for linux container config functions
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "libvirt_internal.h"
#include "domain_conf.h"
#include "domain_event.h"
#include "capabilities.h"
#include "virthread.h"
#include "security/security_manager.h"
#include "configmake.h"
#include "vircgroup.h"
#include "virsysinfo.h"
#include "virusb.h"
#include "virclosecallbacks.h"
#include "virhostdev.h"

#define LXC_DRIVER_NAME "LXC"

#define LXC_CONFIG_DIR SYSCONFDIR "/libvirt/lxc"
#define LXC_STATE_DIR RUNSTATEDIR "/libvirt/lxc"
#define LXC_LOG_DIR LOCALSTATEDIR "/log/libvirt/lxc"
#define LXC_AUTOSTART_DIR LXC_CONFIG_DIR "/autostart"

typedef struct _virLXCDriver virLXCDriver;
typedef virLXCDriver *virLXCDriverPtr;

typedef struct _virLXCDriverConfig virLXCDriverConfig;
typedef virLXCDriverConfig *virLXCDriverConfigPtr;

struct _virLXCDriverConfig {
    virObject parent;

    char *configDir;
    char *autostartDir;
    char *stateDir;
    char *logDir;
    bool log_libvirtd;
    int have_netns;

    char *securityDriverName;
    bool securityDefaultConfined;
    bool securityRequireConfined;
};

struct _virLXCDriver {
    virMutex lock;

    /* Require lock to get reference on 'config',
     * then lockless thereafter */
    virLXCDriverConfigPtr config;

    /* pid file FD, ensures two copies of the driver can't use the same root */
    int lockFD;

    /* Require lock to get a reference on the object,
     * lockless access thereafter */
    virCapsPtr caps;

    /* Immutable pointer, Immutable object */
    virDomainXMLOptionPtr xmlopt;

    /* Immutable pointer, lockless APIs */
    virSysinfoDefPtr hostsysinfo;

    /* Atomic inc/dec only */
    unsigned int nactive;

    /* Immutable pointers. Caller must provide locking */
    virStateInhibitCallback inhibitCallback;
    void *inhibitOpaque;

    /* Immutable pointer, self-locking APIs */
    virDomainObjListPtr domains;

    virHostdevManagerPtr hostdevMgr;

    /* Immutable pointer, self-locking APIs */
    virObjectEventStatePtr domainEventState;

    /* Immutable pointer. self-locking APIs */
    virSecurityManagerPtr securityManager;

    /* Immutable pointer, self-locking APIs */
    virCloseCallbacksPtr closeCallbacks;
};

virLXCDriverConfigPtr virLXCDriverConfigNew(void);
virLXCDriverConfigPtr virLXCDriverGetConfig(virLXCDriverPtr driver);
int virLXCLoadDriverConfig(virLXCDriverConfigPtr cfg,
                           const char *filename);
virCapsPtr virLXCDriverCapsInit(virLXCDriverPtr driver);
virCapsPtr virLXCDriverGetCapabilities(virLXCDriverPtr driver,
                                       bool refresh);
virDomainXMLOptionPtr lxcDomainXMLConfInit(virLXCDriverPtr driver);

static inline void lxcDriverLock(virLXCDriverPtr driver)
{
    virMutexLock(&driver->lock);
}
static inline void lxcDriverUnlock(virLXCDriverPtr driver)
{
    virMutexUnlock(&driver->lock);
}
