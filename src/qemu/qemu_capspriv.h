/*
 * qemu_capspriv.h: private declarations for QEMU capabilities generation
 *
 * Copyright (C) 2015 Samsung Electronics Co. Ltd
 * Copyright (C) 2015 Pavel Fedin
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef LIBVIRT_QEMU_CAPSPRIV_H_ALLOW
# error "qemu_capspriv.h may only be included by qemu_capabilities.c or test suites"
#endif /* LIBVIRT_QEMU_CAPSPRIV_H_ALLOW */

#pragma once

virQEMUCapsPtr virQEMUCapsNewCopy(virQEMUCapsPtr qemuCaps);

virQEMUCapsPtr
virQEMUCapsNewForBinaryInternal(virArch hostArch,
                                const char *binary,
                                const char *libDir,
                                uid_t runUid,
                                gid_t runGid,
                                const char *hostCPUSignature,
                                unsigned int microcodeVersion,
                                const char *kernelVersion);

int virQEMUCapsLoadCache(virArch hostArch,
                         virQEMUCapsPtr qemuCaps,
                         const char *filename,
                         bool skipInvalidation);
char *virQEMUCapsFormatCache(virQEMUCapsPtr qemuCaps);

int
virQEMUCapsInitQMPMonitor(virQEMUCapsPtr qemuCaps,
                          qemuMonitorPtr mon);

int
virQEMUCapsInitQMPMonitorTCG(virQEMUCapsPtr qemuCaps,
                             qemuMonitorPtr mon);

void
virQEMUCapsSetArch(virQEMUCapsPtr qemuCaps,
                   virArch arch);

void
virQEMUCapsInitHostCPUModel(virQEMUCapsPtr qemuCaps,
                            virArch hostArch,
                            virDomainVirtType type);

int
virQEMUCapsInitCPUModel(virQEMUCapsPtr qemuCaps,
                        virDomainVirtType type,
                        virCPUDefPtr cpu,
                        bool migratable);

void
virQEMUCapsInitQMPBasicArch(virQEMUCapsPtr qemuCaps);

qemuMonitorCPUModelInfoPtr
virQEMUCapsGetCPUModelInfo(virQEMUCapsPtr qemuCaps,
                           virDomainVirtType type);

void
virQEMUCapsSetCPUModelInfo(virQEMUCapsPtr qemuCaps,
                           virDomainVirtType type,
                           qemuMonitorCPUModelInfoPtr modelInfo);

virCPUDataPtr
virQEMUCapsGetCPUModelX86Data(virQEMUCapsPtr qemuCaps,
                              qemuMonitorCPUModelInfoPtr model,
                              bool migratable);

virCPUDefPtr
virQEMUCapsProbeHostCPU(virArch hostArch,
                        virDomainCapsCPUModelsPtr models) G_GNUC_NO_INLINE;

void
virQEMUCapsSetGICCapabilities(virQEMUCapsPtr qemuCaps,
                              virGICCapability *capabilities,
                              size_t ncapabilities);

void
virQEMUCapsSetSEVCapabilities(virQEMUCapsPtr qemuCaps,
                              virSEVCapability *capabilities);

int
virQEMUCapsProbeCPUDefinitionsTest(virQEMUCapsPtr qemuCaps,
                                   qemuMonitorPtr mon);

void
virQEMUCapsSetMicrocodeVersion(virQEMUCapsPtr qemuCaps,
                               unsigned int microcodeVersion);

void
virQEMUCapsStripMachineAliases(virQEMUCapsPtr qemuCaps);

bool
virQEMUCapsHasMachines(virQEMUCapsPtr qemuCaps);

void
virQEMUCapsAddMachine(virQEMUCapsPtr qemuCaps,
                      virDomainVirtType virtType,
                      const char *name,
                      const char *alias,
                      const char *defaultCPU,
                      int maxCpus,
                      bool hotplugCpus,
                      bool isDefault,
                      bool numaMemSupported);
