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
    virObjectRWLockable parent;

    bool wantRemoved;

    virNWFilterDefPtr def;
    virNWFilterDefPtr newDef;
};

struct _virNWFilterObjList {
    virObjectRWLockable parent;

    /* uuid string -> virNWFilterObj  mapping
     * for O(1), lockless lookup-by-uuid */
    virHashTable *objs;

    /* name -> virNWFilterObj mapping for O(1),
     * lockless lookup-by-name */
    virHashTable *objsName;
};

static virClassPtr virNWFilterObjClass;
static virClassPtr virNWFilterObjListClass;
static void virNWFilterObjDispose(void *opaque);
static void virNWFilterObjListDispose(void *opaque);


static int
virNWFilterObjOnceInit(void)
{
    if (!(virNWFilterObjClass = virClassNew(virClassForObjectRWLockable(),
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

    if (!(obj = virObjectRWLockableNew(virNWFilterObjClass)))
        return NULL;

    virObjectRWLockWrite(obj);
    return obj;
}


static void
virNWFilterObjPromoteToWrite(virNWFilterObjPtr obj)
{
    virObjectRWUnlock(obj);
    virObjectRWLockWrite(obj);
}


static void
virNWFilterObjDemoteFromWrite(virNWFilterObjPtr obj)
{
    virObjectRWUnlock(obj);
    virObjectRWLockRead(obj);
}


void
virNWFilterObjEndAPI(virNWFilterObjPtr *obj)
{
    if (!*obj)
        return;

    virObjectRWUnlock(*obj);
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
virNWFilterObjDispose(void *opaque)
{
    virNWFilterObjPtr obj = opaque;

    if (!obj)
        return;

    virNWFilterDefFree(obj->def);
    virNWFilterDefFree(obj->newDef);
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


virNWFilterObjListPtr
virNWFilterObjListNew(void)
{
    virNWFilterObjListPtr nwfilters;

    if (virNWFilterObjInitialize() < 0)
        return NULL;

    if (!(nwfilters = virObjectRWLockableNew(virNWFilterObjListClass)))
        return NULL;

    if (!(nwfilters->objs = virHashCreate(10, virObjectFreeHashData))) {
        virObjectUnref(nwfilters);
        return NULL;
    }

    if (!(nwfilters->objsName = virHashCreate(10, virObjectFreeHashData))) {
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
    virObjectRef(obj);
    virObjectRWUnlock(obj);
    virObjectRWLockWrite(nwfilters);
    virObjectRWLockWrite(obj);
    virHashRemoveEntry(nwfilters->objs, uuidstr);
    virHashRemoveEntry(nwfilters->objsName, def->name);
    virObjectRWUnlock(obj);
    virObjectUnref(obj);
    virObjectRWUnlock(nwfilters);
}


/**
 * virNWFilterObjListFindByUUID[Locked]
 * @nwfilters: Pointer to filter list
 * @uuidstr: UUID to use to lookup the object
 *
 * The static [Locked] version would only be used when the Object List is
 * already locked, such as is the case during virNWFilterObjListAssignDef.
 * The caller is thus responsible for locking the object.
 *
 * Search for the object by uuidstr in the hash table and return a read
 * locked copy of the object.
 *
 * Returns: A reffed object or NULL on error
 */
static virNWFilterObjPtr
virNWFilterObjListFindByUUIDLocked(virNWFilterObjListPtr nwfilters,
                                   const char *uuidstr)
{
    return virObjectRef(virHashLookup(nwfilters->objs, uuidstr));
}


/*
 * Returns: A reffed and read locked object or NULL on error
 */
virNWFilterObjPtr
virNWFilterObjListFindByUUID(virNWFilterObjListPtr nwfilters,
                             const char *uuidstr)
{
    virNWFilterObjPtr obj;

    virObjectRWLockRead(nwfilters);
    obj = virNWFilterObjListFindByUUIDLocked(nwfilters, uuidstr);
    virObjectRWUnlock(nwfilters);
    if (obj)
        virObjectRWLockRead(obj);

    return obj;
}


/**
 * virNWFilterObjListFindByName[Locked]
 * @nwfilters: Pointer to filter list
 * @name: filter name to use to lookup the object
 *
 * The static [Locked] version would only be used when the Object List is
 * already locked, such as is the case during virNWFilterObjListAssignDef.
 * The caller is thus responsible for locking the object.
 *
 * Search for the object by name in the hash table and return a read
 * locked copy of the object.
 *
 * Returns: A reffed object or NULL on error
 */
static virNWFilterObjPtr
virNWFilterObjListFindByNameLocked(virNWFilterObjListPtr nwfilters,
                                   const char *name)
{
    return virObjectRef(virHashLookup(nwfilters->objsName, name));
}


/*
 * Returns: A reffed and read locked object or NULL on error
 */
virNWFilterObjPtr
virNWFilterObjListFindByName(virNWFilterObjListPtr nwfilters,
                             const char *name)
{
    virNWFilterObjPtr obj;

    virObjectRWLockRead(nwfilters);
    obj = virNWFilterObjListFindByNameLocked(nwfilters, name);
    virObjectRWUnlock(nwfilters);
    if (obj)
        virObjectRWLockRead(obj);

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

            obj = virNWFilterObjListFindByName(nwfilters,
                                               entry->include->filterref);
            if (obj) {
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


/* virNWFilterObjTestUnassignDef
 * @obj: A read locked nwfilter object
 *
 * Cause the rebuild to occur because we're about to undefine the nwfilter.
 * The rebuild code is designed to check if the filter is currently in use
 * by a domain and thus disallow the unassign.
 *
 * NB: Although we enter with the UPDATE lock from UNDEFINE, let's still
 *     promote to a WRITE lock before changing *this* object's wantRemoved
 *     value that will be used in the virNWFilterObjListFindInstantiateFilter
 *     processing to determine whether we can really remove this filter or not.
 *
 * Returns 0 if we can continue with the unassign, -1 if it's in use
 */
int
virNWFilterObjTestUnassignDef(virNWFilterObjPtr obj)
{
    int rc = 0;

    virNWFilterObjPromoteToWrite(obj);
    obj->wantRemoved = true;
    virNWFilterObjDemoteFromWrite(obj);

    /* trigger the update on VMs referencing the filter */
    if (virNWFilterTriggerVMFilterRebuild() < 0)
        rc = -1;

    virNWFilterObjPromoteToWrite(obj);
    obj->wantRemoved = false;
    virNWFilterObjDemoteFromWrite(obj);

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

    virUUIDFormat(def->uuid, uuidstr);

    if ((obj = virNWFilterObjListFindByUUID(nwfilters, uuidstr))) {
        objdef = obj->def;

        if (STRNEQ(def->name, objdef->name)) {
            virReportError(VIR_ERR_OPERATION_FAILED,
                           _("filter with same UUID but different name "
                             "('%s') already exists"),
                           objdef->name);
            virNWFilterObjEndAPI(&obj);
            return NULL;
        }
        virNWFilterObjEndAPI(&obj);
    } else {
        if ((obj = virNWFilterObjListFindByName(nwfilters, def->name))) {
            objdef = obj->def;
            virReportError(VIR_ERR_OPERATION_FAILED,
                           _("filter '%s' already exists with uuid %s"),
                           def->name, uuidstr);
            virNWFilterObjEndAPI(&obj);
            return NULL;
        }
    }

    if (virNWFilterObjListDefLoopDetect(nwfilters, def) < 0) {
        virReportError(VIR_ERR_OPERATION_FAILED,
                       "%s", _("filter would introduce a loop"));
        return NULL;
    }

    /* We're about to make some changes to objects on the list - so get the
     * list READ lock in order to Find the object and WRITE lock the object
     * since both paths would immediately promote it anyway */
    virObjectRWLockRead(nwfilters);
    if ((obj = virNWFilterObjListFindByNameLocked(nwfilters, def->name))) {
        virObjectRWLockWrite(obj);
        virObjectRWUnlock(nwfilters);

        objdef = obj->def;
        if (virNWFilterDefEqual(def, objdef)) {
            virNWFilterDefFree(objdef);
            obj->def = def;
            virNWFilterObjDemoteFromWrite(obj);
            return obj;
        }

        obj->newDef = def;

        /* Demote while the trigger runs since it may need to grab a read
         * lock on this object and promote before returning. */
        virNWFilterObjDemoteFromWrite(obj);

        /* trigger the update on VMs referencing the filter */
        if (virNWFilterTriggerVMFilterRebuild() < 0) {
            virNWFilterObjPromoteToWrite(obj);
            obj->newDef = NULL;
            /* NB: We're done, no need to Demote and End, just End */
            virNWFilterObjEndAPI(&obj);
            return NULL;
        }

        virNWFilterObjPromoteToWrite(obj);
        virNWFilterDefFree(objdef);
        obj->def = def;
        obj->newDef = NULL;
        virNWFilterObjDemoteFromWrite(obj);
        return obj;
    }

    /* Promote the nwfilters to add a new object */
    virObjectRWUnlock(nwfilters);
    virObjectRWLockWrite(nwfilters);
    if (!(obj = virNWFilterObjNew()))
        goto cleanup;

    if (virHashAddEntry(nwfilters->objs, uuidstr, obj) < 0)
        goto error;
    virObjectRef(obj);

    if (virHashAddEntry(nwfilters->objsName, def->name, obj) < 0) {
        virHashRemoveEntry(nwfilters->objs, uuidstr);
        goto error;
    }
    virObjectRef(obj);

    obj->def = def;
    virNWFilterObjDemoteFromWrite(obj);

 cleanup:
    virObjectRWUnlock(nwfilters);
    return obj;

 error:
    virObjectRWUnlock(obj);
    virObjectUnref(obj);
    virObjectRWUnlock(nwfilters);
    return NULL;
}


struct virNWFilterCountData {
    virConnectPtr conn;
    virNWFilterObjListFilter filter;
    int nelems;
};

static int
virNWFilterObjListNumOfNWFiltersCallback(void *payload,
                                         const void *name ATTRIBUTE_UNUSED,
                                         void *opaque)
{
    virNWFilterObjPtr obj = payload;
    struct virNWFilterCountData *data = opaque;

    virObjectRWLockRead(obj);
    if (!data->filter || data->filter(data->conn, obj->def))
        data->nelems++;
    virObjectRWUnlock(obj);
    return 0;
}

int
virNWFilterObjListNumOfNWFilters(virNWFilterObjListPtr nwfilters,
                                 virConnectPtr conn,
                                 virNWFilterObjListFilter filter)
{
    struct virNWFilterCountData data = {
        .conn = conn, .filter = filter, .nelems = 0 };

    virObjectRWLockRead(nwfilters);
    virHashForEach(nwfilters->objs, virNWFilterObjListNumOfNWFiltersCallback,
                   &data);
    virObjectRWUnlock(nwfilters);

    return data.nelems;
}


struct virNWFilterListData {
    virConnectPtr conn;
    virNWFilterObjListFilter filter;
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

    virObjectRWLockRead(obj);
    def = obj->def;

    if (!data->filter || data->filter(data->conn, def)) {
        if (VIR_STRDUP(data->elems[data->nelems], def->name) < 0) {
            data->error = true;
            goto cleanup;
        }
        data->nelems++;
    }

 cleanup:
    virObjectRWUnlock(obj);
    return 0;
}


int
virNWFilterObjListGetNames(virNWFilterObjListPtr nwfilters,
                           virConnectPtr conn,
                           virNWFilterObjListFilter filter,
                           char **const names,
                           int maxnames)
{
    struct virNWFilterListData data = { .conn = conn, .filter = filter,
        .nelems = 0, .elems = names, .maxelems = maxnames, .error = false };

    virObjectRWLockRead(nwfilters);
    virHashForEach(nwfilters->objs, virNWFilterObjListGetNamesCallback, &data);
    virObjectRWUnlock(nwfilters);

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
    virNWFilterObjListFilter filter;
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

    virObjectRWLockRead(obj);
    def = obj->def;

    if (data->filter && !data->filter(data->conn, def))
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
    virObjectRWUnlock(obj);
    return 0;
}


int
virNWFilterObjListExport(virConnectPtr conn,
                         virNWFilterObjListPtr nwfilters,
                         virNWFilterPtr **filters,
                         virNWFilterObjListFilter filter)
{
    struct virNWFilterExportData data = { .conn = conn, .filter = filter,
        .filters = NULL, .nfilters = 0, .error = false };

    virObjectRWLockRead(nwfilters);
    if (filters &&
        VIR_ALLOC_N(data.filters, virHashSize(nwfilters->objs) + 1) < 0) {
        virObjectRWUnlock(nwfilters);
        return -1;
    }

    virHashForEach(nwfilters->objs, virNWFilterObjListExportCallback, &data);
    virObjectRWUnlock(nwfilters);

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
