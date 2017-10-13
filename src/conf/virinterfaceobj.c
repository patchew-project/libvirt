/*
 * virinterfaceobj.c: interface object handling
 *                    (derived from interface_conf.c)
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
#include "interface_conf.h"

#include "viralloc.h"
#include "virerror.h"
#include "virinterfaceobj.h"
#include "virhash.h"
#include "virlog.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_INTERFACE

VIR_LOG_INIT("conf.virinterfaceobj");

struct _virInterfaceObj {
    virObjectLockable parent;

    bool active;           /* true if interface is active (up) */
    virInterfaceDefPtr def; /* The interface definition */
};

struct _virInterfaceObjList {
    virObjectRWLockable parent;

    /* name string -> virInterfaceObj  mapping
     * for O(1), lockless lookup-by-name */
    virHashTable *objsName;
};

/* virInterfaceObj manipulation */

static virClassPtr virInterfaceObjClass;
static virClassPtr virInterfaceObjListClass;
static void virInterfaceObjDispose(void *obj);
static void virInterfaceObjListDispose(void *obj);

static int
virInterfaceObjOnceInit(void)
{
    if (!(virInterfaceObjClass = virClassNew(virClassForObjectLockable(),
                                             "virInterfaceObj",
                                             sizeof(virInterfaceObj),
                                             virInterfaceObjDispose)))
        return -1;

    if (!(virInterfaceObjListClass = virClassNew(virClassForObjectRWLockable(),
                                                 "virInterfaceObjList",
                                                 sizeof(virInterfaceObjList),
                                                 virInterfaceObjListDispose)))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(virInterfaceObj)


static void
virInterfaceObjDispose(void *opaque)
{
    virInterfaceObjPtr obj = opaque;

    virInterfaceDefFree(obj->def);
}


static virInterfaceObjPtr
virInterfaceObjNew(void)
{
    virInterfaceObjPtr obj;

    if (virInterfaceObjInitialize() < 0)
        return NULL;

    if (!(obj = virObjectLockableNew(virInterfaceObjClass)))
        return NULL;

    virObjectLock(obj);

    return obj;
}


void
virInterfaceObjEndAPI(virInterfaceObjPtr *obj)
{
    if (!*obj)
        return;

    virObjectUnlock(*obj);
    virObjectUnref(*obj);
    *obj = NULL;
}


virInterfaceDefPtr
virInterfaceObjGetDef(virInterfaceObjPtr obj)
{
    return obj->def;
}


bool
virInterfaceObjIsActive(virInterfaceObjPtr obj)
{
    return obj->active;
}


void
virInterfaceObjSetActive(virInterfaceObjPtr obj,
                         bool active)
{
    obj->active = active;
}


/* virInterfaceObjList manipulation */
virInterfaceObjListPtr
virInterfaceObjListNew(void)
{
    virInterfaceObjListPtr interfaces;

    if (virInterfaceObjInitialize() < 0)
        return NULL;

    if (!(interfaces = virObjectRWLockableNew(virInterfaceObjListClass)))
        return NULL;

    if (!(interfaces->objsName = virHashCreate(10, virObjectFreeHashData))) {
        virObjectUnref(interfaces);
        return NULL;
    }

    return interfaces;
}


struct _virInterfaceObjForEachData {
    bool wantActive;
    const char *matchStr;
    bool error;
    int nElems;
    int maxElems;
    char **const elems;
};

static int
virInterfaceObjListFindByMACStringCb(void *payload,
                                     const void *name ATTRIBUTE_UNUSED,
                                     void *opaque)
{
    virInterfaceObjPtr obj = payload;
    struct _virInterfaceObjForEachData *data = opaque;

    if (data->error)
        return 0;

    if (data->nElems == data->maxElems)
        return 0;

    virObjectLock(obj);

    if (STRCASEEQ(obj->def->mac, data->matchStr)) {
        if (VIR_STRDUP(data->elems[data->nElems], data->matchStr) < 0) {
            data->error = true;
            goto cleanup;
        }
        data->nElems++;
    }

 cleanup:
    virObjectUnlock(obj);
    return 0;
}


int
virInterfaceObjListFindByMACString(virInterfaceObjListPtr interfaces,
                                   const char *mac,
                                   char **const matches,
                                   int maxmatches)
{
    struct _virInterfaceObjForEachData data = { .matchStr = mac,
                                                .error = false,
                                                .nElems = 0,
                                                .maxElems = maxmatches,
                                                .elems = matches };

    virObjectRWLockRead(interfaces);
    virHashForEach(interfaces->objsName, virInterfaceObjListFindByMACStringCb,
                   &data);
    virObjectRWUnlock(interfaces);

    if (data.error)
        goto error;

    return data.nElems;

 error:
    while (--data.nElems >= 0)
        VIR_FREE(data.elems[data.nElems]);

    return -1;
}


static virInterfaceObjPtr
virInterfaceObjListFindByNameLocked(virInterfaceObjListPtr interfaces,
                                    const char *name)
{
    return virObjectRef(virHashLookup(interfaces->objsName, name));
}


virInterfaceObjPtr
virInterfaceObjListFindByName(virInterfaceObjListPtr interfaces,
                              const char *name)
{
    virInterfaceObjPtr obj;
    virObjectRWLockRead(interfaces);
    obj = virInterfaceObjListFindByNameLocked(interfaces, name);
    virObjectRWUnlock(interfaces);
    if (obj)
        virObjectLock(obj);

    return obj;
}


void
virInterfaceObjListDispose(void *obj)
{
    virInterfaceObjListPtr interfaces = obj;

    virHashFree(interfaces->objsName);
}


struct _virInterfaceObjListCloneData {
    bool error;
    virInterfaceObjListPtr dest;
};

static int
virInterfaceObjListCloneCb(void *payload,
                           const void *name ATTRIBUTE_UNUSED,
                           void *opaque)
{
    virInterfaceObjPtr srcObj = payload;
    struct _virInterfaceObjListCloneData *data = opaque;
    char *xml = NULL;
    virInterfaceDefPtr backup = NULL;
    virInterfaceObjPtr obj;

    if (data->error)
        return 0;

    virObjectLock(srcObj);

    if (!(xml = virInterfaceDefFormat(srcObj->def)))
        goto error;

    if (!(backup = virInterfaceDefParseString(xml)))
        goto error;
    VIR_FREE(xml);

    if (!(obj = virInterfaceObjListAssignDef(data->dest, backup)))
        goto error;
    virInterfaceObjEndAPI(&obj);

    virObjectUnlock(srcObj);
    return 0;

 error:
    data->error = true;
    VIR_FREE(xml);
    virInterfaceDefFree(backup);
    virObjectUnlock(srcObj);
    return 0;
}


virInterfaceObjListPtr
virInterfaceObjListClone(virInterfaceObjListPtr interfaces)
{
    struct _virInterfaceObjListCloneData data = { .error = false,
                                                  .dest = NULL };

    if (!interfaces)
        return NULL;

    if (!(data.dest = virInterfaceObjListNew()))
        return NULL;

    virObjectRWLockRead(interfaces);
    virHashForEach(interfaces->objsName, virInterfaceObjListCloneCb, &data);
    virObjectRWUnlock(interfaces);

    if (data.error)
        goto error;

    return data.dest;

 error:
    virObjectUnref(data.dest);
    return NULL;
}


virInterfaceObjPtr
virInterfaceObjListAssignDef(virInterfaceObjListPtr interfaces,
                             virInterfaceDefPtr def)
{
    virInterfaceObjPtr obj;

    virObjectRWLockWrite(interfaces);
    if ((obj = virInterfaceObjListFindByNameLocked(interfaces, def->name))) {
        virInterfaceDefFree(obj->def);
        obj->def = def;
        virObjectRWUnlock(interfaces);

        return obj;
    }

    if (!(obj = virInterfaceObjNew()))
        return NULL;

    if (virHashAddEntry(interfaces->objsName, def->name, obj) < 0)
        goto error;
    virObjectRef(obj);

    obj->def = def;
    virObjectRWUnlock(interfaces);

    return obj;

 error:
    virInterfaceObjEndAPI(&obj);
    virObjectRWUnlock(interfaces);
    return NULL;
}


void
virInterfaceObjListRemove(virInterfaceObjListPtr interfaces,
                          virInterfaceObjPtr obj)
{
    virObjectRef(obj);
    virObjectUnlock(obj);
    virObjectRWLockWrite(interfaces);
    virObjectLock(obj);
    virHashRemoveEntry(interfaces->objsName, obj->def->name);
    virObjectUnlock(obj);
    virObjectUnref(obj);
    virObjectRWUnlock(interfaces);
}


static int
virInterfaceObjListForEachCb(void *payload,
                             const void *name ATTRIBUTE_UNUSED,
                             void *opaque)
{
    virInterfaceObjPtr obj = payload;
    struct _virInterfaceObjForEachData *data = opaque;

    if (data->error)
        return 0;

    if (data->maxElems >= 0 && data->nElems == data->maxElems)
        return 0;

    virObjectLock(obj);

    if (data->wantActive != virInterfaceObjIsActive(obj))
        goto cleanup;

    if (data->elems) {
        if (VIR_STRDUP(data->elems[data->nElems], obj->def->name) < 0) {
            data->error = true;
            goto cleanup;
        }
    }

    data->nElems++;

 cleanup:
    virObjectUnlock(obj);
    return 0;
}


int
virInterfaceObjListNumOfInterfaces(virInterfaceObjListPtr interfaces,
                                   bool wantActive)
{
    struct _virInterfaceObjForEachData data = { .wantActive = wantActive,
                                                .error = false,
                                                .nElems = 0,
                                                .maxElems = -1,
                                                .elems = NULL };

    virObjectRWLockRead(interfaces);
    virHashForEach(interfaces->objsName, virInterfaceObjListForEachCb, &data);
    virObjectRWUnlock(interfaces);

    return data.nElems;
}


int
virInterfaceObjListGetNames(virInterfaceObjListPtr interfaces,
                            bool wantActive,
                            char **const names,
                            int maxnames)
{
    struct _virInterfaceObjForEachData data = { .wantActive = wantActive,
                                                .error = false,
                                                .nElems = 0,
                                                .maxElems = maxnames,
                                                .elems = names };

    virObjectRWLockRead(interfaces);
    virHashForEach(interfaces->objsName, virInterfaceObjListForEachCb, &data);
    virObjectRWUnlock(interfaces);

    if (data.error)
        goto error;

    return data.nElems;

 error:
    while (--data.nElems >= 0)
        VIR_FREE(data.elems[data.nElems]);

    return -1;
}
