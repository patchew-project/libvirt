/*
 * domain_lock.c: Locking for domain lifecycle operations
 *
 * Copyright (C) 2010-2014 Red Hat, Inc.
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
 *
 */

#include <config.h>

#include "domain_lock.h"
#include "viralloc.h"
#include "viruuid.h"
#include "virerror.h"
#include "virlog.h"

#define VIR_FROM_THIS VIR_FROM_LOCKING

VIR_LOG_INIT("locking.domain_lock");


static int virDomainLockManagerAddLease(virLockManagerPtr lock,
                                        virDomainLeaseDefPtr lease)
{
    unsigned int leaseFlags = 0;
    virLockManagerParam lparams[] = {
        { .type = VIR_LOCK_MANAGER_PARAM_TYPE_STRING,
          .key = "path",
          .value = { .str = lease->path },
        },
        { .type = VIR_LOCK_MANAGER_PARAM_TYPE_ULONG,
          .key = "offset",
          .value = { .ul = lease->offset },
        },
        { .type = VIR_LOCK_MANAGER_PARAM_TYPE_STRING,
          .key = "lockspace",
          .value = { .str = lease->lockspace },
        },
    };
    size_t nparams = ARRAY_CARDINALITY(lparams);
    if (!lease->lockspace)
        nparams--;

    VIR_DEBUG("Add lease %s", lease->path);
    if (virLockManagerAddResource(lock,
                                  VIR_LOCK_MANAGER_RESOURCE_TYPE_LEASE,
                                  lease->key,
                                  nparams,
                                  lparams,
                                  leaseFlags) < 0) {
        VIR_DEBUG("Failed to add lease %s", lease->path);
        return -1;
    }
    return 0;
}


static int virDomainLockManagerAddImage(virLockManagerPtr lock,
                                        virStorageSourcePtr src,
                                        bool metadataOnly)
{
    unsigned int diskFlags = 0;
    int type = virStorageSourceGetActualType(src);

    if (!src->path)
        return 0;

    if (!(type == VIR_STORAGE_TYPE_BLOCK ||
          type == VIR_STORAGE_TYPE_FILE ||
          type == VIR_STORAGE_TYPE_DIR))
        return 0;

    if (metadataOnly) {
        diskFlags = VIR_LOCK_MANAGER_RESOURCE_METADATA;
    } else {
        if (src->readonly)
            diskFlags |= VIR_LOCK_MANAGER_RESOURCE_READONLY;
        if (src->shared)
            diskFlags |= VIR_LOCK_MANAGER_RESOURCE_SHARED;
    }

    VIR_DEBUG("Add disk %s", src->path);
    if (virLockManagerAddResource(lock,
                                  VIR_LOCK_MANAGER_RESOURCE_TYPE_DISK,
                                  src->path,
                                  0,
                                  NULL,
                                  diskFlags) < 0) {
        VIR_DEBUG("Failed add disk %s", src->path);
        return -1;
    }
    return 0;
}


static int
virDomainLockManagerAddMemory(virLockManagerPtr lock,
                              const virDomainMemoryDef *mem)
{
    const char *path = NULL;

    switch ((virDomainMemoryModel) mem->model) {
    case VIR_DOMAIN_MEMORY_MODEL_NVDIMM:
        path = mem->nvdimmPath;
        break;

    case VIR_DOMAIN_MEMORY_MODEL_DIMM:
    case VIR_DOMAIN_MEMORY_MODEL_LAST:
    case VIR_DOMAIN_MEMORY_MODEL_NONE:
        break;
    }

    if (!path)
        return 0;

    VIR_DEBUG("Adding memory %s", path);
    if (virLockManagerAddResource(lock,
                                  VIR_LOCK_MANAGER_RESOURCE_TYPE_DISK,
                                  path,
                                  0,
                                  NULL,
                                  VIR_LOCK_MANAGER_RESOURCE_METADATA) < 0)
        return -1;

    return 0;
}


static int
virDomainLockManagerAddFile(virLockManagerPtr lock,
                            const char *file)
{
    if (!file)
        return 0;

    VIR_DEBUG("Adding file %s", file);
    if (virLockManagerAddResource(lock,
                                  VIR_LOCK_MANAGER_RESOURCE_TYPE_DISK,
                                  file,
                                  0,
                                  NULL,
                                  VIR_LOCK_MANAGER_RESOURCE_METADATA) < 0)
        return -1;

    return 0;
}


static virLockManagerPtr virDomainLockManagerNew(virLockManagerPluginPtr plugin,
                                                 const char *uri,
                                                 virDomainObjPtr dom,
                                                 bool withResources,
                                                 bool metadataOnly,
                                                 unsigned int flags)
{
    virLockManagerPtr lock;
    const virDomainDef *def = dom->def;
    size_t i;
    virLockManagerParam params[] = {
        { .type = VIR_LOCK_MANAGER_PARAM_TYPE_UUID,
          .key = "uuid",
        },
        { .type = VIR_LOCK_MANAGER_PARAM_TYPE_STRING,
          .key = "name",
          .value = { .str = def->name },
        },
        { .type = VIR_LOCK_MANAGER_PARAM_TYPE_UINT,
          .key = "id",
          .value = { .iv = def->id },
        },
        { .type = VIR_LOCK_MANAGER_PARAM_TYPE_UINT,
          .key = "pid",
          .value = { .iv = dom->pid },
        },
        { .type = VIR_LOCK_MANAGER_PARAM_TYPE_CSTRING,
          .key = "uri",
          .value = { .cstr = uri },
        },
    };
    VIR_DEBUG("plugin=%p dom=%p withResources=%d",
              plugin, dom, withResources);

    memcpy(params[0].value.uuid, def->uuid, VIR_UUID_BUFLEN);

    if (!(lock = virLockManagerNew(virLockManagerPluginGetDriver(plugin),
                                   VIR_LOCK_MANAGER_OBJECT_TYPE_DOMAIN,
                                   ARRAY_CARDINALITY(params),
                                   params,
                                   flags)))
        goto error;

    if (withResources) {
        VIR_DEBUG("Adding leases");
        for (i = 0; i < def->nleases; i++)
            if (virDomainLockManagerAddLease(lock, def->leases[i]) < 0)
                goto error;
    }

    if (withResources || metadataOnly) {
        VIR_DEBUG("Adding disks");
        for (i = 0; i < def->ndisks; i++) {
            virDomainDiskDefPtr disk = def->disks[i];

            if (virDomainLockManagerAddImage(lock, disk->src, metadataOnly) < 0)
                goto error;
        }
    }

    if (metadataOnly) {
        for (i = 0; i < def->nmems; i++) {
            virDomainMemoryDefPtr mem = def->mems[i];

            if (virDomainLockManagerAddMemory(lock, mem) < 0)
                goto error;
        }

        if (def->os.loader &&
            virDomainLockManagerAddFile(lock, def->os.loader->nvram) < 0)
            goto error;

        if (virDomainLockManagerAddFile(lock, def->os.kernel) < 0)
            goto error;

        if (virDomainLockManagerAddFile(lock, def->os.initrd) < 0)
            goto error;

        if (virDomainLockManagerAddFile(lock, def->os.dtb) < 0)
            goto error;

        if (virDomainLockManagerAddFile(lock, def->os.slic_table) < 0)
            goto error;
    }

    return lock;

 error:
    virLockManagerFree(lock);
    return NULL;
}


int virDomainLockProcessStart(virLockManagerPluginPtr plugin,
                              const char *uri,
                              virDomainObjPtr dom,
                              bool paused,
                              int *fd)
{
    virLockManagerPtr lock;
    int ret;
    int flags = VIR_LOCK_MANAGER_ACQUIRE_RESTRICT;

    VIR_DEBUG("plugin=%p dom=%p paused=%d fd=%p",
              plugin, dom, paused, fd);

    if (!(lock = virDomainLockManagerNew(plugin, uri, dom, true, false,
                                         VIR_LOCK_MANAGER_NEW_STARTED)))
        return -1;

    if (paused)
        flags |= VIR_LOCK_MANAGER_ACQUIRE_REGISTER_ONLY;

    ret = virLockManagerAcquire(lock, NULL, flags,
                                dom->def->onLockFailure, fd);

    virLockManagerFree(lock);

    return ret;
}

int virDomainLockProcessPause(virLockManagerPluginPtr plugin,
                              virDomainObjPtr dom,
                              char **state)
{
    virLockManagerPtr lock;
    int ret;

    VIR_DEBUG("plugin=%p dom=%p state=%p",
              plugin, dom, state);

    if (!(lock = virDomainLockManagerNew(plugin, NULL, dom, true, false, 0)))
        return -1;

    ret = virLockManagerRelease(lock, state, 0);
    virLockManagerFree(lock);

    return ret;
}

int virDomainLockProcessResume(virLockManagerPluginPtr plugin,
                               const char *uri,
                               virDomainObjPtr dom,
                               const char *state)
{
    virLockManagerPtr lock;
    int ret;

    VIR_DEBUG("plugin=%p dom=%p state=%s",
              plugin, dom, NULLSTR(state));

    if (!(lock = virDomainLockManagerNew(plugin, uri, dom, true, false, 0)))
        return -1;

    ret = virLockManagerAcquire(lock, state, 0, dom->def->onLockFailure, NULL);
    virLockManagerFree(lock);

    return ret;
}

int virDomainLockProcessInquire(virLockManagerPluginPtr plugin,
                                virDomainObjPtr dom,
                                char **state)
{
    virLockManagerPtr lock;
    int ret;

    VIR_DEBUG("plugin=%p dom=%p state=%p",
              plugin, dom, state);

    if (!(lock = virDomainLockManagerNew(plugin, NULL, dom, true, false, 0)))
        return -1;

    ret = virLockManagerInquire(lock, state, 0);
    virLockManagerFree(lock);

    return ret;
}


int virDomainLockImageAttach(virLockManagerPluginPtr plugin,
                             const char *uri,
                             virDomainObjPtr dom,
                             virStorageSourcePtr src)
{
    virLockManagerPtr lock;
    int ret = -1;

    VIR_DEBUG("plugin=%p dom=%p src=%p", plugin, dom, src);

    if (!(lock = virDomainLockManagerNew(plugin, uri, dom, false, false, 0)))
        return -1;

    if (virDomainLockManagerAddImage(lock, src, false) < 0)
        goto cleanup;

    if (virLockManagerAcquire(lock, NULL, 0,
                              dom->def->onLockFailure, NULL) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virLockManagerFree(lock);

    return ret;
}


int virDomainLockDiskAttach(virLockManagerPluginPtr plugin,
                            const char *uri,
                            virDomainObjPtr dom,
                            virDomainDiskDefPtr disk)
{
    return virDomainLockImageAttach(plugin, uri, dom, disk->src);
}


int virDomainLockImageDetach(virLockManagerPluginPtr plugin,
                             virDomainObjPtr dom,
                             virStorageSourcePtr src)
{
    virLockManagerPtr lock;
    int ret = -1;

    VIR_DEBUG("plugin=%p dom=%p src=%p", plugin, dom, src);

    if (!(lock = virDomainLockManagerNew(plugin, NULL, dom, false, false, 0)))
        return -1;

    if (virDomainLockManagerAddImage(lock, src, false) < 0)
        goto cleanup;

    if (virLockManagerRelease(lock, NULL, 0) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virLockManagerFree(lock);

    return ret;
}


int virDomainLockDiskDetach(virLockManagerPluginPtr plugin,
                            virDomainObjPtr dom,
                            virDomainDiskDefPtr disk)
{
    return virDomainLockImageDetach(plugin, dom, disk->src);
}


int virDomainLockLeaseAttach(virLockManagerPluginPtr plugin,
                             const char *uri,
                             virDomainObjPtr dom,
                             virDomainLeaseDefPtr lease)
{
    virLockManagerPtr lock;
    int ret = -1;

    VIR_DEBUG("plugin=%p dom=%p lease=%p",
              plugin, dom, lease);

    if (!(lock = virDomainLockManagerNew(plugin, uri, dom, false, false, 0)))
        return -1;

    if (virDomainLockManagerAddLease(lock, lease) < 0)
        goto cleanup;

    if (virLockManagerAcquire(lock, NULL, 0,
                              dom->def->onLockFailure, NULL) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virLockManagerFree(lock);

    return ret;
}

int virDomainLockLeaseDetach(virLockManagerPluginPtr plugin,
                             virDomainObjPtr dom,
                             virDomainLeaseDefPtr lease)
{
    virLockManagerPtr lock;
    int ret = -1;

    VIR_DEBUG("plugin=%p dom=%p lease=%p",
              plugin, dom, lease);

    if (!(lock = virDomainLockManagerNew(plugin, NULL, dom, false, false, 0)))
        return -1;

    if (virDomainLockManagerAddLease(lock, lease) < 0)
        goto cleanup;

    if (virLockManagerRelease(lock, NULL, 0) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virLockManagerFree(lock);

    return ret;
}


int
virDomainLockMetadataLock(virLockManagerPluginPtr plugin,
                          virDomainObjPtr dom)
{
    virLockManagerPtr lock;
    const unsigned int flags = 0;
    int ret = -1;

    VIR_DEBUG("plugin=%p dom=%p", plugin, dom);

    if (!(lock = virDomainLockManagerNew(plugin, NULL, dom, false, true, 0)))
        return -1;

    if (virLockManagerAcquire(lock, NULL, flags,
                              VIR_DOMAIN_LOCK_FAILURE_DEFAULT, NULL) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    virLockManagerFree(lock);
    return ret;
}


int
virDomainLockMetadataUnlock(virLockManagerPluginPtr plugin,
                            virDomainObjPtr dom)
{
    virLockManagerPtr lock;
    const unsigned int flags = 0;
    int ret = -1;

    VIR_DEBUG("plugin=%p dom=%p", plugin, dom);

    if (!(lock = virDomainLockManagerNew(plugin, NULL, dom, false, true, 0)))
        return -1;

    if (virLockManagerRelease(lock, NULL, flags) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    virLockManagerFree(lock);
    return ret;
}


int
virDomainLockMetadataImageLock(virLockManagerPluginPtr plugin,
                               virDomainObjPtr dom,
                               virStorageSourcePtr src)
{
    virLockManagerPtr lock;
    int ret = -1;

    VIR_DEBUG("plugin=%p dom=%p src=%p", plugin, dom, src);

    if (!(lock = virDomainLockManagerNew(plugin, NULL, dom, false, false, 0)))
        return -1;

    if (virDomainLockManagerAddImage(lock, src, true) < 0)
        goto cleanup;

    if (virLockManagerAcquire(lock, NULL, 0,
                              VIR_DOMAIN_LOCK_FAILURE_DEFAULT, NULL) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    virLockManagerFree(lock);
    return ret;
}


int
virDomainLockMetadataImageUnlock(virLockManagerPluginPtr plugin,
                                 virDomainObjPtr dom,
                                 virStorageSourcePtr src)
{
    virLockManagerPtr lock;
    int ret = -1;

    VIR_DEBUG("plugin=%p dom=%p src=%p", plugin, dom, src);

    if (!(lock = virDomainLockManagerNew(plugin, NULL, dom, false, false, 0)))
        return -1;

    if (virDomainLockManagerAddImage(lock, src, true) < 0)
        goto cleanup;

    if (virLockManagerRelease(lock, NULL, 0) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    virLockManagerFree(lock);
    return ret;
}


int
virDomainLockMetadataDiskLock(virLockManagerPluginPtr plugin,
                              virDomainObjPtr dom,
                              virDomainDiskDefPtr disk)
{
    return virDomainLockMetadataImageLock(plugin, dom, disk->src);
}


int
virDomainLockMetadataDiskUnlock(virLockManagerPluginPtr plugin,
                                virDomainObjPtr dom,
                                virDomainDiskDefPtr disk)
{
    return virDomainLockMetadataImageUnlock(plugin, dom, disk->src);
}


int
virDomainLockMetadataMemLock(virLockManagerPluginPtr plugin,
                             virDomainObjPtr dom,
                             virDomainMemoryDefPtr mem)
{
    virLockManagerPtr lock;
    int ret = -1;

    VIR_DEBUG("plugin=%p dom=%p mem=%p", plugin, dom, mem);

    if (!(lock = virDomainLockManagerNew(plugin, NULL, dom, false, false, 0)))
        return -1;

    if (virDomainLockManagerAddMemory(lock, mem) < 0)
        goto cleanup;

    if (virLockManagerAcquire(lock, NULL, 0,
                              VIR_DOMAIN_LOCK_FAILURE_DEFAULT, NULL) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    virLockManagerFree(lock);
    return ret;
}


int
virDomainLockMetadataMemUnlock(virLockManagerPluginPtr plugin,
                               virDomainObjPtr dom,
                               virDomainMemoryDefPtr mem)
{
    virLockManagerPtr lock;
    int ret = -1;

    VIR_DEBUG("plugin=%p dom=%p mem=%p", plugin, dom, mem);

    if (!(lock = virDomainLockManagerNew(plugin, NULL, dom, false, false, 0)))
        return -1;

    if (virDomainLockManagerAddMemory(lock, mem) < 0)
        goto cleanup;

    if (virLockManagerRelease(lock, NULL, 0) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    virLockManagerFree(lock);
    return ret;
}
