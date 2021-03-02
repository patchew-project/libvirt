/*
 * node_device_driver.c: node device enumeration
 *
 * Copyright (C) 2010-2015 Red Hat, Inc.
 * Copyright (C) 2008 Virtual Iron Software, Inc.
 * Copyright (C) 2008 David F. Lively
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

#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include "virerror.h"
#include "datatypes.h"
#include "domain_addr.h"
#include "viralloc.h"
#include "virfile.h"
#include "virjson.h"
#include "virstring.h"
#include "node_device_conf.h"
#include "node_device_event.h"
#include "node_device_driver.h"
#include "node_device_util.h"
#include "virvhba.h"
#include "viraccessapicheck.h"
#include "virnetdev.h"
#include "virutil.h"
#include "vircommand.h"

#define VIR_FROM_THIS VIR_FROM_NODEDEV

virNodeDeviceDriverStatePtr driver;

virDrvOpenStatus
nodeConnectOpen(virConnectPtr conn,
                virConnectAuthPtr auth G_GNUC_UNUSED,
                virConfPtr conf G_GNUC_UNUSED,
                unsigned int flags)
{
    virCheckFlags(VIR_CONNECT_RO, VIR_DRV_OPEN_ERROR);

    if (driver == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("nodedev state driver is not active"));
        return VIR_DRV_OPEN_ERROR;
    }

    if (!virConnectValidateURIPath(conn->uri->path,
                                   "nodedev",
                                   driver->privileged))
        return VIR_DRV_OPEN_ERROR;

    if (virConnectOpenEnsureACL(conn) < 0)
        return VIR_DRV_OPEN_ERROR;

    return VIR_DRV_OPEN_SUCCESS;
}

int nodeConnectClose(virConnectPtr conn G_GNUC_UNUSED)
{
    return 0;
}


int nodeConnectIsSecure(virConnectPtr conn G_GNUC_UNUSED)
{
    /* Trivially secure, since always inside the daemon */
    return 1;
}


int nodeConnectIsEncrypted(virConnectPtr conn G_GNUC_UNUSED)
{
    /* Not encrypted, but remote driver takes care of that */
    return 0;
}


int nodeConnectIsAlive(virConnectPtr conn G_GNUC_UNUSED)
{
    return 1;
}

#if defined (__linux__) && defined(WITH_UDEV)
/* NB: It was previously believed that changes in driver name were
 * relayed to libvirt as "change" events by udev, and the udev event
 * notification is setup to recognize such events and effectively
 * recreate the device entry in the cache. However, neither the kernel
 * nor udev sends such an event, so it is necessary to manually update
 * the driver name for a device each time its entry is used.
 */
static int
nodeDeviceUpdateDriverName(virNodeDeviceDefPtr def)
{
    g_autofree char *driver_link = NULL;
    g_autofree char *devpath = NULL;
    char *p;

    VIR_FREE(def->driver);

    driver_link = g_strdup_printf("%s/driver", def->sysfs_path);

    /* Some devices don't have an explicit driver, so just return
       without a name */
    if (access(driver_link, R_OK) < 0)
        return 0;

    if (virFileResolveLink(driver_link, &devpath) < 0) {
        virReportSystemError(errno,
                             _("cannot resolve driver link %s"), driver_link);
        return -1;
    }

    p = strrchr(devpath, '/');
    if (p)
        def->driver = g_strdup(p + 1);

    return 0;
}
#else
/* XXX: Implement me for non-linux */
static int
nodeDeviceUpdateDriverName(virNodeDeviceDefPtr def G_GNUC_UNUSED)
{
    return 0;
}
#endif


void
nodeDeviceLock(void)
{
    virMutexLock(&driver->lock);
}


void
nodeDeviceUnlock(void)
{
    virMutexUnlock(&driver->lock);
}


static int
nodeDeviceWaitInit(void)
{
    nodeDeviceLock();
    while (!driver->initialized) {
        if (virCondWait(&driver->initCond, &driver->lock) < 0) {
            virReportSystemError(errno, "%s",
                                 _("failed to wait on condition"));
            nodeDeviceUnlock();
            return -1;
        }
    }
    nodeDeviceUnlock();
    return 0;
}

int
nodeNumOfDevices(virConnectPtr conn,
                 const char *cap,
                 unsigned int flags)
{
    if (virNodeNumOfDevicesEnsureACL(conn) < 0)
        return -1;

    virCheckFlags(0, -1);

    if (nodeDeviceWaitInit() < 0)
        return -1;

    return virNodeDeviceObjListNumOfDevices(driver->devs, conn, cap,
                                            virNodeNumOfDevicesCheckACL);
}


int
nodeListDevices(virConnectPtr conn,
                const char *cap,
                char **const names,
                int maxnames,
                unsigned int flags)
{
    if (virNodeListDevicesEnsureACL(conn) < 0)
        return -1;

    virCheckFlags(0, -1);

    if (nodeDeviceWaitInit() < 0)
        return -1;

    return virNodeDeviceObjListGetNames(driver->devs, conn,
                                        virNodeListDevicesCheckACL,
                                        cap, names, maxnames);
}


int
nodeConnectListAllNodeDevices(virConnectPtr conn,
                              virNodeDevicePtr **devices,
                              unsigned int flags)
{
    virCheckFlags(VIR_CONNECT_LIST_NODE_DEVICES_FILTERS_ALL, -1);

    if (virConnectListAllNodeDevicesEnsureACL(conn) < 0)
        return -1;

    if (nodeDeviceWaitInit() < 0)
        return -1;

    return virNodeDeviceObjListExport(conn, driver->devs, devices,
                                      virConnectListAllNodeDevicesCheckACL,
                                      flags);
}


static virNodeDeviceObjPtr
nodeDeviceObjFindByName(const char *name)
{
    virNodeDeviceObjPtr obj;

    if (!(obj = virNodeDeviceObjListFindByName(driver->devs, name))) {
        virReportError(VIR_ERR_NO_NODE_DEVICE,
                       _("no node device with matching name '%s'"),
                       name);
    }

    return obj;
}


virNodeDevicePtr
nodeDeviceLookupByName(virConnectPtr conn,
                       const char *name)
{
    virNodeDeviceObjPtr obj;
    virNodeDeviceDefPtr def;
    virNodeDevicePtr device = NULL;

    if (nodeDeviceWaitInit() < 0)
        return NULL;

    if (!(obj = nodeDeviceObjFindByName(name)))
        return NULL;
    def = virNodeDeviceObjGetDef(obj);

    if (virNodeDeviceLookupByNameEnsureACL(conn, def) < 0)
        goto cleanup;

    if ((device = virGetNodeDevice(conn, name)))
        device->parentName = g_strdup(def->parent);

 cleanup:
    virNodeDeviceObjEndAPI(&obj);
    return device;
}


virNodeDevicePtr
nodeDeviceLookupSCSIHostByWWN(virConnectPtr conn,
                              const char *wwnn,
                              const char *wwpn,
                              unsigned int flags)
{
    virNodeDeviceObjPtr obj = NULL;
    virNodeDeviceDefPtr def;
    virNodeDevicePtr device = NULL;

    virCheckFlags(0, NULL);

    if (nodeDeviceWaitInit() < 0)
        return NULL;

    if (!(obj = virNodeDeviceObjListFindSCSIHostByWWNs(driver->devs,
                                                       wwnn, wwpn)))
        return NULL;

    def = virNodeDeviceObjGetDef(obj);

    if (virNodeDeviceLookupSCSIHostByWWNEnsureACL(conn, def) < 0)
        goto cleanup;

    if ((device = virGetNodeDevice(conn, def->name)))
        device->parentName = g_strdup(def->parent);

 cleanup:
    virNodeDeviceObjEndAPI(&obj);
    return device;
}

static virNodeDevicePtr
nodeDeviceLookupMediatedDeviceByUUID(virConnectPtr conn,
                                     const char *uuid,
                                     unsigned int flags)
{
    virNodeDeviceObjPtr obj = NULL;
    virNodeDeviceDefPtr def;
    virNodeDevicePtr device = NULL;

    virCheckFlags(0, NULL);

    if (!(obj = virNodeDeviceObjListFindMediatedDeviceByUUID(driver->devs,
                                                             uuid)))
        return NULL;

    def = virNodeDeviceObjGetDef(obj);

    if ((device = virGetNodeDevice(conn, def->name)))
        device->parentName = g_strdup(def->parent);

    virNodeDeviceObjEndAPI(&obj);
    return device;
}


char *
nodeDeviceGetXMLDesc(virNodeDevicePtr device,
                     unsigned int flags)
{
    virNodeDeviceObjPtr obj;
    virNodeDeviceDefPtr def;
    char *ret = NULL;

    virCheckFlags(0, NULL);

    if (!(obj = nodeDeviceObjFindByName(device->name)))
        return NULL;
    def = virNodeDeviceObjGetDef(obj);

    if (virNodeDeviceGetXMLDescEnsureACL(device->conn, def) < 0)
        goto cleanup;

    if (nodeDeviceUpdateDriverName(def) < 0)
        goto cleanup;

    if (virNodeDeviceUpdateCaps(def) < 0)
        goto cleanup;

    ret = virNodeDeviceDefFormat(def);

 cleanup:
    virNodeDeviceObjEndAPI(&obj);
    return ret;
}


char *
nodeDeviceGetParent(virNodeDevicePtr device)
{
    virNodeDeviceObjPtr obj;
    virNodeDeviceDefPtr def;
    char *ret = NULL;

    if (!(obj = nodeDeviceObjFindByName(device->name)))
        return NULL;
    def = virNodeDeviceObjGetDef(obj);

    if (virNodeDeviceGetParentEnsureACL(device->conn, def) < 0)
        goto cleanup;

    if (def->parent) {
        ret = g_strdup(def->parent);
    } else {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("no parent for this device"));
    }

 cleanup:
    virNodeDeviceObjEndAPI(&obj);
    return ret;
}


int
nodeDeviceNumOfCaps(virNodeDevicePtr device)
{
    virNodeDeviceObjPtr obj;
    virNodeDeviceDefPtr def;
    int ret = -1;

    if (!(obj = nodeDeviceObjFindByName(device->name)))
        return -1;
    def = virNodeDeviceObjGetDef(obj);

    if (virNodeDeviceNumOfCapsEnsureACL(device->conn, def) < 0)
        goto cleanup;

    ret = virNodeDeviceCapsListExport(def, NULL);

 cleanup:
    virNodeDeviceObjEndAPI(&obj);
    return ret;
}



int
nodeDeviceListCaps(virNodeDevicePtr device,
                   char **const names,
                   int maxnames)
{
    virNodeDeviceObjPtr obj;
    virNodeDeviceDefPtr def;
    virNodeDevCapType *list = NULL;
    int ncaps = 0;
    int ret = -1;
    size_t i = 0;

    if (!(obj = nodeDeviceObjFindByName(device->name)))
        return -1;
    def = virNodeDeviceObjGetDef(obj);

    if (virNodeDeviceListCapsEnsureACL(device->conn, def) < 0)
        goto cleanup;

    if ((ncaps = virNodeDeviceCapsListExport(def, &list)) < 0)
        goto cleanup;

    if (ncaps > maxnames)
        ncaps = maxnames;

    for (i = 0; i < ncaps; i++)
        names[i] = g_strdup(virNodeDevCapTypeToString(list[i]));

    ret = ncaps;

 cleanup:
    virNodeDeviceObjEndAPI(&obj);
    if (ret < 0) {
        size_t j;
        for (j = 0; j < i; j++)
            VIR_FREE(names[j]);
    }

    VIR_FREE(list);
    return ret;
}


static int
nodeDeviceGetTime(time_t *t)
{
    int ret = 0;

    *t = time(NULL);
    if (*t == (time_t)-1) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("Could not get current time"));

        *t = 0;
        ret = -1;
    }

    return ret;
}


typedef virNodeDevicePtr (*nodeDeviceFindNewDeviceFunc)(virConnectPtr conn,
                                                        const void* opaque);


/* When large numbers of devices are present on the host, it's
 * possible for udev not to realize that it has work to do before we
 * get here.  We thus keep trying to find the new device we just
 * created for up to LINUX_NEW_DEVICE_WAIT_TIME.  Note that udev's
 * default settle time is 180 seconds, so once udev realizes that it
 * has work to do, it might take that long for the udev wait to
 * return.  Thus the total maximum time for this function to return is
 * the udev settle time plus LINUX_NEW_DEVICE_WAIT_TIME.
 *
 * This whole area is a race, but if we retry the udev wait for
 * LINUX_NEW_DEVICE_WAIT_TIME seconds and there's still no device,
 * it's probably safe to assume it's not going to appear.
 */
static virNodeDevicePtr
nodeDeviceFindNewDevice(virConnectPtr conn,
                        nodeDeviceFindNewDeviceFunc func,
                        const void *opaque)
{
    virNodeDevicePtr device = NULL;
    time_t start = 0, now = 0;

    nodeDeviceGetTime(&start);

    while ((now - start) < LINUX_NEW_DEVICE_WAIT_TIME) {

        virWaitForDevices();

        device = func(conn, opaque);

        if (device != NULL)
            break;

        sleep(5);
        if (nodeDeviceGetTime(&now) == -1)
            break;
    }

    return device;
}


static virNodeDevicePtr
nodeDeviceFindNewMediatedDeviceFunc(virConnectPtr conn,
                                    const void *opaque)
{
    const char *uuid = opaque;

    return nodeDeviceLookupMediatedDeviceByUUID(conn, uuid, 0);
}


static virNodeDevicePtr
nodeDeviceFindNewMediatedDevice(virConnectPtr conn,
                                const char *mdev_uuid)
{
    return nodeDeviceFindNewDevice(conn,
                                   nodeDeviceFindNewMediatedDeviceFunc,
                                   mdev_uuid);
}


typedef struct _NewSCSIHostFuncData NewSCSIHostFuncData;
struct _NewSCSIHostFuncData
{
    const char *wwnn;
    const char *wwpn;
};


static virNodeDevicePtr
nodeDeviceFindNewSCSIHostFunc(virConnectPtr conn,
                              const void *opaque)
{
    const NewSCSIHostFuncData *data = opaque;

    return nodeDeviceLookupSCSIHostByWWN(conn, data->wwnn, data->wwpn, 0);
}


static virNodeDevicePtr
nodeDeviceFindNewSCSIHost(virConnectPtr conn,
                          const char *wwnn,
                          const char *wwpn)
{
    NewSCSIHostFuncData data = { .wwnn = wwnn, .wwpn = wwpn};

    return nodeDeviceFindNewDevice(conn, nodeDeviceFindNewSCSIHostFunc, &data);
}


static bool
nodeDeviceHasCapability(virNodeDeviceDefPtr def, virNodeDevCapType type)
{
    virNodeDevCapsDefPtr cap = def->caps;

    while (cap != NULL) {
        if (cap->data.type == type)
            return true;
        cap = cap->next;
    }

    return false;
}


/* format a json string that provides configuration information about this mdev
 * to the mdevctl utility */
static int
nodeDeviceDefToMdevctlConfig(virNodeDeviceDefPtr def, char **buf)
{
    size_t i;
    virNodeDevCapMdevPtr mdev = &def->caps->data.mdev;
    g_autoptr(virJSONValue) json = virJSONValueNewObject();

    if (virJSONValueObjectAppendString(json, "mdev_type", mdev->type) < 0)
        return -1;

    if (virJSONValueObjectAppendString(json, "start", "manual") < 0)
        return -1;

    if (mdev->attributes) {
        g_autoptr(virJSONValue) attributes = virJSONValueNewArray();

        for (i = 0; i < mdev->nattributes; i++) {
            virMediatedDeviceAttrPtr attr = mdev->attributes[i];
            g_autoptr(virJSONValue) jsonattr = virJSONValueNewObject();

            if (virJSONValueObjectAppendString(jsonattr, attr->name, attr->value) < 0)
                return -1;

            if (virJSONValueArrayAppend(attributes, &jsonattr) < 0)
                return -1;
        }

        if (virJSONValueObjectAppend(json, "attrs", &attributes) < 0)
            return -1;
    }

    *buf = virJSONValueToString(json, false);
    if (!*buf)
        return -1;

    return 0;
}


static char *
nodeDeviceFindAddressByName(const char *name)
{
    virNodeDeviceDefPtr def = NULL;
    virNodeDevCapsDefPtr caps = NULL;
    char *addr = NULL;
    virNodeDeviceObjPtr dev = virNodeDeviceObjListFindByName(driver->devs, name);

    if (!dev) {
        virReportError(VIR_ERR_NO_NODE_DEVICE,
                       _("could not find device '%s'"), name);
        return NULL;
    }

    def = virNodeDeviceObjGetDef(dev);
    for (caps = def->caps; caps != NULL; caps = caps->next) {
        switch (caps->data.type) {
        case VIR_NODE_DEV_CAP_PCI_DEV: {
            virPCIDeviceAddress pci_addr = {
                .domain = caps->data.pci_dev.domain,
                .bus = caps->data.pci_dev.bus,
                .slot = caps->data.pci_dev.slot,
                .function = caps->data.pci_dev.function
            };

            addr = virPCIDeviceAddressAsString(&pci_addr);
            break;
            }

        case VIR_NODE_DEV_CAP_CSS_DEV: {
            virDomainDeviceCCWAddress ccw_addr = {
                .cssid = caps->data.ccw_dev.cssid,
                .ssid = caps->data.ccw_dev.ssid,
                .devno = caps->data.ccw_dev.devno
            };

            addr = virDomainCCWAddressAsString(&ccw_addr);
            break;
            }

        case VIR_NODE_DEV_CAP_AP_MATRIX:
            addr = g_strdup(caps->data.ap_matrix.addr);
            break;

        case VIR_NODE_DEV_CAP_SYSTEM:
        case VIR_NODE_DEV_CAP_USB_DEV:
        case VIR_NODE_DEV_CAP_USB_INTERFACE:
        case VIR_NODE_DEV_CAP_NET:
        case VIR_NODE_DEV_CAP_SCSI_HOST:
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
        case VIR_NODE_DEV_CAP_VDPA:
        case VIR_NODE_DEV_CAP_AP_CARD:
        case VIR_NODE_DEV_CAP_AP_QUEUE:
        case VIR_NODE_DEV_CAP_LAST:
            break;
        }

        if (addr)
            break;
    }

    virNodeDeviceObjEndAPI(&dev);

    return addr;
}


virCommandPtr
nodeDeviceGetMdevctlStartCommand(virNodeDeviceDefPtr def,
                                 char **uuid_out,
                                 char **errmsg)
{
    virCommandPtr cmd;
    g_autofree char *json = NULL;
    g_autofree char *parent_addr = nodeDeviceFindAddressByName(def->parent);

    if (!parent_addr) {
        virReportError(VIR_ERR_NO_NODE_DEVICE,
                       _("unable to find parent device '%s'"), def->parent);
        return NULL;
    }

    if (nodeDeviceDefToMdevctlConfig(def, &json) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("couldn't convert node device def to mdevctl JSON"));
        return NULL;
    }

    cmd = virCommandNewArgList(MDEVCTL, "start",
                               "-p", parent_addr,
                               "--jsonfile", "/dev/stdin",
                               NULL);

    virCommandSetInputBuffer(cmd, json);
    virCommandSetOutputBuffer(cmd, uuid_out);
    virCommandSetErrorBuffer(cmd, errmsg);

    return cmd;
}

static int
virMdevctlStart(virNodeDeviceDefPtr def, char **uuid, char **errmsg)
{
    int status;
    g_autoptr(virCommand) cmd = nodeDeviceGetMdevctlStartCommand(def, uuid,
                                                                 errmsg);
    if (!cmd)
        return -1;

    /* an auto-generated uuid is returned via stdout if no uuid is specified in
     * the mdevctl args */
    if (virCommandRun(cmd, &status) < 0 || status != 0)
        return -1;

    /* remove newline */
    *uuid = g_strstrip(*uuid);

    return 0;
}


static virNodeDevicePtr
nodeDeviceCreateXMLMdev(virConnectPtr conn,
                        virNodeDeviceDefPtr def)
{
    g_autofree char *uuid = NULL;
    g_autofree char *errmsg = NULL;

    if (!def->parent) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("cannot create a mediated device without a parent"));
        return NULL;
    }

    if (virMdevctlStart(def, &uuid, &errmsg) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unable to start mediated device '%s': %s"), def->name,
                       errmsg && errmsg[0] ? errmsg : "Unknown Error");
        return NULL;
    }

    return nodeDeviceFindNewMediatedDevice(conn, uuid);
}


virNodeDevicePtr
nodeDeviceCreateXML(virConnectPtr conn,
                    const char *xmlDesc,
                    unsigned int flags)
{
    g_autoptr(virNodeDeviceDef) def = NULL;
    g_autofree char *wwnn = NULL;
    g_autofree char *wwpn = NULL;
    virNodeDevicePtr device = NULL;
    const char *virt_type = NULL;

    virCheckFlags(0, NULL);

    if (nodeDeviceWaitInit() < 0)
        return NULL;

    virt_type  = virConnectGetType(conn);

    if (!(def = virNodeDeviceDefParseString(xmlDesc, CREATE_DEVICE, virt_type)))
        return NULL;

    if (virNodeDeviceCreateXMLEnsureACL(conn, def) < 0)
        return NULL;

    if (nodeDeviceHasCapability(def, VIR_NODE_DEV_CAP_SCSI_HOST)) {
        int parent_host;

        if (virNodeDeviceGetWWNs(def, &wwnn, &wwpn) == -1)
            return NULL;

        if ((parent_host = virNodeDeviceObjListGetParentHost(driver->devs, def)) < 0)
            return NULL;

        if (virVHBAManageVport(parent_host, wwpn, wwnn, VPORT_CREATE) < 0)
            return NULL;

        device = nodeDeviceFindNewSCSIHost(conn, wwnn, wwpn);
        /* We don't check the return value, because one way or another,
         * we're returning what we get... */

        if (device == NULL)
            virReportError(VIR_ERR_NO_NODE_DEVICE,
                           _("no node device for '%s' with matching "
                             "wwnn '%s' and wwpn '%s'"),
                           def->name, wwnn, wwpn);
    } else if (nodeDeviceHasCapability(def, VIR_NODE_DEV_CAP_MDEV)) {
        device = nodeDeviceCreateXMLMdev(conn, def);
    } else {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Unsupported device type"));
    }

    return device;
}


virCommandPtr
nodeDeviceGetMdevctlStopCommand(const char *uuid, char **errmsg)
{
    virCommandPtr cmd = virCommandNewArgList(MDEVCTL,
                                             "stop",
                                             "-u",
                                             uuid,
                                             NULL);
    virCommandSetErrorBuffer(cmd, errmsg);
    return cmd;

}

static int
virMdevctlStop(virNodeDeviceDefPtr def, char **errmsg)
{
    int status;
    g_autoptr(virCommand) cmd = NULL;

    cmd = nodeDeviceGetMdevctlStopCommand(def->caps->data.mdev.uuid, errmsg);

    if (virCommandRun(cmd, &status) < 0 || status != 0)
        return -1;

    return 0;
}


virCommandPtr
nodeDeviceGetMdevctlListCommand(bool defined,
                                char **output)
{
    virCommandPtr cmd = virCommandNewArgList(MDEVCTL,
                                             "list",
                                             "--dumpjson",
                                             NULL);

    if (defined)
        virCommandAddArg(cmd, "--defined");

    virCommandSetOutputBuffer(cmd, output);

    return cmd;
}


static void mdevGenerateDeviceName(virNodeDeviceDefPtr dev)
{
    nodeDeviceGenerateName(dev, "mdev", dev->caps->data.mdev.uuid, NULL);
}


static virNodeDeviceDefPtr
nodeDeviceParseMdevctlChildDevice(const char *parent,
                                  virJSONValuePtr json)
{
    virNodeDevCapMdevPtr mdev;
    const char *uuid;
    virJSONValuePtr props;
    virJSONValuePtr attrs;
    g_autoptr(virNodeDeviceDef) child = g_new0(virNodeDeviceDef, 1);

    /* the child object should have a single key equal to its uuid.
     * The value is an object describing the properties of the mdev */
    if (virJSONValueObjectKeysNumber(json) != 1)
        return NULL;

    uuid = virJSONValueObjectGetKey(json, 0);
    props = virJSONValueObjectGetValue(json, 0);

    child->parent = g_strdup(parent);
    child->caps = g_new0(virNodeDevCapsDef, 1);
    child->caps->data.type = VIR_NODE_DEV_CAP_MDEV;

    mdev = &child->caps->data.mdev;
    mdev->uuid = g_strdup(uuid);
    mdev->type =
        g_strdup(virJSONValueObjectGetString(props, "mdev_type"));

    attrs = virJSONValueObjectGet(props, "attrs");

    if (attrs && virJSONValueIsArray(attrs)) {
        size_t i;
        int nattrs = virJSONValueArraySize(attrs);

        mdev->attributes = g_new0(virMediatedDeviceAttrPtr, nattrs);
        mdev->nattributes = nattrs;

        for (i = 0; i < nattrs; i++) {
            virJSONValuePtr attr = virJSONValueArrayGet(attrs, i);
            virMediatedDeviceAttrPtr attribute;
            virJSONValuePtr value;

            if (!virJSONValueIsObject(attr) ||
                virJSONValueObjectKeysNumber(attr) != 1)
                return NULL;

            attribute = g_new0(virMediatedDeviceAttr, 1);
            attribute->name = g_strdup(virJSONValueObjectGetKey(attr, 0));
            value = virJSONValueObjectGetValue(attr, 0);
            attribute->value = g_strdup(virJSONValueGetString(value));
            mdev->attributes[i] = attribute;
        }
    }
    mdevGenerateDeviceName(child);

    return g_steal_pointer(&child);
}


int
nodeDeviceParseMdevctlJSON(const char *jsonstring,
                           virNodeDeviceDefPtr **devs)
{
    int n;
    g_autoptr(virJSONValue) json_devicelist = NULL;
    virNodeDeviceDefPtr *outdevs = NULL;
    size_t noutdevs = 0;
    size_t i;
    size_t j;

    json_devicelist = virJSONValueFromString(jsonstring);

    if (!json_devicelist || !virJSONValueIsArray(json_devicelist)) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("mdevctl JSON response contains no devices"));
        goto error;
    }

    n = virJSONValueArraySize(json_devicelist);

    for (i = 0; i < n; i++) {
        virJSONValuePtr obj = virJSONValueArrayGet(json_devicelist, i);
        const char *parent;
        virJSONValuePtr child_array;
        int nchildren;

        if (!virJSONValueIsObject(obj)) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Parent device is not an object"));
            goto error;
        }

        /* mdevctl returns an array of objects.  Each object is a parent device
         * object containing a single key-value pair which maps from the name
         * of the parent device to an array of child devices */
        if (virJSONValueObjectKeysNumber(obj) != 1) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Unexpected format for parent device object"));
            goto error;
        }

        parent = virJSONValueObjectGetKey(obj, 0);
        child_array = virJSONValueObjectGetValue(obj, 0);

        if (!virJSONValueIsArray(child_array)) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Parent device's JSON object data is not an array"));
            goto error;
        }

        nchildren = virJSONValueArraySize(child_array);

        for (j = 0; j < nchildren; j++) {
            g_autoptr(virNodeDeviceDef) child = NULL;
            virJSONValuePtr child_obj = virJSONValueArrayGet(child_array, j);

            if (!(child = nodeDeviceParseMdevctlChildDevice(parent, child_obj))) {
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("Unable to parse child device"));
                goto error;
            }

            if (VIR_APPEND_ELEMENT(outdevs, noutdevs, child) < 0)
                goto error;
        }
    }

    *devs = outdevs;
    return noutdevs;

 error:
    for (i = 0; i < noutdevs; i++)
        virNodeDeviceDefFree(outdevs[i]);
    VIR_FREE(outdevs);
    return -1;
}


int
nodeDeviceDestroy(virNodeDevicePtr device)
{
    int ret = -1;
    virNodeDeviceObjPtr obj = NULL;
    virNodeDeviceDefPtr def;
    g_autofree char *parent = NULL;
    g_autofree char *wwnn = NULL;
    g_autofree char *wwpn = NULL;
    unsigned int parent_host;

    if (nodeDeviceWaitInit() < 0)
        return -1;

    if (!(obj = nodeDeviceObjFindByName(device->name)))
        return -1;
    def = virNodeDeviceObjGetDef(obj);

    if (virNodeDeviceDestroyEnsureACL(device->conn, def) < 0)
        goto cleanup;

    if (nodeDeviceHasCapability(def, VIR_NODE_DEV_CAP_SCSI_HOST)) {
        if (virNodeDeviceGetWWNs(def, &wwnn, &wwpn) < 0)
            goto cleanup;

        /* Because we're about to release the lock and thus run into a race
         * possibility (however improbable) with a udevAddOneDevice change
         * event which would essentially free the existing @def (obj->def) and
         * replace it with something new, we need to grab the parent field
         * and then find the parent obj in order to manage the vport */
        parent = g_strdup(def->parent);

        virNodeDeviceObjEndAPI(&obj);

        if (!(obj = virNodeDeviceObjListFindByName(driver->devs, parent))) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("cannot find parent '%s' definition"), parent);
            goto cleanup;
        }

        if (virSCSIHostGetNumber(parent, &parent_host) < 0)
            goto cleanup;

        if (virVHBAManageVport(parent_host, wwpn, wwnn, VPORT_DELETE) < 0)
            goto cleanup;

        ret = 0;
    } else if (nodeDeviceHasCapability(def, VIR_NODE_DEV_CAP_MDEV)) {
        g_autofree char *errmsg = NULL;

        if (virMdevctlStop(def, &errmsg) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Unable to destroy '%s': %s"), def->name,
                           errmsg && errmsg[0] ? errmsg : "Unknown error");
            goto cleanup;
        }
        ret = 0;
    } else {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Unsupported device type"));
    }

 cleanup:
    virNodeDeviceObjEndAPI(&obj);
    return ret;
}


int
nodeConnectNodeDeviceEventRegisterAny(virConnectPtr conn,
                                      virNodeDevicePtr device,
                                      int eventID,
                                      virConnectNodeDeviceEventGenericCallback callback,
                                      void *opaque,
                                      virFreeCallback freecb)
{
    int callbackID = -1;

    if (virConnectNodeDeviceEventRegisterAnyEnsureACL(conn) < 0)
        return -1;

    if (nodeDeviceWaitInit() < 0)
        return -1;

    if (virNodeDeviceEventStateRegisterID(conn, driver->nodeDeviceEventState,
                                          device, eventID, callback,
                                          opaque, freecb, &callbackID) < 0)
        callbackID = -1;

    return callbackID;
}


int
nodeConnectNodeDeviceEventDeregisterAny(virConnectPtr conn,
                                        int callbackID)
{
    if (virConnectNodeDeviceEventDeregisterAnyEnsureACL(conn) < 0)
        return -1;

    if (nodeDeviceWaitInit() < 0)
        return -1;

    if (virObjectEventStateDeregisterID(conn,
                                        driver->nodeDeviceEventState,
                                        callbackID, true) < 0)
        return -1;

    return 0;
}

int
nodedevRegister(void)
{
#ifdef WITH_UDEV
    return udevNodeRegister();
#endif
}


void
nodeDeviceGenerateName(virNodeDeviceDefPtr def,
                       const char *subsystem,
                       const char *sysname,
                       const char *s)
{
    size_t i;
    g_auto(virBuffer) buf = VIR_BUFFER_INITIALIZER;

    virBufferAsprintf(&buf, "%s_%s",
                      subsystem,
                      sysname);

    if (s != NULL)
        virBufferAsprintf(&buf, "_%s", s);

    def->name = virBufferContentAndReset(&buf);

    for (i = 0; i < strlen(def->name); i++) {
        if (!(g_ascii_isalnum(*(def->name + i))))
            *(def->name + i) = '_';
    }
}


static int
virMdevctlListDefined(virNodeDeviceDefPtr **devs)
{
    int status;
    g_autofree char *output = NULL;
    g_autoptr(virCommand) cmd = nodeDeviceGetMdevctlListCommand(true, &output);

    if (virCommandRun(cmd, &status) < 0 || status != 0)
        return -1;

    if (!output)
        return -1;

    return nodeDeviceParseMdevctlJSON(output, devs);
}


int
nodeDeviceUpdateMediatedDevices(void)
{
    g_autofree virNodeDeviceDefPtr *defs = NULL;
    int ndefs;
    size_t i;

    if ((ndefs = virMdevctlListDefined(&defs)) < 0) {
        virReportSystemError(errno, "%s",
                             _("failed to query mdevs from mdevctl"));
        return -1;
    }

    for (i = 0; i < ndefs; i++) {
        virNodeDeviceObjPtr obj;
        virObjectEventPtr event;
        g_autoptr(virNodeDeviceDef) def = defs[i];
        g_autofree char *name = g_strdup(def->name);
        bool defined = false;

        def->driver = g_strdup("vfio_mdev");

        if (!(obj = virNodeDeviceObjListFindByName(driver->devs, def->name))) {
            virNodeDeviceDefPtr d = g_steal_pointer(&def);
            if (!(obj = virNodeDeviceObjListAssignDef(driver->devs, d))) {
                virNodeDeviceDefFree(d);
                return -1;
            }
        } else {
            bool changed;
            virNodeDeviceDefPtr olddef = virNodeDeviceObjGetDef(obj);

            defined = virNodeDeviceObjIsPersistent(obj);
            /* Active devices contain some additional information (e.g. sysfs
             * path) that is not provided by mdevctl, so re-use the existing
             * definition and copy over new mdev data */
            changed = nodeDeviceDefCopyFromMdevctl(olddef, def);

            if (defined && !changed) {
                /* if this device was already defined and the definition
                 * hasn't changed, there's nothing to do for this device */
                virNodeDeviceObjEndAPI(&obj);
                continue;
            }
        }

        /* all devices returned by virMdevctlListDefined() are persistent */
        virNodeDeviceObjSetPersistent(obj, true);

        if (!defined)
            event = virNodeDeviceEventLifecycleNew(name,
                                                   VIR_NODE_DEVICE_EVENT_DEFINED,
                                                   0);
        else
            event = virNodeDeviceEventUpdateNew(name);

        virNodeDeviceObjEndAPI(&obj);
        virObjectEventStateQueue(driver->nodeDeviceEventState, event);
    }

    return 0;
}


/* returns true if any attributes were copied, else returns false */
static bool
virMediatedDeviceAttrsCopy(virNodeDevCapMdevPtr dst,
                           virNodeDevCapMdevPtr src)
{
    bool ret = false;
    size_t i;

    if (src->nattributes != dst->nattributes) {
        ret = true;
        for (i = 0; i < dst->nattributes; i++)
            virMediatedDeviceAttrFree(dst->attributes[i]);
        g_free(dst->attributes);

        dst->nattributes = src->nattributes;
        dst->attributes = g_new0(virMediatedDeviceAttrPtr,
                                 src->nattributes);
        for (i = 0; i < dst->nattributes; i++)
            dst->attributes[i] = virMediatedDeviceAttrNew();
    }

    for (i = 0; i < src->nattributes; i++) {
        if (STRNEQ_NULLABLE(src->attributes[i]->name,
                            dst->attributes[i]->name)) {
            ret = true;
            g_free(dst->attributes[i]->name);
            dst->attributes[i]->name =
                g_strdup(src->attributes[i]->name);
        }
        if (STRNEQ_NULLABLE(src->attributes[i]->value,
                            dst->attributes[i]->value)) {
            ret = true;
            g_free(dst->attributes[i]->value);
            dst->attributes[i]->value =
                g_strdup(src->attributes[i]->value);
        }
    }

    return ret;
}


/* A mediated device definitions from mdevctl contains additional info that is
 * not available from udev. Transfer this data to the new definition.
 * Returns true if anything was copied, else returns false */
bool
nodeDeviceDefCopyFromMdevctl(virNodeDeviceDefPtr dst,
                             virNodeDeviceDefPtr src)
{
    bool ret = false;
    virNodeDevCapMdevPtr srcmdev = &src->caps->data.mdev;
    virNodeDevCapMdevPtr dstmdev = &dst->caps->data.mdev;

    if (STRNEQ_NULLABLE(dstmdev->type, srcmdev->type)) {
        ret = true;
        g_free(dstmdev->type);
        dstmdev->type = g_strdup(srcmdev->type);
    }

    if (STRNEQ_NULLABLE(dstmdev->uuid, srcmdev->uuid)) {
        ret = true;
        g_free(dstmdev->uuid);
        dstmdev->uuid = g_strdup(srcmdev->uuid);
    }

    if (virMediatedDeviceAttrsCopy(dstmdev, srcmdev))
        ret = true;

    return ret;
}
