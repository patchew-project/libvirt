/*
 * virnodedeviceobj.c: node device object handling
 *                     (derived from node_device_conf.c)
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
#include "node_device_conf.h"

#include "viralloc.h"
#include "virnodedeviceobj.h"
#include "virerror.h"
#include "virlog.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NODEDEV

VIR_LOG_INIT("conf.virnodedeviceobj");

static int
nodeDeviceHasCap(const virNodeDeviceDef *def,
                 const char *cap)
{
    virNodeDevCapsDefPtr caps = def->caps;
    const char *fc_host_cap =
        virNodeDevCapTypeToString(VIR_NODE_DEV_CAP_FC_HOST);
    const char *vports_cap =
        virNodeDevCapTypeToString(VIR_NODE_DEV_CAP_VPORTS);

    while (caps) {
        if (STREQ(cap, virNodeDevCapTypeToString(caps->data.type)))
            return 1;
        else if (caps->data.type == VIR_NODE_DEV_CAP_SCSI_HOST)
            if ((STREQ(cap, fc_host_cap) &&
                (caps->data.scsi_host.flags &
                 VIR_NODE_DEV_CAP_FLAG_HBA_FC_HOST)) ||
                (STREQ(cap, vports_cap) &&
                (caps->data.scsi_host.flags &
                 VIR_NODE_DEV_CAP_FLAG_HBA_VPORT_OPS)))
                return 1;
        caps = caps->next;
    }
    return 0;
}


/* nodeDeviceFindFCCapDef:
 * @def: Node device definition
 *
 * Search the device object 'caps' array for fc_host capability.
 *
 * Returns:
 * Pointer to the caps or NULL if not found
 */
static virNodeDevCapsDefPtr
nodeDeviceFindFCCapDef(const virNodeDeviceDef *def)
{
    virNodeDevCapsDefPtr caps = def->caps;

    while (caps) {
        if (caps->data.type == VIR_NODE_DEV_CAP_SCSI_HOST &&
            (caps->data.scsi_host.flags & VIR_NODE_DEV_CAP_FLAG_HBA_FC_HOST))
            break;

        caps = caps->next;
    }
    return caps;
}


/* nodeDeviceFindVPORTCapDef:
 * @def: Node device definition
 *
 * Search the device object 'caps' array for vport_ops capability.
 *
 * Returns:
 * Pointer to the caps or NULL if not found
 */
static virNodeDevCapsDefPtr
nodeDeviceFindVPORTCapDef(const virNodeDeviceDef *def)
{
    virNodeDevCapsDefPtr caps = def->caps;

    while (caps) {
        if (caps->data.type == VIR_NODE_DEV_CAP_SCSI_HOST &&
            (caps->data.scsi_host.flags & VIR_NODE_DEV_CAP_FLAG_HBA_VPORT_OPS))
            break;

        caps = caps->next;
    }
    return caps;
}


struct searchData {
    const char *sysfs_path;
    const char *cap;
    const char *parent_wwnn;
    const char *parent_wwpn;
    const char *parent_fabric_wwn;
};


static bool
searchBySysfsPath(virPoolObjPtr obj,
                  void *opaque)
{
    virNodeDeviceDefPtr def = virPoolObjGetDef(obj);
    struct searchData *data = opaque;

    if (STREQ_NULLABLE(def->sysfs_path, data->sysfs_path))
        return true;

    return false;
}


static bool
searchByCap(virPoolObjPtr obj,
            void *opaque)
{
    virNodeDeviceDefPtr def = virPoolObjGetDef(obj);
    struct searchData *data = opaque;

    if (nodeDeviceHasCap(def, data->cap))
        return true;

    return false;
}


static bool
searchByWWNs(virPoolObjPtr obj,
             void *opaque)
{
    virNodeDeviceDefPtr def = virPoolObjGetDef(obj);
    struct searchData *data = opaque;
    virNodeDevCapsDefPtr cap;

    if ((cap = nodeDeviceFindFCCapDef(def)) &&
        STREQ_NULLABLE(cap->data.scsi_host.wwnn, data->parent_wwnn) &&
        STREQ_NULLABLE(cap->data.scsi_host.wwpn, data->parent_wwpn))
        return true;

    return false;
}


static bool
searchByFabricWWN(virPoolObjPtr obj,
                  void *opaque)
{
    virNodeDeviceDefPtr def = virPoolObjGetDef(obj);
    struct searchData *data = opaque;
    virNodeDevCapsDefPtr cap;

    if ((cap = nodeDeviceFindFCCapDef(def)) &&
        STREQ_NULLABLE(cap->data.scsi_host.fabric_wwn, data->parent_fabric_wwn))
        return true;

    return false;
}


/* virNodeDeviceObjFindBy{SysfsPath|Cap|WWNs|FabricWWN}
 * @devs: Pointer to pool object table
 *
 * API specific:
 * @sysfs_path: Sysfs path to search
 * @cap: Capability name to search
 * @wwnn & @wwpn: WWNN & WWPN name to search
 * @fabric_wwn: Fabric_WWN to search
 *
 * Search by API specific argument in the table to return a pool object
 * matching the argument specifics
 *
 * Returns: NULL if not found, a locked and ref'd object reference that
 *          needs a virPoolObjEndAPI to clear
 */
virPoolObjPtr
virNodeDeviceObjFindBySysfsPath(virPoolObjTablePtr devs,
                                const char *sysfs_path)
{
    struct searchData data = { .sysfs_path = sysfs_path };

    return virPoolObjTableSearchRef(devs, searchBySysfsPath, &data);
}


static virPoolObjPtr
nodeDeviceFindByCap(virPoolObjTablePtr devs,
                    const char *cap)
{
    struct searchData data = { .cap = cap };

    return virPoolObjTableSearchRef(devs, searchByCap, &data);
}


static virPoolObjPtr
nodeDeviceFindByWWNs(virPoolObjTablePtr devs,
                     const char *parent_wwnn,
                     const char *parent_wwpn)
{
    struct searchData data = { .parent_wwnn = parent_wwnn,
                               .parent_wwpn = parent_wwpn };

    return virPoolObjTableSearchRef(devs, searchByWWNs, &data);
}


static virPoolObjPtr
nodeDeviceFindByFabricWWN(virPoolObjTablePtr devs,
                          const char *parent_fabric_wwn)
{
    struct searchData data = { .parent_fabric_wwn = parent_fabric_wwn };

    return virPoolObjTableSearchRef(devs, searchByFabricWWN, &data);
}


/*
 * Return the NPIV dev's parent device name
 */
/* nodeDeviceFindFCParentHost:
 * @def: Pointer to node device def
 * @parent_host: Pointer to return parent host number
 *
 * Search the capabilities for the device to find the FC capabilities
 * in order to set the parent_host value.
 *
 * Returns:
 *   0 on success with parent_host set, -1 otherwise;
 */
static int
nodeDeviceFindFCParentHost(const virNodeDeviceDef *def,
                           int *parent_host)
{
    virNodeDevCapsDefPtr cap = nodeDeviceFindVPORTCapDef(def);

    if (!cap) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Parent device %s is not capable "
                         "of vport operations"),
                       def->name);
        return -1;
    }

    *parent_host = cap->data.scsi_host.host;
    return 0;
}


int
virNodeDeviceObjGetParentHost(virPoolObjTablePtr devs,
                              const char *dev_name,
                              const char *parent_name,
                              int *parent_host)
{
    virPoolObjPtr obj;
    virNodeDeviceDefPtr def;
    int ret;

    if (!(obj = virPoolObjTableFindByName(devs, parent_name))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not find parent device for '%s'"),
                       dev_name);
        return -1;
    }
    def = virPoolObjGetDef(obj);

    ret = nodeDeviceFindFCParentHost(def, parent_host);

    virPoolObjEndAPI(&obj);
    return ret;
}


int
virNodeDeviceObjGetParentHostByWWNs(virPoolObjTablePtr devs,
                                    const char *dev_name,
                                    const char *parent_wwnn,
                                    const char *parent_wwpn,
                                    int *parent_host)
{
    virPoolObjPtr obj = NULL;
    virNodeDeviceDefPtr def;
    int ret;

    if (!(obj = nodeDeviceFindByWWNs(devs, parent_wwnn, parent_wwpn))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not find parent device for '%s'"),
                       dev_name);
        return -1;
    }
    def = virPoolObjGetDef(obj);

    ret = nodeDeviceFindFCParentHost(def, parent_host);

    virPoolObjEndAPI(&obj);

    return ret;
}


int
virNodeDeviceObjGetParentHostByFabricWWN(virPoolObjTablePtr devs,
                                         const char *dev_name,
                                         const char *parent_fabric_wwn,
                                         int *parent_host)
{
    virPoolObjPtr obj;
    virNodeDeviceDefPtr def;
    int ret;

    if (!(obj = nodeDeviceFindByFabricWWN(devs, parent_fabric_wwn))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not find parent device for '%s'"),
                       dev_name);
        return -1;
    }
    def = virPoolObjGetDef(obj);

    ret = nodeDeviceFindFCParentHost(def, parent_host);

    virPoolObjEndAPI(&obj);

    return ret;
}


int
virNodeDeviceObjFindVportParentHost(virPoolObjTablePtr devs,
                                    int *parent_host)
{
    virPoolObjPtr obj;
    virNodeDeviceDefPtr def;
    const char *cap = virNodeDevCapTypeToString(VIR_NODE_DEV_CAP_VPORTS);
    int ret;

    if (!(obj = nodeDeviceFindByCap(devs, cap))) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Could not find any vport capable device"));
        return -1;
    }
    def = virPoolObjGetDef(obj);

    ret = nodeDeviceFindFCParentHost(def, parent_host);

    virPoolObjEndAPI(&obj);
    return ret;
}


static bool
nodeDeviceCapMatch(virPoolObjPtr obj,
                   int type)
{
    virNodeDeviceDefPtr def = virPoolObjGetDef(obj);
    virNodeDevCapsDefPtr cap = NULL;

    for (cap = def->caps; cap; cap = cap->next) {
        if (type == cap->data.type)
            return true;

        if (cap->data.type == VIR_NODE_DEV_CAP_SCSI_HOST) {
            if (type == VIR_NODE_DEV_CAP_FC_HOST &&
                (cap->data.scsi_host.flags &
                 VIR_NODE_DEV_CAP_FLAG_HBA_FC_HOST))
                return true;

            if (type == VIR_NODE_DEV_CAP_VPORTS &&
                (cap->data.scsi_host.flags &
                 VIR_NODE_DEV_CAP_FLAG_HBA_VPORT_OPS))
                return true;
        }
    }

    return false;
}


virPoolObjPtr
virNodeDeviceObjFindByName(virPoolObjTablePtr devobjs,
                           const char *name)
{
    virPoolObjPtr obj;

    if (!(obj = virPoolObjTableFindByName(devobjs, name)))
        virReportError(VIR_ERR_NO_NODE_DEVICE,
                       _("no node device with matching name '%s'"),
                       name);

    return obj;
}


struct nodeCountData {
    const char *cap;
    int count;
};

static int
nodeCount(virPoolObjPtr obj,
          void *opaque)
{
    virNodeDeviceDefPtr def = virPoolObjGetDef(obj);
    struct nodeCountData *data = opaque;

    if (!data->cap || nodeDeviceHasCap(def, data->cap))
        data->count++;

    return 0;
}


int
virNodeDeviceObjNumOfDevices(virPoolObjTablePtr devobjs,
                             virConnectPtr conn,
                             const char *cap,
                             virPoolObjACLFilter aclfilter)
{
    struct nodeCountData data = { .cap = cap,
                                  .count = 0 };

    if (virPoolObjTableList(devobjs, conn, aclfilter, nodeCount, &data) < 0)
        return 0;

    return data.count;
}


struct nodedevNameData {
    const char *cap;
    int nnames;
    char **const names;
    int maxnames;
};

static int
nodedevGetNames(virPoolObjPtr obj,
                void *opaque)
{
    virNodeDeviceDefPtr def = virPoolObjGetDef(obj);
    struct nodedevNameData *data = opaque;

    if (data->nnames < data->maxnames) {
        if (!data->cap || nodeDeviceHasCap(def, data->cap)) {
            if (VIR_STRDUP(data->names[data->nnames++], def->name) < 0)
                return -1;
        }
    }
    return 0;
}


int
virNodeDeviceObjGetNames(virPoolObjTablePtr devobjs,
                         virConnectPtr conn,
                         virPoolObjACLFilter aclfilter,
                         const char *cap,
                         char **const names,
                         int maxnames)
{
    struct nodedevNameData data = { .cap = cap,
                                    .nnames = 0,
                                    .names = names,
                                    .maxnames = maxnames };

    if (virPoolObjTableList(devobjs, conn, aclfilter,
                            nodedevGetNames, &data) < 0)
        goto failure;

    return data.nnames;

 failure:
    while (--data.nnames >= 0)
        VIR_FREE(names[data.nnames]);
    return -1;
}


#define MATCH(FLAG) ((flags & (VIR_CONNECT_LIST_NODE_DEVICES_CAP_ ## FLAG)) && \
                     nodeDeviceCapMatch(obj, VIR_NODE_DEV_CAP_ ## FLAG))
static bool
nodeDeviceMatchFilter(virPoolObjPtr obj,
                      unsigned int flags)
{
    /* filter by cap type */
    if (flags & VIR_CONNECT_LIST_NODE_DEVICES_FILTERS_CAP) {
        if (!(MATCH(SYSTEM)        ||
              MATCH(PCI_DEV)       ||
              MATCH(USB_DEV)       ||
              MATCH(USB_INTERFACE) ||
              MATCH(NET)           ||
              MATCH(SCSI_HOST)     ||
              MATCH(SCSI_TARGET)   ||
              MATCH(SCSI)          ||
              MATCH(STORAGE)       ||
              MATCH(FC_HOST)       ||
              MATCH(VPORTS)        ||
              MATCH(SCSI_GENERIC)))
            return false;
    }

    return true;
}
#undef MATCH


int
virNodeDeviceObjExportList(virConnectPtr conn,
                           virPoolObjTablePtr devobjs,
                           virNodeDevicePtr **devices,
                           virPoolObjACLFilter aclfilter,
                           unsigned int flags)
{
    virPoolObjPtr *objs = NULL;
    virNodeDevicePtr *devs = NULL;
    size_t nobjs = 0;
    int ret = -1;
    size_t i;

    if (virPoolObjTableCollect(devobjs, conn, &objs, &nobjs, aclfilter,
                               nodeDeviceMatchFilter, flags) < 0)
        return -1;

    if (devices) {
        if (VIR_ALLOC_N(devs, nobjs + 1) < 0)
            goto cleanup;

        for (i = 0; i < nobjs; i++) {
            virPoolObjPtr obj = objs[i];
            virNodeDeviceDefPtr def;

            virObjectLock(obj);
            def = virPoolObjGetDef(obj);
            devs[i] = virGetNodeDevice(conn, def->name);
            virObjectUnlock(obj);

            if (!devs[i])
                goto cleanup;
        }

        *devices = devs;
        devs = NULL;
    }

    ret = nobjs;

 cleanup:
    virObjectListFree(devs);
    virObjectListFreeCount(objs, nobjs);
    return ret;
}
