/*
 * virusb.h: helper APIs for managing host USB devices
 *
 * Copyright (C) 2009 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virobject.h"

#define USB_DEVFS "/dev/bus/usb/"

typedef struct _virUSBDevice virUSBDevice;
typedef virUSBDevice *virUSBDevicePtr;
typedef struct _virUSBDeviceList virUSBDeviceList;
typedef virUSBDeviceList *virUSBDeviceListPtr;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virUSBDeviceList, virObjectUnref);


virUSBDevicePtr virUSBDeviceNew(unsigned int bus,
                                unsigned int devno,
                                const char *vroot);

int virUSBDeviceFindByBus(unsigned int bus,
                          unsigned int devno,
                          const char *vroot,
                          bool mandatory,
                          virUSBDevicePtr *usb);

int virUSBDeviceFindByVendor(unsigned int vendor,
                             unsigned int product,
                             const char *vroot,
                             bool mandatory,
                             virUSBDeviceListPtr *devices);

int virUSBDeviceFind(unsigned int vendor,
                     unsigned int product,
                     unsigned int bus,
                     unsigned int devno,
                     const char *vroot,
                     bool mandatory,
                     virUSBDevicePtr *usb);

void virUSBDeviceFree(virUSBDevicePtr dev);
int virUSBDeviceSetUsedBy(virUSBDevicePtr dev,
                          const char *drv_name,
                          const char *dom_name);
void virUSBDeviceGetUsedBy(virUSBDevicePtr dev,
                           const char **drv_name,
                           const char **dom_name);
const char *virUSBDeviceGetName(virUSBDevicePtr dev);
const char *virUSBDeviceGetPath(virUSBDevicePtr usb);

unsigned int virUSBDeviceGetBus(virUSBDevicePtr dev);
unsigned int virUSBDeviceGetDevno(virUSBDevicePtr dev);

/*
 * Callback that will be invoked once for each file
 * associated with / used for USB host device access.
 *
 * Should return 0 if successfully processed, or
 * -1 to indicate error and abort iteration
 */
typedef int (*virUSBDeviceFileActor)(virUSBDevicePtr dev,
                                     const char *path, void *opaque);

int virUSBDeviceFileIterate(virUSBDevicePtr dev,
                            virUSBDeviceFileActor actor,
                            void *opaque);

virUSBDeviceListPtr virUSBDeviceListNew(void);
int virUSBDeviceListAdd(virUSBDeviceListPtr list,
                        virUSBDevicePtr *dev);
virUSBDevicePtr virUSBDeviceListGet(virUSBDeviceListPtr list,
                                    int idx);
size_t virUSBDeviceListCount(virUSBDeviceListPtr list);
virUSBDevicePtr virUSBDeviceListSteal(virUSBDeviceListPtr list,
                                      virUSBDevicePtr dev);
void virUSBDeviceListDel(virUSBDeviceListPtr list,
                         virUSBDevicePtr dev);
virUSBDevicePtr virUSBDeviceListFind(virUSBDeviceListPtr list,
                                     virUSBDevicePtr dev);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virUSBDevice, virUSBDeviceFree);
