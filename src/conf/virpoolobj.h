/*
 * virpoolobj.h: internal pool objects handling
 *
 * Copyright (C) 2016 Red Hat, Inc.
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

#ifndef __VIRPOOLOBJ_H__
# define __VIRPOOLOBJ_H__

# include "internal.h"

typedef enum {
    VIR_POOLOBJTABLE_NODEDEVICE,
    VIR_POOLOBJTABLE_INTERFACE,
    VIR_POOLOBJTABLE_NWFILTER,
    VIR_POOLOBJTABLE_VOLUME,
    VIR_POOLOBJTABLE_BLOCK_STORAGE,
    VIR_POOLOBJTABLE_SECRET,
    VIR_POOLOBJTABLE_NETWORK,
    VIR_POOLOBJTABLE_SNAPSHOT,
    VIR_POOLOBJTABLE_DOMAIN,

    VIR_POOLOBJTABLE_LAST
} virPoolObjTableType;

/* Some default hash table size start values */
# define VIR_POOLOBJTABLE_NODEDEVICE_HASHSTART 50
# define VIR_POOLOBJTABLE_INTERFACE_HASHSTART 10
# define VIR_POOLOBJTABLE_NWFILTER_HASHSTART 20
# define VIR_POOLOBJTABLE_VOLUME_HASHSTART 10
# define VIR_POOLOBJTABLE_BLOCK_STORAGE_HASHSTART 20
# define VIR_POOLOBJTABLE_SECRET_HASHSTART 20
# define VIR_POOLOBJTABLE_NETWORK_HASHSTART 10
# define VIR_POOLOBJTABLE_SNAPSHOT_HASHSTART 10
# define VIR_POOLOBJTABLE_DOMAIN_HASHSTART 50

typedef struct _virPoolDef virPoolDef;
typedef virPoolDef *virPoolDefPtr;

typedef struct _virPoolObj virPoolObj;
typedef virPoolObj *virPoolObjPtr;

typedef struct _virPoolObjTable virPoolObjTable;
typedef virPoolObjTable *virPoolObjTablePtr;


/* virPoolObjTableAssignDef
 */
typedef int (*virPoolObjTableAssignDefFunc)(virPoolObjPtr obj,
                                            void *newDef,
                                            void *oldDef,
                                            unsigned int flags);

/*
 * virPoolObjTableSearchIterator:
 * @obj: the virPoolObj for the current table entry
 * @opaque: opaque user data provided at registration
 *
 * The virPoolObjTableSearch[Ref] functions walk through the PoolObjTable
 * hash table entries expecting the SearchIterator function to return true
 * once the searched on criteria is met.
 *
 * It is forbidden to call any other libvirt APIs from an implementation
 * of this callback, since it can be invoked from a context which is not
 * re-entrant safe. Failure to abide by this requirement may lead to
 * application deadlocks or crashes.
 */
typedef bool (*virPoolObjTableSearchIterator)(virPoolObjPtr obj, void *opaque);
struct virPoolObjTableSearchIterData {
    virPoolObjTableSearchIterator callback;
    void *opaque;
};

/*
 * virPoolObjACLFilter:
 * Used by Collect and List API's in order filter results based on object ACL
 */
typedef bool (*virPoolObjACLFilter)(virConnectPtr conn, void *objdef);

/*
 * virPoolObjMatchFilter:
 * Used by Collect and List API's in order filter results based on caller
 * specific criteria
 */
typedef bool (*virPoolObjMatchFilter)(virPoolObjPtr obj, unsigned int filter);

/*
 * virPoolObjTableListCallback:
 * Similar to the Match filter except, passing an opaque handle allows the
 * List function to alter/generate what data is collected/filtered
 */
typedef int (*virPoolObjTableListCallback)(virPoolObjPtr obj, void *opaque);
struct virPoolObjTableListIterData {
    virConnectPtr conn;
    virPoolObjACLFilter aclfilter;
    virPoolObjTableListCallback callback;
    void *opaque;
    int ret;
};


typedef void *(*virPoolObjTableCloneCallback)(virPoolObjPtr src);

typedef void (*virPoolObjTableIteratorCallback)(virPoolObjPtr obj,
                                                void *opaque);


virPoolObjPtr virPoolObjNew(virPoolDefPtr pooldef,
                            void *def,
                            void *newDef,
                            virFreeCallback defFreeFunc);

void virPoolObjSetActive(virPoolObjPtr obj, bool active);

void virPoolObjSetAutostart(virPoolObjPtr obj, bool autostart);

void virPoolObjSetBeingRemoved(virPoolObjPtr obj, bool beingRemoved);

void virPoolObjSetPersistent(virPoolObjPtr obj, bool persistent);

void virPoolObjSetDef(virPoolObjPtr obj, void *def);

void virPoolObjSetNewDef(virPoolObjPtr obj, void *newDef);

void virPoolObjSetPrivateData(virPoolObjPtr obj,
                              void *privateData,
                              virFreeCallback privateDataFreeFunc);

bool virPoolObjIsActive(const virPoolObj *obj);

bool virPoolObjIsAutostart(const virPoolObj *obj);

bool virPoolObjIsBeingRemoved(const virPoolObj *obj);

bool virPoolObjIsPersistent(const virPoolObj *obj);

void *virPoolObjGetDef(const virPoolObj *obj);

void *virPoolObjGetNewDef(const virPoolObj *obj);

void *virPoolObjGetPrivateData(const virPoolObj *obj);

void virPoolObjEndAPI(virPoolObjPtr *obj);

virPoolObjTablePtr virPoolObjTableNew(virPoolObjTableType type,
                                      int hashStart,
                                      bool nameOnly);

virPoolObjPtr virPoolObjTableAdd(virPoolObjTablePtr poolobjs,
                                 const char *new_uuidstr,
                                 const char *new_name,
                                 void *def,
                                 void *newDef,
                                 void *oldDef,
                                 virFreeCallback defFreeFunc,
                                 virPoolObjTableAssignDefFunc assignDefFunc,
                                 unsigned int assignFlags)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(3) ATTRIBUTE_NONNULL(4)
    ATTRIBUTE_NONNULL(7);

void virPoolObjTableClearAll(virPoolObjTablePtr poolobjs);

void virPoolObjTableRemove(virPoolObjTablePtr poolobjs,
                           virPoolObjPtr *obj);

virPoolObjPtr virPoolObjTableFindByUUID(virPoolObjTablePtr poolobjs,
                                        const unsigned char *uuid);

virPoolObjPtr virPoolObjTableFindByUUIDRef(virPoolObjTablePtr poolobjs,
                                           const unsigned char *uuid);

virPoolObjPtr virPoolObjTableFindByName(virPoolObjTablePtr poolobjs,
                                        const char *name);

virPoolObjPtr virPoolObjTableSearch(virPoolObjTablePtr poolobjs,
                                    virPoolObjTableSearchIterator callback,
                                    void *opaque);

virPoolObjPtr virPoolObjTableSearchRef(virPoolObjTablePtr poolobjs,
                                       virPoolObjTableSearchIterator callback,
                                       void *opaque);

void virPoolObjTableIterate(virPoolObjTablePtr src,
                            virPoolObjTableIteratorCallback callback,
                            void *opaque);

int virPoolObjTableCollect(virPoolObjTablePtr poolobjs,
                           virConnectPtr conn,
                           virPoolObjPtr **objs,
                           size_t *nobjs,
                           virPoolObjACLFilter aclfilter,
                           virPoolObjMatchFilter matchfilter,
                           unsigned int flags);

int virPoolObjTableList(virPoolObjTablePtr poolobjs,
                        virConnectPtr conn,
                        virPoolObjACLFilter aclfilter,
                        virPoolObjTableListCallback callback,
                        void *opaque);

virPoolObjTablePtr virPoolObjTableClone(virPoolObjTablePtr src,
                                        virPoolObjTableCloneCallback callback);

void virPoolObjTablePrune(virPoolObjTablePtr poolobjs,
                          virPoolObjMatchFilter matchfilter,
                          unsigned int flags);

#endif /* __VIRPOOLOBJ_H__ */
