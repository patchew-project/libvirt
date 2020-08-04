/*
 * nwfilter_gentech_driver.h: generic technology driver include file
 *
 * Copyright (C) 2013 Red Hat, Inc.
 * Copyright (C) 2010 IBM Corp.
 * Copyright (C) 2010 Stefan Berger
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virnwfilterobj.h"
#include "virnwfilterbindingdef.h"
#include "nwfilter_tech_driver.h"

virNWFilterTechDriverPtr virNWFilterTechDriverForName(const char *name);

int virNWFilterTechDriversInit(bool privileged);
void virNWFilterTechDriversShutdown(void);

enum instCase {
    INSTANTIATE_ALWAYS,
    INSTANTIATE_FOLLOW_NEWFILTER,
};


int virNWFilterInstantiateFilter(virNWFilterDriverStatePtr driver,
                                 virNWFilterBindingDefPtr binding);
int virNWFilterUpdateInstantiateFilter(virNWFilterDriverStatePtr driver,
                                       virNWFilterBindingDefPtr binding,
                                       bool *skipIface);

int virNWFilterInstantiateFilterLate(virNWFilterDriverStatePtr driver,
                                     virNWFilterBindingDefPtr binding,
                                     int ifindex);

int virNWFilterTeardownFilter(virNWFilterBindingDefPtr binding);

virHashTablePtr virNWFilterCreateVarHashmap(const char *macaddr,
                                            const virNWFilterVarValue *value);

int virNWFilterBuildAll(virNWFilterDriverStatePtr driver,
                        bool newFilters);
