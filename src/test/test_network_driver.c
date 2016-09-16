/*
 * test_network_driver.c: A "mock" hypervisor for use by application unit tests
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
#include "test_network_driver.h"

#include "test_private_driver.h"

static int testConnectNumOfNetworks(virConnectPtr conn)
{
    testDriverPtr privconn = conn->privateData;
    int numActive;

    numActive = virNetworkObjListNumOfNetworks(privconn->networks,
                                               true, NULL, conn);
    return numActive;
}

static int testConnectListNetworks(virConnectPtr conn, char **const names, int nnames) {
    testDriverPtr privconn = conn->privateData;
    int n;

    n = virNetworkObjListGetNames(privconn->networks,
                                  true, names, nnames, NULL, conn);
    return n;
}

static int testConnectNumOfDefinedNetworks(virConnectPtr conn)
{
    testDriverPtr privconn = conn->privateData;
    int numInactive;

    numInactive = virNetworkObjListNumOfNetworks(privconn->networks,
                                                 false, NULL, conn);
    return numInactive;
}

static int testConnectListDefinedNetworks(virConnectPtr conn, char **const names, int nnames) {
    testDriverPtr privconn = conn->privateData;
    int n;

    n = virNetworkObjListGetNames(privconn->networks,
                                  false, names, nnames, NULL, conn);
    return n;
}

static int
testConnectListAllNetworks(virConnectPtr conn,
                           virNetworkPtr **nets,
                           unsigned int flags)
{
    testDriverPtr privconn = conn->privateData;

    virCheckFlags(VIR_CONNECT_LIST_NETWORKS_FILTERS_ALL, -1);

    return virNetworkObjListExport(conn, privconn->networks, nets, NULL, flags);
}

static int testNetworkIsActive(virNetworkPtr net)
{
    testDriverPtr privconn = net->conn->privateData;
    virNetworkObjPtr obj;
    int ret = -1;

    obj = virNetworkObjFindByUUID(privconn->networks, net->uuid);
    if (!obj) {
        virReportError(VIR_ERR_NO_NETWORK, NULL);
        goto cleanup;
    }
    ret = virNetworkObjIsActive(obj);

 cleanup:
    virNetworkObjEndAPI(&obj);
    return ret;
}

static int testNetworkIsPersistent(virNetworkPtr net)
{
    testDriverPtr privconn = net->conn->privateData;
    virNetworkObjPtr obj;
    int ret = -1;

    obj = virNetworkObjFindByUUID(privconn->networks, net->uuid);
    if (!obj) {
        virReportError(VIR_ERR_NO_NETWORK, NULL);
        goto cleanup;
    }
    ret = obj->persistent;

 cleanup:
    virNetworkObjEndAPI(&obj);
    return ret;
}


static virNetworkPtr testNetworkCreateXML(virConnectPtr conn, const char *xml)
{
    testDriverPtr privconn = conn->privateData;
    virNetworkDefPtr def;
    virNetworkObjPtr net = NULL;
    virNetworkPtr ret = NULL;
    virObjectEventPtr event = NULL;

    if ((def = virNetworkDefParseString(xml)) == NULL)
        goto cleanup;

    if (!(net = virNetworkAssignDef(privconn->networks, def,
                                    VIR_NETWORK_OBJ_LIST_ADD_LIVE |
                                    VIR_NETWORK_OBJ_LIST_ADD_CHECK_LIVE)))
        goto cleanup;
    def = NULL;
    net->active = 1;

    event = virNetworkEventLifecycleNew(net->def->name, net->def->uuid,
                                        VIR_NETWORK_EVENT_STARTED,
                                        0);

    ret = virGetNetwork(conn, net->def->name, net->def->uuid);

 cleanup:
    virNetworkDefFree(def);
    testObjectEventQueue(privconn, event);
    virNetworkObjEndAPI(&net);
    return ret;
}

static
virNetworkPtr testNetworkDefineXML(virConnectPtr conn, const char *xml)
{
    testDriverPtr privconn = conn->privateData;
    virNetworkDefPtr def;
    virNetworkObjPtr net = NULL;
    virNetworkPtr ret = NULL;
    virObjectEventPtr event = NULL;

    if ((def = virNetworkDefParseString(xml)) == NULL)
        goto cleanup;

    if (!(net = virNetworkAssignDef(privconn->networks, def, 0)))
        goto cleanup;
    def = NULL;

    event = virNetworkEventLifecycleNew(net->def->name, net->def->uuid,
                                        VIR_NETWORK_EVENT_DEFINED,
                                        0);

    ret = virGetNetwork(conn, net->def->name, net->def->uuid);

 cleanup:
    virNetworkDefFree(def);
    testObjectEventQueue(privconn, event);
    virNetworkObjEndAPI(&net);
    return ret;
}

static int testNetworkUndefine(virNetworkPtr network)
{
    testDriverPtr privconn = network->conn->privateData;
    virNetworkObjPtr privnet;
    int ret = -1;
    virObjectEventPtr event = NULL;

    privnet = virNetworkObjFindByName(privconn->networks, network->name);

    if (privnet == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    if (virNetworkObjIsActive(privnet)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("Network '%s' is still running"), network->name);
        goto cleanup;
    }

    event = virNetworkEventLifecycleNew(network->name, network->uuid,
                                        VIR_NETWORK_EVENT_UNDEFINED,
                                        0);

    virNetworkRemoveInactive(privconn->networks, privnet);
    ret = 0;

 cleanup:
    testObjectEventQueue(privconn, event);
    virNetworkObjEndAPI(&privnet);
    return ret;
}

static int
testNetworkUpdate(virNetworkPtr net,
                  unsigned int command,
                  unsigned int section,
                  int parentIndex,
                  const char *xml,
                  unsigned int flags)
{
    testDriverPtr privconn = net->conn->privateData;
    virNetworkObjPtr network = NULL;
    int isActive, ret = -1;

    virCheckFlags(VIR_NETWORK_UPDATE_AFFECT_LIVE |
                  VIR_NETWORK_UPDATE_AFFECT_CONFIG,
                  -1);

    network = virNetworkObjFindByUUID(privconn->networks, net->uuid);
    if (!network) {
        virReportError(VIR_ERR_NO_NETWORK,
                       "%s", _("no network with matching uuid"));
        goto cleanup;
    }

    /* VIR_NETWORK_UPDATE_AFFECT_CURRENT means "change LIVE if network
     * is active, else change CONFIG
    */
    isActive = virNetworkObjIsActive(network);
    if ((flags & (VIR_NETWORK_UPDATE_AFFECT_LIVE
                   | VIR_NETWORK_UPDATE_AFFECT_CONFIG)) ==
        VIR_NETWORK_UPDATE_AFFECT_CURRENT) {
        if (isActive)
            flags |= VIR_NETWORK_UPDATE_AFFECT_LIVE;
        else
            flags |= VIR_NETWORK_UPDATE_AFFECT_CONFIG;
    }

    /* update the network config in memory/on disk */
    if (virNetworkObjUpdate(network, command, section, parentIndex, xml, flags) < 0)
       goto cleanup;

    ret = 0;
 cleanup:
    virNetworkObjEndAPI(&network);
    return ret;
}

static int testNetworkCreate(virNetworkPtr network)
{
    testDriverPtr privconn = network->conn->privateData;
    virNetworkObjPtr privnet;
    int ret = -1;
    virObjectEventPtr event = NULL;

    privnet = virNetworkObjFindByName(privconn->networks, network->name);
    if (privnet == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    if (virNetworkObjIsActive(privnet)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("Network '%s' is already running"), network->name);
        goto cleanup;
    }

    privnet->active = 1;
    event = virNetworkEventLifecycleNew(privnet->def->name, privnet->def->uuid,
                                        VIR_NETWORK_EVENT_STARTED,
                                        0);
    ret = 0;

 cleanup:
    testObjectEventQueue(privconn, event);
    virNetworkObjEndAPI(&privnet);
    return ret;
}

static int testNetworkDestroy(virNetworkPtr network)
{
    testDriverPtr privconn = network->conn->privateData;
    virNetworkObjPtr privnet;
    int ret = -1;
    virObjectEventPtr event = NULL;

    privnet = virNetworkObjFindByName(privconn->networks, network->name);
    if (privnet == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    privnet->active = 0;
    event = virNetworkEventLifecycleNew(privnet->def->name, privnet->def->uuid,
                                        VIR_NETWORK_EVENT_STOPPED,
                                        0);
    if (!privnet->persistent)
        virNetworkRemoveInactive(privconn->networks, privnet);

    ret = 0;

 cleanup:
    testObjectEventQueue(privconn, event);
    virNetworkObjEndAPI(&privnet);
    return ret;
}

static char *testNetworkGetXMLDesc(virNetworkPtr network,
                                   unsigned int flags)
{
    testDriverPtr privconn = network->conn->privateData;
    virNetworkObjPtr privnet;
    char *ret = NULL;

    virCheckFlags(0, NULL);

    privnet = virNetworkObjFindByName(privconn->networks, network->name);
    if (privnet == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    ret = virNetworkDefFormat(privnet->def, flags);

 cleanup:
    virNetworkObjEndAPI(&privnet);
    return ret;
}

static char *testNetworkGetBridgeName(virNetworkPtr network) {
    testDriverPtr privconn = network->conn->privateData;
    char *bridge = NULL;
    virNetworkObjPtr privnet;

    privnet = virNetworkObjFindByName(privconn->networks, network->name);
    if (privnet == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    if (!(privnet->def->bridge)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("network '%s' does not have a bridge name."),
                       privnet->def->name);
        goto cleanup;
    }

    ignore_value(VIR_STRDUP(bridge, privnet->def->bridge));

 cleanup:
    virNetworkObjEndAPI(&privnet);
    return bridge;
}

static int testNetworkGetAutostart(virNetworkPtr network,
                                   int *autostart)
{
    testDriverPtr privconn = network->conn->privateData;
    virNetworkObjPtr privnet;
    int ret = -1;

    privnet = virNetworkObjFindByName(privconn->networks, network->name);
    if (privnet == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    *autostart = privnet->autostart;
    ret = 0;

 cleanup:
    virNetworkObjEndAPI(&privnet);
    return ret;
}

static int testNetworkSetAutostart(virNetworkPtr network,
                                   int autostart)
{
    testDriverPtr privconn = network->conn->privateData;
    virNetworkObjPtr privnet;
    int ret = -1;

    privnet = virNetworkObjFindByName(privconn->networks, network->name);
    if (privnet == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    privnet->autostart = autostart ? 1 : 0;
    ret = 0;

 cleanup:
    virNetworkObjEndAPI(&privnet);
    return ret;
}

static virNetworkPtr testNetworkLookupByUUID(virConnectPtr conn,
                                             const unsigned char *uuid)
{
    testDriverPtr privconn = conn->privateData;
    virNetworkObjPtr net;
    virNetworkPtr ret = NULL;

    net = virNetworkObjFindByUUID(privconn->networks, uuid);
    if (net == NULL) {
        virReportError(VIR_ERR_NO_NETWORK, NULL);
        goto cleanup;
    }

    ret = virGetNetwork(conn, net->def->name, net->def->uuid);

 cleanup:
    virNetworkObjEndAPI(&net);
    return ret;
}

static virNetworkPtr testNetworkLookupByName(virConnectPtr conn,
                                             const char *name)
{
    testDriverPtr privconn = conn->privateData;
    virNetworkObjPtr net;
    virNetworkPtr ret = NULL;

    net = virNetworkObjFindByName(privconn->networks, name);
    if (net == NULL) {
        virReportError(VIR_ERR_NO_NETWORK, NULL);
        goto cleanup;
    }

    ret = virGetNetwork(conn, net->def->name, net->def->uuid);

 cleanup:
    virNetworkObjEndAPI(&net);
    return ret;
}

static int
testConnectNetworkEventRegisterAny(virConnectPtr conn,
                                   virNetworkPtr net,
                                   int eventID,
                                   virConnectNetworkEventGenericCallback callback,
                                   void *opaque,
                                   virFreeCallback freecb)
{
    testDriverPtr driver = conn->privateData;
    int ret;

    if (virNetworkEventStateRegisterID(conn, driver->eventState,
                                       net, eventID, callback,
                                       opaque, freecb, &ret) < 0)
        ret = -1;

    return ret;
}

static int
testConnectNetworkEventDeregisterAny(virConnectPtr conn,
                                     int callbackID)
{
    testDriverPtr driver = conn->privateData;
    int ret = 0;

    if (virObjectEventStateDeregisterID(conn, driver->eventState,
                                        callbackID) < 0)
        ret = -1;

    return ret;
}

virNetworkDriver testNetworkDriver = {
    .connectNumOfNetworks = testConnectNumOfNetworks, /* 0.3.2 */
    .connectListNetworks = testConnectListNetworks, /* 0.3.2 */
    .connectNumOfDefinedNetworks = testConnectNumOfDefinedNetworks, /* 0.3.2 */
    .connectListDefinedNetworks = testConnectListDefinedNetworks, /* 0.3.2 */
    .connectListAllNetworks = testConnectListAllNetworks, /* 0.10.2 */
    .connectNetworkEventRegisterAny = testConnectNetworkEventRegisterAny, /* 1.2.1 */
    .connectNetworkEventDeregisterAny = testConnectNetworkEventDeregisterAny, /* 1.2.1 */
    .networkLookupByUUID = testNetworkLookupByUUID, /* 0.3.2 */
    .networkLookupByName = testNetworkLookupByName, /* 0.3.2 */
    .networkCreateXML = testNetworkCreateXML, /* 0.3.2 */
    .networkDefineXML = testNetworkDefineXML, /* 0.3.2 */
    .networkUndefine = testNetworkUndefine, /* 0.3.2 */
    .networkUpdate = testNetworkUpdate, /* 0.10.2 */
    .networkCreate = testNetworkCreate, /* 0.3.2 */
    .networkDestroy = testNetworkDestroy, /* 0.3.2 */
    .networkGetXMLDesc = testNetworkGetXMLDesc, /* 0.3.2 */
    .networkGetBridgeName = testNetworkGetBridgeName, /* 0.3.2 */
    .networkGetAutostart = testNetworkGetAutostart, /* 0.3.2 */
    .networkSetAutostart = testNetworkSetAutostart, /* 0.3.2 */
    .networkIsActive = testNetworkIsActive, /* 0.7.3 */
    .networkIsPersistent = testNetworkIsPersistent, /* 0.7.3 */
};
