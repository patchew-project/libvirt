/*
 * storage_backend_linstor.c: storage backend for linstor volume handling
 *
 * Copyright (C) 2020-2021 Rene Peinthor
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

#include "storage_backend_linstor.h"
#define LIBVIRT_STORAGE_BACKEND_LINSTOR_PRIV_H_ALLOW
#include "storage_backend_linstor_priv.h"
#include "virerror.h"
#include "virjson.h"
#include "virstring.h"
#include "virlog.h"
#include "viralloc.h"
#include "storage_conf.h"
#include "storage_util.h"

#include <sys/utsname.h>

#define VIR_FROM_THIS VIR_FROM_STORAGE

VIR_LOG_INIT("storage.storage_backend_linstor");


#define LINSTORCLI "linstor"


/**
 * @brief virStorageBackendLinstorGetNodeName
 *        Get the configured linstor node name, checks pool host[0]
 *        if node isn't set there, it will try to get hostname and use that.
 * @param pool Pool configuration
 * @param nodenameOut Retrieved nodename will be copied here, caller is responsible to free.
 * @return -1 on error, otherwise 0
 */
static int
virStorageBackendLinstorGetNodeName(virStoragePoolObjPtr pool, char **nodenameOut)
{
    int ret = 0;
    struct utsname host;
    virStoragePoolDefPtr def = virStoragePoolObjGetDef(pool);
    if (def->source.nhost > 0 && def->source.hosts[0].name != NULL)
         *nodenameOut = g_strdup(def->source.hosts[0].name);
    else if (uname(&host) == 0)
        *nodenameOut = g_strdup(host.nodename);
    else
        ret = -1;

    return ret;
}


static virCommandPtr
virStorageBackendLinstorPrepLinstorCmd(bool machineout)
{
    if (machineout)
        return virCommandNewArgList(LINSTORCLI, "-m", "--output-version", "v1", NULL);
    else
        return virCommandNewArgList(LINSTORCLI, NULL);
}


/**
 * @brief virStorageBackendLinstorUnpackLinstorJSON
 *        Linstor client results are packed into an array, as results usually contain
 *        a list of apicallrcs. But lists usually only have 1 entry.
 * @param replyArr linstor reply array json
 * @return Pointer to the first array element or NULL if no array or empty
 */
static virJSONValuePtr
virStorageBackendLinstorUnpackLinstorJSON(virJSONValuePtr replyArr)
{
    if (replyArr == NULL) {
        return NULL;
    }

    if (!virJSONValueIsArray(replyArr)) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Root Linstor list result is expected to be an array"));
        return NULL;
    }

    if (virJSONValueArraySize(replyArr) == 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Empty reply from Linstor client"));
        return NULL;
    }

    return virJSONValueArrayGet(replyArr, 0);
}


int
virStorageBackendLinstorFilterRscDefsForRscGroup(const char *resourceGroup,
                                                 const char *output,
                                                 virJSONValuePtr rscDefArrayOut)
{
    int ret = -1;
    virJSONValuePtr replyArr = NULL;
    virJSONValuePtr rscDefArr = NULL;
    size_t i;

    replyArr = virJSONValueFromString(output);

    rscDefArr = virStorageBackendLinstorUnpackLinstorJSON(replyArr);
    if (rscDefArr == NULL) {
        goto cleanup;
    }

    for (i = 0; i < virJSONValueArraySize(rscDefArr); i++) {
        virJSONValuePtr rscDefObj = virJSONValueArrayGet(rscDefArr, i);

        if (g_ascii_strcasecmp(virJSONValueObjectGetString(rscDefObj, "resource_group_name"),
                               resourceGroup) == 0) {

            virJSONValueArrayAppendString(rscDefArrayOut,
                                          g_strdup(virJSONValueObjectGetString(rscDefObj, "name")));
        }
    }

    ret = 0;
 cleanup:
    virJSONValueFree(replyArr);
    return ret;
}


int
virStorageBackendLinstorParseResourceGroupList(const char *resourceGroup,
                                               const char *output,
                                               virJSONValuePtr *storPoolArrayOut)
{
    int ret = -1;
    bool rscGrpFound = false;
    virJSONValuePtr replyArr = NULL;
    virJSONValuePtr rscGrpArr = NULL;
    virJSONValuePtr rscGrpSelFilterObj = NULL;
    virJSONValuePtr storPoolsArr = NULL;
    size_t i;

    replyArr = virJSONValueFromString(output);

    rscGrpArr = virStorageBackendLinstorUnpackLinstorJSON(replyArr);
    if (rscGrpArr == NULL) {
        goto cleanup;
    }

    for (i = 0; i < virJSONValueArraySize(rscGrpArr); i++) {
        virJSONValuePtr rscGrpObj = virJSONValueArrayGet(rscGrpArr, i);

        if (g_ascii_strcasecmp(virJSONValueObjectGetString(rscGrpObj, "name"),
                               resourceGroup) == 0) {
            rscGrpFound = true;

            rscGrpSelFilterObj = virJSONValueObjectGetObject(rscGrpObj, "select_filter");
            if (rscGrpSelFilterObj != NULL) {
                storPoolsArr = virJSONValueObjectGetArray(rscGrpSelFilterObj, "storage_pool_list");

                *storPoolArrayOut = virJSONValueCopy(storPoolsArr);
            }
            break;
        }
    }

    if (!rscGrpFound) {
        virReportError(VIR_ERR_INVALID_STORAGE_POOL,
                      _("Specified resource group '%s' not found in linstor"), resourceGroup);
        goto cleanup;
    }

    ret = 0;
 cleanup:
    virJSONValueFree(replyArr);
    return ret;
}


/**
 * @brief virStorageBackendLinstorParseStoragePoolList
 *        Parses a storage pool list result and updates the pools capacity, allocation numbers,
 *        for the given node.
 * @param pool Pool object to update
 * @param nodename Node name of which storage pools are taken for the update.
 * @param output JSON output content from the `linstor storage-pool list` command
 * @return -1 on error, 0 on success
 */
int
virStorageBackendLinstorParseStoragePoolList(virStoragePoolDefPtr pool,
                                             const char* nodename,
                                             const char *output)
{
    int ret = -1;
    virJSONValuePtr replyArr = NULL;
    virJSONValuePtr storpoolArr = NULL;
    unsigned long long capacity = 0;
    unsigned long long freeCapacity = 0;
    size_t i;

    replyArr = virJSONValueFromString(output);

    storpoolArr = virStorageBackendLinstorUnpackLinstorJSON(replyArr);
    if (storpoolArr == NULL) {
        goto cleanup;
    }

    if (!virJSONValueIsArray(storpoolArr)) {
        // probably an ApiCallRc then, with an error
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Storage pool list not recieved"));
        goto cleanup;
    }

    for (i = 0; i < virJSONValueArraySize(storpoolArr); i++) {
        unsigned long long storCapacity = 0;
        unsigned long long storFree = 0;
        virJSONValuePtr storPoolObj = virJSONValueArrayGet(storpoolArr, i);

        if (!virJSONValueIsObject(storPoolObj)) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Unable to parse storage pool object for pool '%s'"),
                           pool->name);
            goto cleanup;
        }

        if (g_ascii_strcasecmp(virJSONValueObjectGetString(storPoolObj, "node_name"), nodename) == 0) {
            if (g_str_equal(virJSONValueObjectGetString(storPoolObj, "provider_kind"), "DISKLESS")) {
                /* ignore diskless pools, as they have no capacity */
                continue;
            }

            if (virJSONValueObjectGetNumberUlong(storPoolObj, "total_capacity", &storCapacity)) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Unable to parse storage pool '%s' capacity"),
                               virJSONValueObjectGetString(storPoolObj, "storage_pool_name"));
                goto cleanup;
            }
            if (virJSONValueObjectGetNumberUlong(storPoolObj, "free_capacity", &storFree)) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Unable to parse storage pool '%s' free capacity"),
                               virJSONValueObjectGetString(storPoolObj, "storage_pool_name"));
                goto cleanup;
            }
            capacity += storCapacity * 1024; // linstor reports in KiB
            freeCapacity += storFree * 1024; // linstor reports in KiB
        }
    }

    pool->capacity = capacity;
    pool->available = freeCapacity;
    pool->allocation = capacity - freeCapacity;

    ret = 0;

 cleanup:
    virJSONValueFree(replyArr);
    return ret;
}


/**
 * @brief virStorageBackendLinstorParseVolumeDefinition
 *        Parses the machine output of `linstor volume-definition list` and updates
 *        the virStorageVolDef capacity.
 * @param vol Volume to update the capacity
 * @param output JSON output of `linstor volume-definition list -r ...`
 * @return -1 on error, 0 on success
 */
int
virStorageBackendLinstorParseVolumeDefinition(virStorageVolDefPtr vol,
                                              const char *output)
{
    int ret = -1;
    virJSONValuePtr replyArr = NULL;
    virJSONValuePtr resourceDefArr = NULL;
    size_t i;

    replyArr = virJSONValueFromString(output);

    resourceDefArr = virStorageBackendLinstorUnpackLinstorJSON(replyArr);
    if (resourceDefArr == NULL) {
        goto cleanup;
    }

    if (!virJSONValueIsArray(resourceDefArr)) {
        /* probably an ApiCallRc then, with an error */
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Volume definition list not recieved"));
        goto cleanup;
    }

    for (i = 0; i < virJSONValueArraySize(resourceDefArr); i++) {
        unsigned long long volDefCapacityKiB = 0;
        virJSONValuePtr resourceDefObj = virJSONValueArrayGet(resourceDefArr, i);

        if (resourceDefObj == NULL || !virJSONValueIsObject(resourceDefObj)) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Unable to parse resource definition object"));
            goto cleanup;
        }

        if (g_ascii_strcasecmp(virJSONValueObjectGetString(resourceDefObj, "name"), vol->name) == 0) {
            virJSONValuePtr volumeDefArr = virJSONValueObjectGet(resourceDefObj, "volume_definitions");
            virJSONValuePtr volumeDefObj = NULL;

            if (volumeDefArr == NULL || !virJSONValueIsArray(volumeDefArr)
                    || virJSONValueArraySize(volumeDefArr) == 0) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Volume definition list incorrect for resource definition '%s'"),
                               vol->name);
                goto cleanup;
            }

            volumeDefObj = virJSONValueArrayGet(volumeDefArr, 0);
            if (virJSONValueObjectGetNumberUlong(volumeDefObj, "size_kib", &volDefCapacityKiB)) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Unable to parse volume definition size for resource '%s'"),
                               vol->name);
                goto cleanup;
            }

            /* linstor reports in KiB */
            vol->target.capacity = volDefCapacityKiB * 1024;
            break;
        }
    }

    ret = 0;

 cleanup:
    virJSONValueFree(replyArr);
    return ret;
}


static int
virStorageBackendLinstorRefreshVolFromJSON(const char *sourceName,
                                           virStorageVolDefPtr vol,
                                           virJSONValuePtr linstorResObj,
                                           const char *volumeDefListOutput)
{
    virJSONValuePtr volumesArr = NULL;
    virJSONValuePtr volumeObj = NULL;
    long long alloc_kib = 0;

    volumesArr = virJSONValueObjectGet(linstorResObj, "volumes");

    if (volumesArr != NULL && !virJSONValueIsArray(volumesArr)) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("'volumes' not found in resource object JSON"));
        return -1;
    }

    volumeObj = virJSONValueArrayGet(volumesArr, 0);

    vol->type = VIR_STORAGE_VOL_BLOCK;
    VIR_FREE(vol->key);
    vol->key = g_strdup_printf("%s/%s", sourceName, vol->name);
    VIR_FREE(vol->target.path);
    vol->target.path = g_strdup(virJSONValueObjectGetString(volumeObj, "device_path"));
    vol->target.format = VIR_STORAGE_FILE_RAW;

    virJSONValueObjectGetNumberLong(volumeObj, "allocated_size_kib", &alloc_kib);

    if (alloc_kib >= 0)
        vol->target.allocation = alloc_kib * 1024;
    else
        vol->target.allocation = 0;

    if (volumeDefListOutput != NULL) {
        return virStorageBackendLinstorParseVolumeDefinition(vol, volumeDefListOutput);
    }

    return 0;
}


static int
virStorageBackendLinstorRefreshVol(virStoragePoolObjPtr pool,
                                   virStorageVolDefPtr vol)
{
    int ret = -1;
    g_autofree char *output = NULL;
    g_autofree char *outputVolDef = NULL;
    g_autofree char *nodename = NULL;
    g_autoptr(virCommand) cmdResList = NULL;
    g_autoptr(virCommand) cmdVolDefList = NULL;
    virJSONValuePtr replyArr = NULL;
    virJSONValuePtr rscArr = NULL;
    virStoragePoolDefPtr def = virStoragePoolObjGetDef(pool);

    if (virStorageBackendLinstorGetNodeName(pool, &nodename))
        return -1;

    cmdResList = virStorageBackendLinstorPrepLinstorCmd(true);
    virCommandAddArgList(cmdResList, "resource", "list", "-n", nodename, "-r", vol->name, NULL);
    virCommandSetOutputBuffer(cmdResList, &output);
    if (virCommandRun(cmdResList, NULL) < 0)
        return -1;

    cmdVolDefList = virStorageBackendLinstorPrepLinstorCmd(true);
    virCommandAddArgList(cmdVolDefList, "volume-definition", "list", "-r", vol->name, NULL);
    virCommandSetOutputBuffer(cmdVolDefList, &outputVolDef);
    if (virCommandRun(cmdVolDefList, NULL) < 0)
        return -1;

    replyArr = virJSONValueFromString(output);

    rscArr = virStorageBackendLinstorUnpackLinstorJSON(replyArr);
    if (rscArr == NULL) {
        goto cleanup;
    }

    if (!virJSONValueIsArray(rscArr)) {
        // probably an ApiCallRc then, with an error
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Resource list not recieved"));
        goto cleanup;
    }

    if (virJSONValueArraySize(rscArr) != 1) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Couldn't find resource '%s' in Linstor resource list JSON"), vol->name);
        goto cleanup;
    }

    ret = virStorageBackendLinstorRefreshVolFromJSON(
                def->source.name, vol, virJSONValueArrayGet(rscArr, 0), outputVolDef);

 cleanup:
    virJSONValueFree(rscArr);
    return ret;
}


static int
virStorageBackendLinstorAddVolume(virStoragePoolObjPtr pool,
                                  virJSONValuePtr resourceObj,
                                  const char *outputVolDef)
{
    g_autoptr(virStorageVolDef) vol = NULL;
    virStoragePoolDefPtr def = virStoragePoolObjGetDef(pool);

    if (resourceObj == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Missing disk info when adding volume"));
        return -1;
    }

    vol = g_new0(virStorageVolDef, 1);

    vol->name = g_strdup(virJSONValueObjectGetString(resourceObj, "name"));

    if (virStorageBackendLinstorRefreshVolFromJSON(def->source.name,
                                                   vol, resourceObj, outputVolDef) < 0) {
        virStorageVolDefFree(vol);
        return -1;
    }

    if (virStoragePoolObjAddVol(pool, vol) < 0) {
        virStorageVolDefFree(vol);
        return -1;
    }
    vol = NULL;

    return 0;
}


static bool
virStorageBackendLinstorStringInJSONArray(virJSONValuePtr arr, const char *string)
{
    size_t i;
    for (i = 0; i < virJSONValueArraySize(arr); i++) {
        if (g_ascii_strcasecmp(virJSONValueGetString(virJSONValueArrayGet(arr, i)), string) == 0) {
            return true;
        }
    }
    return false;
}


int
virStorageBackendLinstorParseResourceList(virStoragePoolObjPtr pool,
                                          const char *nodeName,
                                          virJSONValuePtr rscDefFilterArr,
                                          const char *outputRscList,
                                          const char *outputVolDef)
{
    int ret = -1;
    virJSONValuePtr replyArr = NULL;
    virJSONValuePtr rscListArr = NULL;
    size_t i;

    replyArr = virJSONValueFromString(outputRscList);

    rscListArr = virStorageBackendLinstorUnpackLinstorJSON(replyArr);
    if (rscListArr == NULL) {
        goto cleanup;
    }

    if (!virJSONValueIsArray(rscListArr)) {
        // probably an ApiCallRc then, with an error
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Storage pool list not recieved"));
        goto cleanup;
    }

    for (i = 0; i < virJSONValueArraySize(rscListArr); i++) {
        virJSONValuePtr rscObj = virJSONValueArrayGet(rscListArr, i);

        if (g_ascii_strcasecmp(virJSONValueObjectGetString(rscObj, "node_name"), nodeName) == 0 &&
                virStorageBackendLinstorStringInJSONArray(rscDefFilterArr,
                                                          virJSONValueObjectGetString(rscObj, "name"))) {
            if (virStorageBackendLinstorAddVolume(pool, rscObj, outputVolDef)) {
                goto cleanup;
            }
        }
    }

    ret = 0;

 cleanup:
    virJSONValueFree(rscListArr);
    return ret;
}

static int
virStorageBackendLinstorRefreshAllVol(virStoragePoolObjPtr pool)
{
    int ret = -1;
    g_autofree char *output = NULL;
    g_autofree char *outputVolDef = NULL;
    g_autofree char *nodename = NULL;
    g_autoptr(virCommand) cmdRscList = NULL;
    g_autoptr(virCommand) cmdVolDefList = NULL;
    virJSONValuePtr rscDefFilterArr = virJSONValueNewArray();
    virStoragePoolDefPtr def = virStoragePoolObjGetDef(pool);

    /* Get all resources usable on that node */
    if (virStorageBackendLinstorGetNodeName(pool, &nodename)) {
        goto cleanup;
    }

    cmdRscList = virStorageBackendLinstorPrepLinstorCmd(true);
    virCommandAddArgList(cmdRscList, "resource", "list", "-n", nodename, NULL);
    virCommandSetOutputBuffer(cmdRscList, &output);
    if (virCommandRun(cmdRscList, NULL) < 0)
        goto cleanup;

    /* Get a list of resources that belong to the rsc group for filtering */
    cmdVolDefList = virStorageBackendLinstorPrepLinstorCmd(true);
    virCommandAddArgList(cmdVolDefList, "volume-definition", "list", NULL);
    virCommandSetOutputBuffer(cmdVolDefList, &outputVolDef);
    if (virCommandRun(cmdVolDefList, NULL) < 0) {
        goto cleanup;
    }

    /* resource belonging to the resource group will be stored in rscDefFilterArr */
    if (virStorageBackendLinstorFilterRscDefsForRscGroup(def->source.name,
                                                         outputVolDef,
                                                         rscDefFilterArr)) {
        goto cleanup;
    }

    ret = virStorageBackendLinstorParseResourceList(pool,
                                                    nodename,
                                                    rscDefFilterArr,
                                                    output,
                                                    outputVolDef);

 cleanup:
    virJSONValueFree(rscDefFilterArr);
    return ret;
}


/**
 * @brief virStorageBackendLinstorGetRscGrpPools
 *        Retrieves the set storage pools used in resource group.
 *        On success caller is responsible to free the virJSONValuePtr.
 * @param rscgrpname resource group name to get the storage pools
 * @param storagePoolsOut virJSONArray with used storage pools
 * @return -1 on error, 0 on success
 */
static int
virStorageBackendLinstorGetRscGrpPools(const char* rscgrpname, virJSONValuePtr *storagePoolsOut)
{
    g_autofree char *outputRscGrp = NULL;
    g_autoptr(virCommand) cmdRscGrpList = NULL;

    cmdRscGrpList = virStorageBackendLinstorPrepLinstorCmd(true);
    virCommandAddArgList(cmdRscGrpList, "resource-group", "list", "-r", rscgrpname, NULL);
    virCommandSetOutputBuffer(cmdRscGrpList, &outputRscGrp);
    if (virCommandRun(cmdRscGrpList, NULL) < 0)
        return -1;

    if (virStorageBackendLinstorParseResourceGroupList(rscgrpname,
                                                       outputRscGrp,
                                                       storagePoolsOut)) {
        return -1;
    }

    return 0;
}


static int
virStorageBackendLinstorRefreshPool(virStoragePoolObjPtr pool)
{
    size_t i;
    g_autofree char *outputStorPoolList = NULL;
    g_autofree char *nodename = NULL;
    g_autoptr(virCommand) cmdStorPoolList = NULL;
    virJSONValuePtr storagePoolArr = NULL;
    virStoragePoolDefPtr def = virStoragePoolObjGetDef(pool);

    if (virStorageBackendLinstorGetNodeName(pool, &nodename))
        return -1;

    if (virStorageBackendLinstorGetRscGrpPools(def->source.name, &storagePoolArr))
        return -1;

    /* Get storage pools used in the used resource group */
    cmdStorPoolList = virStorageBackendLinstorPrepLinstorCmd(true);
    virCommandAddArgList(cmdStorPoolList, "storage-pool", "list", "-n", nodename, NULL);

    if (storagePoolArr != NULL && virJSONValueArraySize(storagePoolArr) > 0) {
        virCommandAddArgList(cmdStorPoolList, "-s", NULL);
        for (i = 0; i < virJSONValueArraySize(storagePoolArr); i++) {
            virCommandAddArg(cmdStorPoolList,
                             virJSONValueGetString(virJSONValueArrayGet(storagePoolArr, i)));
        }

        virJSONValueFree(storagePoolArr);
    }

    virCommandSetOutputBuffer(cmdStorPoolList, &outputStorPoolList);
    if (virCommandRun(cmdStorPoolList, NULL) < 0)
        return -1;

    /* update capacity and allocated from used storage pools */
    if (virStorageBackendLinstorParseStoragePoolList(virStoragePoolObjGetDef(pool),
                                                     nodename,
                                                     outputStorPoolList) < 0)
        return -1;

    /* Get volumes used in the resource group and add */
    return virStorageBackendLinstorRefreshAllVol(pool);
}

static int
virStorageBackendLinstorCreateVol(virStoragePoolObjPtr pool,
                                  virStorageVolDefPtr vol)
{
    virStoragePoolDefPtr def = virStoragePoolObjGetDef(pool);
    g_autoptr(virCommand) cmdRscGrp = NULL;

    VIR_DEBUG("Creating Linstor image %s/%s with size %llu",
              def->source.name, vol->name, vol->target.capacity);

    if (!vol->target.capacity) {
        virReportError(VIR_ERR_NO_SUPPORT, "%s",
                       _("volume capacity required for this storage pool"));
        return -1;
    }

    if (vol->target.format != VIR_STORAGE_FILE_RAW) {
        virReportError(VIR_ERR_NO_SUPPORT, "%s",
                       _("only RAW volumes are supported by this storage pool"));
        return -1;
    }

    if (vol->target.encryption != NULL) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("storage pool does not support encrypted volumes"));
        return -1;
    }

    /* spawn resource */
    cmdRscGrp = virStorageBackendLinstorPrepLinstorCmd(false);
    virCommandAddArgList(cmdRscGrp, "resource-group", "spawn",
                         "--partial", def->source.name, vol->name, NULL);
    virCommandAddArgFormat(cmdRscGrp, "%lluKiB", vol->target.capacity / 1024);
    if (virCommandRun(cmdRscGrp, NULL) < 0)
        return -1;

    /* set volume path and key */
    /* we could skip getting the capacity as we already know it */
    return virStorageBackendLinstorRefreshVol(pool, vol);
}


static int
virStorageBackendLinstorBuildVolFrom(virStoragePoolObjPtr pool,
                                     virStorageVolDefPtr vol,
                                     virStorageVolDefPtr inputvol,
                                     unsigned int flags)
{
    virStorageBackendBuildVolFrom build_func;

    build_func = virStorageBackendGetBuildVolFromFunction(vol, inputvol);
    if (!build_func)
        return -1;

    return build_func(pool, vol, inputvol, flags);
}


static int
virStorageBackendLinstorDeleteVol(virStoragePoolObjPtr pool,
                                  virStorageVolDefPtr vol,
                                  unsigned int flags)
{
    g_autoptr(virCommand) cmd = NULL;

    (void)pool;
    virCheckFlags(0, -1);

    cmd = virStorageBackendLinstorPrepLinstorCmd(false);
    virCommandAddArgList(cmd, "resource-definition", "delete", vol->name, NULL);
    return virCommandRun(cmd, NULL);
}


static int
virStorageBackendLinstorResizeVol(virStoragePoolObjPtr pool,
                                  virStorageVolDefPtr vol,
                                  unsigned long long capacity,
                                  unsigned int flags)
{
    g_autoptr(virCommand) cmd = NULL;

    (void)pool;
    virCheckFlags(0, -1);

    cmd = virStorageBackendLinstorPrepLinstorCmd(false);
    virCommandAddArgList(cmd, "volume-definition", "set-size", vol->name, "0", NULL);
    virCommandAddArgFormat(cmd, "%lluKiB", capacity / 1024);
    return virCommandRun(cmd, NULL);
}


/**
 * @brief virStorageBackendVzCheck
 *        Check if we can connect to a Linstor-Controller
 */
static int
virStorageBackendLinstorCheck(virStoragePoolObjPtr pool,
                         bool *isActive)
{
    g_autoptr(virCommand) cmd = NULL;

    (void)pool;

    /* This command gets the controller version */
    cmd = virStorageBackendLinstorPrepLinstorCmd(false);
    virCommandAddArgList(cmd, "controller", "version", NULL);
    if (virCommandRun(cmd, NULL)) {
        *isActive = false;
    }

    *isActive = true;
    return 0;
}

virStorageBackend virStorageBackendLinstor = {
    .type = VIR_STORAGE_POOL_LINSTOR,

    .refreshPool = virStorageBackendLinstorRefreshPool,
    .checkPool = virStorageBackendLinstorCheck,
    .createVol = virStorageBackendLinstorCreateVol,
    .buildVol = NULL,
    .buildVolFrom = virStorageBackendLinstorBuildVolFrom,
    .refreshVol = virStorageBackendLinstorRefreshVol,
    .deleteVol = virStorageBackendLinstorDeleteVol,
    .resizeVol = virStorageBackendLinstorResizeVol,
    .uploadVol = virStorageBackendVolUploadLocal,
    .downloadVol = virStorageBackendVolDownloadLocal,
    .wipeVol = virStorageBackendVolWipeLocal,
};


int
virStorageBackendLinstorRegister(void)
{
    return virStorageBackendRegister(&virStorageBackendLinstor);
}
