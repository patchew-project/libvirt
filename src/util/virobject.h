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

typedef struct _virObjectLookupKeys virObjectLookupKeys;
typedef virObjectLookupKeys *virObjectLookupKeysPtr;

typedef struct _virObjectLookupHash virObjectLookupHash;
typedef virObjectLookupHash *virObjectLookupHashPtr;

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
    virClassPtr klass;
};

struct _virObjectLockable {
    virObject parent;
    virMutex lock;
};

struct _virObjectLookupKeys {
    virObjectLockable parent;

    char *uuid;
    char *name;

    bool active;           /* true if object is active */
};

struct _virObjectLookupHash {
    virObjectLockable parent;

    int tableElemsStart;

    /* uuid string -> object mapping for O(1),
     * lockless lookup-by-uuid */
    virHashTable *objsUUID;

    /* name key -> object mapping for O(1),
     * lockless lookup-by-name */
    virHashTable *objsName;
};


virClassPtr virClassForObject(void);
virClassPtr virClassForObjectLockable(void);
virClassPtr virClassForObjectLookupKeys(void);
virClassPtr virClassForObjectLookupHash(void);

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
virObjectLookupKeysNew(virClassPtr klass,
                       const char *uuid,
                       const char *name)
    ATTRIBUTE_NONNULL(1);

void *
virObjectLookupHashNew(virClassPtr klass,
                       int tableElemsStart)
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

bool
virObjectLookupKeysIsActive(void *anyobj);

void
virObjectLookupKeysSetActive(void *anyobj,
                             bool active);

const char *
virObjectLookupKeysGetUUID(void *anyobj);

const char *
virObjectLookupKeysGetName(void *anyobj);

int
virObjectLookupHashAdd(void *tableobj,
                       virObjectLookupKeysPtr obj);

void
virObjectLookupHashRemove(void *tableobj,
                          virObjectLookupKeysPtr obj);

virHashTablePtr
virObjectLookupHashGetUUID(void *anyobj);

virHashTablePtr
virObjectLookupHashGetName(void *anyobj);

#endif /* __VIR_OBJECT_H */
