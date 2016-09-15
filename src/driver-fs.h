/*
 * driver-fs.h: entry points for fs drivers
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

#ifndef __VIR_DRIVER_FS_H__
# define __VIR_DRIVER_FS_H__

# ifndef __VIR_DRIVER_H_INCLUDES___
#  error "Don't include this file directly, only use driver.h"
# endif

typedef int
(*virDrvConnectListAllFSPools)(virConnectPtr conn,
                               virFSPoolPtr **pools,
                               unsigned int flags);

typedef virFSPoolPtr
(*virDrvFSPoolLookupByName)(virConnectPtr conn,
                            const char *name);

typedef virFSPoolPtr
(*virDrvFSPoolLookupByUUID)(virConnectPtr conn,
                            const unsigned char *uuid);
typedef virFSPoolPtr
(*virDrvFSPoolLookupByItem)(virFSItemPtr item);

typedef virFSPoolPtr
(*virDrvFSPoolCreateXML)(virConnectPtr conn,
                         const char *xmlDesc,
                         unsigned int flags);

typedef virFSPoolPtr
(*virDrvFSPoolDefineXML)(virConnectPtr conn,
                         const char *xmlDesc,
                         unsigned int flags);

typedef int
(*virDrvFSPoolUndefine)(virFSPoolPtr fspool);

typedef int
(*virDrvFSPoolBuild)(virFSPoolPtr fspool,
                     unsigned int flags);

typedef int
(*virDrvFSPoolCreate)(virFSPoolPtr fspool,
                     unsigned int flags);
typedef int
(*virDrvFSPoolDestroy)(virFSPoolPtr fspool);
typedef int
(*virDrvFSPoolRefresh)(virFSPoolPtr fspool,
                       unsigned int flags);

typedef int
(*virDrvFSPoolDelete)(virFSPoolPtr fspool,
                      unsigned int flags);

typedef int
(*virDrvFSPoolGetInfo)(virFSPoolPtr fspool,
                       virFSPoolInfoPtr info);

typedef char *
(*virDrvFSPoolGetXMLDesc)(virFSPoolPtr fspool,
                          unsigned int flags);
typedef int
(*virDrvFSPoolGetAutostart)(virFSPoolPtr fspool,
                            int *autostart);
typedef int
(*virDrvFSPoolSetAutostart)(virFSPoolPtr fspool,
                            int autostart);

typedef int
(*virDrvFSPoolNumOfItems)(virFSPoolPtr fspool);

typedef int
(*virDrvFSPoolListItems)(virFSPoolPtr fspool,
                           char **const names,
                           int maxnames);

typedef int
(*virDrvFSPoolListAllItems)(virFSPoolPtr fspool,
                            virFSItemPtr **items,
                            unsigned int flags);

typedef virFSItemPtr
(*virDrvFSItemLookupByName)(virFSPoolPtr fspool,
                            const char *name);

typedef virFSItemPtr
(*virDrvFSItemLookupByKey)(virConnectPtr fspool,
                           const char *key);

typedef virFSItemPtr
(*virDrvFSItemLookupByPath)(virConnectPtr fspool,
                            const char *path);

typedef virFSItemPtr
(*virDrvFSItemCreateXML)(virFSPoolPtr fspool,
                         const char *xmldesc,
                         unsigned int flags);

typedef int
(*virDrvFSItemDelete)(virFSItemPtr item,
                      unsigned int flags);


typedef int
(*virDrvFSItemGetInfo)(virFSItemPtr item,
                       virFSItemInfoPtr info);

typedef char *
(*virDrvFSItemGetXMLDesc)(virFSItemPtr fspool,
                          unsigned int flags);

typedef char *
(*virDrvFSItemGetPath)(virFSItemPtr item);

typedef virFSItemPtr
(*virDrvFSItemCreateXMLFrom)(virFSPoolPtr fspool,
                             const char *xmldesc,
                             virFSItemPtr cloneitem,
                             unsigned int flags);

typedef struct _virFSDriver virFSDriver;
typedef virFSDriver *virFSDriverPtr;

typedef int
(*virDrvFSPoolIsActive)(virFSPoolPtr fspool);

typedef int
(*virDrvFSPoolIsPersistent)(virFSPoolPtr fspool);



/**
 * _virFSDriver:
 *
 * Structure associated to a storage driver, defining the various
 * entry points for it.
 */
struct _virFSDriver {
    const char *name; /* the name of the driver */
    virDrvConnectListAllFSPools connectListAllFSPools;
    virDrvFSPoolLookupByName fsPoolLookupByName;
    virDrvFSPoolLookupByUUID fsPoolLookupByUUID;
    virDrvFSPoolLookupByItem fsPoolLookupByItem;
    virDrvFSPoolCreateXML fsPoolCreateXML;
    virDrvFSPoolDefineXML fsPoolDefineXML;
    virDrvFSPoolBuild fsPoolBuild;
    virDrvFSPoolUndefine fsPoolUndefine;
    virDrvFSPoolCreate fsPoolCreate;
    virDrvFSPoolDestroy fsPoolDestroy;
    virDrvFSPoolDelete fsPoolDelete;
    virDrvFSPoolRefresh fsPoolRefresh;
    virDrvFSPoolGetInfo fsPoolGetInfo;
    virDrvFSPoolGetXMLDesc fsPoolGetXMLDesc;
    virDrvFSPoolGetAutostart fsPoolGetAutostart;
    virDrvFSPoolSetAutostart fsPoolSetAutostart;
    virDrvFSPoolNumOfItems fsPoolNumOfItems;
    virDrvFSPoolListItems fsPoolListItems;
    virDrvFSPoolListAllItems fsPoolListAllItems;
    virDrvFSItemLookupByName fsItemLookupByName;
    virDrvFSItemLookupByKey fsItemLookupByKey;
    virDrvFSItemLookupByPath fsItemLookupByPath;
    virDrvFSItemCreateXML fsItemCreateXML;
    virDrvFSItemCreateXMLFrom fsItemCreateXMLFrom;
    virDrvFSItemDelete fsItemDelete;
    virDrvFSItemGetInfo fsItemGetInfo;
    virDrvFSItemGetXMLDesc fsItemGetXMLDesc;
    virDrvFSItemGetPath fsItemGetPath;
    virDrvFSPoolIsActive fsPoolIsActive;
    virDrvFSPoolIsPersistent fsPoolIsPersistent;
};


#endif /* __VIR_DRIVER_FS_H__ */
