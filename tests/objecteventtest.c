/*
 * Copyright (C) 2014 Red Hat, Inc.
 * Copyright (C) 2013 SUSE LINUX Products GmbH, Nuernberg, Germany.
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
 * License along with this library;  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Cedric Bosdonnat <cbosdonnat@suse.com>
 */

#include <config.h>

#include "testutils.h"

#include "virerror.h"
#include "virxml.h"

#define VIR_FROM_THIS VIR_FROM_NONE


static const char domainDef[] =
"<domain type='test'>"
"  <name>test-domain</name>"
"  <uuid>77a6fc12-07b5-9415-8abb-a803613f2a40</uuid>"
"  <memory>8388608</memory>"
"  <currentMemory>2097152</currentMemory>"
"  <vcpu>2</vcpu>"
"  <os>"
"    <type>hvm</type>"
"  </os>"
"</domain>";

static const char networkDef[] =
"<network>\n"
"  <name>test</name>\n"
"  <bridge name=\"virbr0\"/>\n"
"  <forward/>\n"
"  <ip address=\"192.168.122.1\" netmask=\"255.255.255.0\">\n"
"    <dhcp>\n"
"      <range start=\"192.168.122.2\" end=\"192.168.122.254\"/>\n"
"    </dhcp>\n"
"  </ip>\n"
"</network>\n";

static const char storagePoolDef[] =
"<pool type='dir'>\n"
"  <name>P</name>\n"
"  <target>\n"
"    <path>/target-path</path>\n"
"  </target>\n"
"</pool>\n";

static const char nodeDeviceDef[] =
"<device>\n"
"  <parent>scsi_host1</parent>\n"
"  <capability type='scsi_host'>\n"
"    <capability type='fc_host'>\n"
"      <wwpn>1000000023452345</wwpn>\n"
"      <wwnn>2000000023452345</wwnn>\n"
"    </capability>\n"
"  </capability>\n"
"</device>\n";

typedef struct {
    int startEvents;
    int stopEvents;
    int defineEvents;
    int undefineEvents;
    int unexpectedEvents;
    int createdEvents;
    int deletedEvents;
} lifecycleEventCounter;

static void
lifecycleEventCounter_reset(lifecycleEventCounter *counter)
{
    counter->startEvents = 0;
    counter->stopEvents = 0;
    counter->defineEvents = 0;
    counter->undefineEvents = 0;
    counter->unexpectedEvents = 0;
    counter->createdEvents = 0;
    counter->deletedEvents = 0;
}

typedef struct {
    virConnectPtr conn;
    virNetworkPtr net;
    virStoragePoolPtr pool;
    virNodeDevicePtr dev;
} objecteventTest;


static int
domainLifecycleCb(virConnectPtr conn ATTRIBUTE_UNUSED,
                  virDomainPtr dom ATTRIBUTE_UNUSED,
                  int event,
                  int detail ATTRIBUTE_UNUSED,
                  void *opaque)
{
    lifecycleEventCounter *counter = opaque;

    switch (event) {
        case VIR_DOMAIN_EVENT_STARTED:
            counter->startEvents++;
            break;
        case VIR_DOMAIN_EVENT_STOPPED:
            counter->stopEvents++;
            break;
        case VIR_DOMAIN_EVENT_DEFINED:
            counter->defineEvents++;
            break;
        case VIR_DOMAIN_EVENT_UNDEFINED:
            counter->undefineEvents++;
            break;
        default:
            /* Ignore other events */
            break;
    }
    return 0;
}

static void
networkLifecycleCb(virConnectPtr conn ATTRIBUTE_UNUSED,
                   virNetworkPtr net ATTRIBUTE_UNUSED,
                   int event,
                   int detail ATTRIBUTE_UNUSED,
                   void* opaque)
{
    lifecycleEventCounter *counter = opaque;

    if (event == VIR_NETWORK_EVENT_STARTED)
        counter->startEvents++;
    else if (event == VIR_NETWORK_EVENT_STOPPED)
        counter->stopEvents++;
    else if (event == VIR_NETWORK_EVENT_DEFINED)
        counter->defineEvents++;
    else if (event == VIR_NETWORK_EVENT_UNDEFINED)
        counter->undefineEvents++;
}

static void
storagePoolLifecycleCb(virConnectPtr conn ATTRIBUTE_UNUSED,
                       virStoragePoolPtr pool ATTRIBUTE_UNUSED,
                       int event,
                       int detail ATTRIBUTE_UNUSED,
                       void* opaque)
{
    lifecycleEventCounter *counter = opaque;

    if (event == VIR_STORAGE_POOL_EVENT_STARTED)
        counter->startEvents++;
    else if (event == VIR_STORAGE_POOL_EVENT_STOPPED)
        counter->stopEvents++;
    else if (event == VIR_STORAGE_POOL_EVENT_DEFINED)
        counter->defineEvents++;
    else if (event == VIR_STORAGE_POOL_EVENT_UNDEFINED)
        counter->undefineEvents++;
    else if (event == VIR_STORAGE_POOL_EVENT_CREATED)
        counter->createdEvents++;
    else if (event == VIR_STORAGE_POOL_EVENT_DELETED)
        counter->deletedEvents++;
}

static void
storagePoolRefreshCb(virConnectPtr conn ATTRIBUTE_UNUSED,
                     virStoragePoolPtr pool ATTRIBUTE_UNUSED,
                     void* opaque)
{
    int *counter = opaque;

    (*counter)++;
}

static void
nodeDeviceLifecycleCb(virConnectPtr conn ATTRIBUTE_UNUSED,
                      virNodeDevicePtr dev ATTRIBUTE_UNUSED,
                      int event,
                      int detail ATTRIBUTE_UNUSED,
                      void* opaque)
{
    lifecycleEventCounter *counter = opaque;

    if (event == VIR_NODE_DEVICE_EVENT_CREATED)
        counter->createdEvents++;
    else if (event == VIR_NODE_DEVICE_EVENT_DELETED)
        counter->deletedEvents++;
}

static int
testDomainCreateXMLOld(const void *data)
{
    const objecteventTest *test = data;
    lifecycleEventCounter counter;
    virDomainPtr dom = NULL;
    int ret = -1;
    bool registered = false;

    lifecycleEventCounter_reset(&counter);

    if (virConnectDomainEventRegister(test->conn,
                                      domainLifecycleCb,
                                      &counter, NULL) != 0)
        goto cleanup;
    registered = true;
    dom = virDomainCreateXML(test->conn, domainDef, 0);

    if (dom == NULL || virEventRunDefaultImpl() < 0)
        goto cleanup;

    if (counter.startEvents != 1 || counter.unexpectedEvents > 0)
        goto cleanup;

    if (virConnectDomainEventDeregister(test->conn, domainLifecycleCb) != 0)
        goto cleanup;
    registered = false;
    ret = 0;

 cleanup:
    if (registered)
        virConnectDomainEventDeregister(test->conn, domainLifecycleCb);
    if (dom) {
        virDomainDestroy(dom);
        virDomainFree(dom);
    }

    return ret;
}

static int
testDomainCreateXMLNew(const void *data)
{
    const objecteventTest *test = data;
    lifecycleEventCounter counter;
    int eventId = VIR_DOMAIN_EVENT_ID_LIFECYCLE;
    virDomainPtr dom = NULL;
    int id;
    int ret = -1;

    lifecycleEventCounter_reset(&counter);

    id = virConnectDomainEventRegisterAny(test->conn, NULL, eventId,
                           VIR_DOMAIN_EVENT_CALLBACK(&domainLifecycleCb),
                           &counter, NULL);
    if (id < 0)
        goto cleanup;
    dom = virDomainCreateXML(test->conn, domainDef, 0);

    if (dom == NULL || virEventRunDefaultImpl() < 0)
        goto cleanup;

    if (counter.startEvents != 1 || counter.unexpectedEvents > 0)
        goto cleanup;

    if (virConnectDomainEventDeregisterAny(test->conn, id) != 0)
        goto cleanup;
    id = -1;
    ret = 0;

 cleanup:
    if (id >= 0)
        virConnectDomainEventDeregisterAny(test->conn, id);
    if (dom) {
        virDomainDestroy(dom);
        virDomainFree(dom);
    }

    return ret;
}

static int
testDomainCreateXMLMixed(const void *data)
{
    const objecteventTest *test = data;
    lifecycleEventCounter counter;
    virDomainPtr dom;
    int ret = -1;
    int id1 = -1;
    int id2 = -1;
    bool registered = false;

    lifecycleEventCounter_reset(&counter);

    /* Fun with mixing old and new API, also with global and
     * per-domain.  Handler should be fired three times, once for each
     * registration.  */
    dom = virDomainDefineXML(test->conn, domainDef);
    if (dom == NULL)
        goto cleanup;

    id1 = virConnectDomainEventRegisterAny(test->conn, dom,
                                           VIR_DOMAIN_EVENT_ID_LIFECYCLE,
                           VIR_DOMAIN_EVENT_CALLBACK(&domainLifecycleCb),
                                           &counter, NULL);
    if (id1 < 0)
        goto cleanup;
    if (virConnectDomainEventRegister(test->conn,
                                      domainLifecycleCb,
                                      &counter, NULL) != 0)
        goto cleanup;
    registered = true;
    id2 = virConnectDomainEventRegisterAny(test->conn, NULL,
                                           VIR_DOMAIN_EVENT_ID_LIFECYCLE,
                           VIR_DOMAIN_EVENT_CALLBACK(&domainLifecycleCb),
                                           &counter, NULL);
    if (id2 < 0)
        goto cleanup;

    virDomainUndefine(dom);
    virDomainDestroy(dom);
    virDomainFree(dom);

    dom = virDomainCreateXML(test->conn, domainDef, 0);
    if (dom == NULL || virEventRunDefaultImpl() < 0)
        goto cleanup;

    if (counter.startEvents != 3 || counter.unexpectedEvents > 0)
        goto cleanup;

    if (virConnectDomainEventDeregister(test->conn, domainLifecycleCb) != 0)
        goto cleanup;
    registered = false;
    if (virConnectDomainEventDeregisterAny(test->conn, id1) != 0)
        goto cleanup;
    id1 = -1;
    if (virConnectDomainEventDeregisterAny(test->conn, id2) != 0)
        goto cleanup;
    id2 = -1;
    ret = 0;

 cleanup:
    if (id1 >= 0)
        virConnectDomainEventDeregisterAny(test->conn, id1);
    if (id2 >= 0)
        virConnectDomainEventDeregisterAny(test->conn, id2);
    if (registered)
        virConnectDomainEventDeregister(test->conn, domainLifecycleCb);
    if (dom != NULL) {
        virDomainUndefine(dom);
        virDomainDestroy(dom);
        virDomainFree(dom);
    }

    return ret;
}


static int
testDomainDefine(const void *data)
{
    const objecteventTest *test = data;
    lifecycleEventCounter counter;
    int eventId = VIR_DOMAIN_EVENT_ID_LIFECYCLE;
    virDomainPtr dom = NULL;
    int id;
    int ret = 0;

    lifecycleEventCounter_reset(&counter);

    id = virConnectDomainEventRegisterAny(test->conn, NULL, eventId,
                           VIR_DOMAIN_EVENT_CALLBACK(&domainLifecycleCb),
                           &counter, NULL);

    /* Make sure the define event is triggered */
    dom = virDomainDefineXML(test->conn, domainDef);

    if (dom == NULL || virEventRunDefaultImpl() < 0) {
        ret = -1;
        goto cleanup;
    }

    if (counter.defineEvents != 1 || counter.unexpectedEvents > 0) {
        ret = -1;
        goto cleanup;
    }

    /* Make sure the undefine event is triggered */
    virDomainUndefine(dom);

    if (virEventRunDefaultImpl() < 0) {
        ret = -1;
        goto cleanup;
    }

    if (counter.undefineEvents != 1 || counter.unexpectedEvents > 0) {
        ret = -1;
        goto cleanup;
    }


 cleanup:
    virConnectDomainEventDeregisterAny(test->conn, id);
    if (dom != NULL)
        virDomainFree(dom);

    return ret;
}

static int
testDomainStartStopEvent(const void *data)
{
    const objecteventTest *test = data;
    lifecycleEventCounter counter;
    int eventId = VIR_DOMAIN_EVENT_ID_LIFECYCLE;
    int id;
    int ret = -1;
    virDomainPtr dom;
    virConnectPtr conn2 = NULL;
    virDomainPtr dom2 = NULL;

    lifecycleEventCounter_reset(&counter);

    dom = virDomainLookupByName(test->conn, "test");
    if (dom == NULL)
        return -1;

    id = virConnectDomainEventRegisterAny(test->conn, dom, eventId,
                           VIR_DOMAIN_EVENT_CALLBACK(&domainLifecycleCb),
                           &counter, NULL);

    /* Test domain is started */
    virDomainDestroy(dom);
    if (virDomainCreate(dom) < 0)
        goto cleanup;

    if (virEventRunDefaultImpl() < 0)
        goto cleanup;

    if (counter.startEvents != 1 || counter.stopEvents != 1 ||
            counter.unexpectedEvents > 0)
        goto cleanup;

    /* Repeat the test, but this time, trigger the events via an
     * alternate connection.  */
    if (!(conn2 = virConnectOpen("test:///default")))
        goto cleanup;
    if (!(dom2 = virDomainLookupByName(conn2, "test")))
        goto cleanup;

    if (virDomainDestroy(dom2) < 0)
        goto cleanup;
    if (virDomainCreate(dom2) < 0)
        goto cleanup;

    if (virEventRunDefaultImpl() < 0)
        goto cleanup;

    if (counter.startEvents != 2 || counter.stopEvents != 2 ||
            counter.unexpectedEvents > 0)
        goto cleanup;

    ret = 0;
 cleanup:
    virConnectDomainEventDeregisterAny(test->conn, id);
    virDomainFree(dom);
    if (dom2)
        virDomainFree(dom2);
    if (conn2)
        virConnectClose(conn2);

    return ret;
}


typedef struct {
    int count_boot_order;
    int count_os_boot;
    char *bootdeviceIdentifier;
    char *kernel;
    char *initrd;
    char *cmdline;
} bootConfiguration;


static void
bootConfigurationFree(bootConfiguration *conf)
{
    if (!conf)
        return;

    VIR_FREE(conf->bootdeviceIdentifier);
    VIR_FREE(conf->kernel);
    VIR_FREE(conf->initrd);
    VIR_FREE(conf->cmdline);
    VIR_FREE(conf);
}


static bool
bootConfigurationEqual(bootConfiguration *a,
                       bootConfiguration *b)
{
    if (!a || !b)
        return a == b;

    return a->count_boot_order == b->count_boot_order &&
        a->count_os_boot == b->count_os_boot &&
        STREQ_NULLABLE(a->bootdeviceIdentifier, b->bootdeviceIdentifier) &&
        STREQ_NULLABLE(a->kernel, b->kernel) &&
        STREQ_NULLABLE(a->initrd, b->initrd) &&
        STREQ_NULLABLE(a->cmdline, b->cmdline);
}


/* Caller must free() the returned value */
static bootConfiguration*
getBootConfiguration(virDomainPtr dom)
{
   bootConfiguration* ret;
   char *xml = NULL;
   xmlDocPtr doc = NULL;
   xmlXPathContextPtr ctxt = NULL;
   xmlNodePtr node = NULL;

   if (VIR_ALLOC(ret) < 0)
       return NULL;

    if (!(xml = virDomainGetXMLDesc(dom, 0)))
        goto error;

    if (!(doc = virXMLParseStringCtxt(xml, "(domain_definition)", &ctxt)))
        goto error;

    ret->kernel = virXPathString("string(./os/kernel[1])", ctxt);
    ret->initrd = virXPathString("string(./os/initrd[1])", ctxt);
    ret->cmdline = virXPathString("string(./os/cmdline[1])", ctxt);

    if (virXPathInt("count(./os/boot)", ctxt, &ret->count_boot_order) < 0)
        goto error;

    if ((virXPathInt("count(./devices/*/boot[@order='1'])", ctxt, &ret->count_boot_order) < 0))
        goto error;

    if (ret->count_boot_order > 0) {
        node = virXPathNode("./devices/*/boot[@order='1']/..", ctxt);
        if (!node)
            goto error;

        ctxt->node = node;

        /* As we're using a heuristic for setting the boot device do
         * the same here.
         *
         * Represents the XML node a disk? */
        ret->bootdeviceIdentifier = virXPathString("string(./target/@dev)", ctxt);

        /* Represents the XML node a network interface? (we only allow
         * MAC addresses as boot device identifier for the tests (at
         * least for the moment)) */
        if (!ret->bootdeviceIdentifier)
            ret->bootdeviceIdentifier = virXPathString("string(./mac/@address)", ctxt);
    } else {
        ret->bootdeviceIdentifier = NULL;
    }

 cleanup:
    xmlFreeDoc(doc);
    xmlXPathFreeContext(ctxt);
    VIR_FREE(xml);
    return ret;

 error:
    bootConfigurationFree(ret);
    ret = NULL;
    goto cleanup;
}


static int
verifyOriginalState(virDomainPtr dom, bootConfiguration *original_conf)
{
    bool ret;
    bootConfiguration *current_conf = getBootConfiguration(dom);

    if (!current_conf)
        return false;

    ret = bootConfigurationEqual(original_conf,
                                 current_conf);
    bootConfigurationFree(current_conf);
    return ret;
}


static int
verifyChanges(virDomainPtr dom,
              const char *bootdevice,
              const char *kernel,
              const char *initrd,
              const char *cmdline)
{
    int ret = -1;
    bootConfiguration *current_conf;

    if (!(current_conf = getBootConfiguration(dom)))
        goto cleanup;

    /* verify the new boot order */
    if (bootdevice) {
        if (STRNEQ_NULLABLE(current_conf->bootdeviceIdentifier, bootdevice))
            goto cleanup;

        if (current_conf->count_os_boot != 0)
            goto cleanup;

        if (current_conf->count_boot_order < 1)
            goto cleanup;
    }

    /* verify the other OS node changes */
    if ((kernel && virStringIsEmpty(kernel) && current_conf->kernel) ||
        (!virStringIsEmpty(kernel) && STRNEQ_NULLABLE(current_conf->kernel, kernel)))
        goto cleanup;

    if ((initrd && virStringIsEmpty(initrd) && current_conf->initrd) ||
        (!virStringIsEmpty(initrd) && STRNEQ_NULLABLE(current_conf->initrd, initrd)))
        goto cleanup;

    if ((cmdline && virStringIsEmpty(cmdline) && current_conf->cmdline) ||
        (!virStringIsEmpty(cmdline) && STRNEQ_NULLABLE(current_conf->cmdline, cmdline)))
        goto cleanup;

    ret = 0;
 cleanup:
    bootConfigurationFree(current_conf);
    return ret;
}


static int
testDomainCreateWithParamsHelper(virDomainPtr dom, lifecycleEventCounter *counter,
                                 bool failure_expected, const char *bootdevice,
                                 const char *kernel, const char *initrd,
                                 const char *cmdline, unsigned int flags, bootConfiguration *original_conf)
{
    int rc;
    int ret = -1;
    virTypedParameterPtr params = NULL;
    int nparams = 0;
    int maxparams = 0;

    lifecycleEventCounter_reset(counter);

    if (bootdevice)
        virTypedParamsAddFromString(&params,
                                    &nparams,
                                    &maxparams,
                                    VIR_DOMAIN_CREATE_PARM_DEVICE_IDENTIFIER,
                                    VIR_TYPED_PARAM_STRING,
                                    bootdevice);

    if (kernel)
        virTypedParamsAddFromString(&params,
                                    &nparams,
                                    &maxparams,
                                    VIR_DOMAIN_CREATE_PARM_KERNEL,
                                    VIR_TYPED_PARAM_STRING,
                                    kernel);

    if (initrd)
        virTypedParamsAddFromString(&params,
                                    &nparams,
                                    &maxparams,
                                    VIR_DOMAIN_CREATE_PARM_INITRD,
                                    VIR_TYPED_PARAM_STRING,
                                    initrd);

    if (cmdline)
        virTypedParamsAddFromString(&params,
                                    &nparams,
                                    &maxparams,
                                    VIR_DOMAIN_CREATE_PARM_CMDLINE,
                                    VIR_TYPED_PARAM_STRING,
                                    cmdline);

    rc = virDomainCreateWithParams(dom,
                                   params,
                                   nparams,
                                   flags);
    if (rc < 0) {
        if (failure_expected)
            ret = 0;
        goto cleanup;
    }

    if (virEventRunDefaultImpl() < 0)
        goto cleanup;

    if (counter->startEvents != 1 ||
        counter->stopEvents != 0)
        goto cleanup;

    if (verifyChanges(dom, bootdevice, kernel, initrd, cmdline) < 0)
        goto cleanup;

    if (virDomainDestroy(dom) < 0)
        goto cleanup;

    if (verifyOriginalState(dom, original_conf) < 0)
        goto cleanup;

    if (virEventRunDefaultImpl() < 0)
        goto cleanup;

    if (counter->startEvents != 1 ||
        counter->stopEvents != 1)
        goto cleanup;

    ret = 0;
 cleanup:
    virTypedParamsFree(params, nparams);
    return ret;
}


static int
testDomainCreateWithParams(const void *data)
{
    const objecteventTest *test = data;
    lifecycleEventCounter counter;
    int eventId = VIR_DOMAIN_EVENT_ID_LIFECYCLE;
    int id;
    int ret = -1;
    virDomainPtr dom;
    bootConfiguration *original_boot_conf = NULL;

    dom = virDomainLookupByName(test->conn, "test");
    if (!dom)
        return -1;

    /* First clean up, register for the life cycle events, and get the
     * original, persistent boot configuration of the domain */
    virDomainDestroy(dom);

    id = virConnectDomainEventRegisterAny(test->conn, dom, eventId,
                                          VIR_DOMAIN_EVENT_CALLBACK(&domainLifecycleCb),
                                          &counter, NULL);

    if (!(original_boot_conf = getBootConfiguration(dom)))
        goto cleanup;

    if (testDomainCreateWithParamsHelper(dom, &counter, true, "notAvailableBootDevice",
                                         NULL, NULL, NULL, 0, original_boot_conf) < 0)
        goto cleanup;
    if (testDomainCreateWithParamsHelper(dom, &counter, false, NULL, NULL, NULL,
                                         NULL, 0, original_boot_conf) < 0)
        goto cleanup;
    if (testDomainCreateWithParamsHelper(dom, &counter, false, NULL, "newKernel",
                                         NULL, NULL, 0, original_boot_conf) < 0)
        goto cleanup;
    if (testDomainCreateWithParamsHelper(dom, &counter, false, NULL, NULL, "newInitrd",
                                         NULL, 0, original_boot_conf) < 0)
        goto cleanup;
    if (testDomainCreateWithParamsHelper(dom, &counter, true, "notAvailableBootDevice",
                                         "newInitrd", NULL, NULL, 0, original_boot_conf) < 0)
        goto cleanup;
    if (testDomainCreateWithParamsHelper(dom, &counter, false, NULL, NULL, NULL, "newCmdline",
                                         0, original_boot_conf) < 0)
        goto cleanup;
    if (testDomainCreateWithParamsHelper(dom, &counter, false, NULL, "newKernel", "newInitrd",
                                         "newCmdline", 0, original_boot_conf) < 0)
        goto cleanup;
    if (testDomainCreateWithParamsHelper(dom, &counter, false, NULL, "", "", "", 0,
                                         original_boot_conf) < 0)
        goto cleanup;
    if (testDomainCreateWithParamsHelper(dom, &counter, false, "vda", NULL, NULL, NULL,
                                         0, original_boot_conf) < 0)
        goto cleanup;
    if (testDomainCreateWithParamsHelper(dom, &counter, false, "vda", NULL, "blaa", "bla",
                                         0, original_boot_conf) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    bootConfigurationFree(original_boot_conf);
    virConnectDomainEventDeregisterAny(test->conn, id);
    virDomainFree(dom);

    return ret;
}


static int
testNetworkCreateXML(const void *data)
{
    const objecteventTest *test = data;
    lifecycleEventCounter counter;
    virNetworkPtr net;
    int id;
    int ret = 0;

    lifecycleEventCounter_reset(&counter);

    id = virConnectNetworkEventRegisterAny(test->conn, NULL,
                           VIR_NETWORK_EVENT_ID_LIFECYCLE,
                           VIR_NETWORK_EVENT_CALLBACK(&networkLifecycleCb),
                           &counter, NULL);
    net = virNetworkCreateXML(test->conn, networkDef);

    if (!net || virEventRunDefaultImpl() < 0) {
        ret = -1;
        goto cleanup;
    }

    if (counter.startEvents != 1 || counter.unexpectedEvents > 0) {
        ret = -1;
        goto cleanup;
    }

 cleanup:
    virConnectNetworkEventDeregisterAny(test->conn, id);
    if (net) {
        virNetworkDestroy(net);
        virNetworkFree(net);
    }
    return ret;
}

static int
testNetworkDefine(const void *data)
{
    const objecteventTest *test = data;
    lifecycleEventCounter counter;
    virNetworkPtr net;
    int id;
    int ret = 0;

    lifecycleEventCounter_reset(&counter);

    id = virConnectNetworkEventRegisterAny(test->conn, NULL,
                           VIR_NETWORK_EVENT_ID_LIFECYCLE,
                           VIR_NETWORK_EVENT_CALLBACK(&networkLifecycleCb),
                           &counter, NULL);

    /* Make sure the define event is triggered */
    net = virNetworkDefineXML(test->conn, networkDef);

    if (!net || virEventRunDefaultImpl() < 0) {
        ret = -1;
        goto cleanup;
    }

    if (counter.defineEvents != 1 || counter.unexpectedEvents > 0) {
        ret = -1;
        goto cleanup;
    }

    /* Make sure the undefine event is triggered */
    virNetworkUndefine(net);

    if (virEventRunDefaultImpl() < 0) {
        ret = -1;
        goto cleanup;
    }

    if (counter.undefineEvents != 1 || counter.unexpectedEvents > 0) {
        ret = -1;
        goto cleanup;
    }


 cleanup:
    virConnectNetworkEventDeregisterAny(test->conn, id);
    if (net)
        virNetworkFree(net);

    return ret;
}

static int
testNetworkStartStopEvent(const void *data)
{
    const objecteventTest *test = data;
    lifecycleEventCounter counter;
    int id;
    int ret = 0;

    if (!test->net)
        return -1;

    lifecycleEventCounter_reset(&counter);

    id = virConnectNetworkEventRegisterAny(test->conn, test->net,
                           VIR_NETWORK_EVENT_ID_LIFECYCLE,
                           VIR_NETWORK_EVENT_CALLBACK(&networkLifecycleCb),
                           &counter, NULL);
    virNetworkCreate(test->net);
    virNetworkDestroy(test->net);

    if (virEventRunDefaultImpl() < 0) {
        ret = -1;
        goto cleanup;
    }

    if (counter.startEvents != 1 || counter.stopEvents != 1 ||
        counter.unexpectedEvents > 0) {
        ret = -1;
        goto cleanup;
    }
 cleanup:
    virConnectNetworkEventDeregisterAny(test->conn, id);

    return ret;
}

static int
testStoragePoolCreateXML(const void *data)
{
    const objecteventTest *test = data;
    lifecycleEventCounter counter;
    virStoragePoolPtr pool;
    int id;
    int ret = 0;

    lifecycleEventCounter_reset(&counter);

    id = virConnectStoragePoolEventRegisterAny(test->conn, NULL,
                      VIR_STORAGE_POOL_EVENT_ID_LIFECYCLE,
                      VIR_STORAGE_POOL_EVENT_CALLBACK(&storagePoolLifecycleCb),
                      &counter, NULL);
    pool = virStoragePoolCreateXML(test->conn, storagePoolDef, 0);

    if (!pool || virEventRunDefaultImpl() < 0) {
        ret = -1;
        goto cleanup;
    }

    if (counter.startEvents != 1 || counter.unexpectedEvents > 0) {
        ret = -1;
        goto cleanup;
    }

 cleanup:
    virConnectStoragePoolEventDeregisterAny(test->conn, id);
    if (pool) {
        virStoragePoolDestroy(pool);
        virStoragePoolFree(pool);
    }
    return ret;
}

static int
testStoragePoolDefine(const void *data)
{
    const objecteventTest *test = data;
    lifecycleEventCounter counter;
    virStoragePoolPtr pool;
    int id;
    int ret = 0;

    lifecycleEventCounter_reset(&counter);

    id = virConnectStoragePoolEventRegisterAny(test->conn, NULL,
                      VIR_STORAGE_POOL_EVENT_ID_LIFECYCLE,
                      VIR_STORAGE_POOL_EVENT_CALLBACK(&storagePoolLifecycleCb),
                      &counter, NULL);

    /* Make sure the define event is triggered */
    pool = virStoragePoolDefineXML(test->conn, storagePoolDef, 0);

    if (!pool || virEventRunDefaultImpl() < 0) {
        ret = -1;
        goto cleanup;
    }

    if (counter.defineEvents != 1 || counter.unexpectedEvents > 0) {
        ret = -1;
        goto cleanup;
    }

    /* Make sure the undefine event is triggered */
    virStoragePoolUndefine(pool);

    if (virEventRunDefaultImpl() < 0) {
        ret = -1;
        goto cleanup;
    }

    if (counter.undefineEvents != 1 || counter.unexpectedEvents > 0) {
        ret = -1;
        goto cleanup;
    }


 cleanup:
    virConnectStoragePoolEventDeregisterAny(test->conn, id);
    if (pool)
        virStoragePoolFree(pool);

    return ret;
}

static int
testStoragePoolStartStopEvent(const void *data)
{
    const objecteventTest *test = data;
    lifecycleEventCounter counter;
    int refreshCounter;
    int id1, id2;
    int ret = 0;

    if (!test->pool)
        return -1;

    lifecycleEventCounter_reset(&counter);
    refreshCounter = 0;

    id1 = virConnectStoragePoolEventRegisterAny(test->conn, test->pool,
                      VIR_STORAGE_POOL_EVENT_ID_LIFECYCLE,
                      VIR_STORAGE_POOL_EVENT_CALLBACK(&storagePoolLifecycleCb),
                      &counter, NULL);
    id2 = virConnectStoragePoolEventRegisterAny(test->conn, test->pool,
                      VIR_STORAGE_POOL_EVENT_ID_REFRESH,
                      VIR_STORAGE_POOL_EVENT_CALLBACK(&storagePoolRefreshCb),
                      &refreshCounter, NULL);
    virStoragePoolCreate(test->pool, 0);
    virStoragePoolRefresh(test->pool, 0);
    virStoragePoolDestroy(test->pool);

    if (virEventRunDefaultImpl() < 0) {
        ret = -1;
        goto cleanup;
    }

    if (counter.startEvents != 1 || counter.stopEvents != 1 ||
        refreshCounter != 1 || counter.unexpectedEvents > 0) {
        ret = -1;
        goto cleanup;
    }

 cleanup:
    virConnectStoragePoolEventDeregisterAny(test->conn, id1);
    virConnectStoragePoolEventDeregisterAny(test->conn, id2);
    return ret;
}

static int
testStoragePoolBuild(const void *data)
{
    const objecteventTest *test = data;
    lifecycleEventCounter counter;
    int id;
    int ret = 0;

    lifecycleEventCounter_reset(&counter);

    id = virConnectStoragePoolEventRegisterAny(test->conn, NULL,
                      VIR_STORAGE_POOL_EVENT_ID_LIFECYCLE,
                      VIR_STORAGE_POOL_EVENT_CALLBACK(&storagePoolLifecycleCb),
                      &counter, NULL);

    virStoragePoolBuild(test->pool, 0);

    if (virEventRunDefaultImpl() < 0) {
        ret = -1;
        goto cleanup;
    }

    if (counter.createdEvents != 1) {
        ret = -1;
        goto cleanup;
    }

 cleanup:
    virConnectStoragePoolEventDeregisterAny(test->conn, id);
    return ret;
}

static int
testStoragePoolDelete(const void *data)
{
    const objecteventTest *test = data;
    lifecycleEventCounter counter;
    int id;
    int ret = 0;

    lifecycleEventCounter_reset(&counter);

    id = virConnectStoragePoolEventRegisterAny(test->conn, NULL,
                      VIR_STORAGE_POOL_EVENT_ID_LIFECYCLE,
                      VIR_STORAGE_POOL_EVENT_CALLBACK(&storagePoolLifecycleCb),
                      &counter, NULL);

    virStoragePoolDelete(test->pool, 0);

    if (virEventRunDefaultImpl() < 0) {
        ret = -1;
        goto cleanup;
    }

    if (counter.deletedEvents != 1) {
        ret = -1;
        goto cleanup;
    }

 cleanup:
    virConnectStoragePoolEventDeregisterAny(test->conn, id);
    return ret;
}
static int
testNodeDeviceCreateXML(const void *data)
{
    const objecteventTest *test = data;
    lifecycleEventCounter counter;
    virNodeDevicePtr dev;
    int id;
    int ret = 0;

    lifecycleEventCounter_reset(&counter);

    id = virConnectNodeDeviceEventRegisterAny(test->conn, NULL,
                        VIR_NODE_DEVICE_EVENT_ID_LIFECYCLE,
                        VIR_NODE_DEVICE_EVENT_CALLBACK(&nodeDeviceLifecycleCb),
                        &counter, NULL);
    dev = virNodeDeviceCreateXML(test->conn, nodeDeviceDef, 0);
    virNodeDeviceDestroy(dev);

    if (!dev || virEventRunDefaultImpl() < 0) {
        ret = -1;
        goto cleanup;
    }

    if (counter.createdEvents != 1 || counter.deletedEvents != 1 ||
        counter.unexpectedEvents > 0) {
        ret = -1;
        goto cleanup;
    }

 cleanup:
    virConnectNodeDeviceEventDeregisterAny(test->conn, id);
    if (dev)
        virNodeDeviceFree(dev);
    return ret;
}

static void
timeout(int id ATTRIBUTE_UNUSED, void *opaque ATTRIBUTE_UNUSED)
{
    fputs("test taking too long; giving up", stderr);
    _exit(EXIT_FAILURE);
}

static int
mymain(void)
{
    objecteventTest test;
    int ret = EXIT_SUCCESS;
    int timer;

    virEventRegisterDefaultImpl();

    /* Set up a timer to abort this test if it takes 10 seconds.  */
    if ((timer = virEventAddTimeout(10 * 1000, timeout, NULL, NULL)) < 0)
        return EXIT_FAILURE;

    if (!(test.conn = virConnectOpen("test:///default")))
        return EXIT_FAILURE;

    virTestQuiesceLibvirtErrors(false);

    /* Domain event tests */
    if (virTestRun("Domain createXML start event (old API)",
                   testDomainCreateXMLOld, &test) < 0)
        ret = EXIT_FAILURE;
    if (virTestRun("Domain createXML start event (new API)",
                   testDomainCreateXMLNew, &test) < 0)
        ret = EXIT_FAILURE;
    if (virTestRun("Domain createXML start event (both API)",
                   testDomainCreateXMLMixed, &test) < 0)
        ret = EXIT_FAILURE;
    if (virTestRun("Domain (un)define events", testDomainDefine, &test) < 0)
        ret = EXIT_FAILURE;
    if (virTestRun("Domain start stop events", testDomainStartStopEvent, &test) < 0)
        ret = EXIT_FAILURE;
    if (virTestRun("Domain start stop events with params", testDomainCreateWithParams, &test) < 0)
        ret = EXIT_FAILURE;

    /* Network event tests */
    /* Tests requiring the test network not to be set up*/
    if (virTestRun("Network createXML start event ", testNetworkCreateXML, &test) < 0)
        ret = EXIT_FAILURE;
    if (virTestRun("Network (un)define events", testNetworkDefine, &test) < 0)
        ret = EXIT_FAILURE;

    /* Define a test network */
    if (!(test.net = virNetworkDefineXML(test.conn, networkDef)))
        ret = EXIT_FAILURE;
    if (virTestRun("Network start stop events ", testNetworkStartStopEvent, &test) < 0)
        ret = EXIT_FAILURE;

    /* Cleanup */
    if (test.net) {
        virNetworkUndefine(test.net);
        virNetworkFree(test.net);
    }

    /* Storage pool event tests */
    if (virTestRun("Storage pool createXML start event ",
                   testStoragePoolCreateXML, &test) < 0)
        ret = EXIT_FAILURE;
    if (virTestRun("Storage pool (un)define events",
                   testStoragePoolDefine, &test) < 0)
        ret = EXIT_FAILURE;

    /* Define a test storage pool */
    if (!(test.pool = virStoragePoolDefineXML(test.conn, storagePoolDef, 0)))
        ret = EXIT_FAILURE;
    if (virTestRun("Storage pool start stop events ",
                   testStoragePoolStartStopEvent, &test) < 0)
        ret = EXIT_FAILURE;
    /* Storage pool build and delete events */
    if (virTestRun("Storage pool build event ",
                   testStoragePoolBuild, &test) < 0)
        ret = EXIT_FAILURE;
    if (virTestRun("Storage pool delete event ",
                   testStoragePoolDelete, &test) < 0)
        ret = EXIT_FAILURE;

    /* Node device event tests */
    if (virTestRun("Node device createXML add event ",
                   testNodeDeviceCreateXML, &test) < 0)
        ret = EXIT_FAILURE;

    /* Cleanup */
    if (test.pool) {
        virStoragePoolUndefine(test.pool);
        virStoragePoolFree(test.pool);
    }

    virConnectClose(test.conn);
    virEventRemoveTimeout(timer);

    return ret;
}

VIR_TEST_MAIN(mymain)
