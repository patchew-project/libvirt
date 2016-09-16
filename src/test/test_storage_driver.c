/*
 * test_storage_driver.c: A "mock" hypervisor for use by application unit tests
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

#include "test_storage_driver.h"

#include "test_private_driver.h"

static int
testConnectNumOfStoragePools(virConnectPtr conn)
{
    testDriverPtr privconn = conn->privateData;
    int numActive = 0;
    size_t i;

    testDriverLock(privconn);
    for (i = 0; i < privconn->pools.count; i++)
        if (virStoragePoolObjIsActive(privconn->pools.objs[i]))
            numActive++;
    testDriverUnlock(privconn);

    return numActive;
}

static int
testConnectListStoragePools(virConnectPtr conn,
                            char **const names,
                            int nnames)
{
    testDriverPtr privconn = conn->privateData;
    int n = 0;
    size_t i;

    testDriverLock(privconn);
    memset(names, 0, sizeof(*names)*nnames);
    for (i = 0; i < privconn->pools.count && n < nnames; i++) {
        virStoragePoolObjLock(privconn->pools.objs[i]);
        if (virStoragePoolObjIsActive(privconn->pools.objs[i]) &&
            VIR_STRDUP(names[n++], privconn->pools.objs[i]->def->name) < 0) {
            virStoragePoolObjUnlock(privconn->pools.objs[i]);
            goto error;
        }
        virStoragePoolObjUnlock(privconn->pools.objs[i]);
    }
    testDriverUnlock(privconn);

    return n;

 error:
    for (n = 0; n < nnames; n++)
        VIR_FREE(names[n]);
    testDriverUnlock(privconn);
    return -1;
}

static int
testConnectNumOfDefinedStoragePools(virConnectPtr conn)
{
    testDriverPtr privconn = conn->privateData;
    int numInactive = 0;
    size_t i;

    testDriverLock(privconn);
    for (i = 0; i < privconn->pools.count; i++) {
        virStoragePoolObjLock(privconn->pools.objs[i]);
        if (!virStoragePoolObjIsActive(privconn->pools.objs[i]))
            numInactive++;
        virStoragePoolObjUnlock(privconn->pools.objs[i]);
    }
    testDriverUnlock(privconn);

    return numInactive;
}

static int
testConnectListDefinedStoragePools(virConnectPtr conn,
                                   char **const names,
                                   int nnames)
{
    testDriverPtr privconn = conn->privateData;
    int n = 0;
    size_t i;

    testDriverLock(privconn);
    memset(names, 0, sizeof(*names)*nnames);
    for (i = 0; i < privconn->pools.count && n < nnames; i++) {
        virStoragePoolObjLock(privconn->pools.objs[i]);
        if (!virStoragePoolObjIsActive(privconn->pools.objs[i]) &&
            VIR_STRDUP(names[n++], privconn->pools.objs[i]->def->name) < 0) {
            virStoragePoolObjUnlock(privconn->pools.objs[i]);
            goto error;
        }
        virStoragePoolObjUnlock(privconn->pools.objs[i]);
    }
    testDriverUnlock(privconn);

    return n;

 error:
    for (n = 0; n < nnames; n++)
        VIR_FREE(names[n]);
    testDriverUnlock(privconn);
    return -1;
}

static int
testConnectListAllStoragePools(virConnectPtr conn,
                               virStoragePoolPtr **pools,
                               unsigned int flags)
{
    testDriverPtr privconn = conn->privateData;
    int ret = -1;

    virCheckFlags(VIR_CONNECT_LIST_STORAGE_POOLS_FILTERS_ALL, -1);

    testDriverLock(privconn);
    ret = virStoragePoolObjListExport(conn, privconn->pools, pools,
                                      NULL, flags);
    testDriverUnlock(privconn);

    return ret;
}

static const char *defaultPoolSourcesLogicalXML =
"<sources>\n"
"  <source>\n"
"    <device path='/dev/sda20'/>\n"
"    <name>testvg1</name>\n"
"    <format type='lvm2'/>\n"
"  </source>\n"
"  <source>\n"
"    <device path='/dev/sda21'/>\n"
"    <name>testvg2</name>\n"
"    <format type='lvm2'/>\n"
"  </source>\n"
"</sources>\n";

static const char *defaultPoolSourcesNetFSXML =
"<sources>\n"
"  <source>\n"
"    <host name='%s'/>\n"
"    <dir path='/testshare'/>\n"
"    <format type='nfs'/>\n"
"  </source>\n"
"</sources>\n";

static char *
testConnectFindStoragePoolSources(virConnectPtr conn ATTRIBUTE_UNUSED,
                                  const char *type,
                                  const char *srcSpec,
                                  unsigned int flags)
{
    virStoragePoolSourcePtr source = NULL;
    int pool_type;
    char *ret = NULL;

    virCheckFlags(0, NULL);

    pool_type = virStoragePoolTypeFromString(type);
    if (!pool_type) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("unknown storage pool type %s"), type);
        goto cleanup;
    }

    if (srcSpec) {
        source = virStoragePoolDefParseSourceString(srcSpec, pool_type);
        if (!source)
            goto cleanup;
    }

    switch (pool_type) {

    case VIR_STORAGE_POOL_LOGICAL:
        ignore_value(VIR_STRDUP(ret, defaultPoolSourcesLogicalXML));
        break;

    case VIR_STORAGE_POOL_NETFS:
        if (!source || !source->hosts[0].name) {
            virReportError(VIR_ERR_INVALID_ARG,
                           "%s", _("hostname must be specified for netfs sources"));
            goto cleanup;
        }

        ignore_value(virAsprintf(&ret, defaultPoolSourcesNetFSXML,
                                 source->hosts[0].name));
        break;

    default:
        virReportError(VIR_ERR_NO_SUPPORT,
                       _("pool type '%s' does not support source discovery"), type);
    }

 cleanup:
    virStoragePoolSourceFree(source);
    return ret;
}

static int
testConnectStoragePoolEventRegisterAny(virConnectPtr conn,
                                       virStoragePoolPtr pool,
                                       int eventID,
                                       virConnectStoragePoolEventGenericCallback callback,
                                       void *opaque,
                                       virFreeCallback freecb)
{
    testDriverPtr driver = conn->privateData;
    int ret;

    if (virStoragePoolEventStateRegisterID(conn, driver->eventState,
                                           pool, eventID, callback,
                                           opaque, freecb, &ret) < 0)
        ret = -1;

    return ret;
}

static int
testConnectStoragePoolEventDeregisterAny(virConnectPtr conn,
                                         int callbackID)
{
    testDriverPtr driver = conn->privateData;
    int ret = 0;

    if (virObjectEventStateDeregisterID(conn, driver->eventState,
                                        callbackID) < 0)
        ret = -1;

    return ret;
}

static virStoragePoolPtr
testStoragePoolLookupByName(virConnectPtr conn,
                            const char *name)
{
    testDriverPtr privconn = conn->privateData;
    virStoragePoolObjPtr pool;
    virStoragePoolPtr ret = NULL;

    testDriverLock(privconn);
    pool = virStoragePoolObjFindByName(&privconn->pools, name);
    testDriverUnlock(privconn);

    if (pool == NULL) {
        virReportError(VIR_ERR_NO_STORAGE_POOL, NULL);
        goto cleanup;
    }

    ret = virGetStoragePool(conn, pool->def->name, pool->def->uuid,
                            NULL, NULL);

 cleanup:
    if (pool)
        virStoragePoolObjUnlock(pool);
    return ret;
}

static virStoragePoolPtr
testStoragePoolLookupByVolume(virStorageVolPtr vol)
{
    return testStoragePoolLookupByName(vol->conn, vol->pool);
}

static virStoragePoolPtr
testStoragePoolLookupByUUID(virConnectPtr conn,
                            const unsigned char *uuid)
{
    testDriverPtr privconn = conn->privateData;
    virStoragePoolObjPtr pool;
    virStoragePoolPtr ret = NULL;

    testDriverLock(privconn);
    pool = virStoragePoolObjFindByUUID(&privconn->pools, uuid);
    testDriverUnlock(privconn);

    if (pool == NULL) {
        virReportError(VIR_ERR_NO_STORAGE_POOL, NULL);
        goto cleanup;
    }

    ret = virGetStoragePool(conn, pool->def->name, pool->def->uuid,
                            NULL, NULL);

 cleanup:
    if (pool)
        virStoragePoolObjUnlock(pool);
    return ret;
}

static virStoragePoolPtr
testStoragePoolCreateXML(virConnectPtr conn,
                         const char *xml,
                         unsigned int flags)
{
    testDriverPtr privconn = conn->privateData;
    virStoragePoolDefPtr def;
    virStoragePoolObjPtr pool = NULL;
    virStoragePoolPtr ret = NULL;
    virObjectEventPtr event = NULL;

    virCheckFlags(0, NULL);

    testDriverLock(privconn);
    if (!(def = virStoragePoolDefParseString(xml)))
        goto cleanup;

    pool = virStoragePoolObjFindByUUID(&privconn->pools, def->uuid);
    if (!pool)
        pool = virStoragePoolObjFindByName(&privconn->pools, def->name);
    if (pool) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("storage pool already exists"));
        goto cleanup;
    }

    if (!(pool = virStoragePoolObjAssignDef(&privconn->pools, def)))
        goto cleanup;
    def = NULL;

    if (testStoragePoolObjSetDefaults(pool) == -1) {
        virStoragePoolObjRemove(&privconn->pools, pool);
        pool = NULL;
        goto cleanup;
    }
    pool->active = 1;

    event = virStoragePoolEventLifecycleNew(pool->def->name, pool->def->uuid,
                                            VIR_STORAGE_POOL_EVENT_STARTED,
                                            0);

    ret = virGetStoragePool(conn, pool->def->name, pool->def->uuid,
                            NULL, NULL);

 cleanup:
    virStoragePoolDefFree(def);
    testObjectEventQueue(privconn, event);
    if (pool)
        virStoragePoolObjUnlock(pool);
    testDriverUnlock(privconn);
    return ret;
}

static virStoragePoolPtr
testStoragePoolDefineXML(virConnectPtr conn,
                         const char *xml,
                         unsigned int flags)
{
    testDriverPtr privconn = conn->privateData;
    virStoragePoolDefPtr def;
    virStoragePoolObjPtr pool = NULL;
    virStoragePoolPtr ret = NULL;
    virObjectEventPtr event = NULL;

    virCheckFlags(0, NULL);

    testDriverLock(privconn);
    if (!(def = virStoragePoolDefParseString(xml)))
        goto cleanup;

    def->capacity = defaultPoolCap;
    def->allocation = defaultPoolAlloc;
    def->available = defaultPoolCap - defaultPoolAlloc;

    if (!(pool = virStoragePoolObjAssignDef(&privconn->pools, def)))
        goto cleanup;
    def = NULL;

    event = virStoragePoolEventLifecycleNew(pool->def->name, pool->def->uuid,
                                            VIR_STORAGE_POOL_EVENT_DEFINED,
                                            0);

    if (testStoragePoolObjSetDefaults(pool) == -1) {
        virStoragePoolObjRemove(&privconn->pools, pool);
        pool = NULL;
        goto cleanup;
    }

    ret = virGetStoragePool(conn, pool->def->name, pool->def->uuid,
                            NULL, NULL);

 cleanup:
    virStoragePoolDefFree(def);
    testObjectEventQueue(privconn, event);
    if (pool)
        virStoragePoolObjUnlock(pool);
    testDriverUnlock(privconn);
    return ret;
}

static int
testStoragePoolUndefine(virStoragePoolPtr pool)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr privpool;
    int ret = -1;
    virObjectEventPtr event = NULL;

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           pool->name);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    if (virStoragePoolObjIsActive(privpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is already active"), pool->name);
        goto cleanup;
    }

    event = virStoragePoolEventLifecycleNew(pool->name, pool->uuid,
                                            VIR_STORAGE_POOL_EVENT_UNDEFINED,
                                            0);

    virStoragePoolObjRemove(&privconn->pools, privpool);
    privpool = NULL;
    ret = 0;

 cleanup:
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    testObjectEventQueue(privconn, event);
    testDriverUnlock(privconn);
    return ret;
}

static int
testStoragePoolBuild(virStoragePoolPtr pool,
                     unsigned int flags)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr privpool;
    int ret = -1;

    virCheckFlags(0, -1);

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           pool->name);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    if (virStoragePoolObjIsActive(privpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is already active"), pool->name);
        goto cleanup;
    }
    ret = 0;

 cleanup:
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

static int
testStoragePoolDestroy(virStoragePoolPtr pool)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr privpool;
    int ret = -1;
    virObjectEventPtr event = NULL;

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           pool->name);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    if (!virStoragePoolObjIsActive(privpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), pool->name);
        goto cleanup;
    }

    privpool->active = 0;
    event = virStoragePoolEventLifecycleNew(privpool->def->name, privpool->def->uuid,
                                            VIR_STORAGE_POOL_EVENT_STOPPED,
                                            0);

    if (privpool->configFile == NULL) {
        virStoragePoolObjRemove(&privconn->pools, privpool);
        privpool = NULL;
    }
    ret = 0;

 cleanup:
    testObjectEventQueue(privconn, event);
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    testDriverUnlock(privconn);
    return ret;
}

static int
testStoragePoolDelete(virStoragePoolPtr pool,
                      unsigned int flags)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr privpool;
    int ret = -1;

    virCheckFlags(0, -1);

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           pool->name);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    if (virStoragePoolObjIsActive(privpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is already active"), pool->name);
        goto cleanup;
    }

    ret = 0;

 cleanup:
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

static int
testStoragePoolRefresh(virStoragePoolPtr pool,
                       unsigned int flags)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr privpool;
    int ret = -1;
    virObjectEventPtr event = NULL;

    virCheckFlags(0, -1);

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           pool->name);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    if (!virStoragePoolObjIsActive(privpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), pool->name);
        goto cleanup;
    }

    event = virStoragePoolEventRefreshNew(pool->name, pool->uuid);
    ret = 0;

 cleanup:
    testObjectEventQueue(privconn, event);
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

static int
testStoragePoolGetInfo(virStoragePoolPtr pool,
                       virStoragePoolInfoPtr info)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr privpool;
    int ret = -1;

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           pool->name);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    memset(info, 0, sizeof(virStoragePoolInfo));
    if (privpool->active)
        info->state = VIR_STORAGE_POOL_RUNNING;
    else
        info->state = VIR_STORAGE_POOL_INACTIVE;
    info->capacity = privpool->def->capacity;
    info->allocation = privpool->def->allocation;
    info->available = privpool->def->available;
    ret = 0;

 cleanup:
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

static char *
testStoragePoolGetXMLDesc(virStoragePoolPtr pool,
                          unsigned int flags)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr privpool;
    char *ret = NULL;

    virCheckFlags(0, NULL);

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           pool->name);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    ret = virStoragePoolDefFormat(privpool->def);

 cleanup:
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

static int
testStoragePoolGetAutostart(virStoragePoolPtr pool,
                            int *autostart)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr privpool;
    int ret = -1;

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           pool->name);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    if (!privpool->configFile) {
        *autostart = 0;
    } else {
        *autostart = privpool->autostart;
    }
    ret = 0;

 cleanup:
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

static int
testStoragePoolSetAutostart(virStoragePoolPtr pool,
                            int autostart)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr privpool;
    int ret = -1;

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           pool->name);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    if (!privpool->configFile) {
        virReportError(VIR_ERR_INVALID_ARG,
                       "%s", _("pool has no config file"));
        goto cleanup;
    }

    autostart = (autostart != 0);
    privpool->autostart = autostart;
    ret = 0;

 cleanup:
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

static int
testStoragePoolNumOfVolumes(virStoragePoolPtr pool)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr privpool;
    int ret = -1;

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           pool->name);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    if (!virStoragePoolObjIsActive(privpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), pool->name);
        goto cleanup;
    }

    ret = privpool->volumes.count;

 cleanup:
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

static int
testStoragePoolListVolumes(virStoragePoolPtr pool,
                           char **const names,
                           int maxnames)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr privpool;
    size_t i = 0;
    int n = 0;

    memset(names, 0, maxnames * sizeof(*names));

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           pool->name);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }


    if (!virStoragePoolObjIsActive(privpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), pool->name);
        goto cleanup;
    }

    for (i = 0; i < privpool->volumes.count && n < maxnames; i++) {
        if (VIR_STRDUP(names[n++], privpool->volumes.objs[i]->name) < 0)
            goto cleanup;
    }

    virStoragePoolObjUnlock(privpool);
    return n;

 cleanup:
    for (n = 0; n < maxnames; n++)
        VIR_FREE(names[i]);

    memset(names, 0, maxnames * sizeof(*names));
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return -1;
}

static int
testStoragePoolListAllVolumes(virStoragePoolPtr obj,
                              virStorageVolPtr **vols,
                              unsigned int flags)
{
    testDriverPtr privconn = obj->conn->privateData;
    virStoragePoolObjPtr pool;
    size_t i;
    virStorageVolPtr *tmp_vols = NULL;
    virStorageVolPtr vol = NULL;
    int nvols = 0;
    int ret = -1;

    virCheckFlags(0, -1);

    testDriverLock(privconn);
    pool = virStoragePoolObjFindByUUID(&privconn->pools, obj->uuid);
    testDriverUnlock(privconn);

    if (!pool) {
        virReportError(VIR_ERR_NO_STORAGE_POOL, "%s",
                       _("no storage pool with matching uuid"));
        goto cleanup;
    }

    if (!virStoragePoolObjIsActive(pool)) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("storage pool is not active"));
        goto cleanup;
    }

     /* Just returns the volumes count */
    if (!vols) {
        ret = pool->volumes.count;
        goto cleanup;
    }

    if (VIR_ALLOC_N(tmp_vols, pool->volumes.count + 1) < 0)
         goto cleanup;

    for (i = 0; i < pool->volumes.count; i++) {
        if (!(vol = virGetStorageVol(obj->conn, pool->def->name,
                                     pool->volumes.objs[i]->name,
                                     pool->volumes.objs[i]->key,
                                     NULL, NULL)))
            goto cleanup;
        tmp_vols[nvols++] = vol;
    }

    *vols = tmp_vols;
    tmp_vols = NULL;
    ret = nvols;

 cleanup:
    if (tmp_vols) {
        for (i = 0; i < nvols; i++)
            virObjectUnref(tmp_vols[i]);
        VIR_FREE(tmp_vols);
    }

    if (pool)
        virStoragePoolObjUnlock(pool);

    return ret;
}

static virStorageVolPtr
testStorageVolLookupByName(virStoragePoolPtr pool,
                           const char *name ATTRIBUTE_UNUSED)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr privpool;
    virStorageVolDefPtr privvol;
    virStorageVolPtr ret = NULL;

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           pool->name);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }


    if (!virStoragePoolObjIsActive(privpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), pool->name);
        goto cleanup;
    }

    privvol = virStorageVolDefFindByName(privpool, name);

    if (!privvol) {
        virReportError(VIR_ERR_NO_STORAGE_VOL,
                       _("no storage vol with matching name '%s'"), name);
        goto cleanup;
    }

    ret = virGetStorageVol(pool->conn, privpool->def->name,
                           privvol->name, privvol->key,
                           NULL, NULL);

 cleanup:
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

static virStorageVolPtr
testStorageVolLookupByKey(virConnectPtr conn,
                          const char *key)
{
    testDriverPtr privconn = conn->privateData;
    size_t i;
    virStorageVolPtr ret = NULL;

    testDriverLock(privconn);
    for (i = 0; i < privconn->pools.count; i++) {
        virStoragePoolObjLock(privconn->pools.objs[i]);
        if (virStoragePoolObjIsActive(privconn->pools.objs[i])) {
            virStorageVolDefPtr privvol =
                virStorageVolDefFindByKey(privconn->pools.objs[i], key);

            if (privvol) {
                ret = virGetStorageVol(conn,
                                       privconn->pools.objs[i]->def->name,
                                       privvol->name,
                                       privvol->key,
                                       NULL, NULL);
                virStoragePoolObjUnlock(privconn->pools.objs[i]);
                break;
            }
        }
        virStoragePoolObjUnlock(privconn->pools.objs[i]);
    }
    testDriverUnlock(privconn);

    if (!ret)
        virReportError(VIR_ERR_NO_STORAGE_VOL,
                       _("no storage vol with matching key '%s'"), key);

    return ret;
}

static virStorageVolPtr
testStorageVolLookupByPath(virConnectPtr conn,
                           const char *path)
{
    testDriverPtr privconn = conn->privateData;
    size_t i;
    virStorageVolPtr ret = NULL;

    testDriverLock(privconn);
    for (i = 0; i < privconn->pools.count; i++) {
        virStoragePoolObjLock(privconn->pools.objs[i]);
        if (virStoragePoolObjIsActive(privconn->pools.objs[i])) {
            virStorageVolDefPtr privvol =
                virStorageVolDefFindByPath(privconn->pools.objs[i], path);

            if (privvol) {
                ret = virGetStorageVol(conn,
                                       privconn->pools.objs[i]->def->name,
                                       privvol->name,
                                       privvol->key,
                                       NULL, NULL);
                virStoragePoolObjUnlock(privconn->pools.objs[i]);
                break;
            }
        }
        virStoragePoolObjUnlock(privconn->pools.objs[i]);
    }
    testDriverUnlock(privconn);

    if (!ret)
        virReportError(VIR_ERR_NO_STORAGE_VOL,
                       _("no storage vol with matching path '%s'"), path);

    return ret;
}

static virStorageVolPtr
testStorageVolCreateXML(virStoragePoolPtr pool,
                        const char *xmldesc,
                        unsigned int flags)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr privpool;
    virStorageVolDefPtr privvol = NULL;
    virStorageVolPtr ret = NULL;

    virCheckFlags(0, NULL);

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           pool->name);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    if (!virStoragePoolObjIsActive(privpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), pool->name);
        goto cleanup;
    }

    privvol = virStorageVolDefParseString(privpool->def, xmldesc, 0);
    if (privvol == NULL)
        goto cleanup;

    if (virStorageVolDefFindByName(privpool, privvol->name)) {
        virReportError(VIR_ERR_OPERATION_FAILED,
                       "%s", _("storage vol already exists"));
        goto cleanup;
    }

    /* Make sure enough space */
    if ((privpool->def->allocation + privvol->target.allocation) >
         privpool->def->capacity) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Not enough free space in pool for volume '%s'"),
                       privvol->name);
        goto cleanup;
    }

    if (virAsprintf(&privvol->target.path, "%s/%s",
                    privpool->def->target.path,
                    privvol->name) == -1)
        goto cleanup;

    if (VIR_STRDUP(privvol->key, privvol->target.path) < 0 ||
        VIR_APPEND_ELEMENT_COPY(privpool->volumes.objs,
                                privpool->volumes.count, privvol) < 0)
        goto cleanup;

    privpool->def->allocation += privvol->target.allocation;
    privpool->def->available = (privpool->def->capacity -
                                privpool->def->allocation);

    ret = virGetStorageVol(pool->conn, privpool->def->name,
                           privvol->name, privvol->key,
                           NULL, NULL);
    privvol = NULL;

 cleanup:
    virStorageVolDefFree(privvol);
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

static virStorageVolPtr
testStorageVolCreateXMLFrom(virStoragePoolPtr pool,
                            const char *xmldesc,
                            virStorageVolPtr clonevol,
                            unsigned int flags)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr privpool;
    virStorageVolDefPtr privvol = NULL, origvol = NULL;
    virStorageVolPtr ret = NULL;

    virCheckFlags(0, NULL);

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           pool->name);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    if (!virStoragePoolObjIsActive(privpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), pool->name);
        goto cleanup;
    }

    privvol = virStorageVolDefParseString(privpool->def, xmldesc, 0);
    if (privvol == NULL)
        goto cleanup;

    if (virStorageVolDefFindByName(privpool, privvol->name)) {
        virReportError(VIR_ERR_OPERATION_FAILED,
                       "%s", _("storage vol already exists"));
        goto cleanup;
    }

    origvol = virStorageVolDefFindByName(privpool, clonevol->name);
    if (!origvol) {
        virReportError(VIR_ERR_NO_STORAGE_VOL,
                       _("no storage vol with matching name '%s'"),
                       clonevol->name);
        goto cleanup;
    }

    /* Make sure enough space */
    if ((privpool->def->allocation + privvol->target.allocation) >
         privpool->def->capacity) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Not enough free space in pool for volume '%s'"),
                       privvol->name);
        goto cleanup;
    }
    privpool->def->available = (privpool->def->capacity -
                                privpool->def->allocation);

    if (virAsprintf(&privvol->target.path, "%s/%s",
                    privpool->def->target.path,
                    privvol->name) == -1)
        goto cleanup;

    if (VIR_STRDUP(privvol->key, privvol->target.path) < 0 ||
        VIR_APPEND_ELEMENT_COPY(privpool->volumes.objs,
                                privpool->volumes.count, privvol) < 0)
        goto cleanup;

    privpool->def->allocation += privvol->target.allocation;
    privpool->def->available = (privpool->def->capacity -
                                privpool->def->allocation);

    ret = virGetStorageVol(pool->conn, privpool->def->name,
                           privvol->name, privvol->key,
                           NULL, NULL);
    privvol = NULL;

 cleanup:
    virStorageVolDefFree(privvol);
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

static int
testStorageVolDelete(virStorageVolPtr vol,
                     unsigned int flags)
{
    testDriverPtr privconn = vol->conn->privateData;
    virStoragePoolObjPtr privpool;
    virStorageVolDefPtr privvol;
    size_t i;
    int ret = -1;

    virCheckFlags(0, -1);

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           vol->pool);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }


    privvol = virStorageVolDefFindByName(privpool, vol->name);

    if (privvol == NULL) {
        virReportError(VIR_ERR_NO_STORAGE_VOL,
                       _("no storage vol with matching name '%s'"),
                       vol->name);
        goto cleanup;
    }

    if (!virStoragePoolObjIsActive(privpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), vol->pool);
        goto cleanup;
    }


    privpool->def->allocation -= privvol->target.allocation;
    privpool->def->available = (privpool->def->capacity -
                                privpool->def->allocation);

    for (i = 0; i < privpool->volumes.count; i++) {
        if (privpool->volumes.objs[i] == privvol) {
            virStorageVolDefFree(privvol);

            VIR_DELETE_ELEMENT(privpool->volumes.objs, i, privpool->volumes.count);
            break;
        }
    }
    ret = 0;

 cleanup:
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

static int testStorageVolumeTypeForPool(int pooltype)
{

    switch (pooltype) {
        case VIR_STORAGE_POOL_DIR:
        case VIR_STORAGE_POOL_FS:
        case VIR_STORAGE_POOL_NETFS:
            return VIR_STORAGE_VOL_FILE;
        default:
            return VIR_STORAGE_VOL_BLOCK;
    }
}

static int
testStorageVolGetInfo(virStorageVolPtr vol,
                      virStorageVolInfoPtr info)
{
    testDriverPtr privconn = vol->conn->privateData;
    virStoragePoolObjPtr privpool;
    virStorageVolDefPtr privvol;
    int ret = -1;

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           vol->pool);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    privvol = virStorageVolDefFindByName(privpool, vol->name);

    if (privvol == NULL) {
        virReportError(VIR_ERR_NO_STORAGE_VOL,
                       _("no storage vol with matching name '%s'"),
                       vol->name);
        goto cleanup;
    }

    if (!virStoragePoolObjIsActive(privpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), vol->pool);
        goto cleanup;
    }

    memset(info, 0, sizeof(*info));
    info->type = testStorageVolumeTypeForPool(privpool->def->type);
    info->capacity = privvol->target.capacity;
    info->allocation = privvol->target.allocation;
    ret = 0;

 cleanup:
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

static char *
testStorageVolGetXMLDesc(virStorageVolPtr vol,
                         unsigned int flags)
{
    testDriverPtr privconn = vol->conn->privateData;
    virStoragePoolObjPtr privpool;
    virStorageVolDefPtr privvol;
    char *ret = NULL;

    virCheckFlags(0, NULL);

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           vol->pool);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    privvol = virStorageVolDefFindByName(privpool, vol->name);

    if (privvol == NULL) {
        virReportError(VIR_ERR_NO_STORAGE_VOL,
                       _("no storage vol with matching name '%s'"),
                       vol->name);
        goto cleanup;
    }

    if (!virStoragePoolObjIsActive(privpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), vol->pool);
        goto cleanup;
    }

    ret = virStorageVolDefFormat(privpool->def, privvol);

 cleanup:
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

static char *
testStorageVolGetPath(virStorageVolPtr vol)
{
    testDriverPtr privconn = vol->conn->privateData;
    virStoragePoolObjPtr privpool;
    virStorageVolDefPtr privvol;
    char *ret = NULL;

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           vol->pool);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    privvol = virStorageVolDefFindByName(privpool, vol->name);

    if (privvol == NULL) {
        virReportError(VIR_ERR_NO_STORAGE_VOL,
                       _("no storage vol with matching name '%s'"),
                       vol->name);
        goto cleanup;
    }

    if (!virStoragePoolObjIsActive(privpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), vol->pool);
        goto cleanup;
    }

    ignore_value(VIR_STRDUP(ret, privvol->target.path));

 cleanup:
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

int testStoragePoolObjSetDefaults(virStoragePoolObjPtr pool)
{

    pool->def->capacity = defaultPoolCap;
    pool->def->allocation = defaultPoolAlloc;
    pool->def->available = defaultPoolCap - defaultPoolAlloc;

    return VIR_STRDUP(pool->configFile, "");
}

static int testStoragePoolIsActive(virStoragePoolPtr pool)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr obj;
    int ret = -1;

    testDriverLock(privconn);
    obj = virStoragePoolObjFindByUUID(&privconn->pools, pool->uuid);
    testDriverUnlock(privconn);
    if (!obj) {
        virReportError(VIR_ERR_NO_STORAGE_POOL, NULL);
        goto cleanup;
    }
    ret = virStoragePoolObjIsActive(obj);

 cleanup:
    if (obj)
        virStoragePoolObjUnlock(obj);
    return ret;
}

static int testStoragePoolIsPersistent(virStoragePoolPtr pool)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr obj;
    int ret = -1;

    testDriverLock(privconn);
    obj = virStoragePoolObjFindByUUID(&privconn->pools, pool->uuid);
    testDriverUnlock(privconn);
    if (!obj) {
        virReportError(VIR_ERR_NO_STORAGE_POOL, NULL);
        goto cleanup;
    }
    ret = obj->configFile ? 1 : 0;

 cleanup:
    if (obj)
        virStoragePoolObjUnlock(obj);
    return ret;
}

static int
testStoragePoolCreate(virStoragePoolPtr pool,
                      unsigned int flags)
{
    testDriverPtr privconn = pool->conn->privateData;
    virStoragePoolObjPtr privpool;
    int ret = -1;
    virObjectEventPtr event = NULL;

    virCheckFlags(0, -1);

    testDriverLock(privconn);
    privpool = virStoragePoolObjFindByName(&privconn->pools,
                                           pool->name);
    testDriverUnlock(privconn);

    if (privpool == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, __FUNCTION__);
        goto cleanup;
    }

    if (virStoragePoolObjIsActive(privpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is already active"), pool->name);
        goto cleanup;
    }

    privpool->active = 1;

    event = virStoragePoolEventLifecycleNew(pool->name, pool->uuid,
                                            VIR_STORAGE_POOL_EVENT_STARTED,
                                            0);
    ret = 0;

 cleanup:
    testObjectEventQueue(privconn, event);
    if (privpool)
        virStoragePoolObjUnlock(privpool);
    return ret;
}

virStorageDriver testStorageDriver = {
    .connectNumOfStoragePools = testConnectNumOfStoragePools, /* 0.5.0 */
    .connectListStoragePools = testConnectListStoragePools, /* 0.5.0 */
    .connectNumOfDefinedStoragePools = testConnectNumOfDefinedStoragePools, /* 0.5.0 */
    .connectListDefinedStoragePools = testConnectListDefinedStoragePools, /* 0.5.0 */
    .connectListAllStoragePools = testConnectListAllStoragePools, /* 0.10.2 */
    .connectFindStoragePoolSources = testConnectFindStoragePoolSources, /* 0.5.0 */
    .connectStoragePoolEventRegisterAny = testConnectStoragePoolEventRegisterAny, /* 2.0.0 */
    .connectStoragePoolEventDeregisterAny = testConnectStoragePoolEventDeregisterAny, /* 2.0.0 */
    .storagePoolLookupByName = testStoragePoolLookupByName, /* 0.5.0 */
    .storagePoolLookupByUUID = testStoragePoolLookupByUUID, /* 0.5.0 */
    .storagePoolLookupByVolume = testStoragePoolLookupByVolume, /* 0.5.0 */
    .storagePoolCreateXML = testStoragePoolCreateXML, /* 0.5.0 */
    .storagePoolDefineXML = testStoragePoolDefineXML, /* 0.5.0 */
    .storagePoolBuild = testStoragePoolBuild, /* 0.5.0 */
    .storagePoolUndefine = testStoragePoolUndefine, /* 0.5.0 */
    .storagePoolCreate = testStoragePoolCreate, /* 0.5.0 */
    .storagePoolDestroy = testStoragePoolDestroy, /* 0.5.0 */
    .storagePoolDelete = testStoragePoolDelete, /* 0.5.0 */
    .storagePoolRefresh = testStoragePoolRefresh, /* 0.5.0 */
    .storagePoolGetInfo = testStoragePoolGetInfo, /* 0.5.0 */
    .storagePoolGetXMLDesc = testStoragePoolGetXMLDesc, /* 0.5.0 */
    .storagePoolGetAutostart = testStoragePoolGetAutostart, /* 0.5.0 */
    .storagePoolSetAutostart = testStoragePoolSetAutostart, /* 0.5.0 */
    .storagePoolNumOfVolumes = testStoragePoolNumOfVolumes, /* 0.5.0 */
    .storagePoolListVolumes = testStoragePoolListVolumes, /* 0.5.0 */
    .storagePoolListAllVolumes = testStoragePoolListAllVolumes, /* 0.10.2 */

    .storageVolLookupByName = testStorageVolLookupByName, /* 0.5.0 */
    .storageVolLookupByKey = testStorageVolLookupByKey, /* 0.5.0 */
    .storageVolLookupByPath = testStorageVolLookupByPath, /* 0.5.0 */
    .storageVolCreateXML = testStorageVolCreateXML, /* 0.5.0 */
    .storageVolCreateXMLFrom = testStorageVolCreateXMLFrom, /* 0.6.4 */
    .storageVolDelete = testStorageVolDelete, /* 0.5.0 */
    .storageVolGetInfo = testStorageVolGetInfo, /* 0.5.0 */
    .storageVolGetXMLDesc = testStorageVolGetXMLDesc, /* 0.5.0 */
    .storageVolGetPath = testStorageVolGetPath, /* 0.5.0 */
    .storagePoolIsActive = testStoragePoolIsActive, /* 0.7.3 */
    .storagePoolIsPersistent = testStoragePoolIsPersistent, /* 0.7.3 */
};
