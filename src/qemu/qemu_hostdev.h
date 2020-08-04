/*
 * qemu_hostdev.h: QEMU hostdev management
 *
 * Copyright (C) 2006-2007, 2009-2013 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "qemu_conf.h"
#include "domain_conf.h"

bool qemuHostdevNeedsVFIO(const virDomainHostdevDef *hostdev);

bool qemuHostdevHostSupportsPassthroughVFIO(void);

int qemuHostdevUpdateActiveNVMeDisks(virQEMUDriverPtr driver,
                                     virDomainDefPtr def);
int qemuHostdevUpdateActiveMediatedDevices(virQEMUDriverPtr driver,
                                           virDomainDefPtr def);
int qemuHostdevUpdateActivePCIDevices(virQEMUDriverPtr driver,
                                      virDomainDefPtr def);
int qemuHostdevUpdateActiveUSBDevices(virQEMUDriverPtr driver,
                                      virDomainDefPtr def);
int qemuHostdevUpdateActiveSCSIDevices(virQEMUDriverPtr driver,
                                       virDomainDefPtr def);
int qemuHostdevUpdateActiveDomainDevices(virQEMUDriverPtr driver,
                                         virDomainDefPtr def);

int qemuHostdevPrepareOneNVMeDisk(virQEMUDriverPtr driver,
                                  const char *name,
                                  virStorageSourcePtr src);
int qemuHostdevPrepareNVMeDisks(virQEMUDriverPtr driver,
                                const char *name,
                                virDomainDiskDefPtr *disks,
                                size_t ndisks);
int qemuHostdevPreparePCIDevices(virQEMUDriverPtr driver,
                                 const char *name,
                                 const unsigned char *uuid,
                                 virDomainHostdevDefPtr *hostdevs,
                                 int nhostdevs,
                                 virQEMUCapsPtr qemuCaps,
                                 unsigned int flags);
int qemuHostdevPrepareUSBDevices(virQEMUDriverPtr driver,
                                 const char *name,
                                 virDomainHostdevDefPtr *hostdevs,
                                 int nhostdevs,
                                 unsigned int flags);
int qemuHostdevPrepareSCSIDevices(virQEMUDriverPtr driver,
                                  const char *name,
                                  virDomainHostdevDefPtr *hostdevs,
                                  int nhostdevs);
int qemuHostdevPrepareSCSIVHostDevices(virQEMUDriverPtr driver,
                                       const char *name,
                                       virDomainHostdevDefPtr *hostdevs,
                                       int nhostdevs);
int qemuHostdevPrepareMediatedDevices(virQEMUDriverPtr driver,
                                      const char *name,
                                      virDomainHostdevDefPtr *hostdevs,
                                      int nhostdevs);
int qemuHostdevPrepareDomainDevices(virQEMUDriverPtr driver,
                                    virDomainDefPtr def,
                                    virQEMUCapsPtr qemuCaps,
                                    unsigned int flags);

void qemuHostdevReAttachOneNVMeDisk(virQEMUDriverPtr driver,
                                    const char *name,
                                    virStorageSourcePtr src);
void qemuHostdevReAttachNVMeDisks(virQEMUDriverPtr driver,
                                  const char *name,
                                  virDomainDiskDefPtr *disks,
                                  size_t ndisks);
void qemuHostdevReAttachPCIDevices(virQEMUDriverPtr driver,
                                   const char *name,
                                   virDomainHostdevDefPtr *hostdevs,
                                   int nhostdevs);
void qemuHostdevReAttachUSBDevices(virQEMUDriverPtr driver,
                                   const char *name,
                                   virDomainHostdevDefPtr *hostdevs,
                                   int nhostdevs);
void qemuHostdevReAttachSCSIDevices(virQEMUDriverPtr driver,
                                    const char *name,
                                    virDomainHostdevDefPtr *hostdevs,
                                    int nhostdevs);
void qemuHostdevReAttachSCSIVHostDevices(virQEMUDriverPtr driver,
                                         const char *name,
                                         virDomainHostdevDefPtr *hostdevs,
                                         int nhostdevs);
void qemuHostdevReAttachMediatedDevices(virQEMUDriverPtr driver,
                                        const char *name,
                                        virDomainHostdevDefPtr *hostdevs,
                                        int nhostdevs);
void qemuHostdevReAttachDomainDevices(virQEMUDriverPtr driver,
                                      virDomainDefPtr def);
