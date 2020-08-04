/*
 * virnodedeviceobj.h: node device object handling for node devices
 *                     (derived from node_device_conf.h)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virthread.h"

#include "node_device_conf.h"
#include "object_event.h"


typedef struct _virNodeDeviceObj virNodeDeviceObj;
typedef virNodeDeviceObj *virNodeDeviceObjPtr;

typedef struct _virNodeDeviceObjList virNodeDeviceObjList;
typedef virNodeDeviceObjList *virNodeDeviceObjListPtr;

typedef struct _virNodeDeviceDriverState virNodeDeviceDriverState;
typedef virNodeDeviceDriverState *virNodeDeviceDriverStatePtr;
struct _virNodeDeviceDriverState {
    virMutex lock;
    virCond initCond;
    bool initialized;

    /* pid file FD, ensures two copies of the driver can't use the same root */
    int lockFD;

    char *stateDir;

    virNodeDeviceObjListPtr devs;       /* currently-known devices */
    void *privateData;                  /* driver-specific private data */
    bool privileged;                    /* whether we run in privileged mode */

    /* Immutable pointer, self-locking APIs */
    virObjectEventStatePtr nodeDeviceEventState;
};

void
virNodeDeviceObjEndAPI(virNodeDeviceObjPtr *obj);

virNodeDeviceDefPtr
virNodeDeviceObjGetDef(virNodeDeviceObjPtr obj);

virNodeDeviceObjPtr
virNodeDeviceObjListFindByName(virNodeDeviceObjListPtr devs,
                               const char *name);

virNodeDeviceObjPtr
virNodeDeviceObjListFindBySysfsPath(virNodeDeviceObjListPtr devs,
                                    const char *sysfs_path)
    ATTRIBUTE_NONNULL(2);

virNodeDeviceObjPtr
virNodeDeviceObjListFindSCSIHostByWWNs(virNodeDeviceObjListPtr devs,
                                       const char *wwnn,
                                       const char *wwpn);

virNodeDeviceObjPtr
virNodeDeviceObjListAssignDef(virNodeDeviceObjListPtr devs,
                              virNodeDeviceDefPtr def);

void
virNodeDeviceObjListRemove(virNodeDeviceObjListPtr devs,
                           virNodeDeviceObjPtr dev);

int
virNodeDeviceObjListGetParentHost(virNodeDeviceObjListPtr devs,
                                  virNodeDeviceDefPtr def);

virNodeDeviceObjListPtr
virNodeDeviceObjListNew(void);

void
virNodeDeviceObjListFree(virNodeDeviceObjListPtr devs);

typedef bool
(*virNodeDeviceObjListFilter)(virConnectPtr conn,
                              virNodeDeviceDefPtr def);

int
virNodeDeviceObjListNumOfDevices(virNodeDeviceObjListPtr devs,
                                 virConnectPtr conn,
                                 const char *cap,
                                 virNodeDeviceObjListFilter filter);

int
virNodeDeviceObjListGetNames(virNodeDeviceObjListPtr devs,
                             virConnectPtr conn,
                             virNodeDeviceObjListFilter filter,
                             const char *cap,
                             char **const names,
                             int maxnames);

int
virNodeDeviceObjListExport(virConnectPtr conn,
                           virNodeDeviceObjListPtr devobjs,
                           virNodeDevicePtr **devices,
                           virNodeDeviceObjListFilter filter,
                           unsigned int flags);

void
virNodeDeviceObjSetSkipUpdateCaps(virNodeDeviceObjPtr obj,
                                  bool skipUpdateCaps);
virNodeDeviceObjPtr
virNodeDeviceObjListFindMediatedDeviceByUUID(virNodeDeviceObjListPtr devs,
                                             const char *uuid);
