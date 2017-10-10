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
#include "virhash.h"
#include "virlog.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NODEDEV

VIR_LOG_INIT("conf.virnodedeviceobj");

struct _virNodeDeviceObj {
    virObjectLockable parent;

    virNodeDeviceDefPtr def;		/* device definition */
};

struct _virNodeDeviceObjList {
    virObjectRWLockable parent;

    /* name string -> virNodeDeviceObj mapping
     * for O(1), lockless lookup-by-name */
    virHashTable *objs;

};


static virClassPtr virNodeDeviceObjClass;
static virClassPtr virNodeDeviceObjListClass;
static void virNodeDeviceObjDispose(void *opaque);
static void virNodeDeviceObjListDispose(void *opaque);

static int
virNodeDeviceObjOnceInit(void)
{
    if (!(virNodeDeviceObjClass = virClassNew(virClassForObjectLockable(),
                                              "virNodeDeviceObj",
                                              sizeof(virNodeDeviceObj),
                                              virNodeDeviceObjDispose)))
        return -1;

    if (!(virNodeDeviceObjListClass = virClassNew(virClassForObjectRWLockable(),
                                                  "virNodeDeviceObjList",
                                                  sizeof(virNodeDeviceObjList),
                                                  virNodeDeviceObjListDispose)))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(virNodeDeviceObj)


static void
virNodeDeviceObjDispose(void *opaque)
{
    virNodeDeviceObjPtr obj = opaque;

    virNodeDeviceDefFree(obj->def);
}


static virNodeDeviceObjPtr
virNodeDeviceObjNew(void)
{
    virNodeDeviceObjPtr obj;

    if (virNodeDeviceObjInitialize() < 0)
        return NULL;

    if (!(obj = virObjectLockableNew(virNodeDeviceObjClass)))
        return NULL;

    virObjectLock(obj);

    return obj;
}


void
virNodeDeviceObjEndAPI(virNodeDeviceObjPtr *obj)
{
    if (!*obj)
        return;

    virObjectUnlock(*obj);
    virObjectUnref(*obj);
    *obj = NULL;
}


virNodeDeviceDefPtr
virNodeDeviceObjGetDef(virNodeDeviceObjPtr obj)
{
    return obj->def;
}


static bool
virNodeDeviceObjHasCap(const virNodeDeviceObj *obj,
                       const char *cap)
{
    virNodeDevCapsDefPtr caps = obj->def->caps;
    const char *fc_host_cap =
        virNodeDevCapTypeToString(VIR_NODE_DEV_CAP_FC_HOST);
    const char *vports_cap =
        virNodeDevCapTypeToString(VIR_NODE_DEV_CAP_VPORTS);
    const char *mdev_types =
        virNodeDevCapTypeToString(VIR_NODE_DEV_CAP_MDEV_TYPES);

    while (caps) {
        if (STREQ(cap, virNodeDevCapTypeToString(caps->data.type))) {
            return true;
        } else {
            switch (caps->data.type) {
            case VIR_NODE_DEV_CAP_PCI_DEV:
                if ((STREQ(cap, mdev_types)) &&
                    (caps->data.pci_dev.flags & VIR_NODE_DEV_CAP_FLAG_PCI_MDEV))
                    return true;
                break;

            case VIR_NODE_DEV_CAP_SCSI_HOST:
                if ((STREQ(cap, fc_host_cap) &&
                    (caps->data.scsi_host.flags & VIR_NODE_DEV_CAP_FLAG_HBA_FC_HOST)) ||
                    (STREQ(cap, vports_cap) &&
                    (caps->data.scsi_host.flags & VIR_NODE_DEV_CAP_FLAG_HBA_VPORT_OPS)))
                    return true;
                break;

            case VIR_NODE_DEV_CAP_SYSTEM:
            case VIR_NODE_DEV_CAP_USB_DEV:
            case VIR_NODE_DEV_CAP_USB_INTERFACE:
            case VIR_NODE_DEV_CAP_NET:
            case VIR_NODE_DEV_CAP_SCSI_TARGET:
            case VIR_NODE_DEV_CAP_SCSI:
            case VIR_NODE_DEV_CAP_STORAGE:
            case VIR_NODE_DEV_CAP_FC_HOST:
            case VIR_NODE_DEV_CAP_VPORTS:
            case VIR_NODE_DEV_CAP_SCSI_GENERIC:
            case VIR_NODE_DEV_CAP_DRM:
            case VIR_NODE_DEV_CAP_MDEV_TYPES:
            case VIR_NODE_DEV_CAP_MDEV:
            case VIR_NODE_DEV_CAP_CCW_DEV:
            case VIR_NODE_DEV_CAP_LAST:
                break;
            }
        }

        caps = caps->next;
    }
    return false;
}


/* virNodeDeviceFindFCCapDef:
 * @obj: Pointer to current device
 *
 * Search the device object 'caps' array for fc_host capability.
 *
 * Returns:
 * Pointer to the caps or NULL if not found
 */
static virNodeDevCapsDefPtr
virNodeDeviceFindFCCapDef(const virNodeDeviceObj *obj)
{
    virNodeDevCapsDefPtr caps = obj->def->caps;

    while (caps) {
        if (caps->data.type == VIR_NODE_DEV_CAP_SCSI_HOST &&
            (caps->data.scsi_host.flags & VIR_NODE_DEV_CAP_FLAG_HBA_FC_HOST))
            break;

        caps = caps->next;
    }
    return caps;
}


/* virNodeDeviceFindVPORTCapDef:
 * @obj: Pointer to current device
 *
 * Search the device object 'caps' array for vport_ops capability.
 *
 * Returns:
 * Pointer to the caps or NULL if not found
 */
static virNodeDevCapsDefPtr
virNodeDeviceFindVPORTCapDef(const virNodeDeviceObj *obj)
{
    virNodeDevCapsDefPtr caps = obj->def->caps;

    while (caps) {
        if (caps->data.type == VIR_NODE_DEV_CAP_SCSI_HOST &&
            (caps->data.scsi_host.flags & VIR_NODE_DEV_CAP_FLAG_HBA_VPORT_OPS))
            break;

        caps = caps->next;
    }
    return caps;
}


static virNodeDeviceObjPtr
virNodeDeviceObjListSearch(virNodeDeviceObjListPtr devs,
                           virHashSearcher callback,
                           const void *data)
{
    virNodeDeviceObjPtr obj;

    virObjectRWLockRead(devs);
    obj = virHashSearch(devs->objs, callback, data, NULL);
    virObjectRef(obj);
    virObjectRWUnlock(devs);

    if (obj)
        virObjectLock(obj);

    return obj;
}


static int
virNodeDeviceObjListFindBySysfsPathCallback(const void *payload,
                                            const void *name ATTRIBUTE_UNUSED,
                                            const void *opaque)
{
    virNodeDeviceObjPtr obj = (virNodeDeviceObjPtr) payload;
    const char *sysfs_path = opaque;
    int want = 0;

    virObjectLock(obj);
    if (STREQ_NULLABLE(obj->def->sysfs_path, sysfs_path))
        want = 1;
    virObjectUnlock(obj);
    return want;
}


virNodeDeviceObjPtr
virNodeDeviceObjListFindBySysfsPath(virNodeDeviceObjListPtr devs,
                                    const char *sysfs_path)
{
    return virNodeDeviceObjListSearch(devs,
                                      virNodeDeviceObjListFindBySysfsPathCallback,
                                      sysfs_path);
}


static virNodeDeviceObjPtr
virNodeDeviceObjListFindByNameLocked(virNodeDeviceObjListPtr devs,
                                     const char *name)
{
    return virObjectRef(virHashLookup(devs->objs, name));
}


virNodeDeviceObjPtr
virNodeDeviceObjListFindByName(virNodeDeviceObjListPtr devs,
                               const char *name)
{
    virNodeDeviceObjPtr obj;

    virObjectRWLockRead(devs);
    obj = virNodeDeviceObjListFindByNameLocked(devs, name);
    virObjectRWUnlock(devs);
    if (obj)
        virObjectLock(obj);

    return obj;
}


struct virNodeDeviceObjListFindByWWNsData {
    const char *parent_wwnn;
    const char *parent_wwpn;
};

static int
virNodeDeviceObjListFindByWWNsCallback(const void *payload,
                                       const void *name ATTRIBUTE_UNUSED,
                                       const void *opaque)
{
    virNodeDeviceObjPtr obj = (virNodeDeviceObjPtr) payload;
    struct virNodeDeviceObjListFindByWWNsData *data =
        (struct virNodeDeviceObjListFindByWWNsData *) opaque;
    virNodeDevCapsDefPtr cap;
    int want = 0;

    virObjectLock(obj);
    if ((cap = virNodeDeviceFindFCCapDef(obj)) &&
        STREQ_NULLABLE(cap->data.scsi_host.wwnn, data->parent_wwnn) &&
        STREQ_NULLABLE(cap->data.scsi_host.wwpn, data->parent_wwpn) &&
        virNodeDeviceFindVPORTCapDef(obj))
        want = 1;
    virObjectUnlock(obj);
    return want;
}


static virNodeDeviceObjPtr
virNodeDeviceObjListFindByWWNs(virNodeDeviceObjListPtr devs,
                               const char *parent_wwnn,
                               const char *parent_wwpn)
{
    struct virNodeDeviceObjListFindByWWNsData data = {
        .parent_wwnn = parent_wwnn, .parent_wwpn = parent_wwpn };

    return virNodeDeviceObjListSearch(devs,
                                      virNodeDeviceObjListFindByWWNsCallback,
                                      &data);
}


static int
virNodeDeviceObjListFindByFabricWWNCallback(const void *payload,
                                            const void *name ATTRIBUTE_UNUSED,
                                            const void *opaque)
{
    virNodeDeviceObjPtr obj = (virNodeDeviceObjPtr) payload;
    const char *matchstr = opaque;
    virNodeDevCapsDefPtr cap;
    int want = 0;

    virObjectLock(obj);
    if ((cap = virNodeDeviceFindFCCapDef(obj)) &&
        STREQ_NULLABLE(cap->data.scsi_host.fabric_wwn, matchstr) &&
        virNodeDeviceFindVPORTCapDef(obj))
        want = 1;
    virObjectUnlock(obj);
    return want;
}


static virNodeDeviceObjPtr
virNodeDeviceObjListFindByFabricWWN(virNodeDeviceObjListPtr devs,
                                    const char *parent_fabric_wwn)
{
    return virNodeDeviceObjListSearch(devs,
                                      virNodeDeviceObjListFindByFabricWWNCallback,
                                      parent_fabric_wwn);
}


static int
virNodeDeviceObjListFindByCapCallback(const void *payload,
                                      const void *name ATTRIBUTE_UNUSED,
                                      const void *opaque)
{
    virNodeDeviceObjPtr obj = (virNodeDeviceObjPtr) payload;
    const char *matchstr = opaque;
    int want = 0;

    virObjectLock(obj);
    if (virNodeDeviceObjHasCap(obj, matchstr))
        want = 1;
    virObjectUnlock(obj);
    return want;
}


static virNodeDeviceObjPtr
virNodeDeviceObjListFindByCap(virNodeDeviceObjListPtr devs,
                              const char *cap)
{
    return virNodeDeviceObjListSearch(devs,
                                      virNodeDeviceObjListFindByCapCallback,
                                      cap);
}


struct virNodeDeviceObjListFindSCSIHostByWWNsData {
    const char *wwnn;
    const char *wwpn;
};

static int
virNodeDeviceObjListFindSCSIHostByWWNsCallback(const void *payload,
                                               const void *name ATTRIBUTE_UNUSED,
                                               const void *opaque)
{
    virNodeDeviceObjPtr obj = (virNodeDeviceObjPtr) payload;
    struct virNodeDeviceObjListFindSCSIHostByWWNsData *data =
        (struct virNodeDeviceObjListFindSCSIHostByWWNsData *) opaque;
    virNodeDevCapsDefPtr cap;
    int want = 0;

    virObjectLock(obj);
    cap = obj->def->caps;

    while (cap) {
        if (cap->data.type == VIR_NODE_DEV_CAP_SCSI_HOST) {
            virNodeDeviceGetSCSIHostCaps(&cap->data.scsi_host);
            if (cap->data.scsi_host.flags &
                VIR_NODE_DEV_CAP_FLAG_HBA_FC_HOST) {
                if (STREQ(cap->data.scsi_host.wwnn, data->wwnn) &&
                    STREQ(cap->data.scsi_host.wwpn, data->wwpn)) {
                    want = 1;
                    break;
                }
            }
        }
        cap = cap->next;
     }

    virObjectUnlock(obj);
    return want;
}


virNodeDeviceObjPtr
virNodeDeviceObjListFindSCSIHostByWWNs(virNodeDeviceObjListPtr devs,
                                       const char *wwnn,
                                       const char *wwpn)
{
    struct virNodeDeviceObjListFindSCSIHostByWWNsData data = {
        .wwnn = wwnn, .wwpn = wwpn };

    return virNodeDeviceObjListSearch(devs,
                                      virNodeDeviceObjListFindSCSIHostByWWNsCallback,
                                      &data);
}


static void
virNodeDeviceObjListDispose(void *obj)
{
    virNodeDeviceObjListPtr devs = obj;

    virHashFree(devs->objs);
}


virNodeDeviceObjListPtr
virNodeDeviceObjListNew(void)
{
    virNodeDeviceObjListPtr devs;

    if (virNodeDeviceObjInitialize() < 0)
        return NULL;

    if (!(devs = virObjectRWLockableNew(virNodeDeviceObjListClass)))
        return NULL;

    if (!(devs->objs = virHashCreate(50, virObjectFreeHashData))) {
        virObjectUnref(devs);
        return NULL;
    }

    return devs;
}


void
virNodeDeviceObjListFree(virNodeDeviceObjListPtr devs)
{
    virObjectUnref(devs);
}


virNodeDeviceObjPtr
virNodeDeviceObjListAssignDef(virNodeDeviceObjListPtr devs,
                              virNodeDeviceDefPtr def)
{
    virNodeDeviceObjPtr obj;

    virObjectRWLockWrite(devs);

    if ((obj = virNodeDeviceObjListFindByNameLocked(devs, def->name))) {
        virObjectLock(obj);
        virNodeDeviceDefFree(obj->def);
        obj->def = def;
    } else {
        if (!(obj = virNodeDeviceObjNew()))
            goto cleanup;

        if (virHashAddEntry(devs->objs, def->name, obj) < 0) {
            virNodeDeviceObjEndAPI(&obj);
            goto cleanup;
        }

        obj->def = def;
        virObjectRef(obj);
    }

 cleanup:
    virObjectRWUnlock(devs);
    return obj;
}


void
virNodeDeviceObjListRemove(virNodeDeviceObjListPtr devs,
                           virNodeDeviceObjPtr obj)
{
    virNodeDeviceDefPtr def;

    if (!obj)
        return;
    def = obj->def;

    virObjectRef(obj);
    virObjectUnlock(obj);
    virObjectRWLockWrite(devs);
    virObjectLock(obj);
    virHashRemoveEntry(devs->objs, def->name);
    virObjectUnlock(obj);
    virObjectUnref(obj);
    virObjectRWUnlock(devs);
}


/*
 * Return the NPIV dev's parent device name
 */
/* virNodeDeviceFindFCParentHost:
 * @obj: Pointer to node device object
 *
 * Search the capabilities for the device to find the FC capabilities
 * in order to set the parent_host value.
 *
 * Returns:
 *   parent_host value on success (>= 0), -1 otherwise.
 */
static int
virNodeDeviceFindFCParentHost(virNodeDeviceObjPtr obj)
{
    virNodeDevCapsDefPtr cap = virNodeDeviceFindVPORTCapDef(obj);

    if (!cap) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Parent device %s is not capable "
                         "of vport operations"),
                       obj->def->name);
        return -1;
    }

    return cap->data.scsi_host.host;
}


static int
virNodeDeviceObjListGetParentHostByParent(virNodeDeviceObjListPtr devs,
                                          const char *dev_name,
                                          const char *parent_name)
{
    virNodeDeviceObjPtr obj = NULL;
    int ret;

    if (!(obj = virNodeDeviceObjListFindByName(devs, parent_name))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not find parent device for '%s'"),
                       dev_name);
        return -1;
    }

    ret = virNodeDeviceFindFCParentHost(obj);

    virNodeDeviceObjEndAPI(&obj);

    return ret;
}


static int
virNodeDeviceObjListGetParentHostByWWNs(virNodeDeviceObjListPtr devs,
                                        const char *dev_name,
                                        const char *parent_wwnn,
                                        const char *parent_wwpn)
{
    virNodeDeviceObjPtr obj = NULL;
    int ret;

    if (!(obj = virNodeDeviceObjListFindByWWNs(devs, parent_wwnn,
                                               parent_wwpn))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not find parent device for '%s'"),
                       dev_name);
        return -1;
    }

    ret = virNodeDeviceFindFCParentHost(obj);

    virNodeDeviceObjEndAPI(&obj);

    return ret;
}


static int
virNodeDeviceObjListGetParentHostByFabricWWN(virNodeDeviceObjListPtr devs,
                                             const char *dev_name,
                                             const char *parent_fabric_wwn)
{
    virNodeDeviceObjPtr obj = NULL;
    int ret;

    if (!(obj = virNodeDeviceObjListFindByFabricWWN(devs, parent_fabric_wwn))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not find parent device for '%s'"),
                       dev_name);
        return -1;
    }

    ret = virNodeDeviceFindFCParentHost(obj);

    virNodeDeviceObjEndAPI(&obj);

    return ret;
}


static int
virNodeDeviceObjListFindVportParentHost(virNodeDeviceObjListPtr devs)
{
    virNodeDeviceObjPtr obj = NULL;
    const char *cap = virNodeDevCapTypeToString(VIR_NODE_DEV_CAP_VPORTS);
    int ret;

    if (!(obj = virNodeDeviceObjListFindByCap(devs, cap))) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Could not find any vport capable device"));
        return -1;
    }

    ret = virNodeDeviceFindFCParentHost(obj);

    virNodeDeviceObjEndAPI(&obj);

    return ret;
}


int
virNodeDeviceObjListGetParentHost(virNodeDeviceObjListPtr devs,
                                  virNodeDeviceDefPtr def)
{
    int parent_host = -1;

    if (def->parent) {
        parent_host = virNodeDeviceObjListGetParentHostByParent(devs, def->name,
                                                                def->parent);
    } else if (def->parent_wwnn && def->parent_wwpn) {
        parent_host = virNodeDeviceObjListGetParentHostByWWNs(devs, def->name,
                                                              def->parent_wwnn,
                                                              def->parent_wwpn);
    } else if (def->parent_fabric_wwn) {
        parent_host =
            virNodeDeviceObjListGetParentHostByFabricWWN(devs, def->name,
                                                         def->parent_fabric_wwn);
    } else {
        /* Try to find a vport capable scsi_host when no parent supplied */
        parent_host = virNodeDeviceObjListFindVportParentHost(devs);
    }

    return parent_host;
}


static bool
virNodeDeviceCapMatch(virNodeDeviceObjPtr obj,
                      int type)
{
    virNodeDevCapsDefPtr cap = NULL;

    for (cap = obj->def->caps; cap; cap = cap->next) {
        if (type == cap->data.type)
            return true;

        switch (cap->data.type) {
        case VIR_NODE_DEV_CAP_PCI_DEV:
            if (type == VIR_NODE_DEV_CAP_MDEV_TYPES &&
                (cap->data.pci_dev.flags & VIR_NODE_DEV_CAP_FLAG_PCI_MDEV))
                return true;
            break;

        case VIR_NODE_DEV_CAP_SCSI_HOST:
            if (type == VIR_NODE_DEV_CAP_FC_HOST &&
                (cap->data.scsi_host.flags & VIR_NODE_DEV_CAP_FLAG_HBA_FC_HOST))
                return true;

            if (type == VIR_NODE_DEV_CAP_VPORTS &&
                (cap->data.scsi_host.flags & VIR_NODE_DEV_CAP_FLAG_HBA_VPORT_OPS))
                return true;
            break;

        case VIR_NODE_DEV_CAP_SYSTEM:
        case VIR_NODE_DEV_CAP_USB_DEV:
        case VIR_NODE_DEV_CAP_USB_INTERFACE:
        case VIR_NODE_DEV_CAP_NET:
        case VIR_NODE_DEV_CAP_SCSI_TARGET:
        case VIR_NODE_DEV_CAP_SCSI:
        case VIR_NODE_DEV_CAP_STORAGE:
        case VIR_NODE_DEV_CAP_FC_HOST:
        case VIR_NODE_DEV_CAP_VPORTS:
        case VIR_NODE_DEV_CAP_SCSI_GENERIC:
        case VIR_NODE_DEV_CAP_DRM:
        case VIR_NODE_DEV_CAP_MDEV_TYPES:
        case VIR_NODE_DEV_CAP_MDEV:
        case VIR_NODE_DEV_CAP_CCW_DEV:
        case VIR_NODE_DEV_CAP_LAST:
            break;
        }
    }

    return false;
}


typedef bool (*virNodeDeviceObjListHasCap)(const virNodeDeviceObj *obj,
                                           const char *cap);
typedef bool (*virNodeDeviceObjListMatch)(virNodeDeviceObjPtr obj,
                                          unsigned int flags);
struct _virNodeDeviceObjForEachData {
    virConnectPtr conn;
    virNodeDeviceObjListFilter filter;
    virNodeDeviceObjListHasCap hascap;
    const char *cap;
    virNodeDeviceObjListMatch match;
    unsigned int flags;
    bool error;
    int nElems;
    int maxElems;
    char **const elems;
    virNodeDevicePtr *devices;
};

static int
virNodeDeviceObjListForEachCb(void *payload,
                              const void *name ATTRIBUTE_UNUSED,
                              void *opaque)
{
    virNodeDeviceObjPtr obj = payload;
    struct _virNodeDeviceObjForEachData *data = opaque;
    virNodeDevicePtr device = NULL;

    if (data->error)
        return 0;

    if (data->maxElems >= 0 && data->nElems == data->maxElems)
        return 0;

    virObjectLock(obj);

    if (data->filter && !data->filter(data->conn, obj->def))
        goto cleanup;

    if (data->hascap && data->hascap(obj, data->cap))
        goto cleanup;

    if (data->match && !data->match(obj, data->flags))
        goto cleanup;

    if (data->elems) {
        if (VIR_STRDUP(data->elems[data->nElems], obj->def->name) < 0) {
            data->error = true;
            goto cleanup;
        }
    } else if (data->devices) {
        if (!(device = virGetNodeDevice(data->conn, obj->def->name)) ||
            VIR_STRDUP(device->parent, obj->def->parent) < 0) {
            virObjectUnref(device);
            data->error = true;
            goto cleanup;
        }
        data->devices[data->nElems] = device;
    }

    data->nElems++;

 cleanup:
    virObjectUnlock(obj);
    return 0;
}


int
virNodeDeviceObjListNumOfDevices(virNodeDeviceObjListPtr devs,
                                 virConnectPtr conn,
                                 const char *cap,
                                 virNodeDeviceObjListFilter filter)
{
    struct _virNodeDeviceObjForEachData data = { .conn = conn,
        .filter = filter, .hascap = virNodeDeviceObjHasCap, .cap = cap,
        .match = NULL, .flags = 0, .error = false, .nElems = 0, .maxElems = -1,
        .elems = NULL, .devices = NULL };

    virObjectRWLockRead(devs);
    virHashForEach(devs->objs, virNodeDeviceObjListForEachCb, &data);
    virObjectRWUnlock(devs);

    return data.nElems;
}


int
virNodeDeviceObjListGetNames(virNodeDeviceObjListPtr devs,
                             virConnectPtr conn,
                             virNodeDeviceObjListFilter filter,
                             const char *cap,
                             char **const names,
                             int maxnames)
{
    struct  _virNodeDeviceObjForEachData data = { .conn = conn,
        .filter = filter, .hascap = virNodeDeviceObjHasCap, .cap = cap,
        .match = NULL, .flags = 0, .error = false, .nElems = 0,
        .maxElems = maxnames, .elems = names, .devices = NULL };

    virObjectRWLockRead(devs);
    virHashForEach(devs->objs, virNodeDeviceObjListForEachCb, &data);
    virObjectRWUnlock(devs);

    if (data.error)
        goto error;

    return data.nElems;

 error:
    while (--data.nElems)
        VIR_FREE(data.elems[data.nElems]);
    return -1;
}


#define MATCH(FLAG) ((flags & (VIR_CONNECT_LIST_NODE_DEVICES_CAP_ ## FLAG)) && \
                     virNodeDeviceCapMatch(obj, VIR_NODE_DEV_CAP_ ## FLAG))
static bool
virNodeDeviceMatch(virNodeDeviceObjPtr obj,
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
              MATCH(SCSI_GENERIC)  ||
              MATCH(DRM)           ||
              MATCH(MDEV_TYPES)    ||
              MATCH(MDEV)          ||
              MATCH(CCW_DEV)))
            return false;
    }

    return true;
}
#undef MATCH


int
virNodeDeviceObjListExport(virConnectPtr conn,
                           virNodeDeviceObjListPtr devs,
                           virNodeDevicePtr **devices,
                           virNodeDeviceObjListFilter filter,
                           unsigned int flags)
{
    struct  _virNodeDeviceObjForEachData data = { .conn = conn,
        .filter = filter, .hascap = NULL, .cap = NULL,
        .match = virNodeDeviceMatch, .flags = flags, .error = false,
         .nElems = 0, .maxElems = -1, .elems = NULL, .devices = NULL };

    virObjectRWLockRead(devs);
    if (devices &&
        VIR_ALLOC_N(data.devices, virHashSize(devs->objs) + 1) < 0) {
        virObjectRWUnlock(devs);
        return -1;
    }
    if (data.devices)
        data.maxElems = virHashSize(devs->objs) + 1;

    virHashForEach(devs->objs, virNodeDeviceObjListForEachCb, &data);
    virObjectRWUnlock(devs);

    if (data.error)
        goto cleanup;

    if (data.devices) {
        ignore_value(VIR_REALLOC_N(data.devices, data.nElems + 1));
        *devices = data.devices;
     }

    return data.nElems;

 cleanup:
    virObjectListFree(data.devices);
    return -1;
}
