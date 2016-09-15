/*
 * fs_driver.c: file system driver implementation
 * Author: Olga Krishtal <okrishtal@virtuozzo.com>
 *
 * Copyright (C) 2016 Parallels IP Holdings GmbH
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

#include <config.h>

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>

#include <errno.h>
#include <string.h>

#include "virerror.h"
#include "datatypes.h"
#include "driver.h"
#include "fs_driver.h"
#include "fs_conf.h"
#include "fs_backend.h"
#include "viralloc.h"
#include "virlog.h"
#include "virfile.h"
#include "fdstream.h"
#include "configmake.h"
#include "virstring.h"
#include "viraccessapicheck.h"
#include "dirname.h"

#if WITH_FS_DIR
# include "fs_backend_dir.h"
#endif

#define VIR_FROM_THIS VIR_FROM_FSPOOL

VIR_LOG_INIT("fs.fs_driver");

static virFSDriverStatePtr driver;

static int fsStateCleanup(void);

static void fsDriverLock(void)
{
    virMutexLock(&driver->lock);
}

static void fsDriverUnlock(void)
{
    virMutexUnlock(&driver->lock);
}

static virFSBackendPtr backends[] = {
#if WITH_FS_DIR
    &virFSBackendDir,
#endif
};

static virFSBackendPtr
virFSBackendForType(int type)
{
    size_t i;
    for (i = 0; backends[i]; i++)
        if (backends[i]->type == type)
            return backends[i];

    virReportError(VIR_ERR_INTERNAL_ERROR,
                   _("missing backend for fspool type %d (%s)"),
                   type, NULLSTR(virFSPoolTypeToString(type)));
    return NULL;
}

static void
fsItemRemoveFromFSPool(virFSPoolObjPtr fspool,
                       virFSItemDefPtr item)
{
    size_t i;

    for (i = 0; i < fspool->items.count; i++) {
        if (fspool->items.objs[i] == item) {
            VIR_INFO("Deleting item '%s' from fspool '%s'",
                     item->name, fspool->def->name);
            virFSItemDefFree(item);

            VIR_DELETE_ELEMENT(fspool->items.objs, i, fspool->items.count);
            break;
        }
    }
}

static int
fsItemDeleteInternal(virFSItemPtr obj,
                     virFSBackendPtr backend,
                     virFSPoolObjPtr fspool,
                     virFSItemDefPtr item,
                     unsigned int flags)
{
    int ret = -1;

    virCheckFlags(0, -1);

    if (!backend->deleteItem) {
        virReportError(VIR_ERR_NO_SUPPORT,
                  "%s", _("fspool does not support item deletion"));
        goto cleanup;
    }
    if (backend->deleteItem(obj->conn, fspool, item, flags) < 0)
        goto cleanup;

    fsItemRemoveFromFSPool(fspool, item);

    ret = 0;

 cleanup:
    return ret;
}

static virFSItemDefPtr
virFSItemDefFromItem(virFSItemPtr obj,
                     virFSPoolObjPtr *fspool,
                     virFSBackendPtr *backend)
{
    virFSItemDefPtr item = NULL;

    *fspool = NULL;

    fsDriverLock();
    *fspool = virFSPoolObjFindByName(&driver->fspools, obj->fspool);
    fsDriverUnlock();

    if (!*fspool) {
        virReportError(VIR_ERR_NO_FSPOOL,
                       _("no fspool with matching name '%s'"),
                       obj->fspool);
        return NULL;
    }

    if (!virFSPoolObjIsActive(*fspool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("fspool '%s' is not active"),
                       (*fspool)->def->name);
        goto error;
    }

    if (!(item = virFSItemDefFindByName(*fspool, obj->name))) {
        virReportError(VIR_ERR_NO_FSITEM,
                       _("no fsitem with matching name '%s'"),
                       obj->name);
        goto error;
    }

    if (backend) {
        if (!(*backend = virFSBackendForType((*fspool)->def->type)))
            goto error;
    }

    return item;

 error:
    virFSPoolObjUnlock(*fspool);
    *fspool = NULL;

    return NULL;
}

static void
fsPoolUpdateState(virFSPoolObjPtr fspool)
{
    bool active;
    virFSBackendPtr backend;
    int ret = -1;
    char *stateFile;

    if (!(stateFile = virFileBuildPath(driver->stateDir,
                                       fspool->def->name, ".xml")))
        goto error;

    if ((backend = virFSBackendForType(fspool->def->type)) == NULL) {
        VIR_ERROR(_("Missing backend %d"), fspool->def->type);
        goto error;
    }

    /* Backends which do not support 'checkFSpool' are considered
     * inactive by default.
     */
    active = false;
    if (backend->checkFSpool &&
        backend->checkFSpool(fspool, &active) < 0) {
        virErrorPtr err = virGetLastError();
        VIR_ERROR(_("Failed to initialize fspool '%s': %s"),
                  fspool->def->name, err ? err->message :
                  _("no error message found"));
        goto error;
    }

    /* We can pass NULL as connection, most backends do not use
     * it anyway, but if they do and fail, we want to log error and
     * continue with other fspools.
     */
    if (active) {
        virFSPoolObjClearItems(fspool);
        if (backend->refreshFSpool(NULL, fspool) < 0) {
            virErrorPtr err = virGetLastError();
            if (backend->stopFSpool)
                backend->stopFSpool(NULL, fspool);
            VIR_ERROR(_("Failed to restart fspool '%s': %s"),
                      fspool->def->name, err ? err->message :
                      _("no error message found"));
            goto error;
        }
    }

    fspool->active = active;
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
fsPoolUpdateAllState(void)
{
    size_t i;

    for (i = 0; i < driver->fspools.count; i++) {
        virFSPoolObjPtr fspool = driver->fspools.objs[i];

        virFSPoolObjLock(fspool);
        fsPoolUpdateState(fspool);
        virFSPoolObjUnlock(fspool);
    }
}

static void
fsDriverAutostart(void)
{
    size_t i;
    virConnectPtr conn = NULL;

    /* XXX Remove hardcoding of QEMU URI */
    if (driver->privileged)
        conn = virConnectOpen("qemu:///system");
    else
        conn = virConnectOpen("qemu:///session");
    /* Ignoring NULL conn - let backends decide */

   for (i = 0; i < driver->fspools.count; i++) {
        virFSPoolObjPtr fspool = driver->fspools.objs[i];
        virFSBackendPtr backend;
        bool started = false;

        virFSPoolObjLock(fspool);
        if ((backend = virFSBackendForType(fspool->def->type)) == NULL) {
            virFSPoolObjUnlock(fspool);
            continue;
        }

        if (fspool->autostart &&
            !virFSPoolObjIsActive(fspool)) {
            if (backend->startFSpool &&
                backend->startFSpool(conn, fspool) < 0) {
                virErrorPtr err = virGetLastError();
                VIR_ERROR(_("Failed to autostart fspool '%s': %s"),
                          fspool->def->name, err ? err->message :
                          _("no error message found"));
                virFSPoolObjUnlock(fspool);
                continue;
            }
            started = true;
        }

        if (started) {
            char *stateFile;

            virFSPoolObjClearItems(fspool);
            stateFile = virFileBuildPath(driver->stateDir,
                                         fspool->def->name, ".xml");
            if (!stateFile ||
                virFSPoolSaveState(stateFile, fspool->def) < 0 ||
                backend->refreshFSpool(conn, fspool) < 0) {
                virErrorPtr err = virGetLastError();
                if (stateFile)
                    unlink(stateFile);
                if (backend->stopFSpool)
                    backend->stopFSpool(conn, fspool);
                VIR_ERROR(_("Failed to autostart fspool '%s': %s"),
                          fspool->def->name, err ? err->message :
                          _("no error message found"));
            } else {
                fspool->active = true;
            }
            VIR_FREE(stateFile);
        }
        virFSPoolObjUnlock(fspool);
    }

    virObjectUnref(conn);
}

/**
 * virFSStartup:
 *
 * Initialization function for the FS Driver
 */
static int
fsStateInitialize(bool privileged,
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
    fsDriverLock();

    if (privileged) {
        if (VIR_STRDUP(driver->configDir,
                       SYSCONFDIR "/libvirt/fs") < 0 ||
            VIR_STRDUP(driver->autostartDir,
                       SYSCONFDIR "/libvirt/fs/autostart") < 0 ||
            VIR_STRDUP(driver->stateDir,
                       LOCALSTATEDIR "/run/libvirt/fs") < 0)
            goto error;
    } else {
        configdir = virGetUserConfigDirectory();
        rundir = virGetUserRuntimeDirectory();
        if (!(configdir && rundir))
            goto error;

        if ((virAsprintf(&driver->configDir,
                        "%s/fs", configdir) < 0) ||
            (virAsprintf(&driver->autostartDir,
                        "%s/fs/autostart", configdir) < 0) ||
            (virAsprintf(&driver->stateDir,
                         "%s/fs/run", rundir) < 0))
            goto error;
    }
    driver->privileged = privileged;

    if (virFileMakePath(driver->stateDir) < 0) {
        virReportError(errno,
                       _("cannot create directory %s"),
                       driver->stateDir);
        goto error;
    }

    if (virFSPoolLoadAllState(&driver->fspools,
                                   driver->stateDir) < 0)
        goto error;

    if (virFSPoolLoadAllConfigs(&driver->fspools,
                                     driver->configDir,
                                     driver->autostartDir) < 0)
        goto error;

    fsPoolUpdateAllState();

    fsDriverUnlock();

    ret = 0;
 cleanup:
    VIR_FREE(configdir);
    VIR_FREE(rundir);
    return ret;

 error:
    fsDriverUnlock();
    fsStateCleanup();
    goto cleanup;
}

/**
 * fsStateAutoStart:
 *
 * Function to auto start the fs_driver
 */
static void
fsStateAutoStart(void)
{
    if (!driver)
        return;

    fsDriverLock();
    fsDriverAutostart();
    fsDriverUnlock();
}

/**
 * fsStateReload:
 *
 * Function to restart the fs_driver, it will recheck the configuration
 * files and update its state
 */
static int
fsStateReload(void)
{
    if (!driver)
        return -1;

    fsDriverLock();
    virFSPoolLoadAllState(&driver->fspools,
                          driver->stateDir);
    virFSPoolLoadAllConfigs(&driver->fspools,
                            driver->configDir,
                            driver->autostartDir);
    fsDriverAutostart();
    fsDriverUnlock();

    return 0;
}


/**
 * fsStateCleanup
 *
 * Shutdown the fs driver, it will stop all active fspools
 */
static int
fsStateCleanup(void)
{
    if (!driver)
        return -1;

    fsDriverLock();

    /* free inactive fspools */
    virFSPoolObjListFree(&driver->fspools);

    VIR_FREE(driver->configDir);
    VIR_FREE(driver->autostartDir);
    VIR_FREE(driver->stateDir);
    fsDriverUnlock();
    virMutexDestroy(&driver->lock);
    VIR_FREE(driver);

    return 0;
}


static virFSPoolObjPtr
virFSPoolObjFromFSPool(virFSPoolPtr fspool)
{
    char uuidstr[VIR_UUID_STRING_BUFLEN];
    virFSPoolObjPtr ret;

    fsDriverLock();
    if (!(ret = virFSPoolObjFindByUUID(&driver->fspools, fspool->uuid))) {
        virUUIDFormat(fspool->uuid, uuidstr);
        virReportError(VIR_ERR_NO_FSPOOL,
                       _("no fspool with matching uuid '%s' (%s)"),
                       uuidstr, fspool->name);
    }
    fsDriverUnlock();

    return ret;
}

static int
fsConnectListAllFSPools(virConnectPtr conn,
                        virFSPoolPtr **fspools,
                        unsigned int flags)
{
    int ret = -1;

    virCheckFlags(VIR_CONNECT_LIST_FSPOOLS_FILTERS_ALL, -1);

    if (virConnectListAllFSPoolsEnsureACL(conn) < 0)
        goto cleanup;

    fsDriverLock();
    ret = virFSPoolObjListExport(conn, driver->fspools, fspools,
                                 virConnectListAllFSPoolsCheckACL,
                                 flags);
    fsDriverUnlock();

 cleanup:
    return ret;
}

static virFSPoolPtr
fsPoolLookupByUUID(virConnectPtr conn,
                   const unsigned char *uuid)
{
    virFSPoolObjPtr fspool;
    virFSPoolPtr ret = NULL;

    fsDriverLock();
    fspool = virFSPoolObjFindByUUID(&driver->fspools, uuid);
    fsDriverUnlock();

    if (!fspool) {
        char uuidstr[VIR_UUID_STRING_BUFLEN];
        virUUIDFormat(uuid, uuidstr);
        virReportError(VIR_ERR_NO_FSPOOL,
                       _("no fspool with matching uuid '%s'"), uuidstr);
        return NULL;
    }

    if (virFSPoolLookupByUUIDEnsureACL(conn, fspool->def) < 0)
        goto cleanup;

    ret = virGetFSPool(conn, fspool->def->name, fspool->def->uuid,
                       NULL, NULL);

 cleanup:
    virFSPoolObjUnlock(fspool);
    return ret;
}

static virFSPoolPtr
fsPoolLookupByName(virConnectPtr conn,
                   const char *name)
{
    virFSPoolObjPtr fspool;
    virFSPoolPtr ret = NULL;

    fsDriverLock();
    fspool = virFSPoolObjFindByName(&driver->fspools, name);
    fsDriverUnlock();

    if (!fspool) {
        virReportError(VIR_ERR_NO_FSPOOL,
                       _("no fspool with matching name '%s'"), name);
        return NULL;
    }

    if (virFSPoolLookupByNameEnsureACL(conn, fspool->def) < 0)
        goto cleanup;

    ret = virGetFSPool(conn, fspool->def->name, fspool->def->uuid,
                       NULL, NULL);

 cleanup:
    virFSPoolObjUnlock(fspool);
    return ret;
}

static virFSPoolPtr
fsPoolLookupByItem(virFSItemPtr item)
{
    virFSPoolObjPtr fspool;
    virFSPoolPtr ret = NULL;

    fsDriverLock();
    fspool = virFSPoolObjFindByName(&driver->fspools, item->fspool);
    fsDriverUnlock();

    if (!fspool) {
        virReportError(VIR_ERR_NO_FSPOOL,
                       _("no fspool with matching name '%s'"),
                       item->fspool);
        return NULL;
    }

    if (virFSPoolLookupByItemEnsureACL(item->conn, fspool->def) < 0)
        goto cleanup;

    ret = virGetFSPool(item->conn, fspool->def->name, fspool->def->uuid,
                            NULL, NULL);

 cleanup:
    virFSPoolObjUnlock(fspool);
    return ret;
}


static virFSPoolPtr
fsPoolCreateXML(virConnectPtr conn,
                const char *xml,
                unsigned int flags)
{
    virFSPoolDefPtr def;
    virFSPoolObjPtr fspool = NULL;
    virFSPoolPtr ret = NULL;
    virFSBackendPtr backend;
    char *stateFile = NULL;
    unsigned int build_flags = 0;

    virCheckFlags(VIR_FSPOOL_CREATE_WITH_BUILD |
                  VIR_FSPOOL_CREATE_WITH_BUILD_OVERWRITE |
                  VIR_FSPOOL_CREATE_WITH_BUILD_NO_OVERWRITE, NULL);

    VIR_EXCLUSIVE_FLAGS_RET(VIR_FSPOOL_BUILD_OVERWRITE,
                            VIR_FSPOOL_BUILD_NO_OVERWRITE, NULL);

    fsDriverLock();
    if (!(def = virFSPoolDefParseString(xml)))
        goto cleanup;

    if (virFSPoolCreateXMLEnsureACL(conn, def) < 0)
        goto cleanup;

    if (virFSPoolObjIsDuplicate(&driver->fspools, def, 1) < 0)
        goto cleanup;

    if (virFSPoolSourceFindDuplicate(conn, &driver->fspools, def) < 0)
        goto cleanup;

    if ((backend = virFSBackendForType(def->type)) == NULL)
        goto cleanup;

    if (!(fspool = virFSPoolObjAssignDef(&driver->fspools, def)))
        goto cleanup;
    def = NULL;

    if (backend->buildFSpool) {
        if (flags & VIR_FSPOOL_CREATE_WITH_BUILD_OVERWRITE)
            build_flags |= VIR_FSPOOL_BUILD_OVERWRITE;
        else if (flags & VIR_FSPOOL_CREATE_WITH_BUILD_NO_OVERWRITE)
            build_flags |= VIR_FSPOOL_BUILD_NO_OVERWRITE;

        if (build_flags ||
            (flags & VIR_FSPOOL_CREATE_WITH_BUILD)) {
            if (backend->buildFSpool(conn, fspool, build_flags) < 0) {
                virFSPoolObjRemove(&driver->fspools, fspool);
                fspool = NULL;
                goto cleanup;
            }
        }
    }

    if (backend->startFSpool &&
        backend->startFSpool(conn, fspool) < 0) {
        virFSPoolObjRemove(&driver->fspools, fspool);
        fspool = NULL;
        goto cleanup;
    }

    stateFile = virFileBuildPath(driver->stateDir,
                                 fspool->def->name, ".xml");

    if (!stateFile || virFSPoolSaveState(stateFile, fspool->def) < 0 ||
        backend->refreshFSpool(conn, fspool) < 0) {
        if (stateFile)
            unlink(stateFile);
        if (backend->stopFSpool)
            backend->stopFSpool(conn, fspool);
        virFSPoolObjRemove(&driver->fspools, fspool);
        fspool = NULL;
        goto cleanup;
    }
    VIR_INFO("Creating fspool '%s'", fspool->def->name);
    fspool->active = true;

    ret = virGetFSPool(conn, fspool->def->name, fspool->def->uuid,
                       NULL, NULL);

 cleanup:
    VIR_FREE(stateFile);
    virFSPoolDefFree(def);
    if (fspool)
        virFSPoolObjUnlock(fspool);
    fsDriverUnlock();
    return ret;
}

static virFSPoolPtr
fsPoolDefineXML(virConnectPtr conn,
                const char *xml,
                unsigned int flags)
{
    virFSPoolDefPtr def;
    virFSPoolObjPtr fspool = NULL;
    virFSPoolPtr ret = NULL;

    virCheckFlags(0, NULL);

    fsDriverLock();
    if (!(def = virFSPoolDefParseString(xml)))
        goto cleanup;

    if (virFSPoolDefineXMLEnsureACL(conn, def) < 0)
        goto cleanup;

    if (virFSPoolObjIsDuplicate(&driver->fspools, def, 0) < 0)
        goto cleanup;

    if (virFSPoolSourceFindDuplicate(conn, &driver->fspools, def) < 0)
        goto cleanup;

    if (virFSBackendForType(def->type) == NULL)
        goto cleanup;

    if (!(fspool = virFSPoolObjAssignDef(&driver->fspools, def)))
        goto cleanup;

    if (virFSPoolObjSaveDef(driver, fspool, def) < 0) {
        virFSPoolObjRemove(&driver->fspools, fspool);
        def = NULL;
        fspool = NULL;
        goto cleanup;
    }
    def = NULL;

    VIR_INFO("Defining fspool '%s'", fspool->def->name);
    ret = virGetFSPool(conn, fspool->def->name, fspool->def->uuid,
                            NULL, NULL);

 cleanup:
    virFSPoolDefFree(def);
    if (fspool)
        virFSPoolObjUnlock(fspool);
    fsDriverUnlock();
    return ret;
}

static int
fsPoolCreate(virFSPoolPtr obj,
             unsigned int flags)
{
    virFSPoolObjPtr fspool;
    virFSBackendPtr backend;
    int ret = -1;
    char *stateFile = NULL;
    unsigned int build_flags = 0;

    virCheckFlags(VIR_FSPOOL_CREATE_WITH_BUILD |
                  VIR_FSPOOL_CREATE_WITH_BUILD_OVERWRITE |
                  VIR_FSPOOL_CREATE_WITH_BUILD_NO_OVERWRITE, -1);

    VIR_EXCLUSIVE_FLAGS_RET(VIR_FSPOOL_BUILD_OVERWRITE,
                            VIR_FSPOOL_BUILD_NO_OVERWRITE, -1);

    if (!(fspool = virFSPoolObjFromFSPool(obj)))
        return -1;

    if (virFSPoolCreateEnsureACL(obj->conn, fspool->def) < 0)
        goto cleanup;

    if ((backend = virFSBackendForType(fspool->def->type)) == NULL)
        goto cleanup;

    if (virFSPoolObjIsActive(fspool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("fspool '%s' is already active"),
                       fspool->def->name);
        goto cleanup;
    }

    if (backend->buildFSpool) {
        if (flags & VIR_FSPOOL_CREATE_WITH_BUILD_OVERWRITE)
            build_flags |= VIR_FSPOOL_BUILD_OVERWRITE;
        else if (flags & VIR_FSPOOL_CREATE_WITH_BUILD_NO_OVERWRITE)
            build_flags |= VIR_FSPOOL_BUILD_NO_OVERWRITE;

        if (build_flags ||
            (flags & VIR_FSPOOL_CREATE_WITH_BUILD)) {
            if (backend->buildFSpool(obj->conn, fspool, build_flags) < 0) {
                virFSPoolObjRemove(&driver->fspools, fspool);
                fspool = NULL;
                goto cleanup;
            }
        }
    }

    VIR_INFO("Starting up fspool '%s'", fspool->def->name);
    if (backend->startFSpool &&
        backend->startFSpool(obj->conn, fspool) < 0)
        goto cleanup;

    stateFile = virFileBuildPath(driver->stateDir,
                                 fspool->def->name, ".xml");

    virFSPoolObjClearItems(fspool);
    if (!stateFile || virFSPoolSaveState(stateFile, fspool->def) < 0 ||
        backend->refreshFSpool(obj->conn, fspool) < 0) {
        if (stateFile)
            unlink(stateFile);
        goto cleanup;
    }

    fspool->active = true;
    ret = 0;

 cleanup:
    VIR_FREE(stateFile);
    if (fspool)
        virFSPoolObjUnlock(fspool);
    return ret;
}

static int
fsPoolBuild(virFSPoolPtr obj,
            unsigned int flags)
{
    virFSPoolObjPtr fspool;
    virFSBackendPtr backend;
    int ret = -1;

    virCheckFlags(0, -1);

    if (!(fspool = virFSPoolObjFromFSPool(obj)))
        return -1;

    if (virFSPoolBuildEnsureACL(obj->conn, fspool->def) < 0)
        goto cleanup;

    if ((backend = virFSBackendForType(fspool->def->type)) == NULL)
        goto cleanup;

    if (virFSPoolObjIsActive(fspool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("fspool '%s' is already active"),
                       fspool->def->name);
        goto cleanup;
    }

    if (backend->buildFSpool &&
        backend->buildFSpool(obj->conn, fspool, flags) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virFSPoolObjUnlock(fspool);
    return ret;
}

static int
fsPoolUndefine(virFSPoolPtr obj)
{
    virFSPoolObjPtr fspool;
    int ret = -1;

    fsDriverLock();
    if (!(fspool = virFSPoolObjFindByUUID(&driver->fspools, obj->uuid))) {
        char uuidstr[VIR_UUID_STRING_BUFLEN];
        virUUIDFormat(obj->uuid, uuidstr);
        virReportError(VIR_ERR_NO_FSPOOL,
                       _("no fspool with matching uuid '%s' (%s)"),
                       uuidstr, obj->name);
        goto cleanup;
    }

    if (virFSPoolUndefineEnsureACL(obj->conn, fspool->def) < 0)
        goto cleanup;

    if (virFSPoolObjIsActive(fspool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("fspool '%s' is still active"),
                       fspool->def->name);
        goto cleanup;
    }

    if (fspool->asyncjobs > 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("fspool '%s' has asynchronous jobs running."),
                       fspool->def->name);
        goto cleanup;
    }

    if (virFSPoolObjDeleteDef(fspool) < 0)
        goto cleanup;

    if (unlink(fspool->autostartLink) < 0 &&
        errno != ENOENT &&
        errno != ENOTDIR) {
        char ebuf[1024];
        VIR_ERROR(_("Failed to delete autostart link '%s': %s"),
                  fspool->autostartLink, virStrerror(errno, ebuf, sizeof(ebuf)));
    }

    VIR_FREE(fspool->configFile);
    VIR_FREE(fspool->autostartLink);

    VIR_INFO("Undefining fspool '%s'", fspool->def->name);
    virFSPoolObjRemove(&driver->fspools, fspool);
    fspool = NULL;
    ret = 0;

 cleanup:
    if (fspool)
        virFSPoolObjUnlock(fspool);
    fsDriverUnlock();
    return ret;
}

static int
fsPoolDestroy(virFSPoolPtr obj)
{
    virFSPoolObjPtr fspool;
    virFSBackendPtr backend;
    char *stateFile = NULL;
    int ret = -1;

    fsDriverLock();
    if (!(fspool = virFSPoolObjFindByUUID(&driver->fspools, obj->uuid))) {
        char uuidstr[VIR_UUID_STRING_BUFLEN];
        virUUIDFormat(obj->uuid, uuidstr);
        virReportError(VIR_ERR_NO_FSPOOL,
                       _("no fspool with matching uuid '%s' (%s)"),
                       uuidstr, obj->name);
        goto cleanup;
    }

    if (virFSPoolDestroyEnsureACL(obj->conn, fspool->def) < 0)
        goto cleanup;

    if ((backend = virFSBackendForType(fspool->def->type)) == NULL)
        goto cleanup;

    VIR_INFO("Destroying fspool '%s'", fspool->def->name);

    if (!virFSPoolObjIsActive(fspool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("fspool '%s' is not active"), fspool->def->name);
        goto cleanup;
    }

    if (fspool->asyncjobs > 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("fspool '%s' has asynchronous jobs running."),
                       fspool->def->name);
        goto cleanup;
    }

    if (!(stateFile = virFileBuildPath(driver->stateDir,
                                       fspool->def->name,
                                       ".xml")))
        goto cleanup;

    unlink(stateFile);
    VIR_FREE(stateFile);

    if (backend->stopFSpool &&
        backend->stopFSpool(obj->conn, fspool) < 0)
        goto cleanup;

    virFSPoolObjClearItems(fspool);

    fspool->active = false;

    if (fspool->configFile == NULL) {
        virFSPoolObjRemove(&driver->fspools, fspool);
        fspool = NULL;
    } else if (fspool->newDef) {
        virFSPoolDefFree(fspool->def);
        fspool->def = fspool->newDef;
        fspool->newDef = NULL;
    }

    ret = 0;

 cleanup:
    if (fspool)
        virFSPoolObjUnlock(fspool);
    fsDriverUnlock();
    return ret;
}

static int
fsPoolDelete(virFSPoolPtr obj,
             unsigned int flags)
{
    virFSPoolObjPtr fspool;
    virFSBackendPtr backend;
    char *stateFile = NULL;
    int ret = -1;

    virCheckFlags(0, -1);

    if (!(fspool = virFSPoolObjFromFSPool(obj)))
        return -1;

    if (virFSPoolDeleteEnsureACL(obj->conn, fspool->def) < 0)
        goto cleanup;

    if ((backend = virFSBackendForType(fspool->def->type)) == NULL)
        goto cleanup;

    VIR_INFO("Deleting fspool '%s'", fspool->def->name);

    if (virFSPoolObjIsActive(fspool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("fspool '%s' is still active"),
                       fspool->def->name);
        goto cleanup;
    }

    if (fspool->asyncjobs > 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("fspool '%s' has asynchronous jobs running."),
                       fspool->def->name);
        goto cleanup;
    }

    if (!(stateFile = virFileBuildPath(driver->stateDir,
                                       fspool->def->name,
                                       ".xml")))
        goto cleanup;

    unlink(stateFile);
    VIR_FREE(stateFile);

    if (!backend->deleteFSpool) {
        virReportError(VIR_ERR_NO_SUPPORT,
                       "%s", _("fspool does not support fspool deletion"));
        goto cleanup;
    }
    if (backend->deleteFSpool(obj->conn, fspool, flags) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virFSPoolObjUnlock(fspool);
    return ret;
}

static int
fsPoolRefresh(virFSPoolPtr obj,
              unsigned int flags)
{
    virFSPoolObjPtr fspool;
    virFSBackendPtr backend;
    int ret = -1;

    virCheckFlags(0, -1);

    fsDriverLock();
    if (!(fspool = virFSPoolObjFindByUUID(&driver->fspools, obj->uuid))) {
        char uuidstr[VIR_UUID_STRING_BUFLEN];
        virUUIDFormat(obj->uuid, uuidstr);
        virReportError(VIR_ERR_NO_FSPOOL,
                       _("no fspool with matching uuid '%s' (%s)"),
                       uuidstr, obj->name);
        goto cleanup;
    }

    if (virFSPoolRefreshEnsureACL(obj->conn, fspool->def) < 0)
        goto cleanup;

    if ((backend = virFSBackendForType(fspool->def->type)) == NULL)
        goto cleanup;

    if (!virFSPoolObjIsActive(fspool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("fspool '%s' is not active"), fspool->def->name);
        goto cleanup;
    }

    if (fspool->asyncjobs > 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("fspool '%s' has asynchronous jobs running."),
                       fspool->def->name);
        goto cleanup;
    }

    virFSPoolObjClearItems(fspool);
    if (backend->refreshFSpool(obj->conn, fspool) < 0) {
        if (backend->stopFSpool)
            backend->stopFSpool(obj->conn, fspool);

        fspool->active = false;

        if (fspool->configFile == NULL) {
            virFSPoolObjRemove(&driver->fspools, fspool);
            fspool = NULL;
        }
        goto cleanup;
    }
    ret = 0;

 cleanup:
    if (fspool)
        virFSPoolObjUnlock(fspool);
    fsDriverUnlock();
    return ret;

    return 0;
}


static int
fsPoolGetInfo(virFSPoolPtr obj,
              virFSPoolInfoPtr info)
{
    virFSPoolObjPtr fspool;
    int ret = -1;

    if (!(fspool = virFSPoolObjFromFSPool(obj)))
        return -1;

    if (virFSPoolGetInfoEnsureACL(obj->conn, fspool->def) < 0)
        goto cleanup;

    if (virFSBackendForType(fspool->def->type) == NULL)
        goto cleanup;

    memset(info, 0, sizeof(virFSPoolInfo));
    if (fspool->active)
        info->state = VIR_FSPOOL_RUNNING;
    else
        info->state = VIR_FSPOOL_INACTIVE;
    info->capacity = fspool->def->capacity;
    info->allocation = fspool->def->allocation;
    info->available = fspool->def->available;
    ret = 0;

 cleanup:
    virFSPoolObjUnlock(fspool);
    return ret;
}

static char *
fsPoolGetXMLDesc(virFSPoolPtr obj,
                 unsigned int flags)
{
    virFSPoolObjPtr fspool;
    virFSPoolDefPtr def;
    char *ret = NULL;

    virCheckFlags(VIR_FS_XML_INACTIVE, NULL);

    if (!(fspool = virFSPoolObjFromFSPool(obj)))
        return NULL;

    if (virFSPoolGetXMLDescEnsureACL(obj->conn, fspool->def) < 0)
        goto cleanup;

    if ((flags & VIR_FS_XML_INACTIVE) && fspool->newDef)
        def = fspool->newDef;
    else
        def = fspool->def;

    ret = virFSPoolDefFormat(def);

 cleanup:
    virFSPoolObjUnlock(fspool);
    return ret;
}

static int
fsPoolGetAutostart(virFSPoolPtr obj, int *autostart)
{
    virFSPoolObjPtr fspool;
    int ret = -1;

    if (!(fspool = virFSPoolObjFromFSPool(obj)))
        return -1;

    if (virFSPoolGetAutostartEnsureACL(obj->conn, fspool->def) < 0)
        goto cleanup;

    if (!fspool->configFile) {
        *autostart = 0;
    } else {
        *autostart = fspool->autostart;
    }
    ret = 0;

 cleanup:
    virFSPoolObjUnlock(fspool);
    return ret;
}

static int
fsPoolSetAutostart(virFSPoolPtr obj, int autostart)
{
    virFSPoolObjPtr fspool;
    int ret = -1;

    fsDriverLock();
    fspool = virFSPoolObjFindByUUID(&driver->fspools, obj->uuid);

    if (!fspool) {
        char uuidstr[VIR_UUID_STRING_BUFLEN];
        virUUIDFormat(obj->uuid, uuidstr);
        virReportError(VIR_ERR_NO_FSPOOL,
                       _("no fspool with matching uuid '%s' (%s)"),
                       uuidstr, obj->name);
        goto cleanup;
    }

    if (virFSPoolSetAutostartEnsureACL(obj->conn, fspool->def) < 0)
        goto cleanup;

    if (!fspool->configFile) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("fspool has no config file"));
        goto cleanup;
    }

    autostart = (autostart != 0);

    if (fspool->autostart != autostart) {
        if (autostart) {
            if (virFileMakePath(driver->autostartDir) < 0) {
                virReportSystemError(errno,
                                     _("cannot create autostart directory %s"),
                                     driver->autostartDir);
                goto cleanup;
            }

            if (symlink(fspool->configFile, fspool->autostartLink) < 0) {
                virReportSystemError(errno,
                                     _("Failed to create symlink '%s' to '%s'"),
                                     fspool->autostartLink, fspool->configFile);
                goto cleanup;
            }
        } else {
            if (unlink(fspool->autostartLink) < 0 &&
                errno != ENOENT && errno != ENOTDIR) {
                virReportSystemError(errno,
                                     _("Failed to delete symlink '%s'"),
                                     fspool->autostartLink);
                goto cleanup;
            }
        }
        fspool->autostart = autostart;
    }
    ret = 0;

 cleanup:
    if (fspool)
        virFSPoolObjUnlock(fspool);
    fsDriverUnlock();
    return ret;
}

static int
fsPoolNumOfItems(virFSPoolPtr obj)
{
    virFSPoolObjPtr fspool;
    int ret = -1;
    size_t i;

    if (!(fspool = virFSPoolObjFromFSPool(obj)))
        return -1;

    if (virFSPoolNumOfItemsEnsureACL(obj->conn, fspool->def) < 0)
        goto cleanup;

    if (!virFSPoolObjIsActive(fspool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("fspool '%s' is not active"), fspool->def->name);
        goto cleanup;
    }
    ret = 0;
    for (i = 0; i < fspool->items.count; i++) {
        if (virFSPoolNumOfItemsCheckACL(obj->conn, fspool->def,
                                               fspool->items.objs[i]))
            ret++;
    }

 cleanup:
    virFSPoolObjUnlock(fspool);
    return ret;
}

static int
fsPoolListItems(virFSPoolPtr obj,
                char **const names,
                int maxnames)
{
    virFSPoolObjPtr fspool;
    size_t i;
    int n = 0;

    memset(names, 0, maxnames * sizeof(*names));

    if (!(fspool = virFSPoolObjFromFSPool(obj)))
        return -1;

    if (virFSPoolListItemsEnsureACL(obj->conn, fspool->def) < 0)
        goto cleanup;

    if (!virFSPoolObjIsActive(fspool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("fspool '%s' is not active"), fspool->def->name);
        goto cleanup;
    }

    for (i = 0; i < fspool->items.count && n < maxnames; i++) {
        if (!virFSPoolListItemsCheckACL(obj->conn, fspool->def,
                                        fspool->items.objs[i]))
            continue;
        if (VIR_STRDUP(names[n++], fspool->items.objs[i]->name) < 0)
            goto cleanup;
    }

    virFSPoolObjUnlock(fspool);
    return n;

 cleanup:
    virFSPoolObjUnlock(fspool);
    for (n = 0; n < maxnames; n++)
        VIR_FREE(names[n]);

    memset(names, 0, maxnames * sizeof(*names));
    return -1;
}

static int
fsPoolListAllItems(virFSPoolPtr fspool,
                   virFSItemPtr **items,
                   unsigned int flags)
{
    virFSPoolObjPtr obj;
    size_t i;
    virFSItemPtr *tmp_items = NULL;
    virFSItemPtr item = NULL;
    int nitems = 0;
    int ret = -1;

    virCheckFlags(0, -1);

    if (!(obj = virFSPoolObjFromFSPool(fspool)))
        return -1;

    if (virFSPoolListAllItemsEnsureACL(fspool->conn, obj->def) < 0)
        goto cleanup;

    if (!virFSPoolObjIsActive(obj)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("fspool '%s' is not active"), obj->def->name);
        goto cleanup;
    }

     /* Just returns the items count */
    if (!items) {
        ret = obj->items.count;
        goto cleanup;
    }

    if (VIR_ALLOC_N(tmp_items, obj->items.count + 1) < 0)
        goto cleanup;

    for (i = 0; i < obj->items.count; i++) {
        if (!virFSPoolListAllItemsCheckACL(fspool->conn, obj->def,
                                                  obj->items.objs[i]))
            continue;
        if (!(item = virGetFSItem(fspool->conn, obj->def->name,
                                  obj->items.objs[i]->name,
                                  obj->items.objs[i]->key,
                                  NULL, NULL)))
            goto cleanup;
        tmp_items[nitems++] = item;
    }

    *items = tmp_items;
    tmp_items = NULL;
    ret = nitems;

 cleanup:
    if (tmp_items) {
        for (i = 0; i < nitems; i++)
            virObjectUnref(tmp_items[i]);
        VIR_FREE(tmp_items);
    }

    virFSPoolObjUnlock(obj);

    return ret;
}

static virFSItemPtr
fsItemLookupByName(virFSPoolPtr obj, const char *name)
{
    virFSPoolObjPtr fspool;
    virFSItemDefPtr item;
    virFSItemPtr ret = NULL;

    if (!(fspool = virFSPoolObjFromFSPool(obj)))
        return NULL;

    if (!virFSPoolObjIsActive(fspool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("fspool '%s' is not active"), fspool->def->name);
        goto cleanup;
    }

    item = virFSItemDefFindByName(fspool, name);

    if (!item) {
        virReportError(VIR_ERR_NO_FSITEM,
                       _("no fspool item with matching name '%s'"),
                       name);
        goto cleanup;
    }

    if (virFSItemLookupByNameEnsureACL(obj->conn, fspool->def, item) < 0)
        goto cleanup;

    ret = virGetFSItem(obj->conn, fspool->def->name, item->name, item->key,
                       NULL, NULL);

 cleanup:
    virFSPoolObjUnlock(fspool);
    return ret;
}


static virFSItemPtr
fsItemLookupByKey(virConnectPtr conn, const char *key)
{
    size_t i;
    virFSItemPtr ret = NULL;

    fsDriverLock();
    for (i = 0; i < driver->fspools.count && !ret; i++) {
        virFSPoolObjLock(driver->fspools.objs[i]);
        if (virFSPoolObjIsActive(driver->fspools.objs[i])) {
            virFSItemDefPtr item =
                virFSItemDefFindByKey(driver->fspools.objs[i], key);

            if (item) {
                virFSPoolDefPtr def = driver->fspools.objs[i]->def;
                if (virFSItemLookupByKeyEnsureACL(conn, def, item) < 0) {
                    virFSPoolObjUnlock(driver->fspools.objs[i]);
                    goto cleanup;
                }

                ret = virGetFSItem(conn,
                                   def->name,
                                   item->name,
                                   item->key,
                                   NULL, NULL);
            }
        }
        virFSPoolObjUnlock(driver->fspools.objs[i]);
    }

    if (!ret)
        virReportError(VIR_ERR_NO_FSITEM,
                       _("no fspool item with matching key %s"), key);

 cleanup:
    fsDriverUnlock();
    return ret;
}

static virFSItemPtr
fsItemLookupByPath(virConnectPtr conn,
                   const char *path)
{
    size_t i;
    virFSItemPtr ret = NULL;
    char *cleanpath;

    cleanpath = virFileSanitizePath(path);
    if (!cleanpath)
        return NULL;

    fsDriverLock();
    for (i = 0; i < driver->fspools.count && !ret; i++) {
        virFSPoolObjPtr fspool = driver->fspools.objs[i];
        virFSItemDefPtr item;

        virFSPoolObjLock(fspool);

        if (!virFSPoolObjIsActive(fspool)) {
           virFSPoolObjUnlock(fspool);
           continue;
        }

        item = virFSItemDefFindByPath(fspool, cleanpath);

        if (item) {
            if (virFSItemLookupByPathEnsureACL(conn, fspool->def, item) < 0) {
                virFSPoolObjUnlock(fspool);
                goto cleanup;
            }

            ret = virGetFSItem(conn, fspool->def->name,
                               item->name, item->key,
                               NULL, NULL);
        }

        virFSPoolObjUnlock(fspool);
    }

    if (!ret) {
        if (STREQ(path, cleanpath)) {
            virReportError(VIR_ERR_NO_FSITEM,
                           _("no fspool item with matching path '%s'"), path);
        } else {
            virReportError(VIR_ERR_NO_FSITEM,
                           _("no fspool item with matching path '%s' (%s)"),
                           path, cleanpath);
        }
    }

 cleanup:
    VIR_FREE(cleanpath);
    fsDriverUnlock();
    return ret;
}

static virFSItemPtr
fsItemCreateXML(virFSPoolPtr obj,
                const char *xmldesc,
                unsigned int flags)
{
    virFSPoolObjPtr fspool;
    virFSBackendPtr backend;
    virFSItemDefPtr itemdef = NULL;
    virFSItemPtr ret = NULL, itemobj = NULL;

    virCheckFlags(0, NULL);

    if (!(fspool = virFSPoolObjFromFSPool(obj)))
        return NULL;

    if (!virFSPoolObjIsActive(fspool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("fspool '%s' is not active"), fspool->def->name);
        goto cleanup;
    }

    if ((backend = virFSBackendForType(fspool->def->type)) == NULL)
        goto cleanup;

    itemdef = virFSItemDefParseString(fspool->def, xmldesc,
                                      VIR_ITEM_XML_PARSE_OPT_CAPACITY);
    if (itemdef == NULL)
        goto cleanup;

    if (!itemdef->target.capacity && !backend->buildItem) {
        virReportError(VIR_ERR_NO_SUPPORT,
                       "%s", _("item capacity required for this "
                               "fspool"));
        goto cleanup;
    }

    if (virFSItemCreateXMLEnsureACL(obj->conn, fspool->def, itemdef) < 0)
        goto cleanup;

    if (virFSItemDefFindByName(fspool, itemdef->name)) {
        virReportError(VIR_ERR_FSITEM_EXIST,
                       _("'%s'"), itemdef->name);
        goto cleanup;
    }

    if (!backend->createItem) {
        virReportError(VIR_ERR_NO_SUPPORT,
                       "%s", _("fspool does not support item "
                               "creation"));
        goto cleanup;
    }

    if (VIR_REALLOC_N(fspool->items.objs,
                      fspool->items.count+1) < 0)
        goto cleanup;

    /* Wipe any key the user may have suggested, as item creation
     * will generate the canonical key.  */
    VIR_FREE(itemdef->key);
    if (backend->createItem(obj->conn, fspool, itemdef) < 0)
        goto cleanup;

    fspool->items.objs[fspool->items.count++] = itemdef;
    itemobj = virGetFSItem(obj->conn, fspool->def->name, itemdef->name,
                           itemdef->key, NULL, NULL);
    if (!itemobj) {
        fspool->items.count--;
        goto cleanup;
    }


    if (backend->buildItem) {
        int buildret;
        virFSItemDefPtr builditemdef = NULL;

        if (VIR_ALLOC(builditemdef) < 0) {
            itemdef = NULL;
            goto cleanup;
        }

        /* Make a shallow copy of the 'defined' item definition, since the
         * original allocation value will change as the user polls 'info',
         * but we only need the initial requested values
         */
        memcpy(builditemdef, itemdef, sizeof(*itemdef));

        /* Drop the fspool lock during item allocation */
        fspool->asyncjobs++;
        itemdef->building = true;
        virFSPoolObjUnlock(fspool);

        buildret = backend->buildItem(obj->conn, fspool, builditemdef, flags);

        VIR_FREE(builditemdef);

        fsDriverLock();
        virFSPoolObjLock(fspool);
        fsDriverUnlock();

        itemdef->building = false;
        fspool->asyncjobs--;

        if (buildret < 0) {
            /* buildItem handles deleting item on failure */
            fsItemRemoveFromFSPool(fspool, itemdef);
            itemdef = NULL;
            goto cleanup;
        }

    }

    if (backend->refreshItem &&
        backend->refreshItem(obj->conn, fspool, itemdef) < 0) {
        fsItemDeleteInternal(itemobj, backend, fspool, itemdef, 0);
        itemdef = NULL;
        goto cleanup;
    }

    /* Update fspool metadata ignoring the disk backend since
     * it updates the fspool values.
     */

    VIR_INFO("Creating item '%s' in fspool '%s'",
             itemobj->name, fspool->def->name);
    ret = itemobj;
    itemobj = NULL;
    itemdef = NULL;

 cleanup:
    virObjectUnref(itemobj);
    virFSItemDefFree(itemdef);
    if (fspool)
        virFSPoolObjUnlock(fspool);
    return ret;
}

static virFSItemPtr
fsItemCreateXMLFrom(virFSPoolPtr obj,
                    const char *xmldesc,
                    virFSItemPtr vobj,
                    unsigned int flags)
{
    virFSPoolObjPtr fspool, origpool = NULL;
    virFSBackendPtr backend;
    virFSItemDefPtr origitem = NULL, newitem = NULL, shadowitem = NULL;
    virFSItemPtr ret = NULL, itemobj = NULL;
    int buildret;

    virCheckFlags(0, NULL);

    fsDriverLock();
    fspool = virFSPoolObjFindByUUID(&driver->fspools, obj->uuid);
    if (fspool && STRNEQ(obj->name, vobj->fspool)) {
        virFSPoolObjUnlock(fspool);
        origpool = virFSPoolObjFindByName(&driver->fspools, vobj->fspool);
        virFSPoolObjLock(fspool);
    }
    fsDriverUnlock();
    if (!fspool) {
        char uuidstr[VIR_UUID_STRING_BUFLEN];
        virUUIDFormat(obj->uuid, uuidstr);
        virReportError(VIR_ERR_NO_FSPOOL,
                       _("no fspool with matching uuid '%s' (%s)"),
                       uuidstr, obj->name);
        goto cleanup;
    }

    if (STRNEQ(obj->name, vobj->fspool) && !origpool) {
        virReportError(VIR_ERR_NO_FSPOOL,
                       _("no fspool with matching name '%s'"),
                       vobj->fspool);
        goto cleanup;
    }

    if (!virFSPoolObjIsActive(fspool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("fspool '%s' is not active"), fspool->def->name);
        goto cleanup;
    }

    if (origpool && !virFSPoolObjIsActive(origpool)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("fspool '%s' is not active"),
                       origpool->def->name);
        goto cleanup;
    }

    if ((backend = virFSBackendForType(fspool->def->type)) == NULL)
        goto cleanup;

    origitem = virFSItemDefFindByName(origpool ?
                                         origpool : fspool, vobj->name);
    if (!origitem) {
        virReportError(VIR_ERR_NO_FSITEM,
                       _("no fsitem with matching name '%s'"),
                       vobj->name);
        goto cleanup;
    }

    newitem = virFSItemDefParseString(fspool->def, xmldesc,
                                         VIR_VOL_XML_PARSE_NO_CAPACITY);
    if (newitem == NULL)
        goto cleanup;

    if (virFSItemCreateXMLFromEnsureACL(obj->conn, fspool->def, newitem) < 0)
        goto cleanup;

    if (virFSItemDefFindByName(fspool, newitem->name)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("fsitem name '%s' already in use."),
                       newitem->name);
        goto cleanup;
    }

    /* Use the original item's capacity in case the new capacity
     * is less than that, or it was omitted */
    if (newitem->target.capacity < origitem->target.capacity)
        newitem->target.capacity = origitem->target.capacity;

    if (!backend->buildItemFrom) {
        virReportError(VIR_ERR_NO_SUPPORT,
                       "%s", _("fspool does not support"
                               " item creation from an existing item"));
        goto cleanup;
    }

    if (origitem->building) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("fsitem '%s' is still being allocated."),
                       origitem->name);
        goto cleanup;
    }

    if (backend->refreshItem &&
        backend->refreshItem(obj->conn, fspool, origitem) < 0)
        goto cleanup;

    if (VIR_REALLOC_N(fspool->items.objs,
                      fspool->items.count+1) < 0)
        goto cleanup;

    /* 'Define' the new item so we get async progress reporting.
     * Wipe any key the user may have suggested, as item creation
     * will generate the canonical key.  */
    VIR_FREE(newitem->key);
    if (backend->createItem(obj->conn, fspool, newitem) < 0)
        goto cleanup;

    /* Make a shallow copy of the 'defined' item definition, since the
     * original allocation value will change as the user polls 'info',
     * but we only need the initial requested values
     */
    if (VIR_ALLOC(shadowitem) < 0)
        goto cleanup;

    memcpy(shadowitem, newitem, sizeof(*newitem));

    fspool->items.objs[fspool->items.count++] = newitem;
    itemobj = virGetFSItem(obj->conn, fspool->def->name, newitem->name,
                              newitem->key, NULL, NULL);
    if (!itemobj) {
        fspool->items.count--;
        goto cleanup;
    }

    /* Drop the fspool lock during item allocation */
    fspool->asyncjobs++;
    newitem->building = true;
    origitem->in_use++;
    virFSPoolObjUnlock(fspool);

    if (origpool) {
        origpool->asyncjobs++;
        virFSPoolObjUnlock(origpool);
    }

    buildret = backend->buildItemFrom(obj->conn, fspool, shadowitem, origitem, flags);

    fsDriverLock();
    virFSPoolObjLock(fspool);
    if (origpool)
        virFSPoolObjLock(origpool);
    fsDriverUnlock();

    origitem->in_use--;
    newitem->building = false;
    fspool->asyncjobs--;

    if (origpool) {
        origpool->asyncjobs--;
        virFSPoolObjUnlock(origpool);
        origpool = NULL;
    }

    if (buildret < 0 ||
        (backend->refreshItem &&
         backend->refreshItem(obj->conn, fspool, newitem) < 0)) {
        fsItemDeleteInternal(itemobj, backend, fspool, newitem, 0);
        newitem = NULL;
        goto cleanup;
    }

    fspool->def->allocation += newitem->target.allocation;
    fspool->def->available -= newitem->target.allocation;

    VIR_INFO("Creating item '%s' in fspool '%s'",
             itemobj->name, fspool->def->name);
    ret = itemobj;
    itemobj = NULL;
    newitem = NULL;

 cleanup:
    virObjectUnref(itemobj);
    virFSItemDefFree(newitem);
    VIR_FREE(shadowitem);
    if (fspool)
        virFSPoolObjUnlock(fspool);
    if (origpool)
        virFSPoolObjUnlock(origpool);
    return ret;
}


static int
fsItemGetInfo(virFSItemPtr obj,
              virFSItemInfoPtr info)
{
    virFSPoolObjPtr fspool;
    virFSBackendPtr backend;
    virFSItemDefPtr item;
    int ret = -1;

    if (!(item = virFSItemDefFromItem(obj, &fspool, &backend)))
        return -1;

    if (virFSItemGetInfoEnsureACL(obj->conn, fspool->def, item) < 0)
        goto cleanup;

    if (backend->refreshItem &&
        backend->refreshItem(obj->conn, fspool, item) < 0)
        goto cleanup;

    memset(info, 0, sizeof(*info));
    info->type = item->type;
    info->capacity = item->target.capacity;
    info->allocation = item->target.allocation;
    ret = 0;

 cleanup:
    virFSPoolObjUnlock(fspool);
    return ret;
}

static char *
fsItemGetXMLDesc(virFSItemPtr obj, unsigned int flags)
{
    virFSPoolObjPtr fspool;
    virFSBackendPtr backend;
    virFSItemDefPtr item;
    char *ret = NULL;

    virCheckFlags(0, NULL);

    if (!(item = virFSItemDefFromItem(obj, &fspool, &backend)))
        return NULL;

    if (virFSItemGetXMLDescEnsureACL(obj->conn, fspool->def, item) < 0)
        goto cleanup;

    if (backend->refreshItem &&
        backend->refreshItem(obj->conn, fspool, item) < 0)
        goto cleanup;

    ret = virFSItemDefFormat(fspool->def, item);

 cleanup:
    virFSPoolObjUnlock(fspool);

    return ret;
}

static char *
fsItemGetPath(virFSItemPtr obj)
{
    virFSPoolObjPtr fspool;
    virFSItemDefPtr item;
    char *ret = NULL;

    if (!(item = virFSItemDefFromItem(obj, &fspool, NULL)))
        return NULL;

    if (virFSItemGetPathEnsureACL(obj->conn, fspool->def, item) < 0)
        goto cleanup;

    ignore_value(VIR_STRDUP(ret, item->target.path));

 cleanup:
    virFSPoolObjUnlock(fspool);
    return ret;
}


static int
fsPoolIsActive(virFSPoolPtr fspool)
{
    virFSPoolObjPtr obj;
    int ret = -1;

    if (!(obj = virFSPoolObjFromFSPool(fspool)))
        return -1;

    if (virFSPoolIsActiveEnsureACL(fspool->conn, obj->def) < 0)
        goto cleanup;

    ret = virFSPoolObjIsActive(obj);

 cleanup:
    virFSPoolObjUnlock(obj);
    return ret;
}

static int
fsPoolIsPersistent(virFSPoolPtr fspool)
{
    virFSPoolObjPtr obj;
    int ret = -1;

    if (!(obj = virFSPoolObjFromFSPool(fspool)))
        return -1;

    if (virFSPoolIsPersistentEnsureACL(fspool->conn, obj->def) < 0)
        goto cleanup;

    ret = obj->configFile ? 1 : 0;

 cleanup:
    virFSPoolObjUnlock(obj);
    return ret;
}


static int
fsItemDelete(virFSItemPtr obj,
             unsigned int flags)
{
    virFSPoolObjPtr fspool;
    virFSBackendPtr backend;
    virFSItemDefPtr item = NULL;
    int ret = -1;

    virCheckFlags(0, -1);

    if (!(item = virFSItemDefFromItem(obj, &fspool, &backend)))
        return -1;

    if (virFSItemDeleteEnsureACL(obj->conn, fspool->def, item) < 0)
        goto cleanup;

    if (item->in_use) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("item '%s' is still in use."),
                       item->name);
        goto cleanup;
    }

    if (item->building) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("item '%s' is still being allocated."),
                       item->name);
        goto cleanup;
    }

    if (fsItemDeleteInternal(obj, backend, fspool, item, flags) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virFSPoolObjUnlock(fspool);
    return ret;
}

static virFSDriver fsDriver = {
    .name = "fs",
    .connectListAllFSPools = fsConnectListAllFSPools, /* 2.3.0 */
    .fsPoolLookupByName = fsPoolLookupByName, /* 2.3.0 */
    .fsPoolLookupByUUID = fsPoolLookupByUUID, /* 2.3.0 */
    .fsPoolLookupByItem = fsPoolLookupByItem, /* 2.3.0 */
    .fsPoolCreateXML = fsPoolCreateXML, /* 2.3.0 */
    .fsPoolDefineXML = fsPoolDefineXML, /* 2.3.0 */
    .fsPoolBuild = fsPoolBuild, /* 2.3.0 */
    .fsPoolCreate = fsPoolCreate, /* 2.3.0 */
    .fsPoolUndefine = fsPoolUndefine, /* 2.3.0 */
    .fsPoolDestroy = fsPoolDestroy, /* 2.3.0 */
    .fsPoolDelete = fsPoolDelete, /* 2.3.0 */
    .fsPoolRefresh = fsPoolRefresh, /* 2.3.0 */
    .fsPoolGetInfo = fsPoolGetInfo, /* 2.3.0 */
    .fsPoolGetXMLDesc = fsPoolGetXMLDesc, /* 2.3.0 */
    .fsPoolGetAutostart = fsPoolGetAutostart, /* 2.3.0 */
    .fsPoolSetAutostart = fsPoolSetAutostart, /* 2.3.0 */
    .fsPoolNumOfItems = fsPoolNumOfItems, /* 2.3.0 */
    .fsPoolListItems = fsPoolListItems, /* 2.3.0 */
    .fsPoolListAllItems = fsPoolListAllItems, /* 2.3.0 */
    .fsItemLookupByName = fsItemLookupByName, /* 2.3.0 */
    .fsItemLookupByKey = fsItemLookupByKey, /* 2.3.0 */
    .fsItemLookupByPath = fsItemLookupByPath, /* 2.3.0 */
    .fsItemCreateXML = fsItemCreateXML, /* 2.3.0 */
    .fsItemCreateXMLFrom = fsItemCreateXMLFrom, /* 2.3.0 */
    .fsItemDelete = fsItemDelete, /* 2.3.0 */
    .fsItemGetInfo = fsItemGetInfo, /* 2.3.0 */
    .fsItemGetXMLDesc = fsItemGetXMLDesc, /* 2.3.0 */
    .fsItemGetPath = fsItemGetPath, /* 2.3.0 */
    .fsPoolIsActive = fsPoolIsActive, /* 2.3.0 */
    .fsPoolIsPersistent = fsPoolIsPersistent, /* 2.3.0 */
};


static virStateDriver stateDriver = {
    .name = "fs",
    .stateInitialize = fsStateInitialize,
    .stateAutoStart = fsStateAutoStart,
    .stateCleanup = fsStateCleanup,
    .stateReload = fsStateReload,
};

int fsRegister(void)
{
    VIR_DEBUG("fsDriver = %p", &fsDriver);

    if (virSetSharedFSDriver(&fsDriver) < 0)
        return -1;

    if (virRegisterStateDriver(&stateDriver) < 0)
        return -1;

    VIR_DEBUG("fsDriver = %p", &fsDriver);

    return 0;
}
