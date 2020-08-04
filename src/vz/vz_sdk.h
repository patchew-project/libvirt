/*
 * vz_sdk.h: core driver functions for managing
 * Parallels Cloud Server hosts
 *
 * Copyright (C) 2014 Parallels, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include <Parallels.h>

#include "vz_utils.h"

int prlsdkInit(void);
void prlsdkDeinit(void);
int prlsdkConnect(vzDriverPtr driver);
void prlsdkDisconnect(vzDriverPtr driver);
int
prlsdkLoadDomains(vzDriverPtr driver);
virDomainObjPtr
prlsdkAddDomainByUUID(vzDriverPtr driver, const unsigned char *uuid);
virDomainObjPtr
prlsdkAddDomainByName(vzDriverPtr driver, const char *name);
int prlsdkUpdateDomain(vzDriverPtr driver, virDomainObjPtr dom);

int prlsdkStart(virDomainObjPtr dom);
int prlsdkKill(virDomainObjPtr dom);
int prlsdkStop(virDomainObjPtr dom);
int prlsdkPause(virDomainObjPtr dom);
int prlsdkResume(virDomainObjPtr dom);
int prlsdkSuspend(virDomainObjPtr dom);
int prlsdkRestart(virDomainObjPtr dom);
int prlsdkReset(virDomainObjPtr dom);

int
prlsdkApplyConfig(vzDriverPtr driver,
                  virDomainObjPtr dom,
                  virDomainDefPtr new);
int prlsdkCreateVm(vzDriverPtr driver, virDomainDefPtr def);
int prlsdkCreateCt(virConnectPtr conn, virDomainDefPtr def);
int
prlsdkUnregisterDomain(vzDriverPtr driver, virDomainObjPtr dom, unsigned int flags);
int
prlsdkDomainManagedSaveRemove(virDomainObjPtr dom);
int
prlsdkAttachDevice(vzDriverPtr driver, virDomainObjPtr dom, virDomainDeviceDefPtr dev);
int
prlsdkDetachDevice(vzDriverPtr driver, virDomainObjPtr dom, virDomainDeviceDefPtr dev);
int
prlsdkUpdateDevice(vzDriverPtr driver, virDomainObjPtr dom, virDomainDeviceDefPtr dev);
int
prlsdkGetBlockStats(PRL_HANDLE sdkstats, virDomainDiskDefPtr disk, virDomainBlockStatsPtr stats, bool isCt);
int
prlsdkGetNetStats(PRL_HANDLE sdkstas, PRL_HANDLE sdkdom, const char *path, virDomainInterfaceStatsPtr stats);
int
prlsdkGetVcpuStats(PRL_HANDLE sdkstas, int idx, unsigned long long *time);
int
prlsdkGetMemoryStats(PRL_HANDLE sdkstas, virDomainMemoryStatPtr stats, unsigned int nr_stats);
/* memsize is in MiB */
int prlsdkSetMemsize(virDomainObjPtr dom, unsigned int memsize);
int prlsdkSetCpuCount(virDomainObjPtr dom, unsigned int count);
int
prlsdkDomainSetUserPassword(virDomainObjPtr dom,
                            const char *user,
                            const char *password);
virDomainSnapshotObjListPtr prlsdkLoadSnapshots(virDomainObjPtr dom);
int prlsdkCreateSnapshot(virDomainObjPtr dom, const char *description);
int prlsdkDeleteSnapshot(virDomainObjPtr dom, const char *uuid, bool children);
int prlsdkSwitchToSnapshot(virDomainObjPtr dom, const char *uuid, bool paused);
int
prlsdkMigrate(virDomainObjPtr dom,
              virURIPtr uri,
              const char unsigned *session_uuid,
              const char *dname,
              unsigned int flags);

PRL_HANDLE
prlsdkSdkDomainLookupByName(vzDriverPtr driver, const char *name);
int prlsdkCancelJob(virDomainObjPtr dom);
int prlsdkResizeImage(virDomainObjPtr dom, virDomainDiskDefPtr disk, unsigned long long newsize);
