/*
 * qemu_backup.h: Implementation and handling of the backup jobs
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

int
qemuBackupBegin(virDomainObjPtr vm,
                const char *backupXML,
                const char *checkpointXML,
                unsigned int flags);

char *
qemuBackupGetXMLDesc(virDomainObjPtr vm,
                     unsigned int flags);

void
qemuBackupJobCancelBlockjobs(virDomainObjPtr vm,
                             virDomainBackupDefPtr backup,
                             bool terminatebackup,
                             int asyncJob);

void
qemuBackupNotifyBlockjobEnd(virDomainObjPtr vm,
                            virDomainDiskDefPtr disk,
                            qemuBlockjobState state,
                            const char *errmsg,
                            unsigned long long cur,
                            unsigned long long end,
                            int asyncJob);

void
qemuBackupJobTerminate(virDomainObjPtr vm,
                       qemuDomainJobStatus jobstatus);

int
qemuBackupGetJobInfoStats(virQEMUDriverPtr driver,
                          virDomainObjPtr vm,
                          qemuDomainJobInfoPtr jobInfo);

/* exported for testing */
int
qemuBackupDiskPrepareOneBitmapsChain(virStorageSourcePtr backingChain,
                                     virStorageSourcePtr targetsrc,
                                     const char *targetbitmap,
                                     const char *incremental,
                                     virJSONValuePtr actions,
                                     virHashTablePtr blockNamedNodeData);
