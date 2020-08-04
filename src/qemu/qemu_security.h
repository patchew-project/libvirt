/*
 * qemu_security.h: QEMU security management
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "qemu_conf.h"
#include "domain_conf.h"
#include "security/security_manager.h"

int qemuSecuritySetAllLabel(virQEMUDriverPtr driver,
                            virDomainObjPtr vm,
                            const char *incomingPath,
                            bool migrated);

void qemuSecurityRestoreAllLabel(virQEMUDriverPtr driver,
                                 virDomainObjPtr vm,
                                 bool migrated);

int qemuSecuritySetImageLabel(virQEMUDriverPtr driver,
                              virDomainObjPtr vm,
                              virStorageSourcePtr src,
                              bool backingChain,
                              bool chainTop);

int qemuSecurityRestoreImageLabel(virQEMUDriverPtr driver,
                                  virDomainObjPtr vm,
                                  virStorageSourcePtr src,
                                  bool backingChain);

int qemuSecurityMoveImageMetadata(virQEMUDriverPtr driver,
                                  virDomainObjPtr vm,
                                  virStorageSourcePtr src,
                                  virStorageSourcePtr dst);

int qemuSecuritySetHostdevLabel(virQEMUDriverPtr driver,
                                virDomainObjPtr vm,
                                virDomainHostdevDefPtr hostdev);

int qemuSecurityRestoreHostdevLabel(virQEMUDriverPtr driver,
                                    virDomainObjPtr vm,
                                    virDomainHostdevDefPtr hostdev);

int qemuSecuritySetMemoryLabel(virQEMUDriverPtr driver,
                               virDomainObjPtr vm,
                               virDomainMemoryDefPtr mem);

int qemuSecurityRestoreMemoryLabel(virQEMUDriverPtr driver,
                                   virDomainObjPtr vm,
                                   virDomainMemoryDefPtr mem);

int qemuSecuritySetInputLabel(virDomainObjPtr vm,
                              virDomainInputDefPtr input);

int qemuSecurityRestoreInputLabel(virDomainObjPtr vm,
                                  virDomainInputDefPtr input);

int qemuSecuritySetChardevLabel(virQEMUDriverPtr driver,
                                virDomainObjPtr vm,
                                virDomainChrDefPtr chr);

int qemuSecurityRestoreChardevLabel(virQEMUDriverPtr driver,
                                    virDomainObjPtr vm,
                                    virDomainChrDefPtr chr);

int qemuSecurityStartVhostUserGPU(virQEMUDriverPtr driver,
                                  virDomainObjPtr vm,
                                  virCommandPtr cmd,
                                  int *exitstatus,
                                  int *cmdret);

int qemuSecurityStartTPMEmulator(virQEMUDriverPtr driver,
                                 virDomainObjPtr vm,
                                 virCommandPtr cmd,
                                 uid_t uid,
                                 gid_t gid,
                                 int *exitstatus,
                                 int *cmdret);

void qemuSecurityCleanupTPMEmulator(virQEMUDriverPtr driver,
                                    virDomainObjPtr vm);

int qemuSecuritySetSavedStateLabel(virQEMUDriverPtr driver,
                                   virDomainObjPtr vm,
                                   const char *savefile);

int qemuSecurityRestoreSavedStateLabel(virQEMUDriverPtr driver,
                                       virDomainObjPtr vm,
                                       const char *savefile);

int qemuSecurityDomainSetPathLabel(virQEMUDriverPtr driver,
                                   virDomainObjPtr vm,
                                   const char *path,
                                   bool allowSubtree);

int qemuSecurityDomainRestorePathLabel(virQEMUDriverPtr driver,
                                       virDomainObjPtr vm,
                                       const char *path);

int qemuSecurityCommandRun(virQEMUDriverPtr driver,
                           virDomainObjPtr vm,
                           virCommandPtr cmd,
                           uid_t uid,
                           gid_t gid,
                           int *exitstatus,
                           int *cmdret);

/* Please note that for these APIs there is no wrapper yet. Do NOT blindly add
 * new APIs here. If an API can touch a file add a proper wrapper instead.
 */
#define qemuSecurityCheckAllLabel virSecurityManagerCheckAllLabel
#define qemuSecurityClearSocketLabel virSecurityManagerClearSocketLabel
#define qemuSecurityGenLabel virSecurityManagerGenLabel
#define qemuSecurityGetBaseLabel virSecurityManagerGetBaseLabel
#define qemuSecurityGetDOI virSecurityManagerGetDOI
#define qemuSecurityGetModel virSecurityManagerGetModel
#define qemuSecurityGetMountOptions virSecurityManagerGetMountOptions
#define qemuSecurityGetNested virSecurityManagerGetNested
#define qemuSecurityGetProcessLabel virSecurityManagerGetProcessLabel
#define qemuSecurityNew virSecurityManagerNew
#define qemuSecurityNewDAC virSecurityManagerNewDAC
#define qemuSecurityNewStack virSecurityManagerNewStack
#define qemuSecurityPostFork virSecurityManagerPostFork
#define qemuSecurityPreFork virSecurityManagerPreFork
#define qemuSecurityReleaseLabel virSecurityManagerReleaseLabel
#define qemuSecurityReserveLabel virSecurityManagerReserveLabel
#define qemuSecurityRestoreSavedStateLabel virSecurityManagerRestoreSavedStateLabel
#define qemuSecuritySetChildProcessLabel virSecurityManagerSetChildProcessLabel
#define qemuSecuritySetDaemonSocketLabel virSecurityManagerSetDaemonSocketLabel
#define qemuSecuritySetImageFDLabel virSecurityManagerSetImageFDLabel
#define qemuSecuritySetSavedStateLabel virSecurityManagerSetSavedStateLabel
#define qemuSecuritySetSocketLabel virSecurityManagerSetSocketLabel
#define qemuSecuritySetTapFDLabel virSecurityManagerSetTapFDLabel
#define qemuSecurityStackAddNested virSecurityManagerStackAddNested
#define qemuSecurityVerify virSecurityManagerVerify
