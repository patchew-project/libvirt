/* libvirt-fs.h
 * Summary: APIs for management of filesystem pools and items
 * Description: Provides APIs for the management of filesystem pools and items
 * Author: Olga Krishtal <okrishtal@virtuozzo.com>
 *
 * Copyright (C) 2016 Parallels IP Holdings GmbH
 *
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

#ifndef __VIR_LIBVIRT_FS_H__
# define __VIR_LIBVIRT_FS_H__

# ifndef __VIR_LIBVIRT_H_INCLUDES__
#  error "Don't include this file directly, only use libvirt/libvirt.h"
# endif

typedef enum {
    VIR_FSPOOL_CREATE_NORMAL = 0,

    /* Create the fspool and perform fspool build without any flags */
    VIR_FSPOOL_CREATE_WITH_BUILD = 1 << 0,

    /* Create the fspool and perform fspool build using the
     * exclusive to VIR_FSPOOL_CREATE_WITH_BUILD_NO_OVERWRITE */
    VIR_FSPOOL_CREATE_WITH_BUILD_OVERWRITE = 1 << 1,

    /* Create the pool and perform pool build using the
     * VIR_FSPOOL_BUILD_NO_OVERWRITE flag. This is mutually
     * exclusive to VIR_FSPOOL_CREATE_WITH_BUILD_OVERWRITE */
    VIR_FSPOOL_CREATE_WITH_BUILD_NO_OVERWRITE = 1 << 2,
} virFSPoolCreateFlags;

typedef enum {
    VIR_FSPOOL_BUILD_NEW  = 0,   /* Regular build from scratch */
    VIR_FSPOOL_BUILD_NO_OVERWRITE = (1 << 2),  /* Do not overwrite existing pool */
    VIR_FSPOOL_BUILD_OVERWRITE = (1 << 3),  /* Overwrite data */
} virFSPoolBuildFlags;

/**
 * virFSPool:
 *
 * a virFSPool is a private structure representing a fspool
 */
typedef struct _virFSPool virFSPool;

/**
 * virFSPoolPtr:
 *
 * a virFSPoolPtr is pointer to a virFSPool private structure, this is the
 * type used to reference a fspool in the API.
 */
typedef virFSPool *virFSPoolPtr;

typedef enum {
    VIR_FSPOOL_INACTIVE = 0,
    VIR_FSPOOL_BUILDING = 1,
    VIR_FSPOOL_RUNNING = 2,

# ifdef VIR_ENUM_SENTINELS
    VIR_FSPOOL_STATE_LAST
# endif
} virFSPoolState;

typedef struct _virFSPoolInfo virFSPoolInfo;

struct _virFSPoolInfo {
    int state;                     /* virFSPoolState flags */
    unsigned long long capacity;   /* Logical size bytes */
    unsigned long long allocation; /* Current allocation bytes */
    unsigned long long available;  /* Remaining free space bytes */
};

typedef virFSPoolInfo *virFSPoolInfoPtr;

/**
 * virFSItem:
 *
 * a virFSItem is a private structure representing a fspool item
 */
typedef struct _virFSItem virFSItem;

/**
 * virFSItemPtr:
 *
 * a virFSItemPtr is pointer to a virFSItem private structure, this is the
 * type used to reference a fspool item in the API.
 */
typedef virFSItem *virFSItemPtr;

typedef struct _virFSItemInfo virFSItemInfo;

typedef enum {
    VIR_FSITEM_DIR = 0,
    VIR_FSITEM_LAST
} virFSItemType;

struct _virFSItemInfo {
    int type;                      /* virFSItemType flags */
    unsigned long long capacity;   /* Logical size bytes */
    unsigned long long allocation; /* Current allocation bytes */
};

typedef virFSItemInfo *virFSItemInfoPtr;
/*
 * Get connection from fspool.
 */
virConnectPtr virFSPoolGetConnect(virFSPoolPtr fspool);


/*
 * virConnectListAllFSPoolsFlags:
 *
 * Flags used to tune fspools returned by virConnectListAllFSPools().
 * Note that these flags come in groups; if all bits from a group are 0,
 * then that group is not used to filter results.
 */
typedef enum {
    VIR_CONNECT_LIST_FSPOOLS_INACTIVE      = 1 << 0,
    VIR_CONNECT_LIST_FSPOOLS_ACTIVE        = 1 << 1,

    VIR_CONNECT_LIST_FSPOOLS_PERSISTENT    = 1 << 2,
    VIR_CONNECT_LIST_FSPOOLS_TRANSIENT     = 1 << 3,

    VIR_CONNECT_LIST_FSPOOLS_AUTOSTART     = 1 << 4,
    VIR_CONNECT_LIST_FSPOOLS_NO_AUTOSTART  = 1 << 5,

    /* List fspools by type */
    VIR_CONNECT_LIST_FSPOOLS_DIR           = 1 << 6,
} virConnectListAllFSPoolsFlags;

typedef enum {
    VIR_FS_XML_INACTIVE    = (1 << 0), /* dump inactive fspool/item information */
} virFsXMLFlags;


int virConnectListAllFSPools(virConnectPtr conn,
                             virFSPoolPtr **fspools,
                             unsigned int flags);

/*
 * Lookup fspool by name or uuid
 */

virFSPoolPtr virFSPoolLookupByName(virConnectPtr conn,
                                   const char *name);
virFSPoolPtr virFSPoolLookupByUUID(virConnectPtr conn,
                                   const unsigned char *uuid);
virFSPoolPtr virFSPoolLookupByUUIDString(virConnectPtr conn,
                                         const char *uuid);
virFSPoolPtr virFSPoolLookupByItem(virFSItemPtr item);


/*
 * Creating/destroying fspools
 */
virFSPoolPtr virFSPoolCreateXML(virConnectPtr conn,
                                const char *xmlDesc,
                                unsigned int flags);
virFSPoolPtr virFSPoolDefineXML(virConnectPtr conn,
                                const char *xmlDesc,
                                unsigned int flags);
int virFSPoolBuild(virFSPoolPtr fspool,
                   unsigned int flags);
int virFSPoolRefresh(virFSPoolPtr fspool,
                     unsigned int flags);
int virFSPoolUndefine(virFSPoolPtr fspool);
int virFSPoolCreate(virFSPoolPtr fspool,
                    unsigned int flags);
int virFSPoolDestroy(virFSPoolPtr fspool);
int virFSPoolDelete(virFSPoolPtr fspool,
                    unsigned int flags);
int virFSPoolRefresh(virFSPoolPtr fspool,
                     unsigned int flags);
int virFSPoolRef(virFSPoolPtr fspool);
int virFSPoolFree(virFSPoolPtr fspool);

/*
 * FSPool information
 */
const char * virFSPoolGetName(virFSPoolPtr fspool);
int virFSPoolGetUUID(virFSPoolPtr fspool,
                     unsigned char *uuid);
int virFSPoolGetUUIDString(virFSPoolPtr fspool,
                           char *buf);

int virFSPoolGetInfo(virFSPoolPtr pool,
                     virFSPoolInfoPtr info);

char * virFSPoolGetXMLDesc(virFSPoolPtr fspool,
                          unsigned int flags);
int virFSPoolGetAutostart(virFSPoolPtr fspool,
                          int *autostart);
int virFSPoolSetAutostart(virFSPoolPtr fspool,
                          int autostart);


/*
 * List/lookup fs items within a fspool
 */
int virFSPoolNumOfItems(virFSPoolPtr fspool);
int virFSPoolListItems(virFSPoolPtr fspool,
                       char **const names,
                       int maxnames);
int virFSPoolListAllItems(virFSPoolPtr fspool,
                          virFSItemPtr **items,
                          unsigned int flags);

virConnectPtr virFSItemGetConnect(virFSItemPtr item);

/*
 * Lookup itemumes based on various attributes
 */
virFSItemPtr virFSItemLookupByName(virFSPoolPtr fspool,
                                   const char *name);
virFSItemPtr virFSItemLookupByKey(virConnectPtr conn,
                                  const char *key);
virFSItemPtr virFSItemLookupByPath(virConnectPtr conn,
                                   const char *path);


const char * virFSItemGetName(virFSItemPtr item);
const char * virFSItemGetKey(virFSItemPtr item);

virFSItemPtr virFSItemCreateXML(virFSPoolPtr fspool,
                                const char *xmldesc,
                                unsigned int flags);
virFSItemPtr virFSItemCreateXMLFrom(virFSPoolPtr fspool,
                                    const char *xmldesc,
                                    virFSItemPtr cloneitem,
                                    unsigned int flags);

int virFSItemDelete(virFSItemPtr item,
                    unsigned int flags);
int virFSItemRef(virFSItemPtr item);
int virFSItemFree(virFSItemPtr item);

int virFSItemGetInfo(virFSItemPtr item,
                     virFSItemInfoPtr info);
char * virFSItemGetXMLDesc(virFSItemPtr item,
                           unsigned int flags);

char * virFSItemGetPath(virFSItemPtr item);

int virFSPoolIsActive(virFSPoolPtr fspool);
int virFSPoolIsPersistent(virFSPoolPtr fspool);



#endif /* __VIR_LIBVIRT_FS_H__ */
