/*
 * virnvme.h: helper APIs for managing NVMe devices
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virpci.h"

typedef struct _virNVMeDevice virNVMeDevice;
typedef virNVMeDevice *virNVMeDevicePtr;

/* Note that this list is lockable, and in fact, it is caller's
 * responsibility to acquire the lock and release it. The reason
 * is that in a lot of cases the list must be locked between two
 * API calls and therefore only caller knows when it is safe to
 * finally release the lock. */
typedef struct _virNVMeDeviceList virNVMeDeviceList;
typedef virNVMeDeviceList *virNVMeDeviceListPtr;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virNVMeDeviceList, virObjectUnref);

virNVMeDevicePtr
virNVMeDeviceNew(const virPCIDeviceAddress *address,
                 unsigned long namespace,
                 bool managed);

void
virNVMeDeviceFree(virNVMeDevicePtr dev);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virNVMeDevice, virNVMeDeviceFree);

virNVMeDevicePtr
virNVMeDeviceCopy(const virNVMeDevice *dev);

const virPCIDeviceAddress *
virNVMeDeviceAddressGet(const virNVMeDevice *dev);

void
virNVMeDeviceUsedByClear(virNVMeDevicePtr dev);

void
virNVMeDeviceUsedByGet(const virNVMeDevice *dev,
                       const char **drv,
                       const char **dom);

void
virNVMeDeviceUsedBySet(virNVMeDevicePtr dev,
                       const char *drv,
                       const char *dom);

virNVMeDeviceListPtr
virNVMeDeviceListNew(void);

size_t
virNVMeDeviceListCount(const virNVMeDeviceList *list);

int
virNVMeDeviceListAdd(virNVMeDeviceListPtr list,
                     const virNVMeDevice *dev);

int
virNVMeDeviceListDel(virNVMeDeviceListPtr list,
                     const virNVMeDevice *dev);

virNVMeDevicePtr
virNVMeDeviceListGet(virNVMeDeviceListPtr list,
                     size_t i);

virNVMeDevicePtr
virNVMeDeviceListLookup(virNVMeDeviceListPtr list,
                        const virNVMeDevice *dev);

ssize_t
virNVMeDeviceListLookupIndex(virNVMeDeviceListPtr list,
                             const virNVMeDevice *dev);

virPCIDeviceListPtr
virNVMeDeviceListCreateDetachList(virNVMeDeviceListPtr activeList,
                                  virNVMeDeviceListPtr toDetachList);

virPCIDeviceListPtr
virNVMeDeviceListCreateReAttachList(virNVMeDeviceListPtr activeList,
                                    virNVMeDeviceListPtr toReAttachList);
