/*
 * virstoragefilebackend.h: internal storage source backend contract
 *
 * Copyright (C) 2007-2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <sys/stat.h>

#include "virstoragefile.h"

/* ------- virStorageFile backends ------------ */
typedef struct _virStorageFileBackend virStorageFileBackend;
typedef virStorageFileBackend *virStorageFileBackendPtr;

struct _virStorageDriverData {
    virStorageFileBackendPtr backend;
    void *priv;

    uid_t uid;
    gid_t gid;
};

typedef int
(*virStorageFileBackendInit)(virStorageSourcePtr src);

typedef void
(*virStorageFileBackendDeinit)(virStorageSourcePtr src);

typedef int
(*virStorageFileBackendCreate)(virStorageSourcePtr src);

typedef int
(*virStorageFileBackendUnlink)(virStorageSourcePtr src);

typedef int
(*virStorageFileBackendStat)(virStorageSourcePtr src,
                             struct stat *st);

typedef ssize_t
(*virStorageFileBackendRead)(virStorageSourcePtr src,
                             size_t offset,
                             size_t len,
                             char **buf);

typedef const char *
(*virStorageFileBackendGetUniqueIdentifier)(virStorageSourcePtr src);

typedef int
(*virStorageFileBackendAccess)(virStorageSourcePtr src,
                               int mode);

typedef int
(*virStorageFileBackendChown)(const virStorageSource *src,
                              uid_t uid,
                              gid_t gid);

int virStorageFileBackendForType(int type,
                                 int protocol,
                                 bool required,
                                 virStorageFileBackendPtr *backend);

struct _virStorageFileBackend {
    int type;
    int protocol;

    /* All storage file callbacks may be omitted if not implemented */

    /* The following group of callbacks is expected to set a libvirt
     * error on failure. */
    virStorageFileBackendInit backendInit;
    virStorageFileBackendDeinit backendDeinit;
    virStorageFileBackendRead storageFileRead;
    virStorageFileBackendGetUniqueIdentifier storageFileGetUniqueIdentifier;

    /* The following group of callbacks is expected to set errno
     * and return -1 on error. No libvirt error shall be reported */
    virStorageFileBackendCreate storageFileCreate;
    virStorageFileBackendUnlink storageFileUnlink;
    virStorageFileBackendStat   storageFileStat;
    virStorageFileBackendAccess storageFileAccess;
    virStorageFileBackendChown  storageFileChown;
};

int virStorageFileBackendRegister(virStorageFileBackendPtr backend);
