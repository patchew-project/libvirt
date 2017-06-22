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
    virObjectLookupKeys parent;

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
    if (!(virInterfaceObjClass = virClassNew(virClassForObjectLookupKeys(),
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
virInterfaceObjNew(virInterfaceDefPtr def)
{
    virInterfaceObjPtr obj;

    if (virInterfaceObjInitialize() < 0)
        return NULL;

    if (!(obj = virObjectLookupKeysNew(virInterfaceObjClass, NULL, def->name)))
        return NULL;

    virObjectLock(obj);
    obj->def = def;

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
    return virObjectLookupKeysIsActive(obj);
}


void
virInterfaceObjSetActive(virInterfaceObjPtr obj,
                         bool active)
{
    virObjectLookupKeysSetActive(obj, active);
}


/* virInterfaceObjList manipulation */
virInterfaceObjListPtr
virInterfaceObjListNew(void)
{
    return virObjectLookupHashNew(virClassForObjectLookupHash(), 10);
}


static int
virInterfaceObjListFindByMACStringCallback(void *payload,
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
    if (STRCASEEQ(def->mac, data->matchstr)) {
        if (data->nelems < data->maxelems) {
            if (VIR_STRDUP(matches[data->nelems], def->name) < 0) {
                data->error = true;
                goto cleanup;
            }
            data->nelems++;
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
        .error = false, .matchstr = mac, .nelems = 0,
        .elems = (void **)matches, .maxelems = maxmatches };

    return virObjectLookupHashForEach(interfaces, false,
                                      virInterfaceObjListFindByMACStringCallback,
                                      &data);
}


static virInterfaceObjPtr
virInterfaceObjListFindByNameLocked(virInterfaceObjListPtr interfaces,
                                    const char *name)
{
    virObjectLookupKeysPtr obj = virObjectLookupHashFind(interfaces, false,
                                                         name);
    return virObjectRef((virInterfaceObjPtr)obj);
}


virInterfaceObjPtr
virInterfaceObjListFindByName(virInterfaceObjListPtr interfaces,
                              const char *name)
{
    virInterfaceObjPtr obj;

    virObjectLock(interfaces);
    obj = virInterfaceObjListFindByNameLocked(interfaces, name);
    virObjectUnlock(interfaces);
    if (obj)
        virObjectLock(obj);

    return obj;
}


void
virInterfaceObjListFree(virInterfaceObjListPtr interfaces)
{
    virObjectUnref(interfaces);
}


struct interfaceCloneData {
    const char *primaryKey;
    virInterfaceObjListPtr dest;
    bool error;
};

static int
virInterfaceObjListCloneCallback(void *payload,
                                 const void *name ATTRIBUTE_UNUSED,
                                 void *opaque)
{
    virInterfaceObjPtr srcobj = payload;
    struct interfaceCloneData *data = opaque;
    int ret = -1;
    char *xml = NULL;
    virInterfaceObjPtr obj;
    virInterfaceDefPtr backup = NULL;

    if (data->error)
        return 0;

    virObjectLock(srcobj);
    if (!(xml = virInterfaceDefFormat(srcobj->def)))
        goto error;

    if (!(backup = virInterfaceDefParseString(xml)))
        goto error;

    if (!(obj = virInterfaceObjListAssignDef(data->dest, backup)))
        goto error;

    virInterfaceObjEndAPI(&obj);
    ret = 0;

 cleanup:
    VIR_FREE(xml);
    virInterfaceDefFree(backup);

    return ret;

 error:
    data->error = true;
    goto cleanup;
}


virInterfaceObjListPtr
virInterfaceObjListClone(virInterfaceObjListPtr interfaces)
{
    virHashTablePtr objsName;
    struct interfaceCloneData data = { .error = false };

    if (!interfaces)
        return NULL;

    if (!(data.dest = virInterfaceObjListNew()))
        return NULL;

    virObjectLock(interfaces);
    objsName = virObjectLookupHashGetName(interfaces);
    virHashForEach(objsName, virInterfaceObjListCloneCallback, &data);
    virObjectUnlock(interfaces);

    if (data.error)
        goto cleanup;

    return data.dest;

 cleanup:
    virObjectUnref(data.dest);
     return NULL;
}


virInterfaceObjPtr
virInterfaceObjListAssignDef(virInterfaceObjListPtr interfaces,
                             virInterfaceDefPtr def)
{
    virInterfaceObjPtr obj;
    virInterfaceObjPtr ret = NULL;

    virObjectLock(interfaces);

    if ((obj = virInterfaceObjListFindByNameLocked(interfaces, def->name))) {
        virObjectLock(obj);
        virInterfaceDefFree(obj->def);
        obj->def = def;
    } else {
        if (!(obj = virInterfaceObjNew(def)))
            goto cleanup;

        if (virObjectLookupHashAdd(interfaces,
                                   (virObjectLookupKeysPtr)obj) < 0) {
            obj->def = NULL;
            virInterfaceObjEndAPI(&obj);
            goto cleanup;
        }
    }

    ret = obj;

 cleanup:
    virObjectUnlock(interfaces);
    return ret;
}


void
virInterfaceObjListRemove(virInterfaceObjListPtr interfaces,
                          virInterfaceObjPtr obj)
{
    virObjectLookupHashRemove(interfaces, (virObjectLookupKeysPtr)obj);
}


static int
virInterfaceObjListNumOfInterfacesCb(void *payload,
                                     const void *name ATTRIBUTE_UNUSED,
                                     void *opaque)
{
    virInterfaceObjPtr obj = payload;
    virObjectLookupHashForEachDataPtr data = opaque;

    virObjectLock(obj);
    if (data->wantActive == virInterfaceObjIsActive(obj))
        data->nelems++;
    virObjectUnlock(obj);

    return 0;
}


int
virInterfaceObjListNumOfInterfaces(virInterfaceObjListPtr interfaces,
                                   bool wantActive)
{
    virObjectLookupHashForEachData data = {
        .wantActive = wantActive, .nelems = 0, .error = false };

    return virObjectLookupHashForEach(interfaces, false,
                                      virInterfaceObjListNumOfInterfacesCb,
                                      &data);
}


static int
virInterfaceObjListGetNamesCb(void *payload,
                              const void *name ATTRIBUTE_UNUSED,
                              void *opaque)
{
    virInterfaceObjPtr obj = payload;
    virObjectLookupHashForEachDataPtr data = opaque;
    char **const names = (char **const)data->elems;
    virInterfaceDefPtr def;

    if (data->error)
        return 0;

    if (data->maxelems >= 0 && data->nelems == data->maxelems)
        return 0;

    virObjectLock(obj);
    def = obj->def;
    if (data->wantActive == virInterfaceObjIsActive(obj)) {
        if (VIR_STRDUP(names[data->nelems], def->name) < 0) {
            data->error = true;
            goto cleanup;
        }
        data->nelems++;
     }

 cleanup:
    virObjectUnlock(obj);
    return 0;
}


int
virInterfaceObjListGetNames(virInterfaceObjListPtr interfaces,
                            bool wantActive,
                            char **const names,
                            int maxnames)
{
    virObjectLookupHashForEachData data = {
        .wantActive = wantActive, .error = false, .nelems = 0,
        .elems = (void **)names, .maxelems = maxnames };

    return virObjectLookupHashForEach(interfaces, false,
                                      virInterfaceObjListGetNamesCb, &data);
}
