/*
 * domain_audit.h: Domain audit management
 *
 * Copyright (C) 2006-2011 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "domain_conf.h"
#include "vircgroup.h"

void virDomainAuditStart(virDomainObjPtr vm,
                         const char *reason,
                         bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);
void virDomainAuditInit(virDomainObjPtr vm,
                        pid_t pid,
                        ino_t pidns)
    ATTRIBUTE_NONNULL(1);
void virDomainAuditStop(virDomainObjPtr vm,
                        const char *reason)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);
void virDomainAuditDisk(virDomainObjPtr vm,
                        virStorageSourcePtr oldDef,
                        virStorageSourcePtr newDef,
                        const char *reason,
                        bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(4);
void virDomainAuditFS(virDomainObjPtr vm,
                      virDomainFSDefPtr oldDef,
                      virDomainFSDefPtr newDef,
                      const char *reason,
                      bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(4);
void virDomainAuditNet(virDomainObjPtr vm,
                       virDomainNetDefPtr oldDef,
                       virDomainNetDefPtr newDef,
                       const char *reason,
                       bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(4);
void virDomainAuditNetDevice(virDomainDefPtr vmDef,
                             virDomainNetDefPtr netDef,
                             const char *device,
                             bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);
void virDomainAuditHostdev(virDomainObjPtr vm,
                           virDomainHostdevDefPtr def,
                           const char *reason,
                           bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);
void virDomainAuditCgroup(virDomainObjPtr vm,
                          virCgroupPtr group,
                          const char *reason,
                          const char *extra,
                          bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3)
    ATTRIBUTE_NONNULL(4);
void virDomainAuditCgroupMajor(virDomainObjPtr vm,
                               virCgroupPtr group,
                               const char *reason,
                               int maj,
                               const char *name,
                               const char *perms,
                               bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3)
    ATTRIBUTE_NONNULL(5) ATTRIBUTE_NONNULL(6);
void virDomainAuditCgroupPath(virDomainObjPtr vm,
                              virCgroupPtr group,
                              const char *reason,
                              const char *path,
                              const char *perms,
                              int rc)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3)
    ATTRIBUTE_NONNULL(4) ATTRIBUTE_NONNULL(5);
void virDomainAuditMemory(virDomainObjPtr vm,
                          unsigned long long oldmem,
                          unsigned long long newmem,
                          const char *reason,
                          bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(4);
void virDomainAuditVcpu(virDomainObjPtr vm,
                        unsigned int oldvcpu,
                        unsigned int newvcpu,
                        const char *reason,
                        bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(4);
void virDomainAuditIOThread(virDomainObjPtr vm,
                            unsigned int oldiothread,
                            unsigned int newiothread,
                            const char *reason,
                            bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(4);
void virDomainAuditSecurityLabel(virDomainObjPtr vm,
                                 bool success)
    ATTRIBUTE_NONNULL(1);
void virDomainAuditRedirdev(virDomainObjPtr vm,
                            virDomainRedirdevDefPtr def,
                            const char *reason,
                            bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);

void virDomainAuditChardev(virDomainObjPtr vm,
                           virDomainChrDefPtr oldDef,
                           virDomainChrDefPtr newDef,
                           const char *reason,
                           bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(4);
void virDomainAuditRNG(virDomainObjPtr vm,
                       virDomainRNGDefPtr oldDef,
                       virDomainRNGDefPtr newDef,
                       const char *reason,
                       bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(4);
void virDomainAuditShmem(virDomainObjPtr vm,
                         virDomainShmemDefPtr def,
                         const char *reason, bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);
void virDomainAuditInput(virDomainObjPtr vm,
                         virDomainInputDefPtr input,
                         const char *reason,
                         bool success)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);
