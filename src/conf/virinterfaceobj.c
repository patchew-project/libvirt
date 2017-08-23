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
    virObjectLookupHash parent;
};

/* virInterfaceObj manipulation */

static virClassPtr virInterfaceObjClass;
static void virInterfaceObjDispose(void *obj);

static int
virInterfaceObjOnceInit(void)
{
    if (!(virInterfaceObjClass = virClassNew(virClassForObjectLockable(),
                                             "virInterfaceObj",
                                             sizeof(virInterfaceObj),
                                             virInterfaceObjDispose)))
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
    return virObjectLookupHashNew(virClassForObjectLookupHash(), 10,
                                  VIR_OBJECT_LOOKUP_HASH_NAME);
}


static int
virInterfaceObjListFindByMACStringCb(void *payload,
                                     const void *name ATTRIBUTE_UNUSED,
                                     void *opaque)
{
    virInterfaceObjPtr obj = payload;
    virObjectLookupHashForEachDataPtr data = opaque;
    char **const matches = (char **const)data->elems;
    virInterfaceDefPtr def;
    int ret = -1;

    if (data->error)
        return 0;

    virObjectLock(obj);
    def = obj->def;
    if (STRCASEEQ(def->mac, data->matchStr)) {
        if (data->nElems < data->maxElems) {
            if (VIR_STRDUP(matches[data->nElems], def->name) < 0) {
                data->error = true;
                goto cleanup;
            }
            data->nElems++;
        }
    }
    ret = 0;

 cleanup:
    virObjectUnlock(obj);
    return ret;
}


int
virInterfaceObjListFindByMACString(virInterfaceObjListPtr interfaces,
                                   const char *mac,
                                   char **const matches,
                                   int maxmatches)
{
    virObjectLookupHashForEachData data = {
        .error = false, .matchStr = mac, .nElems = 0,
        .elems = (void **)matches, .maxElems = maxmatches };

    return virObjectLookupHashForEachName(interfaces,
                                          virInterfaceObjListFindByMACStringCb,
                                          &data);
}


static virInterfaceObjPtr
virInterfaceObjListFindByNameLocked(virInterfaceObjListPtr interfaces,
                                    const char *name)
{
    return virObjectLookupHashFindLocked(interfaces, name);
}


virInterfaceObjPtr
virInterfaceObjListFindByName(virInterfaceObjListPtr interfaces,
                              const char *name)
{
    return virObjectLookupHashFind(interfaces, name);
}


void
virInterfaceObjListFree(virInterfaceObjListPtr interfaces)
{
    virObjectUnref(interfaces);
}


static int
virInterfaceObjListCloneCb(void *dstHashTable,
                           void *sourceObject)
{
    virInterfaceObjListPtr dest = dstHashTable;
    virInterfaceObjPtr srcObj = sourceObject;
    int ret = -1;
    char *xml = NULL;
    virInterfaceObjPtr obj;
    virInterfaceDefPtr backup = NULL;

    if (!(xml = virInterfaceDefFormat(srcObj->def)))
        goto cleanup;

    if (!(backup = virInterfaceDefParseString(xml)))
        goto cleanup;

    if (!(obj = virInterfaceObjListAssignDef(dest, backup)))
        goto cleanup;

    ret = 0;

 cleanup:
    VIR_FREE(xml);
    virInterfaceDefFree(backup);

    return ret;
}


virInterfaceObjListPtr
virInterfaceObjListClone(virInterfaceObjListPtr interfaces)
{
    virInterfaceObjListPtr destInterfaces = NULL;

    if (!interfaces)
        return NULL;

    if (!(destInterfaces = virInterfaceObjListNew()))
        return NULL;

    if (virObjectLookupHashClone(interfaces, destInterfaces,
                                 virInterfaceObjListCloneCb) < 0)
        goto cleanup;

    return destInterfaces;

 cleanup:
    virObjectUnref(destInterfaces);
    return NULL;
}


virInterfaceObjPtr
virInterfaceObjListAssignDef(virInterfaceObjListPtr interfaces,
                             virInterfaceDefPtr def)
{
    virInterfaceObjPtr obj;
    virInterfaceObjPtr ret = NULL;

    virObjectRWLockWrite(interfaces);

    if ((obj = virInterfaceObjListFindByNameLocked(interfaces, def->name))) {
        virInterfaceDefFree(obj->def);
        obj->def = def;
    } else {
        if (!(obj = virInterfaceObjNew()))
            goto cleanup;

        if (virObjectLookupHashAdd(interfaces, obj, NULL, def->name) < 0)
            goto cleanup;
        obj->def = def;
    }

    ret = obj;
    obj = NULL;

 cleanup:
    virInterfaceObjEndAPI(&obj);
    virObjectRWUnlock(interfaces);
    return ret;
}


void
virInterfaceObjListRemove(virInterfaceObjListPtr interfaces,
                          virInterfaceObjPtr obj)
{
    if (!obj)
        return;

    /* @obj is locked upon entry */
    virObjectLookupHashRemove(interfaces, obj, NULL, obj->def->name);
}


static int
virInterfaceObjListGetHelper(void *payload,
                             const void *name ATTRIBUTE_UNUSED,
                             void *opaque)
{
    virInterfaceObjPtr obj = payload;
    virObjectLookupHashForEachDataPtr data = opaque;
    char **const names = (char **const)data->elems;
    virInterfaceDefPtr def;

    if (data->error)
        return 0;

    if (data->maxElems >= 0 && data->nElems == data->maxElems)
        return 0;

    virObjectLock(obj);
    def = obj->def;
    if (data->wantActive == virInterfaceObjIsActive(obj)) {
        if (names && VIR_STRDUP(names[data->nElems], def->name) < 0) {
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
virInterfaceObjListNumOfInterfaces(virInterfaceObjListPtr interfaces,
                                   bool wantActive)
{
    virObjectLookupHashForEachData data = {
        .wantActive = wantActive, .error = false, .nElems = 0,
        .elems = NULL, .maxElems = -2 };

    return virObjectLookupHashForEachName(interfaces,
                                          virInterfaceObjListGetHelper,
                                          &data);
}


int
virInterfaceObjListGetNames(virInterfaceObjListPtr interfaces,
                            bool wantActive,
                            char **const names,
                            int maxnames)
{
    virObjectLookupHashForEachData data = {
        .wantActive = wantActive, .error = false, .nElems = 0,
        .elems = (void **)names, .maxElems = maxnames };

    return virObjectLookupHashForEachName(interfaces,
                                          virInterfaceObjListGetHelper,
                                          &data);
}
