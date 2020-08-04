/*
 * virscsivhost.h: helper APIs for managing host scsi_host devices
 *
 * Copyright (C) 2016 IBM Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virobject.h"

typedef struct _virSCSIVHostDevice virSCSIVHostDevice;
typedef virSCSIVHostDevice *virSCSIVHostDevicePtr;
typedef struct _virSCSIVHostDeviceList virSCSIVHostDeviceList;
typedef virSCSIVHostDeviceList *virSCSIVHostDeviceListPtr;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virSCSIVHostDeviceList, virObjectUnref);


typedef int (*virSCSIVHostDeviceFileActor)(virSCSIVHostDevicePtr dev,
                                           const char *name, void *opaque);

int virSCSIVHostDeviceFileIterate(virSCSIVHostDevicePtr dev,
                                  virSCSIVHostDeviceFileActor actor,
                                  void *opaque);
const char *virSCSIVHostDeviceGetName(virSCSIVHostDevicePtr dev);
const char *virSCSIVHostDeviceGetPath(virSCSIVHostDevicePtr dev);
virSCSIVHostDevicePtr virSCSIVHostDeviceListGet(virSCSIVHostDeviceListPtr list,
                                                int idx);
size_t virSCSIVHostDeviceListCount(virSCSIVHostDeviceListPtr list);
virSCSIVHostDevicePtr virSCSIVHostDeviceListSteal(virSCSIVHostDeviceListPtr list,
                                                  virSCSIVHostDevicePtr dev);
virSCSIVHostDevicePtr virSCSIVHostDeviceListFind(virSCSIVHostDeviceListPtr list,
                                                 virSCSIVHostDevicePtr dev);
int  virSCSIVHostDeviceListAdd(virSCSIVHostDeviceListPtr list,
                               virSCSIVHostDevicePtr dev);
void virSCSIVHostDeviceListDel(virSCSIVHostDeviceListPtr list,
                               virSCSIVHostDevicePtr dev);
virSCSIVHostDeviceListPtr virSCSIVHostDeviceListNew(void);
virSCSIVHostDevicePtr virSCSIVHostDeviceNew(const char *name);
int virSCSIVHostDeviceSetUsedBy(virSCSIVHostDevicePtr dev,
                                const char *drvname,
                                const char *domname);
void virSCSIVHostDeviceGetUsedBy(virSCSIVHostDevicePtr dev,
                                 const char **drv_name,
                                 const char **dom_name);
void virSCSIVHostDeviceFree(virSCSIVHostDevicePtr dev);
int virSCSIVHostOpenVhostSCSI(int *vhostfd) G_GNUC_NO_INLINE;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virSCSIVHostDevice, virSCSIVHostDeviceFree);
