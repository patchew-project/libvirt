/*
 * virnodedeviceobj.h: node device object handling for node devices
 *                     (derived from node_device_conf.h)
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
 */

#ifndef __VIRNODEDEVICEOBJ_H__
# define __VIRNODEDEVICEOBJ_H__

# include "internal.h"
# include "virpoolobj.h"
# include "virthread.h"

# include "object_event.h"


typedef struct _virNodeDeviceDriverState virNodeDeviceDriverState;
typedef virNodeDeviceDriverState *virNodeDeviceDriverStatePtr;
struct _virNodeDeviceDriverState {
    virMutex lock;

    virPoolObjTablePtr devs;		/* currently-known devices */
    void *privateData;			/* driver-specific private data */

    /* Immutable pointer, self-locking APIs */
    virObjectEventStatePtr nodeDeviceEventState;
};

virPoolObjPtr virNodeDeviceObjFindBySysfsPath(virPoolObjTablePtr devs,
                                              const char *sysfs_path)
    ATTRIBUTE_NONNULL(2);

int virNodeDeviceObjGetParentHost(virPoolObjTablePtr devs,
                                  const char *dev_name,
                                  const char *parent_name,
                                  int *parent_host);

int virNodeDeviceObjGetParentHostByWWNs(virPoolObjTablePtr devs,
                                        const char *dev_name,
                                        const char *parent_wwnn,
                                        const char *parent_wwpn,
                                        int *parent_host);

int virNodeDeviceObjGetParentHostByFabricWWN(virPoolObjTablePtr devs,
                                             const char *dev_name,
                                             const char *parent_fabric_wwn,
                                             int *parent_host);

int virNodeDeviceObjFindVportParentHost(virPoolObjTablePtr devs,
                                        int *parent_host);

virPoolObjPtr virNodeDeviceObjFindByName(virPoolObjTablePtr devobjs,
                                         const char *name);

int virNodeDeviceObjNumOfDevices(virPoolObjTablePtr devobjs,
                                 virConnectPtr conn,
                                 const char *cap,
                                 virPoolObjACLFilter aclfilter);

int virNodeDeviceObjGetNames(virPoolObjTablePtr devobjs,
                             virConnectPtr conn,
                             virPoolObjACLFilter aclfilter,
                             const char *cap,
                             char **const names,
                             int maxnames);

int virNodeDeviceObjExportList(virConnectPtr conn,
                               virPoolObjTablePtr devobjs,
                               virNodeDevicePtr **devices,
                               virPoolObjACLFilter aclfilter,
                               unsigned int flags);

#endif /* __VIRNODEDEVICEOBJ_H__ */
