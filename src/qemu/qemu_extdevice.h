/*
 * qemu_extdevice.h: QEMU external devices support
 *
 * Copyright (C) 2018 IBM Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "qemu_conf.h"
#include "qemu_domain.h"

int qemuExtDeviceLogCommand(virQEMUDriverPtr driver,
                            virDomainObjPtr vm,
                            virCommandPtr cmd,
                            const char *info)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3) ATTRIBUTE_NONNULL(4)
    G_GNUC_WARN_UNUSED_RESULT;

int qemuExtDevicesPrepareDomain(virQEMUDriverPtr driver,
                                virDomainObjPtr vm)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2)
    G_GNUC_WARN_UNUSED_RESULT;

int qemuExtDevicesPrepareHost(virQEMUDriverPtr driver,
                              virDomainObjPtr vm)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2)
    G_GNUC_WARN_UNUSED_RESULT;

void qemuExtDevicesCleanupHost(virQEMUDriverPtr driver,
                               virDomainDefPtr def)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int qemuExtDevicesStart(virQEMUDriverPtr driver,
                        virDomainObjPtr vm,
                        virLogManagerPtr logManager,
                        bool incomingMigration)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2)
    G_GNUC_WARN_UNUSED_RESULT;

void qemuExtDevicesStop(virQEMUDriverPtr driver,
                        virDomainObjPtr vm)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

bool qemuExtDevicesHasDevice(virDomainDefPtr def);

int qemuExtDevicesSetupCgroup(virQEMUDriverPtr driver,
                              virDomainObjPtr vm,
                              virCgroupPtr cgroup);
