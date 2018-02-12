/*
 * virnwfilterobj.c: network filter object processing
 *                   (derived from nwfilter_conf.c)
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
#include <dirent.h>

#include "datatypes.h"

#include "viralloc.h"
#include "virerror.h"
#include "virfile.h"
#include "virlog.h"
#include "virnwfilterobj.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NWFILTER

VIR_LOG_INIT("conf.virnwfilterobj");

struct _virNWFilterObj {
    virObjectLockable parent;

    bool wantRemoved;

    virNWFilterDefPtr def;
    virNWFilterDefPtr newDef;
};

static virClassPtr virNWFilterObjClass;
static virClassPtr virNWFilterObjListClass;
static void virNWFilterObjDispose(void *obj);
static void virNWFilterObjListDispose(void *obj);

struct _virNWFilterObjList {
    virObjectRWLockable parent;

    /* uuid string -> virNWFilterObj  mapping
     * for O(1), lockless lookup-by-uuid */
    virHashTable *objs;

    /* name -> virNWFilterObj mapping for O(1),
     * lockless lookup-by-name */
    virHashTable *objsName;
};

static int virNWFilterObjOnceInit(void)
{
    if (!(virNWFilterObjClass = virClassNew(virClassForObjectLockable(),
                                            "virNWFilterObj",
                                            sizeof(virNWFilterObj),
                                            virNWFilterObjDispose)))
        return -1;

    if (!(virNWFilterObjListClass = virClassNew(virClassForObjectRWLockable(),
                                                "virNWFilterObjList",
                                                sizeof(virNWFilterObjList),
                                                virNWFilterObjListDispose)))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(virNWFilterObj)


static virNWFilterObjPtr
virNWFilterObjNew(void)
{
    virNWFilterObjPtr obj;

    if (virNWFilterObjInitialize() < 0)
        return NULL;

    if (!(obj = virObjectRecursiveLockableNew(virNWFilterObjClass)))
        return NULL;

    virObjectLock(obj);
    return obj;
}


static void
virNWFilterObjDispose(void *opaque)
{
    virNWFilterObjPtr obj = opaque;

    virNWFilterDefFree(obj->def);
    virNWFilterDefFree(obj->newDef);
}


void
virNWFilterObjEndAPI(virNWFilterObjPtr *obj)
{
    if (!*obj)
        return;

    virObjectUnlock(*obj);
    virObjectUnref(*obj);
    *obj = NULL;
}


virNWFilterDefPtr
virNWFilterObjGetDef(virNWFilterObjPtr obj)
{
    return obj->def;
}


virNWFilterDefPtr
virNWFilterObjGetNewDef(virNWFilterObjPtr obj)
{
    return obj->newDef;
}


bool
virNWFilterObjWantRemoved(virNWFilterObjPtr obj)
{
    return obj->wantRemoved;
}


static void
virNWFilterObjListDispose(void *obj)
{

    virNWFilterObjListPtr nwfilters = obj;

    virHashFree(nwfilters->objs);
    virHashFree(nwfilters->objsName);
}


virNWFilterObjListPtr
virNWFilterObjListNew(void)
{
    virNWFilterObjListPtr nwfilters;

    if (virNWFilterObjInitialize() < 0)
        return NULL;

    if (!(nwfilters = virObjectRWLockableNew(virNWFilterObjListClass)))
        return NULL;

    if (!(nwfilters->objs = virHashCreate(10, virObjectFreeHashData)) ||
        !(nwfilters->objsName = virHashCreate(10, virObjectFreeHashData))) {
        virObjectUnref(nwfilters);
        return NULL;
    }

    return nwfilters;
}


void
virNWFilterObjListRemove(virNWFilterObjListPtr nwfilters,
                         virNWFilterObjPtr obj)
{
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(obj->def->uuid, uuidstr);
    virObjectRef(obj);
    virObjectUnlock(obj);

    virObjectRWLockWrite(nwfilters);
    virObjectLock(obj);
    virHashRemoveEntry(nwfilters->objs, uuidstr);
    virHashRemoveEntry(nwfilters->objsName, obj->def->name);
    virObjectUnlock(obj);
    virObjectUnref(obj);
    virObjectRWUnlock(nwfilters);
}


static virNWFilterObjPtr
virNWFilterObjListFindByUUIDLocked(virNWFilterObjListPtr nwfilters,
                                   const unsigned char *uuid)
{
    virNWFilterObjPtr obj = NULL;
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(uuid, uuidstr);

    obj = virHashLookup(nwfilters->objs, uuidstr);
    if (obj)
        virObjectRef(obj);
    return obj;
}


virNWFilterObjPtr
virNWFilterObjListFindByUUID(virNWFilterObjListPtr nwfilters,
                             const unsigned char *uuid)
{
    virNWFilterObjPtr obj;

    virObjectRWLockRead(nwfilters);
    obj = virNWFilterObjListFindByUUIDLocked(nwfilters, uuid);
    virObjectRWUnlock(nwfilters);
    if (obj)
        virObjectLock(obj);
    return obj;
}


static virNWFilterObjPtr
virNWFilterObjListFindByNameLocked(virNWFilterObjListPtr nwfilters,
                                   const char *name)
{
    virNWFilterObjPtr obj;

    obj = virHashLookup(nwfilters->objsName, name);
    if (obj)
        virObjectRef(obj);
    return obj;
}


virNWFilterObjPtr
virNWFilterObjListFindByName(virNWFilterObjListPtr nwfilters,
                             const char *name)
{
    virNWFilterObjPtr obj;

    virObjectRWLockRead(nwfilters);
    obj = virNWFilterObjListFindByNameLocked(nwfilters, name);
    virObjectRWUnlock(nwfilters);
    if (obj)
        virObjectLock(obj);
    return obj;
}


virNWFilterObjPtr
virNWFilterObjListFindInstantiateFilter(virNWFilterObjListPtr nwfilters,
                                        const char *filtername)
{
    virNWFilterObjPtr obj;

    if (!(obj = virNWFilterObjListFindByName(nwfilters, filtername))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("referenced filter '%s' is missing"), filtername);
        return NULL;
    }

    if (virNWFilterObjWantRemoved(obj)) {
        virReportError(VIR_ERR_NO_NWFILTER,
                       _("Filter '%s' is in use."), filtername);
        virNWFilterObjEndAPI(&obj);
        return NULL;
    }

    return obj;
}


static int
_virNWFilterObjListDefLoopDetect(virNWFilterObjListPtr nwfilters,
                                 virNWFilterDefPtr def,
                                 const char *filtername)
{
    int rc = 0;
    size_t i;
    virNWFilterEntryPtr entry;
    virNWFilterObjPtr obj;

    if (!def)
        return 0;

    for (i = 0; i < def->nentries; i++) {
        entry = def->filterEntries[i];
        if (entry->include) {

            if (STREQ(filtername, entry->include->filterref)) {
                rc = -1;
                break;
            }

            obj = virNWFilterObjListFindByNameLocked(nwfilters,
                                                     entry->include->filterref);
            if (obj) {
                virObjectLock(obj);
                rc = _virNWFilterObjListDefLoopDetect(nwfilters, obj->def,
                                                      filtername);
                virNWFilterObjEndAPI(&obj);
                if (rc < 0)
                    break;
            }
        }
    }

    return rc;
}


/*
 * virNWFilterObjListDefLoopDetect:
 * @nwfilters : the nwfilters to search
 * @def : the filter definition that may add a loop and is to be tested
 *
 * Detect a loop introduced through the filters being able to
 * reference each other.
 *
 * Returns 0 in case no loop was detected, -1 otherwise.
 */
static int
virNWFilterObjListDefLoopDetect(virNWFilterObjListPtr nwfilters,
                                virNWFilterDefPtr def)
{
    return _virNWFilterObjListDefLoopDetect(nwfilters, def, def->name);
}


int
virNWFilterObjTestUnassignDef(virNWFilterObjPtr obj)
{
    int rc = 0;

    obj->wantRemoved = true;
    /* trigger the update on VMs referencing the filter */
    if (virNWFilterTriggerVMFilterRebuild() < 0)
        rc = -1;

    obj->wantRemoved = false;

    return rc;
}


static bool
virNWFilterDefEqual(const virNWFilterDef *def1,
                    virNWFilterDefPtr def2)
{
    bool ret = false;
    char *xml1 = NULL;
    char *xml2 = NULL;

    if (!(xml1 = virNWFilterDefFormat(def1)) ||
        !(xml2 = virNWFilterDefFormat(def2)))
        goto cleanup;

    ret = STREQ(xml1, xml2);

 cleanup:
    VIR_FREE(xml1);
    VIR_FREE(xml2);

    return ret;
}


static virNWFilterObjPtr
virNWFilterObjListAssignDefLocked(virNWFilterObjListPtr nwfilters,
                                  virNWFilterDefPtr def)
{
    virNWFilterObjPtr obj = NULL;
    virNWFilterObjPtr ret = NULL;
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(def->uuid, uuidstr);

    if ((obj = virNWFilterObjListFindByUUIDLocked(nwfilters, def->uuid))) {
        virObjectLock(obj);

        if (STRNEQ(def->name, obj->def->name)) {
            virReportError(VIR_ERR_OPERATION_FAILED,
                           _("filter with same UUID but different name "
                             "('%s') already exists"),
                           obj->def->name);
            goto cleanup;
        }
    } else {
        if ((obj = virNWFilterObjListFindByNameLocked(nwfilters, def->name))) {
            virObjectLock(obj);

            virUUIDFormat(obj->def->uuid, uuidstr);
            virReportError(VIR_ERR_OPERATION_FAILED,
                           _("filter '%s' already exists with UUID %s"),
                           def->name, uuidstr);
            goto cleanup;
        }
    }

    virNWFilterObjEndAPI(&obj);

    if (virNWFilterObjListDefLoopDetect(nwfilters, def) < 0) {
        virReportError(VIR_ERR_OPERATION_FAILED,
                       "%s", _("filter would introduce a loop"));
        goto cleanup;
    }


    if ((obj = virNWFilterObjListFindByNameLocked(nwfilters, def->name))) {
        virObjectLock(obj);

        if (virNWFilterDefEqual(def, obj->def)) {
            virNWFilterDefFree(obj->def);
            obj->def = def;
            return obj;
        }

        obj->newDef = def;
        /* trigger the update on VMs referencing the filter */
        if (virNWFilterTriggerVMFilterRebuild() < 0) {
            obj->newDef = NULL;
            virNWFilterObjEndAPI(&obj);
            return NULL;
        }

        virNWFilterDefFree(obj->def);
        obj->def = def;
        obj->newDef = NULL;
        return obj;
    }

    if (!(obj = virNWFilterObjNew()))
        return NULL;

    if (virHashAddEntry(nwfilters->objs, uuidstr, obj) < 0)
        goto cleanup;

    if (virHashAddEntry(nwfilters->objsName, def->name, obj) < 0) {
        virHashRemoveEntry(nwfilters->objs, uuidstr);
        goto cleanup;
    }
    virObjectRef(obj);

    /* Increase refcounter again. We need two references for the
     * hash tables above and one to return to the caller. */
    virObjectRef(obj);
    obj->def = def;

    ret = obj;
    obj = NULL;

 cleanup:
    virNWFilterObjEndAPI(&obj);
    return ret;
}


virNWFilterObjPtr
virNWFilterObjListAssignDef(virNWFilterObjListPtr nwfilters,
                            virNWFilterDefPtr def)
{
    virNWFilterObjPtr obj;

    virObjectRWLockWrite(nwfilters);
    obj = virNWFilterObjListAssignDefLocked(nwfilters, def);
    virObjectRWUnlock(nwfilters);
    return obj;
}


struct virNWFilterObjListData {
    virNWFilterObjListFilter filter;
    virConnectPtr conn;
    int count;
};


static int
virNWFilterObjListCount(void *payload,
                        const void *name ATTRIBUTE_UNUSED,
                        void *opaque)
{
    virNWFilterObjPtr obj = payload;
    struct virNWFilterObjListData *data = opaque;

    virObjectLock(obj);
    if (!data->filter ||
        data->filter(data->conn, obj->def))
        data->count++;
    virObjectUnlock(obj);
    return 0;
}


int
virNWFilterObjListNumOfNWFilters(virNWFilterObjListPtr nwfilters,
                                 virConnectPtr conn,
                                 virNWFilterObjListFilter filter)
{
    struct virNWFilterObjListData data = { filter, conn, 0 };

    virObjectRWLockRead(nwfilters);
    virHashForEach(nwfilters->objs, virNWFilterObjListCount, &data);
    virObjectRWUnlock(nwfilters);
    return data.count;
}


struct virNWFilterNameData {
    virNWFilterObjListFilter filter;
    virConnectPtr conn;
    int oom;
    int numnames;
    int maxnames;
    char **const names;
};


static int
virNWFilterObjListCopyNames(void *payload,
                            const void *name ATTRIBUTE_UNUSED,
                            void *opaque)
{
    virNWFilterObjPtr obj = payload;
    struct virNWFilterNameData *data = opaque;

    if (data->oom)
        return 0;

    virObjectLock(obj);
    if (data->filter &&
        !data->filter(data->conn, obj->def))
        goto cleanup;
    if (data->numnames < data->maxnames) {
        if (VIR_STRDUP(data->names[data->numnames], obj->def->name) < 0)
            data->oom = 1;
        else
            data->numnames++;
    }
 cleanup:
    virObjectUnlock(obj);
    return 0;
}


int
virNWFilterObjListGetNames(virNWFilterObjListPtr nwfilters,
                           virConnectPtr conn,
                           virNWFilterObjListFilter filter,
                           char **const names,
                           int maxnames)
{
    struct virNWFilterNameData data = {filter, conn, 0, 0, maxnames, names};
    size_t i;

    virObjectRWLockRead(nwfilters);
    virHashForEach(nwfilters->objs, virNWFilterObjListCopyNames, &data);
    virObjectRWUnlock(nwfilters);
    if (data.oom) {
        for (i = 0; i < data.numnames; i++)
            VIR_FREE(data.names[i]);
        return -1;
    }

    return data.numnames;
}


struct virNWFilterListData {
    virConnectPtr conn;
    virNWFilterPtr *nwfilters;
    virNWFilterObjListFilter filter;
    int nnwfilters;
    bool error;
};


static int
virNWFilterObjListPopulate(void *payload,
                           const void *name ATTRIBUTE_UNUSED,
                           void *opaque)
{
    struct virNWFilterListData *data = opaque;
    virNWFilterObjPtr obj = payload;
    virNWFilterPtr nwfilter = NULL;

    if (data->error)
        return 0;

    virObjectLock(obj);

    if (data->filter &&
        !data->filter(data->conn, obj->def))
        goto cleanup;

    if (!data->nwfilters) {
        data->nnwfilters++;
        goto cleanup;
    }

    if (!(nwfilter = virGetNWFilter(data->conn, obj->def->name, obj->def->uuid))) {
        data->error = true;
        goto cleanup;
    }

    data->nwfilters[data->nnwfilters++] = nwfilter;

 cleanup:
    virObjectUnlock(obj);
    return 0;
}


int
virNWFilterObjListExport(virConnectPtr conn,
                         virNWFilterObjListPtr nwfilters,
                         virNWFilterPtr **filters,
                         virNWFilterObjListFilter filter)
{
    int ret = -1;
    struct virNWFilterListData data = {.conn = conn, .nwfilters = NULL,
        .filter = filter, .nnwfilters = 0, .error = false};

    virObjectRWLockRead(nwfilters);
    if (filters && VIR_ALLOC_N(data.nwfilters, virHashSize(nwfilters->objs) + 1) < 0)
        goto cleanup;

    virHashForEach(nwfilters->objs, virNWFilterObjListPopulate, &data);

    if (data.error)
        goto cleanup;

    if (data.nnwfilters) {
        /* trim the array to the final size */
        ignore_value(VIR_REALLOC_N(data.nwfilters, data.nnwfilters + 1));
        *filters = data.nwfilters;
        data.nwfilters = NULL;
    }

    ret = data.nnwfilters;
 cleanup:
    virObjectRWUnlock(nwfilters);
    while (data.nwfilters && data.nnwfilters)
        virObjectUnref(data.nwfilters[--data.nnwfilters]);

    VIR_FREE(data.nwfilters);
    return ret;
}


static virNWFilterObjPtr
virNWFilterObjListLoadConfig(virNWFilterObjListPtr nwfilters,
                             const char *configDir,
                             const char *name)
{
    virNWFilterDefPtr def = NULL;
    virNWFilterObjPtr obj;
    char *configFile = NULL;

    if (!(configFile = virFileBuildPath(configDir, name, ".xml")))
        goto error;

    if (!(def = virNWFilterDefParseFile(configFile)))
        goto error;

    if (STRNEQ(name, def->name)) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("network filter config filename '%s' "
                         "does not match name '%s'"),
                       configFile, def->name);
        goto error;
    }

    /* We generated a UUID, make it permanent by saving the config to disk */
    if (!def->uuid_specified &&
        virNWFilterSaveConfig(configDir, def) < 0)
        goto error;

    if (!(obj = virNWFilterObjListAssignDef(nwfilters, def)))
        goto error;

    VIR_FREE(configFile);
    return obj;

 error:
    VIR_FREE(configFile);
    virNWFilterDefFree(def);
    return NULL;
}


int
virNWFilterObjListLoadAllConfigs(virNWFilterObjListPtr nwfilters,
                                 const char *configDir)
{
    DIR *dir;
    struct dirent *entry;
    int ret = -1;
    int rc;

    if ((rc = virDirOpenIfExists(&dir, configDir)) <= 0)
        return rc;

    while ((ret = virDirRead(dir, &entry, configDir)) > 0) {
        virNWFilterObjPtr obj;

        if (!virFileStripSuffix(entry->d_name, ".xml"))
            continue;

        obj = virNWFilterObjListLoadConfig(nwfilters, configDir, entry->d_name);
        virNWFilterObjEndAPI(&obj);
    }

    VIR_DIR_CLOSE(dir);
    return ret;
}
