/*
 * object_event.h: object event queue processing helpers
 *
 * Copyright (C) 2012-2014 Red Hat, Inc.
 * Copyright (C) 2008 VirtualIron
 * Copyright (C) 2013 SUSE LINUX Products GmbH, Nuernberg, Germany.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

#include "virobject.h"

/**
 * Dispatching domain events that come in while
 * in a call / response rpc
 */
typedef struct _virObjectEvent virObjectEvent;
typedef virObjectEvent *virObjectEventPtr;

typedef struct _virObjectEventState virObjectEventState;
typedef virObjectEventState *virObjectEventStatePtr;


virObjectEventStatePtr
virObjectEventStateNew(void);

/**
 * virConnectObjectEventGenericCallback:
 * @conn: the connection pointer
 * @obj: the object pointer
 * @opaque: application specified data
 *
 * A generic object event callback handler. Specific events usually
 * have a customization with extra parameters
 */
typedef void (*virConnectObjectEventGenericCallback)(virConnectPtr conn,
                                                     void *obj,
                                                     void *opaque);

#define VIR_OBJECT_EVENT_CALLBACK(cb) \
    ((virConnectObjectEventGenericCallback)(cb))

void
virObjectEventStateQueue(virObjectEventStatePtr state,
                         virObjectEventPtr event)
    ATTRIBUTE_NONNULL(1);

void
virObjectEventStateQueueRemote(virObjectEventStatePtr state,
                               virObjectEventPtr event,
                               int remoteID)
    ATTRIBUTE_NONNULL(1);

int
virObjectEventStateDeregisterID(virConnectPtr conn,
                                virObjectEventStatePtr state,
                                int callbackID,
                                bool doFreeCb)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int
virObjectEventStateEventID(virConnectPtr conn,
                           virObjectEventStatePtr state,
                           int callbackID,
                           int *remoteID)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

void
virObjectEventStateSetRemote(virConnectPtr conn,
                             virObjectEventStatePtr state,
                             int callbackID,
                             int remoteID)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);
