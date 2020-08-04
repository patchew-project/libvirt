/*
 * nwfilter_dhcpsnoop.h: support DHCP snooping for a VM on an interface
 *
 * Copyright (C) 2013 Red Hat, Inc.
 * Copyright (C) 2010-2012 IBM Corp.
 * Copyright (C) 2010-2012 David L Stevens
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "nwfilter_tech_driver.h"

int virNWFilterDHCPSnoopInit(void);
void virNWFilterDHCPSnoopShutdown(void);
int virNWFilterDHCPSnoopReq(virNWFilterTechDriverPtr techdriver,
                            virNWFilterBindingDefPtr binding,
                            virNWFilterDriverStatePtr driver);
void virNWFilterDHCPSnoopEnd(const char *ifname);
