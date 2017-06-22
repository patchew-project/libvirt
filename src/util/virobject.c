/*
 * virobject.c: libvirt reference counted object
 *
 * Copyright (C) 2012-2014 Red Hat, Inc.
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
 */

#include <config.h>

#define VIR_PARENT_REQUIRED /* empty, to allow virObject to have no parent */
#include "virobject.h"
#include "virthread.h"
#include "viralloc.h"
#include "viratomic.h"
#include "virerror.h"
#include "virlog.h"
#include "virprobe.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("util.object");

static unsigned int magicCounter = 0xCAFE0000;

struct _virClass {
    virClassPtr parent;

    unsigned int magic;
    char *name;
    size_t objectSize;

    virObjectDisposeCallback dispose;
};

#define VIR_OBJECT_NOTVALID(obj) (!obj || ((obj->u.s.magic & 0xCAFE0000) != 0xCAFE0000))

#define VIR_OBJECT_USAGE_PRINT_WARNING(anyobj, objclass)                    \
    do {                                                                    \
        virObjectPtr obj = anyobj;                                          \
        if (VIR_OBJECT_NOTVALID(obj)) {                                     \
            if (!obj)                                                       \
                VIR_WARN("Object cannot be NULL");                          \
            else                                                            \
                VIR_WARN("Object %p has a bad magic number %X",             \
                         obj, obj->u.s.magic);                              \
        } else {                                                            \
            VIR_WARN("Object %p (%s) is not a %s instance",                 \
                      anyobj, obj->klass->name, #objclass);                 \
        }                                                                   \
    } while (0)

static virClassPtr virObjectClass;
static virClassPtr virObjectLockableClass;
static virClassPtr virObjectLookupKeysClass;
static virClassPtr virObjectLookupHashClass;

static void virObjectLockableDispose(void *anyobj);
static void virObjectLookupKeysDispose(void *anyobj);
static void virObjectLookupHashDispose(void *anyobj);

static int
virObjectOnceInit(void)
{
    if (!(virObjectClass = virClassNew(NULL,
                                       "virObject",
                                       sizeof(virObject),
                                       NULL)))
        return -1;

    if (!(virObjectLockableClass = virClassNew(virObjectClass,
                                               "virObjectLockable",
                                               sizeof(virObjectLockable),
                                               virObjectLockableDispose)))
        return -1;

    if (!(virObjectLookupKeysClass = virClassNew(virObjectLockableClass,
                                                 "virObjectLookupKeys",
                                                 sizeof(virObjectLookupKeys),
                                                 virObjectLookupKeysDispose)))
        return -1;

    if (!(virObjectLookupHashClass = virClassNew(virObjectLockableClass,
                                                 "virObjectLookupHash",
                                                 sizeof(virObjectLookupHash),
                                                 virObjectLookupHashDispose)))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(virObject);


/**
 * virClassForObject:
 *
 * Returns the class instance for the base virObject type
 */
virClassPtr
virClassForObject(void)
{
    if (virObjectInitialize() < 0)
        return NULL;

    return virObjectClass;
}


/**
 * virClassForObjectLockable:
 *
 * Returns the class instance for the virObjectLockable type
 */
virClassPtr
virClassForObjectLockable(void)
{
    if (virObjectInitialize() < 0)
        return NULL;

    return virObjectLockableClass;
}


/**
 * virClassForObjectLookupKeys:
 *
 * Returns the class instance for the virObjectLookupKeys type
 */
virClassPtr
virClassForObjectLookupKeys(void)
{
    if (virObjectInitialize() < 0)
        return NULL;

    return virObjectLookupKeysClass;
}


/**
 * virClassForObjectLookupHash:
 *
 * Returns the class instance for the virObjectLookupHash type
 */
virClassPtr
virClassForObjectLookupHash(void)
{
    if (virObjectInitialize() < 0)
        return NULL;

    return virObjectLookupHashClass;
}


/**
 * virClassNew:
 * @parent: the parent class
 * @name: the class name
 * @objectSize: total size of the object struct
 * @dispose: callback to run to free object fields
 *
 * Register a new object class with @name. The @objectSize
 * should give the total size of the object struct, which
 * is expected to have a 'virObject object;' field as its
 * first member. When the last reference on the object is
 * released, the @dispose callback will be invoked to free
 * memory of the object fields
 *
 * Returns a new class instance
 */
virClassPtr
virClassNew(virClassPtr parent,
            const char *name,
            size_t objectSize,
            virObjectDisposeCallback dispose)
{
    virClassPtr klass;

    if (parent == NULL &&
        STRNEQ(name, "virObject")) {
        virReportInvalidNonNullArg(parent);
        return NULL;
    } else if (parent &&
               objectSize <= parent->objectSize) {
        virReportInvalidArg(objectSize,
                            _("object size %zu of %s is smaller than parent class %zu"),
                            objectSize, name, parent->objectSize);
        return NULL;
    }

    if (VIR_ALLOC(klass) < 0)
        goto error;

    klass->parent = parent;
    klass->magic = virAtomicIntInc(&magicCounter);
    if (klass->magic > 0xCAFEFFFF) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("too many object classes defined"));
        goto error;
    }
    if (VIR_STRDUP(klass->name, name) < 0)
        goto error;
    klass->objectSize = objectSize;
    klass->dispose = dispose;

    return klass;

 error:
    VIR_FREE(klass);
    return NULL;
}


/**
 * virClassIsDerivedFrom:
 * @klass: the klass to check
 * @parent: the possible parent class
 *
 * Determine if @klass is derived from @parent
 *
 * Return true if @klass is derived from @parent, false otherwise
 */
bool
virClassIsDerivedFrom(virClassPtr klass,
                      virClassPtr parent)
{
    while (klass) {
        if (klass->magic == parent->magic)
            return true;
        klass = klass->parent;
    }
    return false;
}


/**
 * virObjectNew:
 * @klass: the klass of object to create
 *
 * Allocates a new object of type @klass. The returned
 * object will be an instance of "virObjectPtr", which
 * can be cast to the struct associated with @klass.
 *
 * The initial reference count of the object will be 1.
 *
 * Returns the new object
 */
void *
virObjectNew(virClassPtr klass)
{
    virObjectPtr obj = NULL;

    if (VIR_ALLOC_VAR(obj,
                      char,
                      klass->objectSize - sizeof(virObject)) < 0)
        return NULL;

    obj->u.s.magic = klass->magic;
    obj->klass = klass;
    virAtomicIntSet(&obj->u.s.refs, 1);

    PROBE(OBJECT_NEW, "obj=%p classname=%s", obj, obj->klass->name);

    return obj;
}


void *
virObjectLockableNew(virClassPtr klass)
{
    virObjectLockablePtr obj;

    if (!virClassIsDerivedFrom(klass, virClassForObjectLockable())) {
        virReportInvalidArg(klass,
                            _("Class %s must derive from virObjectLockable"),
                            virClassName(klass));
        return NULL;
    }

    if (!(obj = virObjectNew(klass)))
        return NULL;

    if (virMutexInit(&obj->lock) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Unable to initialize mutex"));
        virObjectUnref(obj);
        return NULL;
    }

    return obj;
}


static void
virObjectLockableDispose(void *anyobj)
{
    virObjectLockablePtr obj = anyobj;

    virMutexDestroy(&obj->lock);
}


void *
virObjectLookupKeysNew(virClassPtr klass,
                       const char *uuid,
                       const char *name)
{
    virObjectLookupKeysPtr obj;

    if (!virClassIsDerivedFrom(klass, virClassForObjectLookupKeys())) {
        virReportInvalidArg(klass,
                            _("Class %s must derive from virObjectLookupKeys"),
                            virClassName(klass));
        return NULL;
    }

    if (!uuid && !name) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("no key, either 'uuid' or 'name' must be defined"));
        return NULL;
    }

    if (!(obj = virObjectLockableNew(klass)))
        return NULL;

    if (VIR_STRDUP(obj->uuid, uuid) < 0)
        goto error;

    if (VIR_STRDUP(obj->name, name) < 0)
        goto error;

    VIR_DEBUG("obj=%p, uuid=%s name=%s",
              obj, NULLSTR(obj->uuid), NULLSTR(obj->name));

    return obj;

 error:
    virObjectUnref(obj);
    return NULL;
}


static void
virObjectLookupKeysDispose(void *anyobj)
{
    virObjectLookupKeysPtr obj = anyobj;

    VIR_DEBUG("dispose obj=%p", obj);

    VIR_FREE(obj->uuid);
    VIR_FREE(obj->name);
}


/**
 * virObjectLookupHashNew:
 * @klass: the klass to check
 * @tableElemsStart: number of elements initially in the table
 *
 * Create a new poolable hash table object for storing hash tables capable
 * of storing virObjectLookupKeys objects.
 *
 * Returns: New object on success, NULL on failure w/ error message set
 */
void *
virObjectLookupHashNew(virClassPtr klass,
                       int tableElemsStart)
{
    virObjectLookupHashPtr obj;

    if (!virClassIsDerivedFrom(klass, virClassForObjectLookupHash())) {
        virReportInvalidArg(klass,
                            _("Class %s must derive from virObjectLookupHash"),
                            virClassName(klass));
        return NULL;
    }

    if (!(obj = virObjectLockableNew(klass)))
        return NULL;

    obj->tableElemsStart = tableElemsStart;

    if (!(obj->objsUUID = virHashCreate(tableElemsStart,
                                        virObjectFreeHashData)))
        goto error;

    if (!(obj->objsName = virHashCreate(tableElemsStart,
                                        virObjectFreeHashData)))
        goto error;

    VIR_DEBUG("obj=%p, elems=%d objsUUID=%p objsName=%p",
              obj, tableElemsStart, obj->objsUUID, obj->objsName);

    return obj;

 error:
    virObjectUnref(obj);
    return NULL;
}


static void
virObjectLookupHashDispose(void *anyobj)
{
    virObjectLookupHashPtr obj = anyobj;

    VIR_DEBUG("dispose obj=%p", obj);

    virHashFree(obj->objsUUID);
    virHashFree(obj->objsName);
}


/**
 * virObjectUnref:
 * @anyobj: any instance of virObjectPtr
 *
 * Decrement the reference count on @anyobj and if
 * it hits zero, runs the "dispose" callback associated
 * with the object class and frees @anyobj.
 *
 * Returns true if the remaining reference count is
 * non-zero, false if the object was disposed of
 */
bool
virObjectUnref(void *anyobj)
{
    virObjectPtr obj = anyobj;

    if (VIR_OBJECT_NOTVALID(obj))
        return false;

    bool lastRef = virAtomicIntDecAndTest(&obj->u.s.refs);
    PROBE(OBJECT_UNREF, "obj=%p", obj);
    if (lastRef) {
        PROBE(OBJECT_DISPOSE, "obj=%p", obj);
        virClassPtr klass = obj->klass;
        while (klass) {
            if (klass->dispose)
                klass->dispose(obj);
            klass = klass->parent;
        }

        /* Clear & poison object */
        memset(obj, 0, obj->klass->objectSize);
        obj->u.s.magic = 0xDEADBEEF;
        obj->klass = (void*)0xDEADBEEF;
        VIR_FREE(obj);
    }

    return !lastRef;
}


/**
 * virObjectRef:
 * @anyobj: any instance of virObjectPtr
 *
 * Increment the reference count on @anyobj and return
 * the same pointer
 *
 * Returns @anyobj
 */
void *
virObjectRef(void *anyobj)
{
    virObjectPtr obj = anyobj;

    if (VIR_OBJECT_NOTVALID(obj))
        return NULL;
    virAtomicIntInc(&obj->u.s.refs);
    PROBE(OBJECT_REF, "obj=%p", obj);
    return anyobj;
}


static virObjectLockablePtr
virObjectGetLockableObj(void *anyobj)
{
    if (virObjectIsClass(anyobj, virObjectLockableClass) ||
        virObjectIsClass(anyobj, virObjectLookupKeysClass) ||
        virObjectIsClass(anyobj, virObjectLookupHashClass))
        return anyobj;

    VIR_OBJECT_USAGE_PRINT_WARNING(anyobj, virObjectLockableClass);

    return NULL;
}


static virObjectLookupKeysPtr
virObjectGetLookupKeysObj(void *anyobj)
{
    if (virObjectIsClass(anyobj, virObjectLookupKeysClass))
        return anyobj;

    VIR_OBJECT_USAGE_PRINT_WARNING(anyobj, virObjectLookupKeysClass);

    return NULL;
}


static virObjectLookupHashPtr
virObjectGetLookupHashObj(void *anyobj)
{
    if (virObjectIsClass(anyobj, virObjectLookupHashClass))
        return anyobj;

    VIR_OBJECT_USAGE_PRINT_WARNING(anyobj, virObjectLookupHashClass);

    return NULL;
}


/**
 * virObjectLock:
 * @anyobj: any instance of virObjectLockablePtr
 *
 * Acquire a lock on @anyobj. The lock must be
 * released by virObjectUnlock.
 *
 * The caller is expected to have acquired a reference
 * on the object before locking it (eg virObjectRef).
 * The object must be unlocked before releasing this
 * reference.
 */
void
virObjectLock(void *anyobj)
{
    virObjectLockablePtr obj = virObjectGetLockableObj(anyobj);

    if (!obj)
        return;

    virMutexLock(&obj->lock);
}


/**
 * virObjectUnlock:
 * @anyobj: any instance of virObjectLockablePtr
 *
 * Release a lock on @anyobj. The lock must have been
 * acquired by virObjectLock.
 */
void
virObjectUnlock(void *anyobj)
{
    virObjectLockablePtr obj = virObjectGetLockableObj(anyobj);

    if (!obj)
        return;

    virMutexUnlock(&obj->lock);
}


/**
 * virObjectIsClass:
 * @anyobj: any instance of virObjectPtr
 * @klass: the class to check
 *
 * Checks whether @anyobj is an instance of
 * @klass
 *
 * Returns true if @anyobj is an instance of @klass
 */
bool
virObjectIsClass(void *anyobj,
                 virClassPtr klass)
{
    virObjectPtr obj = anyobj;
    if (VIR_OBJECT_NOTVALID(obj))
        return false;

    return virClassIsDerivedFrom(obj->klass, klass);
}


/**
 * virClassName:
 * @klass: the object class
 *
 * Returns the name of @klass
 */
const char *
virClassName(virClassPtr klass)
{
    return klass->name;
}


/**
 * virObjectFreeCallback:
 * @opaque: a pointer to a virObject instance
 *
 * Provides identical functionality to virObjectUnref,
 * but with the signature matching the virFreeCallback
 * typedef.
 */
void virObjectFreeCallback(void *opaque)
{
    virObjectUnref(opaque);
}


/**
 * virObjectFreeHashData:
 * @opaque: a pointer to a virObject instance
 * @name: ignored, name of the hash key being deleted
 *
 * Provides identical functionality to virObjectUnref,
 * but with the signature matching the virHashDataFree
 * typedef.
 */
void
virObjectFreeHashData(void *opaque,
                      const void *name ATTRIBUTE_UNUSED)
{
    virObjectUnref(opaque);
}


/**
 * virObjectListFree:
 * @list: A pointer to a NULL-terminated list of object pointers to free
 *
 * Unrefs all members of @list and frees the list itself.
 */
void
virObjectListFree(void *list)
{
    void **next;

    if (!list)
        return;

    for (next = (void **) list; *next; next++)
        virObjectUnref(*next);

    VIR_FREE(list);
}


/**
 * virObjectListFreeCount:
 * @list: A pointer to a list of object pointers to freea
 * @count: Number of elements in the list.
 *
 * Unrefs all members of @list and frees the list itself.
 */
void
virObjectListFreeCount(void *list,
                       size_t count)
{
    size_t i;

    if (!list)
        return;

    for (i = 0; i < count; i++)
        virObjectUnref(((void **)list)[i]);

    VIR_FREE(list);
}


/**
 * virObjectLookupKeysIsActive
 * @anyobj: Pointer to a locked LookupKeys object
 *
 * Returns: True if object is active, false if not
 */
bool
virObjectLookupKeysIsActive(void *anyobj)
{
    virObjectLookupKeysPtr obj = virObjectGetLookupKeysObj(anyobj);

    if (!obj)
        return false;

    return obj->active;
}


/**
 * virObjectLookupKeysSetActive
 * @anyobj: Pointer to a locked LookupKeys object
 * @active: New active setting
 *
 * Set the lookup keys active bool value; value not changed if object
 * is not a lookup keys object
 */
void
virObjectLookupKeysSetActive(void *anyobj,
                             bool active)
{
    virObjectLookupKeysPtr obj = virObjectGetLookupKeysObj(anyobj);

    if (!obj)
        return;

    obj->active = active;
}


/**
 * virObjectLookupKeysGetUUID
 * @anyobj: Pointer to a LookupKeys object
 *
 * Returns: Pointer to the object's uuid key value or possibly NULL
 */
const char *
virObjectLookupKeysGetUUID(void *anyobj)
{
    virObjectLookupKeysPtr obj = virObjectGetLookupKeysObj(anyobj);

    if (!obj)
        return NULL;

    return obj->uuid;
}


/**
 * virObjectLookupKeysGetName
 * @anyobj: Pointer to a LookupKeys object
 *
 * Returns: Pointer to the object's name key value or possibly NULL
 */
const char *
virObjectLookupKeysGetName(void *anyobj)
{
    virObjectLookupKeysPtr obj = virObjectGetLookupKeysObj(anyobj);

    if (!obj)
        return NULL;

    return obj->name;
}


/**
 * virObjectLookupHashAdd:
 * @tableobj: LookupHash object with objsUUID/objsName
 * @obj: The LookupKeys object to insert in the hash table(s)
 *
 * Insert @obj into the hash tables found in @tableobj.
 *
 * Returns 0 on success, -1 on failure.
 */
int
virObjectLookupHashAdd(void *tableobj,
                       virObjectLookupKeysPtr obj)
{
    virObjectLookupHashPtr hashObj = virObjectGetLookupHashObj(tableobj);

    if (!hashObj) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("tableobj=%p is not a lookup hash object"), tableobj);
        return -1;
    }

    if (obj->uuid) {
        if (virHashAddEntry(hashObj->objsUUID, obj->uuid, obj) < 0)
            return -1;
        virObjectRef(obj);
    }

    if (obj->name) {
        if (virHashAddEntry(hashObj->objsName, obj->name, obj) < 0) {
            virHashRemoveEntry(hashObj->objsUUID, obj->uuid);
            return -1;
        }
        virObjectRef(obj);
    }

    return 0;
}


/**
 * virObjectLookupHashRemove:
 * @tableobj: LookupHash object with objsUUID/objsName
 * @obj: The LookupKeys object to remove from the hash table(s)
 *
 * Remove @obj from the hash tables found in @tableobj. The common
 * function to remove an object from a hash table will also cause
 * the virObjectUnref to be called via virObjectFreeHashData since
 * the virHashCreate used that as the Free object element argument.
 *
 * Even though this is a void, report the error for a bad @tableobj.
 */
void
virObjectLookupHashRemove(void *tableobj,
                          virObjectLookupKeysPtr obj)
{
    virObjectLookupHashPtr hashObj;

    if (!obj)
        return;

    if (!(hashObj = virObjectGetLookupHashObj(tableobj))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("tableobj=%p is not a lookup hash object"), tableobj);
        return;
    }

    virObjectRef(obj);
    virObjectUnlock(obj);
    virObjectLock(tableobj);
    virObjectLock(obj);
    if (obj->uuid)
        virHashRemoveEntry(hashObj->objsUUID, obj->uuid);
    if (obj->name)
        virHashRemoveEntry(hashObj->objsName, obj->name);
    virObjectUnlock(obj);
    virObjectUnref(obj);
    virObjectUnlock(tableobj);
}


/**
 * virObjectLookupHashGetUUID
 * @anyobj: Pointer to a LookupHash object
 *
 * Returns: Pointer to the UUID Hash Table or NULL on failure
 */
virHashTablePtr
virObjectLookupHashGetUUID(void *anyobj)
{
    virObjectLookupHashPtr obj = virObjectGetLookupHashObj(anyobj);

    if (!obj)
        return NULL;

    return obj->objsUUID;
}


/**
 * virObjectLookupHashFind:
 * @tableobj: poolable hash table pointer find data
 * @useUUID: boolean to use objsUUID
 * @key: Key to use for lookup
 *
 * Returns a pointer to the entry or NULL on failure
 */
virObjectLookupKeysPtr
virObjectLookupHashFind(void *tableobj,
                        bool useUUID,
                        const char *key)
{
    virObjectLookupHashPtr hashObj = virObjectGetLookupHashObj(tableobj);

    if (!hashObj) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       ("tableobj=%p is not a lookup hash object"), tableobj);
        return NULL;
    }

    return virHashLookup(useUUID ? hashObj->objsUUID : hashObj->objsName, key);
}


/**
 * virObjectLookupHashGetName
 * @anyobj: Pointer to a LookupHash object
 *
 * Returns: Pointer to the Name Hash Table or NULL on failure
 */
virHashTablePtr
virObjectLookupHashGetName(void *anyobj)
{
    virObjectLookupHashPtr obj = virObjectGetLookupHashObj(anyobj);

    if (!obj)
        return NULL;

    return obj->objsName;
}


/**
 * virObjectLookupHashForEach
 * @tableobj: poolable hash table pointer to walk through
 * @useUUID: boolean to use objsUUID
 * @callback: callback function to handle the object specific checks
 * @opaque: callback data
 *
 * For each element of the objsName hash table make a call into the
 * callback routine to handle its task
 *
 * Returns 0 on success, -1 on failure
 */
int
virObjectLookupHashForEach(void *tableobj,
                           bool useUUID,
                           virHashIterator callback,
                           virObjectLookupHashForEachDataPtr data)
{
    virObjectLookupHashPtr hashObj = virObjectGetLookupHashObj(tableobj);
    virHashTablePtr tbl;

    if (!hashObj)
        return -1;

    if (useUUID)
        tbl = hashObj->objsUUID;
    else
        tbl = hashObj->objsName;

    if (data->maxelems == -1) {
        if (VIR_ALLOC_N(data->elems, virHashSize(tbl) + 1) < 0)
            return -1;
    }

    virObjectLock(hashObj);
    virHashForEach(tbl, callback, data);
    virObjectUnlock(hashObj);

    if (data->error)
        goto error;

    if (data->maxelems == -1) {
        /* trim the array to the final size */
        ignore_value(VIR_REALLOC_N(data->elems, data->nelems + 1));
    }

    return data->nelems;

 error:
    if (data->elems) {
        if (data->maxelems == -1) {
            virObjectListFree(data->elems);
        } else {
            while (--data->nelems)
                VIR_FREE(data->elems[data->nelems]);
        }
    }
    return -1;
}


/**
 * virObjectLookupHashSearchName
 * @tableobj: poolable hash table pointer to search
 * @useUUID: boolean to use objsUUID
 * @callback: callback function to handle the object specific checks
 * @opaque: callback data
 *
 * Search the objsName hash table calling the specified @callback routine
 * with an object and @opaque data in order to determine whether the searched
 * object is represented by the @opaque data.
 *
 * Returns locked/refcnt incremented object on success, NULL on failure
 */
virObjectLookupKeysPtr
virObjectLookupHashSearch(void *tableobj,
                          bool useUUID,
                          virHashSearcher callback,
                          void *opaque)
{
    virObjectLookupHashPtr tableObj = virObjectGetLookupHashObj(tableobj);
    virObjectLookupKeysPtr obj;

    if (!tableObj)
        return NULL;

    virObjectLock(tableObj);
    obj = virHashSearch(useUUID ? tableObj->objsUUID : tableObj->objsName,
                        callback, opaque);
    virObjectRef(obj);
    virObjectUnlock(tableObj);

    if (obj)
        virObjectLock(obj);

    return obj;
}


struct cloneData {
    virObjectLookupHashCloneCallback callback;
    virObjectLookupHashPtr dst;
    bool error;
};

/*
 * Take the provided virHashForEach element and call the driver @cb function
 * with the input @dstTable and the source element from the @srcTable in order
 * to perform the copy - tracking success/failure using the error boolean.
 * Once there's a failure, no future copy/clone will occur.
 *
 * The @cb function can expect the @srcTable object to be locked upon entry.
 *
 * Returns 0 to the virHashForEach on success, -1 on failure.
 */
static int
cloneCallback(void *payload,
              const void *name ATTRIBUTE_UNUSED,
              void *opaque)
{
    virObjectLookupKeysPtr obj = payload;
    struct cloneData *data = opaque;

    if (data->error)
        return 0;

    virObjectLock(obj);

    if (data->callback(data->dst, obj) < 0)
        data->error = true;

    virObjectUnlock(obj);

    if (data->error)
        return -1;

    return 0;
}

/**
 * virObjectLookupHashClone
 * @srcTable: source poolable hash table pointer to clone from
 * @dstTable: destination poolable hash table pointer to clone to
 * @useUUID: Use the objsUUID for clone (or objsName)
 * @cb: callback function from driver code to handle the clone
 *
 * The clone function is designed to traverse each @srcTable hash element
 * and call the driver specific @cb function with the element from the
 * @srcTable in order to clone into the @dstTable. If @useUUID is true,
 * then clone the objsUUID table; otherwise, clone the objsName table.
 *
 * Return 0 on success, -1 on failure
 */
int
virObjectLookupHashClone(void *srcTable,
                         void *dstTable,
                         bool useUUID,
                         virObjectLookupHashCloneCallback cb)
{
    virObjectLookupHashPtr src = virObjectGetLookupHashObj(srcTable);
    virObjectLookupHashPtr dst = virObjectGetLookupHashObj(dstTable);
    struct cloneData data = { .callback = cb, .dst = dst, .error = false };

    if (!src || !dst)
        return -1;

    virObjectLock(src);
    virHashForEach(useUUID ? src->objsUUID : src->objsName,
                   cloneCallback, &data);
    virObjectUnlock(src);

    if (data.error)
        return -1;

    return 0;
}
