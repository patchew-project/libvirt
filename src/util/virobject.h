/*
 * virobject.h: libvirt reference counted object
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

#ifndef __VIR_OBJECT_H__
# define __VIR_OBJECT_H__

# include "internal.h"
# include "virhash.h"
# include "virthread.h"

typedef struct _virClass virClass;
typedef virClass *virClassPtr;

typedef struct _virObject virObject;
typedef virObject *virObjectPtr;

typedef struct _virObjectLockable virObjectLockable;
typedef virObjectLockable *virObjectLockablePtr;

typedef struct _virObjectPoolableHashTable virObjectPoolableHashTable;
typedef virObjectPoolableHashTable *virObjectPoolableHashTablePtr;

typedef void (*virObjectDisposeCallback)(void *obj);

/* Most code should not play with the contents of this struct; however,
 * the struct itself is public so that it can be embedded as the first
 * field of a subclassed object.  */
struct _virObject {
    /* Ensure correct alignment of this and all subclasses, even on
     * platforms where 'long long' or function pointers have stricter
     * requirements than 'void *'.  */
    union {
        long long dummy_align1;
        void (*dummy_align2) (void);
        struct {
            unsigned int magic;
            int refs;
        } s;
    } u;
    unsigned int magic_marker;
    virClassPtr klass;
};

struct _virObjectLockable {
    virObject parent;
    virMutex lock;
};

typedef enum {
    VIR_OBJECTPOOLABLE_NODEDEVICE,
    VIR_OBJECTPOOLABLE_INTERFACE,
    VIR_OBJECTPOOLABLE_NWFILTER,
    VIR_OBJECTPOOLABLE_VOLUME,
    VIR_OBJECTPOOLABLE_BLOCK_STORAGE,
    VIR_OBJECTPOOLABLE_SECRET,
    VIR_OBJECTPOOLABLE_NETWORK,
    VIR_OBJECTPOOLABLE_SNAPSHOT,
    VIR_OBJECTPOOLABLE_DOMAIN,

    VIR_OBJECTPOOLABLE_LAST
} virObjectPoolableHashTableObjType;

struct _virObjectPoolableHashTable {
    virObjectLockable parent;
    virObjectPoolableHashTableObjType objtype;
};


virClassPtr virClassForObject(void);
virClassPtr virClassForObjectLockable(void);
virClassPtr virClassForObjectPoolableHashTable(void);

# ifndef VIR_PARENT_REQUIRED
#  define VIR_PARENT_REQUIRED ATTRIBUTE_NONNULL(1)
# endif
virClassPtr
virClassNew(virClassPtr parent,
            const char *name,
            size_t objectSize,
            virObjectDisposeCallback dispose)
    VIR_PARENT_REQUIRED ATTRIBUTE_NONNULL(2);

const char *
virClassName(virClassPtr klass)
    ATTRIBUTE_NONNULL(1);

bool
virClassIsDerivedFrom(virClassPtr klass,
                      virClassPtr parent)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

void *
virObjectNew(virClassPtr klass)
    ATTRIBUTE_NONNULL(1);

bool
virObjectUnref(void *obj);

void *
virObjectRef(void *obj);

bool
virObjectIsClass(void *obj,
                 virClassPtr klass)
    ATTRIBUTE_NONNULL(2);

void
virObjectFreeCallback(void *opaque);

void
virObjectFreeHashData(void *opaque,
                      const void *name);

void *
virObjectLockableNew(virClassPtr klass)
    ATTRIBUTE_NONNULL(1);

void *
virObjectPoolableHashTableNew(virClassPtr klass,
                              virObjectPoolableHashTableObjType objtype)
    ATTRIBUTE_NONNULL(1);

void
virObjectLock(void *lockableobj)
    ATTRIBUTE_NONNULL(1);

void
virObjectUnlock(void *lockableobj)
    ATTRIBUTE_NONNULL(1);

void
virObjectListFree(void *list);

void
virObjectListFreeCount(void *list,
                       size_t count);

#endif /* __VIR_OBJECT_H */
