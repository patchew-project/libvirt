/*
 * virsecretobj.h: internal <secret> objects handling
 *
 * Copyright (C) 2009-2010, 2013-2014, 2016 Red Hat, Inc.
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

#ifndef __VIRSECRETOBJ_H__
# define __VIRSECRETOBJ_H__

# include "internal.h"

# include "secret_conf.h"
# include "virobject.h"
# include "virpoolobj.h"

typedef struct _virSecretObjTablePrivate virSecretObjTablePrivate;
typedef virSecretObjTablePrivate *virSecretObjTablePrivatePtr;

typedef struct _virSecretObjPrivate virSecretObjPrivate;
typedef virSecretObjPrivate *virSecretObjPrivatePtr;

virPoolObjPtr virSecretObjAdd(virPoolObjTablePtr secrets,
                              virSecretDefPtr def,
                              const char *configDir,
                              virSecretDefPtr *oldDef);

int virSecretObjNumOfSecrets(virPoolObjTablePtr secretobjs,
                             virConnectPtr conn,
                             virPoolObjACLFilter aclfilter);

int virSecretObjGetUUIDs(virPoolObjTablePtr secrets,
                         char **uuids,
                         int nuuids,
                         virPoolObjACLFilter aclfilter,
                         virConnectPtr conn);

int virSecretObjExportList(virConnectPtr conn,
                           virPoolObjTablePtr secretobjs,
                           virSecretPtr **secrets,
                           virPoolObjACLFilter aclfilter,
                           unsigned int flags);

int virSecretObjDeleteConfig(virPoolObjPtr obj);

void virSecretObjDeleteData(virPoolObjPtr obj);

int virSecretObjSaveConfig(virPoolObjPtr obj);

int virSecretObjSaveData(virPoolObjPtr obj);

virSecretDefPtr virSecretObjGetDef(virPoolObjPtr obj);

void virSecretObjSetDef(virPoolObjPtr obj, virSecretDefPtr def);

unsigned char *virSecretObjGetValue(virPoolObjPtr obj);

int virSecretObjSetValue(virPoolObjPtr obj,
                         const unsigned char *value, size_t value_size);

size_t virSecretObjGetValueSize(virPoolObjPtr obj);

void virSecretObjSetValueSize(virPoolObjPtr obj, size_t value_size);

int virSecretLoadAllConfigs(virPoolObjTablePtr secrets,
                            const char *configDir);
#endif /* __VIRSECRETOBJ_H__ */
