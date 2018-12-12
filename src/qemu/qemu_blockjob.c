/*
 * qemu_blockjob.c: helper functions for QEMU block jobs
 *
 * Copyright (C) 2006-2015 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
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

#include "internal.h"

#include "qemu_blockjob.h"
#include "qemu_block.h"
#include "qemu_domain.h"

#include "conf/domain_conf.h"
#include "conf/domain_event.h"

#include "virlog.h"
#include "virstoragefile.h"
#include "virthread.h"
#include "virtime.h"
#include "locking/domain_lock.h"
#include "viralloc.h"

#define VIR_FROM_THIS VIR_FROM_QEMU

VIR_LOG_INIT("qemu.qemu_blockjob");


static virClassPtr qemuBlockJobDataClass;


static void
qemuBlockJobDataDispose(void *obj)
{
    qemuBlockJobDataPtr job = obj;

    VIR_FREE(job->errmsg);
}


static int
qemuBlockJobDataOnceInit(void)
{
    if (!VIR_CLASS_NEW(qemuBlockJobData, virClassForObject()))
        return -1;

    return 0;
}


VIR_ONCE_GLOBAL_INIT(qemuBlockJobData)

qemuBlockJobDataPtr
qemuBlockJobDataNew(void)
{
    if (qemuBlockJobDataInitialize() < 0)
        return NULL;

    return virObjectNew(qemuBlockJobDataClass);
}


static void
qemuBlockJobDataReset(qemuBlockJobDataPtr job)
{
    job->started = false;
    job->type = -1;
    job->newstate = -1;
    VIR_FREE(job->errmsg);
    job->synchronous = false;
}


/**
 * qemuBlockJobDiskNew:
 * @disk: disk definition
 *
 * Start/associate a new blockjob with @disk.
 *
 * Returns 0 on success and -1 on failure.
 */
qemuBlockJobDataPtr
qemuBlockJobDiskNew(virDomainDiskDefPtr disk)
{
    qemuBlockJobDataPtr job = QEMU_DOMAIN_DISK_PRIVATE(disk)->blockjob;

    qemuBlockJobDataReset(job);
    return virObjectRef(job);
}


/**
 * qemuBlockJobDiskGetJob:
 * @disk: disk definition
 *
 * Get a reference to the block job data object associated with @disk.
 */
qemuBlockJobDataPtr
qemuBlockJobDiskGetJob(virDomainDiskDefPtr disk)
{
    qemuBlockJobDataPtr job = QEMU_DOMAIN_DISK_PRIVATE(disk)->blockjob;

    if (!job)
        return NULL;

    return virObjectRef(job);
}


/**
 * qemuBlockJobStarted:
 * @job: job data
 *
 * Mark @job as started in qemu.
 */
void
qemuBlockJobStarted(qemuBlockJobDataPtr job)
{
    job->started = true;
}


/**
 * qemuBlockJobStartupFinalize:
 * @job: job being started
 *
 * Cancels and clears the job private data if the job was not started with
 * qemu (see qemuBlockJobStarted) or just clears up the local reference
 * to @job if it was started.
 */
void
qemuBlockJobStartupFinalize(qemuBlockJobDataPtr job)
{
    if (!job)
        return;

    if (!job->started)
        qemuBlockJobDataReset(job);

    virObjectUnref(job);
}


/**
 * qemuBlockJobEmitEvents:
 *
 * Emits the VIR_DOMAIN_EVENT_ID_BLOCK_JOB and VIR_DOMAIN_EVENT_ID_BLOCK_JOB_2
 * for a block job. The former event is emitted only for local disks.
 */
static void
qemuBlockJobEmitEvents(virQEMUDriverPtr driver,
                       virDomainObjPtr vm,
                       virDomainDiskDefPtr disk,
                       virDomainBlockJobType type,
                       virConnectDomainEventBlockJobStatus status)
{
    virObjectEventPtr event = NULL;
    virObjectEventPtr event2 = NULL;

    if (virStorageSourceIsLocalStorage(disk->src) &&
        !virStorageSourceIsEmpty(disk->src)) {
        event = virDomainEventBlockJobNewFromObj(vm, disk->src->path, type, status);
        virObjectEventStateQueue(driver->domainEventState, event);
    }

    event2 = virDomainEventBlockJob2NewFromObj(vm, disk->dst, type, status);
    virObjectEventStateQueue(driver->domainEventState, event2);
}


static void
qemuBlockJobEventProcessLegacyCompleted(virQEMUDriverPtr driver,
                                        virDomainObjPtr vm,
                                        virDomainDiskDefPtr disk,
                                        int asyncJob)
{
    qemuDomainDiskPrivatePtr diskPriv = QEMU_DOMAIN_DISK_PRIVATE(disk);
    virDomainDiskDefPtr persistDisk = NULL;

    if (disk->mirrorState == VIR_DOMAIN_DISK_MIRROR_STATE_PIVOT) {
        if (vm->newDef) {
            virStorageSourcePtr copy = NULL;

            if ((persistDisk = virDomainDiskByName(vm->newDef,
                                                   disk->dst, false))) {
                copy = virStorageSourceCopy(disk->mirror, false);
                if (!copy ||
                    virStorageSourceInitChainElement(copy,
                                                     persistDisk->src,
                                                     true) < 0) {
                    VIR_WARN("Unable to update persistent definition "
                             "on vm %s after block job",
                             vm->def->name);
                    virStorageSourceFree(copy);
                    copy = NULL;
                    persistDisk = NULL;
                }
            }
            if (copy) {
                virStorageSourceFree(persistDisk->src);
                persistDisk->src = copy;
            }
        }

        /* XXX We want to revoke security labels as well as audit that
         * revocation, before dropping the original source.  But it gets
         * tricky if both source and mirror share common backing files (we
         * want to only revoke the non-shared portion of the chain); so for
         * now, we leak the access to the original.  */
        virDomainLockImageDetach(driver->lockManager, vm, disk->src);
        virStorageSourceFree(disk->src);
        disk->src = disk->mirror;
    } else {
        if (disk->mirror) {
            virDomainLockImageDetach(driver->lockManager, vm, disk->mirror);
            virStorageSourceFree(disk->mirror);
        }
    }

    /* Recompute the cached backing chain to match our
     * updates.  Better would be storing the chain ourselves
     * rather than reprobing, but we haven't quite completed
     * that conversion to use our XML tracking. */
    disk->mirror = NULL;
    disk->mirrorState = VIR_DOMAIN_DISK_MIRROR_STATE_NONE;
    disk->mirrorJob = VIR_DOMAIN_BLOCK_JOB_TYPE_UNKNOWN;
    disk->src->id = 0;
    virStorageSourceBackingStoreClear(disk->src);
    ignore_value(qemuDomainDetermineDiskChain(driver, vm, disk, true));
    ignore_value(qemuBlockNodeNamesDetect(driver, vm, asyncJob));
    diskPriv->blockjob->started = false;
}


/**
 * qemuBlockJobEventProcessLegacy:
 * @driver: qemu driver
 * @vm: domain
 * @disk: domain disk
 * @type: block job type
 * @status: block job status
 *
 * Update disk's mirror state in response to a block job event
 * from QEMU. For mirror state's that must survive libvirt
 * restart, also update the domain's status XML.
 */
static void
qemuBlockJobEventProcessLegacy(virQEMUDriverPtr driver,
                               virDomainObjPtr vm,
                               virDomainDiskDefPtr disk,
                               int asyncJob,
                               int type,
                               int status)
{
    virQEMUDriverConfigPtr cfg = virQEMUDriverGetConfig(driver);
    qemuDomainDiskPrivatePtr diskPriv = QEMU_DOMAIN_DISK_PRIVATE(disk);

    VIR_DEBUG("disk=%s, mirrorState=%s, type=%d, status=%d",
              disk->dst,
              NULLSTR(virDomainDiskMirrorStateTypeToString(disk->mirrorState)),
              type,
              status);

    if (type == VIR_DOMAIN_BLOCK_JOB_TYPE_COMMIT &&
        disk->mirrorJob == VIR_DOMAIN_BLOCK_JOB_TYPE_ACTIVE_COMMIT)
        type = disk->mirrorJob;

    qemuBlockJobEmitEvents(driver, vm, disk, type, status);

    /* If we completed a block pull or commit, then update the XML
     * to match.  */
    switch ((virConnectDomainEventBlockJobStatus) status) {
    case VIR_DOMAIN_BLOCK_JOB_COMPLETED:
        qemuBlockJobEventProcessLegacyCompleted(driver, vm, disk, asyncJob);
        break;

    case VIR_DOMAIN_BLOCK_JOB_READY:
        disk->mirrorState = VIR_DOMAIN_DISK_MIRROR_STATE_READY;
        break;

    case VIR_DOMAIN_BLOCK_JOB_FAILED:
    case VIR_DOMAIN_BLOCK_JOB_CANCELED:
        if (disk->mirror) {
            virDomainLockImageDetach(driver->lockManager, vm, disk->mirror);
            virStorageSourceFree(disk->mirror);
            disk->mirror = NULL;
        }
        disk->mirrorState = VIR_DOMAIN_DISK_MIRROR_STATE_NONE;
        disk->mirrorJob = VIR_DOMAIN_BLOCK_JOB_TYPE_UNKNOWN;
        diskPriv->blockjob->started = false;
        break;

    case VIR_DOMAIN_BLOCK_JOB_LAST:
        break;
    }

    if (virDomainSaveStatus(driver->xmlopt, cfg->stateDir, vm, driver->caps) < 0)
        VIR_WARN("Unable to save status on vm %s after block job", vm->def->name);

    if (status == VIR_DOMAIN_BLOCK_JOB_COMPLETED && vm->newDef) {
        if (virDomainSaveConfig(cfg->configDir, driver->caps, vm->newDef) < 0)
            VIR_WARN("Unable to update persistent definition on vm %s "
                     "after block job", vm->def->name);
    }

    virObjectUnref(cfg);
}


/**
 * qemuBlockJobUpdateDisk:
 * @vm: domain
 * @disk: domain disk
 * @error: error (output parameter)
 *
 * Update disk's mirror state in response to a block job event stored in
 * blockJobStatus by qemuProcessHandleBlockJob event handler.
 *
 * Returns the block job event processed or -1 if there was no pending event.
 */
int
qemuBlockJobUpdateDisk(virDomainObjPtr vm,
                       int asyncJob,
                       virDomainDiskDefPtr disk,
                       char **error)
{
    qemuBlockJobDataPtr job = QEMU_DOMAIN_DISK_PRIVATE(disk)->blockjob;
    qemuDomainObjPrivatePtr priv = vm->privateData;
    int state = job->newstate;

    if (error)
        *error = NULL;

    if (state != -1) {
        qemuBlockJobEventProcessLegacy(priv->driver, vm, disk, asyncJob,
                                       job->type, state);
        job->newstate = -1;
        if (error)
            VIR_STEAL_PTR(*error, job->errmsg);
        else
            VIR_FREE(job->errmsg);
    }

    return state;
}


/**
 * qemuBlockJobSyncBeginDisk:
 * @disk: domain disk
 *
 * Begin a new synchronous block job for @disk. The synchronous
 * block job is ended by a call to qemuBlockJobSyncEndDisk, or by
 * the guest quitting.
 *
 * During a synchronous block job, a block job event for @disk
 * will not be processed asynchronously. Instead, it will be
 * processed only when qemuBlockJobUpdateDisk or qemuBlockJobSyncEndDisk
 * is called.
 */
void
qemuBlockJobSyncBeginDisk(virDomainDiskDefPtr disk)
{
    qemuBlockJobDataPtr job = QEMU_DOMAIN_DISK_PRIVATE(disk)->blockjob;

    VIR_DEBUG("disk=%s", disk->dst);
    job->synchronous = true;
    job->newstate = -1;
}


/**
 * qemuBlockJobSyncEndDisk:
 * @vm: domain
 * @disk: domain disk
 *
 * End a synchronous block job for @disk. Any pending block job event
 * for the disk is processed. Note that it's not necessary to call this function
 * in case the block job was not started successfully if
 * qemuBlockJobStartupFinalize will be called.
 */
void
qemuBlockJobSyncEndDisk(virDomainObjPtr vm,
                        int asyncJob,
                        virDomainDiskDefPtr disk)
{
    VIR_DEBUG("disk=%s", disk->dst);
    qemuBlockJobUpdateDisk(vm, asyncJob, disk, NULL);
    QEMU_DOMAIN_DISK_PRIVATE(disk)->blockjob->synchronous = false;
}
