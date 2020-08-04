/*
 * qemu_vhost_user.h: QEMU vhost-user
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "domain_conf.h"
#include "qemu_conf.h"
#include "virarch.h"

typedef struct _qemuVhostUser qemuVhostUser;
typedef qemuVhostUser *qemuVhostUserPtr;

void
qemuVhostUserFree(qemuVhostUserPtr fw);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(qemuVhostUser, qemuVhostUserFree);

qemuVhostUserPtr
qemuVhostUserParse(const char *path);

char *
qemuVhostUserFormat(qemuVhostUserPtr fw);

int
qemuVhostUserFetchConfigs(char ***configs,
                         bool privileged);

int
qemuVhostUserFillDomainGPU(virQEMUDriverPtr driver,
                           virDomainVideoDefPtr video);

int
qemuVhostUserFillDomainFS(virQEMUDriverPtr driver,
                          virDomainFSDefPtr fs);
