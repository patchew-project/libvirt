/*
 * virscsi.h: helper APIs for managing host SCSI devices
 *
 * Copyright (C) 2013 Fujitsu, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virobject.h"

typedef struct _virSCSIDevice virSCSIDevice;
typedef virSCSIDevice *virSCSIDevicePtr;

typedef struct _virSCSIDeviceList virSCSIDeviceList;
typedef virSCSIDeviceList *virSCSIDeviceListPtr;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virSCSIDeviceList, virObjectUnref);


char *virSCSIDeviceGetSgName(const char *sysfs_prefix,
                             const char *adapter,
                             unsigned int bus,
                             unsigned int target,
                             unsigned long long unit) G_GNUC_NO_INLINE;
char *virSCSIDeviceGetDevName(const char *sysfs_prefix,
                              const char *adapter,
                              unsigned int bus,
                              unsigned int target,
                              unsigned long long unit);

virSCSIDevicePtr virSCSIDeviceNew(const char *sysfs_prefix,
                                  const char *adapter,
                                  unsigned int bus,
                                  unsigned int target,
                                  unsigned long long unit,
                                  bool readonly,
                                  bool shareable);

void virSCSIDeviceFree(virSCSIDevicePtr dev);
int virSCSIDeviceSetUsedBy(virSCSIDevicePtr dev,
                           const char *drvname,
                           const char *domname);
bool virSCSIDeviceIsAvailable(virSCSIDevicePtr dev);
const char *virSCSIDeviceGetName(virSCSIDevicePtr dev);
const char *virSCSIDeviceGetPath(virSCSIDevicePtr dev);
unsigned int virSCSIDeviceGetAdapter(virSCSIDevicePtr dev);
unsigned int virSCSIDeviceGetBus(virSCSIDevicePtr dev);
unsigned int virSCSIDeviceGetTarget(virSCSIDevicePtr dev);
unsigned long long virSCSIDeviceGetUnit(virSCSIDevicePtr dev);
bool virSCSIDeviceGetReadonly(virSCSIDevicePtr dev);
bool virSCSIDeviceGetShareable(virSCSIDevicePtr dev);

/*
 * Callback that will be invoked once for each file
 * associated with / used for SCSI host device access.
 *
 * Should return 0 if successfully processed, or
 * -1 to indicate error and abort iteration
 */
typedef int (*virSCSIDeviceFileActor)(virSCSIDevicePtr dev,
                                      const char *path, void *opaque);

int virSCSIDeviceFileIterate(virSCSIDevicePtr dev,
                             virSCSIDeviceFileActor actor,
                             void *opaque);

virSCSIDeviceListPtr virSCSIDeviceListNew(void);
int virSCSIDeviceListAdd(virSCSIDeviceListPtr list,
                         virSCSIDevicePtr dev);
virSCSIDevicePtr virSCSIDeviceListGet(virSCSIDeviceListPtr list,
                                      int idx);
size_t virSCSIDeviceListCount(virSCSIDeviceListPtr list);
virSCSIDevicePtr virSCSIDeviceListSteal(virSCSIDeviceListPtr list,
                                        virSCSIDevicePtr dev);
void virSCSIDeviceListDel(virSCSIDeviceListPtr list,
                          virSCSIDevicePtr dev,
                          const char *drvname,
                          const char *domname);
virSCSIDevicePtr virSCSIDeviceListFind(virSCSIDeviceListPtr list,
                                       virSCSIDevicePtr dev);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virSCSIDevice, virSCSIDeviceFree);
