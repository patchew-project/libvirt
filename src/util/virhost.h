/*
 * virhost.h: helper APIs for managing host scsi_host devices
 *
 * Copyright (C) 2016 IBM Corporation
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
 *     Eric Farman <farman@linux.vnet.ibm.com>
 */

#ifndef __VIR_HOST_H__
# define __VIR_HOST_H__

# include "internal.h"
# include "virobject.h"
# include "virutil.h"

typedef struct _virHostDevice virHostDevice;
typedef virHostDevice *virHostDevicePtr;
typedef struct _virHostDeviceAddress virHostDeviceAddress;
typedef virHostDeviceAddress *virHostDeviceAddressPtr;
typedef struct _virHostDeviceList virHostDeviceList;
typedef virHostDeviceList *virHostDeviceListPtr;

struct _virHostDeviceAddress {
    char *wwpn;
};

typedef int (*virHostDeviceFileActor)(virHostDevicePtr dev,
                                      const char *name, void *opaque);

int virHostDeviceFileIterate(virHostDevicePtr dev,
                             virHostDeviceFileActor actor,
                             void *opaque);
const char *virHostDeviceGetName(virHostDevicePtr dev);
virHostDevicePtr virHostDeviceListGet(virHostDeviceListPtr list,
                                      int idx);
size_t virHostDeviceListCount(virHostDeviceListPtr list);
virHostDevicePtr virHostDeviceListSteal(virHostDeviceListPtr list,
                                        virHostDevicePtr dev);
int virHostDeviceListFindIndex(virHostDeviceListPtr list,
                               virHostDevicePtr dev);
virHostDevicePtr virHostDeviceListFind(virHostDeviceListPtr list,
                                       virHostDevicePtr dev);
int  virHostDeviceListAdd(virHostDeviceListPtr list,
                          virHostDevicePtr dev);
void virHostDeviceListDel(virHostDeviceListPtr list,
                          virHostDevicePtr dev,
                          const char *drvname,
                          const char *domname);
virHostDeviceListPtr virHostDeviceListNew(void);
virHostDevicePtr virHostDeviceNew(const char *name);
int virHostDeviceSetUsedBy(virHostDevicePtr dev,
                           const char *drvname,
                           const char *domname);
void virHostDeviceFree(virHostDevicePtr dev);
int virHostOpenVhostSCSI(int *vhostfd);

#endif /* __VIR_HOST_H__ */
