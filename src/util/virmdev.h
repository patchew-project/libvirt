/*
 * virmdev.h: helper APIs for managing host mediated devices
 *
 * Copyright (C) 2017-2018 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *     Erik Skultety <eskultet@redhat.com>
 */

#ifndef __VIR_MDEV_H__
# define __VIR_MDEV_H__

# include "internal.h"
# include "virobject.h"
# include "virutil.h"
# include "virpci.h"

typedef struct _virMediatedDevice virMediatedDevice;
typedef virMediatedDevice *virMediatedDevicePtr;
typedef struct _virMediatedDeviceAddress virMediatedDeviceAddress;
typedef virMediatedDeviceAddress *virMediatedDeviceAddressPtr;
typedef struct _virMediatedDeviceList virMediatedDeviceList;
typedef virMediatedDeviceList *virMediatedDeviceListPtr;

typedef int (*virMediatedDeviceCallback)(virMediatedDevicePtr dev,
                                         const char *path, void *opaque);

virMediatedDevicePtr virMediatedDeviceNew(virPCIDeviceAddressPtr addr,
                                          const char *uuidstr);

virMediatedDevicePtr virMediatedDeviceCopy(virMediatedDevicePtr dev);

void virMediatedDeviceFree(virMediatedDevicePtr dev);

const char *virMediatedDeviceGetPath(virMediatedDevicePtr dev);

void virMediatedDeviceGetUsedBy(virMediatedDevicePtr dev,
                                char **drvname, char **domname);

int virMediatedDeviceSetUsedBy(virMediatedDevicePtr dev,
                               const char *drvname,
                               const char *domname);

char *virMediatedDeviceGetIOMMUGroupDev(virMediatedDevicePtr dev);

virMediatedDeviceListPtr virMediatedDeviceListNew(void);

int  virMediatedDeviceListAdd(virMediatedDeviceListPtr list,
                              virMediatedDevicePtr dev);

virMediatedDevicePtr virMediatedDeviceListGet(virMediatedDeviceListPtr list,
                                              int idx);

size_t virMediatedDeviceListCount(virMediatedDeviceListPtr list);

virMediatedDevicePtr virMediatedDeviceListSteal(virMediatedDeviceListPtr list,
                                                virMediatedDevicePtr dev);

virMediatedDevicePtr virMediatedDeviceListStealIndex(virMediatedDeviceListPtr list,
                                                     int idx);

void virMediatedDeviceListDel(virMediatedDeviceListPtr list,
                              virMediatedDevicePtr dev);

virMediatedDevicePtr virMediatedDeviceListFind(virMediatedDeviceListPtr list,
                                               virMediatedDevicePtr dev);

int virMediatedDeviceListFindIndex(virMediatedDeviceListPtr list,
                                   virMediatedDevicePtr dev);

#endif /* __VIR_MDEV_H__ */
