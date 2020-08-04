/*
 * storage_capabilities.h: storage pool capabilities XML processing
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

typedef struct _virStoragePoolCaps virStoragePoolCaps;
typedef virStoragePoolCaps *virStoragePoolCapsPtr;
struct _virStoragePoolCaps {
    virObjectLockable parent;

    virCapsPtr driverCaps;
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virStoragePoolCaps, virObjectUnref);


virStoragePoolCapsPtr
virStoragePoolCapsNew(virCapsPtr driverCaps);

char *
virStoragePoolCapsFormat(const virStoragePoolCaps *caps);
