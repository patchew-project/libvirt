/*
 * virpoolobj.c: internal pool objects handling
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

#include <config.h>

#include "datatypes.h"
#include "viralloc.h"
#include "virpoolobj.h"
#include "virerror.h"
#include "virlog.h"
#include "virhash.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_POOLOBJ

VIR_LOG_INIT("conf.virpoolobj");

VIR_ENUM_DECL(virPoolObjTable);
VIR_ENUM_IMPL(virPoolObjTable, VIR_POOLOBJTABLE_LAST,
              "nodedev", "interface", "nwfilter", "volume", "block storage",
              "secret", "network", "domain snapshot", "domain")

struct _virPoolDef {
    char *uuid;
    char *name;
};

struct _virPoolObj {
    virObjectLockable parent;

    /* copy of the def->name and def->uuid (if available) used for lookups
     * without needing to know the object type */
    virPoolDefPtr pooldef;

    /* Boolean states that can be managed by consumer */
    bool active;       /* object is active or not */
    bool beingRemoved; /* object being prepared for removal */
    bool autostart;    /* object is autostarted */
    bool persistent;   /* object definition is persistent */
    bool updated;      /* object config has been updated in some way */

    /* Boolean states managed by virPoolObj */
    bool removing;  /* true when object has been removed from table(s) */

    /* 'def' is the current config definition.
     * 'newDef' is the next boot configuration.
     * The 'type' value will describe which _vir*Def struct describes
     * The 'privateData' will be private object data for each pool obj
     * and specific to the 'type' of object being managed. */
    void *def;
    void *newDef;
    virFreeCallback defFreeFunc;

    /* Private data to be managed by the specific object using
     * vir{$OBJ}ObjPrivate{Get|Set}{$FIELD} type API's. Each of those
     * calling the virPoolObjGetPrivateData in order to access this field */
    void *privateData;
    void (*privateDataFreeFunc)(void *);
};

struct _virPoolObjTable {
    virObjectLockable parent;
    virPoolObjTableType type;
    bool nameOnly;
    int hashStart;

    /* uuid string -> virPoolObj  mapping
     * for O(1), lockless lookup-by-uuid */
    virHashTable *objsUuid;

    /* name -> virPoolObj mapping for O(1),
     * lockless lookup-by-name */
    virHashTable *objsName;
};

static virClassPtr virPoolObjClass;
static void virPoolObjDispose(void *obj);

static int
virPoolObjOnceInit(void)
{
    if (!(virPoolObjClass = virClassNew(virClassForObjectLockable(),
                                        "virPoolObj",
                                        sizeof(virPoolObj),
                                        virPoolObjDispose)))
        return -1;

    return 0;
}
VIR_ONCE_GLOBAL_INIT(virPoolObj)


/**
 * @pooldef: The 'name' and possible 'uuid' of the def for lookups
 * @def: The 'def' for the object
 * @newDef: The 'newDef' (next) for the object
 * @defFreeFunc: The vir$OBJFreeDef callback
 *
 * Create/return a new poolobj to be placed into the hash table
 *
 * Returns: Object on success, NULL on failure
 */
virPoolObjPtr
virPoolObjNew(virPoolDefPtr pooldef,
              void *def,
              void *newDef,
              virFreeCallback defFreeFunc)
{
    virPoolObjPtr obj;

    if (virPoolObjInitialize() < 0)
        return NULL;

    if (!(obj = virObjectLockableNew(virPoolObjClass)))
        return NULL;

    VIR_DEBUG("obj=%p pooldef=%p def=%p newDef=%p ff=%p",
              obj, pooldef, def, newDef, defFreeFunc);
    obj->pooldef = pooldef;
    obj->def = def;
    obj->newDef = newDef;
    obj->defFreeFunc = defFreeFunc;

    return obj;
}


static void
virPoolDefFree(virPoolDefPtr def)
{
    if (!def)
        return;

    VIR_FREE(def->uuid);
    VIR_FREE(def->name);
    VIR_FREE(def);
}


static void
virPoolObjDispose(void *_obj)
{
    virPoolObjPtr obj = _obj;

    if (!obj)
        return;

    VIR_DEBUG("obj=%p, pooldef=%p def=%p newDef=%p ff=%p",
              obj, obj->pooldef, obj->def, obj->newDef, obj->defFreeFunc);

    virPoolDefFree(obj->pooldef);
    (obj->defFreeFunc)(obj->def);
    (obj->defFreeFunc)(obj->newDef);

    if (obj->privateDataFreeFunc)
        (obj->privateDataFreeFunc)(obj->privateData);
}


/**
 * @obj: A locked poolobj pointer
 * @active: The "new" state
 *
 * Set the @active bit in the object.
 */
void
virPoolObjSetActive(virPoolObjPtr obj,
                    bool active)
{
    obj->active = active;
}


/**
 * @obj: A locked poolobj pointer
 * @autostart: The @autostart value sent from the driver
 *
 * Set the @autostart bit in the object.
 */
void
virPoolObjSetAutostart(virPoolObjPtr obj,
                       bool autostart)
{
    obj->autostart = autostart;
}


/**
 * @obj: A locked poolobj pointer
 * @beingRemoved: The @beingRemoved value sent from the driver
 *
 * Set the @beingRemoved bit in the object.
 */
void
virPoolObjSetBeingRemoved(virPoolObjPtr obj,
                          bool beingRemoved)
{
    obj->beingRemoved = beingRemoved;
}


/**
 * @obj: A locked poolobj pointer
 * @persistent: The @persistent value sent from the driver
 *
 * Set the @persistent bit in the object.
 */
void
virPoolObjSetPersistent(virPoolObjPtr obj,
                        bool persistent)
{
    obj->persistent = persistent;
}


/**
 * @obj: A locked poolobj pointer
 * @def: Opaque 'def' to replace in the object
 *
 * Set the @def in the object. This code will call the defFreeFunc
 * on the existing object in obj->def before assigning.
 */
void
virPoolObjSetDef(virPoolObjPtr obj,
                 void *def)
{
    obj->defFreeFunc(obj->def);
    obj->def = def;
}


/**
 * @obj: A locked poolobj pointer
 * @newDef: Opaque 'newDef' to replace in the object
 *
 * Set the @newDef in the object. This code will call the defFreeFunc
 * on the existing object in obj->newDef before assigning.
 */
void
virPoolObjSetNewDef(virPoolObjPtr obj,
                    void *newDef)
{
    obj->defFreeFunc(obj->newDef);
    obj->newDef = newDef;
}


/**
 * @obj: A locked poolobj pointer
 * @privateData: Pointer to private data
 * @privateDataFreeFunc: Callback function when the object is free t
 *                       free the privateData details
 *
 * Set the @privateData address and @privateDataFreeFunc addresses.
 */
void
virPoolObjSetPrivateData(virPoolObjPtr obj,
                         void *privateData,
                         virFreeCallback privateDataFreeFunc)
{
    obj->privateData = privateData;
    obj->privateDataFreeFunc = privateDataFreeFunc;
}


/* virPoolObjIs{Active|Autostart|BeingRemoved|Persistent}
 * @obj: A locked poolobj pointer
 *
 * Various accessor fetch functions for the bool bits in the object
 */
bool
virPoolObjIsActive(const virPoolObj *obj)
{
    return obj->active;
}


bool
virPoolObjIsAutostart(const virPoolObj *obj)
{
    return obj->autostart;
}


bool
virPoolObjIsBeingRemoved(const virPoolObj *obj)
{
    return obj->beingRemoved;
}


bool
virPoolObjIsPersistent(const virPoolObj *obj)
{
    return obj->persistent;
}


/**
 * @obj: A locked poolobj pointer
 *
 * Return the obj->def pointer to the caller
 */
void *
virPoolObjGetDef(const virPoolObj *obj)
{
    return obj->def;
}


/**
 * @obj: A locked poolobj pointer
 *
 * Return the obj->newDef pointer to the caller
 */
void *
virPoolObjGetNewDef(const virPoolObj *obj)
{
    return obj->newDef;
}


/**
 * @obj: A locked poolobj pointer
 *
 * Return the obj->privateData pointer to the caller
 */
void *
virPoolObjGetPrivateData(const virPoolObj *obj)
{
    return obj->privateData;
}


/**
 * @obj: A locked and ref'd poolobj pointer
 *
 * Unref and unlock the object
 */
void
virPoolObjEndAPI(virPoolObjPtr *obj)
{
    if (!*obj)
        return;

    virObjectUnlock(*obj);
    virObjectUnref(*obj);
    *obj = NULL;
}


static virClassPtr virPoolObjTableClass;
static void virPoolObjTableDispose(void *obj);

static int
virPoolObjTableOnceInit(void)
{
    if (!(virPoolObjTableClass = virClassNew(virClassForObjectLockable(),
                                             "virPoolObjTable",
                                             sizeof(virPoolObjTable),
                                             virPoolObjTableDispose)))
        return -1;

    return 0;
}


VIR_ONCE_GLOBAL_INIT(virPoolObjTable)

/* virPoolObjTableNew:
 * @type: The virPoolObjTableType type of hash table
 * @hashStart: Initial size of the table
 * @nameOnly: Whether the table will only have names
 *
 * Allocate the hash table object and initialize the hash table(s).
 * At least a "Name" hash table is always allocated, but there may
 * also be a "UUID" hash table as well based on the @nameOnly argument
 *
 * Returns: The hash table on success, NULL on failure
 */
virPoolObjTablePtr
virPoolObjTableNew(virPoolObjTableType type,
                   int hashStart,
                   bool nameOnly)
{
    virPoolObjTablePtr poolobjs;

    if (virPoolObjTableInitialize() < 0)
        return NULL;

    if (!(poolobjs = virObjectLockableNew(virPoolObjTableClass)))
        return NULL;

    VIR_DEBUG("objtable=%p type=%s",
              poolobjs, virPoolObjTableTypeToString(type));
    poolobjs->type = type;
    poolobjs->hashStart = hashStart;
    poolobjs->nameOnly = nameOnly;

    if (!nameOnly &&
        !(poolobjs->objsUuid = virHashCreate(hashStart, virObjectFreeHashData)))
        goto error;

    if (!(poolobjs->objsName = virHashCreate(hashStart, virObjectFreeHashData)))
        goto error;

    return poolobjs;

 error:
    virObjectUnref(poolobjs);
    return NULL;
}


static void
virPoolObjTableDispose(void *obj)
{
    virPoolObjTablePtr poolobjs = obj;

    if (!poolobjs)
        return;

    VIR_DEBUG("objtable=%p type=%s",
              poolobjs, virPoolObjTableTypeToString(poolobjs->type));

    if (!poolobjs->nameOnly)
        virHashFree(poolobjs->objsUuid);
    virHashFree(poolobjs->objsName);
}


/* virPoolObjTableAdd[Locked]
 * @poolobjs: Pool object table
 * @new_uuidstr: Optional uuid by formatted string (offers more uniqueness)
 * @new_name: Required name for uniqueness
 * @def: Pointer to a void Object definition
 * @newDef: Pointer to the "next" void Object definition
 * @defFreeFunc: Callback to free the def/newDef
 * @assignDefFunc: Callback to assign def logic (def/newDef manipulation)
 * @assignFlags: Flags to pass to @assignDefFunc
 *
 * Search for the @new_uuidstr/@new_name keys in @poolobjs and either
 * update the definition via the @assignDefFunc or add the @def and
 * @newDef to @poolobjs.
 *
 * The @defFreeFunc is a reference to the vir*DefFree function for
 * the type of pool
 *
 * The optional @assignDefFunc is a callback function that will
 * handle the management of @def, @newDef, and @oldDef long with
 * an @assignFlags option depending on how/where it's called.
 *
 * Returns: A locked pool object with incremented ref or NULL
 *          Caller must use virPoolObjEndAPI when done.
 */
static virPoolObjPtr
virPoolObjTableAddLocked(virPoolObjTablePtr poolobjs,
                         const char *new_uuidstr,
                         const char *new_name,
                         void *def,
                         void *newDef,
                         void *oldDef,
                         virFreeCallback defFreeFunc,
                         virPoolObjTableAssignDefFunc assignDefFunc,
                         unsigned int assignFlags)
{
    virPoolDefPtr pooldef = NULL;
    virPoolObjPtr obj = NULL;

    VIR_DEBUG("poolobjs=%p uuidstr=%s name=%s",
              poolobjs, NULLSTR(new_uuidstr), new_name);

    /* UUID is the primary search, although if the name is sufficiently
     * unique (such as for node devices and interfaces), then name will
     * be the primary */
    if (poolobjs->nameOnly)
        obj = virHashLookup(poolobjs->objsName, new_name);
    else
        obj = virHashLookup(poolobjs->objsUuid, new_uuidstr);

    if (obj) {
        virObjectLock(obj);

        /* If used UUID for Lookup, ensure name matches too */
        if (new_uuidstr && STRNEQ(obj->pooldef->name, new_name)) {
            virReportError(VIR_ERR_OPERATION_FAILED,
                           _("%s '%s' is already defined with uuid %s"),
                           virPoolObjTableTypeToString(poolobjs->type),
                           obj->pooldef->name, obj->pooldef->uuid);
            virObjectUnlock(obj);
            return NULL;
        }

        if (assignDefFunc) {
            if (assignDefFunc(obj, def, oldDef, assignFlags) < 0) {
                virObjectUnlock(obj);
                return NULL;
            }
        } else {
            /* Simple replacement */
            defFreeFunc(obj->def);
            obj->def = def;
        }
    } else {
        /* Ensure that something by the same name doesn't already exist */
        if (!new_uuidstr &&
            (obj = virHashLookup(poolobjs->objsName, new_name))) {
            virObjectLock(obj);
            virReportError(VIR_ERR_OPERATION_FAILED,
                           _("%s '%s' already exists with uuid %s"),
                           virPoolObjTableTypeToString(poolobjs->type),
                           new_name, obj->pooldef->uuid);
            virObjectUnlock(obj);
            return NULL;
        }

        if (VIR_ALLOC(pooldef) < 0)
            return NULL;

        if (VIR_STRDUP(pooldef->uuid, new_uuidstr) < 0 ||
            VIR_STRDUP(pooldef->name, new_name) < 0)
            goto error;

        if (!(obj = virPoolObjNew(pooldef, def, newDef, defFreeFunc)))
            goto error;

        virObjectLock(obj);

        if (!poolobjs->nameOnly &&
            virHashAddEntry(poolobjs->objsUuid, new_uuidstr, obj) < 0)
            goto error;

        if (virHashAddEntry(poolobjs->objsName, new_name, obj) < 0) {
            if (!poolobjs->nameOnly)
                virHashRemoveEntry(poolobjs->objsUuid, new_uuidstr);
            goto error;
        }

        /* If it's in two hash tables, increment ref count! */
        if (new_uuidstr)
            virObjectRef(obj);
    }

    virObjectRef(obj);
    return obj;

 error:
    VIR_DEBUG("Fail to add object to table");
    virPoolDefFree(pooldef);
    virObjectUnref(obj);
    virObjectUnlock(obj);
    return NULL;
}


virPoolObjPtr
virPoolObjTableAdd(virPoolObjTablePtr poolobjs,
                   const char *new_uuidstr,
                   const char *new_name,
                   void *def,
                   void *newDef,
                   void *oldDef,
                   virFreeCallback defFreeFunc,
                   virPoolObjTableAssignDefFunc assignDefFunc,
                   unsigned int assignFlags)
{
    virPoolObjPtr ret;

    virObjectLock(poolobjs);
    ret = virPoolObjTableAddLocked(poolobjs, new_uuidstr, new_name,
                                   def, newDef, oldDef, defFreeFunc,
                                   assignDefFunc, assignFlags);
    VIR_DEBUG("poolobjs=%p, ret=%p", poolobjs, ret);
    virObjectUnlock(poolobjs);
    return ret;
}


/*
 * virPoolObjTableClear
 * @poolobjs: Pool object table
 *
 * Remove all the objects from @poolobjs, but don't free the table.
 * Useful for @poolobjs tables that can repopulate themselves and don't
 * want to determine the difference between two objects.
 */
void
virPoolObjTableClearAll(virPoolObjTablePtr poolobjs)
{
    ssize_t count;
    virObjectLock(poolobjs);
    if (!poolobjs->nameOnly) {
        count = virHashRemoveAll(poolobjs->objsUuid);
        VIR_DEBUG("cleared out %ld objects from objsUuid", count);
    }

    count = virHashRemoveAll(poolobjs->objsName);
    VIR_DEBUG("cleared out %ld objects from objsName", count);
    virObjectUnlock(poolobjs);
}


/*
 * virPoolObjTableRemove
 * @poolobjs: Pool object table
 * @obj: Object to remove
 *
 * Remove the object from the table.
 *
 * The caller must hold a lock on the driver owning @poolobjs and
 * must have locked @obj to ensure no one else is either waiting for
 * @obj or still using it. The caller can also take out an extra
 * reference on @obj to ensure it doesn't disappear if it needs it
 * beyond this removal.
 *
 * Upon exit, the lock held upon entry is removed.
 */
void
virPoolObjTableRemove(virPoolObjTablePtr poolobjs,
                      virPoolObjPtr *obj)
{
    virPoolDefPtr pooldef = (*obj)->pooldef;

    VIR_DEBUG("poolobjs=%p, obj=%p", poolobjs, obj);

    (*obj)->removing = true;
    virObjectRef(*obj);
    virObjectUnlock(*obj);

    virObjectLock(poolobjs);
    virObjectLock(*obj);
    if (!poolobjs->nameOnly)
        virHashRemoveEntry(poolobjs->objsUuid, pooldef->uuid);
    virHashRemoveEntry(poolobjs->objsName, pooldef->name);
    virObjectUnlock(*obj);
    virObjectUnref(*obj);
    virObjectUnlock(poolobjs);
}


/**
 * virPoolObjFindByUUIDInternal:
 * @poolobjs: Pool object table
 * @uuid: pool uuid to find
 *
 * Lock and search the pool for the UUID. Depending on 'ref' argument
 * we either return with referenced count incremented on the object too
 *
 * Returns: pool object
 */
static virPoolObjPtr
virPoolObjTableFindByUUIDInternal(virPoolObjTablePtr poolobjs,
                                  const unsigned char *uuid,
                                  bool ref)
{
    char uuidstr[VIR_UUID_STRING_BUFLEN];
    virPoolObjPtr obj;

    virObjectLock(poolobjs);
    if (poolobjs->nameOnly) {
        virObjectUnlock(poolobjs);
        return NULL;
    }

    virUUIDFormat(uuid, uuidstr);

    obj = virHashLookup(poolobjs->objsUuid, uuidstr);
    if (ref) {
        virObjectRef(obj);
        virObjectUnlock(poolobjs);
    }
    if (obj) {
        virObjectLock(obj);
        if (obj->removing) {
            virObjectUnlock(obj);
            if (ref)
                virObjectUnref(obj);
            obj = NULL;
        }
    }
    if (!ref)
        virObjectUnlock(poolobjs);
    return obj;
}


/**
 * virPoolObjFindByUUID:
 * @poolobjs: Pool object table
 * @uuid: pool uuid to find
 *
 * Returns: Locked pool object.
 */
virPoolObjPtr
virPoolObjTableFindByUUID(virPoolObjTablePtr poolobjs,
                          const unsigned char *uuid)
{
    return virPoolObjTableFindByUUIDInternal(poolobjs, uuid, false);
}


/**
 * virPoolObjFindByUUIDUnlocked:
 * @poolobjs: Pool object table
 * @uuid: pool uuid to find
 *
 * Returns: Locked and ref count incremented pool object.
 */
virPoolObjPtr
virPoolObjTableFindByUUIDRef(virPoolObjTablePtr poolobjs,
                             const unsigned char *uuid)
{
    return virPoolObjTableFindByUUIDInternal(poolobjs, uuid, true);
}


virPoolObjPtr
virPoolObjTableFindByName(virPoolObjTablePtr poolobjs,
                          const char *name)
{
    virPoolObjPtr obj;

    virObjectLock(poolobjs);
    obj = virHashLookup(poolobjs->objsName, name);
    virObjectRef(obj);
    virObjectUnlock(poolobjs);
    if (obj) {
        virObjectLock(obj);
        if (obj->removing) {
            virObjectUnlock(obj);
            virObjectUnref(obj);
            obj = NULL;
        }
    }
    return obj;
}


static int
virPoolObjTableSearchCallback(const void *payload,
                              const void *name ATTRIBUTE_UNUSED,
                              const void *opaque)
{
    virPoolObjPtr obj = (virPoolObjPtr) payload;
    struct virPoolObjTableSearchIterData *data =
        (struct virPoolObjTableSearchIterData *) opaque;
    int found = 0;

    virObjectLock(obj);

    if (data->callback(obj, data->opaque) == 1)
        found = 1;

    virObjectUnlock(obj);
    return found;
}


/* virPoolObjTableSearchInternal
 * @poolobjs: Pool object table
 * @callback: Consumer specific callback function for search
 * @opaque: Consumer specific data to the callback function
 * @ref: Whether to place a reference on the object or not
 *
 * Search through the poolobj table by name, calling the consumer
 * specific callback with the obj and consumer specific data structure.
 * If the consumer determines there's a match, it will return true
 * and the cause the search to complete
 *
 * Returns virPoolObjPtr if data is found, NULL if not. The returned
 * object will be locked and can have it reference count incremented.
 * If the reference count is incremented, use the virPoolObjEndAPI on
 * the object; otherwise, use virObjectUnlock to release the object lock.
 */
static virPoolObjPtr
virPoolObjTableSearchInternal(virPoolObjTablePtr poolobjs,
                              virPoolObjTableSearchIterator callback,
                              void *opaque,
                              bool ref)
{
    virPoolObjPtr ret;
    struct virPoolObjTableSearchIterData data = { .callback = callback,
                                                  .opaque = opaque };

    virObjectLock(poolobjs);
    if (poolobjs->nameOnly)
        ret = virHashSearch(poolobjs->objsName, virPoolObjTableSearchCallback,
                            &data);
    else
        ret = virHashSearch(poolobjs->objsUuid, virPoolObjTableSearchCallback,
                            &data);
    if (ref) {
        virObjectRef(ret);
        virObjectUnlock(poolobjs);
    }
    if (ret) {
        virObjectLock(ret);
        if (ret->removing) {
            virObjectUnlock(ret);
            if (ref)
                virObjectUnref(ret);
            ret = NULL;
        }
    }
    if (!ref)
        virObjectUnlock(poolobjs);

    return ret;
}


virPoolObjPtr
virPoolObjTableSearch(virPoolObjTablePtr poolobjs,
                      virPoolObjTableSearchIterator callback,
                      void *opaque)
{
    return virPoolObjTableSearchInternal(poolobjs, callback, opaque, false);
}


virPoolObjPtr
virPoolObjTableSearchRef(virPoolObjTablePtr poolobjs,
                         virPoolObjTableSearchIterator callback,
                         void *opaque)
{
    return virPoolObjTableSearchInternal(poolobjs, callback, opaque, true);
}


struct iterateData {
    virPoolObjTableIteratorCallback callback;
    void *opaque;
};

static int
virPoolObjTableIterator(void *payload,
                        const void *name ATTRIBUTE_UNUSED,
                        void *opaque)
{
    virPoolObjPtr obj = payload;
    struct iterateData *data = opaque;

    virObjectLock(obj);
    data->callback(obj, data->opaque);
    virObjectUnlock(obj);

    return 0;
}


/* virPoolObjTableIterate:
 * @poolobjs: Source table to perform iteration
 * @callback: Function to call
 * @opaque: Opaque data for callback
 *
 * Just iterate through each entry in @poolobjs calling the
 * void function with the locked pool object
 *
 * Returns: void
 */
void
virPoolObjTableIterate(virPoolObjTablePtr poolobjs,
                       virPoolObjTableIteratorCallback callback,
                       void *opaque)
{
    struct iterateData data = { .callback = callback,
                                .opaque = opaque };
    virObjectLock(poolobjs);
    if (poolobjs->nameOnly)
        virHashForEach(poolobjs->objsName, virPoolObjTableIterator, &data);
    else
        virHashForEach(poolobjs->objsUuid, virPoolObjTableIterator, &data);
    virObjectUnlock(poolobjs);
}


struct collectData {
    virPoolObjPtr *objs;
    size_t nobjs;
};

static int
virPoolObjTableCollectIterator(void *payload,
                               const void *name ATTRIBUTE_UNUSED,
                               void *opaque)
{
    struct collectData *data = opaque;

    data->objs[data->nobjs++] = virObjectRef(payload);
    return 0;
}


static void
virPoolObjTableFilter(virPoolObjPtr **list,
                      size_t *nobjs,
                      virConnectPtr conn,
                      virPoolObjACLFilter aclfilter,
                      virPoolObjMatchFilter matchfilter,
                      unsigned int flags)
{
    size_t i = 0;

    while (i < *nobjs) {
        virPoolObjPtr obj = (*list)[i];

        virObjectLock(obj);

        /* do not list the object if:
         * 1) it's being removed.
         * 2) connection does not have ACL to see it
         * 3) it doesn't match the filter
         */
        if (obj->removing ||
            (aclfilter && !aclfilter(conn, obj->def)) ||
            (matchfilter && !matchfilter(obj, flags))) {
            virObjectUnlock(obj);
            virObjectUnref(obj);
            VIR_DELETE_ELEMENT(*list, i, *nobjs);
            continue;
        }

        virObjectUnlock(obj);
        i++;
    }
}


/* virPoolObjTableCollect:
 * @poolobjs: Pool object table
 * @conn: Pointer to connection
 * @objs: Pointer to a list of objects to return
 * @nobjs: Count of objects
 * @aclfilter: Filter results based on ACL
 * @matchfilter: Possible consumer callback to futher "match" the data
 * @flags: Consumer specific flags
 *
 * Create a list of poolobjs that match both the ACL filter and the consumer
 * provided match filter and return them and the count to the caller. Each
 * of the returned object is returned with an incremented ref count. When
 * the caller is done with the list, each object in the returned @objs list
 * should be run through virObjectListFreeCount to Unref the object.
 *
 * Returns 0 on success, -1 on failure
 */
int
virPoolObjTableCollect(virPoolObjTablePtr poolobjs,
                       virConnectPtr conn,
                       virPoolObjPtr **objs,
                       size_t *nobjs,
                       virPoolObjACLFilter aclfilter,
                       virPoolObjMatchFilter matchfilter,
                       unsigned int flags)
{
    struct collectData data = { NULL, 0 };

    virObjectLock(poolobjs);
    if (poolobjs->nameOnly) {
        if (VIR_ALLOC_N(data.objs, virHashSize(poolobjs->objsName)) < 0) {
            virObjectUnlock(poolobjs);
            return -1;
        }
        virHashForEach(poolobjs->objsName, virPoolObjTableCollectIterator,
                       &data);
    } else {
        if (VIR_ALLOC_N(data.objs, virHashSize(poolobjs->objsUuid)) < 0) {
            virObjectUnlock(poolobjs);
            return -1;
        }
        virHashForEach(poolobjs->objsUuid, virPoolObjTableCollectIterator,
                       &data);
    }
    virObjectUnlock(poolobjs);

    virPoolObjTableFilter(&data.objs, &data.nobjs, conn,
                          aclfilter, matchfilter, flags);

    *nobjs = data.nobjs;
    *objs = data.objs;

    return 0;
}


static int
virPoolObjTableListIterator(void *payload,
                            const void *name ATTRIBUTE_UNUSED,
                            void *opaque)
{
    virPoolObjPtr obj = payload;
    struct virPoolObjTableListIterData *data = opaque;

    virObjectLock(obj);
    if (data->aclfilter && !data->aclfilter(data->conn, obj->def))
        goto cleanup;

    if (data->callback && data->callback(obj, data->opaque) < 0)
        data->ret = -1;

 cleanup:
    virObjectUnlock(obj);
    return 0;
}


/* virPoolObjTableList:
 * @poolobjs: Pool object table
 * @conn: Pointer to connection
 * @aclfilter: Filter results based on ACL
 * @callback: Possible consumer callback handle @opaque data
 * @opaque: Consumer opaque data
 *
 * Similar to Collect, except allowing the consumer to build up it's
 * @opaque data based on consumer @callback defined rules.
 *
 * Returns 0 on success, -1 on failure
 */
int
virPoolObjTableList(virPoolObjTablePtr poolobjs,
                    virConnectPtr conn,
                    virPoolObjACLFilter aclfilter,
                    virPoolObjTableListCallback callback,
                    void *opaque)
{
    struct virPoolObjTableListIterData data = { .conn = conn,
                                                .aclfilter = aclfilter,
                                                .callback = callback,
                                                .opaque = opaque,
                                                .ret = 0 };

    virObjectLock(poolobjs);
    if (poolobjs->nameOnly)
        virHashForEach(poolobjs->objsName, virPoolObjTableListIterator, &data);
    else
        virHashForEach(poolobjs->objsUuid, virPoolObjTableListIterator, &data);
    virObjectUnlock(poolobjs);

    return data.ret;
}


struct cloneData {
    virPoolObjTableCloneCallback callback;
    int ret;
    virPoolObjTablePtr dst;
};

static int
virPoolObjTableCloneIterator(void *payload,
                             const void *name ATTRIBUTE_UNUSED,
                             void *opaque)
{
    virPoolObjPtr src = payload;
    virPoolObjPtr dst = NULL;
    struct cloneData *data = opaque;
    void *def;
    int ret = -1;

    virObjectLock(src);

    if (!(def = data->callback(src)))
        goto error;

    if (!(dst = virPoolObjTableAdd(data->dst, src->pooldef->uuid,
                                   src->pooldef->name, def, NULL, NULL,
                                   src->defFreeFunc, NULL, 0)))
        goto error;
    def = NULL;

    ret = 0;

 cleanup:
    src->defFreeFunc(def);
    virObjectUnlock(dst);
    virObjectUnlock(src);
    return ret;

 error:
    data->ret = -1;
    goto cleanup;
}


/* virPoolObjTableClone:
 * @src: Source table to perform a deep copy on
 * @callback: Callback to create @def to be added into the cloned table.
 *
 * Make a deep copy of the object table including all objects and
 * all defs within each hash table. This function does not use the
 * assignFlags for the virPoolObjTableAdd[Locked] since generation
 * of the cloned objects would be all considered new rather than
 * assigning or redefining a definition.
 *
 * Returns: Cloned table or NULL
 */
virPoolObjTablePtr
virPoolObjTableClone(virPoolObjTablePtr src,
                     virPoolObjTableCloneCallback callback)
{
    struct cloneData data = { .callback = callback,
                              .ret = 0 };

    virObjectLock(src);
    if (!(data.dst = virPoolObjTableNew(src->type, src->hashStart,
                                        src->nameOnly)))
        goto cleanup;

    /* Clone both the UUID and Name table in the result */
    if (!src->nameOnly)
        virHashForEach(src->objsUuid, virPoolObjTableCloneIterator, &data);

    virHashForEach(src->objsName, virPoolObjTableCloneIterator, &data);

    if (data.ret < 0) {
        virObjectUnref(data.dst);
        data.dst = NULL;
    }

 cleanup:
    virObjectUnlock(src);
    return data.dst;
}


struct pruneData {
    unsigned int flags;
    virPoolObjMatchFilter matchfilter;
};

static int
pruneList(const void *payload,
          const void *name ATTRIBUTE_UNUSED,
          const void *opaque)
{
    virPoolObjPtr obj = (virPoolObjPtr) payload;
    const struct pruneData *data = opaque;
    int want = 0;

    virObjectLock(obj);
    want = data->matchfilter(obj, data->flags);
    virObjectUnlock(obj);
    return want;
}


/**
 * virPoolObjTablePrune:
 * @poolobjs: Pool objects table
 * @flags: bitwise-OR of flags to be prunedvirConnectListAllNetworksFlags
 *
 * Iterate over list of objects and remove the desired ones from the
 * name and uuid (if exists) table
 */
void
virPoolObjTablePrune(virPoolObjTablePtr poolobjs,
                     virPoolObjMatchFilter matchfilter,
                     unsigned int flags)
{
    struct pruneData data = { .flags = flags,
                              .matchfilter = matchfilter };

    virObjectLock(poolobjs);
    if (!poolobjs->nameOnly)
        virHashRemoveSet(poolobjs->objsUuid, pruneList, &data);
    virHashRemoveSet(poolobjs->objsName, pruneList, &data);
    virObjectUnlock(poolobjs);
}
