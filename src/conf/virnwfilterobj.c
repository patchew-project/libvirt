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
    size_t count;
    virNWFilterObjPtr *objs;
};

static virClassPtr virNWFilterObjClass;
static void virNWFilterObjDispose(void *opaque);


static int
virNWFilterObjOnceInit(void)
{
    if (!(virNWFilterObjClass = virClassNew(virClassForObjectRWLockable(),
                                            "virNWFilterObj",
                                            sizeof(virNWFilterObj),
                                            virNWFilterObjDispose)))
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


void
virNWFilterObjListFree(virNWFilterObjListPtr nwfilters)
{
    size_t i;
    for (i = 0; i < nwfilters->count; i++)
        virObjectUnref(nwfilters->objs[i]);
    VIR_FREE(nwfilters->objs);
    VIR_FREE(nwfilters);
}


virNWFilterObjListPtr
virNWFilterObjListNew(void)
{
    virNWFilterObjListPtr nwfilters;

    if (VIR_ALLOC(nwfilters) < 0)
        return NULL;
    return nwfilters;
}


void
virNWFilterObjListRemove(virNWFilterObjListPtr nwfilters,
                         virNWFilterObjPtr obj)
{
    size_t i;

    virObjectRWUnlock(obj);

    for (i = 0; i < nwfilters->count; i++) {
        virObjectRWLockWrite(nwfilters->objs[i]);
        if (nwfilters->objs[i] == obj) {
            virObjectRWUnlock(nwfilters->objs[i]);
            virObjectUnref(nwfilters->objs[i]);

            VIR_DELETE_ELEMENT(nwfilters->objs, i, nwfilters->count);
            break;
        }
        virObjectRWUnlock(nwfilters->objs[i]);
    }
}


/**
 * virNWFilterObjListFindByUUID
 * @nwfilters: Pointer to filter list
 * @uuid: UUID to use to lookup the object
 *
 * Search for the object by uuidstr in the hash table and return a read
 * locked copy of the object.
 *
 * Returns: A reffed and read locked object or NULL on error
 */
virNWFilterObjPtr
virNWFilterObjListFindByUUID(virNWFilterObjListPtr nwfilters,
                             const unsigned char *uuid)
{
    size_t i;
    virNWFilterObjPtr obj;
    virNWFilterDefPtr def;

    for (i = 0; i < nwfilters->count; i++) {
        obj = nwfilters->objs[i];
        virObjectRWLockRead(obj);
        def = obj->def;
        if (!memcmp(def->uuid, uuid, VIR_UUID_BUFLEN))
            return virObjectRef(obj);
        virObjectRWUnlock(obj);
    }

    return NULL;
}


/**
 * virNWFilterObjListFindByName
 * @nwfilters: Pointer to filter list
 * @name: filter name to use to lookup the object
 *
 * Search for the object by name in the hash table and return a read
 * locked copy of the object.
 *
 * Returns: A reffed and read locked object or NULL on error
 */
virNWFilterObjPtr
virNWFilterObjListFindByName(virNWFilterObjListPtr nwfilters,
                             const char *name)
{
    size_t i;
    virNWFilterObjPtr obj;
    virNWFilterDefPtr def;

    for (i = 0; i < nwfilters->count; i++) {
        obj = nwfilters->objs[i];
        virObjectRWLockRead(obj);
        def = obj->def;
        if (STREQ_NULLABLE(def->name, name))
            return virObjectRef(obj);
        virObjectRWUnlock(obj);
    }

    return NULL;
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

    if ((obj = virNWFilterObjListFindByUUID(nwfilters, def->uuid))) {
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
            char uuidstr[VIR_UUID_STRING_BUFLEN];

            objdef = obj->def;
            virUUIDFormat(objdef->uuid, uuidstr);
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


    /* Get a READ lock and immediately promote to WRITE while we adjust
     * data within. */
    if ((obj = virNWFilterObjListFindByName(nwfilters, def->name))) {

        objdef = obj->def;
        if (virNWFilterDefEqual(def, objdef)) {
            virNWFilterObjPromoteToWrite(obj);
            virNWFilterDefFree(objdef);
            obj->def = def;
            virNWFilterObjDemoteFromWrite(obj);
            return obj;
        }

        /* Set newDef and run the trigger with a demoted lock since it may need
         * to grab a read lock on this object and promote before returning. */
        virNWFilterObjPromoteToWrite(obj);
        obj->newDef = def;
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

    if (!(obj = virNWFilterObjNew()))
        return NULL;

    if (VIR_APPEND_ELEMENT_COPY(nwfilters->objs,
                                nwfilters->count, obj) < 0) {
        virNWFilterObjEndAPI(&obj);
        return NULL;
    }
    virObjectRef(obj);
    obj->def = def;
    virNWFilterObjDemoteFromWrite(obj);

    return obj;
}


int
virNWFilterObjListNumOfNWFilters(virNWFilterObjListPtr nwfilters,
                                 virConnectPtr conn,
                                 virNWFilterObjListFilter filter)
{
    size_t i;
    int nfilters = 0;

    for (i = 0; i < nwfilters->count; i++) {
        virNWFilterObjPtr obj = nwfilters->objs[i];
        virObjectRWLockRead(obj);
        if (!filter || filter(conn, obj->def))
            nfilters++;
        virObjectRWUnlock(obj);
    }

    return nfilters;
}


int
virNWFilterObjListGetNames(virNWFilterObjListPtr nwfilters,
                           virConnectPtr conn,
                           virNWFilterObjListFilter filter,
                           char **const names,
                           int maxnames)
{
    int nnames = 0;
    size_t i;
    virNWFilterDefPtr def;

    for (i = 0; i < nwfilters->count && nnames < maxnames; i++) {
        virNWFilterObjPtr obj = nwfilters->objs[i];
        virObjectRWLockRead(obj);
        def = obj->def;
        if (!filter || filter(conn, def)) {
            if (VIR_STRDUP(names[nnames], def->name) < 0) {
                virObjectRWUnlock(obj);
                goto failure;
            }
            nnames++;
        }
        virObjectRWUnlock(obj);
    }

    return nnames;

 failure:
    while (--nnames >= 0)
        VIR_FREE(names[nnames]);

    return -1;
}


int
virNWFilterObjListExport(virConnectPtr conn,
                         virNWFilterObjListPtr nwfilters,
                         virNWFilterPtr **filters,
                         virNWFilterObjListFilter filter)
{
    virNWFilterPtr *tmp_filters = NULL;
    int nfilters = 0;
    virNWFilterPtr nwfilter = NULL;
    virNWFilterObjPtr obj = NULL;
    virNWFilterDefPtr def;
    size_t i;
    int ret = -1;

    if (!filters) {
        ret = nwfilters->count;
        goto cleanup;
    }

    if (VIR_ALLOC_N(tmp_filters, nwfilters->count + 1) < 0)
        goto cleanup;

    for (i = 0; i < nwfilters->count; i++) {
        obj = nwfilters->objs[i];
        virObjectRWLockRead(obj);
        def = obj->def;
        if (!filter || filter(conn, def)) {
            if (!(nwfilter = virGetNWFilter(conn, def->name, def->uuid))) {
                virObjectRWUnlock(obj);
                goto cleanup;
            }
            tmp_filters[nfilters++] = nwfilter;
        }
        virObjectRWUnlock(obj);
    }

    *filters = tmp_filters;
    tmp_filters = NULL;
    ret = nfilters;

 cleanup:
    if (tmp_filters) {
        for (i = 0; i < nfilters; i ++)
            virObjectUnref(tmp_filters[i]);
    }
    VIR_FREE(tmp_filters);

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
