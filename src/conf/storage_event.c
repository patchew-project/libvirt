/*
 * storage_event.c: storage event queue processing helpers
 *
 * Copyright (C) 2010-2014 Red Hat, Inc.
 * Copyright (C) 2008 VirtualIron
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
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
*/

#include <config.h>

#include "storage_event.h"
#include "object_event.h"
#include "object_event_private.h"
#include "datatypes.h"
#include "virlog.h"

VIR_LOG_INIT("conf.storage_event");

struct _virStoragePoolEvent {
    virObjectEvent parent;

    /* Unused attribute to allow for subclass creation */
    bool dummy;
};
typedef struct _virStoragePoolEvent virStoragePoolEvent;
typedef virStoragePoolEvent *virStoragePoolEventPtr;

struct _virStoragePoolEventLifecycle {
    virStoragePoolEvent parent;

    int type;
    int detail;
};
typedef struct _virStoragePoolEventLifecycle virStoragePoolEventLifecycle;
typedef virStoragePoolEventLifecycle *virStoragePoolEventLifecyclePtr;

struct _virStoragePoolEventRefresh {
    virStoragePoolEvent parent;

    bool dummy;
};
typedef struct _virStoragePoolEventRefresh virStoragePoolEventRefresh;
typedef virStoragePoolEventRefresh *virStoragePoolEventRefreshPtr;

struct _virStorageVolEvent {
    virObjectEvent parent;

    /* Unused attribute to allow for subclass creation */
    bool dummy;
};
typedef struct _virStorageVolEvent virStorageVolEvent;
typedef virStorageVolEvent *virStorageVolEventPtr;

struct _virStorageVolEventLifecycle {
    virStorageVolEvent parent;

    int type;
    int detail;
};
typedef struct _virStorageVolEventLifecycle virStorageVolEventLifecycle;
typedef virStorageVolEventLifecycle *virStorageVolEventLifecyclePtr;

static virClassPtr virStoragePoolEventClass;
static virClassPtr virStoragePoolEventLifecycleClass;
static virClassPtr virStoragePoolEventRefreshClass;
static void virStoragePoolEventDispose(void *obj);
static void virStoragePoolEventLifecycleDispose(void *obj);
static void virStoragePoolEventRefreshDispose(void *obj);

static virClassPtr virStorageVolEventClass;
static virClassPtr virStorageVolEventLifecycleClass;
static void virStorageVolEventDispose(void *obj);
static void virStorageVolEventLifecycleDispose(void *obj);

static int
virStoragePoolEventsOnceInit(void)
{
    if (!VIR_CLASS_NEW(virStoragePoolEvent, virClassForObjectEvent()))
        return -1;

    if (!VIR_CLASS_NEW(virStoragePoolEventLifecycle, virStoragePoolEventClass))
        return -1;

    if (!VIR_CLASS_NEW(virStoragePoolEventRefresh, virStoragePoolEventClass))
        return -1;

    return 0;
}

static int
virStorageVolEventsOnceInit(void)
{
    if (!VIR_CLASS_NEW(virStorageVolEvent, virClassForObjectEvent()))
        return -1;

    if (!VIR_CLASS_NEW(virStorageVolEventLifecycle, virStorageVolEventClass))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(virStoragePoolEvents)
VIR_ONCE_GLOBAL_INIT(virStorageVolEvents)

static void
virStoragePoolEventDispose(void *obj)
{
    virStoragePoolEventPtr event = obj;
    VIR_DEBUG("obj=%p", event);
}


static void
virStoragePoolEventLifecycleDispose(void *obj)
{
    virStoragePoolEventLifecyclePtr event = obj;
    VIR_DEBUG("obj=%p", event);
}


static void
virStoragePoolEventRefreshDispose(void *obj)
{
    virStoragePoolEventRefreshPtr event = obj;
    VIR_DEBUG("obj=%p", event);
}

static void
virStorageVolEventDispose(void *obj)
{
    virStorageVolEventPtr event = obj;
    VIR_DEBUG("obj=%p", event);
}


static void
virStorageVolEventLifecycleDispose(void *obj)
{
    virStorageVolEventLifecyclePtr event = obj;
    VIR_DEBUG("obj=%p", event);
}

static void
virStoragePoolEventDispatchDefaultFunc(virConnectPtr conn,
                                       virObjectEventPtr event,
                                       virConnectObjectEventGenericCallback cb,
                                       void *cbopaque)
{
    virStoragePoolPtr pool = virGetStoragePool(conn,
                                               event->meta.name,
                                               event->meta.uuid,
                                               NULL, NULL);
    if (!pool)
        return;

    switch ((virStoragePoolEventID)event->eventID) {
    case VIR_STORAGE_POOL_EVENT_ID_LIFECYCLE:
        {
            virStoragePoolEventLifecyclePtr storagePoolLifecycleEvent;

            storagePoolLifecycleEvent = (virStoragePoolEventLifecyclePtr)event;
            ((virConnectStoragePoolEventLifecycleCallback)cb)(conn, pool,
                                                              storagePoolLifecycleEvent->type,
                                                              storagePoolLifecycleEvent->detail,
                                                              cbopaque);
            goto cleanup;
        }

    case VIR_STORAGE_POOL_EVENT_ID_REFRESH:
        {
            ((virConnectStoragePoolEventGenericCallback)cb)(conn, pool,
                                                            cbopaque);
            goto cleanup;
        }

    case VIR_STORAGE_POOL_EVENT_ID_LAST:
        break;
    }
    VIR_WARN("Unexpected event ID %d", event->eventID);

 cleanup:
    virObjectUnref(pool);
}


static void
virStorageVolEventDispatchDefaultFunc(virConnectPtr conn,
                                      virObjectEventPtr event,
                                      virConnectObjectEventGenericCallback cb ATTRIBUTE_UNUSED,
                                      void *cbopaque ATTRIBUTE_UNUSED)
{
    virStorageVolPtr vol = virStorageVolLookupByKey(conn,
                                                    event->meta.key);
    if (!vol)
        return;

    switch ((virStorageVolEventID)event->eventID) {
    case VIR_STORAGE_VOL_EVENT_ID_LIFECYCLE:
        {
            virStorageVolEventLifecyclePtr storageVolLifecycleEvent;

            storageVolLifecycleEvent = (virStorageVolEventLifecyclePtr)event;
            ((virConnectStorageVolEventLifecycleCallback)cb)(conn, vol,
                                                             storageVolLifecycleEvent->type,
                                                             storageVolLifecycleEvent->detail,
                                                             cbopaque);
            goto cleanup;
        }

    case VIR_STORAGE_VOL_EVENT_ID_LAST:
        break;
    }
    VIR_WARN("Unexpected event ID %d", event->eventID);

 cleanup:
    virObjectUnref(vol);
}


/**
 * virStoragePoolEventStateRegisterID:
 * @conn: connection to associate with callback
 * @state: object event state
 * @pool: storage pool to filter on or NULL for all storage pools
 * @eventID: ID of the event type to register for
 * @cb: function to invoke when event occurs
 * @opaque: data blob to pass to @callback
 * @freecb: callback to free @opaque
 * @callbackID: filled with callback ID
 *
 * Register the function @cb with connection @conn, from @state, for
 * events of type @eventID, and return the registration handle in
 * @callbackID.
 *
 * Returns: the number of callbacks now registered, or -1 on error
 */
int
virStoragePoolEventStateRegisterID(virConnectPtr conn,
                                   virObjectEventStatePtr state,
                                   virStoragePoolPtr pool,
                                   int eventID,
                                   virConnectStoragePoolEventGenericCallback cb,
                                   void *opaque,
                                   virFreeCallback freecb,
                                   int *callbackID)
{
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    if (virStoragePoolEventsInitialize() < 0)
        return -1;

    if (pool)
        virUUIDFormat(pool->uuid, uuidstr);
    return virObjectEventStateRegisterID(conn, state, pool ? uuidstr : NULL,
                                         NULL, NULL,
                                         virStoragePoolEventClass, eventID,
                                         VIR_OBJECT_EVENT_CALLBACK(cb),
                                         opaque, freecb,
                                         false, callbackID, false);
}

/**
 * virStorageVolEventStateRegisterID:
 * @conn: connection to associate with callback
 * @state: object event state
 * @vol: storage vol to filter on or NULL for all storage volumes
 * @eventID: ID of the event type to register for
 * @cb: function to invoke when event occurs
 * @opaque: data blob to pass to @callback
 * @freecb: callback to free @opaque
 * @callbackID: filled with callback ID
 *
 * Register the function @cb with connection @conn, from @state, for
 * events of type @eventID, and return the registration handle in
 * @callbackID.
 *
 * Returns: the number of callbacks now registered, or -1 on error
 */
int
virStorageVolEventStateRegisterID(virConnectPtr conn,
                                  virObjectEventStatePtr state,
                                  virStorageVolPtr vol,
                                  int eventID,
                                  virConnectStorageVolEventGenericCallback cb,
                                  void *opaque,
                                  virFreeCallback freecb,
                                  int *callbackID)
{
    if (virStorageVolEventsInitialize() < 0)
        return -1;

    return virObjectEventStateRegisterID(conn, state, vol ? vol->key : NULL,
                                         NULL, NULL,
                                         virStorageVolEventClass, eventID,
                                         VIR_OBJECT_EVENT_CALLBACK(cb),
                                         opaque, freecb,
                                         false, callbackID, false);
}


/**
 * virStoragePoolEventStateRegisterClient:
 * @conn: connection to associate with callback
 * @state: object event state
 * @pool: storage pool to filter on or NULL for all storage pools
 * @eventID: ID of the event type to register for
 * @cb: function to invoke when event occurs
 * @opaque: data blob to pass to @callback
 * @freecb: callback to free @opaque
 * @callbackID: filled with callback ID
 *
 * Register the function @cb with connection @conn, from @state, for
 * events of type @eventID, and return the registration handle in
 * @callbackID.  This version is intended for use on the client side
 * of RPC.
 *
 * Returns: the number of callbacks now registered, or -1 on error
 */
int
virStoragePoolEventStateRegisterClient(virConnectPtr conn,
                                       virObjectEventStatePtr state,
                                       virStoragePoolPtr pool,
                                       int eventID,
                                       virConnectStoragePoolEventGenericCallback cb,
                                       void *opaque,
                                       virFreeCallback freecb,
                                       int *callbackID)
{
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    if (virStoragePoolEventsInitialize() < 0)
        return -1;

    if (pool)
        virUUIDFormat(pool->uuid, uuidstr);
    return virObjectEventStateRegisterID(conn, state, pool ? uuidstr : NULL,
                                         NULL, NULL,
                                         virStoragePoolEventClass, eventID,
                                         VIR_OBJECT_EVENT_CALLBACK(cb),
                                         opaque, freecb,
                                         false, callbackID, true);
}


/**
 * virStoragePoolEventLifecycleNew:
 * @name: name of the storage pool object the event describes
 * @uuid: uuid of the storage pool object the event describes
 * @type: type of lifecycle event
 * @detail: more details about @type
 *
 * Create a new storage pool lifecycle event.
 */
virObjectEventPtr
virStoragePoolEventLifecycleNew(const char *name,
                                const unsigned char *uuid,
                                int type,
                                int detail)
{
    virStoragePoolEventLifecyclePtr event;
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    if (virStoragePoolEventsInitialize() < 0)
        return NULL;

    virUUIDFormat(uuid, uuidstr);
    if (!(event = virObjectEventNew(virStoragePoolEventLifecycleClass,
                                    virStoragePoolEventDispatchDefaultFunc,
                                    VIR_STORAGE_POOL_EVENT_ID_LIFECYCLE,
                                    0, name, uuid, uuidstr)))
        return NULL;

    event->type = type;
    event->detail = detail;

    return (virObjectEventPtr)event;
}

/**
 * virStorageVolEventLifecycleNew:
 * @name: name of the storage volume object the event describes
 * @key: key of the storage volume object the event describes
 * @type: type of lifecycle event
 * @detail: more details about @type
 *
 * Create a new storage volume lifecycle event.
 */
virObjectEventPtr
virStorageVolEventLifecycleNew(const char *pool,
                               const char *name,
                               const unsigned char *key,
                               int type,
                               int detail)
{
    virStorageVolEventLifecyclePtr event;

    if (virStorageVolEventsInitialize() < 0)
        return NULL;

    if (!(event = virObjectEventNew(virStorageVolEventLifecycleClass,
                                    virStorageVolEventDispatchDefaultFunc,
                                    VIR_STORAGE_VOL_EVENT_ID_LIFECYCLE,
                                    0, name, key, pool)))
        return NULL;

    event->type = type;
    event->detail = detail;

    return (virObjectEventPtr)event;
}


/**
 * virStoragePoolEventRefreshNew:
 * @name: name of the storage pool object the event describes
 * @uuid: uuid of the storage pool object the event describes
 *
 * Create a new storage pool refresh event.
 */
virObjectEventPtr
virStoragePoolEventRefreshNew(const char *name,
                              const unsigned char *uuid)
{
    virStoragePoolEventRefreshPtr event;
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    if (virStoragePoolEventsInitialize() < 0)
        return NULL;

    virUUIDFormat(uuid, uuidstr);
    if (!(event = virObjectEventNew(virStoragePoolEventRefreshClass,
                                    virStoragePoolEventDispatchDefaultFunc,
                                    VIR_STORAGE_POOL_EVENT_ID_REFRESH,
                                    0, name, uuid, uuidstr)))
        return NULL;

    return (virObjectEventPtr)event;
}
