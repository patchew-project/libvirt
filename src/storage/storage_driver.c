/*
 * storage_driver.c: core driver for storage APIs
 *
 * Copyright (C) 2006-2015 Red Hat, Inc.
 * Copyright (C) 2006-2008 Daniel P. Berrange
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
 * Author: Daniel P. Berrange <berrange@redhat.com>
 */

#include <config.h>

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>

#if HAVE_PWD_H
# include <pwd.h>
#endif
#include <errno.h>
#include <string.h>

#include "virerror.h"
#include "datatypes.h"
#include "driver.h"
#include "storage_driver.h"
#include "storage_conf.h"
#include "storage_event.h"
#include "viralloc.h"
#include "storage_backend.h"
#include "virlog.h"
#include "virfile.h"
#include "fdstream.h"
#include "configmake.h"
#include "virstring.h"
#include "viraccessapicheck.h"
#include "dirname.h"
#include "storage_util.h"

#define VIR_FROM_THIS VIR_FROM_STORAGE

VIR_LOG_INIT("storage.storage_driver");

static virStorageDriverStatePtr driver;

static int storageStateCleanup(void);

typedef struct _virStorageVolStreamInfo virStorageVolStreamInfo;
typedef virStorageVolStreamInfo *virStorageVolStreamInfoPtr;
struct _virStorageVolStreamInfo {
    char *pool_name;
    char *vol_path;
};

static void storageDriverLock(void)
{
    virMutexLock(&driver->lock);
}
static void storageDriverUnlock(void)
{
    virMutexUnlock(&driver->lock);
}

static void
storagePoolUpdateState(virPoolObjPtr obj,
                       void *opaque ATTRIBUTE_UNUSED)
{
    virStoragePoolDefPtr def = virPoolObjGetDef(obj);
    bool active;
    virStorageBackendPtr backend;
    int ret = -1;
    char *stateFile;

    if (!(stateFile = virFileBuildPath(driver->stateDir, def->name, ".xml")))
        goto error;

    if ((backend = virStorageBackendForType(def->type)) == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Missing backend %d"), def->type);
        goto error;
    }

    /* Backends which do not support 'checkPool' are considered
     * inactive by default.
     */
    active = false;
    if (backend->checkPool &&
        backend->checkPool(obj, &active) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to initialize storage pool '%s': %s"),
                       def->name, virGetLastErrorMessage());
        goto error;
    }

    /* We can pass NULL as connection, most backends do not use
     * it anyway, but if they do and fail, we want to log error and
     * continue with other pools.
     */
    if (active) {
        virStoragePoolObjClearVols(obj);
        if (backend->refreshPool(NULL, obj) < 0) {
            if (backend->stopPool)
                backend->stopPool(NULL, obj);
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Failed to restart storage pool '%s': %s"),
                           def->name, virGetLastErrorMessage());
            goto error;
        }
    }

    virPoolObjSetActive(obj, active);
    ret = 0;
 error:
    if (ret < 0) {
        if (stateFile)
            unlink(stateFile);
    }
    VIR_FREE(stateFile);

    return;
}


static void
storagePoolDoAutostart(virPoolObjPtr obj,
                       void *opaque ATTRIBUTE_UNUSED)
{
    virStoragePoolDefPtr def = virPoolObjGetDef(obj);
    virConnectPtr conn = opaque;
    virStorageBackendPtr backend;
    bool started = false;

    if (!(backend = virStorageBackendForType(def->type)))
        return;

    if (virPoolObjIsAutostart(obj) && !virPoolObjIsActive(obj)) {
        if (backend->startPool &&
            backend->startPool(conn, obj) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Failed to autostart storage pool '%s': %s"),
                           def->name, virGetLastErrorMessage());
            return;
        }
        started = true;
    }

    if (started) {
        char *stateFile;

        virStoragePoolObjClearVols(obj);
        stateFile = virFileBuildPath(driver->stateDir, def->name, ".xml");
        if (!stateFile ||
            virStoragePoolSaveState(stateFile, def) < 0 ||
            backend->refreshPool(conn, obj) < 0) {
            if (stateFile)
                unlink(stateFile);
            if (backend->stopPool)
                backend->stopPool(conn, obj);
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Failed to autostart storage pool '%s': %s"),
                           def->name, virGetLastErrorMessage());
        } else {
            virPoolObjSetActive(obj, true);
        }
        VIR_FREE(stateFile);
    }

}


static void
storageDriverAutostart(void)
{
    virConnectPtr conn = NULL;

    /* XXX Remove hardcoding of QEMU URI */
    if (driver->privileged)
        conn = virConnectOpen("qemu:///system");
    else
        conn = virConnectOpen("qemu:///session");

    /* Ignoring NULL conn - let backends decide */
    virPoolObjTableIterate(driver->pools, storagePoolDoAutostart, conn);

    virObjectUnref(conn);
}


/**
 * virStorageStartup:
 *
 * Initialization function for the Storage Driver
 */
static int
storageStateInitialize(bool privileged,
                       virStateInhibitCallback callback ATTRIBUTE_UNUSED,
                       void *opaque ATTRIBUTE_UNUSED)
{
    int ret = -1;
    char *configdir = NULL;
    char *rundir = NULL;

    if (VIR_ALLOC(driver) < 0)
        return ret;

    if (virMutexInit(&driver->lock) < 0) {
        VIR_FREE(driver);
        return ret;
    }
    storageDriverLock();

    if (privileged) {
        if (VIR_STRDUP(driver->configDir,
                       SYSCONFDIR "/libvirt/storage") < 0 ||
            VIR_STRDUP(driver->autostartDir,
                       SYSCONFDIR "/libvirt/storage/autostart") < 0 ||
            VIR_STRDUP(driver->stateDir,
                       LOCALSTATEDIR "/run/libvirt/storage") < 0)
            goto error;
    } else {
        configdir = virGetUserConfigDirectory();
        rundir = virGetUserRuntimeDirectory();
        if (!(configdir && rundir))
            goto error;

        if ((virAsprintf(&driver->configDir,
                        "%s/storage", configdir) < 0) ||
            (virAsprintf(&driver->autostartDir,
                        "%s/storage/autostart", configdir) < 0) ||
            (virAsprintf(&driver->stateDir,
                         "%s/storage/run", rundir) < 0))
            goto error;
    }
    driver->privileged = privileged;

    if (virFileMakePath(driver->stateDir) < 0) {
        virReportError(errno,
                       _("cannot create directory %s"),
                       driver->stateDir);
        goto error;
    }

    if (!(driver->pools =
          virPoolObjTableNew(VIR_POOLOBJTABLE_BLOCK_STORAGE,
                             VIR_POOLOBJTABLE_BLOCK_STORAGE_HASHSTART, false)))
        goto error;

    if (virStoragePoolObjLoadAllState(driver->pools,
                                      driver->stateDir) < 0)
        goto error;

    if (virStoragePoolObjLoadAllConfigs(driver->pools,
                                        driver->configDir,
                                        driver->autostartDir) < 0)
        goto error;

    virPoolObjTableIterate(driver->pools, storagePoolUpdateState, NULL);

    driver->storageEventState = virObjectEventStateNew();

    storageDriverUnlock();

    ret = 0;
 cleanup:
    VIR_FREE(configdir);
    VIR_FREE(rundir);
    return ret;

 error:
    storageDriverUnlock();
    storageStateCleanup();
    goto cleanup;
}

/**
 * storageStateAutoStart:
 *
 * Function to auto start the storage driver
 */
static void
storageStateAutoStart(void)
{
    if (!driver)
        return;

    storageDriverLock();
    storageDriverAutostart();
    storageDriverUnlock();
}

/**
 * storageStateReload:
 *
 * Function to restart the storage driver, it will recheck the configuration
 * files and update its state
 */
static int
storageStateReload(void)
{
    if (!driver)
        return -1;

    storageDriverLock();
    virStoragePoolObjLoadAllState(driver->pools,
                                  driver->stateDir);
    virStoragePoolObjLoadAllConfigs(driver->pools,
                                    driver->configDir,
                                    driver->autostartDir);
    storageDriverAutostart();
    storageDriverUnlock();

    return 0;
}


/**
 * storageStateCleanup
 *
 * Shutdown the storage driver, it will stop all active storage pools
 */
static int
storageStateCleanup(void)
{
    if (!driver)
        return -1;

    storageDriverLock();

    virObjectUnref(driver->storageEventState);

    /* free inactive pools */
    virObjectUnref(driver->pools);

    VIR_FREE(driver->configDir);
    VIR_FREE(driver->autostartDir);
    VIR_FREE(driver->stateDir);
    storageDriverUnlock();
    virMutexDestroy(&driver->lock);
    VIR_FREE(driver);

    return 0;
}


static virPoolObjPtr
storagePoolObjFindByUUID(const unsigned char *uuid,
                         const char *name)
{
    virPoolObjPtr obj;
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    if (!(obj = virPoolObjTableFindByUUIDRef(driver->pools, uuid))) {
        virUUIDFormat(uuid, uuidstr);
        if (name)
            virReportError(VIR_ERR_NO_STORAGE_POOL,
                           _("no storage pool with matching uuid '%s' (%s)"),
                           uuidstr, name);
        else
            virReportError(VIR_ERR_NO_STORAGE_POOL,
                           _("no storage pool with matching uuid '%s'"),
                           uuidstr);
    }

    return obj;
}


static virPoolObjPtr
storagePoolObjFromStoragePool(virStoragePoolPtr pool)
{
    return storagePoolObjFindByUUID(pool->uuid, pool->name);
}


static virPoolObjPtr
storagePoolObjFindByName(const char *name)
{
    virPoolObjPtr obj;

    if (!(obj = virPoolObjTableFindByName(driver->pools, name))) {
        virReportError(VIR_ERR_NO_STORAGE_VOL,
                       _("no storage vol with matching name '%s'"), name);
    }

    return obj;
}


static virStoragePoolPtr
storagePoolLookupByUUID(virConnectPtr conn,
                        const unsigned char *uuid)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    virStoragePoolPtr ret = NULL;

    if (!(obj = storagePoolObjFindByUUID(uuid, NULL)))
        return NULL;
    def = virPoolObjGetDef(obj);

    if (virStoragePoolLookupByUUIDEnsureACL(conn, def) < 0)
        goto cleanup;

    ret = virGetStoragePool(conn, def->name, def->uuid, NULL, NULL);

 cleanup:
    virPoolObjEndAPI(&obj);
    return ret;
}


static virStoragePoolPtr
storagePoolLookupByName(virConnectPtr conn,
                        const char *name)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    virStoragePoolPtr ret = NULL;

    if (!(obj = storagePoolObjFindByName(name)))
        return NULL;
    def = virPoolObjGetDef(obj);

    if (virStoragePoolLookupByNameEnsureACL(conn, def) < 0)
        goto cleanup;

    ret = virGetStoragePool(conn, def->name, def->uuid, NULL, NULL);

 cleanup:
    virPoolObjEndAPI(&obj);
    return ret;
}


static virStoragePoolPtr
storagePoolLookupByVolume(virStorageVolPtr volume)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    virStoragePoolPtr ret = NULL;

    if (!(obj = virPoolObjTableFindByName(driver->pools, volume->pool))) {
        virReportError(VIR_ERR_NO_STORAGE_POOL,
                       _("no storage pool with matching name '%s'"),
                       volume->pool);
        return NULL;
    }
    def = virPoolObjGetDef(obj);

    if (virStoragePoolLookupByVolumeEnsureACL(volume->conn, def) < 0)
        goto cleanup;

    ret = virGetStoragePool(volume->conn, def->name, def->uuid, NULL, NULL);

 cleanup:
    virPoolObjEndAPI(&obj);
    return ret;
}


static int
storageConnectNumOfStoragePools(virConnectPtr conn)
{

    if (virConnectNumOfStoragePoolsEnsureACL(conn) < 0)
        return -1;

    return virStoragePoolObjNumOfStoragePools(driver->pools, conn, true,
                                          virConnectNumOfStoragePoolsCheckACL);
}


static int
storageConnectListStoragePools(virConnectPtr conn,
                               char **const names,
                               int maxnames)
{
    if (virConnectListStoragePoolsEnsureACL(conn) < 0)
        return -1;

    return virStoragePoolObjGetNames(driver->pools, conn, true,
                                     virConnectListStoragePoolsCheckACL,
                                     names, maxnames);
}


static int
storageConnectNumOfDefinedStoragePools(virConnectPtr conn)
{
    if (virConnectNumOfDefinedStoragePoolsEnsureACL(conn) < 0)
        return -1;

    return virStoragePoolObjNumOfStoragePools(driver->pools, conn, false,
                                   virConnectNumOfDefinedStoragePoolsCheckACL);
}


static int
storageConnectListDefinedStoragePools(virConnectPtr conn,
                                      char **const names,
                                      int maxnames)
{
    if (virConnectListDefinedStoragePoolsEnsureACL(conn) < 0)
        return -1;

    return virStoragePoolObjGetNames(driver->pools, conn, false,
                                     virConnectListDefinedStoragePoolsCheckACL,
                                     names, maxnames);
}


/* This method is required to be re-entrant / thread safe, so
   uses no driver lock */
static char *
storageConnectFindStoragePoolSources(virConnectPtr conn,
                                     const char *type,
                                     const char *srcSpec,
                                     unsigned int flags)
{
    int backend_type;
    virStorageBackendPtr backend;
    char *ret = NULL;

    if (virConnectFindStoragePoolSourcesEnsureACL(conn) < 0)
        return NULL;

    backend_type = virStoragePoolTypeFromString(type);
    if (backend_type < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("unknown storage pool type %s"), type);
        goto cleanup;
    }

    backend = virStorageBackendForType(backend_type);
    if (backend == NULL)
        goto cleanup;

    if (!backend->findPoolSources) {
        virReportError(VIR_ERR_NO_SUPPORT,
                       _("pool type '%s' does not support source "
                         "discovery"), type);
        goto cleanup;
    }

    ret = backend->findPoolSources(conn, srcSpec, flags);

 cleanup:
    return ret;
}


static int
storagePoolIsActive(virStoragePoolPtr pool)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    int ret = -1;

    if (!(obj = storagePoolObjFromStoragePool(pool)))
        return -1;
    def = virPoolObjGetDef(obj);

    if (virStoragePoolIsActiveEnsureACL(pool->conn, def) < 0)
        goto cleanup;

    ret = virPoolObjIsActive(obj);

 cleanup:
    virPoolObjEndAPI(&obj);
    return ret;
}


static int
storagePoolIsPersistent(virStoragePoolPtr pool)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    int ret = -1;

    if (!(obj = storagePoolObjFromStoragePool(pool)))
        return -1;
    def = virPoolObjGetDef(obj);

    if (virStoragePoolIsPersistentEnsureACL(pool->conn, def) < 0)
        goto cleanup;

    ret = virStoragePoolObjPrivateGetConfigFile(obj) ? 1 : 0;

 cleanup:
    virPoolObjEndAPI(&obj);
    return ret;
}


static virStoragePoolPtr
storagePoolCreateXML(virConnectPtr conn,
                     const char *xml,
                     unsigned int flags)
{
    virStoragePoolDefPtr def;
    virPoolObjPtr obj = NULL;
    virStoragePoolDefPtr pooldef;
    virStoragePoolPtr ret = NULL;
    virStorageBackendPtr backend;
    virObjectEventPtr event = NULL;
    char *stateFile = NULL;
    unsigned int build_flags = 0;

    virCheckFlags(VIR_STORAGE_POOL_CREATE_WITH_BUILD |
                  VIR_STORAGE_POOL_CREATE_WITH_BUILD_OVERWRITE |
                  VIR_STORAGE_POOL_CREATE_WITH_BUILD_NO_OVERWRITE, NULL);

    VIR_EXCLUSIVE_FLAGS_RET(VIR_STORAGE_POOL_BUILD_OVERWRITE,
                            VIR_STORAGE_POOL_BUILD_NO_OVERWRITE, NULL);

    if (!(def = virStoragePoolDefParseString(xml)))
        return NULL;

    if (virStoragePoolCreateXMLEnsureACL(conn, def) < 0)
        goto cleanup;

    if (virStoragePoolObjIsDuplicate(driver->pools, def, 1) < 0)
        goto cleanup;

    if (virStoragePoolObjFindDuplicate(driver->pools, conn, def))
        goto cleanup;

    if ((backend = virStorageBackendForType(def->type)) == NULL)
        goto cleanup;

    if (!(obj = virStoragePoolObjAdd(driver->pools, def)))
        goto cleanup;
    VIR_STEAL_PTR(pooldef, def);

    if (backend->buildPool) {
        if (flags & VIR_STORAGE_POOL_CREATE_WITH_BUILD_OVERWRITE)
            build_flags |= VIR_STORAGE_POOL_BUILD_OVERWRITE;
        else if (flags & VIR_STORAGE_POOL_CREATE_WITH_BUILD_NO_OVERWRITE)
            build_flags |= VIR_STORAGE_POOL_BUILD_NO_OVERWRITE;

        if (build_flags ||
            (flags & VIR_STORAGE_POOL_CREATE_WITH_BUILD)) {
            if (backend->buildPool(conn, obj, build_flags) < 0) {
                virPoolObjTableRemove(driver->pools, &obj);
                goto cleanup;
            }
        }
    }

    if (backend->startPool &&
        backend->startPool(conn, obj) < 0) {
        virPoolObjTableRemove(driver->pools, &obj);
        goto cleanup;
    }

    stateFile = virFileBuildPath(driver->stateDir, pooldef->name, ".xml");

    virStoragePoolObjClearVols(obj);
    if (!stateFile || virStoragePoolSaveState(stateFile, pooldef) < 0 ||
        backend->refreshPool(conn, obj) < 0) {
        if (stateFile)
            unlink(stateFile);
        if (backend->stopPool)
            backend->stopPool(conn, obj);
        virPoolObjTableRemove(driver->pools, &obj);
        goto cleanup;
    }

    event = virStoragePoolEventLifecycleNew(pooldef->name,
                                            pooldef->uuid,
                                            VIR_STORAGE_POOL_EVENT_STARTED,
                                            0);

    VIR_INFO("Creating storage pool '%s'", pooldef->name);
    virPoolObjSetActive(obj, true);

    ret = virGetStoragePool(conn, pooldef->name, pooldef->uuid, NULL, NULL);

 cleanup:
    VIR_FREE(stateFile);
    virStoragePoolDefFree(def);
    if (event)
        virObjectEventStateQueue(driver->storageEventState, event);
    virPoolObjEndAPI(&obj);
    return ret;
}


static virStoragePoolPtr
storagePoolDefineXML(virConnectPtr conn,
                     const char *xml,
                     unsigned int flags)
{
    virStoragePoolDefPtr def;
    virPoolObjPtr obj = NULL;
    virStoragePoolDefPtr pooldef;
    virStoragePoolPtr ret = NULL;
    virObjectEventPtr event = NULL;

    virCheckFlags(0, NULL);

    if (!(def = virStoragePoolDefParseString(xml)))
        return NULL;

    if (virXMLCheckIllegalChars("name", def->name, "\n") < 0)
        goto cleanup;

    if (virStoragePoolDefineXMLEnsureACL(conn, def) < 0)
        goto cleanup;

    if (virStoragePoolObjIsDuplicate(driver->pools, def, 0) < 0)
        goto cleanup;

    if (virStoragePoolObjFindDuplicate(driver->pools, conn, def))
        goto cleanup;

    if (virStorageBackendForType(def->type) == NULL)
        goto cleanup;

    if (!(obj = virStoragePoolObjAdd(driver->pools, def)))
        goto cleanup;
    VIR_STEAL_PTR(pooldef, def);

    if (virStoragePoolObjSaveDef(driver, obj) < 0) {
        virPoolObjTableRemove(driver->pools, &obj);
        goto cleanup;
    }

    event = virStoragePoolEventLifecycleNew(pooldef->name, pooldef->uuid,
                                            VIR_STORAGE_POOL_EVENT_DEFINED,
                                            0);

    VIR_INFO("Defining storage pool '%s'", pooldef->name);
    ret = virGetStoragePool(conn, pooldef->name, pooldef->uuid, NULL, NULL);

 cleanup:
    if (event)
        virObjectEventStateQueue(driver->storageEventState, event);
    virStoragePoolDefFree(def);
    virPoolObjEndAPI(&obj);
    return ret;
}


static int
storagePoolUndefine(virStoragePoolPtr pool)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    virObjectEventPtr event = NULL;
    int ret = -1;

    if (!(obj = storagePoolObjFromStoragePool(pool)))
        return -1;
    def = virPoolObjGetDef(obj);

    if (virStoragePoolUndefineEnsureACL(pool->conn, def) < 0)
        goto cleanup;

    if (virPoolObjIsActive(obj)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is still active"),
                       def->name);
        goto cleanup;
    }

    if (virStoragePoolObjPrivateGetAsyncjobs(obj) > 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("pool '%s' has asynchronous jobs running."),
                       def->name);
        goto cleanup;
    }

    if (virStoragePoolObjDeleteDef(obj) < 0)
        goto cleanup;

    event = virStoragePoolEventLifecycleNew(def->name,
                                            def->uuid,
                                            VIR_STORAGE_POOL_EVENT_UNDEFINED,
                                            0);

    VIR_INFO("Undefining storage pool '%s'", def->name);
    virPoolObjTableRemove(driver->pools, &obj);
    ret = 0;

 cleanup:
    if (event)
        virObjectEventStateQueue(driver->storageEventState, event);
    virPoolObjEndAPI(&obj);
    return ret;
}


static int
storagePoolCreate(virStoragePoolPtr pool,
                  unsigned int flags)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    virStorageBackendPtr backend;
    virObjectEventPtr event = NULL;
    int ret = -1;
    char *stateFile = NULL;
    unsigned int build_flags = 0;

    virCheckFlags(VIR_STORAGE_POOL_CREATE_WITH_BUILD |
                  VIR_STORAGE_POOL_CREATE_WITH_BUILD_OVERWRITE |
                  VIR_STORAGE_POOL_CREATE_WITH_BUILD_NO_OVERWRITE, -1);

    VIR_EXCLUSIVE_FLAGS_RET(VIR_STORAGE_POOL_BUILD_OVERWRITE,
                            VIR_STORAGE_POOL_BUILD_NO_OVERWRITE, -1);

    if (!(obj = storagePoolObjFromStoragePool(pool)))
        return -1;
    def = virPoolObjGetDef(obj);

    if (virStoragePoolCreateEnsureACL(pool->conn, def) < 0)
        goto cleanup;

    if ((backend = virStorageBackendForType(def->type)) == NULL)
        goto cleanup;

    if (virPoolObjIsActive(obj)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is already active"),
                       def->name);
        goto cleanup;
    }

    if (backend->buildPool) {
        if (flags & VIR_STORAGE_POOL_CREATE_WITH_BUILD_OVERWRITE)
            build_flags |= VIR_STORAGE_POOL_BUILD_OVERWRITE;
        else if (flags & VIR_STORAGE_POOL_CREATE_WITH_BUILD_NO_OVERWRITE)
            build_flags |= VIR_STORAGE_POOL_BUILD_NO_OVERWRITE;

        if (build_flags ||
            (flags & VIR_STORAGE_POOL_CREATE_WITH_BUILD)) {
            if (backend->buildPool(pool->conn, obj, build_flags) < 0)
                goto cleanup;
        }
    }

    VIR_INFO("Starting up storage pool '%s'", def->name);
    if (backend->startPool &&
        backend->startPool(pool->conn, obj) < 0)
        goto cleanup;

    stateFile = virFileBuildPath(driver->stateDir, def->name, ".xml");

    virStoragePoolObjClearVols(obj);
    if (!stateFile || virStoragePoolSaveState(stateFile, def) < 0 ||
        backend->refreshPool(pool->conn, obj) < 0) {
        if (stateFile)
            unlink(stateFile);
        if (backend->stopPool)
            backend->stopPool(pool->conn, obj);
        goto cleanup;
    }

    event = virStoragePoolEventLifecycleNew(def->name,
                                            def->uuid,
                                            VIR_STORAGE_POOL_EVENT_STARTED,
                                            0);

    virPoolObjSetActive(obj, true);
    ret = 0;

 cleanup:
    VIR_FREE(stateFile);
    if (event)
        virObjectEventStateQueue(driver->storageEventState, event);
    virPoolObjEndAPI(&obj);
    return ret;
}


static int
storagePoolBuild(virStoragePoolPtr pool,
                 unsigned int flags)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    virStorageBackendPtr backend;
    int ret = -1;

    if (!(obj = storagePoolObjFromStoragePool(pool)))
        return -1;
    def = virPoolObjGetDef(obj);

    if (virStoragePoolBuildEnsureACL(pool->conn, def) < 0)
        goto cleanup;

    if ((backend = virStorageBackendForType(def->type)) == NULL)
        goto cleanup;

    if (virPoolObjIsActive(obj)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is already active"),
                       def->name);
        goto cleanup;
    }

    if (backend->buildPool &&
        backend->buildPool(pool->conn, obj, flags) < 0)
        goto cleanup;
    ret = 0;

 cleanup:
    virPoolObjEndAPI(&obj);
    return ret;
}


static int
storagePoolDestroy(virStoragePoolPtr pool)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    virStoragePoolDefPtr newDef;
    virStorageBackendPtr backend;
    virObjectEventPtr event = NULL;
    char *stateFile = NULL;
    int ret = -1;

    if (!(obj = storagePoolObjFromStoragePool(pool)))
        return -1;
    def = virPoolObjGetDef(obj);
    newDef = virPoolObjGetNewDef(obj);

    if (virStoragePoolDestroyEnsureACL(pool->conn, def) < 0)
        goto cleanup;

    if ((backend = virStorageBackendForType(def->type)) == NULL)
        goto cleanup;

    VIR_INFO("Destroying storage pool '%s'", def->name);

    if (!virPoolObjIsActive(obj)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), def->name);
        goto cleanup;
    }

    if (virStoragePoolObjPrivateGetAsyncjobs(obj) > 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("pool '%s' has asynchronous jobs running."),
                       def->name);
        goto cleanup;
    }

    if (!(stateFile = virFileBuildPath(driver->stateDir, def->name, ".xml")))
        goto cleanup;

    unlink(stateFile);
    VIR_FREE(stateFile);

    if (backend->stopPool &&
        backend->stopPool(pool->conn, obj) < 0)
        goto cleanup;

    virStoragePoolObjClearVols(obj);

    event = virStoragePoolEventLifecycleNew(def->name,
                                            def->uuid,
                                            VIR_STORAGE_POOL_EVENT_STOPPED,
                                            0);

    virPoolObjSetActive(obj, false);

    if (!virStoragePoolObjPrivateGetConfigFile(obj))
        virPoolObjTableRemove(driver->pools, &obj);
    else if (newDef)
        virPoolObjSetDef(obj, newDef);

    ret = 0;

 cleanup:
    if (event)
        virObjectEventStateQueue(driver->storageEventState, event);
    virPoolObjEndAPI(&obj);
    return ret;
}


static int
storagePoolDelete(virStoragePoolPtr pool,
                  unsigned int flags)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    virStorageBackendPtr backend;
    char *stateFile = NULL;
    int ret = -1;

    if (!(obj = storagePoolObjFromStoragePool(pool)))
        return -1;
    def = virPoolObjGetDef(obj);

    if (virStoragePoolDeleteEnsureACL(pool->conn, def) < 0)
        goto cleanup;

    if ((backend = virStorageBackendForType(def->type)) == NULL)
        goto cleanup;

    VIR_INFO("Deleting storage pool '%s'", def->name);

    if (virPoolObjIsActive(obj)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is still active"),
                       def->name);
        goto cleanup;
    }

    if (virStoragePoolObjPrivateGetAsyncjobs(obj) > 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("pool '%s' has asynchronous jobs running."),
                       def->name);
        goto cleanup;
    }

    if (!(stateFile = virFileBuildPath(driver->stateDir, def->name, ".xml")))
        goto cleanup;

    unlink(stateFile);
    VIR_FREE(stateFile);

    if (!backend->deletePool) {
        virReportError(VIR_ERR_NO_SUPPORT,
                       "%s", _("pool does not support pool deletion"));
        goto cleanup;
    }
    if (backend->deletePool(pool->conn, obj, flags) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virPoolObjEndAPI(&obj);
    return ret;
}


static int
storagePoolRefresh(virStoragePoolPtr pool,
                   unsigned int flags)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    virStorageBackendPtr backend;
    int ret = -1;
    virObjectEventPtr event = NULL;

    virCheckFlags(0, -1);

    if (!(obj = storagePoolObjFromStoragePool(pool)))
        return -1;
    def = virPoolObjGetDef(obj);

    if (virStoragePoolRefreshEnsureACL(pool->conn, def) < 0)
        goto cleanup;

    if ((backend = virStorageBackendForType(def->type)) == NULL)
        goto cleanup;

    if (!virPoolObjIsActive(obj)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), def->name);
        goto cleanup;
    }

    if (virStoragePoolObjPrivateGetAsyncjobs(obj) > 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("pool '%s' has asynchronous jobs running."),
                       def->name);
        goto cleanup;
    }

    virStoragePoolObjClearVols(obj);
    if (backend->refreshPool(pool->conn, obj) < 0) {
        if (backend->stopPool)
            backend->stopPool(pool->conn, obj);

        event = virStoragePoolEventLifecycleNew(def->name,
                                                def->uuid,
                                                VIR_STORAGE_POOL_EVENT_STOPPED,
                                                0);
        virPoolObjSetActive(obj, false);

        if (!virStoragePoolObjPrivateGetConfigFile(obj))
            virPoolObjTableRemove(driver->pools, &obj);
        goto cleanup;
    }

    event = virStoragePoolEventRefreshNew(def->name, def->uuid);
    ret = 0;

 cleanup:
    if (event)
        virObjectEventStateQueue(driver->storageEventState, event);
    virPoolObjEndAPI(&obj);
    return ret;
}


static int
storagePoolGetInfo(virStoragePoolPtr pool,
                   virStoragePoolInfoPtr info)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    int ret = -1;

    if (!(obj = storagePoolObjFromStoragePool(pool)))
        return -1;
    def = virPoolObjGetDef(obj);

    if (virStoragePoolGetInfoEnsureACL(pool->conn, def) < 0)
        goto cleanup;

    if (virStorageBackendForType(def->type) == NULL)
        goto cleanup;

    memset(info, 0, sizeof(virStoragePoolInfo));
    if (virPoolObjIsActive(obj))
        info->state = VIR_STORAGE_POOL_RUNNING;
    else
        info->state = VIR_STORAGE_POOL_INACTIVE;
    info->capacity = def->capacity;
    info->allocation = def->allocation;
    info->available = def->available;
    ret = 0;

 cleanup:
    virPoolObjEndAPI(&obj);
    return ret;
}


static char *
storagePoolGetXMLDesc(virStoragePoolPtr pool,
                      unsigned int flags)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    virStoragePoolDefPtr newDef;
    char *ret = NULL;

    virCheckFlags(VIR_STORAGE_XML_INACTIVE, NULL);

    if (!(obj = storagePoolObjFromStoragePool(pool)))
        return NULL;
    def = virPoolObjGetDef(obj);
    newDef = virPoolObjGetNewDef(obj);

    if (virStoragePoolGetXMLDescEnsureACL(pool->conn, def) < 0)
        goto cleanup;

    if ((flags & VIR_STORAGE_XML_INACTIVE) && newDef)
        def = newDef;

    ret = virStoragePoolDefFormat(def);

 cleanup:
    virPoolObjEndAPI(&obj);
    return ret;
}


static int
storagePoolGetAutostart(virStoragePoolPtr pool,
                        int *autostart)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    int ret = -1;

    if (!(obj = storagePoolObjFromStoragePool(pool)))
        return -1;
    def = virPoolObjGetDef(obj);

    if (virStoragePoolGetAutostartEnsureACL(pool->conn, def) < 0)
        goto cleanup;

    *autostart = 0;
    if (virStoragePoolObjPrivateGetConfigFile(obj) &&
        virPoolObjIsAutostart(obj))
        *autostart = 1;

    ret = 0;

 cleanup:
    virPoolObjEndAPI(&obj);
    return ret;
}


static int
storagePoolSetAutostart(virStoragePoolPtr pool,
                        int new_autostart)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    bool autostart;
    const char *configFile;
    int ret = -1;

    if (!(obj = storagePoolObjFromStoragePool(pool)))
        return -1;
    def = virPoolObjGetDef(obj);
    configFile = virStoragePoolObjPrivateGetConfigFile(obj);

    if (virStoragePoolSetAutostartEnsureACL(pool->conn, def) < 0)
        goto cleanup;

    if (!configFile) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("pool has no config file"));
        goto cleanup;
    }

    autostart = (new_autostart != 0);

    if (virPoolObjIsAutostart(obj) != autostart) {
        const char *autostartLink =
            virStoragePoolObjPrivateGetAutostartLink(obj);
        if (autostart) {
            if (virFileMakePath(driver->autostartDir) < 0) {
                virReportSystemError(errno,
                                     _("cannot create autostart directory %s"),
                                     driver->autostartDir);
                goto cleanup;
            }

            if (symlink(configFile, autostartLink) < 0) {
                virReportSystemError(errno,
                                     _("Failed to create symlink '%s' to '%s'"),
                                     autostartLink, configFile);
                goto cleanup;
            }
        } else {
            if (unlink(autostartLink) < 0 &&
                errno != ENOENT && errno != ENOTDIR) {
                virReportSystemError(errno,
                                     _("Failed to delete symlink '%s'"),
                                     autostartLink);
                goto cleanup;
            }
        }
        virPoolObjSetAutostart(obj, autostart);
    }
    ret = 0;

 cleanup:
    virPoolObjEndAPI(&obj);
    return ret;
}


static int
storagePoolNumOfVolumes(virStoragePoolPtr pool)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    virPoolObjTablePtr objvolumes;
    int ret = -1;

    if (!(obj = storagePoolObjFromStoragePool(pool)))
        return -1;
    def = virPoolObjGetDef(obj);
    objvolumes = virStoragePoolObjPrivateGetVolumes(obj);

    if (virStoragePoolNumOfVolumesEnsureACL(pool->conn, def) < 0)
        goto cleanup;

    if (!virPoolObjIsActive(obj)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), def->name);
        goto cleanup;
    }

    /* NB: Cannot call filter in List function since the volume ACL filter
     * function requires 3 instead of 2 params for virPoolObjACLFilter.
     * Setting up this way fulfills check-aclrules.pl's check that the
     * driver function calls the CheckACL API */
    ret = virStoragePoolObjNumOfVolumes(objvolumes, pool->conn, def,
                                        virStoragePoolNumOfVolumesCheckACL);

 cleanup:
    virPoolObjEndAPI(&obj);
    return ret;
}


static int
storagePoolListVolumes(virStoragePoolPtr pool,
                       char **const names,
                       int maxnames)
{
    int ret = -1;
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    virPoolObjTablePtr objvolumes;

    memset(names, 0, maxnames * sizeof(*names));

    if (!(obj = storagePoolObjFromStoragePool(pool)))
        return -1;
    def = virPoolObjGetDef(obj);
    objvolumes = virStoragePoolObjPrivateGetVolumes(obj);

    if (virStoragePoolListVolumesEnsureACL(pool->conn, def) < 0)
        goto cleanup;

    if (!virPoolObjIsActive(obj)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), def->name);
        goto cleanup;
    }

    /* NB: Cannot call filter in List function since the volume ACL filter
     * function requires 3 instead of 2 params for virPoolObjACLFilter.
     * Setting up this way fulfills check-aclrules.pl's check that the
     * driver function calls the CheckACL API */
    ret = virStoragePoolObjListVolumes(objvolumes, pool->conn, def,
                                       virStoragePoolListVolumesCheckACL,
                                       names, maxnames);

 cleanup:
    virPoolObjEndAPI(&obj);
    return ret;
}


static int
storagePoolListAllVolumes(virStoragePoolPtr pool,
                          virStorageVolPtr **volumes,
                          unsigned int flags)
{
    virPoolObjPtr obj;
    virStoragePoolDefPtr def;
    virPoolObjTablePtr objvolumes;
    virPoolObjPtr *volobjs = NULL;
    size_t nvolobjs = 0;
    size_t i;
    virStorageVolPtr *vols = NULL;
    int nvols = 0;
    int ret = -1;

    virCheckFlags(0, -1);

    if (!(obj = storagePoolObjFromStoragePool(pool)))
        return -1;
    def = virPoolObjGetDef(obj);
    objvolumes = virStoragePoolObjPrivateGetVolumes(obj);

    if (virStoragePoolListAllVolumesEnsureACL(pool->conn, def) < 0)
        goto cleanup;

    if (!virPoolObjIsActive(obj)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), def->name);
        goto cleanup;
    }

    if (virPoolObjTableCollect(objvolumes, pool->conn, &volobjs, &nvolobjs,
                               NULL, NULL, flags) < 0)
        goto cleanup;

    if (volumes) {
        if (VIR_ALLOC_N(vols, nvolobjs + 1) < 0)
            goto cleanup;

        for (i = 0; i < nvolobjs; i++) {
            bool passacl = false;
            virPoolObjPtr volobj = volobjs[i];
            virStorageVolDefPtr voldef;

            virObjectLock(volobj);
            voldef = virPoolObjGetDef(volobj);
            /* NB: Cannot call ACL filter in Collect function since it
             * takes 3 params and the volume ACL filter requires 3 */
            if (virStoragePoolListAllVolumesCheckACL(pool->conn, def, obj)) {
                vols[nvols++] = virGetStorageVol(pool->conn, def->name,
                                                 voldef->name, voldef->key,
                                                 NULL, NULL);
                passacl = true;
            }
            virObjectUnlock(volobj);

            if (passacl && !vols[i])
                goto cleanup;
        }

        *volumes = vols;
        vols = NULL;
    }

    ret = nvols;

 cleanup:
    virObjectListFree(vols);
    virObjectListFreeCount(volobjs, nvolobjs);
    virPoolObjEndAPI(&obj);
    return ret;
}


static virStorageVolPtr
storageVolLookupByName(virStoragePoolPtr pool,
                       const char *name)
{
    virPoolObjPtr poolobj;
    virStoragePoolDefPtr pooldef;
    virPoolObjPtr volobj = NULL;
    virStorageVolDefPtr voldef;
    virStorageVolPtr ret = NULL;

    if (!(poolobj = storagePoolObjFromStoragePool(pool)))
        return NULL;
    pooldef = virPoolObjGetDef(poolobj);

    if (!virPoolObjIsActive(poolobj)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), pooldef->name);
        goto cleanup;
    }

    if (!(volobj = virStorageVolObjFindByName(poolobj, name))) {
        virReportError(VIR_ERR_NO_STORAGE_VOL,
                       _("no storage vol with matching name '%s'"),
                       name);
        goto cleanup;
    }
    voldef = virPoolObjGetDef(volobj);

    if (virStorageVolLookupByNameEnsureACL(pool->conn, pooldef, voldef) < 0)
        goto cleanup;

    ret = virGetStorageVol(pool->conn, pooldef->name, voldef->name, voldef->key,
                           NULL, NULL);

 cleanup:
    virPoolObjEndAPI(&volobj);
    virPoolObjEndAPI(&poolobj);
    return ret;
}


/* NB: Cannot use the "real" name since check-aclrules.pl will get confused */
typedef int (*ensureFilter)(virConnectPtr conn,
                            virStoragePoolDefPtr pool,
                            void *opaque);
struct storageSearchData {
    virConnectPtr conn;
    ensureFilter aclfilter;
    const char *key;
    const char *path;
    char *retname;
    char *retkey;
};

static bool
storageVolSearchByKey(virPoolObjPtr obj,
                      void *opaque)
{
    virStoragePoolDefPtr def = virPoolObjGetDef(obj);
    struct storageSearchData *data = opaque;

    if (virPoolObjIsActive(obj)) {
        virPoolObjPtr volobj;

        if ((volobj = virStorageVolObjFindByKey(obj, data->key))) {
            virStorageVolDefPtr voldef = virPoolObjGetDef(volobj);

            if (data->aclfilter(data->conn, def, voldef) == 0)
                ignore_value(VIR_STRDUP(data->retname, def->name));
            virPoolObjEndAPI(&volobj);
        }

        if (data->retname)
            return true;
    }

    return false;
}


static virStorageVolPtr
storageVolLookupByKey(virConnectPtr conn,
                      const char *key)
{
    virStorageVolPtr ret = NULL;
    virPoolObjPtr obj;
    struct storageSearchData data =
           { .conn = conn,
             .aclfilter = virStorageVolLookupByKeyEnsureACL,
             .key = key,
             .retname = NULL };

    if ((obj = virPoolObjTableSearchRef(driver->pools, storageVolSearchByKey,
                                        &data))) {
        virStoragePoolDefPtr def = virPoolObjGetDef(obj);

        ret = virGetStorageVol(conn, def->name, data.retname, key, NULL, NULL);
        VIR_FREE(data.retname);
        virPoolObjEndAPI(&obj);
    }

    if (!ret)
        virReportError(VIR_ERR_NO_STORAGE_VOL,
                       _("no storage vol with matching key %s"), key);

    return ret;
}


static bool
storageVolSearchByPath(virPoolObjPtr obj,
                       void *opaque)
{
    virStoragePoolDefPtr pooldef = virPoolObjGetDef(obj);
    struct storageSearchData *data = opaque;

    if (virPoolObjIsActive(obj)) {
        virPoolObjPtr volobj;
        char *stable_path = NULL;

        switch ((virStoragePoolType) pooldef->type) {
            case VIR_STORAGE_POOL_DIR:
            case VIR_STORAGE_POOL_FS:
            case VIR_STORAGE_POOL_NETFS:
            case VIR_STORAGE_POOL_LOGICAL:
            case VIR_STORAGE_POOL_DISK:
            case VIR_STORAGE_POOL_ISCSI:
            case VIR_STORAGE_POOL_SCSI:
            case VIR_STORAGE_POOL_MPATH:
            case VIR_STORAGE_POOL_VSTORAGE:
                if (!(stable_path = virStorageBackendStablePath(obj, data->path,
                                                                false))) {
                    /* Don't break the whole lookup process if it fails on
                     * getting the stable path for some of the pools.
                     */
                    VIR_WARN("Failed to get stable path for pool '%s'",
                             pooldef->name);
                    return false;
                }
                break;

            case VIR_STORAGE_POOL_GLUSTER:
            case VIR_STORAGE_POOL_RBD:
            case VIR_STORAGE_POOL_SHEEPDOG:
            case VIR_STORAGE_POOL_ZFS:
            case VIR_STORAGE_POOL_LAST:
                if (VIR_STRDUP(stable_path, data->path) < 0)
                    return false;
                break;
        }

        if ((volobj = virStorageVolObjFindByPath(obj, stable_path))) {
            virStorageVolDefPtr voldef = virPoolObjGetDef(volobj);

            if (data->aclfilter(data->conn, pooldef, voldef) == 0) {
                ignore_value(VIR_STRDUP(data->retname, voldef->name));
                ignore_value(VIR_STRDUP(data->retkey, voldef->key));
            }
            virPoolObjEndAPI(&volobj);
        }

        if (data->retname && data->retkey)
            return true;

        VIR_FREE(data->retname);
        VIR_FREE(data->retkey);
    }

    return false;
}


static virStorageVolPtr
storageVolLookupByPath(virConnectPtr conn,
                       const char *path)
{
    virStorageVolPtr ret = NULL;
    virPoolObjPtr obj;
    struct storageSearchData data =
           { .conn = conn,
             .aclfilter = virStorageVolLookupByPathEnsureACL,
             .retname = NULL,
             .retkey = NULL };
    char *cleanpath;

    if (!(cleanpath = virFileSanitizePath(path)))
        return NULL;
    data.path = cleanpath;

    if ((obj = virPoolObjTableSearchRef(driver->pools, storageVolSearchByPath,
                                        &data))) {
        virStoragePoolDefPtr def = virPoolObjGetDef(obj);
        ret = virGetStorageVol(conn, def->name, data.retname, data.retkey,
                               NULL, NULL);
        VIR_FREE(data.retname);
        VIR_FREE(data.retkey);
        virPoolObjEndAPI(&obj);
    }

    if (!ret) {
        if (STREQ(path, cleanpath)) {
            virReportError(VIR_ERR_NO_STORAGE_VOL,
                           _("no storage vol with matching path '%s'"), path);
        } else {
            virReportError(VIR_ERR_NO_STORAGE_VOL,
                           _("no storage vol with matching path '%s' (%s)"),
                           path, cleanpath);
        }
    }

    VIR_FREE(cleanpath);
    return ret;
}


static bool
storagePoolSearchByTargetPath(virPoolObjPtr obj,
                              void *opaque)
{
    virStoragePoolDefPtr def = virPoolObjGetDef(obj);
    struct storageSearchData *data = opaque;

    if (virPoolObjIsActive(obj)) {
        if (STREQ(data->path, def->target.path))
            return true;
    }

    return false;
}


virStoragePoolPtr
storagePoolLookupByTargetPath(virConnectPtr conn,
                              const char *path)
{
    virStoragePoolPtr ret = NULL;
    virPoolObjPtr obj;
    struct storageSearchData data = { 0 };
    char *cleanpath;

    if (!(cleanpath = virFileSanitizePath(path)))
        return NULL;
    data.path = cleanpath;

    if ((obj = virPoolObjTableSearchRef(driver->pools,
                                        storagePoolSearchByTargetPath,
                                        &data))) {
        virStoragePoolDefPtr def = virPoolObjGetDef(obj);

        ret = virGetStoragePool(conn, def->name, def->uuid, NULL, NULL);
        virPoolObjEndAPI(&obj);
    }


    if (!ret) {
        virReportError(VIR_ERR_NO_STORAGE_VOL,
                       _("no storage pool with matching target path '%s'"),
                       path);
    }

    VIR_FREE(cleanpath);
    return ret;
}


static void
storageVolRemoveFromPool(virPoolObjPtr poolobj,
                         virPoolObjPtr *volobj)
{
    virStorageVolDefPtr voldef = virPoolObjGetDef(*volobj);

    VIR_INFO("Deleting volume '%s' from storage pool '%s'",
              voldef->name, voldef->name);

    virStoragePoolObjRemoveVolume(poolobj, volobj);
}


static int
storageVolDeleteInternal(virStorageVolPtr volume,
                         virStorageBackendPtr backend,
                         virPoolObjPtr poolobj,
                         virPoolObjPtr *volobj,
                         unsigned int flags,
                         bool updateMeta)
{
    virStoragePoolDefPtr pooldef = virPoolObjGetDef(poolobj);
    virStorageVolDefPtr voldef = virPoolObjGetDef(*volobj);
    int ret = -1;

    if (!backend->deleteVol) {
        virReportError(VIR_ERR_NO_SUPPORT,
                       "%s", _("storage pool does not support vol deletion"));

        goto cleanup;
    }

    if (backend->deleteVol(volume->conn, poolobj, voldef, flags) < 0)
        goto cleanup;

    /* Update pool metadata - don't update meta data from error paths
     * in this module since the allocation/available weren't adjusted yet.
     * Ignore the disk backend since it updates the pool values.
     */
    if (updateMeta) {
        if (pooldef->type != VIR_STORAGE_POOL_DISK) {
            pooldef->allocation -= voldef->target.allocation;
            pooldef->available += voldef->target.allocation;
        }
    }

    storageVolRemoveFromPool(poolobj, volobj);
    ret = 0;

 cleanup:
    return ret;
}


static virPoolObjPtr
virStorageVolObjFromVol(virStorageVolPtr volume,
                        virPoolObjPtr *pool,
                        virStorageBackendPtr *backend)
{
    virPoolObjPtr poolobj;
    virStoragePoolDefPtr pooldef;
    virPoolObjPtr volobj;

    *pool = NULL;

    if (!(poolobj = storagePoolObjFindByName(volume->pool)))
        return NULL;
    pooldef = virPoolObjGetDef(poolobj);

    if (!virPoolObjIsActive(poolobj)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"),
                       pooldef->name);
        goto error;
    }

    if (!(volobj = virStorageVolObjFindByName(poolobj, volume->name))) {
        virReportError(VIR_ERR_NO_STORAGE_VOL,
                       _("no storage vol with matching name '%s'"),
                       volume->name);
        goto error;
    }

    if (backend) {
        if (!(*backend = virStorageBackendForType(pooldef->type)))
            goto error;
    }

    *pool = poolobj;
    return volobj;

 error:
    virPoolObjEndAPI(&poolobj);

    return NULL;
}


static int
storageVolDelete(virStorageVolPtr volume,
                 unsigned int flags)
{
    virPoolObjPtr poolobj;
    virStoragePoolDefPtr pooldef;
    virPoolObjPtr volobj;
    virStorageVolDefPtr voldef;
    virStorageBackendPtr backend;
    int ret = -1;

    if (!(volobj = virStorageVolObjFromVol(volume, &poolobj, &backend)))
        return -1;
    pooldef = virPoolObjGetDef(poolobj);
    voldef = virPoolObjGetDef(volobj);

    if (virStorageVolDeleteEnsureACL(volume->conn, pooldef, voldef) < 0)
        goto cleanup;

    if (voldef->in_use) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("volume '%s' is still in use."),
                       voldef->name);
        goto cleanup;
    }

    if (voldef->building) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("volume '%s' is still being allocated."),
                       voldef->name);
        goto cleanup;
    }

    if (storageVolDeleteInternal(volume, backend, poolobj, &volobj,
                                 flags, true) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virPoolObjEndAPI(&volobj);
    virPoolObjEndAPI(&poolobj);
    return ret;
}


static virStorageVolPtr
storageVolCreateXML(virStoragePoolPtr pool,
                    const char *xmldesc,
                    unsigned int flags)
{
    virPoolObjPtr poolobj;
    virStoragePoolDefPtr pooldef;
    virStorageBackendPtr backend;
    virStorageVolDefPtr voldef = NULL;
    virStorageVolDefPtr objvoldef;
    virPoolObjPtr volobj = NULL;
    virStorageVolPtr volume = NULL;
    virStorageVolPtr ret = NULL;

    virCheckFlags(VIR_STORAGE_VOL_CREATE_PREALLOC_METADATA, NULL);

    if (!(poolobj = storagePoolObjFromStoragePool(pool)))
        return NULL;
    pooldef = virPoolObjGetDef(poolobj);

    if (!virPoolObjIsActive(poolobj)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), pooldef->name);
        goto cleanup;
    }

    if ((backend = virStorageBackendForType(pooldef->type)) == NULL)
        goto cleanup;

    if (!(voldef = virStorageVolDefParseString(pooldef, xmldesc,
                                               VIR_VOL_XML_PARSE_OPT_CAPACITY)))
        goto cleanup;

    if (!voldef->target.capacity && !backend->buildVol) {
        virReportError(VIR_ERR_NO_SUPPORT,
                       "%s", _("volume capacity required for this "
                               "storage pool"));
        goto cleanup;
    }

    if (virStorageVolCreateXMLEnsureACL(pool->conn, pooldef, voldef) < 0)
        goto cleanup;

    if ((volobj = virStorageVolObjFindByName(poolobj, voldef->name))) {
        virReportError(VIR_ERR_STORAGE_VOL_EXIST,
                       _("'%s'"), voldef->name);
        goto cleanup;
    }

    if (!backend->createVol) {
        virReportError(VIR_ERR_NO_SUPPORT,
                       "%s", _("storage pool does not support volume "
                               "creation"));
        goto cleanup;
    }

    /* Wipe any key the user may have suggested, as volume creation
     * will generate the canonical key.  */
    VIR_FREE(voldef->key);
    if (backend->createVol(pool->conn, poolobj, voldef) < 0)
        goto cleanup;

    if (!(volobj = virStoragePoolObjAddVolume(poolobj, voldef)))
        goto cleanup;
    VIR_STEAL_PTR(objvoldef, voldef);

    if (!(volume = virGetStorageVol(pool->conn, pooldef->name, objvoldef->name,
                                    objvoldef->key, NULL, NULL))) {
        storageVolRemoveFromPool(poolobj, &volobj);
        goto cleanup;
    }

    if (backend->buildVol) {
        int buildret;
        virStorageVolDefPtr buildvoldef = NULL;

        if (VIR_ALLOC(buildvoldef) < 0)
            goto cleanup;

        /* Make a shallow copy of the 'defined' volume definition, since the
         * original allocation value will change as the user polls 'info',
         * but we only need the initial requested values
         */
        memcpy(buildvoldef, objvoldef, sizeof(*objvoldef));

        /* Drop the pool lock during volume allocation */
        virStoragePoolObjPrivateIncrAsyncjobs(poolobj);
        objvoldef->building = true;
        virObjectUnlock(volobj);

        buildret = backend->buildVol(pool->conn, volobj, buildvoldef, flags);

        VIR_FREE(buildvoldef);

        virObjectLock(volobj);

        objvoldef->building = false;
        virStoragePoolObjPrivateDecrAsyncjobs(poolobj);

        if (buildret < 0) {
            /* buildVol handles deleting volume on failure */
            storageVolRemoveFromPool(poolobj, &volobj);
            goto cleanup;
        }

    }

    if (backend->refreshVol &&
        backend->refreshVol(pool->conn, poolobj, objvoldef) < 0) {
        storageVolDeleteInternal(volume, backend, poolobj, &volobj,
                                 0, false);
        goto cleanup;
    }

    /* Update pool metadata ignoring the disk backend since
     * it updates the pool values.
     */
    if (pooldef->type != VIR_STORAGE_POOL_DISK) {
        pooldef->allocation += objvoldef->target.allocation;
        pooldef->available -= objvoldef->target.allocation;
    }

    VIR_INFO("Creating volume '%s' in storage pool '%s'",
             volume->name, pooldef->name);
    ret = volume;
    volume = NULL;

 cleanup:
    virPoolObjEndAPI(&volobj);
    virObjectUnref(volume);
    virStorageVolDefFree(voldef);
    virPoolObjEndAPI(&poolobj);
    return ret;
}


static virStorageVolPtr
storageVolCreateXMLFrom(virStoragePoolPtr pool,
                        const char *xmldesc,
                        virStorageVolPtr volume,
                        unsigned int flags)
{
    virPoolObjPtr poolobj;
    virStoragePoolDefPtr pooldef;
    virPoolObjPtr origpoolobj = NULL;
    virStoragePoolDefPtr origpooldef;
    virStorageBackendPtr backend;
    virPoolObjPtr newvolobj = NULL;
    virPoolObjPtr origvolobj = NULL;
    virStorageVolDefPtr origvoldef;
    virStorageVolDefPtr newvoldef = NULL;
    virStorageVolDefPtr objnewvoldef;
    virStorageVolDefPtr shadowvoldef = NULL;
    virStorageVolPtr vol = NULL;
    virStorageVolPtr ret = NULL;
    int buildret;

    virCheckFlags(VIR_STORAGE_VOL_CREATE_PREALLOC_METADATA |
                  VIR_STORAGE_VOL_CREATE_REFLINK,
                  NULL);

    if (!(poolobj = storagePoolObjFromStoragePool(pool)))
        return NULL;
    pooldef = virPoolObjGetDef(poolobj);

    if (STRNEQ(pooldef->name, volume->pool))
        origpoolobj = storagePoolObjFindByName(volume->pool);

    if (STRNEQ(pooldef->name, volume->pool) && !origpoolobj) {
        virReportError(VIR_ERR_NO_STORAGE_POOL,
                       _("no storage pool with matching name '%s'"),
                       volume->pool);
        goto cleanup;
    }

    if (!virPoolObjIsActive(poolobj)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"), pooldef->name);
        goto cleanup;
    }

    if (origpoolobj && !virPoolObjIsActive(origpoolobj)) {
        origpooldef = virPoolObjGetDef(poolobj);
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("storage pool '%s' is not active"),
                       origpooldef->name);
        goto cleanup;
    }

    if ((backend = virStorageBackendForType(pooldef->type)) == NULL)
        goto cleanup;

    if (!(origvolobj = virStorageVolObjFindByName(origpoolobj ? origpoolobj :
                                                  poolobj, volume->name))) {
        virReportError(VIR_ERR_NO_STORAGE_VOL,
                       _("no storage vol with matching name '%s'"),
                       volume->name);
        goto cleanup;
    }
    origvoldef = virPoolObjGetDef(origvolobj);

    if (!(newvoldef =
          virStorageVolDefParseString(pooldef, xmldesc,
                                      VIR_VOL_XML_PARSE_NO_CAPACITY)))
        goto cleanup;

    if (virStorageVolCreateXMLFromEnsureACL(pool->conn, pooldef, newvoldef) < 0)
        goto cleanup;

    if (virStorageVolObjFindByName(poolobj, newvoldef->name)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("storage volume name '%s' already in use."),
                       newvoldef->name);
        goto cleanup;
    }

    /* Use the original volume's capacity in case the new capacity
     * is less than that, or it was omitted */
    if (newvoldef->target.capacity < origvoldef->target.capacity)
        newvoldef->target.capacity = origvoldef->target.capacity;

    /* If the allocation was not provided in the XML, then use capacity
     * as it's specifically documented "If omitted when creating a volume,
     * the  volume will be fully allocated at time of creation.". This
     * is especially important for logical volume creation. */
    if (!newvoldef->target.has_allocation)
        newvoldef->target.allocation = newvoldef->target.capacity;

    if (!backend->buildVolFrom) {
        virReportError(VIR_ERR_NO_SUPPORT,
                       "%s", _("storage pool does not support"
                               " volume creation from an existing volume"));
        goto cleanup;
    }

    if (origvoldef->building) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("volume '%s' is still being allocated."),
                       origvoldef->name);
        goto cleanup;
    }

    if (backend->refreshVol &&
        backend->refreshVol(pool->conn, poolobj, origvoldef) < 0)
        goto cleanup;

    /* 'Define' the new volume so we get async progress reporting.
     * Wipe any key the user may have suggested, as volume creation
     * will generate the canonical key.  */
    VIR_FREE(newvoldef->key);
    if (backend->createVol(pool->conn, poolobj, newvoldef) < 0)
        goto cleanup;

    /* Make a shallow copy of the 'defined' volume definition, since the
     * original allocation value will change as the user polls 'info',
     * but we only need the initial requested values
     */
    if (VIR_ALLOC(shadowvoldef) < 0)
        goto cleanup;

    memcpy(shadowvoldef, newvoldef, sizeof(*newvoldef));

    if (!(newvolobj = virStoragePoolObjAddVolume(poolobj, newvoldef)))
        goto cleanup;
    VIR_STEAL_PTR(objnewvoldef, newvoldef);

    if (!(vol = virGetStorageVol(pool->conn, pooldef->name, objnewvoldef->name,
                                 objnewvoldef->key, NULL, NULL))) {
        virStoragePoolObjRemoveVolume(poolobj, &newvolobj);
        goto cleanup;
    }

    /* Drop the pool lock during volume allocation */
    virStoragePoolObjPrivateIncrAsyncjobs(poolobj);
    objnewvoldef->building = true;
    origvoldef->in_use++;
    virObjectUnlock(poolobj);

    if (origpoolobj) {
        virStoragePoolObjPrivateIncrAsyncjobs(origpoolobj);
        virObjectUnlock(origpoolobj);
    }

    buildret = backend->buildVolFrom(pool->conn, poolobj, shadowvoldef,
                                     origvoldef, flags);

    virObjectLock(poolobj);
    if (origpoolobj)
        virObjectLock(origpoolobj);

    origvoldef->in_use--;
    objnewvoldef->building = false;
    virStoragePoolObjPrivateDecrAsyncjobs(poolobj);

    if (origpoolobj)
        virStoragePoolObjPrivateDecrAsyncjobs(origpoolobj);

    if (buildret < 0 ||
        (backend->refreshVol &&
         backend->refreshVol(pool->conn, poolobj, objnewvoldef) < 0)) {
        storageVolDeleteInternal(vol, backend, poolobj, &newvolobj, 0, false);
        goto cleanup;
    }

    /* Updating pool metadata ignoring the disk backend since
     * it updates the pool values
     */
    if (pooldef->type != VIR_STORAGE_POOL_DISK) {
        pooldef->allocation += objnewvoldef->target.allocation;
        pooldef->available -= objnewvoldef->target.allocation;
    }

    VIR_INFO("Creating volume '%s' in storage pool '%s'",
             vol->name, pooldef->name);
    ret = vol;
    vol = NULL;

 cleanup:
    virPoolObjEndAPI(&origvolobj);
    virPoolObjEndAPI(&newvolobj);
    virObjectUnref(vol);
    virStorageVolDefFree(newvoldef);
    VIR_FREE(shadowvoldef);
    virPoolObjEndAPI(&origpoolobj);
    virPoolObjEndAPI(&poolobj);
    return ret;
}


static int
storageVolDownload(virStorageVolPtr volume,
                   virStreamPtr stream,
                   unsigned long long offset,
                   unsigned long long length,
                   unsigned int flags)
{
    virPoolObjPtr poolobj;
    virStoragePoolDefPtr pooldef;
    virPoolObjPtr volobj;
    virStorageVolDefPtr voldef;
    virStorageBackendPtr backend;
    int ret = -1;

    virCheckFlags(0, -1);

    if (!(volobj = virStorageVolObjFromVol(volume, &poolobj, &backend)))
        return -1;
    pooldef = virPoolObjGetDef(poolobj);
    voldef = virPoolObjGetDef(volobj);

    if (virStorageVolDownloadEnsureACL(volume->conn, pooldef, voldef) < 0)
        goto cleanup;

    if (voldef->building) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("volume '%s' is still being allocated."),
                       voldef->name);
        goto cleanup;
    }

    if (!backend->downloadVol) {
        virReportError(VIR_ERR_NO_SUPPORT, "%s",
                       _("storage pool doesn't support volume download"));
        goto cleanup;
    }

    ret = backend->downloadVol(volume->conn, poolobj, voldef, stream,
                               offset, length, flags);

 cleanup:
    virPoolObjEndAPI(&volobj);
    virPoolObjEndAPI(&poolobj);
    return ret;
}


/**
 * Frees opaque data.
 *
 * @opaque Data to be freed.
 */
static void
virStorageVolPoolRefreshDataFree(void *opaque)
{
    virStorageVolStreamInfoPtr cbdata = opaque;

    VIR_FREE(cbdata->pool_name);
    VIR_FREE(cbdata);
}

static int
virStorageBackendPloopRestoreDesc(char *path)
{
    int ret = -1;
    virCommandPtr cmd = NULL;
    char *refresh_tool = NULL;
    char *desc = NULL;

    if (virAsprintf(&desc, "%s/DiskDescriptor.xml", path) < 0)
        return ret;

    if (virFileRemove(desc, 0, 0) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("refresh ploop failed:"
                         " unable to delete DiskDescriptor.xml"));
        goto cleanup;
    }

    refresh_tool = virFindFileInPath("ploop");
    if (!refresh_tool) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("unable to find ploop, please install ploop tools"));
        goto cleanup;
    }

    cmd = virCommandNewArgList(refresh_tool, "restore-descriptor",
                               path, NULL);
    virCommandAddArgFormat(cmd, "%s/root.hds", path);
    if (virCommandRun(cmd, NULL) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    VIR_FREE(refresh_tool);
    virCommandFree(cmd);
    VIR_FREE(desc);
    return ret;
}



/**
 * Thread to handle the pool refresh
 *
 * @st Pointer to stream being closed.
 * @opaque Domain's device information structure.
 */
static void
virStorageVolPoolRefreshThread(void *opaque)
{

    virStorageVolStreamInfoPtr cbdata = opaque;
    virPoolObjPtr poolobj = NULL;
    virStoragePoolDefPtr pooldef;
    virStorageBackendPtr backend;
    virObjectEventPtr event = NULL;

    if (cbdata->vol_path) {
        if (virStorageBackendPloopRestoreDesc(cbdata->vol_path) < 0)
            goto cleanup;
    }

    if (!(poolobj = storagePoolObjFindByName(cbdata->pool_name)))
        goto cleanup;
    pooldef = virPoolObjGetDef(poolobj);

    if (!(backend = virStorageBackendForType(pooldef->type)))
        goto cleanup;

    virStoragePoolObjClearVols(poolobj);
    if (backend->refreshPool(NULL, poolobj) < 0)
        VIR_DEBUG("Failed to refresh storage pool");

    event = virStoragePoolEventRefreshNew(pooldef->name,
                                          pooldef->uuid);

 cleanup:
    if (event)
        virObjectEventStateQueue(driver->storageEventState, event);
    virPoolObjEndAPI(&poolobj);
    virStorageVolPoolRefreshDataFree(cbdata);
}


/**
 * Callback being called if a FDstream is closed. Will spin off a thread
 * to perform a pool refresh.
 *
 * @st Pointer to stream being closed.
 * @opaque Buffer to hold the pool name to be refreshed
 */
static void
virStorageVolFDStreamCloseCb(virStreamPtr st ATTRIBUTE_UNUSED,
                             void *opaque)
{
    virThread thread;

    if (virThreadCreate(&thread, false, virStorageVolPoolRefreshThread,
                        opaque) < 0) {
        /* Not much else can be done */
        VIR_ERROR(_("Failed to create thread to handle pool refresh"));
        goto error;
    }
    return; /* Thread will free opaque data */

 error:
    virStorageVolPoolRefreshDataFree(opaque);
}


static int
storageVolUpload(virStorageVolPtr volume,
                 virStreamPtr stream,
                 unsigned long long offset,
                 unsigned long long length,
                 unsigned int flags)
{
    virPoolObjPtr poolobj;
    virStoragePoolDefPtr pooldef;
    virPoolObjPtr volobj;
    virStorageVolDefPtr voldef;
    virStorageBackendPtr backend;
    virStorageVolStreamInfoPtr cbdata = NULL;
    int ret = -1;

    virCheckFlags(0, -1);

    if (!(volobj = virStorageVolObjFromVol(volume, &poolobj, &backend)))
        return -1;
    pooldef = virPoolObjGetDef(poolobj);
    voldef = virPoolObjGetDef(volobj);

    if (virStorageVolUploadEnsureACL(volume->conn, pooldef, voldef) < 0)
        goto cleanup;

    if (voldef->in_use) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("volume '%s' is still in use."),
                       voldef->name);
        goto cleanup;
    }

    if (voldef->building) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("volume '%s' is still being allocated."),
                       voldef->name);
        goto cleanup;
    }

    if (!backend->uploadVol) {
        virReportError(VIR_ERR_NO_SUPPORT, "%s",
                       _("storage pool doesn't support volume upload"));
        goto cleanup;
    }

    /* Use the callback routine in order to
     * refresh the pool after the volume upload stream closes. This way
     * we make sure the volume and pool data are refreshed without user
     * interaction and we can just lookup the backend in the callback
     * routine in order to call the refresh API.
     */
    if (VIR_ALLOC(cbdata) < 0 ||
        VIR_STRDUP(cbdata->pool_name, pooldef->name) < 0)
        goto cleanup;
    if (voldef->type == VIR_STORAGE_VOL_PLOOP &&
        VIR_STRDUP(cbdata->vol_path, voldef->target.path) < 0)
        goto cleanup;

    if ((ret = backend->uploadVol(volume->conn, poolobj, voldef, stream,
                                  offset, length, flags)) < 0)
        goto cleanup;

    /* Add cleanup callback - call after uploadVol since the stream
     * is then fully set up
     */
    virFDStreamSetInternalCloseCb(stream,
                                  virStorageVolFDStreamCloseCb,
                                  cbdata, NULL);
    cbdata = NULL;

 cleanup:
    virPoolObjEndAPI(&volobj);
    virPoolObjEndAPI(&poolobj);
    if (cbdata)
        virStorageVolPoolRefreshDataFree(cbdata);

    return ret;
}


static int
storageVolResize(virStorageVolPtr volume,
                 unsigned long long capacity,
                 unsigned int flags)
{
    virPoolObjPtr poolobj;
    virStoragePoolDefPtr pooldef;
    virPoolObjPtr volobj;
    virStorageVolDefPtr voldef;
    virStorageBackendPtr backend;
    unsigned long long abs_capacity, delta = 0;
    int ret = -1;

    virCheckFlags(VIR_STORAGE_VOL_RESIZE_ALLOCATE |
                  VIR_STORAGE_VOL_RESIZE_DELTA |
                  VIR_STORAGE_VOL_RESIZE_SHRINK, -1);

    if (!(volobj = virStorageVolObjFromVol(volume, &poolobj, &backend)))
        return -1;
    pooldef = virPoolObjGetDef(poolobj);
    voldef = virPoolObjGetDef(volobj);

    if (virStorageVolResizeEnsureACL(volume->conn, pooldef, voldef) < 0)
        goto cleanup;

    if (voldef->in_use) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("volume '%s' is still in use."),
                       voldef->name);
        goto cleanup;
    }

    if (voldef->building) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("volume '%s' is still being allocated."),
                       voldef->name);
        goto cleanup;
    }

    if (flags & VIR_STORAGE_VOL_RESIZE_DELTA) {
        if (flags & VIR_STORAGE_VOL_RESIZE_SHRINK)
            abs_capacity = voldef->target.capacity -
                MIN(capacity, voldef->target.capacity);
        else
            abs_capacity = voldef->target.capacity + capacity;
        flags &= ~VIR_STORAGE_VOL_RESIZE_DELTA;
    } else {
        abs_capacity = capacity;
    }

    if (abs_capacity < voldef->target.allocation) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("can't shrink capacity below existing allocation"));
        goto cleanup;
    }

    if (abs_capacity < voldef->target.capacity &&
        !(flags & VIR_STORAGE_VOL_RESIZE_SHRINK)) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("Can't shrink capacity below current capacity unless "
                         "shrink flag explicitly specified"));
        goto cleanup;
    }

    if (flags & VIR_STORAGE_VOL_RESIZE_ALLOCATE)
        delta = abs_capacity - voldef->target.allocation;

    if (delta > pooldef->available) {
        virReportError(VIR_ERR_OPERATION_FAILED, "%s",
                       _("Not enough space left in storage pool"));
        goto cleanup;
    }

    if (!backend->resizeVol) {
        virReportError(VIR_ERR_NO_SUPPORT, "%s",
                       _("storage pool does not support changing of "
                         "volume capacity"));
        goto cleanup;
    }

    if (backend->resizeVol(volume->conn, poolobj, voldef,
                           abs_capacity, flags) < 0)
        goto cleanup;

    voldef->target.capacity = abs_capacity;
    /* Only update the allocation and pool values if we actually did the
     * allocation; otherwise, this is akin to a create operation with a
     * capacity value different and potentially much larger than available
     */
    if (flags & VIR_STORAGE_VOL_RESIZE_ALLOCATE) {
        voldef->target.allocation = abs_capacity;
        pooldef->allocation += delta;
        pooldef->available -= delta;
    }

    ret = 0;

 cleanup:
    virPoolObjEndAPI(&volobj);
    virPoolObjEndAPI(&poolobj);

    return ret;
}


static int
storageVolWipePattern(virStorageVolPtr volume,
                      unsigned int algorithm,
                      unsigned int flags)
{
    virPoolObjPtr poolobj;
    virStoragePoolDefPtr pooldef;
    virPoolObjPtr volobj;
    virStorageVolDefPtr voldef;
    virStorageBackendPtr backend;
    int ret = -1;

    virCheckFlags(0, -1);

    if (algorithm >= VIR_STORAGE_VOL_WIPE_ALG_LAST) {
        virReportError(VIR_ERR_INVALID_ARG,
                       _("wiping algorithm %d not supported"),
                       algorithm);
        return -1;
    }

    if (!(volobj = virStorageVolObjFromVol(volume, &poolobj, &backend)))
        return -1;
    pooldef = virPoolObjGetDef(poolobj);
    voldef = virPoolObjGetDef(volobj);

    if (virStorageVolWipePatternEnsureACL(volume->conn, pooldef, voldef) < 0)
        goto cleanup;

    if (voldef->in_use) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("volume '%s' is still in use."),
                       voldef->name);
        goto cleanup;
    }

    if (voldef->building) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("volume '%s' is still being allocated."),
                       voldef->name);
        goto cleanup;
    }

    if (!backend->wipeVol) {
        virReportError(VIR_ERR_NO_SUPPORT, "%s",
                       _("storage pool doesn't support volume wiping"));
        goto cleanup;
    }

    if (backend->wipeVol(volume->conn, poolobj, voldef, algorithm, flags) < 0)
        goto cleanup;

    if (backend->refreshVol &&
        backend->refreshVol(volume->conn, poolobj, voldef) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virPoolObjEndAPI(&volobj);
    virPoolObjEndAPI(&poolobj);

    return ret;
}


static int
storageVolWipe(virStorageVolPtr volume,
               unsigned int flags)
{
    return storageVolWipePattern(volume, VIR_STORAGE_VOL_WIPE_ALG_ZERO, flags);
}


static int
storageVolGetInfoFlags(virStorageVolPtr volume,
                       virStorageVolInfoPtr info,
                       unsigned int flags)
{
    virPoolObjPtr poolobj;
    virStoragePoolDefPtr pooldef;
    virPoolObjPtr volobj;
    virStorageVolDefPtr voldef;
    virStorageBackendPtr backend;
    int ret = -1;

    virCheckFlags(VIR_STORAGE_VOL_GET_PHYSICAL, -1);

    if (!(volobj = virStorageVolObjFromVol(volume, &poolobj, &backend)))
        return -1;
    pooldef = virPoolObjGetDef(poolobj);
    voldef = virPoolObjGetDef(volobj);

    if (virStorageVolGetInfoFlagsEnsureACL(volume->conn, pooldef, voldef) < 0)
        goto cleanup;

    if (backend->refreshVol &&
        backend->refreshVol(volume->conn, poolobj, voldef) < 0)
        goto cleanup;

    memset(info, 0, sizeof(*info));
    info->type = voldef->type;
    info->capacity = voldef->target.capacity;
    if (flags & VIR_STORAGE_VOL_GET_PHYSICAL)
        info->allocation = voldef->target.physical;
    else
        info->allocation = voldef->target.allocation;
    ret = 0;

 cleanup:
    virPoolObjEndAPI(&volobj);
    virPoolObjEndAPI(&poolobj);
    return ret;
}


static int
storageVolGetInfo(virStorageVolPtr volume,
                  virStorageVolInfoPtr info)
{
    return storageVolGetInfoFlags(volume, info, 0);
}


static char *
storageVolGetXMLDesc(virStorageVolPtr volume,
                     unsigned int flags)
{
    virPoolObjPtr poolobj;
    virStoragePoolDefPtr pooldef;
    virPoolObjPtr volobj;
    virStorageVolDefPtr voldef;
    virStorageBackendPtr backend;
    char *ret = NULL;

    virCheckFlags(0, NULL);

    if (!(volobj = virStorageVolObjFromVol(volume, &poolobj, &backend)))
        return NULL;
    pooldef = virPoolObjGetDef(poolobj);
    voldef = virPoolObjGetDef(volobj);

    if (virStorageVolGetXMLDescEnsureACL(volume->conn, pooldef, voldef) < 0)
        goto cleanup;

    if (backend->refreshVol &&
        backend->refreshVol(volume->conn, poolobj, voldef) < 0)
        goto cleanup;

    ret = virStorageVolDefFormat(pooldef, voldef);

 cleanup:
    virPoolObjEndAPI(&volobj);
    virPoolObjEndAPI(&poolobj);

    return ret;
}


static char *
storageVolGetPath(virStorageVolPtr volume)
{
    virPoolObjPtr poolobj;
    virStoragePoolDefPtr pooldef;
    virPoolObjPtr volobj;
    virStorageVolDefPtr voldef;
    char *ret = NULL;

    if (!(volobj = virStorageVolObjFromVol(volume, &poolobj, NULL)))
        return NULL;
    pooldef = virPoolObjGetDef(poolobj);
    voldef = virPoolObjGetDef(volobj);

    if (virStorageVolGetPathEnsureACL(volume->conn, pooldef, voldef) < 0)
        goto cleanup;

    ignore_value(VIR_STRDUP(ret, voldef->target.path));

 cleanup:
    virPoolObjEndAPI(&volobj);
    virPoolObjEndAPI(&poolobj);
    return ret;
}


static int
storageConnectListAllStoragePools(virConnectPtr conn,
                                  virStoragePoolPtr **pools,
                                  unsigned int flags)
{
    virCheckFlags(VIR_CONNECT_LIST_STORAGE_POOLS_FILTERS_ALL, -1);

    if (virConnectListAllStoragePoolsEnsureACL(conn) < 0)
        return -1;

    return virStoragePoolObjExportList(conn, driver->pools, pools,
                                       virConnectListAllStoragePoolsCheckACL,
                                       flags);
}


static int
storageConnectStoragePoolEventRegisterAny(virConnectPtr conn,
                                          virStoragePoolPtr pool,
                                          int eventID,
                                          virConnectStoragePoolEventGenericCallback callback,
                                          void *opaque,
                                          virFreeCallback freecb)
{
    int callbackID = -1;

    if (virConnectStoragePoolEventRegisterAnyEnsureACL(conn) < 0)
        goto cleanup;

    if (virStoragePoolEventStateRegisterID(conn, driver->storageEventState,
                                           pool, eventID, callback,
                                           opaque, freecb, &callbackID) < 0)
        callbackID = -1;
 cleanup:
    return callbackID;
}

static int
storageConnectStoragePoolEventDeregisterAny(virConnectPtr conn,
                                            int callbackID)
{
    int ret = -1;

    if (virConnectStoragePoolEventDeregisterAnyEnsureACL(conn) < 0)
        goto cleanup;

    if (virObjectEventStateDeregisterID(conn,
                                        driver->storageEventState,
                                        callbackID) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    return ret;
}



static virStorageDriver storageDriver = {
    .name = "storage",
    .connectNumOfStoragePools = storageConnectNumOfStoragePools, /* 0.4.0 */
    .connectListStoragePools = storageConnectListStoragePools, /* 0.4.0 */
    .connectNumOfDefinedStoragePools = storageConnectNumOfDefinedStoragePools, /* 0.4.0 */
    .connectListDefinedStoragePools = storageConnectListDefinedStoragePools, /* 0.4.0 */
    .connectListAllStoragePools = storageConnectListAllStoragePools, /* 0.10.2 */
    .connectStoragePoolEventRegisterAny = storageConnectStoragePoolEventRegisterAny, /* 2.0.0 */
    .connectStoragePoolEventDeregisterAny = storageConnectStoragePoolEventDeregisterAny, /* 2.0.0 */
    .connectFindStoragePoolSources = storageConnectFindStoragePoolSources, /* 0.4.0 */
    .storagePoolLookupByName = storagePoolLookupByName, /* 0.4.0 */
    .storagePoolLookupByUUID = storagePoolLookupByUUID, /* 0.4.0 */
    .storagePoolLookupByVolume = storagePoolLookupByVolume, /* 0.4.0 */
    .storagePoolCreateXML = storagePoolCreateXML, /* 0.4.0 */
    .storagePoolDefineXML = storagePoolDefineXML, /* 0.4.0 */
    .storagePoolBuild = storagePoolBuild, /* 0.4.0 */
    .storagePoolUndefine = storagePoolUndefine, /* 0.4.0 */
    .storagePoolCreate = storagePoolCreate, /* 0.4.0 */
    .storagePoolDestroy = storagePoolDestroy, /* 0.4.0 */
    .storagePoolDelete = storagePoolDelete, /* 0.4.0 */
    .storagePoolRefresh = storagePoolRefresh, /* 0.4.0 */
    .storagePoolGetInfo = storagePoolGetInfo, /* 0.4.0 */
    .storagePoolGetXMLDesc = storagePoolGetXMLDesc, /* 0.4.0 */
    .storagePoolGetAutostart = storagePoolGetAutostart, /* 0.4.0 */
    .storagePoolSetAutostart = storagePoolSetAutostart, /* 0.4.0 */
    .storagePoolNumOfVolumes = storagePoolNumOfVolumes, /* 0.4.0 */
    .storagePoolListVolumes = storagePoolListVolumes, /* 0.4.0 */
    .storagePoolListAllVolumes = storagePoolListAllVolumes, /* 0.10.2 */

    .storageVolLookupByName = storageVolLookupByName, /* 0.4.0 */
    .storageVolLookupByKey = storageVolLookupByKey, /* 0.4.0 */
    .storageVolLookupByPath = storageVolLookupByPath, /* 0.4.0 */
    .storageVolCreateXML = storageVolCreateXML, /* 0.4.0 */
    .storageVolCreateXMLFrom = storageVolCreateXMLFrom, /* 0.6.4 */
    .storageVolDownload = storageVolDownload, /* 0.9.0 */
    .storageVolUpload = storageVolUpload, /* 0.9.0 */
    .storageVolDelete = storageVolDelete, /* 0.4.0 */
    .storageVolWipe = storageVolWipe, /* 0.8.0 */
    .storageVolWipePattern = storageVolWipePattern, /* 0.9.10 */
    .storageVolGetInfo = storageVolGetInfo, /* 0.4.0 */
    .storageVolGetInfoFlags = storageVolGetInfoFlags, /* 3.0.0 */
    .storageVolGetXMLDesc = storageVolGetXMLDesc, /* 0.4.0 */
    .storageVolGetPath = storageVolGetPath, /* 0.4.0 */
    .storageVolResize = storageVolResize, /* 0.9.10 */

    .storagePoolIsActive = storagePoolIsActive, /* 0.7.3 */
    .storagePoolIsPersistent = storagePoolIsPersistent, /* 0.7.3 */
};


static virStateDriver stateDriver = {
    .name = "storage",
    .stateInitialize = storageStateInitialize,
    .stateAutoStart = storageStateAutoStart,
    .stateCleanup = storageStateCleanup,
    .stateReload = storageStateReload,
};

int storageRegister(void)
{
    if (virSetSharedStorageDriver(&storageDriver) < 0)
        return -1;
    if (virRegisterStateDriver(&stateDriver) < 0)
        return -1;
    return 0;
}


/* ----------- file handlers cooperating with storage driver --------------- */
static bool
virStorageFileIsInitialized(const virStorageSource *src)
{
    return src && src->drv;
}


static bool
virStorageFileSupportsBackingChainTraversal(virStorageSourcePtr src)
{
    int actualType;
    virStorageFileBackendPtr backend;

    if (!src)
        return false;
    actualType = virStorageSourceGetActualType(src);

    if (src->drv) {
        backend = src->drv->backend;
    } else {
        if (!(backend = virStorageFileBackendForTypeInternal(actualType,
                                                             src->protocol,
                                                             false)))
            return false;
    }

    return backend->storageFileGetUniqueIdentifier &&
           backend->storageFileReadHeader &&
           backend->storageFileAccess;
}


/**
 * virStorageFileSupportsSecurityDriver:
 *
 * @src: a storage file structure
 *
 * Check if a storage file supports operations needed by the security
 * driver to perform labelling
 */
bool
virStorageFileSupportsSecurityDriver(const virStorageSource *src)
{
    int actualType;
    virStorageFileBackendPtr backend;

    if (!src)
        return false;
    actualType = virStorageSourceGetActualType(src);

    if (src->drv) {
        backend = src->drv->backend;
    } else {
        if (!(backend = virStorageFileBackendForTypeInternal(actualType,
                                                             src->protocol,
                                                             false)))
            return false;
    }

    return !!backend->storageFileChown;
}


void
virStorageFileDeinit(virStorageSourcePtr src)
{
    if (!virStorageFileIsInitialized(src))
        return;

    if (src->drv->backend &&
        src->drv->backend->backendDeinit)
        src->drv->backend->backendDeinit(src);

    VIR_FREE(src->drv);
}


/**
 * virStorageFileInitAs:
 *
 * @src: storage source definition
 * @uid: uid used to access the file, or -1 for current uid
 * @gid: gid used to access the file, or -1 for current gid
 *
 * Initialize a storage source to be used with storage driver. Use the provided
 * uid and gid if possible for the operations.
 *
 * Returns 0 if the storage file was successfully initialized, -1 if the
 * initialization failed. Libvirt error is reported.
 */
int
virStorageFileInitAs(virStorageSourcePtr src,
                     uid_t uid, gid_t gid)
{
    int actualType = virStorageSourceGetActualType(src);
    if (VIR_ALLOC(src->drv) < 0)
        return -1;

    if (uid == (uid_t) -1)
        src->drv->uid = geteuid();
    else
        src->drv->uid = uid;

    if (gid == (gid_t) -1)
        src->drv->gid = getegid();
    else
        src->drv->gid = gid;

    if (!(src->drv->backend = virStorageFileBackendForType(actualType,
                                                           src->protocol)))
        goto error;

    if (src->drv->backend->backendInit &&
        src->drv->backend->backendInit(src) < 0)
        goto error;

    return 0;

 error:
    VIR_FREE(src->drv);
    return -1;
}


/**
 * virStorageFileInit:
 *
 * See virStorageFileInitAs. The file is initialized to be accessed by the
 * current user.
 */
int
virStorageFileInit(virStorageSourcePtr src)
{
    return virStorageFileInitAs(src, -1, -1);
}


/**
 * virStorageFileCreate: Creates an empty storage file via storage driver
 *
 * @src: file structure pointing to the file
 *
 * Returns 0 on success, -2 if the function isn't supported by the backend,
 * -1 on other failure. Errno is set in case of failure.
 */
int
virStorageFileCreate(virStorageSourcePtr src)
{
    int ret;

    if (!virStorageFileIsInitialized(src) ||
        !src->drv->backend->storageFileCreate) {
        errno = ENOSYS;
        return -2;
    }

    ret = src->drv->backend->storageFileCreate(src);

    VIR_DEBUG("created storage file %p: ret=%d, errno=%d",
              src, ret, errno);

    return ret;
}


/**
 * virStorageFileUnlink: Unlink storage file via storage driver
 *
 * @src: file structure pointing to the file
 *
 * Unlinks the file described by the @file structure.
 *
 * Returns 0 on success, -2 if the function isn't supported by the backend,
 * -1 on other failure. Errno is set in case of failure.
 */
int
virStorageFileUnlink(virStorageSourcePtr src)
{
    int ret;

    if (!virStorageFileIsInitialized(src) ||
        !src->drv->backend->storageFileUnlink) {
        errno = ENOSYS;
        return -2;
    }

    ret = src->drv->backend->storageFileUnlink(src);

    VIR_DEBUG("unlinked storage file %p: ret=%d, errno=%d",
              src, ret, errno);

    return ret;
}


/**
 * virStorageFileStat: returns stat struct of a file via storage driver
 *
 * @src: file structure pointing to the file
 * @stat: stat structure to return data
 *
 * Returns 0 on success, -2 if the function isn't supported by the backend,
 * -1 on other failure. Errno is set in case of failure.
*/
int
virStorageFileStat(virStorageSourcePtr src,
                   struct stat *st)
{
    int ret;

    if (!virStorageFileIsInitialized(src) ||
        !src->drv->backend->storageFileStat) {
        errno = ENOSYS;
        return -2;
    }

    ret = src->drv->backend->storageFileStat(src, st);

    VIR_DEBUG("stat of storage file %p: ret=%d, errno=%d",
              src, ret, errno);

    return ret;
}


/**
 * virStorageFileReadHeader: read the beginning bytes of a file into a buffer
 *
 * @src: file structure pointing to the file
 * @max_len: maximum number of bytes read from the storage file
 * @buf: buffer to read the data into. buffer shall be freed by caller)
 *
 * Returns the count of bytes read on success and -1 on failure, -2 if the
 * function isn't supported by the backend.
 * Libvirt error is reported on failure.
 */
ssize_t
virStorageFileReadHeader(virStorageSourcePtr src,
                         ssize_t max_len,
                         char **buf)
{
    ssize_t ret;

    if (!virStorageFileIsInitialized(src)) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("storage file backend not initialized"));
        return -1;
    }

    if (!src->drv->backend->storageFileReadHeader) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("storage file header reading is not supported for "
                         "storage type %s (protocol: %s)"),
                       virStorageTypeToString(src->type),
                       virStorageNetProtocolTypeToString(src->protocol));
        return -2;
    }

    ret = src->drv->backend->storageFileReadHeader(src, max_len, buf);

    VIR_DEBUG("read of storage header %p: ret=%zd", src, ret);

    return ret;
}


/*
 * virStorageFileGetUniqueIdentifier: Get a unique string describing the volume
 *
 * @src: file structure pointing to the file
 *
 * Returns a string uniquely describing a single volume (canonical path).
 * The string shall not be freed and is valid until the storage file is
 * deinitialized. Returns NULL on error and sets a libvirt error code */
const char *
virStorageFileGetUniqueIdentifier(virStorageSourcePtr src)
{
    if (!virStorageFileIsInitialized(src)) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("storage file backend not initialized"));
        return NULL;
    }

    if (!src->drv->backend->storageFileGetUniqueIdentifier) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("unique storage file identifier not implemented for "
                          "storage type %s (protocol: %s)'"),
                       virStorageTypeToString(src->type),
                       virStorageNetProtocolTypeToString(src->protocol));
        return NULL;
    }

    return src->drv->backend->storageFileGetUniqueIdentifier(src);
}


/**
 * virStorageFileAccess: Check accessibility of a storage file
 *
 * @src: storage file to check access permissions
 * @mode: accessibility check options (see man 2 access)
 *
 * Returns 0 on success, -1 on error and sets errno. No libvirt
 * error is reported. Returns -2 if the operation isn't supported
 * by libvirt storage backend.
 */
int
virStorageFileAccess(virStorageSourcePtr src,
                     int mode)
{
    if (!virStorageFileIsInitialized(src) ||
        !src->drv->backend->storageFileAccess) {
        errno = ENOSYS;
        return -2;
    }

    return src->drv->backend->storageFileAccess(src, mode);
}


/**
 * virStorageFileChown: Change owner of a storage file
 *
 * @src: storage file to change owner of
 * @uid: new owner id
 * @gid: new group id
 *
 * Returns 0 on success, -1 on error and sets errno. No libvirt
 * error is reported. Returns -2 if the operation isn't supported
 * by libvirt storage backend.
 */
int
virStorageFileChown(const virStorageSource *src,
                    uid_t uid,
                    gid_t gid)
{
    if (!virStorageFileIsInitialized(src) ||
        !src->drv->backend->storageFileChown) {
        errno = ENOSYS;
        return -2;
    }

    VIR_DEBUG("chown of storage file %p to %u:%u",
              src, (unsigned int)uid, (unsigned int)gid);

    return src->drv->backend->storageFileChown(src, uid, gid);
}


/* Recursive workhorse for virStorageFileGetMetadata.  */
static int
virStorageFileGetMetadataRecurse(virStorageSourcePtr src,
                                 virStorageSourcePtr parent,
                                 uid_t uid, gid_t gid,
                                 bool allow_probe,
                                 bool report_broken,
                                 virHashTablePtr cycle)
{
    int ret = -1;
    const char *uniqueName;
    char *buf = NULL;
    ssize_t headerLen;
    virStorageSourcePtr backingStore = NULL;
    int backingFormat;

    VIR_DEBUG("path=%s format=%d uid=%u gid=%u probe=%d",
              src->path, src->format,
              (unsigned int)uid, (unsigned int)gid, allow_probe);

    /* exit if we can't load information about the current image */
    if (!virStorageFileSupportsBackingChainTraversal(src))
        return 0;

    if (virStorageFileInitAs(src, uid, gid) < 0)
        return -1;

    if (virStorageFileAccess(src, F_OK) < 0) {
        if (src == parent) {
            virReportSystemError(errno,
                                 _("Cannot access storage file '%s' "
                                   "(as uid:%u, gid:%u)"),
                                 src->path, (unsigned int)uid,
                                 (unsigned int)gid);
        } else {
            virReportSystemError(errno,
                                 _("Cannot access backing file '%s' "
                                   "of storage file '%s' (as uid:%u, gid:%u)"),
                                 src->path, parent->path,
                                 (unsigned int)uid, (unsigned int)gid);
        }

        goto cleanup;
    }

    if (!(uniqueName = virStorageFileGetUniqueIdentifier(src)))
        goto cleanup;

    if (virHashLookup(cycle, uniqueName)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("backing store for %s (%s) is self-referential"),
                       src->path, uniqueName);
        goto cleanup;
    }

    if (virHashAddEntry(cycle, uniqueName, (void *)1) < 0)
        goto cleanup;

    if ((headerLen = virStorageFileReadHeader(src, VIR_STORAGE_MAX_HEADER,
                                              &buf)) < 0)
        goto cleanup;

    if (virStorageFileGetMetadataInternal(src, buf, headerLen,
                                          &backingFormat) < 0)
        goto cleanup;

    /* check whether we need to go deeper */
    if (!src->backingStoreRaw) {
        ret = 0;
        goto cleanup;
    }

    if (!(backingStore = virStorageSourceNewFromBacking(src)))
        goto cleanup;

    if (backingFormat == VIR_STORAGE_FILE_AUTO && !allow_probe)
        backingStore->format = VIR_STORAGE_FILE_RAW;
    else if (backingFormat == VIR_STORAGE_FILE_AUTO_SAFE)
        backingStore->format = VIR_STORAGE_FILE_AUTO;
    else
        backingStore->format = backingFormat;

    if ((ret = virStorageFileGetMetadataRecurse(backingStore, parent,
                                                uid, gid,
                                                allow_probe, report_broken,
                                                cycle)) < 0) {
        if (report_broken)
            goto cleanup;

        /* if we fail somewhere midway, just accept and return a
         * broken chain */
        ret = 0;
        goto cleanup;
    }

    src->backingStore = backingStore;
    backingStore = NULL;
    ret = 0;

 cleanup:
    VIR_FREE(buf);
    virStorageFileDeinit(src);
    virStorageSourceFree(backingStore);
    return ret;
}


/**
 * virStorageFileGetMetadata:
 *
 * Extract metadata about the storage volume with the specified
 * image format. If image format is VIR_STORAGE_FILE_AUTO, it
 * will probe to automatically identify the format.  Recurses through
 * the entire chain.
 *
 * Open files using UID and GID (or pass -1 for the current user/group).
 * Treat any backing files without explicit type as raw, unless ALLOW_PROBE.
 *
 * Callers are advised never to use VIR_STORAGE_FILE_AUTO as a
 * format, since a malicious guest can turn a raw file into any
 * other non-raw format at will.
 *
 * If @report_broken is true, the whole function fails with a possibly sane
 * error instead of just returning a broken chain.
 *
 * Caller MUST free result after use via virStorageSourceFree.
 */
int
virStorageFileGetMetadata(virStorageSourcePtr src,
                          uid_t uid, gid_t gid,
                          bool allow_probe,
                          bool report_broken)
{
    VIR_DEBUG("path=%s format=%d uid=%u gid=%u probe=%d, report_broken=%d",
              src->path, src->format, (unsigned int)uid, (unsigned int)gid,
              allow_probe, report_broken);

    virHashTablePtr cycle = NULL;
    int ret = -1;

    if (!(cycle = virHashCreate(5, NULL)))
        return -1;

    if (src->format <= VIR_STORAGE_FILE_NONE)
        src->format = allow_probe ?
            VIR_STORAGE_FILE_AUTO : VIR_STORAGE_FILE_RAW;

    ret = virStorageFileGetMetadataRecurse(src, src, uid, gid,
                                           allow_probe, report_broken, cycle);

    virHashFree(cycle);
    return ret;
}


static int
virStorageAddISCSIPoolSourceHost(virDomainDiskDefPtr def,
                                 virStoragePoolDefPtr pooldef)
{
    int ret = -1;
    char **tokens = NULL;

    /* Only support one host */
    if (pooldef->source.nhost != 1) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Expected exactly 1 host for the storage pool"));
        goto cleanup;
    }

    /* iscsi pool only supports one host */
    def->src->nhosts = 1;

    if (VIR_ALLOC_N(def->src->hosts, def->src->nhosts) < 0)
        goto cleanup;

    if (VIR_STRDUP(def->src->hosts[0].name, pooldef->source.hosts[0].name) < 0)
        goto cleanup;

    if (virAsprintf(&def->src->hosts[0].port, "%d",
                    pooldef->source.hosts[0].port ?
                    pooldef->source.hosts[0].port :
                    3260) < 0)
        goto cleanup;

    /* iscsi volume has name like "unit:0:0:1" */
    if (!(tokens = virStringSplit(def->src->srcpool->volume, ":", 0)))
        goto cleanup;

    if (virStringListLength((const char * const *)tokens) != 4) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("unexpected iscsi volume name '%s'"),
                       def->src->srcpool->volume);
        goto cleanup;
    }

    /* iscsi pool has only one source device path */
    if (virAsprintf(&def->src->path, "%s/%s",
                    pooldef->source.devices[0].path,
                    tokens[3]) < 0)
        goto cleanup;

    /* Storage pool have not supported these 2 attributes yet,
     * use the defaults.
     */
    def->src->hosts[0].transport = VIR_STORAGE_NET_HOST_TRANS_TCP;
    def->src->hosts[0].socket = NULL;

    def->src->protocol = VIR_STORAGE_NET_PROTOCOL_ISCSI;

    ret = 0;

 cleanup:
    virStringListFree(tokens);
    return ret;
}


static int
virStorageTranslateDiskSourcePoolAuth(virDomainDiskDefPtr def,
                                      virStoragePoolSourcePtr source)
{
    int ret = -1;

    /* Only necessary when authentication set */
    if (!source->auth) {
        ret = 0;
        goto cleanup;
    }
    def->src->auth = virStorageAuthDefCopy(source->auth);
    if (!def->src->auth)
        goto cleanup;
    /* A <disk> doesn't use <auth type='%s', so clear that out for the disk */
    def->src->auth->authType = VIR_STORAGE_AUTH_TYPE_NONE;
    ret = 0;

 cleanup:
    return ret;
}


int
virStorageTranslateDiskSourcePool(virConnectPtr conn,
                                  virDomainDiskDefPtr def)
{
    virStoragePoolDefPtr pooldef = NULL;
    virStoragePoolPtr pool = NULL;
    virStorageVolPtr vol = NULL;
    char *poolxml = NULL;
    virStorageVolInfo info;
    int ret = -1;

    if (def->src->type != VIR_STORAGE_TYPE_VOLUME)
        return 0;

    if (!def->src->srcpool)
        return 0;

    if (!(pool = virStoragePoolLookupByName(conn, def->src->srcpool->pool)))
        return -1;

    if (virStoragePoolIsActive(pool) != 1) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("storage pool '%s' containing volume '%s' "
                         "is not active"),
                       def->src->srcpool->pool, def->src->srcpool->volume);
        goto cleanup;
    }

    if (!(vol = virStorageVolLookupByName(pool, def->src->srcpool->volume)))
        goto cleanup;

    if (virStorageVolGetInfo(vol, &info) < 0)
        goto cleanup;

    if (!(poolxml = virStoragePoolGetXMLDesc(pool, 0)))
        goto cleanup;

    if (!(pooldef = virStoragePoolDefParseString(poolxml)))
        goto cleanup;

    def->src->srcpool->pooltype = pooldef->type;
    def->src->srcpool->voltype = info.type;

    if (def->src->srcpool->mode && pooldef->type != VIR_STORAGE_POOL_ISCSI) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("disk source mode is only valid when "
                         "storage pool is of iscsi type"));
        goto cleanup;
    }

    VIR_FREE(def->src->path);
    virStorageNetHostDefFree(def->src->nhosts, def->src->hosts);
    def->src->nhosts = 0;
    def->src->hosts = NULL;
    virStorageAuthDefFree(def->src->auth);
    def->src->auth = NULL;

    switch ((virStoragePoolType) pooldef->type) {
    case VIR_STORAGE_POOL_DIR:
    case VIR_STORAGE_POOL_FS:
    case VIR_STORAGE_POOL_NETFS:
    case VIR_STORAGE_POOL_LOGICAL:
    case VIR_STORAGE_POOL_DISK:
    case VIR_STORAGE_POOL_SCSI:
    case VIR_STORAGE_POOL_ZFS:
    case VIR_STORAGE_POOL_VSTORAGE:
        if (!(def->src->path = virStorageVolGetPath(vol)))
            goto cleanup;

        if (def->startupPolicy && info.type != VIR_STORAGE_VOL_FILE) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("'startupPolicy' is only valid for "
                             "'file' type volume"));
            goto cleanup;
        }


        switch (info.type) {
        case VIR_STORAGE_VOL_FILE:
            def->src->srcpool->actualtype = VIR_STORAGE_TYPE_FILE;
            break;

        case VIR_STORAGE_VOL_DIR:
            def->src->srcpool->actualtype = VIR_STORAGE_TYPE_DIR;
            break;

        case VIR_STORAGE_VOL_BLOCK:
            def->src->srcpool->actualtype = VIR_STORAGE_TYPE_BLOCK;
            break;

        case VIR_STORAGE_VOL_PLOOP:
            def->src->srcpool->actualtype = VIR_STORAGE_TYPE_FILE;
            break;

        case VIR_STORAGE_VOL_NETWORK:
        case VIR_STORAGE_VOL_NETDIR:
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("unexpected storage volume type '%s' "
                             "for storage pool type '%s'"),
                           virStorageVolTypeToString(info.type),
                           virStoragePoolTypeToString(pooldef->type));
            goto cleanup;
        }

        break;

    case VIR_STORAGE_POOL_ISCSI:
        if (def->startupPolicy) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("'startupPolicy' is only valid for "
                             "'file' type volume"));
            goto cleanup;
        }

       switch (def->src->srcpool->mode) {
       case VIR_STORAGE_SOURCE_POOL_MODE_DEFAULT:
       case VIR_STORAGE_SOURCE_POOL_MODE_LAST:
           def->src->srcpool->mode = VIR_STORAGE_SOURCE_POOL_MODE_HOST;
           /* fallthrough */
       case VIR_STORAGE_SOURCE_POOL_MODE_HOST:
           def->src->srcpool->actualtype = VIR_STORAGE_TYPE_BLOCK;
           if (!(def->src->path = virStorageVolGetPath(vol)))
               goto cleanup;
           break;

       case VIR_STORAGE_SOURCE_POOL_MODE_DIRECT:
           def->src->srcpool->actualtype = VIR_STORAGE_TYPE_NETWORK;
           def->src->protocol = VIR_STORAGE_NET_PROTOCOL_ISCSI;

           if (virStorageTranslateDiskSourcePoolAuth(def,
                                                     &pooldef->source) < 0)
               goto cleanup;

           /* Source pool may not fill in the secrettype field,
            * so we need to do so here
            */
           if (def->src->auth && !def->src->auth->secrettype) {
               const char *secrettype =
                   virSecretUsageTypeToString(VIR_SECRET_USAGE_TYPE_ISCSI);
               if (VIR_STRDUP(def->src->auth->secrettype, secrettype) < 0)
                   goto cleanup;
           }

           if (virStorageAddISCSIPoolSourceHost(def, pooldef) < 0)
               goto cleanup;
           break;
       }
       break;

    case VIR_STORAGE_POOL_MPATH:
    case VIR_STORAGE_POOL_RBD:
    case VIR_STORAGE_POOL_SHEEPDOG:
    case VIR_STORAGE_POOL_GLUSTER:
    case VIR_STORAGE_POOL_LAST:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("using '%s' pools for backing 'volume' disks "
                         "isn't yet supported"),
                       virStoragePoolTypeToString(pooldef->type));
        goto cleanup;
    }

    ret = 0;
 cleanup:
    virObjectUnref(pool);
    virObjectUnref(vol);
    VIR_FREE(poolxml);
    virStoragePoolDefFree(pooldef);
    return ret;
}


/*
 * virStoragePoolObjFindPoolByUUID
 * @uuid: The uuid to lookup
 *
 * Using the passed @uuid, search the driver pools for a matching uuid.
 * If found, then lock the pool
 *
 * Returns NULL if pool is not found or a locked pool object pointer
 */
virPoolObjPtr
virStoragePoolObjFindPoolByUUID(const unsigned char *uuid)
{
    return storagePoolObjFindByUUID(uuid, NULL);
}


/*
 * virStoragePoolBuildTempFilePath
 * @def: pool definition pointer
 * @vol: volume definition
 *
 * Generate a name for a temporary file using the driver stateDir
 * as a path, the pool name, and the volume name to be used as input
 * for a mkostemp
 *
 * Returns a string pointer on success, NULL on failure
 */
char *
virStoragePoolBuildTempFilePath(virStoragePoolDefPtr def,
                                virStorageVolDefPtr vol)

{
    char *tmp = NULL;

    ignore_value(virAsprintf(&tmp, "%s/%s.%s.secret.XXXXXX",
                             driver->stateDir, def->name, vol->name));
    return tmp;
}
