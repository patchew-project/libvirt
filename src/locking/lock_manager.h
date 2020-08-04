/*
 * lock_manager.h: Defines the internal lock manager API
 *
 * Copyright (C) 2010-2011 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "lock_driver.h"

typedef struct _virLockManagerPlugin virLockManagerPlugin;
typedef virLockManagerPlugin *virLockManagerPluginPtr;

virLockManagerPluginPtr virLockManagerPluginNew(const char *name,
                                                const char *driverName,
                                                const char *configDir,
                                                unsigned int flags);
void virLockManagerPluginRef(virLockManagerPluginPtr plugin);
void virLockManagerPluginUnref(virLockManagerPluginPtr plugin);

const char *virLockManagerPluginGetName(virLockManagerPluginPtr plugin);
bool virLockManagerPluginUsesState(virLockManagerPluginPtr plugin);

virLockDriverPtr virLockManagerPluginGetDriver(virLockManagerPluginPtr plugin);

virLockManagerPtr virLockManagerNew(virLockDriverPtr driver,
                                    unsigned int type,
                                    size_t nparams,
                                    virLockManagerParamPtr params,
                                    unsigned int flags);

int virLockManagerAddResource(virLockManagerPtr manager,
                              unsigned int type,
                              const char *name,
                              size_t nparams,
                              virLockManagerParamPtr params,
                              unsigned int flags);

int virLockManagerAcquire(virLockManagerPtr manager,
                          const char *state,
                          unsigned int flags,
                          virDomainLockFailureAction action,
                          int *fd);
int virLockManagerRelease(virLockManagerPtr manager,
                          char **state,
                          unsigned int flags);
int virLockManagerInquire(virLockManagerPtr manager,
                          char **state,
                          unsigned int flags);

int virLockManagerFree(virLockManagerPtr manager);
