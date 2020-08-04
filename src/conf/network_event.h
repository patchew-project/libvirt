/*
 * network_event.h: network event queue processing helpers
 *
 * Copyright (C) 2014 Red Hat, Inc.
 * Copyright (C) 2013 SUSE LINUX Products GmbH, Nuernberg, Germany.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "object_event.h"
#include "object_event_private.h"

int
virNetworkEventStateRegisterID(virConnectPtr conn,
                               virObjectEventStatePtr state,
                               virNetworkPtr net,
                               int eventID,
                               virConnectNetworkEventGenericCallback cb,
                               void *opaque,
                               virFreeCallback freecb,
                               int *callbackID)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(5)
    ATTRIBUTE_NONNULL(8);

int
virNetworkEventStateRegisterClient(virConnectPtr conn,
                                   virObjectEventStatePtr state,
                                   virNetworkPtr net,
                                   int eventID,
                                   virConnectNetworkEventGenericCallback cb,
                                   void *opaque,
                                   virFreeCallback freecb,
                                   int *callbackID)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(5)
    ATTRIBUTE_NONNULL(8);

virObjectEventPtr
virNetworkEventLifecycleNew(const char *name,
                            const unsigned char *uuid,
                            int type,
                            int detail);
