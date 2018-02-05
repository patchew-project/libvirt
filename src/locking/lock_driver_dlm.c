/*
 * lock_driver_dlm.c: a lock driver for dlm
 *
 * Copyright (C) 2018 SUSE LINUX Products, Beijing, China.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include <corosync/cpg.h>
#include <libdlm.h>

#include "lock_driver.h"
#include "viralloc.h"
#include "virconf.h"
#include "vircrypto.h"
#include "virerror.h"
#include "virfile.h"
#include "virlist.h"
#include "virlog.h"
#include "virstring.h"
#include "virthread.h"
#include "viruuid.h"

#define VIR_FROM_THIS VIR_FROM_LOCKING

#define DLM_LOCKSPACE_MODE  0600
#define DLM_LOCKSPACE_NAME  "libvirt"

#define LOCK_RECORD_FILE_MODE       0644
#define LOCK_RECORD_FILE_PATH       "/tmp/libvirtd-dlm-file"

#define PRMODE  "PRMODE"
#define EXMODE  "EXMODE"

#define STATUS             "STATUS"
#define RESOURCE_NAME      "RESOURCE_NAME"
#define LOCK_ID            "LOCK_ID"
#define LOCK_MODE          "LOCK_MODE"
#define VM_PID             "VM_PID"

#define BUFFERLEN          128

/* This will be set after dlm_controld is started. */
#define DLM_CLUSTER_NAME_PATH "/sys/kernel/config/dlm/cluster/cluster_name"

VIR_LOG_INIT("locking.lock_driver_dlm");

typedef struct _virLockInformation virLockInformation;
typedef virLockInformation *virLockInformationPtr;

typedef struct _virLockManagerDlmResource virLockManagerDlmResource;
typedef virLockManagerDlmResource *virLockManagerDlmResourcePtr;

typedef struct _virLockManagerDlmPrivate virLockManagerDlmPrivate;
typedef virLockManagerDlmPrivate *virLockManagerDlmPrivatePtr;

typedef struct _virLockManagerDlmDriver virLockManagerDlmDriver;
typedef virLockManagerDlmDriver *virLockManagerDlmDriverPtr;

typedef struct _virListWait virListWait;
typedef virListWait *virListWaitPtr;

struct _virLockInformation {
    virListHead entry;
    char    *name;
    uint32_t mode;
    uint32_t lkid;
    pid_t    vm_pid;
};

struct _virLockManagerDlmResource {
    char    *name;
    uint32_t mode;
};

struct _virLockManagerDlmPrivate {
    unsigned char vm_uuid[VIR_UUID_BUFLEN];
    char         *vm_name;
    pid_t         vm_pid;
    int           vm_id;

    size_t        nresources;
    virLockManagerDlmResourcePtr resources;

    bool          hasRWDisks;
};

struct _virLockManagerDlmDriver {
    bool  autoDiskLease;
    bool  requireLeaseForDisks;

	bool  purgeLockspace;
	char *lockspaceName;
    char *lockRecordFilePath;
};

struct _virListWait {
    virMutex listMutex;
    virMutex fileMutex;
    virListHead list;
};

static virLockManagerDlmDriverPtr driver;
static dlm_lshandle_t lockspace;
static virListWait lockListWait;

static int virLockManagerDlmLoadConfig(const char *configFile)
{
    virConfPtr conf = NULL;
    int ret = -1;

    if (access(configFile, R_OK) == -1) {
        if (errno != ENOENT) {
            virReportSystemError(errno,
                                 _("Unable to access config file %s"),
                                 configFile);
            return -1;
        }
        return 0;
    }

	if (!(conf = virConfReadFile(configFile, 0)))
		return -1;

    if (virConfGetValueBool(conf, "auto_disk_leases", &driver->autoDiskLease) < 0)
        goto cleanup;

    driver->requireLeaseForDisks = !driver->autoDiskLease;
    if (virConfGetValueBool(conf, "require_lease_for_disks", &driver->requireLeaseForDisks) < 0)
        goto cleanup;

    if (virConfGetValueBool(conf, "purge_lockspace", &driver->purgeLockspace) < 0)
        goto cleanup;

    if (virConfGetValueString(conf, "lockspace_name", &driver->lockspaceName) < 0)
        goto cleanup;

    if (virConfGetValueString(conf, "lock_record_file_path", &driver->lockRecordFilePath) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virConfFree(conf);
    return ret;
}

static int virLockManagerDlmToModeUint(const char *token)
{
    if (STREQ(token, PRMODE))
        return LKM_PRMODE;
    if (STREQ(token, EXMODE))
        return LKM_EXMODE;

    return 0;
}

static const char *virLockManagerDlmToModeText(const uint32_t mode)
{
    switch (mode) {
    case LKM_PRMODE:
        return PRMODE;
    case LKM_EXMODE:
        return EXMODE;
    default:
        return NULL;
    }
}

static virLockInformationPtr virLockManagerDlmRecordLock(const char *name,
                                                         const uint32_t mode,
                                                         const uint32_t lkid,
                                                         const pid_t vm_pid)
{
    virLockInformationPtr lock = NULL;

    if (VIR_ALLOC(lock) < 0)
        goto error;

    if (VIR_STRDUP(lock->name, name) < 0)
        goto error;

    lock->mode = mode;
    lock->lkid = lkid;
    lock->vm_pid = vm_pid;

    virMutexLock(&(lockListWait.listMutex));
    virListAddTail(&lock->entry, &(lockListWait.list));
    virMutexUnlock(&(lockListWait.listMutex));

    VIR_DEBUG("record lock sucessfully, lockName=%s lockMode=%s lockId=%d",
              NULLSTR(name), NULLSTR(virLockManagerDlmToModeText(mode)), lkid);

    return lock;

 error:
    if (lock)
        VIR_FREE(lock->name);
    VIR_FREE(lock);
    return NULL;
}

static void virLockManagerDlmWriteLock(virLockInformationPtr lock, int fd, bool status)
{
    char buffer[BUFFERLEN] = {0};
    off_t offset = 0, rv = 0;

    if (!lock) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("lock is NULL"));
        return;
    }

    /*
     * STATUS RESOURCE_NAME LOCK_MODE VM_PID\n
     *      6            64         9     10
     * 93 = 6 + 1 + 64 + 1 + 9 + 1 + 10 + 1
     */
    offset = 93 * lock->lkid;
	rv = lseek(fd, offset, SEEK_SET);
	if (rv < 0) {
		virReportSystemError(errno,
							 _("unable to lseek fd '%d'"),
                             fd);
        return;
    }

    snprintf(buffer, sizeof(buffer), "%6d %64s %9s %10jd\n", \
             status, lock->name,
             NULLSTR(virLockManagerDlmToModeText(lock->mode)),
             (intmax_t)lock->vm_pid);

    if (safewrite(fd, buffer, strlen(buffer)) != strlen(buffer)) {
        virReportSystemError(errno,
                             _("unable to write lock information '%s' to file '%s'"),
                             buffer, NULLSTR(driver->lockRecordFilePath));
        return;
    }

    VIR_DEBUG("write '%s' to fd=%d", buffer, fd);

    fdatasync(fd);

    return;
}

static void virLockManagerDlmAdoptLock(char *raw) {
    char *str = NULL, *subtoken = NULL, *saveptr = NULL, *endptr = NULL;
    int i = 0, status = 0;
    char *name = NULL;
    uint32_t mode = 0;
    pid_t vm_pid = 0;
    struct dlm_lksb lksb = {0};

    /* every line is the following format:
     *   STATUS RESOURCE_NAME LOCK_MODE VM_PID
     */
    for (i = 0, str = raw, status = 0; ; str = NULL, i++) {
        subtoken = strtok_r(str, " \n", &saveptr);
        if (subtoken == NULL)
            break;

        switch(i) {
        case 0:
            if (virStrToLong_i(subtoken, &endptr, 10, &status) < 0) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("cannot extract lock status '%s'"), subtoken);
                goto cleanup;
            }
            break;
        case 1:
            if (VIR_STRDUP(name, subtoken) != 1)
                goto cleanup;
            break;
        case 2:
            mode = virLockManagerDlmToModeUint(subtoken);
            if (!mode)
                goto cleanup;
            break;
        case 3:
            if ((virStrToLong_i(subtoken, &endptr, 10, &vm_pid) < 0) || !vm_pid) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("cannot extract lock vm_pid '%s'"), subtoken);
                goto cleanup;
            }
            break;
        default:
            goto cleanup;
            break;
        }

        if (status != 1)
            goto cleanup;
    }

    if (i != 4)
        goto cleanup;

    /* copy from `lm_adopt_dlm` in daemons/lvmlockd/lvmlockd-dlm.c of lvm2:
	 *   dlm returns 0 for success, -EAGAIN if an orphan is
     *   found with another mode, and -ENOENT if no orphan.
     *
     *   cast/bast/param are (void *)1 because the kernel
     *   returns errors if some are null.
     */

    status = dlm_ls_lockx(lockspace, mode, &lksb, LKF_PERSISTENT|LKF_ORPHAN,
                          name, strlen(name), 0,
                          (void *)1, (void *)1, (void *)1,
                          NULL, NULL);
    if (status) {
        virReportSystemError(errno,
                             _("unable to adopt lock, rv=%d lockName=%s lockMode=%s"),
                             status, name, NULLSTR(virLockManagerDlmToModeText(mode)));
        goto cleanup;
    }

    if (!virLockManagerDlmRecordLock(name, mode, lksb.sb_lkid, vm_pid)) {
        virReportSystemError(errno,
                             _("unable to record lock information, "
                                 "lockName=%s lockMode=%s lockId=%d vm_pid=%jd"),
                             NULLSTR(name), NULLSTR(virLockManagerDlmToModeText(mode)),
                             lksb.sb_lkid, (intmax_t)vm_pid);
    }


 cleanup:
    if (name)
        VIR_FREE(name);

    return;
}

static int virLockManagerDlmPrepareLockList(const char *lockRecordFilePath)
{
    FILE *fp = NULL;
    int line = 0;
    size_t n = 0;
    ssize_t count = 0;
    char *buffer = NULL;

    fp = fopen(lockRecordFilePath, "r");
    if (!fp) {
        virReportSystemError(errno,
                             _("unable to open '%s'"), lockRecordFilePath);
        return -1;
    }

    /* lock information is from the second line */
    for (line = 0; !feof(fp); line++) {
        count = getline(&buffer, &n, fp);
        if (count <= 0)
            break;

        switch (line) {
        case 0:
            break;
        default:
            virLockManagerDlmAdoptLock(buffer);
            break;
        }
    }

    VIR_FORCE_FCLOSE(fp);
    VIR_FREE(buffer);

    return 0;
}

static int virLockManagerDlmGetLocalNodeId(uint32_t *nodeId)
{
    cpg_handle_t handle = 0;
    int rv = -1;

    if (cpg_model_initialize(&handle, CPG_MODEL_V1, NULL, NULL) != CS_OK) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("unable to create a new connection to the CPG service"));
		return -1;
    }

	if( cpg_local_get(handle, nodeId) != CS_OK) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("unable to get the local node id by the CPG service"));
        goto cleanup;
    }

    VIR_DEBUG("the local nodeid=%u", *nodeId);

    rv = 0;

 cleanup:
    if (cpg_finalize(handle) != CS_OK)
        VIR_WARN("unable to finalize the CPG service");

	return rv;
}

static int virLockManagerDlmDumpLockList(const char *lockRecordFilePath)
{
    virLockInformationPtr theLock = NULL;
    char buffer[BUFFERLEN] = {0};
    int fd = -1, rv = -1;

    /* not need mutex because of only one instance would be initialized */
    fd = open(lockRecordFilePath, O_WRONLY|O_CREAT|O_TRUNC, LOCK_RECORD_FILE_MODE);
    if (fd < 0) {
        virReportSystemError(errno,
                             _("unable to open '%s'"),
                             lockRecordFilePath);
        return -1;
    }

    snprintf(buffer, sizeof(buffer), "%6s %64s %9s %10s\n", \
                    STATUS, RESOURCE_NAME, LOCK_MODE, VM_PID);
    if (safewrite(fd, buffer, strlen(buffer)) != strlen(buffer)) {
        virReportSystemError(errno,
                             _("unable to write '%s' to '%s'"),
                             buffer, lockRecordFilePath);
        goto cleanup;
    }

    virListForEachEntry(theLock, &(lockListWait.list), entry) {
        virLockManagerDlmWriteLock(theLock, fd, 1);
    }

    if (VIR_CLOSE(fd) < 0) {
        virReportSystemError(errno,
                             _("unable to close file '%s'"),
                             lockRecordFilePath);
        goto cleanup;
    }

    rv = 0;

 cleanup:
    if (rv)
        VIR_FORCE_CLOSE(fd);
    return rv;
}

static int virLockManagerDlmSetupLockRecordFile(const char *lockRecordFilePath,
                                                const bool newLockspace,
                                                const bool purgeLockspace)
{
    uint32_t nodeId = 0;

    /* there maybe some orphan locks recorded in the lock record file which
     * should be adopted if lockspace is opened instead of created, we adopt
     * them then add them in the list.
     */
    if (!newLockspace &&
        virLockManagerDlmPrepareLockList(lockRecordFilePath)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("unable to adopt locks from '%s'"),
                       NULLSTR(lockRecordFilePath));
        return -1;
    }

    /* purgeLockspace flag means purging orphan locks belong to any process
     * in this lockspace.
     */
    if (purgeLockspace && !virLockManagerDlmGetLocalNodeId(&nodeId)) {
        if (dlm_ls_purge(lockspace, nodeId, 0)) {
            VIR_WARN("node=%u purge DLM locks failed in lockspace=%s",
                     nodeId, NULLSTR(driver->lockspaceName));
        }
        else
            VIR_DEBUG("node=%u purge DLM locks success in lockspace=%s",
                      nodeId, NULLSTR(driver->lockspaceName));
    }

    /* initialize the lock record file */
    if (virLockManagerDlmDumpLockList(lockRecordFilePath)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("unable to initialize the lock record file '%s'"),
                       lockRecordFilePath);
        return -1;
    }

    return 0;
}

static int virLockManagerDlmSetup(void)
{
    bool newLockspace = false;

    virListHeadInit(&(lockListWait.list));
    if ((virMutexInit(&(lockListWait.listMutex)) < 0) ||
        (virMutexInit(&(lockListWait.fileMutex)) < 0)) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("unable to initialize mutex"));
        return -1;
    }


    /* check whether dlm is running or not */
    if (access(DLM_CLUSTER_NAME_PATH, F_OK)) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("check dlm_controld, ensure it has setuped"));
        return -1;
    }

    /* open lockspace, create it if it doesn't exist */
    lockspace = dlm_open_lockspace(driver->lockspaceName);
    if (!lockspace) {
        lockspace = dlm_create_lockspace(driver->lockspaceName, DLM_LOCKSPACE_MODE);
        if (!lockspace) {
            virReportSystemError(errno, "%s",
                                 _("unable to open and create DLM lockspace"));
            return -1;
        }
        newLockspace = true;
    }

    /* create thread to receive notification from kernel */
    if (dlm_ls_pthread_init(lockspace)) {
        if (errno != EEXIST) {
            virReportSystemError(errno, "%s",
                                 _("unable to initialize lockspace"));
            return -1;
        }
    }

    /* we need file to record lock information used by rebooted libvirtd */
    if (virLockManagerDlmSetupLockRecordFile(driver->lockRecordFilePath,
                                             newLockspace,
                                             driver->purgeLockspace)) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("unable to initialize DLM lock file"));
        return -1;
    }

    return 0;
}

static int virLockManagerDlmDeinit(void);

static int virLockManagerDlmInit(unsigned int version,
                                 const char *configFile,
                                 unsigned int flags)
{
    VIR_DEBUG("version=%u configFile=%s flags=0x%x", version, NULLSTR(configFile), flags);

    virCheckFlags(0, -1);

    if (driver)
        return 0;

    if (geteuid() != 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("dlm lock requires root privileges"));
        return -1;
    }

    if (VIR_ALLOC(driver) < 0)
        return -1;

    driver->autoDiskLease = true;
    driver->requireLeaseForDisks = !driver->autoDiskLease;
    driver->purgeLockspace = true;

    if (virAsprintf(&driver->lockspaceName,
                  "%s", DLM_LOCKSPACE_NAME) < 0)
        goto error;

    if (virAsprintf(&driver->lockRecordFilePath,
                  "%s", LOCK_RECORD_FILE_PATH) < 0)
        goto error;

    if (virLockManagerDlmLoadConfig(configFile) < 0)
        goto error;

    if (virLockManagerDlmSetup() < 0)
        goto error;

    return 0;

 error:
    virLockManagerDlmDeinit();
    return -1;
}

static int virLockManagerDlmDeinit(void)
{
    virLockInformationPtr theLock = NULL;

    if (!driver)
        return 0;

    if(lockspace)
        dlm_close_lockspace(lockspace);

    /* not care about whether adopting lock or not,
     * just release those to prevent memory leak
     */
    virListForEachEntry(theLock, &(lockListWait.list), entry) {
        virListDelete(&(theLock->entry));
        VIR_FREE(theLock->name);
        VIR_FREE(theLock);
    }

    VIR_FREE(driver->lockspaceName);
    VIR_FREE(driver->lockRecordFilePath);
    VIR_FREE(driver);

    return 0;
}

static int virLockManagerDlmNew(virLockManagerPtr lock,
                                unsigned int type,
                                size_t nparams,
                                virLockManagerParamPtr params,
                                unsigned int flags)
{
    virLockManagerDlmPrivatePtr priv = NULL;
    size_t i;

    virCheckFlags(VIR_LOCK_MANAGER_NEW_STARTED, -1);

    if (!driver) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("dlm plugin is not initialized"));
        return -1;
    }

    if (type != VIR_LOCK_MANAGER_OBJECT_TYPE_DOMAIN) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("unsupported object type %d"), type);
        return -1;
    }

    if (VIR_ALLOC(priv) < 0)
        return -1;

    for (i = 0; i< nparams; i++) {
        if (STREQ(params[i].key, "uuid")) {
            memcpy(priv->vm_uuid, params[i].value.uuid, VIR_UUID_BUFLEN);
        } else if (STREQ(params[i].key, "name")) {
            if (VIR_STRDUP(priv->vm_name, params[i].value.str) < 0)
                return -1;
        } else if (STREQ(params[i].key, "id")) {
            priv->vm_id = params[i].value.ui;
        } else if (STREQ(params[i].key, "pid")) {
            priv->vm_pid = params[i].value.iv;
        } else if (STREQ(params[i].key, "uri")) {
            /* there would be a warning in some case according to the history patch,
             * so ignored
             */
        } else {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("unexpected parameter %s for object"),
                           params[i].key);
        }
    }

    /* check the following to prevent some unexpexted state in some case */
    if (priv->vm_pid == 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("missing PID parameter for domain object"));
        return -1;
    }
    if (!priv->vm_name) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("missing name parameter for domain object"));
        return -1;
    }

    if (priv->vm_id == 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("missing ID parameter for domain object"));
        return -1;
    }
    if (!virUUIDIsValid(priv->vm_uuid)) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("missing UUID parameter for domain object"));
        return -1;
    }

    lock->privateData = priv;

    return 0;
}

static void virLockManagerDlmFree(virLockManagerPtr lock)
{
    virLockManagerDlmPrivatePtr priv = lock->privateData;
    size_t i;

    if (!priv)
        return;

    for (i = 0; i < priv->nresources; i++)
        VIR_FREE(priv->resources[i].name);

    VIR_FREE(priv->resources);
    VIR_FREE(priv->vm_name);
    VIR_FREE(priv);
    lock->privateData = NULL;

    return;
}

static int virLockManagerDlmAddResource(virLockManagerPtr lock,
                                        unsigned int type, const char *name,
                                        size_t nparams,
                                        virLockManagerParamPtr params,
                                        unsigned int flags)
{
    virLockManagerDlmPrivatePtr priv = lock->privateData;
    char *newName = NULL;

    virCheckFlags(VIR_LOCK_MANAGER_RESOURCE_READONLY |
                  VIR_LOCK_MANAGER_RESOURCE_SHARED, -1);

    /* Treat read only resources as a no-op lock request */
    if (flags & VIR_LOCK_MANAGER_RESOURCE_READONLY)
        return 0;

    switch (type) {
    case VIR_LOCK_MANAGER_RESOURCE_TYPE_DISK:
            if (params || nparams) {
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("unexpected parameters for disk resource"));
                return -1;
            }

            if (!driver->autoDiskLease) {
                if (!(flags & (VIR_LOCK_MANAGER_RESOURCE_SHARED |
                               VIR_LOCK_MANAGER_RESOURCE_READONLY))) {
                    priv->hasRWDisks = true;
                    /* ignore disk resource without error */
                    return 0;
                }
            }

            if (virCryptoHashString(VIR_CRYPTO_HASH_SHA256, name, &newName) < 0)
                goto cleanup;

        break;

    case VIR_LOCK_MANAGER_RESOURCE_TYPE_LEASE:
        /* we need format the lock information, so the lock name must be the constant length */
        if (virCryptoHashString(VIR_CRYPTO_HASH_SHA256, name, &newName) < 0)
            goto cleanup;

        break;

    default:
        virReportError(VIR_ERR_INTERNAL_ERROR,
                _("unknown lock manager object type %d"),
                type);
        return -1;
    }

    if (VIR_EXPAND_N(priv->resources, priv->nresources, 1) < 0)
        goto cleanup;

    priv->resources[priv->nresources-1].name = newName;

    if (!!(flags & VIR_LOCK_MANAGER_RESOURCE_SHARED))
        priv->resources[priv->nresources-1].mode = LKM_PRMODE;
    else
        priv->resources[priv->nresources-1].mode = LKM_EXMODE;

    return 0;

 cleanup:
    VIR_FREE(newName);

    return -1;
}

static int virLockManagerDlmAcquire(virLockManagerPtr lock,
                                    const char *state ATTRIBUTE_UNUSED,
                                    unsigned int flags,
                                    virDomainLockFailureAction action ATTRIBUTE_UNUSED,
                                    int *fd)
{
    virLockManagerDlmPrivatePtr priv = lock->privateData;
    virLockInformationPtr theLock = NULL;
    struct dlm_lksb lksb = {0};
    int rv = -1, theFd = -1;
    size_t i;

    virCheckFlags(VIR_LOCK_MANAGER_ACQUIRE_REGISTER_ONLY |
                  VIR_LOCK_MANAGER_ACQUIRE_RESTRICT, -1);

    /* allowed to start a guest which has read/write disks, but without any leases */
    if (priv->nresources == 0 &&
        priv->hasRWDisks &&
        driver->requireLeaseForDisks) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("read/write, exclusive access, disks were present, but no leases specified"));
        return -1;
    }

    /* accorting to git patch history, add `fd` parameter in order to
     * 'ensure sanlock socket is labelled with the VM process label',
     * however, fixing sanlock socket security labelling remove related
     * code. Now, `fd` parameter is useless.
     */
    if (fd)
        *fd = -1;

    if(!lockspace) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("lockspace is not opened"));
        return -1;
    }

    if (!(flags & VIR_LOCK_MANAGER_ACQUIRE_REGISTER_ONLY)) {
        VIR_DEBUG("Acquiring object %zu", priv->nresources);

        theFd = open(driver->lockRecordFilePath, O_RDWR);
        if (theFd < 0) {
            virReportSystemError(errno,
                                 _("unable to open '%s'"), driver->lockRecordFilePath);
            return -1;
        }

        for (i = 0; i < priv->nresources; i++) {
            VIR_DEBUG("Acquiring object %zu", priv->nresources);

            memset(&lksb, 0, sizeof(lksb));
            rv = dlm_ls_lock_wait(lockspace, priv->resources[i].mode,
                                  &lksb, LKF_NOQUEUE|LKF_PERSISTENT,
                                  priv->resources[i].name, strlen(priv->resources[i].name),
                                  0, NULL, NULL, NULL);
            /* both `rv` and `lksb.sb_status` equal 0 means lock sucessfully */
            if (rv || lksb.sb_status) {
                if (lksb.sb_status == EAGAIN)
                    virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                                   _("failed to acquire lock: the lock could not be granted"));
                else {
                    virReportSystemError(errno,
                                         _("failed to acquire lock: rv=%d lockStatus=%d"),
                                         rv, lksb.sb_status);
                }
                /* rv would be 0 although acquiring lock failed */
                rv = -1;
                goto cleanup;
            }

            theLock = virLockManagerDlmRecordLock(priv->resources[i].name,
                                                  priv->resources[i].mode,
                                                  lksb.sb_lkid,
                                                  priv->vm_pid);
            if (!theLock) {
			virReportSystemError(errno,
                                     _("unable to record lock information, "
                                        "lockName=%s lockMode=%s lockId=%d vm_pid=%jd"),
                                     NULLSTR(priv->resources[i].name),
                                     NULLSTR(virLockManagerDlmToModeText(priv->resources[i].mode)),
                                     lksb.sb_lkid, (intmax_t)priv->vm_pid);
                /* record lock failed, we can't save lock information in memory, so release it */
                rv = dlm_ls_unlock_wait(lockspace, lksb.sb_lkid, 0, &lksb);
                if (!rv)
                    virReportSystemError(errno,
                                         _("failed to release lock: rv=%d lockStatue=%d"),
                                         rv, lksb.sb_status);
                rv = -1;
                goto cleanup;
            }

            virMutexLock(&(lockListWait.fileMutex));
            virLockManagerDlmWriteLock(theLock, theFd, 1);
            virMutexUnlock(&(lockListWait.fileMutex));
        }

        if(VIR_CLOSE(theFd) < 0) {
            virReportSystemError(errno,
                                 _("unable to save file '%s'"),
                                driver->lockRecordFilePath);
            goto cleanup;
        }
    }

    if (flags & VIR_LOCK_MANAGER_ACQUIRE_RESTRICT) {
        /* no daemon watches this fd, do nothing here, just close lockspace before `execv`
         * `dlm_close_lockspace` always return 0, so ignore return value
         */
        ignore_value(dlm_close_lockspace(lockspace));
        lockspace = NULL;
    }

    rv = 0;

 cleanup:
    if (rv)
        VIR_FORCE_CLOSE(theFd);
    return rv;
}

static void virLockManagerDlmDeleteLock(const virLockInformationPtr lock,
                                        const char *lockRecordFilePath)
{
    int fd = -1;

    if (!lock)
        return;

    virMutexLock(&(lockListWait.listMutex));
    virListDelete(&(lock->entry));
    virMutexUnlock(&(lockListWait.listMutex));

    fd = open(lockRecordFilePath, O_RDWR);
    if (fd < 0) {
        virReportSystemError(errno,
                             _("unable to open '%s'"), lockRecordFilePath);
        goto cleanup;
    }

    virMutexLock(&(lockListWait.fileMutex));
    virLockManagerDlmWriteLock(lock, fd, 0);
    virMutexUnlock(&(lockListWait.fileMutex));

    if (VIR_CLOSE(fd) < 0) {
        virReportSystemError(errno,
                             _("unable to save file '%s'"),
                             lockRecordFilePath);
        VIR_FORCE_CLOSE(fd);
    }

 cleanup:
    VIR_FREE(lock->name);
    VIR_FREE(lock);
}

static int virLockManagerDlmRelease(virLockManagerPtr lock,
                                    char **state,
                                    unsigned int flags)
{
    virLockManagerDlmPrivatePtr priv = lock->privateData;
    virLockManagerDlmResourcePtr resource = NULL;
    virLockInformationPtr theLock = NULL;
    struct dlm_lksb lksb = {0};
    int rv = -1;
    size_t i;

    virCheckFlags(0, -1);

    if(state)
        *state = NULL;

    if(!lockspace) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("lockspace is not opened"));
        return -1;
    }

    for (i = 0; i < priv->nresources; i++) {
        resource = priv->resources + i;

        virListForEachEntry (theLock, &(lockListWait.list), entry) {
            if((theLock->vm_pid == priv->vm_pid)    &&
               STREQ(theLock->name, resource->name) &&
               (theLock->mode == resource->mode)) {

                /*
                 * there are some locks from adopting, the existence of `(void *)1`
                 * when adopting makes 'terminated by signal SIGSEGV (Address
                 * boundary error)' error appear.
                 *
                 * The following code reference to lvm2 project's implement.
                 */
                lksb.sb_lkid = theLock->lkid;
                rv = dlm_ls_lock_wait(lockspace, LKM_NLMODE,
                                      &lksb, LKF_CONVERT,
                                      resource->name,
                                      strlen(resource->name),
                                      0, NULL, NULL, NULL);

                if (rv < 0) {
                    virReportSystemError(errno,
                                         _("failed to convert lock: rv=%d lockStatus=%d"),
                                         rv, lksb.sb_status);
                    goto cleanup;
                }

                /* don't care whether the lock is released or not,
                 * it will be automatically released after the libvirtd dead
                 */
                virLockManagerDlmDeleteLock(theLock, driver->lockRecordFilePath);

                rv = dlm_ls_unlock_wait(lockspace, lksb.sb_lkid, 0, &lksb);
                if (rv < 0) {
                    virReportSystemError(errno,
                                         _("failed to release lock: rv=%d lockStatus=%d"),
                                         rv, lksb.sb_status);
                    goto cleanup;
                }

                break;
            }
        }
    }

    rv = 0;

 cleanup:
    return rv;
}

static int virLockManagerDlmInquire(virLockManagerPtr lock ATTRIBUTE_UNUSED,
                                    char **state,
                                    unsigned int flags)
{
    /* not support mannual lock, so this function almost does nothing */
    virCheckFlags(0, -1);

    if (state)
        *state = NULL;

    return 0;
}

virLockDriver virLockDriverImpl =
{
    .version = VIR_LOCK_MANAGER_VERSION,

    .flags = VIR_LOCK_MANAGER_USES_STATE, // currently not used

    .drvInit = virLockManagerDlmInit,
    .drvDeinit = virLockManagerDlmDeinit,

    .drvNew = virLockManagerDlmNew,
    .drvFree = virLockManagerDlmFree,

    .drvAddResource = virLockManagerDlmAddResource,

    .drvAcquire = virLockManagerDlmAcquire,
    .drvRelease = virLockManagerDlmRelease,
    .drvInquire = virLockManagerDlmInquire,
};
