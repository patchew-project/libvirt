/*
 * qemu_hotplug.h: QEMU device hotplug management
 *
 * Copyright (C) 2006-2007, 2009-2012 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "qemu_conf.h"
#include "qemu_domain.h"
#include "domain_conf.h"

int qemuDomainChangeEjectableMedia(virQEMUDriverPtr driver,
                                   virDomainObjPtr vm,
                                   virDomainDiskDefPtr disk,
                                   virStorageSourcePtr newsrc,
                                   bool force);

void qemuDomainDelTLSObjects(virQEMUDriverPtr driver,
                             virDomainObjPtr vm,
                             qemuDomainAsyncJob asyncJob,
                             const char *secAlias,
                             const char *tlsAlias);

int qemuDomainAddTLSObjects(virQEMUDriverPtr driver,
                            virDomainObjPtr vm,
                            qemuDomainAsyncJob asyncJob,
                            virJSONValuePtr *secProps,
                            virJSONValuePtr *tlsProps);

int qemuDomainGetTLSObjects(virQEMUCapsPtr qemuCaps,
                            qemuDomainSecretInfoPtr secinfo,
                            const char *tlsCertdir,
                            bool tlsListen,
                            bool tlsVerify,
                            const char *alias,
                            virJSONValuePtr *tlsProps,
                            virJSONValuePtr *secProps);

int qemuDomainAttachControllerDevice(virQEMUDriverPtr driver,
                                     virDomainObjPtr vm,
                                     virDomainControllerDefPtr controller);
int qemuDomainAttachDeviceDiskLive(virQEMUDriverPtr driver,
                                   virDomainObjPtr vm,
                                   virDomainDeviceDefPtr dev);
int qemuDomainAttachNetDevice(virQEMUDriverPtr driver,
                              virDomainObjPtr vm,
                              virDomainNetDefPtr net);
int qemuDomainAttachRedirdevDevice(virQEMUDriverPtr driver,
                                   virDomainObjPtr vm,
                                   virDomainRedirdevDefPtr hostdev);
int qemuDomainAttachHostDevice(virQEMUDriverPtr driver,
                               virDomainObjPtr vm,
                               virDomainHostdevDefPtr hostdev);
int qemuDomainAttachShmemDevice(virQEMUDriverPtr driver,
                                virDomainObjPtr vm,
                                virDomainShmemDefPtr shmem);
int qemuDomainAttachWatchdog(virQEMUDriverPtr driver,
                             virDomainObjPtr vm,
                             virDomainWatchdogDefPtr watchdog);
int qemuDomainFindGraphicsIndex(virDomainDefPtr def,
                                virDomainGraphicsDefPtr dev);
int qemuDomainAttachMemory(virQEMUDriverPtr driver,
                           virDomainObjPtr vm,
                           virDomainMemoryDefPtr mem);
int qemuDomainChangeGraphics(virQEMUDriverPtr driver,
                             virDomainObjPtr vm,
                             virDomainGraphicsDefPtr dev);
int qemuDomainChangeGraphicsPasswords(virQEMUDriverPtr driver,
                                      virDomainObjPtr vm,
                                      int type,
                                      virDomainGraphicsAuthDefPtr auth,
                                      const char *defaultPasswd,
                                      int asyncJob);
int qemuDomainChangeNet(virQEMUDriverPtr driver,
                        virDomainObjPtr vm,
                        virDomainDeviceDefPtr dev);
int qemuDomainChangeNetLinkState(virQEMUDriverPtr driver,
                                 virDomainObjPtr vm,
                                 virDomainNetDefPtr dev,
                                 int linkstate);

int qemuDomainAttachInputDevice(virQEMUDriverPtr driver,
                                virDomainObjPtr vm,
                                virDomainInputDefPtr input);

int qemuDomainAttachVsockDevice(virQEMUDriverPtr driver,
                                virDomainObjPtr vm,
                                virDomainVsockDefPtr vsock);

int qemuDomainAttachLease(virQEMUDriverPtr driver,
                          virDomainObjPtr vm,
                          virDomainLeaseDefPtr lease);
int qemuDomainAttachChrDevice(virQEMUDriverPtr driver,
                              virDomainObjPtr vm,
                              virDomainChrDefPtr chr);
int qemuDomainAttachRNGDevice(virQEMUDriverPtr driver,
                              virDomainObjPtr vm,
                              virDomainRNGDefPtr rng);

int qemuDomainDetachDeviceLive(virDomainObjPtr vm,
                               virDomainDeviceDefPtr match,
                               virQEMUDriverPtr driver,
                               bool async);

void qemuDomainRemoveVcpuAlias(virQEMUDriverPtr driver,
                               virDomainObjPtr vm,
                               const char *alias);

int
qemuDomainChrInsert(virDomainDefPtr vmdef,
                    virDomainChrDefPtr chr);
virDomainChrDefPtr
qemuDomainChrRemove(virDomainDefPtr vmdef,
                    virDomainChrDefPtr chr);

int qemuDomainRemoveDevice(virQEMUDriverPtr driver,
                           virDomainObjPtr vm,
                           virDomainDeviceDefPtr dev);

bool qemuDomainSignalDeviceRemoval(virDomainObjPtr vm,
                                   const char *devAlias,
                                   qemuDomainUnpluggingDeviceStatus status);

int qemuDomainSetVcpusInternal(virQEMUDriverPtr driver,
                               virDomainObjPtr vm,
                               virDomainDefPtr def,
                               virDomainDefPtr persistentDef,
                               unsigned int nvcpus,
                               bool hotpluggable);

int qemuDomainSetVcpuInternal(virQEMUDriverPtr driver,
                              virDomainObjPtr vm,
                              virDomainDefPtr def,
                              virDomainDefPtr persistentDef,
                              virBitmapPtr vcpus,
                              bool state);

unsigned long long qemuDomainGetUnplugTimeout(virDomainObjPtr vm) G_GNUC_NO_INLINE;

int qemuHotplugAttachDBusVMState(virQEMUDriverPtr driver,
                                 virDomainObjPtr vm,
                                 qemuDomainAsyncJob asyncJob);

int qemuHotplugRemoveDBusVMState(virQEMUDriverPtr driver,
                                 virDomainObjPtr vm,
                                 qemuDomainAsyncJob asyncJob);
