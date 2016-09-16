/*
 * test_interface_driver.c: A "mock" hypervisor for use by application unit tests
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

#include "test_interface_driver.h"

#include "test_private_driver.h"

static int testConnectNumOfInterfaces(virConnectPtr conn)
{
    testDriverPtr privconn = conn->privateData;
    size_t i;
    int count = 0;

    testDriverLock(privconn);
    for (i = 0; (i < privconn->ifaces.count); i++) {
        virInterfaceObjLock(privconn->ifaces.objs[i]);
        if (virInterfaceObjIsActive(privconn->ifaces.objs[i]))
            count++;
        virInterfaceObjUnlock(privconn->ifaces.objs[i]);
    }
    testDriverUnlock(privconn);
    return count;
}

static int testConnectListInterfaces(virConnectPtr conn, char **const names, int nnames)
{
    testDriverPtr privconn = conn->privateData;
    int n = 0;
    size_t i;

    testDriverLock(privconn);
    memset(names, 0, sizeof(*names)*nnames);
    for (i = 0; (i < privconn->ifaces.count) && (n < nnames); i++) {
        virInterfaceObjLock(privconn->ifaces.objs[i]);
        if (virInterfaceObjIsActive(privconn->ifaces.objs[i])) {
            if (VIR_STRDUP(names[n++], privconn->ifaces.objs[i]->def->name) < 0) {
                virInterfaceObjUnlock(privconn->ifaces.objs[i]);
                goto error;
            }
        }
        virInterfaceObjUnlock(privconn->ifaces.objs[i]);
    }
    testDriverUnlock(privconn);

    return n;

 error:
    for (n = 0; n < nnames; n++)
        VIR_FREE(names[n]);
    testDriverUnlock(privconn);
    return -1;
}

static int testConnectNumOfDefinedInterfaces(virConnectPtr conn)
{
    testDriverPtr privconn = conn->privateData;
    size_t i;
    int count = 0;

    testDriverLock(privconn);
    for (i = 0; i < privconn->ifaces.count; i++) {
        virInterfaceObjLock(privconn->ifaces.objs[i]);
        if (!virInterfaceObjIsActive(privconn->ifaces.objs[i]))
            count++;
        virInterfaceObjUnlock(privconn->ifaces.objs[i]);
    }
    testDriverUnlock(privconn);
    return count;
}

static int testConnectListDefinedInterfaces(virConnectPtr conn, char **const names, int nnames)
{
    testDriverPtr privconn = conn->privateData;
    int n = 0;
    size_t i;

    testDriverLock(privconn);
    memset(names, 0, sizeof(*names)*nnames);
    for (i = 0; (i < privconn->ifaces.count) && (n < nnames); i++) {
        virInterfaceObjLock(privconn->ifaces.objs[i]);
        if (!virInterfaceObjIsActive(privconn->ifaces.objs[i])) {
            if (VIR_STRDUP(names[n++], privconn->ifaces.objs[i]->def->name) < 0) {
                virInterfaceObjUnlock(privconn->ifaces.objs[i]);
                goto error;
            }
        }
        virInterfaceObjUnlock(privconn->ifaces.objs[i]);
    }
    testDriverUnlock(privconn);

    return n;

 error:
    for (n = 0; n < nnames; n++)
        VIR_FREE(names[n]);
    testDriverUnlock(privconn);
    return -1;
}

static virInterfacePtr testInterfaceLookupByName(virConnectPtr conn,
                                                 const char *name)
{
    testDriverPtr privconn = conn->privateData;
    virInterfaceObjPtr iface;
    virInterfacePtr ret = NULL;

    testDriverLock(privconn);
    iface = virInterfaceFindByName(&privconn->ifaces, name);
    testDriverUnlock(privconn);

    if (iface == NULL) {
        virReportError(VIR_ERR_NO_INTERFACE, NULL);
        goto cleanup;
    }

    ret = virGetInterface(conn, iface->def->name, iface->def->mac);

 cleanup:
    if (iface)
        virInterfaceObjUnlock(iface);
    return ret;
}

static virInterfacePtr testInterfaceLookupByMACString(virConnectPtr conn,
                                                      const char *mac)
{
    testDriverPtr privconn = conn->privateData;
    virInterfaceObjPtr iface;
    int ifacect;
    virInterfacePtr ret = NULL;

    testDriverLock(privconn);
    ifacect = virInterfaceFindByMACString(&privconn->ifaces, mac, &iface, 1);
    testDriverUnlock(privconn);

    if (ifacect == 0) {
        virReportError(VIR_ERR_NO_INTERFACE, NULL);
        goto cleanup;
    }

    if (ifacect > 1) {
        virReportError(VIR_ERR_MULTIPLE_INTERFACES, NULL);
        goto cleanup;
    }

    ret = virGetInterface(conn, iface->def->name, iface->def->mac);

 cleanup:
    if (iface)
        virInterfaceObjUnlock(iface);
    return ret;
}

static char *testInterfaceGetXMLDesc(virInterfacePtr iface,
                                     unsigned int flags)
{
    testDriverPtr privconn = iface->conn->privateData;
    virInterfaceObjPtr privinterface;
    char *ret = NULL;

    virCheckFlags(0, NULL);

    testDriverLock(privconn);
    privinterface = virInterfaceFindByName(&privconn->ifaces,
                                           iface->name);
    testDriverUnlock(privconn);

    if (privinterface == NULL) {
        virReportError(VIR_ERR_NO_INTERFACE, __FUNCTION__);
        goto cleanup;
    }

    ret = virInterfaceDefFormat(privinterface->def);

 cleanup:
    if (privinterface)
        virInterfaceObjUnlock(privinterface);
    return ret;
}

static virInterfacePtr testInterfaceDefineXML(virConnectPtr conn, const char *xmlStr,
                                              unsigned int flags)
{
    testDriverPtr privconn = conn->privateData;
    virInterfaceDefPtr def;
    virInterfaceObjPtr iface = NULL;
    virInterfacePtr ret = NULL;

    virCheckFlags(0, NULL);

    testDriverLock(privconn);
    if ((def = virInterfaceDefParseString(xmlStr)) == NULL)
        goto cleanup;

    if ((iface = virInterfaceAssignDef(&privconn->ifaces, def)) == NULL)
        goto cleanup;
    def = NULL;

    ret = virGetInterface(conn, iface->def->name, iface->def->mac);

 cleanup:
    virInterfaceDefFree(def);
    if (iface)
        virInterfaceObjUnlock(iface);
    testDriverUnlock(privconn);
    return ret;
}

static int testInterfaceUndefine(virInterfacePtr iface)
{
    testDriverPtr privconn = iface->conn->privateData;
    virInterfaceObjPtr privinterface;
    int ret = -1;

    testDriverLock(privconn);
    privinterface = virInterfaceFindByName(&privconn->ifaces,
                                           iface->name);

    if (privinterface == NULL) {
        virReportError(VIR_ERR_NO_INTERFACE, NULL);
        goto cleanup;
    }

    virInterfaceRemove(&privconn->ifaces,
                       privinterface);
    ret = 0;

 cleanup:
    testDriverUnlock(privconn);
    return ret;
}

static int testInterfaceCreate(virInterfacePtr iface,
                               unsigned int flags)
{
    testDriverPtr privconn = iface->conn->privateData;
    virInterfaceObjPtr privinterface;
    int ret = -1;

    virCheckFlags(0, -1);

    testDriverLock(privconn);
    privinterface = virInterfaceFindByName(&privconn->ifaces,
                                           iface->name);

    if (privinterface == NULL) {
        virReportError(VIR_ERR_NO_INTERFACE, NULL);
        goto cleanup;
    }

    if (privinterface->active != 0) {
        virReportError(VIR_ERR_OPERATION_INVALID, NULL);
        goto cleanup;
    }

    privinterface->active = 1;
    ret = 0;

 cleanup:
    if (privinterface)
        virInterfaceObjUnlock(privinterface);
    testDriverUnlock(privconn);
    return ret;
}

static int testInterfaceDestroy(virInterfacePtr iface,
                                unsigned int flags)
{
    testDriverPtr privconn = iface->conn->privateData;
    virInterfaceObjPtr privinterface;
    int ret = -1;

    virCheckFlags(0, -1);

    testDriverLock(privconn);
    privinterface = virInterfaceFindByName(&privconn->ifaces,
                                           iface->name);

    if (privinterface == NULL) {
        virReportError(VIR_ERR_NO_INTERFACE, NULL);
        goto cleanup;
    }

    if (privinterface->active == 0) {
        virReportError(VIR_ERR_OPERATION_INVALID, NULL);
        goto cleanup;
    }

    privinterface->active = 0;
    ret = 0;

 cleanup:
    if (privinterface)
        virInterfaceObjUnlock(privinterface);
    testDriverUnlock(privconn);
    return ret;
}

static int testInterfaceIsActive(virInterfacePtr iface)
{
    testDriverPtr privconn = iface->conn->privateData;
    virInterfaceObjPtr obj;
    int ret = -1;

    testDriverLock(privconn);
    obj = virInterfaceFindByName(&privconn->ifaces, iface->name);
    testDriverUnlock(privconn);
    if (!obj) {
        virReportError(VIR_ERR_NO_INTERFACE, NULL);
        goto cleanup;
    }
    ret = virInterfaceObjIsActive(obj);

 cleanup:
    if (obj)
        virInterfaceObjUnlock(obj);
    return ret;
}

static int testInterfaceChangeBegin(virConnectPtr conn,
                                    unsigned int flags)
{
    testDriverPtr privconn = conn->privateData;
    int ret = -1;

    virCheckFlags(0, -1);

    testDriverLock(privconn);
    if (privconn->transaction_running) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("there is another transaction running."));
        goto cleanup;
    }

    privconn->transaction_running = true;

    if (virInterfaceObjListClone(&privconn->ifaces,
                                 &privconn->backupIfaces) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    testDriverUnlock(privconn);
    return ret;
}

static int testInterfaceChangeCommit(virConnectPtr conn,
                                     unsigned int flags)
{
    testDriverPtr privconn = conn->privateData;
    int ret = -1;

    virCheckFlags(0, -1);

    testDriverLock(privconn);

    if (!privconn->transaction_running) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("no transaction running, "
                         "nothing to be committed."));
        goto cleanup;
    }

    virInterfaceObjListFree(&privconn->backupIfaces);
    privconn->transaction_running = false;

    ret = 0;

 cleanup:
    testDriverUnlock(privconn);

    return ret;
}

static int testInterfaceChangeRollback(virConnectPtr conn,
                                       unsigned int flags)
{
    testDriverPtr privconn = conn->privateData;
    int ret = -1;

    virCheckFlags(0, -1);

    testDriverLock(privconn);

    if (!privconn->transaction_running) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("no transaction running, "
                         "nothing to rollback."));
        goto cleanup;
    }

    virInterfaceObjListFree(&privconn->ifaces);
    privconn->ifaces.count = privconn->backupIfaces.count;
    privconn->ifaces.objs = privconn->backupIfaces.objs;
    privconn->backupIfaces.count = 0;
    privconn->backupIfaces.objs = NULL;

    privconn->transaction_running = false;

    ret = 0;

 cleanup:
    testDriverUnlock(privconn);
    return ret;
}

virInterfaceDriver testInterfaceDriver = {
    .connectNumOfInterfaces = testConnectNumOfInterfaces, /* 0.7.0 */
    .connectListInterfaces = testConnectListInterfaces, /* 0.7.0 */
    .connectNumOfDefinedInterfaces = testConnectNumOfDefinedInterfaces, /* 0.7.0 */
    .connectListDefinedInterfaces = testConnectListDefinedInterfaces, /* 0.7.0 */
    .interfaceLookupByName = testInterfaceLookupByName, /* 0.7.0 */
    .interfaceLookupByMACString = testInterfaceLookupByMACString, /* 0.7.0 */
    .interfaceGetXMLDesc = testInterfaceGetXMLDesc, /* 0.7.0 */
    .interfaceDefineXML = testInterfaceDefineXML, /* 0.7.0 */
    .interfaceUndefine = testInterfaceUndefine, /* 0.7.0 */
    .interfaceCreate = testInterfaceCreate, /* 0.7.0 */
    .interfaceDestroy = testInterfaceDestroy, /* 0.7.0 */
    .interfaceIsActive = testInterfaceIsActive, /* 0.7.3 */
    .interfaceChangeBegin = testInterfaceChangeBegin,   /* 0.9.2 */
    .interfaceChangeCommit = testInterfaceChangeCommit,  /* 0.9.2 */
    .interfaceChangeRollback = testInterfaceChangeRollback, /* 0.9.2 */
};
