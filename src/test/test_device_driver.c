/*
 * test_device_driver.c: A "mock" hypervisor for use by application unit tests
 *
 * Copyright (C) 2006-2015 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
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
 * Daniel Berrange <berrange@redhat.com>
 */

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libxml/xmlsave.h>
#include <libxml/xpathInternals.h>


#include "virerror.h"
#include "datatypes.h"
#include "test_driver.h"
#include "virbuffer.h"
#include "viruuid.h"
#include "capabilities.h"
#include "configmake.h"
#include "viralloc.h"
#include "network_conf.h"
#include "interface_conf.h"
#include "domain_conf.h"
#include "domain_event.h"
#include "network_event.h"
#include "snapshot_conf.h"
#include "fdstream.h"
#include "storage_conf.h"
#include "storage_event.h"
#include "node_device_conf.h"
#include "node_device_event.h"
#include "virxml.h"
#include "virthread.h"
#include "virlog.h"
#include "virfile.h"
#include "virtypedparam.h"
#include "virrandom.h"
#include "virstring.h"
#include "cpu/cpu.h"
#include "virauth.h"
#include "viratomic.h"
#include "virdomainobjlist.h"
#include "virhostcpu.h"

#include "test_device_driver.h"

#include "test_private_driver.h"

/* Node device implementations */

static int
testConnectNodeDeviceEventRegisterAny(virConnectPtr conn,
                                      virNodeDevicePtr dev,
                                      int eventID,
                                      virConnectNodeDeviceEventGenericCallback callback,
                                      void *opaque,
                                      virFreeCallback freecb)
{
    testDriverPtr driver = conn->privateData;
    int ret;

    if (virNodeDeviceEventStateRegisterID(conn, driver->eventState,
                                          dev, eventID, callback,
                                          opaque, freecb, &ret) < 0)
        ret = -1;

    return ret;
}

static int
testConnectNodeDeviceEventDeregisterAny(virConnectPtr conn,
                                        int callbackID)
{
    testDriverPtr driver = conn->privateData;
    int ret = 0;

    if (virObjectEventStateDeregisterID(conn, driver->eventState,
                                        callbackID) < 0)
        ret = -1;

    return ret;
}

static int
testNodeNumOfDevices(virConnectPtr conn,
                     const char *cap,
                     unsigned int flags)
{
    testDriverPtr driver = conn->privateData;
    int ndevs = 0;
    size_t i;

    virCheckFlags(0, -1);

    testDriverLock(driver);
    for (i = 0; i < driver->devs.count; i++)
        if ((cap == NULL) ||
            virNodeDeviceHasCap(driver->devs.objs[i], cap))
            ++ndevs;
    testDriverUnlock(driver);

    return ndevs;
}

static int
testNodeListDevices(virConnectPtr conn,
                    const char *cap,
                    char **const names,
                    int maxnames,
                    unsigned int flags)
{
    testDriverPtr driver = conn->privateData;
    int ndevs = 0;
    size_t i;

    virCheckFlags(0, -1);

    testDriverLock(driver);
    for (i = 0; i < driver->devs.count && ndevs < maxnames; i++) {
        virNodeDeviceObjLock(driver->devs.objs[i]);
        if (cap == NULL ||
            virNodeDeviceHasCap(driver->devs.objs[i], cap)) {
            if (VIR_STRDUP(names[ndevs++], driver->devs.objs[i]->def->name) < 0) {
                virNodeDeviceObjUnlock(driver->devs.objs[i]);
                goto failure;
            }
        }
        virNodeDeviceObjUnlock(driver->devs.objs[i]);
    }
    testDriverUnlock(driver);

    return ndevs;

 failure:
    testDriverUnlock(driver);
    --ndevs;
    while (--ndevs >= 0)
        VIR_FREE(names[ndevs]);
    return -1;
}

static virNodeDevicePtr
testNodeDeviceLookupByName(virConnectPtr conn, const char *name)
{
    testDriverPtr driver = conn->privateData;
    virNodeDeviceObjPtr obj;
    virNodeDevicePtr ret = NULL;

    testDriverLock(driver);
    obj = virNodeDeviceFindByName(&driver->devs, name);
    testDriverUnlock(driver);

    if (!obj) {
        virReportError(VIR_ERR_NO_NODE_DEVICE,
                       _("no node device with matching name '%s'"),
                       name);
        goto cleanup;
    }

    ret = virGetNodeDevice(conn, name);

 cleanup:
    if (obj)
        virNodeDeviceObjUnlock(obj);
    return ret;
}

static char *
testNodeDeviceGetXMLDesc(virNodeDevicePtr dev,
                         unsigned int flags)
{
    testDriverPtr driver = dev->conn->privateData;
    virNodeDeviceObjPtr obj;
    char *ret = NULL;

    virCheckFlags(0, NULL);

    testDriverLock(driver);
    obj = virNodeDeviceFindByName(&driver->devs, dev->name);
    testDriverUnlock(driver);

    if (!obj) {
        virReportError(VIR_ERR_NO_NODE_DEVICE,
                       _("no node device with matching name '%s'"),
                       dev->name);
        goto cleanup;
    }

    ret = virNodeDeviceDefFormat(obj->def);

 cleanup:
    if (obj)
        virNodeDeviceObjUnlock(obj);
    return ret;
}

static char *
testNodeDeviceGetParent(virNodeDevicePtr dev)
{
    testDriverPtr driver = dev->conn->privateData;
    virNodeDeviceObjPtr obj;
    char *ret = NULL;

    testDriverLock(driver);
    obj = virNodeDeviceFindByName(&driver->devs, dev->name);
    testDriverUnlock(driver);

    if (!obj) {
        virReportError(VIR_ERR_NO_NODE_DEVICE,
                       _("no node device with matching name '%s'"),
                       dev->name);
        goto cleanup;
    }

    if (obj->def->parent) {
        ignore_value(VIR_STRDUP(ret, obj->def->parent));
    } else {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("no parent for this device"));
    }

 cleanup:
    if (obj)
        virNodeDeviceObjUnlock(obj);
    return ret;
}


static int
testNodeDeviceNumOfCaps(virNodeDevicePtr dev)
{
    testDriverPtr driver = dev->conn->privateData;
    virNodeDeviceObjPtr obj;
    virNodeDevCapsDefPtr caps;
    int ncaps = 0;
    int ret = -1;

    testDriverLock(driver);
    obj = virNodeDeviceFindByName(&driver->devs, dev->name);
    testDriverUnlock(driver);

    if (!obj) {
        virReportError(VIR_ERR_NO_NODE_DEVICE,
                       _("no node device with matching name '%s'"),
                       dev->name);
        goto cleanup;
    }

    for (caps = obj->def->caps; caps; caps = caps->next)
        ++ncaps;
    ret = ncaps;

 cleanup:
    if (obj)
        virNodeDeviceObjUnlock(obj);
    return ret;
}


static int
testNodeDeviceListCaps(virNodeDevicePtr dev, char **const names, int maxnames)
{
    testDriverPtr driver = dev->conn->privateData;
    virNodeDeviceObjPtr obj;
    virNodeDevCapsDefPtr caps;
    int ncaps = 0;
    int ret = -1;

    testDriverLock(driver);
    obj = virNodeDeviceFindByName(&driver->devs, dev->name);
    testDriverUnlock(driver);

    if (!obj) {
        virReportError(VIR_ERR_NO_NODE_DEVICE,
                       _("no node device with matching name '%s'"),
                       dev->name);
        goto cleanup;
    }

    for (caps = obj->def->caps; caps && ncaps < maxnames; caps = caps->next) {
        if (VIR_STRDUP(names[ncaps++], virNodeDevCapTypeToString(caps->data.type)) < 0)
            goto cleanup;
    }
    ret = ncaps;

 cleanup:
    if (obj)
        virNodeDeviceObjUnlock(obj);
    if (ret == -1) {
        --ncaps;
        while (--ncaps >= 0)
            VIR_FREE(names[ncaps]);
    }
    return ret;
}

static virNodeDevicePtr
testNodeDeviceCreateXML(virConnectPtr conn,
                        const char *xmlDesc,
                        unsigned int flags)
{
    testDriverPtr driver = conn->privateData;
    virNodeDeviceDefPtr def = NULL;
    virNodeDeviceObjPtr obj = NULL;
    char *wwnn = NULL, *wwpn = NULL;
    int parent_host = -1;
    virNodeDevicePtr dev = NULL;
    virNodeDevCapsDefPtr caps;
    virObjectEventPtr event = NULL;

    virCheckFlags(0, NULL);

    testDriverLock(driver);

    def = virNodeDeviceDefParseString(xmlDesc, CREATE_DEVICE, NULL);
    if (def == NULL)
        goto cleanup;

    /* We run these next two simply for validation */
    if (virNodeDeviceGetWWNs(def, &wwnn, &wwpn) == -1)
        goto cleanup;

    if (virNodeDeviceGetParentHost(&driver->devs,
                                   def->name,
                                   def->parent,
                                   &parent_host) == -1) {
        goto cleanup;
    }

    /* 'name' is supposed to be filled in by the node device backend, which
     * we don't have. Use WWPN instead. */
    VIR_FREE(def->name);
    if (VIR_STRDUP(def->name, wwpn) < 0)
        goto cleanup;

    /* Fill in a random 'host' and 'unique_id' value,
     * since this would also come from the backend */
    caps = def->caps;
    while (caps) {
        if (caps->data.type != VIR_NODE_DEV_CAP_SCSI_HOST)
            continue;

        caps->data.scsi_host.host = virRandomBits(10);
        caps->data.scsi_host.unique_id = 2;
        caps = caps->next;
    }


    if (!(obj = virNodeDeviceAssignDef(&driver->devs, def)))
        goto cleanup;
    virNodeDeviceObjUnlock(obj);

    event = virNodeDeviceEventLifecycleNew(def->name,
                                           VIR_NODE_DEVICE_EVENT_CREATED,
                                           0);

    dev = virGetNodeDevice(conn, def->name);
    def = NULL;
 cleanup:
    testDriverUnlock(driver);
    virNodeDeviceDefFree(def);
    testObjectEventQueue(driver, event);
    VIR_FREE(wwnn);
    VIR_FREE(wwpn);
    return dev;
}

static int
testNodeDeviceDestroy(virNodeDevicePtr dev)
{
    int ret = 0;
    testDriverPtr driver = dev->conn->privateData;
    virNodeDeviceObjPtr obj = NULL;
    char *parent_name = NULL, *wwnn = NULL, *wwpn = NULL;
    int parent_host = -1;
    virObjectEventPtr event = NULL;

    testDriverLock(driver);
    obj = virNodeDeviceFindByName(&driver->devs, dev->name);
    testDriverUnlock(driver);

    if (!obj) {
        virReportError(VIR_ERR_NO_NODE_DEVICE,
                       _("no node device with matching name '%s'"),
                       dev->name);
        goto out;
    }

    if (virNodeDeviceGetWWNs(obj->def, &wwnn, &wwpn) == -1)
        goto out;

    if (VIR_STRDUP(parent_name, obj->def->parent) < 0)
        goto out;

    /* virNodeDeviceGetParentHost will cause the device object's lock to be
     * taken, so we have to dup the parent's name and drop the lock
     * before calling it.  We don't need the reference to the object
     * any more once we have the parent's name.  */
    virNodeDeviceObjUnlock(obj);

    /* We do this just for basic validation */
    if (virNodeDeviceGetParentHost(&driver->devs,
                                   dev->name,
                                   parent_name,
                                   &parent_host) == -1) {
        obj = NULL;
        goto out;
    }

    event = virNodeDeviceEventLifecycleNew(dev->name,
                                           VIR_NODE_DEVICE_EVENT_DELETED,
                                           0);

    virNodeDeviceObjLock(obj);
    virNodeDeviceObjRemove(&driver->devs, &obj);

 out:
    if (obj)
        virNodeDeviceObjUnlock(obj);
    testObjectEventQueue(driver, event);
    VIR_FREE(parent_name);
    VIR_FREE(wwnn);
    VIR_FREE(wwpn);
    return ret;
}

virNodeDeviceDriver testNodeDeviceDriver = {
    .connectNodeDeviceEventRegisterAny = testConnectNodeDeviceEventRegisterAny, /* 2.2.0 */
    .connectNodeDeviceEventDeregisterAny = testConnectNodeDeviceEventDeregisterAny, /* 2.2.0 */
    .nodeNumOfDevices = testNodeNumOfDevices, /* 0.7.2 */
    .nodeListDevices = testNodeListDevices, /* 0.7.2 */
    .nodeDeviceLookupByName = testNodeDeviceLookupByName, /* 0.7.2 */
    .nodeDeviceGetXMLDesc = testNodeDeviceGetXMLDesc, /* 0.7.2 */
    .nodeDeviceGetParent = testNodeDeviceGetParent, /* 0.7.2 */
    .nodeDeviceNumOfCaps = testNodeDeviceNumOfCaps, /* 0.7.2 */
    .nodeDeviceListCaps = testNodeDeviceListCaps, /* 0.7.2 */
    .nodeDeviceCreateXML = testNodeDeviceCreateXML, /* 0.7.3 */
    .nodeDeviceDestroy = testNodeDeviceDestroy, /* 0.7.3 */
};
