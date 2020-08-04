/*
 * nwfilter_learnipaddr.h: support for learning IP address used by a VM
 *                         on an interface
 *
 * Copyright (C) 2012-2013 Red Hat, Inc.
 * Copyright (C) 2010 IBM Corp.
 * Copyright (C) 2010 Stefan Berger
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "conf/nwfilter_params.h"
#include "nwfilter_tech_driver.h"
#include "virnwfilterbindingdef.h"
#include <net/if.h>

enum howDetect {
  DETECT_DHCP = 1,
  DETECT_STATIC = 2,
};

int virNWFilterLearnIPAddress(virNWFilterTechDriverPtr techdriver,
                              virNWFilterBindingDefPtr binding,
                              int ifindex,
                              virNWFilterDriverStatePtr driver,
                              int howDetect);

bool virNWFilterHasLearnReq(int ifindex);
int virNWFilterTerminateLearnReq(const char *ifname);

int virNWFilterLockIface(const char *ifname) G_GNUC_WARN_UNUSED_RESULT;
void virNWFilterUnlockIface(const char *ifname);

int virNWFilterLearnInit(void);
void virNWFilterLearnShutdown(void);
void virNWFilterLearnThreadsTerminate(bool allowNewThreads);
