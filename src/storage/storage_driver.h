/*
 * storage_driver.h: core driver for storage APIs
 *
 * Copyright (C) 2006-2008, 2014 Red Hat, Inc.
 * Copyright (C) 2006-2008 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <sys/stat.h>

#include "domain_conf.h"
#include "virstorageobj.h"

virStoragePoolObjPtr virStoragePoolObjFindPoolByUUID(const unsigned char *uuid)
    ATTRIBUTE_NONNULL(1);

virStoragePoolPtr
storagePoolLookupByTargetPath(virConnectPtr conn,
                              const char *path)
    ATTRIBUTE_NONNULL(2);

char *virStoragePoolObjBuildTempFilePath(virStoragePoolObjPtr obj,
                                         virStorageVolDefPtr voldef)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;

int storageRegister(void);
int storageRegisterAll(void);
