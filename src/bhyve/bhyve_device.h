/*
 * bhyve_device.h: bhyve device management headers
 *
 * Copyright (C) 2014 Roman Bogorodskiy
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "domain_conf.h"
#include "virpci.h"
#include "bhyve_domain.h"

int bhyveDomainAssignPCIAddresses(virDomainDefPtr def, virDomainObjPtr obj);

virDomainPCIAddressSetPtr bhyveDomainPCIAddressSetCreate(virDomainDefPtr def,
                                                         unsigned int nbuses);

int bhyveDomainAssignAddresses(virDomainDefPtr def, virDomainObjPtr obj)
    ATTRIBUTE_NONNULL(1);
