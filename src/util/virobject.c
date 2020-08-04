/*
 * virobject.c: libvirt reference counted object
 *
 * Copyright (C) 2012-2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <config.h>

#define VIR_PARENT_REQUIRED /* empty, to allow virObject to have no parent */
#include "virobject.h"
#include "virthread.h"
#include "viralloc.h"
#include "virerror.h"
#include "virlog.h"
#include "virprobe.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("util.object");

static unsigned int magicCounter = 0xCAFE0000;

struct _virClass {
    virClassPtr parent;

    GType type;
    unsigned int magic;
    char *name;
    size_t objectSize;

    virObjectDisposeCallback dispose;
};

typedef struct _virObjectPrivate virObjectPrivate;
struct _virObjectPrivate {
    virClassPtr klass;
};


G_DEFINE_TYPE_WITH_PRIVATE(virObject, vir_object, G_TYPE_OBJECT)

#define VIR_OBJECT_NOTVALID(obj) (!obj || !VIR_IS_OBJECT(obj))

#define VIR_OBJECT_USAGE_PRINT_WARNING(anyobj, objclass) \
    do { \
        virObjectPtr obj = anyobj; \
        if (!obj) \
            VIR_WARN("Object cannot be NULL"); \
        if (VIR_OBJECT_NOTVALID(obj)) \
            VIR_WARN("Object %p (%s) is not a %s instance", \
                     anyobj, g_type_name_from_instance((void*)anyobj), #objclass); \
    } while (0)


static virClassPtr virObjectClassImpl;
static virClassPtr virObjectLockableClass;
static virClassPtr virObjectRWLockableClass;

static void virObjectLockableDispose(void *anyobj);
static void virObjectRWLockableDispose(void *anyobj);

static int
virObjectOnceInit(void)
{
    if (!(virObjectClassImpl = virClassNew(NULL,
                                           "virObject",
                                           sizeof(virObject),
                                           0,
                                           NULL)))
        return -1;

    if (!VIR_CLASS_NEW(virObjectLockable, virObjectClassImpl))
        return -1;

    if (!VIR_CLASS_NEW(virObjectRWLockable, virObjectClassImpl))
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

    return virObjectClassImpl;
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


static void virClassDummyInit(void *klass G_GNUC_UNUSED)
{
}

static void virObjectDummyInit(void *obj G_GNUC_UNUSED)
{
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
 * is expected to have a 'virObject parent;' field as (or
 * contained in) its first member. When the last reference
 * on the object is released, the @dispose callback will be
 * invoked to free memory of the local object fields, as
 * well as the dispose callbacks of the parent classes.
 *
 * Returns a new class instance
 */
virClassPtr
virClassNew(virClassPtr parent,
            const char *name,
            size_t objectSize,
            size_t parentSize,
            virObjectDisposeCallback dispose)
{
    virClassPtr klass;

    if (parent == NULL &&
        STRNEQ(name, "virObject")) {
        virReportInvalidNonNullArg(parent);
        return NULL;
    } else if (objectSize <= parentSize ||
               parentSize != (parent ? parent->objectSize : 0)) {
        sa_assert(parent);
        virReportInvalidArg(objectSize,
                            _("object size %zu of %s is not larger than parent class %zu"),
                            objectSize, name, parent->objectSize);
        return NULL;
    }

    klass = g_new0(virClass, 1);
    klass->parent = parent;
    klass->magic = g_atomic_int_add(&magicCounter, 1);
    klass->name = g_strdup(name);
    klass->objectSize = objectSize;
    if (parent == NULL) {
        klass->type = vir_object_get_type();
    } else {
        klass->type =
            g_type_register_static_simple(parent->type,
                                          name,
                                          sizeof(virObjectClass),
                                          (GClassInitFunc)virClassDummyInit,
                                          objectSize,
                                          (GInstanceInitFunc)virObjectDummyInit,
                                          0);
    }
    klass->dispose = dispose;

    return klass;
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
    virObjectPrivate *priv;

    obj = g_object_new(klass->type, NULL);

    priv = vir_object_get_instance_private(obj);
    priv->klass = klass;
    PROBE(OBJECT_NEW, "obj=%p classname=%s", obj, priv->klass->name);

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

static void vir_object_finalize(GObject *gobj)
{
    PROBE(OBJECT_DISPOSE, "obj=%p", gobj);
    virObjectPtr obj = VIR_OBJECT(gobj);
    virObjectPrivate *priv = vir_object_get_instance_private(obj);

    virClassPtr klass = priv->klass;
    while (klass) {
        if (klass->dispose)
            klass->dispose(obj);
        klass = klass->parent;
    }

    G_OBJECT_CLASS(vir_object_parent_class)->finalize(gobj);
}

static void vir_object_init(virObject *obj G_GNUC_UNUSED)
{
}


static void vir_object_class_init(virObjectClass *klass)
{
    GObjectClass *obj = G_OBJECT_CLASS(klass);

    obj->finalize = vir_object_finalize;
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
 * virObjectUnref:
 * @anyobj: any instance of virObjectPtr
 *
 * Decrement the reference count on @anyobj and if
 * it hits zero, runs the "dispose" callbacks associated
 * with the object class and its parents before freeing
 * @anyobj.
 */
void
virObjectUnref(void *anyobj)
{
    virObjectPtr obj = anyobj;

    if (VIR_OBJECT_NOTVALID(obj))
        return;

    g_object_unref(anyobj);
    PROBE(OBJECT_UNREF, "obj=%p", obj);
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

    g_object_ref(obj);
    PROBE(OBJECT_REF, "obj=%p", obj);
    return anyobj;
}


static virObjectLockablePtr
virObjectGetLockableObj(void *anyobj)
{
    if (virObjectIsClass(anyobj, virObjectLockableClass))
        return anyobj;

    VIR_OBJECT_USAGE_PRINT_WARNING(anyobj, virObjectLockable);
    return NULL;
}


static virObjectRWLockablePtr
virObjectGetRWLockableObj(void *anyobj)
{
    if (virObjectIsClass(anyobj, virObjectRWLockableClass))
        return anyobj;

    VIR_OBJECT_USAGE_PRINT_WARNING(anyobj, virObjectRWLockable);
    return NULL;
}


/**
 * virObjectLock:
 * @anyobj: any instance of virObjectLockable
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
    virObjectPrivate *priv;

    if (VIR_OBJECT_NOTVALID(obj))
        return false;

    priv = vir_object_get_instance_private(obj);
    return virClassIsDerivedFrom(priv->klass, klass);
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
virObjectFreeHashData(void *opaque)
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
