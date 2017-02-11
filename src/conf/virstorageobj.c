/*
 * virstorageobj.c: internal storage pool and volume objects handling
 *                  (derived from storage_conf.c)
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
 * Author: Daniel P. Berrange <berrange@redhat.com>
 */

#include <config.h>
#include <dirent.h>

#include "datatypes.h"
#include "virstorageobj.h"

#include "viralloc.h"
#include "virerror.h"
#include "virfile.h"
#include "virlog.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_STORAGE

VIR_LOG_INIT("conf.virstorageobj");

struct _virStoragePoolObjPrivate {
    char *configFile;
    char *autostartLink;
    unsigned int asyncjobs;

    virPoolObjTablePtr volumes;
};


static void
virStoragePoolObjPrivateFree(void *obj)
{
    virStoragePoolObjPrivatePtr objpriv = obj;
    if (!objpriv)
        return;

    virObjectUnref(objpriv->volumes);

    VIR_FREE(objpriv->configFile);
    VIR_FREE(objpriv->autostartLink);
    VIR_FREE(objpriv);
}


static virStoragePoolObjPrivatePtr
virStoragePoolObjPrivateAlloc(void)
{
    virStoragePoolObjPrivatePtr objpriv = NULL;

    if (VIR_ALLOC(objpriv) < 0)
        return NULL;

    if (!(objpriv->volumes =
          virPoolObjTableNew(VIR_POOLOBJTABLE_VOLUME,
                             VIR_POOLOBJTABLE_VOLUME_HASHSTART, true)))
        goto error;

    return objpriv;

 error:
    virStoragePoolObjPrivateFree(objpriv);
    return NULL;
}


const char *
virStoragePoolObjPrivateGetConfigFile(virPoolObjPtr poolobj)
{
    virStoragePoolObjPrivatePtr objpriv = virPoolObjGetPrivateData(poolobj);

    return objpriv->configFile;
}


const char *
virStoragePoolObjPrivateGetAutostartLink(virPoolObjPtr poolobj)
{
    virStoragePoolObjPrivatePtr objpriv = virPoolObjGetPrivateData(poolobj);

    return objpriv->autostartLink;
}


int
virStoragePoolObjPrivateGetAsyncjobs(virPoolObjPtr poolobj)
{
    virStoragePoolObjPrivatePtr objpriv = virPoolObjGetPrivateData(poolobj);

    return objpriv->asyncjobs;
}


void
virStoragePoolObjPrivateIncrAsyncjobs(virPoolObjPtr poolobj)
{
    virStoragePoolObjPrivatePtr objpriv = virPoolObjGetPrivateData(poolobj);

    objpriv->asyncjobs++;
}


void
virStoragePoolObjPrivateDecrAsyncjobs(virPoolObjPtr poolobj)
{
    virStoragePoolObjPrivatePtr objpriv = virPoolObjGetPrivateData(poolobj);

    objpriv->asyncjobs--;
}


virPoolObjTablePtr
virStoragePoolObjPrivateGetVolumes(virPoolObjPtr poolobj)
{
    virStoragePoolObjPrivatePtr objpriv = virPoolObjGetPrivateData(poolobj);

    return objpriv->volumes;
}


virPoolObjPtr
virStoragePoolObjAddVolume(virPoolObjPtr poolobj,
                           virStorageVolDefPtr voldef)
{
    virStoragePoolObjPrivatePtr objpriv = virPoolObjGetPrivateData(poolobj);

    return virPoolObjTableAdd(objpriv->volumes, NULL, voldef->name,
                              voldef, NULL, NULL, virStorageVolDefFree,
                              NULL, 0);
}


void
virStoragePoolObjRemoveVolume(virPoolObjPtr poolobj,
                              virPoolObjPtr *volobj)
{
    virStoragePoolObjPrivatePtr objpriv = virPoolObjGetPrivateData(poolobj);

    virPoolObjTableRemove(objpriv->volumes, volobj);
}


void
virStoragePoolObjClearVols(virPoolObjPtr poolobj)
{
    virStoragePoolObjPrivatePtr objpriv = virPoolObjGetPrivateData(poolobj);
    virPoolObjTableClearAll(objpriv->volumes);
}


struct volCountData {
    virConnectPtr conn;
    virStoragePoolDefPtr pooldef;
    virStoragePoolVolumeACLFilter aclfilter;
    int count;
};

static int
volCount(virPoolObjPtr obj,
         void *opaque)
{
    struct volCountData *data = opaque;

    /* Similar to virPoolObjTableListIterator */
    if (data->aclfilter && !data->aclfilter(data->conn, data->pooldef, obj))
        return 0;

    data->count++;
    return 0;
}


int
virStoragePoolObjNumOfVolumes(virPoolObjTablePtr volumes,
                              virConnectPtr conn,
                              virStoragePoolDefPtr pooldef,
                              virStoragePoolVolumeACLFilter aclfilter)
{
    struct volCountData data = { .conn = conn,
                                 .count = 0,
                                 .pooldef = pooldef,
                                 .aclfilter = aclfilter };

    if (virPoolObjTableList(volumes, conn, NULL, volCount, &data) < 0)
        return 0;

    return data.count;
}


struct volListData {
    virConnectPtr conn;
    virStoragePoolDefPtr pooldef;
    virStoragePoolVolumeACLFilter aclfilter;
    int nnames;
    char **const names;
    int maxnames;
};

static int
volListVolumes(virPoolObjPtr obj,
               void *opaque)
{
    virStorageVolDefPtr def = virPoolObjGetDef(obj);
    struct volListData *data = opaque;

    /* Similar to virPoolObjTableListIterator */
    if (data->aclfilter && !data->aclfilter(data->conn, data->pooldef, obj))
        return 0;

    if (data->nnames < data->maxnames) {
        if (VIR_STRDUP(data->names[data->nnames++], def->name) < 0)
            return -1;
    }

    return 0;
}


int
virStoragePoolObjListVolumes(virPoolObjTablePtr volumes,
                             virConnectPtr conn,
                             virStoragePoolDefPtr pooldef,
                             virStoragePoolVolumeACLFilter aclfilter,
                             char **const names,
                             int maxnames)
{
    struct volListData data = { .conn = conn,
                                .nnames = 0,
                                .pooldef = pooldef,
                                .aclfilter = aclfilter,
                                .names = names,
                                .maxnames = maxnames };

    memset(names, 0, maxnames * sizeof(*names));

    if (virPoolObjTableList(volumes, conn, NULL, volListVolumes, &data) < 0)
        goto error;

    return data.nnames;

 error:
    while (--data.nnames >= 0)
        VIR_FREE(names[data.nnames]);
    return -1;
}


/*
 * virStoragePoolObjIsDuplicate:
 * @pools: Pool to search
 * @def: virStoragePoolDefPtr definition of pool to lookup
 * @check_active: If true, ensure that pool is not active
 *
 * Returns: -1 on error
 *          0 if pool is new
 *          1 if pool is a duplicate
 */
int
virStoragePoolObjIsDuplicate(virPoolObjTablePtr pools,
                             virStoragePoolDefPtr def,
                             unsigned int check_active)
{
    int ret = -1;
    virPoolObjPtr obj = NULL;

    /* See if a Pool with matching UUID already exists */
    if ((obj = virPoolObjTableFindByUUIDRef(pools, def->uuid))) {
        virStoragePoolDefPtr objdef = virPoolObjGetDef(obj);

        /* UUID matches, but if names don't match, refuse it */
        if (STRNEQ(objdef->name, def->name)) {
            char uuidstr[VIR_UUID_STRING_BUFLEN];
            virUUIDFormat(objdef->uuid, uuidstr);
            virReportError(VIR_ERR_OPERATION_FAILED,
                           _("pool '%s' is already defined with uuid %s"),
                           objdef->name, uuidstr);
            goto cleanup;
        }

        if (check_active) {
            /* UUID & name match, but if Pool is already active, refuse it */
            if (virPoolObjIsActive(obj)) {
                virReportError(VIR_ERR_OPERATION_INVALID,
                               _("pool is already active as '%s'"),
                               objdef->name);
                goto cleanup;
            }
        }

        ret = 1;
    } else {
        /* UUID does not match, but if a name matches, refuse it */
        if ((obj = virPoolObjTableFindByName(pools, def->name))) {
            virStoragePoolDefPtr objdef = virPoolObjGetDef(obj);
            char uuidstr[VIR_UUID_STRING_BUFLEN];

            virUUIDFormat(objdef->uuid, uuidstr);
            virReportError(VIR_ERR_OPERATION_FAILED,
                           _("pool '%s' already exists with uuid %s"),
                           objdef->name, uuidstr);
            goto cleanup;
        }
        ret = 0;
    }

 cleanup:
    virPoolObjEndAPI(&obj);
    return ret;
}


static int
storagePoolAssignDef(virPoolObjPtr obj,
                     void *newDef,
                     void *oldDef ATTRIBUTE_UNUSED,
                     unsigned int assignFlags ATTRIBUTE_UNUSED)
{
    virStoragePoolDefPtr newdef = newDef;

    if (!virPoolObjIsActive(obj))
        virPoolObjSetDef(obj, newdef);
    else
        virPoolObjSetNewDef(obj, newdef);
    return 0;
}


virPoolObjPtr
virStoragePoolObjAdd(virPoolObjTablePtr pools,
                     virStoragePoolDefPtr def)
{
    virPoolObjPtr obj = NULL;
    virStoragePoolObjPrivatePtr objpriv = NULL;
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(def->uuid, uuidstr);

    if (!(obj = virPoolObjTableAdd(pools, uuidstr, def->name,
                                   def, NULL, NULL, virStoragePoolDefFree,
                                   storagePoolAssignDef, 0)))
        return NULL;

    if (!(objpriv = virPoolObjGetPrivateData(obj))) {
        if (!(objpriv = virStoragePoolObjPrivateAlloc()))
            goto error;

        virPoolObjSetPrivateData(obj, objpriv, virStoragePoolObjPrivateFree);
    }

    return obj;

 error:
    virPoolObjTableRemove(pools, &obj);
    virPoolObjEndAPI(&obj);
    return NULL;
}


static virPoolObjPtr
virStoragePoolObjLoad(virPoolObjTablePtr pools,
                      const char *configFile)
{
    virStoragePoolDefPtr def;

    if (!(def = virStoragePoolDefParseFile(configFile)))
        return NULL;

    if (!virFileMatchesNameSuffix(configFile, def->name, ".xml")) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("Storage pool config filename '%s' does "
                         "not match pool name '%s'"),
                       configFile, def->name);
        virStoragePoolDefFree(def);
        return NULL;
    }

    return virStoragePoolObjAdd(pools, def);
}


static virPoolObjPtr
virStoragePoolLoadState(virPoolObjTablePtr pools,
                        const char *stateDir,
                        const char *name)
{
    char *stateFile = NULL;
    virStoragePoolDefPtr def = NULL;
    virPoolObjPtr obj = NULL;
    xmlDocPtr xml = NULL;
    xmlXPathContextPtr ctxt = NULL;
    xmlNodePtr node = NULL;

    if (!(stateFile = virFileBuildPath(stateDir, name, ".xml")))
        goto error;

    if (!(xml = virXMLParseCtxt(stateFile, NULL, _("(pool state)"), &ctxt)))
        goto error;

    if (!(node = virXPathNode("//pool", ctxt))) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Could not find any 'pool' element in state file"));
        goto error;
    }

    ctxt->node = node;
    if (!(def = virStoragePoolDefParseXML(ctxt)))
        goto error;

    if (STRNEQ(name, def->name)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Storage pool state file '%s' does not match "
                         "pool name '%s'"),
                       stateFile, def->name);
        goto error;
    }

    /* create the object */
    if (!(obj = virStoragePoolObjAdd(pools, def)))
        goto error;

    /* XXX: future handling of some additional useful status data,
     * for now, if a status file for a pool exists, the pool will be marked
     * as active
     */
    virPoolObjSetActive(obj, true);

 cleanup:
    VIR_FREE(stateFile);
    xmlFreeDoc(xml);
    xmlXPathFreeContext(ctxt);
    return obj;

 error:
    virStoragePoolDefFree(def);
    goto cleanup;
}


int
virStoragePoolObjLoadAllState(virPoolObjTablePtr pools,
                              const char *stateDir)
{
    DIR *dir;
    struct dirent *entry;
    int ret = -1;
    int rc;

    if ((rc = virDirOpenIfExists(&dir, stateDir)) <= 0)
        return rc;

    while ((ret = virDirRead(dir, &entry, stateDir)) > 0) {
        virPoolObjPtr obj;

        if (!virFileStripSuffix(entry->d_name, ".xml"))
            continue;

        if (!(obj = virStoragePoolLoadState(pools, stateDir, entry->d_name)))
            continue;
        virPoolObjEndAPI(&obj);
    }

    VIR_DIR_CLOSE(dir);
    return ret;
}


int
virStoragePoolObjLoadAllConfigs(virPoolObjTablePtr pools,
                                const char *configDir,
                                const char *autostartDir)
{
    DIR *dir;
    struct dirent *entry;
    int ret;
    int rc;

    if ((rc = virDirOpenIfExists(&dir, configDir)) <= 0)
        return rc;

    while ((ret = virDirRead(dir, &entry, configDir)) > 0) {
        char *configFile;
        char *autostartLink;
        virPoolObjPtr obj;
        virStoragePoolObjPrivatePtr objpriv;

        if (!virFileHasSuffix(entry->d_name, ".xml"))
            continue;

        if (!(configFile = virFileBuildPath(configDir, entry->d_name, NULL)))
            continue;

        if (!(autostartLink = virFileBuildPath(autostartDir, entry->d_name,
                                               NULL))) {
            VIR_FREE(configFile);
            continue;
        }

        if (!(obj = virStoragePoolObjLoad(pools, configFile))) {
            VIR_FREE(configFile);
            VIR_FREE(autostartLink);
            continue;
        }
        objpriv = virPoolObjGetPrivateData(obj);

        /* for driver reload */
        VIR_FREE(objpriv->configFile);
        VIR_FREE(objpriv->autostartLink);

        VIR_STEAL_PTR(objpriv->configFile, configFile);
        VIR_STEAL_PTR(objpriv->autostartLink, autostartLink);

        if (virFileLinkPointsTo(objpriv->autostartLink, objpriv->configFile))
            virPoolObjSetAutostart(obj, true);
        else
            virPoolObjSetAutostart(obj, false);

        virPoolObjEndAPI(&obj);
    }

    VIR_DIR_CLOSE(dir);
    return ret;
}


int
virStoragePoolObjSaveDef(virStorageDriverStatePtr driver,
                         virPoolObjPtr obj)
{
    virStoragePoolDefPtr def = virPoolObjGetDef(obj);
    virStoragePoolObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    if (!objpriv->configFile) {
        if (virFileMakePath(driver->configDir) < 0) {
            virReportSystemError(errno,
                                 _("cannot create config directory %s"),
                                 driver->configDir);
            return -1;
        }

        if (!(objpriv->configFile = virFileBuildPath(driver->configDir,
                                                     def->name, ".xml"))) {
            return -1;
        }

        if (!(objpriv->autostartLink = virFileBuildPath(driver->autostartDir,
                                                        def->name, ".xml"))) {
            VIR_FREE(objpriv->configFile);
            return -1;
        }
    }

    return virStoragePoolSaveConfig(objpriv->configFile, def);
}


int
virStoragePoolObjDeleteDef(virPoolObjPtr obj)
{
    virStoragePoolDefPtr def = virPoolObjGetDef(obj);
    virStoragePoolObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    if (!objpriv->configFile) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("no config file for %s"), def->name);
        return -1;
    }

    if (unlink(objpriv->configFile) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("cannot remove config for %s"),
                       def->name);
        return -1;
    }

    if (unlink(objpriv->autostartLink) < 0 &&
        errno != ENOENT && errno != ENOTDIR) {
        char ebuf[1024];

        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to delete autostart link '%s': %s"),
                       objpriv->autostartLink,
                       virStrerror(errno, ebuf, sizeof(ebuf)));
        return -1;
    }

    return 0;
}

struct storageCountData {
    bool wantActive;
    int count;
};

static int
storageCount(virPoolObjPtr obj,
             void *opaque)
{
    struct storageCountData *data = opaque;

    if ((data->wantActive && virPoolObjIsActive(obj)) ||
        (!data->wantActive && !virPoolObjIsActive(obj)))
        data->count++;

    return 0;
}


int
virStoragePoolObjNumOfStoragePools(virPoolObjTablePtr pools,
                                   virConnectPtr conn,
                                   bool wantActive,
                                   virPoolObjACLFilter aclfilter)
{
    struct storageCountData data = { .count = 0,
                                     .wantActive = wantActive };

    if (virPoolObjTableList(pools, conn, aclfilter, storageCount, &data) < 0)
        return 0;

    return data.count;
}


struct poolNameData {
    bool wantActive;
    int nnames;
    char **const names;
    int maxnames;
};

static int
storageGetNames(virPoolObjPtr obj ATTRIBUTE_UNUSED,
                void *opaque)
{
    struct poolNameData *data = opaque;

    if (data->nnames < data->maxnames) {
        if (data->wantActive && virPoolObjIsActive(obj)) {
            virStoragePoolDefPtr def = virPoolObjGetDef(obj);

            if (VIR_STRDUP(data->names[data->nnames++], def->name) < 0)
                return -1;
        } else if (!data->wantActive && !virPoolObjIsActive(obj)) {
            virStoragePoolDefPtr def = virPoolObjGetDef(obj);

            if (VIR_STRDUP(data->names[data->nnames++], def->name) < 0)
                return -1;
        }
    }
    return 0;
}


int
virStoragePoolObjGetNames(virPoolObjTablePtr pools,
                          virConnectPtr conn,
                          bool wantActive,
                          virPoolObjACLFilter aclfilter,
                          char **const names,
                          int maxnames)
{
    struct poolNameData data = { .wantActive = wantActive,
                                 .nnames = 0,
                                 .names = names,
                                 .maxnames = maxnames };

    memset(names, 0, sizeof(*names) * maxnames);
    if (virPoolObjTableList(pools, conn, aclfilter,
                            storageGetNames, &data) < 0)
        goto error;

    return data.nnames;

 error:
    while (--data.nnames >= 0)
        VIR_FREE(names[data.nnames]);
    return -1;
}


static int
getSCSIHostNumber(virStoragePoolSourceAdapter adapter,
                  unsigned int *hostnum)
{
    int ret = -1;
    unsigned int num;
    char *name = NULL;

    if (adapter.data.scsi_host.has_parent) {
        virPCIDeviceAddress addr = adapter.data.scsi_host.parentaddr;
        unsigned int unique_id = adapter.data.scsi_host.unique_id;

        if (!(name = virGetSCSIHostNameByParentaddr(addr.domain,
                                                    addr.bus,
                                                    addr.slot,
                                                    addr.function,
                                                    unique_id)))
            goto cleanup;
        if (virGetSCSIHostNumber(name, &num) < 0)
            goto cleanup;
    } else {
        if (virGetSCSIHostNumber(adapter.data.scsi_host.name, &num) < 0)
            goto cleanup;
    }

    *hostnum = num;
    ret = 0;

 cleanup:
    VIR_FREE(name);
    return ret;
}


/*
 * matchFCHostToSCSIHost:
 *
 * @conn: Connection pointer
 * @fc_adapter: fc_host adapter (either def or pool->def)
 * @scsi_hostnum: Already determined "scsi_pool" hostnum
 *
 * Returns true/false whether there is a match between the incoming
 *         fc_adapter host# and the scsi_host host#
 */
static bool
matchFCHostToSCSIHost(virConnectPtr conn,
                      virStoragePoolSourceAdapter fc_adapter,
                      unsigned int scsi_hostnum)
{
    char *name = NULL;
    char *parent_name = NULL;
    unsigned int fc_hostnum;

    /* If we have a parent defined, get its hostnum, and compare to the
     * scsi_hostnum. If they are the same, then we have a match
     */
    if (fc_adapter.data.fchost.parent &&
        virGetSCSIHostNumber(fc_adapter.data.fchost.parent, &fc_hostnum) == 0 &&
        scsi_hostnum == fc_hostnum)
        return true;

    /* If we find an fc_adapter name, then either libvirt created a vHBA
     * for this fc_host or a 'virsh nodedev-create' generated a vHBA.
     */
    if ((name = virGetFCHostNameByWWN(NULL, fc_adapter.data.fchost.wwnn,
                                      fc_adapter.data.fchost.wwpn))) {

        /* Get the scsi_hostN for the vHBA in order to see if it
         * matches our scsi_hostnum
         */
        if (virGetSCSIHostNumber(name, &fc_hostnum) == 0 &&
            scsi_hostnum == fc_hostnum) {
            VIR_FREE(name);
            return true;
        }

        /* We weren't provided a parent, so we have to query the node
         * device driver in order to ascertain the parent of the vHBA.
         * If the parent fc_hostnum is the same as the scsi_hostnum, we
         * have a match.
         */
        if (conn && !fc_adapter.data.fchost.parent) {
            parent_name = virStoragePoolGetVhbaSCSIHostParent(conn, name);
            if (parent_name) {
                if (virGetSCSIHostNumber(parent_name, &fc_hostnum) == 0 &&
                    scsi_hostnum == fc_hostnum) {
                    VIR_FREE(parent_name);
                    VIR_FREE(name);
                    return true;
                }
                VIR_FREE(parent_name);
            } else {
                /* Throw away the error and fall through */
                virResetLastError();
                VIR_DEBUG("Could not determine parent vHBA");
            }
        }
        VIR_FREE(name);
    }

    /* NB: Lack of a name means that this vHBA hasn't yet been created,
     *     which means our scsi_host cannot be using the vHBA. Furthermore,
     *     lack of a provided parent means libvirt is going to choose the
     *     "best" fc_host capable adapter based on availabilty. That could
     *     conflict with an existing scsi_host definition, but there's no
     *     way to know that now.
     */
    return false;
}


static bool
matchSCSIAdapterParent(virPoolObjPtr obj,
                       virStoragePoolDefPtr def)
{
    virStoragePoolDefPtr objdef = virPoolObjGetDef(obj);
    virPCIDeviceAddressPtr objaddr =
        &objdef->source.adapter.data.scsi_host.parentaddr;
    virPCIDeviceAddressPtr defaddr =
        &def->source.adapter.data.scsi_host.parentaddr;
    int obj_unique_id =
        objdef->source.adapter.data.scsi_host.unique_id;
    int def_unique_id =
        def->source.adapter.data.scsi_host.unique_id;

    return (objaddr->domain == defaddr->domain &&
            objaddr->bus == defaddr->bus &&
            objaddr->slot == defaddr->slot &&
            objaddr->function == defaddr->function &&
            obj_unique_id == def_unique_id);
}


static bool
virStoragePoolSourceMatchSingleHost(virStoragePoolSourcePtr poolsrc,
                                    virStoragePoolSourcePtr defsrc)
{
    if (poolsrc->nhost != 1 && defsrc->nhost != 1)
        return false;

    if (defsrc->hosts[0].port &&
        poolsrc->hosts[0].port != defsrc->hosts[0].port)
        return false;

    return STREQ(poolsrc->hosts[0].name, defsrc->hosts[0].name);
}


static bool
virStoragePoolSourceISCSIMatch(virPoolObjPtr obj,
                               virStoragePoolDefPtr def)
{
    virStoragePoolDefPtr pooldef = virPoolObjGetDef(obj);
    virStoragePoolSourcePtr objsrc = &pooldef->source;
    virStoragePoolSourcePtr defsrc = &def->source;

    /* NB: Do not check the source host name */
    if (STRNEQ_NULLABLE(objsrc->initiator.iqn, defsrc->initiator.iqn))
        return false;

    return true;
}


struct storagePoolDuplicateData {
    virConnectPtr conn;
    virStoragePoolDefPtr def;
};

static bool
storagePoolSourceFindDuplicateDevices(virPoolObjPtr obj,
                                      virStoragePoolDefPtr def)
{
    virStoragePoolDefPtr objdef = virPoolObjGetDef(obj);
    size_t i, j;

    for (i = 0; i < objdef->source.ndevice; i++) {
        for (j = 0; j < def->source.ndevice; j++) {
            if (STREQ(objdef->source.devices[i].path,
                      def->source.devices[j].path))
                return true;
        }
    }

    return false;
}


static bool
storagePoolSourceFindDuplicate(virPoolObjPtr obj,
                               void *opaque)
{
    virStoragePoolDefPtr def = virPoolObjGetDef(obj);
    struct storagePoolDuplicateData *data = opaque;

    /* Check the pool list for duplicate underlying storage */
    if (data->def->type != def->type)
        return false;

    /* Don't match against ourself if re-defining existing pool ! */
    if (STREQ(def->name, data->def->name))
        return false;

    switch ((virStoragePoolType)def->type) {
    case VIR_STORAGE_POOL_DIR:
        if (STREQ(def->target.path, data->def->target.path))
            return true;
        break;

    case VIR_STORAGE_POOL_GLUSTER:
        if (STREQ(def->source.name, data->def->source.name) &&
            STREQ_NULLABLE(def->source.dir, data->def->source.dir) &&
            virStoragePoolSourceMatchSingleHost(&def->source,
                                                &data->def->source))
            return true;
        break;

    case VIR_STORAGE_POOL_NETFS:
        if (STREQ(def->source.dir, data->def->source.dir) &&
            virStoragePoolSourceMatchSingleHost(&def->source,
                                                &data->def->source))
            return true;
        break;

    case VIR_STORAGE_POOL_SCSI:
        if (def->source.adapter.type ==
            VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_FC_HOST &&
            data->def->source.adapter.type ==
            VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_FC_HOST) {
            if (STREQ(def->source.adapter.data.fchost.wwnn,
                      data->def->source.adapter.data.fchost.wwnn) &&
                STREQ(def->source.adapter.data.fchost.wwpn,
                      data->def->source.adapter.data.fchost.wwpn))
                return true;
        } else if (def->source.adapter.type ==
                   VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_SCSI_HOST &&
                   data->def->source.adapter.type ==
                   VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_SCSI_HOST) {
            unsigned int pool_hostnum, def_hostnum;

            if (def->source.adapter.data.scsi_host.has_parent &&
                data->def->source.adapter.data.scsi_host.has_parent &&
                matchSCSIAdapterParent(obj, data->def))
                return true;

            if (getSCSIHostNumber(def->source.adapter,
                                  &pool_hostnum) < 0 ||
                getSCSIHostNumber(data->def->source.adapter, &def_hostnum) < 0)
                break;
            if (pool_hostnum == def_hostnum)
                return true;
        } else if (def->source.adapter.type ==
                   VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_FC_HOST &&
                   data->def->source.adapter.type ==
                   VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_SCSI_HOST) {
            unsigned int scsi_hostnum;

            /* Get the scsi_hostN for the scsi_host source adapter def */
            if (getSCSIHostNumber(data->def->source.adapter,
                                  &scsi_hostnum) < 0)
                break;

            if (matchFCHostToSCSIHost(data->conn, def->source.adapter,
                                      scsi_hostnum)) {
                return true;
            }

        } else if (def->source.adapter.type ==
                   VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_SCSI_HOST &&
                   data->def->source.adapter.type ==
                   VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_FC_HOST) {
            unsigned int scsi_hostnum;

            if (getSCSIHostNumber(def->source.adapter,
                                  &scsi_hostnum) < 0)
                break;

            if (matchFCHostToSCSIHost(data->conn, data->def->source.adapter,
                                      scsi_hostnum))
                return true;
        }
        break;
    case VIR_STORAGE_POOL_ISCSI:
        if (storagePoolSourceFindDuplicateDevices(obj, data->def)) {
            if (virStoragePoolSourceISCSIMatch(obj, data->def))
                return true;
        }
        break;
    case VIR_STORAGE_POOL_FS:
    case VIR_STORAGE_POOL_LOGICAL:
    case VIR_STORAGE_POOL_DISK:
    case VIR_STORAGE_POOL_ZFS:
        if (storagePoolSourceFindDuplicateDevices(obj, data->def))
            return true;
        break;
    case VIR_STORAGE_POOL_SHEEPDOG:
        if (virStoragePoolSourceMatchSingleHost(&def->source,
                                                &data->def->source))
            return true;
        break;
    case VIR_STORAGE_POOL_MPATH:
        /* Only one mpath pool is valid per host */
        return true;
        break;
    case VIR_STORAGE_POOL_VSTORAGE:
        if (STREQ(def->source.name, def->source.name))
            return true;
        break;
    case VIR_STORAGE_POOL_RBD:
    case VIR_STORAGE_POOL_LAST:
        break;
    }

    return false;
}


bool
virStoragePoolObjFindDuplicate(virPoolObjTablePtr pools,
                               virConnectPtr conn,
                               virStoragePoolDefPtr def)
{
    virPoolObjPtr obj;
    struct storagePoolDuplicateData data = { .conn = conn,
                                             .def = def };

    if ((obj = virPoolObjTableSearchRef(pools, storagePoolSourceFindDuplicate,
                                        &data))) {
        virStoragePoolDefPtr objdef = virPoolObjGetDef(obj);
        virReportError(VIR_ERR_OPERATION_FAILED,
                       _("Storage source conflict with pool: '%s'"),
                       objdef->name);
        virPoolObjEndAPI(&obj);
        return true;
    }

    return false;

}


#define MATCH(FLAG) (flags & (FLAG))
static bool
virStoragePoolMatch(virPoolObjPtr obj,
                    unsigned int flags)
{
    virStoragePoolDefPtr def = virPoolObjGetDef(obj);
    virStoragePoolObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    /* filter by active state */
    if (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_FILTERS_ACTIVE) &&
        !((MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_ACTIVE) &&
           virPoolObjIsActive(obj)) ||
          (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_INACTIVE) &&
           !virPoolObjIsActive(obj))))
        return false;

    /* filter by persistence */
    if (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_FILTERS_PERSISTENT) &&
        !((MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_PERSISTENT) &&
           objpriv->configFile) ||
          (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_TRANSIENT) &&
           !objpriv->configFile)))
        return false;

    /* filter by autostart option */
    if (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_FILTERS_AUTOSTART) &&
        !((MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_AUTOSTART) &&
           virPoolObjIsAutostart(obj)) ||
          (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_NO_AUTOSTART) &&
           !virPoolObjIsAutostart(obj))))
        return false;

    /* filter by pool type */
    if (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_FILTERS_POOL_TYPE)) {
        if (!((MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_DIR) &&
               (def->type == VIR_STORAGE_POOL_DIR))     ||
              (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_FS) &&
               (def->type == VIR_STORAGE_POOL_FS))      ||
              (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_NETFS) &&
               (def->type == VIR_STORAGE_POOL_NETFS))   ||
              (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_LOGICAL) &&
               (def->type == VIR_STORAGE_POOL_LOGICAL)) ||
              (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_DISK) &&
               (def->type == VIR_STORAGE_POOL_DISK))    ||
              (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_ISCSI) &&
               (def->type == VIR_STORAGE_POOL_ISCSI))   ||
              (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_SCSI) &&
               (def->type == VIR_STORAGE_POOL_SCSI))    ||
              (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_MPATH) &&
               (def->type == VIR_STORAGE_POOL_MPATH))   ||
              (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_RBD) &&
               (def->type == VIR_STORAGE_POOL_RBD))     ||
              (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_SHEEPDOG) &&
               (def->type == VIR_STORAGE_POOL_SHEEPDOG)) ||
              (MATCH(VIR_CONNECT_LIST_STORAGE_POOLS_GLUSTER) &&
               (def->type == VIR_STORAGE_POOL_GLUSTER))))
            return false;
    }

    return true;
}
#undef MATCH

int
virStoragePoolObjExportList(virConnectPtr conn,
                            virPoolObjTablePtr poolobjs,
                            virStoragePoolPtr **pools,
                            virPoolObjACLFilter aclfilter,
                            unsigned int flags)
{
    virPoolObjPtr *objs = NULL;
    size_t nobjs;
    virStoragePoolPtr *tmp_pools = NULL;
    size_t i;

    if (virPoolObjTableCollect(poolobjs, conn, &objs, &nobjs, aclfilter,
                               virStoragePoolMatch, flags) < 0)
        return -1;

    if (pools) {
        if (VIR_ALLOC_N(tmp_pools, nobjs + 1) < 0)
            goto cleanup;

        for (i = 0; i < nobjs; i++) {
            virStoragePoolDefPtr def;

            virObjectLock(objs[i]);
            def = virPoolObjGetDef(objs[i]);
            tmp_pools[i] = virGetStoragePool(conn, def->name, def->uuid,
                                             NULL, NULL);
            virObjectUnlock(objs[i]);
            if (!tmp_pools[i])
                goto cleanup;
        }

        VIR_STEAL_PTR(*pools, tmp_pools);
    }

 cleanup:
    virObjectListFree(tmp_pools);
    virObjectListFreeCount(objs, nobjs);

    return nobjs;
}


struct volSearchData {
    const char *compare;
};

static bool
volFindByKey(virPoolObjPtr obj,
             void *opaque)
{
    virStorageVolDefPtr def = virPoolObjGetDef(obj);
    struct volSearchData *data = opaque;

    if (STREQ(def->key, data->compare))
        return true;

    return false;
}


virPoolObjPtr
virStorageVolObjFindByKey(virPoolObjPtr poolobj,
                          const char *key)
{
    virStoragePoolObjPrivatePtr objpriv = virPoolObjGetPrivateData(poolobj);
    struct volSearchData data = { .compare = key };

    return virPoolObjTableSearchRef(objpriv->volumes, volFindByKey, &data);
}


static bool
volFindByPath(virPoolObjPtr obj,
              void *opaque)
{
    virStorageVolDefPtr def = virPoolObjGetDef(obj);
    struct volSearchData *data = opaque;

    if (STREQ(def->target.path, data->compare))
        return true;

    return false;
}


virPoolObjPtr
virStorageVolObjFindByPath(virPoolObjPtr poolobj,
                           const char *path)
{
    virStoragePoolObjPrivatePtr objpriv = virPoolObjGetPrivateData(poolobj);
    struct volSearchData data = { .compare = path };

    return virPoolObjTableSearchRef(objpriv->volumes, volFindByPath, &data);
}


virPoolObjPtr
virStorageVolObjFindByName(virPoolObjPtr poolobj,
                           const char *name)
{
    virStoragePoolObjPrivatePtr objpriv = virPoolObjGetPrivateData(poolobj);

    return virPoolObjTableFindByName(objpriv->volumes, name);
}
