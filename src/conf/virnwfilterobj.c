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
#include "viratomic.h"
#include "virerror.h"
#include "virfile.h"
#include "virlog.h"
#include "virnwfilterobj.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NWFILTER

VIR_LOG_INIT("conf.virnwfilterobj");

static void
virNWFilterObjLock(virNWFilterObjPtr obj);

static void
virNWFilterObjUnlock(virNWFilterObjPtr obj);

struct _virNWFilterObj {
    virMutex lock;
    int refs;

    bool wantRemoved;

    virNWFilterDefPtr def;
    virNWFilterDefPtr newDef;
};

struct _virNWFilterObjList {
    virObjectLockable parent;

    /* uuid string -> virNWFilterObj  mapping
     * for O(1), lockless lookup-by-uuid */
    virHashTable *objs;

    /* name -> virNWFilterObj mapping for O(1),
     * lockless lookup-by-name */
    virHashTable *objsName;
};

static virClassPtr virNWFilterObjListClass;
static void virNWFilterObjListDispose(void *opaque);

static int
virNWFilterObjOnceInit(void)
{
    if (!(virNWFilterObjListClass = virClassNew(virClassForObjectLockable(),
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

    if (VIR_ALLOC(obj) < 0)
        return NULL;

    if (virMutexInitRecursive(&obj->lock) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("cannot initialize mutex"));
        VIR_FREE(obj);
        return NULL;
    }

    virNWFilterObjLock(obj);
    virAtomicIntSet(&obj->refs, 1);

    return obj;
}


void
virNWFilterObjEndAPI(virNWFilterObjPtr *obj)
{
    if (!*obj)
        return;

    virNWFilterObjUnlock(*obj);
    virNWFilterObjUnref(*obj);
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
virNWFilterObjFree(virNWFilterObjPtr obj)
{
    if (!obj)
        return;

    virNWFilterDefFree(obj->def);
    virNWFilterDefFree(obj->newDef);

    virMutexDestroy(&obj->lock);

    VIR_FREE(obj);
}


virNWFilterObjPtr
virNWFilterObjRef(virNWFilterObjPtr obj)
{
    if (obj)
        virAtomicIntInc(&obj->refs);
    return obj;
}


bool
virNWFilterObjUnref(virNWFilterObjPtr obj)
{
    bool lastRef;
    if (!obj)
        return false;
    if ((lastRef = virAtomicIntDecAndTest(&obj->refs)))
        virNWFilterObjFree(obj);
    return !lastRef;
}


static void
virNWFilterObjListDispose(void *opaque)
{
    virNWFilterObjListPtr nwfilters = opaque;

    virHashFree(nwfilters->objs);
    virHashFree(nwfilters->objsName);
}


void
virNWFilterObjListFree(virNWFilterObjListPtr nwfilters)
{
    virObjectUnref(nwfilters);
}


static void
virNWFilterObjListObjsFreeData(void *payload,
                               const void *name ATTRIBUTE_UNUSED)
{
    virNWFilterObjPtr obj = payload;

    virNWFilterObjUnref(obj);
}


virNWFilterObjListPtr
virNWFilterObjListNew(void)
{
    virNWFilterObjListPtr nwfilters;

    if (virNWFilterObjInitialize() < 0)
        return NULL;

    if (!(nwfilters = virObjectLockableNew(virNWFilterObjListClass)))
        return NULL;

    if (!(nwfilters->objs = virHashCreate(10, virNWFilterObjListObjsFreeData))) {
        virObjectUnref(nwfilters);
        return NULL;
    }

    if (!(nwfilters->objsName = virHashCreate(10, virNWFilterObjListObjsFreeData))) {
        virHashFree(nwfilters->objs);
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
    virNWFilterDefPtr def;

    if (!obj)
        return;
    def = obj->def;

    virUUIDFormat(def->uuid, uuidstr);
    virNWFilterObjRef(obj);
    virNWFilterObjUnlock(obj);
    virObjectLock(nwfilters);
    virNWFilterObjLock(obj);
    virHashRemoveEntry(nwfilters->objs, uuidstr);
    virHashRemoveEntry(nwfilters->objsName, def->name);
    virNWFilterObjUnlock(obj);
    virNWFilterObjUnref(obj);
    virObjectUnlock(nwfilters);
}


static virNWFilterObjPtr
virNWFilterObjListFindByUUIDLocked(virNWFilterObjListPtr nwfilters,
                                   const unsigned char *uuid)
{
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(uuid, uuidstr);
    return virNWFilterObjRef(virHashLookup(nwfilters->objs, uuidstr));
}


virNWFilterObjPtr
virNWFilterObjListFindByUUID(virNWFilterObjListPtr nwfilters,
                             const unsigned char *uuid)
{
    virNWFilterObjPtr obj;

    virObjectLock(nwfilters);
    obj = virNWFilterObjListFindByUUIDLocked(nwfilters, uuid);
    virObjectUnlock(nwfilters);
    if (obj)
        virNWFilterObjLock(obj);
    return obj;
}


static virNWFilterObjPtr
virNWFilterObjListFindByNameLocked(virNWFilterObjListPtr nwfilters,
                                   const char *name)
{
    return virNWFilterObjRef(virHashLookup(nwfilters->objsName, name));
}


virNWFilterObjPtr
virNWFilterObjListFindByName(virNWFilterObjListPtr nwfilters,
                             const char *name)
{
    virNWFilterObjPtr obj;

    virObjectLock(nwfilters);
    obj = virNWFilterObjListFindByNameLocked(nwfilters, name);
    virObjectUnlock(nwfilters);
    if (obj)
        virNWFilterObjLock(obj);

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
 * NB: Enter with nwfilters locked
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


virNWFilterObjPtr
virNWFilterObjListAssignDef(virNWFilterObjListPtr nwfilters,
                            virNWFilterDefPtr def)
{
    virNWFilterObjPtr obj;
    virNWFilterDefPtr objdef;
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    virObjectLock(nwfilters);

    if ((obj = virNWFilterObjListFindByUUIDLocked(nwfilters, def->uuid))) {
        virNWFilterObjLock(obj);
        objdef = obj->def;

        if (STRNEQ(def->name, objdef->name)) {
            virReportError(VIR_ERR_OPERATION_FAILED,
                           _("filter with same UUID but different name "
                             "('%s') already exists"),
                           objdef->name);
            virNWFilterObjEndAPI(&obj);
            virObjectUnlock(nwfilters);
            return NULL;
        }
        virNWFilterObjEndAPI(&obj);
    } else {
        if ((obj = virNWFilterObjListFindByNameLocked(nwfilters, def->name))) {

            virNWFilterObjLock(obj);
            objdef = obj->def;
            virUUIDFormat(objdef->uuid, uuidstr);
            virReportError(VIR_ERR_OPERATION_FAILED,
                           _("filter '%s' already exists with uuid %s"),
                           def->name, uuidstr);
            virNWFilterObjEndAPI(&obj);
            virObjectUnlock(nwfilters);
            return NULL;
        }
    }

    if (virNWFilterObjListDefLoopDetect(nwfilters, def) < 0) {
        virReportError(VIR_ERR_OPERATION_FAILED,
                       "%s", _("filter would introduce a loop"));
        virObjectUnlock(nwfilters);
        return NULL;
    }


    if ((obj = virNWFilterObjListFindByNameLocked(nwfilters, def->name))) {
        virNWFilterObjLock(obj);
        objdef = obj->def;
        if (virNWFilterDefEqual(def, objdef)) {
            virNWFilterDefFree(objdef);
            obj->def = def;
            goto cleanup;
        }

        obj->newDef = def;
        /* trigger the update on VMs referencing the filter */
        if (virNWFilterTriggerVMFilterRebuild() < 0) {
            obj->newDef = NULL;
            virNWFilterObjEndAPI(&obj);
            virObjectUnlock(nwfilters);
            return NULL;
        }

        virNWFilterDefFree(objdef);
        obj->def = def;
        obj->newDef = NULL;
        goto cleanup;
    }

    if (!(obj = virNWFilterObjNew()))
        return NULL;

    virUUIDFormat(def->uuid, uuidstr);
    if (virHashAddEntry(nwfilters->objs, uuidstr, obj) < 0)
        goto error;
    virNWFilterObjRef(obj);

    if (virHashAddEntry(nwfilters->objsName, def->name, obj) < 0) {
        virHashRemoveEntry(nwfilters->objs, uuidstr);
        goto error;
    }

    obj->def = def;
    virNWFilterObjRef(obj);

 cleanup:
    virObjectUnlock(nwfilters);
    return obj;

 error:
    virNWFilterObjUnlock(obj);
    virNWFilterObjUnref(obj);
    virObjectUnlock(nwfilters);
    return NULL;
}


struct virNWFilterCountData {
    virConnectPtr conn;
    virNWFilterObjListFilter aclfilter;
    int nelems;
};

static int
virNWFilterObjListNumOfNWFiltersCallback(void *payload,
                                         const void *name ATTRIBUTE_UNUSED,
                                         void *opaque)
{
    virNWFilterObjPtr obj = payload;
    struct virNWFilterCountData *data = opaque;

    virObjectLock(obj);
    if (!data->aclfilter || data->aclfilter(data->conn, obj->def))
        data->nelems++;
    virObjectUnlock(obj);
    return 0;
}


int
virNWFilterObjListNumOfNWFilters(virNWFilterObjListPtr nwfilters,
                                 virConnectPtr conn,
                                 virNWFilterObjListFilter aclfilter)
{
    struct virNWFilterCountData data = { .conn = conn,
        .aclfilter = aclfilter, .nelems = 0 };

    virObjectLock(nwfilters);
    virHashForEach(nwfilters->objs, virNWFilterObjListNumOfNWFiltersCallback,
                   &data);
    virObjectUnlock(nwfilters);

    return data.nelems;
}


struct virNWFilterListData {
    virConnectPtr conn;
    virNWFilterObjListFilter aclfilter;
    int nelems;
    char **elems;
    int maxelems;
    bool error;
};

static int
virNWFilterObjListGetNamesCallback(void *payload,
                                   const void *name ATTRIBUTE_UNUSED,
                                   void *opaque)
{
    virNWFilterObjPtr obj = payload;
    virNWFilterDefPtr def;
    struct virNWFilterListData *data = opaque;

    if (data->error)
        return 0;

    if (data->maxelems >= 0 && data->nelems == data->maxelems)
        return 0;

    virObjectLock(obj);
    def = obj->def;

    if (!data->aclfilter || data->aclfilter(data->conn, def)) {
        if (VIR_STRDUP(data->elems[data->nelems], def->name) < 0) {
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
virNWFilterObjListGetNames(virNWFilterObjListPtr nwfilters,
                           virConnectPtr conn,
                           virNWFilterObjListFilter aclfilter,
                           char **const names,
                           int maxnames)
{
    struct virNWFilterListData data = { .conn = conn, .aclfilter = aclfilter,
        .nelems = 0, .elems = names, .maxelems = maxnames, .error = false };

    virObjectLock(nwfilters);
    virHashForEach(nwfilters->objs, virNWFilterObjListGetNamesCallback, &data);
    virObjectUnlock(nwfilters);

    if (data.error)
        goto error;

    return data.nelems;

 error:
    while (--data.nelems >= 0)
        VIR_FREE(data.elems[data.nelems]);
    return -1;
}


struct virNWFilterExportData {
    virConnectPtr conn;
    virNWFilterObjListFilter aclfilter;
    virNWFilterPtr *filters;
    int nfilters;
    bool error;
};

static int
virNWFilterObjListExportCallback(void *payload,
                                 const void *name ATTRIBUTE_UNUSED,
                                 void *opaque)
{
    virNWFilterObjPtr obj = payload;
    virNWFilterDefPtr def;
    struct virNWFilterExportData *data = opaque;
    virNWFilterPtr nwfilter;

    if (data->error)
        return 0;

    virObjectLock(obj);
    def = obj->def;

    if (data->aclfilter && !data->aclfilter(data->conn, def))
        goto cleanup;

    if (!data->filters) {
        data->nfilters++;
        goto cleanup;
    }

    if (!(nwfilter = virGetNWFilter(data->conn, def->name, def->uuid))) {
        data->error = true;
        goto cleanup;
    }
    data->filters[data->nfilters++] = nwfilter;

 cleanup:
    virObjectUnlock(obj);
    return 0;
}


int
virNWFilterObjListExport(virConnectPtr conn,
                         virNWFilterObjListPtr nwfilters,
                         virNWFilterPtr **filters,
                         virNWFilterObjListFilter aclfilter)
{
    struct virNWFilterExportData data = { .conn = conn, .aclfilter = aclfilter,
        .filters = NULL, .nfilters = 0, .error = false };

    virObjectLock(nwfilters);
    if (filters &&
        VIR_ALLOC_N(data.filters, virHashSize(nwfilters->objs) + 1) < 0) {
        virObjectUnlock(nwfilters);
        return -1;
    }

    virHashForEach(nwfilters->objs, virNWFilterObjListExportCallback, &data);
    virObjectUnlock(nwfilters);

    if (data.error)
         goto cleanup;

    if (data.filters) {
        /* trim the array to the final size */
        ignore_value(VIR_REALLOC_N(data.filters, data.nfilters + 1));
        *filters = data.filters;
    }

    return data.nfilters;

 cleanup:
    virObjectListFree(data.filters);
    return -1;
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


static void
virNWFilterObjLock(virNWFilterObjPtr obj)
{
    virMutexLock(&obj->lock);
}


static void
virNWFilterObjUnlock(virNWFilterObjPtr obj)
{
    virMutexUnlock(&obj->lock);
}
