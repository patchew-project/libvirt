/*
 * fs_backend.h: file system backend implementation
 * Author: Olga Krishtal <okrishtal@virtuozzo.com>
 *
 * Copyright (C) 2016 Parallels IP Holdings GmbH
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


#ifndef __VIR_FS_BACKEND_H__
# define __VIR_FS_BACKEND_H__

# include <sys/stat.h>

# include "internal.h"
# include "fs_conf.h"
# include "fs_driver.h"

typedef char * (*virFSBackendFindFSpoolSources)(virConnectPtr conn,
                                                const char *srcSpec,
                                                unsigned int flags);
typedef int (*virFSBackendCheckFSpool)(virFSPoolObjPtr fspool,
                                       bool *active);
typedef int (*virFSBackendStartFSpool)(virConnectPtr conn,
                                      virFSPoolObjPtr fspool);
typedef int (*virFSBackendBuildFSpool)(virConnectPtr conn,
                                       virFSPoolObjPtr fspool,
                                       unsigned int flags);
typedef int (*virFSBackendRefreshFSpool)(virConnectPtr conn,
                                         virFSPoolObjPtr fspool);
typedef int (*virFSBackendStopFSpool)(virConnectPtr conn,
                                       virFSPoolObjPtr fspool);
typedef int (*virFSBackendDeleteFSpool)(virConnectPtr conn,
                                        virFSPoolObjPtr fspool,
                                        unsigned int flags);

/* FIXME */

/* A 'buildItem' backend must remove any volume created on error since
 * the storage driver does not distinguish whether the failure is due
 * to failure to create the volume, to reserve any space necessary for
 * the volume, to get data about the volume, to change it's accessibility,
 * etc. This avoids issues arising from a creation failure due to some
 * external action which created a volume of the same name that libvirt
 * was not aware of between checking the fspool and the create attempt. It
 * also avoids extra round trips to just delete a file.
 */
typedef int (*virFSBackendBuildItem)(virConnectPtr conn,
                                     virFSPoolObjPtr fspool,
                                     virFSItemDefPtr item,
                                     unsigned int flags);
typedef int (*virFSBackendCreateItem)(virConnectPtr conn,
                                      virFSPoolObjPtr fspool,
                                      virFSItemDefPtr item);
typedef int (*virFSBackendRefreshItem)(virConnectPtr conn,
                                       virFSPoolObjPtr fspool,
                                       virFSItemDefPtr item);
typedef int (*virFSBackendDeleteItem)(virConnectPtr conn,
                                      virFSPoolObjPtr fspool,
                                      virFSItemDefPtr item,
                                      unsigned int flags);
typedef int (*virFSBackendBuildItemFrom)(virConnectPtr conn,
                                         virFSPoolObjPtr fspool,
                                         virFSItemDefPtr origitem,
                                         virFSItemDefPtr newitem,
                                         unsigned int flags);

typedef struct _virFSBackend virFSBackend;
typedef virFSBackend *virFSBackendPtr;

/* Callbacks are optional unless documented otherwise; but adding more
 * callbacks provides better fspool support.  */
struct _virFSBackend {
    int type;

    virFSBackendFindFSpoolSources findFSpoolSources;
    virFSBackendCheckFSpool checkFSpool;
    virFSBackendStartFSpool startFSpool;
    virFSBackendBuildFSpool buildFSpool;
    virFSBackendRefreshFSpool refreshFSpool; /* Must be non-NULL */
    virFSBackendStopFSpool stopFSpool;
    virFSBackendDeleteFSpool deleteFSpool;

    virFSBackendBuildItem buildItem;
    virFSBackendBuildItemFrom buildItemFrom;
    virFSBackendCreateItem createItem;
    virFSBackendRefreshItem refreshItem;
    virFSBackendDeleteItem deleteItem;
};

# define VIR_FS_DEFAULT_POOL_PERM_MODE 0755
# define VIR_FS_DEFAULT_ITEM_PERM_MODE  0600

#endif /* __VIR_FS_BACKEND_H__ */
