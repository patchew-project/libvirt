/*
 * virclosecallbacks.h: Connection close callbacks routines
 *
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "conf/virdomainobjlist.h"

typedef struct _virCloseCallbacks virCloseCallbacks;
typedef virCloseCallbacks *virCloseCallbacksPtr;

typedef void (*virCloseCallback)(virDomainObjPtr vm,
                                 virConnectPtr conn,
                                 void *opaque);
virCloseCallbacksPtr virCloseCallbacksNew(void);
int virCloseCallbacksSet(virCloseCallbacksPtr closeCallbacks,
                         virDomainObjPtr vm,
                         virConnectPtr conn,
                         virCloseCallback cb);
int virCloseCallbacksUnset(virCloseCallbacksPtr closeCallbacks,
                           virDomainObjPtr vm,
                           virCloseCallback cb);
virCloseCallback
virCloseCallbacksGet(virCloseCallbacksPtr closeCallbacks,
                     virDomainObjPtr vm,
                     virConnectPtr conn);
virConnectPtr
virCloseCallbacksGetConn(virCloseCallbacksPtr closeCallbacks,
                         virDomainObjPtr vm);
void
virCloseCallbacksRun(virCloseCallbacksPtr closeCallbacks,
                     virConnectPtr conn,
                     virDomainObjListPtr domains,
                     void *opaque);
