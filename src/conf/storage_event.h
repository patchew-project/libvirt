/*
 * storage_event.h: storage event queue processing helpers
 *
 * Copyright (C) 2010-2014 Red Hat, Inc.
 * Copyright (C) 2008 VirtualIron
 * Copyright (C) 2013 SUSE LINUX Products GmbH, Nuernberg, Germany.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
*/

#pragma once

#include "internal.h"
#include "object_event.h"
#include "object_event_private.h"

int
virStoragePoolEventStateRegisterID(virConnectPtr conn,
                                   virObjectEventStatePtr state,
                                   virStoragePoolPtr pool,
                                   int eventID,
                                   virConnectStoragePoolEventGenericCallback cb,
                                   void *opaque,
                                   virFreeCallback freecb,
                                   int *callbackID)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(5)
    ATTRIBUTE_NONNULL(8);

int
virStoragePoolEventStateRegisterClient(virConnectPtr conn,
                                   virObjectEventStatePtr state,
                                   virStoragePoolPtr pool,
                                   int eventID,
                                   virConnectStoragePoolEventGenericCallback cb,
                                   void *opaque,
                                   virFreeCallback freecb,
                                   int *callbackID)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(5)
    ATTRIBUTE_NONNULL(8);

virObjectEventPtr
virStoragePoolEventLifecycleNew(const char *name,
                                const unsigned char *uuid,
                                int type,
                                int detail);

virObjectEventPtr
virStoragePoolEventRefreshNew(const char *name,
                              const unsigned char *uuid);
