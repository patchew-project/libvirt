/*
 * qemu_backup.c: Implementation and handling of the backup jobs
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

#include "qemu_block.h"
#include "qemu_conf.h"
#include "qemu_capabilities.h"
#include "qemu_monitor.h"
#include "qemu_process.h"
#include "qemu_backup.h"
#include "qemu_monitor_json.h"
#include "qemu_checkpoint.h"
#include "qemu_command.h"

#include "virerror.h"
#include "virlog.h"
#include "virbuffer.h"
#include "viralloc.h"
#include "virxml.h"
#include "virstoragefile.h"
#include "virstring.h"
#include "backup_conf.h"
#include "virdomaincheckpointobjlist.h"

#define VIR_FROM_THIS VIR_FROM_QEMU

VIR_LOG_INIT("qemu.qemu_backup");


static unsigned long long
qemuDomainGetBackupNextId(virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    return priv->backupnextid++;
}


static virDomainBackupDefPtr
qemuDomainGetBackup(virDomainObjPtr vm,
                    int id)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    if (!priv->backup ||
        (id != 0 && id != priv->backup->id)) {
        virReportError(VIR_ERR_NO_DOMAIN_BACKUP,
                       _("no domain backup job with id '%d'"), id);
        return NULL;
    }

    return priv->backup;
}


static int
qemuBackupPrepare(virDomainObjPtr vm,
                  virDomainBackupDefPtr def)
{
    /* we support just one job but we number them appropriately */
    def->id = qemuDomainGetBackupNextId(vm);

    if (def->type == VIR_DOMAIN_BACKUP_TYPE_PULL) {
        if (!def->server) {
            def->server = g_new(virStorageNetHostDef, 1);

            def->server->transport = VIR_STORAGE_NET_HOST_TRANS_TCP;
            def->server->name = g_strdup("localhost");
        }

        switch ((virStorageNetHostTransport) def->server->transport) {
        case VIR_STORAGE_NET_HOST_TRANS_TCP:
            /* TODO: Update qemu.conf to provide a port range,
             * probably starting at 10809, for obtaining automatic
             * port via virPortAllocatorAcquire, as well as store
             * somewhere if we need to call virPortAllocatorRelease
             * during BackupEnd. Until then, user must provide port */
            if (!def->server->port) {
                virReportError(VIR_ERR_OPERATION_UNSUPPORTED, "%s",
                               _("<domainbackup> must specify TCP port for now"));
                return -1;
            }
            break;

        case VIR_STORAGE_NET_HOST_TRANS_UNIX:
            /* TODO: Do we need to mess with selinux? */
            break;

        case VIR_STORAGE_NET_HOST_TRANS_RDMA:
        case VIR_STORAGE_NET_HOST_TRANS_LAST:
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("unexpected transport in <domainbackup>"));
            return -1;
        }
    }

    return 0;
}


struct qemuBackupDiskData {
    virDomainBackupDiskDefPtr backupdisk;
    virDomainDiskDefPtr domdisk;
    qemuBlockJobDataPtr blockjob;
    virStorageSourcePtr store;
    char *incrementalBitmap;
    qemuBlockStorageSourceChainDataPtr crdata;
    bool labelled;
    bool initialized;
    bool created;
    bool added;
    bool started;
    bool done;
};


static void
qemuBackupDiskDataCleanupOne(virDomainObjPtr vm,
                             struct qemuBackupDiskData *dd)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    if (dd->started)
        return;

    if (dd->added) {
        qemuDomainObjEnterMonitor(priv->driver, vm);
        qemuBlockStorageSourceAttachRollback(priv->mon, dd->crdata->srcdata[0]);
        ignore_value(qemuDomainObjExitMonitor(priv->driver, vm));
    }

    if (dd->created) {
        if (virStorageFileUnlink(dd->store) < 0)
            VIR_WARN("Unable to remove just-created %s", NULLSTR(dd->store->path));
    }

    if (dd->initialized)
        virStorageFileDeinit(dd->store);

    if (dd->labelled)
        qemuDomainStorageSourceAccessRevoke(priv->driver, vm, dd->store);

    if (dd->blockjob)
        qemuBlockJobStartupFinalize(vm, dd->blockjob);

    qemuBlockStorageSourceChainDataFree(dd->crdata);
}


static void
qemuBackupDiskDataCleanup(virDomainObjPtr vm,
                          struct qemuBackupDiskData *dd,
                          size_t ndd)
{
    virErrorPtr orig_err;
    size_t i;

    if (!dd)
        return;

    virErrorPreserveLast(&orig_err);

    for (i = 0; i < ndd; i++)
        qemuBackupDiskDataCleanupOne(vm, dd + i);

    g_free(dd);
    virErrorRestore(&orig_err);
}



static int
qemuBackupDiskPrepareOneBitmaps(struct qemuBackupDiskData *dd,
                                virJSONValuePtr actions,
                                virDomainMomentObjPtr *incremental)
{
    g_autoptr(virJSONValue) mergebitmapsdisk = NULL;
    g_autoptr(virJSONValue) mergebitmapsstore = NULL;

    if (!(mergebitmapsdisk = virJSONValueNewArray()))
        return -1;

    if (!(mergebitmapsstore = virJSONValueNewArray()))
        return -1;

    /* TODO: this code works only if the bitmaps are present on a single node.
     * The algorithm needs to be changed so that it looks into the backing chain
     * so that we can combine all relevant bitmaps for a given backing chain */
    while (*incremental) {
        if (qemuMonitorTransactionBitmapMergeSourceAddBitmap(mergebitmapsdisk,
                                                             dd->domdisk->src->nodeformat,
                                                             (*incremental)->def->name) < 0)
            return -1;

        if (qemuMonitorTransactionBitmapMergeSourceAddBitmap(mergebitmapsstore,
                                                             dd->domdisk->src->nodeformat,
                                                             (*incremental)->def->name) < 0)
            return -1;

        incremental++;
    }

    if (qemuMonitorTransactionBitmapAdd(actions,
                                        dd->domdisk->src->nodeformat,
                                        dd->incrementalBitmap,
                                        false,
                                        true) < 0)
        return -1;

    if (qemuMonitorTransactionBitmapMerge(actions,
                                          dd->domdisk->src->nodeformat,
                                          dd->incrementalBitmap,
                                          &mergebitmapsdisk) < 0)
        return -1;

    if (qemuMonitorTransactionBitmapAdd(actions,
                                        dd->store->nodeformat,
                                        dd->incrementalBitmap,
                                        false,
                                        true) < 0)
        return -1;

    if (qemuMonitorTransactionBitmapMerge(actions,
                                          dd->store->nodeformat,
                                          dd->incrementalBitmap,
                                          &mergebitmapsstore) < 0)
        return -1;


    return 0;
}


static int
qemuBackupDiskPrepareDataOne(virDomainObjPtr vm,
                             virDomainBackupDiskDefPtr backupdisk,
                             struct qemuBackupDiskData *dd,
                             virJSONValuePtr actions,
                             virDomainMomentObjPtr *incremental,
                             virQEMUDriverConfigPtr cfg,
                             unsigned int jobid)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    /* set data structure */
    dd->backupdisk = backupdisk;
    dd->store = dd->backupdisk->store;

    if (!(dd->domdisk = virDomainDiskByTarget(vm->def, dd->backupdisk->name))) {
        virReportError(VIR_ERR_INVALID_ARG,
                       _("no disk named '%s'"), dd->backupdisk->name);
        return -1;
    }

    if (!dd->store->format)
        dd->store->format = VIR_STORAGE_FILE_QCOW2;

    if (qemuDomainStorageFileInit(priv->driver, vm, dd->store, dd->domdisk->src) < 0)
        return -1;

    if (qemuDomainPrepareStorageSourceBlockdev(NULL, dd->store, priv, cfg) < 0)
        return -1;

    if (incremental) {
        dd->incrementalBitmap = g_strdup_printf("backup-%u-%s", jobid, dd->domdisk->dst);

        if (qemuBackupDiskPrepareOneBitmaps(dd, actions, incremental) < 0)
            return -1;
    }

    if (!(dd->blockjob = qemuBlockJobDiskNewBackup(vm, dd->domdisk, dd->store,
                                                   dd->incrementalBitmap,
                                                   jobid)))
        return -1;

    if (!(dd->crdata = qemuBuildStorageSourceChainAttachPrepareBlockdevTop(dd->store,
                                                                           NULL,
                                                                           priv->qemuCaps)))
        return -1;

    return 0;
}


static int
qemuBackupDiskPrepareDataOnePush(virJSONValuePtr actions,
                                 struct qemuBackupDiskData *dd)
{
    qemuMonitorTransactionBackupSyncMode syncmode = QEMU_MONITOR_TRANSACTION_BACKUP_SYNC_MODE_FULL;

    if (dd->incrementalBitmap)
        syncmode = QEMU_MONITOR_TRANSACTION_BACKUP_SYNC_MODE_INCREMENTAL;

    if (qemuMonitorTransactionBackup(actions,
                                     dd->domdisk->src->nodeformat,
                                     dd->blockjob->name,
                                     dd->store->nodeformat,
                                     dd->incrementalBitmap,
                                     syncmode) < 0)
        return -1;

    return 0;
}


static int
qemuBackupDiskPrepareDataOnePull(virJSONValuePtr actions,
                                 struct qemuBackupDiskData *dd)
{
    if (qemuMonitorTransactionBackup(actions,
                                     dd->domdisk->src->nodeformat,
                                     dd->blockjob->name,
                                     dd->store->nodeformat,
                                     NULL,
                                     QEMU_MONITOR_TRANSACTION_BACKUP_SYNC_MODE_NONE) < 0)
        return -1;

    return 0;
}


static ssize_t
qemuBackupDiskPrepareData(virDomainObjPtr vm,
                          virDomainBackupDefPtr def,
                          virDomainMomentObjPtr *incremental,
                          virJSONValuePtr actions,
                          virQEMUDriverConfigPtr cfg,
                          struct qemuBackupDiskData **rdd)
{
    struct qemuBackupDiskData *disks = NULL;
    ssize_t ndisks = 0;
    size_t i;

    disks = g_new0(struct qemuBackupDiskData, def->ndisks);

    for (i = 0; i < def->ndisks; i++) {
        virDomainBackupDiskDef *backupdisk = &def->disks[i];
        struct qemuBackupDiskData *dd = disks + ndisks;

        if (!backupdisk->store)
            continue;

        ndisks++;

        if (qemuBackupDiskPrepareDataOne(vm, backupdisk, dd, actions,
                                         incremental, cfg, def->id) < 0)
            goto error;

        if (def->type == VIR_DOMAIN_BACKUP_TYPE_PULL) {
            if (qemuBackupDiskPrepareDataOnePull(actions, dd) < 0)
                goto error;
        } else {
            if (qemuBackupDiskPrepareDataOnePush(actions, dd) < 0)
                goto error;
        }
    }

    *rdd = g_steal_pointer(&disks);

    return ndisks;

 error:
    qemuBackupDiskDataCleanup(vm, disks, ndisks);
    return -1;
}


static int
qemuBackupDiskPrepareOneStorage(virDomainObjPtr vm,
                                virHashTablePtr blockNamedNodeData,
                                struct qemuBackupDiskData *dd)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    if (virStorageSourceIsLocalStorage(dd->store) &&
        !virFileExists(dd->store->path) &&
        virStorageFileSupportsCreate(dd->store)) {

        if (qemuDomainStorageFileInit(priv->driver, vm, dd->store, NULL) < 0)
            return -1;

        dd->initialized = true;

        if (virStorageFileCreate(dd->store) < 0) {
            virReportSystemError(errno,
                                 _("failed to create image file '%s'"),
                                 NULLSTR(dd->store->path));
            return -1;
        }

        dd->created = true;
    }

    if (qemuDomainStorageSourceAccessAllow(priv->driver, vm, dd->store, false,
                                           true) < 0)
        return -1;

    dd->labelled = true;

    if (qemuBlockStorageSourceCreateDetectSize(blockNamedNodeData,
                                               dd->store, dd->domdisk->src) < 0)
        return -1;

    if (qemuBlockStorageSourceCreate(vm, dd->store, NULL, NULL,
                                     dd->crdata->srcdata[0], QEMU_ASYNC_JOB_NONE) < 0)
        return -1;

    dd->added = true;

    return 0;
}


static int
qemuBackupDiskPrepareStorage(virDomainObjPtr vm,
                             struct qemuBackupDiskData *disks,
                             size_t ndisks,
                             virHashTablePtr blockNamedNodeData)
{
    size_t i;

    for (i = 0; i < ndisks; i++) {
        if (qemuBackupDiskPrepareOneStorage(vm, blockNamedNodeData, disks + i) < 0)
            return -1;
    }

    return 0;
}


static void
qemuBackupDiskStarted(virDomainObjPtr vm,
                      struct qemuBackupDiskData *dd,
                      size_t ndd)
{
    size_t i;

    for (i = 0; i < ndd; i++) {
        dd[i].started = true;
        dd[i].backupdisk->state = VIR_DOMAIN_BACKUP_DISK_STATE_RUNNING;
        qemuBlockJobStarted(dd->blockjob, vm);
    }
}


/**
 * qemuBackupBeginPullExportDisks:
 * @vm: domain object
 * @disks: backup disk data list
 * @ndisks: number of valid disks in @disks
 *
 * Exports all disks from @dd when doing a pull backup in the NBD server. This
 * function must be called while in the monitor context.
 */
static int
qemuBackupBeginPullExportDisks(virDomainObjPtr vm,
                               struct qemuBackupDiskData *disks,
                               size_t ndisks)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    size_t i;

    for (i = 0; i < ndisks; i++) {
        struct qemuBackupDiskData *dd = disks + i;

        if (qemuMonitorNBDServerAdd(priv->mon,
                                    dd->store->nodeformat,
                                    dd->domdisk->dst,
                                    false,
                                    dd->incrementalBitmap) < 0)
            return -1;
    }

    return 0;
}


/**
 * qemuBackupBeginCollectIncrementalCheckpoints:
 * @vm: domain object
 * @incrFrom: name of checkpoint representing starting point of incremental backup
 *
 * Returns a NULL terminated list of pointers to checkpoints in chronological
 * order starting from the 'current' checkpoint until reaching @incrFrom.
 */
static virDomainMomentObjPtr *
qemuBackupBeginCollectIncrementalCheckpoints(virDomainObjPtr vm,
                                             const char *incrFrom)
{
    virDomainMomentObjPtr n = virDomainCheckpointGetCurrent(vm->checkpoints);
    g_autofree virDomainMomentObjPtr *incr = NULL;
    size_t nincr = 0;

    while (n) {
        if (VIR_APPEND_ELEMENT_COPY(incr, nincr, n) < 0)
            return NULL;

        if (STREQ(n->def->name, incrFrom)) {
            virDomainMomentObjPtr terminator = NULL;
            if (VIR_APPEND_ELEMENT_COPY(incr, nincr, terminator) < 0)
                return NULL;

            return g_steal_pointer(&incr);
        }

        if (!n->def->parent_name)
            break;

        n = virDomainCheckpointFindByName(vm->checkpoints, n->def->parent_name);
    }

    virReportError(VIR_ERR_OPERATION_INVALID,
                   _("could not locate checkpoint '%s' for incremental backup"),
                   incrFrom);
    return NULL;
}


static void
qemuBackupJobCancelBlockjobs(virDomainObjPtr vm,
                             virDomainBackupDefPtr backup)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    size_t i;
    int rc = 0;

    for (i = 0; i < backup->ndisks; i++) {
        virDomainBackupDiskDefPtr backupdisk = backup->disks + i;
        virDomainDiskDefPtr disk;
        g_autoptr(qemuBlockJobData) job = NULL;

        if (!backupdisk->store ||
            backupdisk->state != VIR_DOMAIN_BACKUP_DISK_STATE_RUNNING)
            continue;

        /* Look up corresponding disk as backupdisk->idx is no longer reliable */
        if (!(disk = virDomainDiskByTarget(vm->def, backupdisk->name)))
            continue;

        if (!(job = qemuBlockJobDiskGetJob(disk)))
            continue;

        qemuDomainObjEnterMonitor(priv->driver, vm);

        rc = qemuMonitorJobCancel(priv->mon, job->name, false);

        if (qemuDomainObjExitMonitor(priv->driver, vm) < 0)
            return;

        if (rc < 0)
            backupdisk->state = VIR_DOMAIN_BACKUP_DISK_STATE_FAILED;
        else
            backupdisk->state = VIR_DOMAIN_BACKUP_DISK_STATE_CANCELLING;

    }
}


int
qemuBackupBegin(virDomainObjPtr vm,
                const char *backupXML,
                const char *checkpointXML,
                unsigned int flags)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(priv->driver);
    virDomainBackupDefPtr def = NULL;
    g_autoptr(virCaps) caps = NULL;
    g_autofree char *suffix = NULL;
    struct timeval tv;
    bool pull = false;
    virDomainMomentObjPtr chk = NULL;
    g_autoptr(virDomainCheckpointDef) chkdef = NULL;
    g_autofree virDomainMomentObjPtr *incremental = NULL;
    g_autoptr(virJSONValue) actions = NULL;
    struct qemuBackupDiskData *dd = NULL;
    ssize_t ndd = 0;
    g_autoptr(virHashTable) blockNamedNodeData = NULL;
    bool job_started = false;
    bool nbd_running = false;
    int rc = 0;
    int ret = -1;

    virCheckFlags(0, -1);

    if (!virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_INCREMENTAL_BACKUP)) {
        virReportError(VIR_ERR_OPERATION_UNSUPPORTED, "%s",
                       _("incremental backup is not supported yet"));
        return -1;
    }

    if (!(caps = virQEMUDriverGetCapabilities(priv->driver, false)))
        goto cleanup;

    if (!(def = virDomainBackupDefParseString(backupXML, priv->driver->xmlopt, 0)))
        goto cleanup;

    if (checkpointXML) {
        if (!(chkdef = virDomainCheckpointDefParseString(checkpointXML, caps,
                                                         priv->driver->xmlopt,
                                                         priv->qemuCaps, 0)))
            goto cleanup;

        suffix = g_strdup(chkdef->parent.name);
    } else {
        gettimeofday(&tv, NULL);
        suffix = g_strdup_printf("%lld", (long long)tv.tv_sec);
    }

    if (def->type == VIR_DOMAIN_BACKUP_TYPE_PULL)
        pull = true;

    /* We are going to modify the domain below. */
    if (qemuDomainObjBeginJob(priv->driver, vm, QEMU_JOB_MODIFY) < 0)
        goto cleanup;

    if (!virDomainObjIsActive(vm)) {
        virReportError(VIR_ERR_OPERATION_UNSUPPORTED, "%s",
                       _("cannot perform disk backup for inactive domain"));
        goto endjob;
    }

    if (priv->backup) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       "%s", _("another backup job is already running"));
        goto endjob;
    }

    if (qemuBackupPrepare(vm, def) < 0)
        goto endjob;

    if (virDomainBackupAlignDisks(def, vm->def, suffix) < 0)
        goto endjob;

    if (def->incremental &&
        !(incremental = qemuBackupBeginCollectIncrementalCheckpoints(vm, def->incremental)))
        goto endjob;

    if (!(actions = virJSONValueNewArray()))
        goto endjob;

    if (chkdef) {
        if (qemuCheckpointCreateCommon(priv->driver, vm, caps, &chkdef,
                                       &actions, &chk) < 0)
            goto endjob;
    }

    if ((ndd = qemuBackupDiskPrepareData(vm, def, incremental, actions, cfg, &dd)) <= 0) {
        if (ndd == 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("no disks selected for backup"));
        }

        goto endjob;
    }

    qemuDomainObjEnterMonitor(priv->driver, vm);
    blockNamedNodeData = qemuMonitorBlockGetNamedNodeData(priv->mon);
    if (qemuDomainObjExitMonitor(priv->driver, vm) < 0 || !blockNamedNodeData)
        goto endjob;

    if (qemuBackupDiskPrepareStorage(vm, dd, ndd, blockNamedNodeData) < 0)
        goto endjob;

    priv->backup = g_steal_pointer(&def);

    qemuDomainObjEnterMonitor(priv->driver, vm);

    /* TODO: TLS is a must-have for the modern age */
    if (pull) {
        if ((rc = qemuMonitorNBDServerStart(priv->mon, priv->backup->server, NULL)) == 0)
            nbd_running = true;
    }

    if (rc == 0)
        rc = qemuMonitorTransaction(priv->mon, &actions);

    if (qemuDomainObjExitMonitor(priv->driver, vm) < 0 || rc < 0)
        goto endjob;

    job_started = true;
    qemuBackupDiskStarted(vm, dd, ndd);

    if (chk &&
        qemuCheckpointCreateFinalize(priv->driver, vm, cfg, chk, true) < 0)
        goto endjob;

    if (pull) {
        qemuDomainObjEnterMonitor(priv->driver, vm);
        /* note that if the export fails we've already created the checkpoint
         * and we will not delete it */
        rc = qemuBackupBeginPullExportDisks(vm, dd, ndd);
        if (qemuDomainObjExitMonitor(priv->driver, vm) < 0)
            goto endjob;

        if (rc < 0) {
            qemuBackupJobCancelBlockjobs(vm, priv->backup);
            goto endjob;
        }
    }

    ret = priv->backup->id;

 endjob:
    qemuBackupDiskDataCleanup(vm, dd, ndd);
    if (!job_started && nbd_running) {
        qemuDomainObjEnterMonitor(priv->driver, vm);
        ignore_value(qemuMonitorNBDServerStop(priv->mon));
        ignore_value(qemuDomainObjExitMonitor(priv->driver, vm));
    }

    if (ret < 0 && !job_started)
        def = g_steal_pointer(&priv->backup);

    qemuDomainObjEndJob(priv->driver, vm);

 cleanup:
    virDomainBackupDefFree(def);
    return ret;
}


char *
qemuBackupGetXMLDesc(virDomainObjPtr vm,
                     int id,
                     unsigned int flags)
{
    g_auto(virBuffer) buf = VIR_BUFFER_INITIALIZER;
    virDomainBackupDefPtr backup;

    virCheckFlags(0, NULL);

    if (!(backup = qemuDomainGetBackup(vm, id)))
        return NULL;

    if (virDomainBackupDefFormat(&buf, backup, false) < 0)
        return NULL;

    if (virBufferCheckError(&buf) < 0)
        return NULL;

    return virBufferContentAndReset(&buf);
}


static void
qemuBackupJobTerminate(virDomainObjPtr vm,
                       virDomainBackupDefPtr backup,
                       bool success G_GNUC_UNUSED)

{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    VIR_DEBUG("id:'%d'", backup->id);

    if (priv->backup == backup)
        priv->backup = NULL;

    virDomainBackupDefFree(backup);
}


void
qemuBackupNotifyBlockjobEnd(virDomainObjPtr vm,
                            int id,
                            virDomainDiskDefPtr disk,
                            qemuBlockjobState state)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    bool all_complete = true;
    bool has_running = false;
    bool has_cancelling = false;
    virDomainBackupDefPtr backup;
    size_t i;

    VIR_DEBUG("vm: '%s', id:'%d', disk:'%s', state:'%d'",
              vm->def->name, id, disk->dst, state);

    if (!(backup = qemuDomainGetBackup(vm, id)))
        return;

    if (backup->type == VIR_DOMAIN_BACKUP_TYPE_PULL) {
        qemuDomainObjEnterMonitor(priv->driver, vm);
        ignore_value(qemuMonitorNBDServerStop(priv->mon));
        if (qemuDomainObjExitMonitor(priv->driver, vm) < 0)
            return;
    }

    for (i = 0; i < backup->ndisks; i++) {
        virDomainBackupDiskDefPtr backupdisk = backup->disks + i;

        if (!backupdisk->store)
            continue;

        if (STREQ(disk->dst, backupdisk->name)) {
            switch (state) {
            case QEMU_BLOCKJOB_STATE_COMPLETED:
                backupdisk->state = VIR_DOMAIN_BACKUP_DISK_STATE_COMPLETE;
                break;

            case QEMU_BLOCKJOB_STATE_FAILED:
                backupdisk->state = VIR_DOMAIN_BACKUP_DISK_STATE_FAILED;
                break;

            case QEMU_BLOCKJOB_STATE_CANCELLED:
                backupdisk->state = VIR_DOMAIN_BACKUP_DISK_STATE_CANCELLED;
                break;

            case QEMU_BLOCKJOB_STATE_READY:
            case QEMU_BLOCKJOB_STATE_NEW:
            case QEMU_BLOCKJOB_STATE_RUNNING:
            case QEMU_BLOCKJOB_STATE_CONCLUDED:
            case QEMU_BLOCKJOB_STATE_ABORTING:
            case QEMU_BLOCKJOB_STATE_PIVOTING:
            case QEMU_BLOCKJOB_STATE_LAST:
            default:
                break;
            }
        }

        switch (backupdisk->state) {
        case VIR_DOMAIN_BACKUP_DISK_STATE_COMPLETE:
            break;

        case VIR_DOMAIN_BACKUP_DISK_STATE_RUNNING:
            all_complete = false;
            has_running = true;
            break;

        case VIR_DOMAIN_BACKUP_DISK_STATE_CANCELLING:
            all_complete = false;
            has_cancelling = true;
            break;

        case VIR_DOMAIN_BACKUP_DISK_STATE_CANCELLED:
        case VIR_DOMAIN_BACKUP_DISK_STATE_FAILED:
            all_complete = false;
            break;

        case VIR_DOMAIN_BACKUP_DISK_STATE_NONE:
        case VIR_DOMAIN_BACKUP_DISK_STATE_LAST:
            break;
        }
    }

    if (!has_running && !has_cancelling) {
        qemuBackupJobTerminate(vm, backup, all_complete);
    } else if (has_running) {
        qemuBackupJobCancelBlockjobs(vm, backup);
    }
}


int
qemuBackupEnd(virDomainObjPtr vm,
              int id,
              unsigned int flags)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    virDomainBackupDefPtr backup = NULL;
    int ret = -1;

    virCheckFlags(0, -1);

    if (qemuDomainObjBeginJob(priv->driver, vm, QEMU_JOB_MODIFY) < 0)
        return -1;

    if (!(backup = qemuDomainGetBackup(vm, id)))
        goto endjob;

    qemuBackupJobCancelBlockjobs(vm, backup);

    ret = 0;

 endjob:
    qemuDomainObjEndJob(priv->driver, vm);

    return ret;
}
