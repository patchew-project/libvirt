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


/**
 * qemuBlockJobEventProcess:
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
qemuBlockJobEventProcess(virQEMUDriverPtr driver,
                         virDomainObjPtr vm,
                         virDomainDiskDefPtr disk,
                         qemuDomainAsyncJob asyncJob,
                         int type,
                         int status)
{
    virQEMUDriverConfigPtr cfg = virQEMUDriverGetConfig(driver);
    virDomainDiskDefPtr persistDisk = NULL;
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
        diskPriv->blockjob = false;
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
        diskPriv->blockjob = false;
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
 * qemuBlockJobUpdate:
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
qemuBlockJobUpdate(virDomainObjPtr vm,
                   qemuDomainAsyncJob asyncJob,
                   virDomainDiskDefPtr disk,
                   char **error)
{
    qemuDomainDiskPrivatePtr diskPriv = QEMU_DOMAIN_DISK_PRIVATE(disk);
    qemuDomainObjPrivatePtr priv = vm->privateData;
    int status = diskPriv->blockJobStatus;

    if (error)
        *error = NULL;

    if (status != -1) {
        qemuBlockJobEventProcess(priv->driver, vm, disk, asyncJob,
                                 diskPriv->blockJobType,
                                 diskPriv->blockJobStatus);
        diskPriv->blockJobStatus = -1;
        if (error)
            VIR_STEAL_PTR(*error, diskPriv->blockJobError);
        else
            VIR_FREE(diskPriv->blockJobError);
    }

    return status;
}


/**
 * qemuBlockJobSyncBegin:
 * @disk: domain disk
 *
 * Begin a new synchronous block job for @disk. The synchronous
 * block job is ended by a call to qemuBlockJobSyncEnd, or by
 * the guest quitting.
 *
 * During a synchronous block job, a block job event for @disk
 * will not be processed asynchronously. Instead, it will be
 * processed only when qemuBlockJobUpdate or qemuBlockJobSyncEnd
 * is called.
 */
void
qemuBlockJobSyncBegin(virDomainDiskDefPtr disk)
{
    qemuDomainDiskPrivatePtr diskPriv = QEMU_DOMAIN_DISK_PRIVATE(disk);

    VIR_DEBUG("disk=%s", disk->dst);
    diskPriv->blockJobSync = true;
    diskPriv->blockJobStatus = -1;
}


/**
 * qemuBlockJobSyncEnd:
 * @vm: domain
 * @disk: domain disk
 *
 * End a synchronous block job for @disk. Any pending block job event
 * for the disk is processed.
 */
void
qemuBlockJobSyncEnd(virDomainObjPtr vm,
                    qemuDomainAsyncJob asyncJob,
                    virDomainDiskDefPtr disk)
{
    VIR_DEBUG("disk=%s", disk->dst);
    qemuBlockJobUpdate(vm, asyncJob, disk, NULL);
    QEMU_DOMAIN_DISK_PRIVATE(disk)->blockJobSync = false;
}
