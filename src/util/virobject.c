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
static virClassPtr virObjectLookupHashClass;

static void virObjectLockableDispose(void *anyobj);
static void virObjectRWLockableDispose(void *anyobj);
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
 * virObjectLookupHashNew:
 * @klass: the klass to check
 * @tableElemsStart: initial size of each hash table
 * @flags: virObjectLookupHashNewFlags to indicate which tables to create
 *
 * Create a new poolable hash table object for storing either 1 or 2 hash
 * tables capable of storing virObjectLockable objects by UUID or Name. This
 * object will use the RWLockable objects in order to allow for concurrent
 * table reads by multiple threads looking to return lists of data.
 *
 * Returns: New object on success, NULL on failure w/ error message set
 */
void *
virObjectLookupHashNew(virClassPtr klass,
                       int tableElemsStart,
                       virObjectLookupHashNewFlags flags)
{
    virObjectLookupHashPtr obj;

    if (!flags || !(flags & (VIR_OBJECT_LOOKUP_HASH_UUID |
                             VIR_OBJECT_LOOKUP_HASH_NAME))) {
        virReportError(VIR_ERR_INVALID_ARG,
                       _("flags=%x must be non zero or properly set"), flags);
        return NULL;
    }

    if (!virClassIsDerivedFrom(klass, virClassForObjectLookupHash())) {
        virReportInvalidArg(klass,
                            _("Class %s must derive from virObjectLookupHash"),
                            virClassName(klass));
        return NULL;
    }

    if (!(obj = virObjectRWLockableNew(klass)))
        return NULL;

    if (flags & VIR_OBJECT_LOOKUP_HASH_UUID) {
        if (!(obj->objsUUID = virHashCreate(tableElemsStart,
                                            virObjectFreeHashData)))
            goto error;
    }

    if (flags & VIR_OBJECT_LOOKUP_HASH_NAME) {
        if (!(obj->objsName = virHashCreate(tableElemsStart,
                                            virObjectFreeHashData)))
        goto error;
    }

    return obj;

 error:
    virObjectUnref(obj);
    return NULL;
}


static void
virObjectLookupHashDispose(void *anyobj)
{
    virObjectLookupHashPtr obj = anyobj;

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
    if (virObjectIsClass(anyobj, virObjectLockableClass))
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


static virObjectLookupHashPtr
virObjectGetLookupHashObj(void *anyobj)
{
    if (virObjectIsClass(anyobj, virObjectLookupHashClass))
        return anyobj;

    VIR_OBJECT_USAGE_PRINT_ERROR(anyobj, virObjectLookupHashClass);

    return NULL;
}


static bool
virObjectLookupHashValidAddRemoveArgs(virObjectLookupHashPtr hashObj,
                                      virObjectLockablePtr obj,
                                      const char *uuidstr,
                                      const char *name)
{
    if (!hashObj || !obj)
        return false;

    if (uuidstr && !hashObj->objsUUID) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("no objsUUID for hashObj=%p, but uuidstr=%s provided"),
                       hashObj, uuidstr);
        return false;
    }

    if (name && !hashObj->objsName) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("no objsName for hashObj=%p, but name=%s provided"),
                       hashObj, name);
        return false;
    }

    return true;

}


/**
 * virObjectLookupHashAdd:
 * @anyobj: LookupHash object
 * @addObj: The (virObjectLockable) object to add to the hash table(s)
 * @uuidstr: uuid formatted into a char string to add to UUID table
 * @name: name to add to Name table
 *
 * Insert @obj into the hash tables found in @anyobj. Assumes that the
 * caller has determined that @uuidstr and @name do not already exist
 * in their respective hash table to be inserted.
 *
 * Returns 0 on success, -1 on failure.
 */
int
virObjectLookupHashAdd(void *anyobj,
                       void *addObj,
                       const char *uuidstr,
                       const char *name)
{
    virObjectLookupHashPtr hashObj = virObjectGetLookupHashObj(anyobj);
    virObjectLockablePtr obj = virObjectGetLockableObj(addObj);

    if (!virObjectLookupHashValidAddRemoveArgs(hashObj, obj, uuidstr, name))
        return -1;

    if (hashObj->objsUUID) {
        if (virHashAddEntry(hashObj->objsUUID, uuidstr, obj) < 0)
            return -1;
        virObjectRef(obj);
    }

    if (hashObj->objsName) {
        if (virHashAddEntry(hashObj->objsName, name, obj) < 0) {
            if (hashObj->objsUUID)
                virHashRemoveEntry(hashObj->objsUUID, uuidstr);
            return -1;
        }
        virObjectRef(obj);
    }

    return 0;
}


/**
 * virObjectLookupHashRemove:
 * @anyobj: LookupHash object
 * @delObj: The (virObjectLockable) object to remove from the hash table(s)
 * @uuidstr: uuid formatted into a char string to add to UUID table
 * @name: name to add to Name table
 *
 * Remove @obj from the hash tables found in @anyobj. The common
 * function to remove an object from a hash table will also cause
 * the virObjectUnref to be called via virObjectFreeHashData since
 * the virHashCreate used that as the Free object element argument.
 *
 * NB: Caller must first check if @obj is NULL before calling.
 *
 * Even though this is a void, report the error for a bad @anyobj.
 */
void
virObjectLookupHashRemove(void *anyobj,
                          void *delObj,
                          const char *uuidstr,
                          const char *name)
{
    virObjectLookupHashPtr hashObj = virObjectGetLookupHashObj(anyobj);
    virObjectLockablePtr obj = virObjectGetLockableObj(delObj);

    if (!virObjectLookupHashValidAddRemoveArgs(hashObj, obj, uuidstr, name))
        return;

    virObjectRef(obj);
    virObjectUnlock(obj);
    virObjectRWLockWrite(hashObj);
    virObjectLock(obj);
    if (uuidstr)
        virHashRemoveEntry(hashObj->objsUUID, uuidstr);
    if (name)
        virHashRemoveEntry(hashObj->objsName, name);
    virObjectUnlock(obj);
    virObjectUnref(obj);
    virObjectRWUnlock(hashObj);
}


static void *
virObjectLookupHashFindInternal(virObjectLookupHashPtr hashObj,
                                const char *key)
{
    virObjectLockablePtr obj;

    if (hashObj->objsUUID) {
        if ((obj = virHashLookup(hashObj->objsUUID, key)))
            return virObjectRef(obj);
    }

    if (hashObj->objsName) {
        obj = virHashLookup(hashObj->objsName, key);
            return virObjectRef(obj);
    }

    return NULL;
}


/**
 * virObjectLookupHashFindLocked:
 * @anyobj: LookupHash object
 * @key: Key to use for lookup
 *
 * Search through the hash tables looking for the @key. The @key may be
 * either UUID or Name - both tables if they exist will be searched.
 *
 * NB: Assumes that the LookupHash has already been locked
 *
 * Returns a pointer to the entry with refcnt incremented or NULL on failure
 */
void *
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
void *
virObjectLookupHashFind(void *anyobj,
                        const char *key)
{
    virObjectLookupHashPtr hashObj = virObjectGetLookupHashObj(anyobj);
    void *obj;

    if (!hashObj)
        return NULL;

    virObjectRWLockRead(hashObj);
    obj = virObjectLookupHashFindInternal(hashObj, key);
    virObjectRWUnlock(hashObj);

    return obj;
}
