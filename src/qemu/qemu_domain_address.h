/*
 * qemu_domain_address.h: QEMU domain address
 *
 * Copyright (C) 2006-2016 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "domain_addr.h"
#include "domain_conf.h"
#include "qemu_conf.h"
#include "qemu_capabilities.h"

int qemuDomainGetSCSIControllerModel(const virDomainDef *def,
                                     const virDomainControllerDef *cont,
                                     virQEMUCapsPtr qemuCaps);

int qemuDomainSetSCSIControllerModel(const virDomainDef *def,
                                     virDomainControllerDefPtr cont,
                                     virQEMUCapsPtr qemuCaps);

int qemuDomainFindSCSIControllerModel(const virDomainDef *def,
                                      virDomainDeviceInfoPtr info);

int qemuDomainAssignAddresses(virDomainDefPtr def,
                              virQEMUCapsPtr qemuCaps,
                              virQEMUDriverPtr driver,
                              virDomainObjPtr obj,
                              bool newDomain)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);

int qemuDomainEnsurePCIAddress(virDomainObjPtr obj,
                               virDomainDeviceDefPtr dev,
                               virQEMUDriverPtr driver)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);

void qemuDomainFillDeviceIsolationGroup(virDomainDefPtr def,
                                       virDomainDeviceDefPtr dev)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

void qemuDomainReleaseDeviceAddress(virDomainObjPtr vm,
                                    virDomainDeviceInfoPtr info);

int qemuDomainAssignMemoryDeviceSlot(virDomainDefPtr def,
                                     virDomainMemoryDefPtr mem);

int qemuDomainEnsureVirtioAddress(bool *releaseAddr,
                                  virDomainObjPtr vm,
                                  virDomainDeviceDefPtr dev,
                                  const char *devicename);
