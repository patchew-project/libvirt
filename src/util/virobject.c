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

#define VIR_OBJECT_NOTVALID(obj) (!obj || ((obj->u.s.magic & 0xFFFF0000) != 0xCAFE0000))

#define VIR_OBJECT_USAGE_PRINT_ERROR(anyobj, objclass)                      \
    do {                                                                    \
        virObjectPtr obj = anyobj;                                          \
        if (VIR_OBJECT_NOTVALID(obj)) {                                     \
            if (!obj)                                                       \
                VIR_ERROR(_("Object cannot be NULL"));                      \
            else                                                            \
                VIR_ERROR(_("Object %p has a bad magic number %X"),         \
                         obj, obj->u.s.magic);                              \
        } else {                                                            \
            VIR_ERROR(_("Object %p (%s) is not a %s instance"),             \
                      anyobj, obj->klass->name, #objclass);                 \
        }                                                                   \
    } while (0)


static virClassPtr virObjectClass;
static virClassPtr virObjectLockableClass;
static virClassPtr virObjectRWLockableClass;
static virClassPtr virObjectLookupKeysClass;
static virClassPtr virObjectLookupHashClass;

static void virObjectLockableDispose(void *anyobj);
static void virObjectRWLockableDispose(void *anyobj);
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

    if (!(virObjectRWLockableClass = virClassNew(virObjectClass,
                                                 "virObjectRWLockable",
                                                 sizeof(virObjectRWLockable),
                                                 virObjectRWLockableDispose)))
        return -1;

    if (!(virObjectLookupKeysClass = virClassNew(virObjectLockableClass,
                                                 "virObjectLookupKeys",
                                                 sizeof(virObjectLookupKeys),
                                                 virObjectLookupKeysDispose)))
        return -1;

    if (!(virObjectLookupHashClass = virClassNew(virObjectRWLockableClass,
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
 * virClassForObjectRWLockable:
 *
 * Returns the class instance for the virObjectRWLockable type
 */
virClassPtr
virClassForObjectRWLockable(void)
{
    if (virObjectInitialize() < 0)
        return NULL;

    return virObjectRWLockableClass;
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


void *
virObjectRWLockableNew(virClassPtr klass)
{
    virObjectRWLockablePtr obj;

    if (!virClassIsDerivedFrom(klass, virClassForObjectRWLockable())) {
        virReportInvalidArg(klass,
                            _("Class %s must derive from virObjectRWLockable"),
                            virClassName(klass));
        return NULL;
    }

    if (!(obj = virObjectNew(klass)))
        return NULL;

    if (virRWLockInit(&obj->lock) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Unable to initialize RW lock"));
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


static void
virObjectRWLockableDispose(void *anyobj)
{
    virObjectRWLockablePtr obj = anyobj;

    virRWLockDestroy(&obj->lock);
}


/**
 * virObjectLookupKeysNew:
 * @klass: the klass to check
 * @key1: key to be used for unique identifier (required)
 * @key2: second key to be used as secondary unique identifier
 *
 * Create an object with at least @key1 as a means to provide input
 * of an input object to add the object into a hash table. If @key2 is
 * provided, then the object will exist into two hash tables for faster
 * lookups by key for the table; otherwise, hash table searches would
 * need to be used to find data from an object that matches some specific
 * search the caller performs.
 *
 * Returns: New object on success, NULL on failure w/ error message set
 */
void *
virObjectLookupKeysNew(virClassPtr klass,
                       const char *key1,
                       const char *key2)
{
    virObjectLookupKeysPtr obj;

    if (!virClassIsDerivedFrom(klass, virClassForObjectLookupKeys())) {
        virReportInvalidArg(klass,
                            _("Class %s must derive from virObjectLookupKeys"),
                            virClassName(klass));
        return NULL;
    }

    if (!key1) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("key1 must be provided"));
        return NULL;
    }

    if (!(obj = virObjectLockableNew(klass)))
        return NULL;

    if (VIR_STRDUP(obj->key1, key1) < 0)
        goto error;

    if (VIR_STRDUP(obj->key2, key2) < 0)
        goto error;

    return obj;

 error:
    virObjectUnref(obj);
    return NULL;
}


static void
virObjectLookupKeysDispose(void *anyobj)
{
    virObjectLookupKeysPtr obj = anyobj;

    VIR_FREE(obj->key1);
    VIR_FREE(obj->key2);
}


/**
 * virObjectLookupHashNew:
 * @klass: the klass to check
 * @tableElemsStart: initial size of each hash table
 * @createBoth: boolean to determine how many hash tables to create
 *
 * Create a new poolable hash table object for storing either 1 or 2
 * hash tables capable of storing virObjectLookupKeys objects. This
 * object will use the RWLockable objects in order to allow for concurrent
 * table reads by multiple threads looking to return lists of data.
 *
 * Returns: New object on success, NULL on failure w/ error message set
 */
void *
virObjectLookupHashNew(virClassPtr klass,
                       int tableElemsStart,
                       bool createBoth)
{
    virObjectLookupHashPtr obj;

    if (!virClassIsDerivedFrom(klass, virClassForObjectLookupHash())) {
        virReportInvalidArg(klass,
                            _("Class %s must derive from virObjectLookupHash"),
                            virClassName(klass));
        return NULL;
    }

    if (!(obj = virObjectRWLockableNew(klass)))
        return NULL;

    if (!(obj->objsKey1 = virHashCreate(tableElemsStart,
                                        virObjectFreeHashData)))
        goto error;

    if (createBoth &&
        !(obj->objsKey2 = virHashCreate(tableElemsStart,
                                        virObjectFreeHashData)))
        goto error;

    return obj;

 error:
    virObjectUnref(obj);
    return NULL;
}


static void
virObjectLookupHashDispose(void *anyobj)
{
    virObjectLookupHashPtr obj = anyobj;

    virHashFree(obj->objsKey1);
    virHashFree(obj->objsKey2);
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
        virObjectIsClass(anyobj, virObjectLookupKeysClass))
        return anyobj;

    VIR_OBJECT_USAGE_PRINT_ERROR(anyobj, virObjectLockable);
    return NULL;
}


static virObjectRWLockablePtr
virObjectGetRWLockableObj(void *anyobj)
{
    if (virObjectIsClass(anyobj, virObjectRWLockableClass) ||
        virObjectIsClass(anyobj, virObjectLookupHashClass))
        return anyobj;

    VIR_OBJECT_USAGE_PRINT_ERROR(anyobj, virObjectRWLockable);
    return NULL;
}


/**
 * virObjectLock:
 * @anyobj: any instance of virObjectLockable or virObjectRWLockable
 *
 * Acquire a lock on @anyobj. The lock must be released by
 * virObjectUnlock.
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
 * virObjectRWLockRead:
 * @anyobj: any instance of virObjectRWLockable
 *
 * Acquire a read lock on @anyobj. The lock must be
 * released by virObjectRWUnlock.
 *
 * The caller is expected to have acquired a reference
 * on the object before locking it (eg virObjectRef).
 * The object must be unlocked before releasing this
 * reference.
 *
 * NB: It's possible to return without the lock if
 *     @anyobj was invalid - this has been considered
 *     a programming error rather than something that
 *     should be checked.
 */
void
virObjectRWLockRead(void *anyobj)
{
    virObjectRWLockablePtr obj = virObjectGetRWLockableObj(anyobj);

    if (!obj)
        return;

    virRWLockRead(&obj->lock);
}


/**
 * virObjectRWLockWrite:
 * @anyobj: any instance of virObjectRWLockable
 *
 * Acquire a write lock on @anyobj. The lock must be
 * released by virObjectRWUnlock.
 *
 * The caller is expected to have acquired a reference
 * on the object before locking it (eg virObjectRef).
 * The object must be unlocked before releasing this
 * reference.
 *
 * NB: It's possible to return without the lock if
 *     @anyobj was invalid - this has been considered
 *     a programming error rather than something that
 *     should be checked.
 */
void
virObjectRWLockWrite(void *anyobj)
{
    virObjectRWLockablePtr obj = virObjectGetRWLockableObj(anyobj);

    if (!obj)
        return;

    virRWLockWrite(&obj->lock);
}


/**
 * virObjectUnlock:
 * @anyobj: any instance of virObjectLockable
 *
 * Release a lock on @anyobj. The lock must have been acquired by
 * virObjectLock.
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
 * virObjectRWUnlock:
 * @anyobj: any instance of virObjectRWLockable
 *
 * Release a lock on @anyobj. The lock must have been acquired by
 * virObjectRWLockRead or virObjectRWLockWrite.
 */
void
virObjectRWUnlock(void *anyobj)
{
    virObjectRWLockablePtr obj = virObjectGetRWLockableObj(anyobj);

    if (!obj)
        return;

    virRWLockUnlock(&obj->lock);
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


static virObjectLookupKeysPtr
virObjectGetLookupKeysObj(void *anyobj)
{
    if (virObjectIsClass(anyobj, virObjectLookupKeysClass))
        return anyobj;

    VIR_OBJECT_USAGE_PRINT_ERROR(anyobj, virObjectLookupKeysClass);

    return NULL;
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


static virObjectLookupHashPtr
virObjectGetLookupHashObj(void *anyobj)
{
    if (virObjectIsClass(anyobj, virObjectLookupHashClass))
        return anyobj;

    VIR_OBJECT_USAGE_PRINT_ERROR(anyobj, virObjectLookupHashClass);

    return NULL;
}


/**
 * virObjectLookupHashAdd:
 * @anyobj: LookupHash object
 * @obj: The LookupKeys object to insert in the hash table(s)
 *
 * Insert @obj into the hash tables found in @anyobj. Assumes that the
 * caller has determined that the key1 and possibly key2 do not already
 * exist in their respective hash table to be inserted.
 *
 * Returns 0 on success, -1 on failure.
 */
int
virObjectLookupHashAdd(void *anyobj,
                       virObjectLookupKeysPtr obj)
{
    virObjectLookupHashPtr hashObj = virObjectGetLookupHashObj(anyobj);

    if (!hashObj)
        return -1;

    if (obj->key2 && !hashObj->objsKey2) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("hashObj=%p has one table, but two keys from obj=%p"),
                       hashObj, obj);
        return -1;
    }

    if (virHashAddEntry(hashObj->objsKey1, obj->key1, obj) < 0)
        return -1;
    virObjectRef(obj);

    if (obj->key2) {
        if (virHashAddEntry(hashObj->objsKey2, obj->key2, obj) < 0) {
            virHashRemoveEntry(hashObj->objsKey1, obj->key1);
            return -1;
        }
        virObjectRef(obj);
    }

    return 0;
}


/**
 * virObjectLookupHashRemove:
 * @anyobj: LookupHash object
 * @obj: The LookupKeys object to remove from the hash table(s)
 *
 * Remove @obj from the hash tables found in @anyobj. The common
 * function to remove an object from a hash table will also cause
 * the virObjectUnref to be called via virObjectFreeHashData since
 * the virHashCreate used that as the Free object element argument.
 *
 * Even though this is a void, report the error for a bad @anyobj.
 */
void
virObjectLookupHashRemove(void *anyobj,
                          virObjectLookupKeysPtr obj)
{
    virObjectLookupHashPtr hashObj;

    if (!obj)
        return;

    if (!(hashObj = virObjectGetLookupHashObj(anyobj)))
        return;

    virObjectRef(obj);
    virObjectUnlock(obj);
    virObjectRWLockWrite(hashObj);
    virObjectLock(obj);
    virHashRemoveEntry(hashObj->objsKey1, obj->key1);
    if (obj->key2)
        virHashRemoveEntry(hashObj->objsKey2, obj->key2);
    virObjectUnlock(obj);
    virObjectUnref(obj);
    virObjectRWUnlock(hashObj);
}


static virObjectLookupKeysPtr
virObjectLookupHashFindInternal(virObjectLookupHashPtr hashObj,
                                const char *key)
{
    virObjectLookupKeysPtr obj;

    if ((obj = virHashLookup(hashObj->objsKey1, key)))
        return virObjectRef(obj);

    if (hashObj->objsKey2)
        obj = virHashLookup(hashObj->objsKey2, key);

    return virObjectRef(obj);
}


/**
 * virObjectLookupHashFindLocked:
 * @anyobj: LookupHash object
 * @key: Key to use for lookup
 *
 * Search through the hash tables looking for the key. The key may be
 * either key1 or key2 - both tables if they exist will be searched.
 *
 * NB: Assumes that the LookupHash has already been locked
 *
 * Returns a pointer to the entry with refcnt incremented or NULL on failure
 */
virObjectLookupKeysPtr
virObjectLookupHashFindLocked(void *anyobj,
                              const char *key)
{
    virObjectLookupHashPtr hashObj = virObjectGetLookupHashObj(anyobj);

    if (!hashObj)
        return NULL;

    return virObjectLookupHashFindInternal(anyobj, key);

}


/**
 * virObjectLookupHashFind:
 * @anyobj: LookupHash object
 * @key: Key to use for lookup
 *
 * Call virObjectLookupHashFindLocked after locking the LookupHash
 *
 * Returns a pointer to the entry with refcnt incremented or NULL on failure
 */
virObjectLookupKeysPtr
virObjectLookupHashFind(void *anyobj,
                        const char *key)
{
    virObjectLookupHashPtr hashObj = virObjectGetLookupHashObj(anyobj);
    virObjectLookupKeysPtr obj;

    if (!hashObj)
        return NULL;

    virObjectRWLockRead(hashObj);
    obj = virObjectLookupHashFindInternal(hashObj, key);
    virObjectRWUnlock(hashObj);

    return obj;
}


/**
 * virObjectLookupHashForEach
 * @anyobj: LookupHash object
 * @callback: callback function to handle the object specific checks
 * @opaque: callback data
 *
 * For each element of the objsKey1 hash table make a call into the
 * callback routine to handle its task. Even if there were two hash
 * tables all the objects exist in both, so it's only necessary to
 * run through one of them.
 *
 * NB:
 * struct _virObjectLookupHashForEachData {
 *     virConnectPtr conn;     -> Connect ptr for @filter APIs
 *     void *opaque;           -> Opaque data as determined by caller
 *     void *filter;           -> A pointer to function for ACL calls
 *     bool wantActive;        -> Filter active objs
 *     bool error;             -> Set by callback functions for error
 *     const char *matchStr;   -> Filter for specific string in many objs
 *     unsigned int flags;     -> @flags argument to for Export calls
 *     int nElems;             -> # of elements found and passing filters
 *     void **elems;           -> array of elements
 *     int maxElems;           -> maximum # of elements to collect
 *                                Use -1 to allocate array of N table sized
 *                                elements to use for Export functions
 *                                Use -2 for NumOf functions to avoid the
 *                                allocation, but allow sharing with the
 *                                GetNames type functions
 * };
 *
 * Returns number of elements found on success, -1 on failure
 */
int
virObjectLookupHashForEach(void *anyobj,
                           virHashIterator callback,
                           virObjectLookupHashForEachDataPtr data)
{
    virObjectLookupHashPtr hashObj = virObjectGetLookupHashObj(anyobj);

    if (!hashObj)
        return -1;

    if (data->maxElems == -1) {
        if (VIR_ALLOC_N(data->elems, virHashSize(hashObj->objsKey1) + 1) < 0)
            return -1;
    }

    virObjectRWLockRead(hashObj);
    virHashForEach(hashObj->objsKey1, callback, data);
    virObjectRWUnlock(hashObj);

    if (data->error)
        goto error;

    if (data->maxElems == -1) {
        /* trim the array to the final size */
        ignore_value(VIR_REALLOC_N(data->elems, data->nElems + 1));
    }

    return data->nElems;

 error:
    if (data->elems) {
        if (data->maxElems == -1) {
            virObjectListFree(data->elems);
        } else {
            while (--data->nElems)
                VIR_FREE(data->elems[data->nElems]);
        }
    }
    return -1;
}


static virObjectLookupKeysPtr
virObjectLookupHashSearchInternal(virObjectLookupHashPtr hashObj,
                                  virHashSearcher callback,
                                  void *opaque)
{
    virObjectLookupKeysPtr obj;

    obj = virHashSearch(hashObj->objsKey1, callback, opaque, NULL);
    virObjectRef(obj);

    if (obj)
        virObjectLock(obj);

    return obj;
}


/**
 * virObjectLookupHashSearchLocked
 * @anyobj: LookupHash object
 * @callback: callback function to handle the object specific checks
 * @opaque: callback data
 *
 * Search the hash table objsKey1 table calling the specified @callback
 * routine with an object and @opaque data in order to determine whether
 * the object is represented by the @opaque data.
 *
 * NB: Caller assumes the responsibility for locking LookupHash
 *
 * Returns locked/refcnt incremented object on success, NULL on failure
 */
virObjectLookupKeysPtr
virObjectLookupHashSearchLocked(void *anyobj,
                                virHashSearcher callback,
                                void *opaque)
{
    virObjectLookupHashPtr hashObj = virObjectGetLookupHashObj(anyobj);

    if (!hashObj)
        return NULL;

    return virObjectLookupHashSearchInternal(hashObj, callback, opaque);
}


/**
 * virObjectLookupHashSearch
 * @anyobj: LookupHash object
 * @callback: callback function to handle the object specific checks
 * @opaque: callback data
 *
 * Call virObjectLookupHashSearchLocked with a locked hash table
 *
 * Returns @obj from virObjectLookupHashSearchLocked
 */
virObjectLookupKeysPtr
virObjectLookupHashSearch(void *anyobj,
                          virHashSearcher callback,
                          void *opaque)
{
    virObjectLookupHashPtr hashObj = virObjectGetLookupHashObj(anyobj);
    virObjectLookupKeysPtr obj;

    if (!hashObj)
        return NULL;

    virObjectRWLockRead(hashObj);
    obj = virObjectLookupHashSearchInternal(hashObj, callback, opaque);
    virObjectRWUnlock(hashObj);

    return obj;
}


struct cloneData {
    virObjectLookupHashCloneCallback callback;
    virObjectLookupHashPtr dst;
    bool error;
};

/*
 * Take the provided virHashForEach element and call the @cb function
 * with the input @dst hash table and the source element from the
 * @src hash table in order to perform the copy - tracking success/
 * failure using the error boolean.
 *
 * Once there's a failure, no future copy/clone will occur.
 *
 * The @cb function can expect the @src hash table object to be
 * locked upon entry.
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
 * @srcAnyobj: source LookupHash object to clone from
 * @dstAnyobj: destination LookupHash object to clone to
 * @cb: callback function to handle the clone
 *
 * The clone function is designed to traverse each source hash element
 * and call the driver specific @cb function with the element from the
 * source hash table in order to clone into the destination hash table.
 *
 * Return 0 on success, -1 on failure
 */
int
virObjectLookupHashClone(void *srcAnyobj,
                         void *dstAnyobj,
                         virObjectLookupHashCloneCallback cb)
{
    virObjectLookupHashPtr srcHashObj = virObjectGetLookupHashObj(srcAnyobj);
    virObjectLookupHashPtr dstHashObj = virObjectGetLookupHashObj(dstAnyobj);
    struct cloneData data = { .callback = cb, .dst = dstHashObj,
        .error = false };

    if (!srcHashObj || !dstHashObj)
        return -1;

    virObjectRWLockRead(srcHashObj);
    virHashForEach(srcHashObj->objsKey1, cloneCallback, &data);
    virObjectRWUnlock(srcHashObj);

    if (data.error)
        return -1;

    return 0;
}
