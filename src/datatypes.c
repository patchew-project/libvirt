/*
 * datatypes.c: management of structs for public data types
 *
 * Copyright (C) 2006-2019 Red Hat, Inc.
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
 */

#include <config.h>
#include <unistd.h>

#include "datatypes.h"
#include "virerror.h"
#include "virlog.h"
#include "viralloc.h"
#include "viruuid.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("datatypes");

virClassPtr virConnectClass;
virClassPtr virConnectCloseCallbackDataClass;
virClassPtr virDomainClass;
virClassPtr virNodeDeviceClass;
virClassPtr virSecretClass;
virClassPtr virStreamClass;
virClassPtr virStorageVolClass;

static void virConnectDispose(void *obj);
static void virConnectCloseCallbackDataDispose(void *obj);
static void virDomainDispose(void *obj);
static void virNodeDeviceDispose(void *obj);
static void virSecretDispose(void *obj);
static void virStreamDispose(void *obj);
static void virStorageVolDispose(void *obj);

G_DEFINE_TYPE(virDomainCheckpoint, vir_domain_checkpoint, G_TYPE_OBJECT);
static void virDomainCheckpointFinalize(GObject *obj);

static void
vir_domain_checkpoint_init(virDomainCheckpoint *dc G_GNUC_UNUSED)
{
}

static void
vir_domain_checkpoint_class_init(virDomainCheckpointClass *klass)
{
    GObjectClass *obj = G_OBJECT_CLASS(klass);

    obj->finalize = virDomainCheckpointFinalize;
}

G_DEFINE_TYPE(virDomainSnapshot, vir_domain_snapshot, G_TYPE_OBJECT);
static void virDomainSnapshotFinalize(GObject *obj);

static void
vir_domain_snapshot_init(virDomainSnapshot *ds G_GNUC_UNUSED)
{
}

static void
vir_domain_snapshot_class_init(virDomainSnapshotClass *klass)
{
    GObjectClass *obj = G_OBJECT_CLASS(klass);

    obj->finalize = virDomainSnapshotFinalize;
}

G_DEFINE_TYPE(virInterface, vir_interface, G_TYPE_OBJECT);
static void virInterfaceFinalize(GObject *obj);

static void
vir_interface_init(virInterface *iface G_GNUC_UNUSED)
{
}

static void
vir_interface_class_init(virInterfaceClass *klass)
{
    GObjectClass *obj = G_OBJECT_CLASS(klass);

    obj->finalize = virInterfaceFinalize;
}

G_DEFINE_TYPE(virNetwork, vir_network, G_TYPE_OBJECT);
static void virNetworkFinalize(GObject *obj);

static void
vir_network_init(virNetwork *net G_GNUC_UNUSED)
{
}

static void
vir_network_class_init(virNetworkClass *klass)
{
    GObjectClass *obj = G_OBJECT_CLASS(klass);

    obj->finalize = virNetworkFinalize;
}

G_DEFINE_TYPE(virNetworkPort, vir_network_port, G_TYPE_OBJECT);
static void virNetworkPortFinalize(GObject *obj);

static void
vir_network_port_init(virNetworkPort *np G_GNUC_UNUSED)
{
}

static void
vir_network_port_class_init(virNetworkPortClass *klass)
{
    GObjectClass *obj = G_OBJECT_CLASS(klass);

    obj->finalize = virNetworkPortFinalize;
}

G_DEFINE_TYPE(virNWFilter, vir_nw_filter, G_TYPE_OBJECT);
static void virNWFilterFinalize(GObject *obj);

static void
vir_nw_filter_init(virNWFilter *filter G_GNUC_UNUSED)
{
}

static void
vir_nw_filter_class_init(virNWFilterClass *klass)
{
    GObjectClass *obj = G_OBJECT_CLASS(klass);

    obj->finalize = virNWFilterFinalize;
}

G_DEFINE_TYPE(virNWFilterBinding, vir_nw_filter_binding, G_TYPE_OBJECT);
static void virNWFilterBindingFinalize(GObject *obj);

static void
vir_nw_filter_binding_init(virNWFilterBinding *bdg G_GNUC_UNUSED)
{
}

static void
vir_nw_filter_binding_class_init(virNWFilterBindingClass *klass)
{
    GObjectClass *obj = G_OBJECT_CLASS(klass);

    obj->finalize = virNWFilterBindingFinalize;
}

G_DEFINE_TYPE(virStoragePool, vir_storage_pool, G_TYPE_OBJECT);
static void virStoragePoolFinalize(GObject *obj);

static void
vir_storage_pool_init(virStoragePool *pool G_GNUC_UNUSED)
{
}

static void
vir_storage_pool_class_init(virStoragePoolClass *klass)
{
    GObjectClass *obj = G_OBJECT_CLASS(klass);

    obj->finalize = virStoragePoolFinalize;
}

virClassPtr virAdmConnectClass;
virClassPtr virAdmConnectCloseCallbackDataClass;

static void virAdmConnectDispose(void *obj);
static void virAdmConnectCloseCallbackDataDispose(void *obj);

G_DEFINE_TYPE(virAdmClient, vir_adm_client, G_TYPE_OBJECT);
static void virAdmClientFinalize(GObject *obj);

static void
vir_adm_client_init(virAdmClient *clt G_GNUC_UNUSED)
{
}

static void
vir_adm_client_class_init(virAdmClientClass *klass)
{
    GObjectClass *obj = G_OBJECT_CLASS(klass);

    obj->finalize = virAdmClientFinalize;
}

G_DEFINE_TYPE(virAdmServer, vir_adm_server, G_TYPE_OBJECT);
static void virAdmServerFinalize(GObject *obj);

static void
vir_adm_server_init(virAdmServer *srv G_GNUC_UNUSED)
{
}

static void
vir_adm_server_class_init(virAdmServerClass *klass)
{
    GObjectClass *obj = G_OBJECT_CLASS(klass);

    obj->finalize = virAdmServerFinalize;
}

static int
virDataTypesOnceInit(void)
{
#define DECLARE_CLASS_COMMON(basename, parent) \
    if (!(VIR_CLASS_NEW(basename, parent))) \
        return -1;
#define DECLARE_CLASS(basename) \
    DECLARE_CLASS_COMMON(basename, virClassForObject())
#define DECLARE_CLASS_LOCKABLE(basename) \
    DECLARE_CLASS_COMMON(basename, virClassForObjectLockable())

    DECLARE_CLASS_LOCKABLE(virConnect);
    DECLARE_CLASS_LOCKABLE(virConnectCloseCallbackData);
    DECLARE_CLASS(virDomain);
    DECLARE_CLASS(virNodeDevice);
    DECLARE_CLASS(virSecret);
    DECLARE_CLASS(virStream);
    DECLARE_CLASS(virStorageVol);

    DECLARE_CLASS_LOCKABLE(virAdmConnect);
    DECLARE_CLASS_LOCKABLE(virAdmConnectCloseCallbackData);

#undef DECLARE_CLASS_COMMON
#undef DECLARE_CLASS_LOCKABLE
#undef DECLARE_CLASS

    return 0;
}

VIR_ONCE_GLOBAL_INIT(virDataTypes);

/**
 * virGetConnect:
 *
 * Allocates a new hypervisor connection object.
 *
 * Returns a pointer to the connection object, or NULL on error.
 */
virConnectPtr
virGetConnect(void)
{
    if (virDataTypesInitialize() < 0)
        return NULL;

    return virObjectLockableNew(virConnectClass);
}

/**
 * virConnectDispose:
 * @obj: the hypervisor connection to release
 *
 * Unconditionally release all memory associated with a connection.
 * The connection object must not be used once this method returns.
 */
static void
virConnectDispose(void *obj)
{
    virConnectPtr conn = obj;

    if (conn->driver)
        conn->driver->connectClose(conn);

    virResetError(&conn->err);

    virURIFree(conn->uri);
}


static void
virConnectCloseCallbackDataReset(virConnectCloseCallbackDataPtr closeData)
{
    if (closeData->freeCallback)
        closeData->freeCallback(closeData->opaque);

    closeData->freeCallback = NULL;
    closeData->opaque = NULL;
    virObjectUnref(closeData->conn);
    closeData->conn = NULL;
}

/**
 * virConnectCloseCallbackDataDispose:
 * @obj: the close callback data to release
 *
 * Release resources bound to the connection close callback.
 */
static void
virConnectCloseCallbackDataDispose(void *obj)
{
    virConnectCloseCallbackDataReset(obj);
}

virConnectCloseCallbackDataPtr
virNewConnectCloseCallbackData(void)
{
    if (virDataTypesInitialize() < 0)
        return NULL;

    return virObjectLockableNew(virConnectCloseCallbackDataClass);
}

void virConnectCloseCallbackDataRegister(virConnectCloseCallbackDataPtr closeData,
                                         virConnectPtr conn,
                                         virConnectCloseFunc cb,
                                         void *opaque,
                                         virFreeCallback freecb)
{
    virObjectLock(closeData);

    if (closeData->callback != NULL) {
        VIR_WARN("Attempt to register callback on armed"
                 " close callback object %p", closeData);
        goto cleanup;
    }

    closeData->conn = virObjectRef(conn);
    closeData->callback = cb;
    closeData->opaque = opaque;
    closeData->freeCallback = freecb;

 cleanup:

    virObjectUnlock(closeData);
}

void virConnectCloseCallbackDataUnregister(virConnectCloseCallbackDataPtr closeData,
                                           virConnectCloseFunc cb)
{
    virObjectLock(closeData);

    if (closeData->callback != cb) {
        VIR_WARN("Attempt to unregister different callback on "
                 " close callback object %p", closeData);
        goto cleanup;
    }

    virConnectCloseCallbackDataReset(closeData);
    closeData->callback = NULL;

 cleanup:

    virObjectUnlock(closeData);
}

void virConnectCloseCallbackDataCall(virConnectCloseCallbackDataPtr closeData,
                                     int reason)
{
    virObjectLock(closeData);

    if (!closeData->conn)
        goto exit;

    VIR_DEBUG("Triggering connection close callback %p reason=%d, opaque=%p",
              closeData->callback, reason, closeData->opaque);
    closeData->callback(closeData->conn, reason, closeData->opaque);

    virConnectCloseCallbackDataReset(closeData);

 exit:
    virObjectUnlock(closeData);
}

virConnectCloseFunc
virConnectCloseCallbackDataGetCallback(virConnectCloseCallbackDataPtr closeData)
{
    virConnectCloseFunc cb;

    virObjectLock(closeData);
    cb = closeData->callback;
    virObjectUnlock(closeData);

    return cb;
}

/**
 * virGetDomain:
 * @conn: the hypervisor connection
 * @name: pointer to the domain name
 * @uuid: pointer to the uuid
 * @id: domain ID
 *
 * Allocates a new domain object. When the object is no longer needed,
 * virObjectUnref() must be called in order to not leak data.
 *
 * Returns a pointer to the domain object, or NULL on error.
 */
virDomainPtr
virGetDomain(virConnectPtr conn,
             const char *name,
             const unsigned char *uuid,
             int id)
{
    virDomainPtr ret = NULL;

    if (virDataTypesInitialize() < 0)
        return NULL;

    virCheckConnectGoto(conn, error);
    virCheckNonNullArgGoto(name, error);
    virCheckNonNullArgGoto(uuid, error);

    if (!(ret = virObjectNew(virDomainClass)))
        goto error;

    ret->name = g_strdup(name);

    ret->conn = virObjectRef(conn);
    ret->id = id;
    memcpy(&(ret->uuid[0]), uuid, VIR_UUID_BUFLEN);

    return ret;

 error:
    virObjectUnref(ret);
    return NULL;
}

/**
 * virDomainDispose:
 * @obj: the domain to release
 *
 * Unconditionally release all memory associated with a domain.
 * The domain object must not be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virDomainDispose(void *obj)
{
    virDomainPtr domain = obj;
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(domain->uuid, uuidstr);
    VIR_DEBUG("release domain %p %s %s", domain, domain->name, uuidstr);

    VIR_FREE(domain->name);
    virObjectUnref(domain->conn);
}


/**
 * virGetNetwork:
 * @conn: the hypervisor connection
 * @name: pointer to the network name
 * @uuid: pointer to the uuid
 *
 * Allocates a new network object. When the object is no longer needed,
 * g_object_unref() must be called in order to not leak data.
 *
 * Returns a pointer to the network object, or NULL on error.
 */
virNetworkPtr
virGetNetwork(virConnectPtr conn, const char *name, const unsigned char *uuid)
{
    g_autoptr(virNetwork) ret = NULL;

    if (virDataTypesInitialize() < 0)
        return NULL;

    virCheckConnectReturn(conn, NULL);
    virCheckNonNullArgReturn(name, NULL);
    virCheckNonNullArgReturn(uuid, NULL);

    ret = VIR_NETWORK(g_object_new(VIR_TYPE_NETWORK, NULL));

    ret->name = g_strdup(name);

    ret->conn = virObjectRef(conn);
    memcpy(&(ret->uuid[0]), uuid, VIR_UUID_BUFLEN);

    return g_steal_pointer(&ret);
}

/**
 * virNetworkFinalize:
 * @obj: the network to release
 *
 * Unconditionally release all memory associated with a network.
 * The network object must not be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virNetworkFinalize(GObject *obj)
{
    virNetworkPtr network = VIR_NETWORK(obj);
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(network->uuid, uuidstr);
    VIR_DEBUG("release network %p %s %s", network, network->name, uuidstr);

    VIR_FREE(network->name);
    virObjectUnref(network->conn);

    G_OBJECT_CLASS(vir_network_parent_class)->finalize(obj);
}


/**
 * virGetNetworkPort:
 * @net: the network object
 * @uuid: pointer to the uuid
 *
 * Allocates a new network port object. When the object is no longer needed,
 * g_object_unref() must be called in order to not leak data.
 *
 * Returns a pointer to the network port object, or NULL on error.
 */
virNetworkPortPtr
virGetNetworkPort(virNetworkPtr net, const unsigned char *uuid)
{
    g_autoptr(virNetworkPort) ret = NULL;

    if (virDataTypesInitialize() < 0)
        return NULL;

    virCheckNetworkGoto(net, error);
    virCheckNonNullArgGoto(uuid, error);

    ret = VIR_NETWORK_PORT(g_object_new(VIR_TYPE_NETWORK_PORT, NULL));

    ret->net = g_object_ref(net);
    memcpy(&(ret->uuid[0]), uuid, VIR_UUID_BUFLEN);

    return g_steal_pointer(&ret);

 error:
    return NULL;
}

/**
 * virNetworkPortFinalize:
 * @obj: the network port to release
 *
 * Unconditionally release all memory associated with a network port.
 * The network port object must not be used once this method returns.
 *
 * It will also unreference the associated network object,
 * which may also be released if its ref count hits zero.
 */
static void
virNetworkPortFinalize(GObject *obj)
{
    virNetworkPortPtr port = VIR_NETWORK_PORT(obj);
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(port->uuid, uuidstr);
    VIR_DEBUG("release network port %p %s", port, uuidstr);

    g_object_unref(port->net);

    G_OBJECT_CLASS(vir_network_port_parent_class)->finalize(obj);
}


/**
 * virGetInterface:
 * @conn: the hypervisor connection
 * @name: pointer to the interface name
 * @mac: pointer to the mac
 *
 * Allocates a new interface object. When the object is no longer needed,
 * g_object_unref() must be called in order to not leak data.
 *
 * Returns a pointer to the interface object, or NULL on error.
 */
virInterfacePtr
virGetInterface(virConnectPtr conn, const char *name, const char *mac)
{
    g_autoptr(virInterface) ret = NULL;

    if (virDataTypesInitialize() < 0)
        return NULL;

    virCheckConnectReturn(conn, NULL);
    virCheckNonNullArgReturn(name, NULL);

    /* a NULL mac from caller is okay. Treat it as blank */
    if (mac == NULL)
       mac = "";

    ret = VIR_INTERFACE(g_object_new(VIR_TYPE_INTERFACE, NULL));

    ret->name = g_strdup(name);
    ret->mac = g_strdup(mac);

    ret->conn = virObjectRef(conn);

    return g_steal_pointer(&ret);
}

/**
 * virInterfaceFinalize:
 * @obj: the interface to release
 *
 * Unconditionally release all memory associated with an interface.
 * The interface object must not be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virInterfaceFinalize(GObject *obj)
{
    virInterfacePtr iface = VIR_INTERFACE(obj);
    VIR_DEBUG("release interface %p %s", iface, iface->name);

    VIR_FREE(iface->name);
    VIR_FREE(iface->mac);
    virObjectUnref(iface->conn);

    G_OBJECT_CLASS(vir_interface_parent_class)->finalize(obj);
}


/**
 * virGetStoragePool:
 * @conn: the hypervisor connection
 * @name: pointer to the storage pool name
 * @uuid: pointer to the uuid
 * @privateData: pointer to driver specific private data
 * @freeFunc: private data cleanup function pointer specific to driver
 *
 * Allocates a new storage pool object. When the object is no longer needed,
 * g_object_unref() must be called in order to not leak data.
 *
 * Returns a pointer to the storage pool object, or NULL on error.
 */
virStoragePoolPtr
virGetStoragePool(virConnectPtr conn, const char *name,
                  const unsigned char *uuid,
                  void *privateData, virFreeCallback freeFunc)
{
    g_autoptr(virStoragePool) ret = NULL;

    if (virDataTypesInitialize() < 0)
        return NULL;

    virCheckConnectReturn(conn, NULL);
    virCheckNonNullArgReturn(name, NULL);
    virCheckNonNullArgReturn(uuid, NULL);

    ret = VIR_STORAGE_POOL(g_object_new(VIR_TYPE_STORAGE_POOL, NULL));

    ret->name = g_strdup(name);

    ret->conn = virObjectRef(conn);
    memcpy(&(ret->uuid[0]), uuid, VIR_UUID_BUFLEN);

    /* set the driver specific data */
    ret->privateData = privateData;
    ret->privateDataFreeFunc = freeFunc;

    return g_steal_pointer(&ret);
}


/**
 * virStoragePoolFinalize:
 * @obj: the storage pool to release
 *
 * Unconditionally release all memory associated with a pool.
 * The pool object must not be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virStoragePoolFinalize(GObject *obj)
{
    virStoragePoolPtr pool = VIR_STORAGE_POOL(obj);
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(pool->uuid, uuidstr);
    VIR_DEBUG("release pool %p %s %s", pool, pool->name, uuidstr);

    if (pool->privateDataFreeFunc)
        pool->privateDataFreeFunc(pool->privateData);

    VIR_FREE(pool->name);
    virObjectUnref(pool->conn);

    G_OBJECT_CLASS(vir_storage_pool_parent_class)->finalize(obj);
}


/**
 * virGetStorageVol:
 * @conn: the hypervisor connection
 * @pool: pool owning the volume
 * @name: pointer to the storage vol name
 * @key: pointer to unique key of the volume
 * @privateData: pointer to driver specific private data
 * @freeFunc: private data cleanup function pointer specific to driver
 *
 * Allocates a new storage volume object. When the object is no longer needed,
 * virObjectUnref() must be called in order to not leak data.
 *
 * Returns a pointer to the storage volume object, or NULL on error.
 */
virStorageVolPtr
virGetStorageVol(virConnectPtr conn, const char *pool, const char *name,
                 const char *key, void *privateData, virFreeCallback freeFunc)
{
    virStorageVolPtr ret = NULL;

    if (virDataTypesInitialize() < 0)
        return NULL;

    virCheckConnectGoto(conn, error);
    virCheckNonNullArgGoto(pool, error);
    virCheckNonNullArgGoto(name, error);
    virCheckNonNullArgGoto(key, error);

    if (!(ret = virObjectNew(virStorageVolClass)))
        goto error;

    ret->pool = g_strdup(pool);
    ret->name = g_strdup(name);
    ret->key = g_strdup(key);

    ret->conn = virObjectRef(conn);

    /* set driver specific data */
    ret->privateData = privateData;
    ret->privateDataFreeFunc = freeFunc;

    return ret;

 error:
    virObjectUnref(ret);
    return NULL;
}


/**
 * virStorageVolDispose:
 * @obj: the storage volume to release
 *
 * Unconditionally release all memory associated with a volume.
 * The volume object must not be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virStorageVolDispose(void *obj)
{
    virStorageVolPtr vol = obj;
    VIR_DEBUG("release vol %p %s", vol, vol->name);

    if (vol->privateDataFreeFunc)
        vol->privateDataFreeFunc(vol->privateData);

    VIR_FREE(vol->key);
    VIR_FREE(vol->name);
    VIR_FREE(vol->pool);
    virObjectUnref(vol->conn);
}


/**
 * virGetNodeDevice:
 * @conn: the hypervisor connection
 * @name: device name (unique on node)
 *
 * Allocates a new node device object. When the object is no longer needed,
 * virObjectUnref() must be called in order to not leak data.
 *
 * Returns a pointer to the node device object, or NULL on error.
 */
virNodeDevicePtr
virGetNodeDevice(virConnectPtr conn, const char *name)
{
    virNodeDevicePtr ret = NULL;

    if (virDataTypesInitialize() < 0)
        return NULL;

    virCheckConnectGoto(conn, error);
    virCheckNonNullArgGoto(name, error);

    if (!(ret = virObjectNew(virNodeDeviceClass)))
        goto error;

    ret->name = g_strdup(name);

    ret->conn = virObjectRef(conn);
    return ret;

 error:
    virObjectUnref(ret);
    return NULL;
}


/**
 * virNodeDeviceDispose:
 * @obj: the node device to release
 *
 * Unconditionally release all memory associated with a device.
 * The device object must not be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virNodeDeviceDispose(void *obj)
{
    virNodeDevicePtr dev = obj;
    VIR_DEBUG("release dev %p %s", dev, dev->name);

    VIR_FREE(dev->name);
    VIR_FREE(dev->parentName);

    virObjectUnref(dev->conn);
}


/**
 * virGetSecret:
 * @conn: the hypervisor connection
 * @uuid: secret UUID
 *
 * Allocates a new secret object. When the object is no longer needed,
 * virObjectUnref() must be called in order to not leak data.
 *
 * Returns a pointer to the secret object, or NULL on error.
 */
virSecretPtr
virGetSecret(virConnectPtr conn, const unsigned char *uuid,
             int usageType, const char *usageID)
{
    virSecretPtr ret = NULL;

    if (virDataTypesInitialize() < 0)
        return NULL;

    virCheckConnectGoto(conn, error);
    virCheckNonNullArgGoto(uuid, error);

    if (!(ret = virObjectNew(virSecretClass)))
        return NULL;

    memcpy(&(ret->uuid[0]), uuid, VIR_UUID_BUFLEN);
    ret->usageType = usageType;
    ret->usageID = g_strdup(NULLSTR_EMPTY(usageID));

    ret->conn = virObjectRef(conn);

    return ret;

 error:
    virObjectUnref(ret);
    return NULL;
}

/**
 * virSecretDispose:
 * @obj: the secret to release
 *
 * Unconditionally release all memory associated with a secret.
 * The secret object must not be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virSecretDispose(void *obj)
{
    virSecretPtr secret = obj;
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(secret->uuid, uuidstr);
    VIR_DEBUG("release secret %p %s", secret, uuidstr);

    VIR_FREE(secret->usageID);
    virObjectUnref(secret->conn);
}


/**
 * virGetStream:
 * @conn: the hypervisor connection
 *
 * Allocates a new stream object. When the object is no longer needed,
 * virObjectUnref() must be called in order to not leak data.
 *
 * Returns a pointer to the stream object, or NULL on error.
 */
virStreamPtr
virGetStream(virConnectPtr conn)
{
    virStreamPtr ret = NULL;

    if (virDataTypesInitialize() < 0)
        return NULL;

    if (!(ret = virObjectNew(virStreamClass)))
        return NULL;

    ret->conn = virObjectRef(conn);

    return ret;
}

/**
 * virStreamDispose:
 * @obj: the stream to release
 *
 * Unconditionally release all memory associated with a stream.
 * The stream object must not be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virStreamDispose(void *obj)
{
    virStreamPtr st = obj;
    VIR_DEBUG("release dev %p", st);

    if (st->ff)
        st->ff(st->privateData);
    virObjectUnref(st->conn);
}


/**
 * virGetNWFilter:
 * @conn: the hypervisor connection
 * @name: pointer to the network filter pool name
 * @uuid: pointer to the uuid
 *
 * Allocates a new network filter object. When the object is no longer needed,
 * g_object_unref() must be called in order to not leak data.
 *
 * Returns a pointer to the network filter object, or NULL on error.
 */
virNWFilterPtr
virGetNWFilter(virConnectPtr conn, const char *name,
               const unsigned char *uuid)
{
    g_autoptr(virNWFilter) ret = NULL;

    if (virDataTypesInitialize() < 0)
        return NULL;

    virCheckConnectReturn(conn, NULL);
    virCheckNonNullArgReturn(name, NULL);
    virCheckNonNullArgReturn(uuid, NULL);

    ret = VIR_NW_FILTER(g_object_new(VIR_TYPE_NW_FILTER, NULL));

    ret->name = g_strdup(name);

    memcpy(&(ret->uuid[0]), uuid, VIR_UUID_BUFLEN);

    ret->conn = virObjectRef(conn);

    return g_steal_pointer(&ret);
}


/**
 * virNWFilterFinalize:
 * @obj: the network filter to release
 *
 * Unconditionally release all memory associated with a nwfilter.
 * The nwfilter object must not be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virNWFilterFinalize(GObject *obj)
{
    virNWFilterPtr nwfilter = VIR_NW_FILTER(obj);
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(nwfilter->uuid, uuidstr);
    VIR_DEBUG("release nwfilter %p %s %s", nwfilter, nwfilter->name, uuidstr);

    VIR_FREE(nwfilter->name);
    virObjectUnref(nwfilter->conn);

    G_OBJECT_CLASS(vir_nw_filter_parent_class)->finalize(obj);
}


/**
 * virGetNWFilterBinding:
 * @conn: the hypervisor connection
 * @portdev: pointer to the network filter port device name
 * @filtername: name of the network filter
 *
 * Allocates a new network filter binding object. When the object is no longer
 * needed, g_object_unref() must be called in order to not leak data.
 *
 * Returns a pointer to the network filter binding object, or NULL on error.
 */
virNWFilterBindingPtr
virGetNWFilterBinding(virConnectPtr conn, const char *portdev,
                      const char *filtername)
{
    g_autoptr(virNWFilterBinding) ret = NULL;

    if (virDataTypesInitialize() < 0)
        return NULL;

    virCheckConnectReturn(conn, NULL);
    virCheckNonNullArgReturn(portdev, NULL);

    ret = VIR_NW_FILTER_BINDING(g_object_new(VIR_TYPE_NW_FILTER_BINDING, NULL));

    ret->portdev = g_strdup(portdev);

    ret->filtername = g_strdup(filtername);

    ret->conn = virObjectRef(conn);

    return g_steal_pointer(&ret);
}


/**
 * virNWFilterBindingFinalize:
 * @obj: the network filter binding to release
 *
 * Unconditionally release all memory associated with a nwfilter binding.
 * The nwfilter binding object must not be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virNWFilterBindingFinalize(GObject *obj)
{
    virNWFilterBindingPtr binding = VIR_NW_FILTER_BINDING(obj);

    VIR_DEBUG("release binding %p %s", binding, binding->portdev);

    VIR_FREE(binding->portdev);
    VIR_FREE(binding->filtername);
    virObjectUnref(binding->conn);

    G_OBJECT_CLASS(vir_nw_filter_binding_parent_class)->finalize(obj);
}


/**
 * virGetDomainCheckpoint:
 * @domain: the domain to checkpoint
 * @name: pointer to the domain checkpoint name
 *
 * Allocates a new domain checkpoint object. When the object is no longer needed,
 * g_object_unref() must be called in order to not leak data.
 *
 * Returns a pointer to the domain checkpoint object, or NULL on error.
 */
virDomainCheckpointPtr
virGetDomainCheckpoint(virDomainPtr domain,
                       const char *name)
{
    g_autoptr(virDomainCheckpoint) ret = NULL;

    if (virDataTypesInitialize() < 0)
        return NULL;

    virCheckDomainReturn(domain, NULL);
    virCheckNonNullArgReturn(name, NULL);

    ret = VIR_DOMAIN_CHECKPOINT(g_object_new(VIR_TYPE_DOMAIN_CHECKPOINT, NULL));
    ret->name = g_strdup(name);

    ret->domain = virObjectRef(domain);

    return g_steal_pointer(&ret);
}


/**
 * virDomainCheckpointFinalize:
 * @obj: the domain checkpoint to release
 *
 * Unconditionally release all memory associated with a checkpoint.
 * The checkpoint object must not be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virDomainCheckpointFinalize(GObject *obj)
{
    virDomainCheckpointPtr checkpoint = VIR_DOMAIN_CHECKPOINT(obj);
    VIR_DEBUG("release checkpoint %p %s", checkpoint, checkpoint->name);

    VIR_FREE(checkpoint->name);
    virObjectUnref(checkpoint->domain);

    G_OBJECT_CLASS(vir_domain_checkpoint_parent_class)->finalize(obj);
}


/**
 * virGetDomainSnapshot:
 * @domain: the domain to snapshot
 * @name: pointer to the domain snapshot name
 *
 * Allocates a new domain snapshot object. When the object is no longer needed,
 * g_object_unref() must be called in order to not leak data.
 *
 * Returns a pointer to the domain snapshot object, or NULL on error.
 */
virDomainSnapshotPtr
virGetDomainSnapshot(virDomainPtr domain, const char *name)
{
    g_autoptr(virDomainSnapshot) ret = NULL;

    if (virDataTypesInitialize() < 0)
        return NULL;

    virCheckDomainReturn(domain, NULL);
    virCheckNonNullArgReturn(name, NULL);

    ret = VIR_DOMAIN_SNAPSHOT(g_object_new(VIR_TYPE_DOMAIN_SNAPSHOT, NULL));
    ret->name = g_strdup(name);

    ret->domain = virObjectRef(domain);

    return g_steal_pointer(&ret);
}


/**
 * virDomainSnapshotFinalize:
 * @obj: the domain snapshot to release
 *
 * Unconditionally release all memory associated with a snapshot.
 * The snapshot object must not be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virDomainSnapshotFinalize(GObject *obj)
{
    virDomainSnapshotPtr snapshot = VIR_DOMAIN_SNAPSHOT(obj);
    VIR_DEBUG("release snapshot %p %s", snapshot, snapshot->name);

    VIR_FREE(snapshot->name);
    virObjectUnref(snapshot->domain);

    G_OBJECT_CLASS(vir_domain_snapshot_parent_class)->finalize(obj);
}


virAdmConnectPtr
virAdmConnectNew(void)
{
    virAdmConnectPtr ret;

    if (virDataTypesInitialize() < 0)
        return NULL;

    if (!(ret = virObjectLockableNew(virAdmConnectClass)))
        return NULL;

    if (!(ret->closeCallback = virObjectLockableNew(virAdmConnectCloseCallbackDataClass)))
        goto error;

    return ret;

 error:
    virObjectUnref(ret);
    return NULL;
}

static void
virAdmConnectDispose(void *obj)
{
    virAdmConnectPtr conn = obj;

    if (conn->privateDataFreeFunc)
        conn->privateDataFreeFunc(conn);

    virURIFree(conn->uri);
    virObjectUnref(conn->closeCallback);
}

static void
virAdmConnectCloseCallbackDataDispose(void *obj)
{
    virAdmConnectCloseCallbackDataPtr cb_data = obj;

    virObjectLock(cb_data);
    virAdmConnectCloseCallbackDataReset(cb_data);
    virObjectUnlock(cb_data);
}

void
virAdmConnectCloseCallbackDataReset(virAdmConnectCloseCallbackDataPtr cbdata)
{
    if (cbdata->freeCallback)
        cbdata->freeCallback(cbdata->opaque);

    virObjectUnref(cbdata->conn);
    cbdata->conn = NULL;
    cbdata->freeCallback = NULL;
    cbdata->callback = NULL;
    cbdata->opaque = NULL;
}

int
virAdmConnectCloseCallbackDataUnregister(virAdmConnectCloseCallbackDataPtr cbdata,
                                         virAdmConnectCloseFunc cb)
{
    int ret = -1;

    virObjectLock(cbdata);
    if (cbdata->callback != cb) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("A different callback was requested"));
        goto cleanup;
    }

    virAdmConnectCloseCallbackDataReset(cbdata);
    ret = 0;
 cleanup:
    virObjectUnlock(cbdata);
    return ret;
}

int
virAdmConnectCloseCallbackDataRegister(virAdmConnectCloseCallbackDataPtr cbdata,
                                       virAdmConnectPtr conn,
                                       virAdmConnectCloseFunc cb,
                                       void *opaque,
                                       virFreeCallback freecb)
{
    int ret = -1;

    virObjectLock(cbdata);

    if (cbdata->callback) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("A close callback is already registered"));
        goto cleanup;
    }

    cbdata->conn = virObjectRef(conn);
    cbdata->callback = cb;
    cbdata->opaque = opaque;
    cbdata->freeCallback = freecb;

    ret = 0;
 cleanup:
    virObjectUnlock(conn->closeCallback);
    return ret;
}

virAdmServerPtr
virAdmGetServer(virAdmConnectPtr conn, const char *name)
{
    g_autoptr(virAdmServer) ret = NULL;

    if (virDataTypesInitialize() < 0)
        return NULL;

    ret = VIR_ADM_SERVER(g_object_new(VIR_TYPE_ADM_SERVER, NULL));

    ret->name = g_strdup(name);

    ret->conn = virObjectRef(conn);

    return g_steal_pointer(&ret);
}

static void
virAdmServerFinalize(GObject *obj)
{
    virAdmServerPtr srv = VIR_ADM_SERVER(obj);
    VIR_DEBUG("release server srv=%p name=%s", srv, srv->name);

    VIR_FREE(srv->name);
    virObjectUnref(srv->conn);

    G_OBJECT_CLASS(vir_adm_server_parent_class)->finalize(obj);
}

virAdmClientPtr
virAdmGetClient(virAdmServerPtr srv, const unsigned long long id,
                unsigned long long timestamp, unsigned int transport)
{
    g_autoptr(virAdmClient) ret = NULL;

    if (virDataTypesInitialize() < 0)
        return NULL;

    ret = VIR_ADM_CLIENT(g_object_new(VIR_TYPE_ADM_CLIENT, NULL));

    ret->id = id;
    ret->timestamp = timestamp;
    ret->transport = transport;
    ret->srv = g_object_ref(srv);

    return g_steal_pointer(&ret);
}

static void
virAdmClientFinalize(GObject *obj)
{
    virAdmClientPtr clt = VIR_ADM_CLIENT(obj);
    VIR_DEBUG("release client clt=%p, id=%llu", clt, clt->id);

    g_object_unref(clt->srv);

    G_OBJECT_CLASS(vir_adm_client_parent_class)->finalize(obj);
}
