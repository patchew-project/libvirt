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

struct nwfilterLoopSearchData {
    const char *filtername;
};

static bool
nwfilterLoopSearch(virPoolObjPtr obj,
                   void *opaque)
{
    virNWFilterDefPtr def = virPoolObjGetDef(obj);
    struct nwfilterLoopSearchData *data = opaque;
    virNWFilterEntryPtr entry;
    size_t i;

    for (i = 0; i < def->nentries; i++) {
        entry = def->filterEntries[i];
        if (entry->include &&
            STREQ(data->filtername, entry->include->filterref))
            return true;
    }

    return false;
}


/*
 * nwfilterLoopDetect:
 * @nwfilters : the nwfilters to search
 * @filtername: the filter name that may add a loop and is to be tested
 *
 * Detect a loop introduced through the filters being able to
 * reference each other.
 *
 * Returns 0 in case no loop was detected, -1 otherwise.
 */
static int
nwfilterLoopDetect(virPoolObjTablePtr nwfilters,
                   const char *filtername)
{
    struct nwfilterLoopSearchData data = { .filtername = filtername };
    virPoolObjPtr obj;

    if ((obj = virPoolObjTableSearch(nwfilters, nwfilterLoopSearch, &data))) {
        virObjectUnlock(obj);
        return -1;
    }

    return 0;
}


static bool
nwfilterDefEqual(const virNWFilterDef *def1,
                 virNWFilterDefPtr def2,
                 bool cmpUUIDs)
{
    bool ret = false;
    unsigned char rem_uuid[VIR_UUID_BUFLEN];
    char *xml1, *xml2 = NULL;

    if (!cmpUUIDs) {
        /* make sure the UUIDs are equal */
        memcpy(rem_uuid, def2->uuid, sizeof(rem_uuid));
        memcpy(def2->uuid, def1->uuid, sizeof(def2->uuid));
    }

    if (!(xml1 = virNWFilterDefFormat(def1)) ||
        !(xml2 = virNWFilterDefFormat(def2)))
        goto cleanup;

    ret = STREQ(xml1, xml2);

 cleanup:
    if (!cmpUUIDs)
        memcpy(def2->uuid, rem_uuid, sizeof(rem_uuid));

    VIR_FREE(xml1);
    VIR_FREE(xml2);

    return ret;
}


static int
nwfilterAssignDef(virPoolObjPtr obj,
                  void *newDef,
                  void *oldDef ATTRIBUTE_UNUSED,
                  unsigned int assignFlags ATTRIBUTE_UNUSED)
{
    virNWFilterDefPtr newdef = newDef;


    if (nwfilterDefEqual(newdef, virPoolObjGetDef(obj), false)) {
        virPoolObjSetDef(obj, newdef);
    } else {
        virPoolObjSetNewDef(obj, newdef);

        /* trigger the update on VMs referencing the filter */
        if (virNWFilterTriggerVMFilterRebuild()) {
            virPoolObjSetNewDef(obj, NULL);
            return -1;
        }

        virPoolObjSetDef(obj, newdef);
        virPoolObjSetNewDef(obj, NULL);
    }
    return 0;
}


virPoolObjPtr
virNWFilterObjAdd(virPoolObjTablePtr nwfilters,
                  virNWFilterDefPtr def)
{
    virPoolObjPtr obj;
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    /* Check if this filter def's name is referenced already by
     * some other filter's include path */
    if (nwfilterLoopDetect(nwfilters, def->name) < 0) {
        virReportError(VIR_ERR_OPERATION_FAILED,
                       _("filter '%s' would introduce a loop"), def->name);
        return NULL;
    }

    virUUIDFormat(def->uuid, uuidstr);
    if (!(obj = virPoolObjTableAdd(nwfilters, uuidstr, def->name,
                                   def, NULL, NULL, virNWFilterDefFree,
                                   nwfilterAssignDef, 0)))
        return NULL;

    return obj;
}


int
virNWFilterObjTestUnassignDef(virPoolObjPtr obj)
{
    int rc = 0;

    virPoolObjSetBeingRemoved(obj, true);
    /* trigger the update on VMs referencing the filter */
    if (virNWFilterTriggerVMFilterRebuild())
        rc = -1;
    virPoolObjSetBeingRemoved(obj, false);

    return rc;
}


struct nwfilterCountData {
    int count;
};

static int
nwfilterCount(virPoolObjPtr obj ATTRIBUTE_UNUSED,
              void *opaque)
{
    struct nwfilterCountData *data = opaque;

    data->count++;
    return 0;
}


int
virNWFilterObjNumOfNWFilters(virPoolObjTablePtr nwfilters,
                             virConnectPtr conn,
                             virPoolObjACLFilter aclfilter)
{
    struct nwfilterCountData data = { .count = 0 };

    if (virPoolObjTableList(nwfilters, conn, aclfilter,
                            nwfilterCount, &data) < 0)
        return 0;

    return data.count;
}


struct nwfilterNameData {
    int nnames;
    char **const names;
    int maxnames;
};

static int nwfilterGetNames(virPoolObjPtr obj,
                            void *opaque)
{
    virNWFilterDefPtr def = virPoolObjGetDef(obj);
    struct nwfilterNameData *data = opaque;

    if (data->nnames < data->maxnames) {
        if (VIR_STRDUP(data->names[data->nnames++], def->name) < 0)
            return -1;
    }

    return 0;
}


int
virNWFilterObjGetFilters(virPoolObjTablePtr nwfilters,
                         virConnectPtr conn,
                         virPoolObjACLFilter aclfilter,
                         char **names,
                         int maxnames)
{
    struct nwfilterNameData data = { .nnames = 0,
                                     .names = names,
                                     .maxnames = maxnames };

    if (virPoolObjTableList(nwfilters, conn, aclfilter,
                            nwfilterGetNames, &data) < 0)
        goto failure;

    return data.nnames;

 failure:
    while (data.nnames >= 0)
        VIR_FREE(data.names[--data.nnames]);

    return -1;
}


int
virNWFilterObjExportList(virConnectPtr conn,
                         virPoolObjTablePtr nwfilters,
                         virNWFilterPtr **filters,
                         virPoolObjACLFilter aclfilter,
                         unsigned int flags)
{
    virPoolObjPtr *objs = NULL;
    size_t nobjs = 0;
    virNWFilterPtr *filts = NULL;
    int ret = -1;
    size_t i;

    if (virPoolObjTableCollect(nwfilters, conn, &objs, &nobjs, aclfilter,
                               NULL, flags) < 0)
        return -1;

    if (filters) {
        if (VIR_ALLOC_N(filts, nobjs + 1) < 0)
            goto cleanup;

        for (i = 0; i < nobjs; i++) {
            virPoolObjPtr obj = objs[i];
            virNWFilterDefPtr def;

            virObjectLock(obj);
            def = virPoolObjGetDef(obj);
            filts[i] = virGetNWFilter(conn, def->name, def->uuid);
            virObjectUnlock(obj);

            if (!filts[i])
                goto cleanup;
        }

        *filters = filts;
        filts = NULL;
    }

    ret = nobjs;

 cleanup:
    virObjectListFree(filts);
    virObjectListFreeCount(objs, nobjs);
    return ret;
}


static virPoolObjPtr
virNWFilterLoadConfig(virPoolObjTablePtr nwfilters,
                      const char *configDir,
                      const char *name)
{
    virNWFilterDefPtr def = NULL;
    virPoolObjPtr obj;
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

    if (!(obj = virNWFilterObjAdd(nwfilters, def)))
        goto error;

    VIR_FREE(configFile);
    return obj;

 error:
    VIR_FREE(configFile);
    virNWFilterDefFree(def);
    return NULL;
}


int
virNWFilterObjLoadAllConfigs(virPoolObjTablePtr nwfilters,
                             const char *configDir)
{
    DIR *dir;
    struct dirent *entry;
    int ret = -1;
    int rc;

    if ((rc = virDirOpenIfExists(&dir, configDir)) <= 0)
        return rc;

    while ((ret = virDirRead(dir, &entry, configDir)) > 0) {
        virPoolObjPtr obj;

        if (!virFileStripSuffix(entry->d_name, ".xml"))
            continue;

        if ((obj = virNWFilterLoadConfig(nwfilters, configDir, entry->d_name)))
            virPoolObjEndAPI(&obj);
    }

    VIR_DIR_CLOSE(dir);
    return ret;
}
