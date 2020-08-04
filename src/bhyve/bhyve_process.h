/*
 * bhyve_process.h: bhyve process management
 *
 * Copyright (C) 2014 Roman Bogorodskiy
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "bhyve_utils.h"

int virBhyveProcessStart(virConnectPtr conn,
                         virDomainObjPtr vm,
                         virDomainRunningReason reason,
                         unsigned int flags);

int virBhyveProcessStop(bhyveConnPtr driver,
                        virDomainObjPtr vm,
                        virDomainShutoffReason reason);

int virBhyveProcessRestart(bhyveConnPtr driver,
                           virDomainObjPtr vm);

int virBhyveProcessShutdown(virDomainObjPtr vm);

int virBhyveGetDomainTotalCpuStats(virDomainObjPtr vm,
                                   unsigned long long *cpustats);

void virBhyveProcessReconnectAll(bhyveConnPtr driver);

typedef enum {
    VIR_BHYVE_PROCESS_START_AUTODESTROY = 1 << 0,
} bhyveProcessStartFlags;
