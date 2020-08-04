/*
 * qemu_vhost_user_gpu.h: QEMU vhost-user GPU support
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "domain_conf.h"
#include "qemu_domain.h"
#include "qemu_security.h"

int qemuExtVhostUserGPUPrepareDomain(virQEMUDriverPtr driver,
                                     virDomainVideoDefPtr video)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2)
    G_GNUC_WARN_UNUSED_RESULT;

int qemuExtVhostUserGPUStart(virQEMUDriverPtr driver,
                             virDomainObjPtr vm,
                             virDomainVideoDefPtr video)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3)
    G_GNUC_WARN_UNUSED_RESULT;

void qemuExtVhostUserGPUStop(virQEMUDriverPtr driver,
                             virDomainObjPtr def,
                             virDomainVideoDefPtr video)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int
qemuExtVhostUserGPUSetupCgroup(virQEMUDriverPtr driver,
                               virDomainDefPtr def,
                               virDomainVideoDefPtr video,
                               virCgroupPtr cgroup)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3)
    G_GNUC_WARN_UNUSED_RESULT;
