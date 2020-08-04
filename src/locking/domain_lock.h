/*
 * domain_lock.h: Locking for domain lifecycle operations
 *
 * Copyright (C) 2010-2011 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "domain_conf.h"
#include "lock_manager.h"

int virDomainLockProcessStart(virLockManagerPluginPtr plugin,
                              const char *uri,
                              virDomainObjPtr dom,
                              bool paused,
                              int *fd);
int virDomainLockProcessPause(virLockManagerPluginPtr plugin,
                              virDomainObjPtr dom,
                              char **state);
int virDomainLockProcessResume(virLockManagerPluginPtr plugin,
                               const char *uri,
                               virDomainObjPtr dom,
                               const char *state);
int virDomainLockProcessInquire(virLockManagerPluginPtr plugin,
                                virDomainObjPtr dom,
                                char **state);

int virDomainLockImageAttach(virLockManagerPluginPtr plugin,
                             const char *uri,
                             virDomainObjPtr dom,
                             virStorageSourcePtr src);
int virDomainLockImageDetach(virLockManagerPluginPtr plugin,
                             virDomainObjPtr dom,
                             virStorageSourcePtr src);

int virDomainLockLeaseAttach(virLockManagerPluginPtr plugin,
                             const char *uri,
                             virDomainObjPtr dom,
                             virDomainLeaseDefPtr lease);
int virDomainLockLeaseDetach(virLockManagerPluginPtr plugin,
                             virDomainObjPtr dom,
                             virDomainLeaseDefPtr lease);
