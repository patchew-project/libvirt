/*
 * bridge_driver_platform.h: platform specific routines for bridge driver
 *
 * Copyright (C) 2006-2013 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virthread.h"
#include "virdnsmasq.h"
#include "virnetworkobj.h"
#include "object_event.h"

/* Main driver state */
struct _virNetworkDriverState {
    virMutex lock;

    /* Read-only */
    bool privileged;

    /* pid file FD, ensures two copies of the driver can't use the same root */
    int lockFD;

    /* Immutable pointer, self-locking APIs */
    virNetworkObjListPtr networks;

    /* Immutable pointers, Immutable objects */
    char *networkConfigDir;
    char *networkAutostartDir;
    char *stateDir;
    char *pidDir;
    char *dnsmasqStateDir;
    char *radvdStateDir;

    /* Require lock to get a reference on the object,
     * lockless access thereafter
     */
    dnsmasqCapsPtr dnsmasqCaps;

    /* Immutable pointer, self-locking APIs */
    virObjectEventStatePtr networkEventState;

    virNetworkXMLOptionPtr xmlopt;
};

typedef struct _virNetworkDriverState virNetworkDriverState;
typedef virNetworkDriverState *virNetworkDriverStatePtr;

void networkPreReloadFirewallRules(virNetworkDriverStatePtr driver, bool startup, bool force);
void networkPostReloadFirewallRules(bool startup);

int networkCheckRouteCollision(virNetworkDefPtr def);

int networkAddFirewallRules(virNetworkDefPtr def);

void networkRemoveFirewallRules(virNetworkDefPtr def);
