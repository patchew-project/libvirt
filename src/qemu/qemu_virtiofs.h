/*
 * qemu_virtiofs.h: virtiofs support
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once


char *
qemuVirtioFSCreatePidFilename(virDomainObjPtr vm,
                              const char *alias);
char *
qemuVirtioFSCreateSocketFilename(virDomainObjPtr vm,
                                 const char *alias);

int
qemuVirtioFSStart(virLogManagerPtr logManager,
                  virQEMUDriverPtr driver,
                  virDomainObjPtr vm,
                  virDomainFSDefPtr fs);
void
qemuVirtioFSStop(virQEMUDriverPtr driver,
                 virDomainObjPtr vm,
                 virDomainFSDefPtr fs);

int
qemuVirtioFSSetupCgroup(virDomainObjPtr vm,
                        virDomainFSDefPtr fs,
                        virCgroupPtr cgroup);

int
qemuVirtioFSPrepareDomain(virQEMUDriverPtr driver,
                          virDomainFSDefPtr fs);
