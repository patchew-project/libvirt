/*
 * virsecretobj.h: internal <secret> objects handling
 *
 * Copyright (C) 2009-2010, 2013-2014, 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

#include "secret_conf.h"
#include "virobject.h"

typedef struct _virSecretObj virSecretObj;
typedef virSecretObj *virSecretObjPtr;

void
virSecretObjEndAPI(virSecretObjPtr *obj);

typedef struct _virSecretObjList virSecretObjList;
typedef virSecretObjList *virSecretObjListPtr;

virSecretObjListPtr
virSecretObjListNew(void);

virSecretObjPtr
virSecretObjListFindByUUID(virSecretObjListPtr secrets,
                           const char *uuidstr);

virSecretObjPtr
virSecretObjListFindByUsage(virSecretObjListPtr secrets,
                            int usageType,
                            const char *usageID);

void
virSecretObjListRemove(virSecretObjListPtr secrets,
                       virSecretObjPtr obj);

virSecretObjPtr
virSecretObjListAdd(virSecretObjListPtr secrets,
                    virSecretDefPtr newdef,
                    const char *configDir,
                    virSecretDefPtr *oldDef);

typedef bool
(*virSecretObjListACLFilter)(virConnectPtr conn,
                             virSecretDefPtr def);

int
virSecretObjListNumOfSecrets(virSecretObjListPtr secrets,
                             virSecretObjListACLFilter filter,
                             virConnectPtr conn);

int
virSecretObjListExport(virConnectPtr conn,
                       virSecretObjListPtr secretobjs,
                       virSecretPtr **secrets,
                       virSecretObjListACLFilter filter,
                       unsigned int flags);

int
virSecretObjListGetUUIDs(virSecretObjListPtr secrets,
                         char **uuids,
                         int maxuuids,
                         virSecretObjListACLFilter filter,
                         virConnectPtr conn);

int
virSecretObjDeleteConfig(virSecretObjPtr obj);

void
virSecretObjDeleteData(virSecretObjPtr obj);

int
virSecretObjSaveConfig(virSecretObjPtr obj);

int
virSecretObjSaveData(virSecretObjPtr obj);

virSecretDefPtr
virSecretObjGetDef(virSecretObjPtr obj);

void
virSecretObjSetDef(virSecretObjPtr obj,
                   virSecretDefPtr def);

unsigned char *
virSecretObjGetValue(virSecretObjPtr obj);

int
virSecretObjSetValue(virSecretObjPtr obj,
                     const unsigned char *value,
                     size_t value_size);

size_t
virSecretObjGetValueSize(virSecretObjPtr obj);

void
virSecretObjSetValueSize(virSecretObjPtr obj,
                         size_t value_size);

int
virSecretLoadAllConfigs(virSecretObjListPtr secrets,
                        const char *configDir);
