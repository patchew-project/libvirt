/*
 * qemu_tpm.h: QEMU TPM support
 *
 * Copyright (C) 2018 IBM Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vircommand.h"

int qemuExtTPMInitPaths(virQEMUDriverPtr driver,
                        virDomainDefPtr def)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2)
    G_GNUC_WARN_UNUSED_RESULT;

int qemuExtTPMPrepareHost(virQEMUDriverPtr driver,
                          virDomainDefPtr def)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2)
    G_GNUC_WARN_UNUSED_RESULT;

void qemuExtTPMCleanupHost(virDomainDefPtr def)
    ATTRIBUTE_NONNULL(1);

int qemuExtTPMStart(virQEMUDriverPtr driver,
                    virDomainObjPtr vm,
                    bool incomingMigration)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2)
    G_GNUC_WARN_UNUSED_RESULT;

void qemuExtTPMStop(virQEMUDriverPtr driver,
                    virDomainObjPtr vm)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int qemuExtTPMSetupCgroup(virQEMUDriverPtr driver,
                          virDomainDefPtr def,
                          virCgroupPtr cgroup)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3)
    G_GNUC_WARN_UNUSED_RESULT;
